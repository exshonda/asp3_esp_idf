/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  Wi-Fi os_adapter shimの基盤プリミティブ実装（ASP3用）
 *
 *  設計はdocs/wifi-shim.md．FreeRTOS流の動的生成要求を，ASP3の静的
 *  生成オブジェクトのプール（esp_shim.cfg）＋shim実装で提供する：
 *    - セマフォ／ミューテックス：CRE_SEM／CRE_MTXプールから割当て
 *    - キュー：CRE_DTQプール＋ヒープ確保item（正しいブロッキングと
 *      非タスク文脈送信（psnd_dtq）のためDTQを使う．itemはヒープに
 *      コピーしポインタを流す）
 *    - タスク：CRE_TSKプール（共通エントリ＋関数ポインタ渡し）
 *    - ets_timer：shim専用タイマタスク＋期限ソートリスト
 *    - ヒープ：静的配列上のfirst-fit（境界タグ・前方結合）
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <sil.h>
#include <stdarg.h>
#include <stdio.h>
#include "kernel_cfg.h"
#include "esp_shim.h"
#include "esp_shim_cfg.h"
#include "target_timer.h"		/* esp32c6_systimer_read */
#include "wifi_trace.h"			/* DIAGNOSTIC（実施26）：_task_delay計測用 */
#if defined(TOPPERS_ESP32C3_WIFI) || defined(TOPPERS_ESP32C6_WIFI)
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP32C3_WIFI || TOPPERS_ESP32C6_WIFI */

/*
 *  クリティカルセクション（mstatus.MIEの退避・復元＝ネスト対応）
 */
uint32_t
esp_shim_int_disable(void)
{
	uint32_t state;

	Asm("csrrci %0, mstatus, 8" : "=r"(state));
	return(state & 8U);
}

void
esp_shim_int_restore(uint32_t state)
{
	if (state != 0U) {
		Asm("csrsi mstatus, 8");
	}
}

#define SHIM_LOCK()		uint32_t shim_lock_ = esp_shim_int_disable()
#define SHIM_UNLOCK()	esp_shim_int_restore(shim_lock_)

/*
 *  tick（1ms）→ASP3タイムアウト（μs）変換
 */
TMO
esp_shim_tick_to_tmo(uint32_t tick)
{
	if (tick == ESP_SHIM_BLOCK_FOREVER) {
		return(TMO_FEVR);
	}
	if (tick == 0U) {
		return(TMO_POL);
	}
	if (tick > 2000000U) {		/* TMO（μs・32bit）のオーバフロー回避 */
		tick = 2000000U;
	}
	return((TMO)(tick * 1000U));
}

/*
 *  時刻・乱数
 */
int64_t
esp_shim_time_us(void)
{
	return((int64_t)(esp32c6_systimer_read()
					 / ESP32C6_SYSTIMER_TICKS_PER_US));
}

uint32_t
esp_shim_random(void)
{
	/*
	 *  HW乱数生成器（RNG_DATA_REG）．無線が有効になるとRFノイズ由来の
	 *  真性乱数になる（無効時はエントロピー低）．
	 *
	 *  C6の真のHW RNG読出しレジスタはWDEV_RND_REG＝LPPERI_RNG_DATA_REG
	 *  （DR_REG_LPPERI_BASE(0x600B2800)+0x8＝0x600B2808．esp-hal-3rdparty:
	 *  soc/esp32c6/register/soc/lpperi_reg.hおよびsoc/wdev_reg.hの
	 *  WDEV_RND_REG定義で確認．C3のSYSCON_RND_DATA_REGとは全く別の
	 *  ペリフェラル（C6はLP_PERIブロックへ移動）．C3のB-2bで
	 *  「WDEV_RND_REGの実体を正しく引く」ことがWPA2 SNonce不具合の
	 *  修正だった教訓を踏まえ，C6でも最初からwdev_reg.h同値を採用する．
	 */
	return(sil_rew_mem((void *)0x600B2808U));	/* LPPERI_RNG_DATA_REG (WDEV_RND_REG) */
}

/*
 *  ログ（blobの_log_write系・lwIPのLWIP_PLATFORM_DIAG/ASSERT等，
 *  printf系を持たない呼出し元の共通折返し先）
 */
void
esp_shim_log_write(const char *format, ...)
{
	char	buf[128];
	va_list	args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
}

/*
 *		ヒープ（静的配列上のfirst-fit・境界タグ）
 */
typedef struct heap_block {
	size_t				size;		/* ヘッダ込みサイズ（最下位bit=使用中） */
	struct heap_block	*next;		/* アドレス順の次ブロック */
} HEAP_BLOCK;

#define HB_USED			0x1U
#define HB_SIZE(b)		((b)->size & ~(size_t)HB_USED)
#define HB_IS_USED(b)	(((b)->size & HB_USED) != 0U)
#define HB_ALIGN(sz)	(((sz) + 7U) & ~(size_t)7U)

static uint64_t heap_area[ESP_SHIM_HEAP_SIZE / sizeof(uint64_t)];
static HEAP_BLOCK *heap_top;
static size_t heap_free_total;

static void
heap_initialize(void)
{
	heap_top = (HEAP_BLOCK *)heap_area;
	heap_top->size = sizeof(heap_area);
	heap_top->next = NULL;
	heap_free_total = sizeof(heap_area);
}

void *
esp_shim_malloc(size_t size)
{
	HEAP_BLOCK	*b;
	size_t		need;
	void		*ret = NULL;

	if (size == 0U) {
		size = 1U;
	}
	need = HB_ALIGN(size) + sizeof(HEAP_BLOCK);

	SHIM_LOCK();
	for (b = heap_top; b != NULL; b = b->next) {
		if (!HB_IS_USED(b) && HB_SIZE(b) >= need) {
			if (HB_SIZE(b) >= need + sizeof(HEAP_BLOCK) + 16U) {
				/* 分割 */
				HEAP_BLOCK *rest = (HEAP_BLOCK *)((char *)b + need);
				rest->size = HB_SIZE(b) - need;
				rest->next = b->next;
				b->size = need;
				b->next = rest;
			}
			b->size |= HB_USED;
			heap_free_total -= HB_SIZE(b);
			ret = (void *)(b + 1);
			break;
		}
	}
	SHIM_UNLOCK();

	if (ret == NULL) {
		syslog(LOG_ERROR, "esp_shim: malloc(%u) failed (free=%u)",
			   (uint_t)size, (uint_t)heap_free_total);
	}
	return(ret);
}

void
esp_shim_free(void *ptr)
{
	HEAP_BLOCK	*b;

	if (ptr == NULL) {
		return;
	}
	b = ((HEAP_BLOCK *)ptr) - 1;

	SHIM_LOCK();
	b->size &= ~(size_t)HB_USED;
	heap_free_total += HB_SIZE(b);
	/* 前方（アドレス順の次）との結合 */
	while (b->next != NULL && !HB_IS_USED(b->next)
		   && (char *)b + HB_SIZE(b) == (char *)b->next) {
		b->size = HB_SIZE(b) + HB_SIZE(b->next);
		b->next = b->next->next;
	}
	SHIM_UNLOCK();
}

void *
esp_shim_calloc(size_t n, size_t size)
{
	void	*p = esp_shim_malloc(n * size);

	if (p != NULL) {
		memset(p, 0, n * size);
	}
	return(p);
}

void *
esp_shim_realloc(void *ptr, size_t size)
{
	void		*np;
	HEAP_BLOCK	*b;
	size_t		old;

	if (ptr == NULL) {
		return(esp_shim_malloc(size));
	}
	if (size == 0U) {
		esp_shim_free(ptr);
		return(NULL);
	}
	b = ((HEAP_BLOCK *)ptr) - 1;
	old = HB_SIZE(b) - sizeof(HEAP_BLOCK);
	if (old >= size) {
		return(ptr);
	}
	np = esp_shim_malloc(size);
	if (np != NULL) {
		memcpy(np, ptr, old);
		esp_shim_free(ptr);
	}
	return(np);
}

size_t
esp_shim_heap_free_size(void)
{
	return(heap_free_total);
}

/*
 *		セマフォプール
 */
static const ID shim_sem_id[ESP_SHIM_NUM_SEM] = {
	SHIM_SEM1,  SHIM_SEM2,  SHIM_SEM3,  SHIM_SEM4,
	SHIM_SEM5,  SHIM_SEM6,  SHIM_SEM7,  SHIM_SEM8,
	SHIM_SEM9,  SHIM_SEM10, SHIM_SEM11, SHIM_SEM12,
	SHIM_SEM13, SHIM_SEM14, SHIM_SEM15, SHIM_SEM16,
	SHIM_SEM17, SHIM_SEM18, SHIM_SEM19, SHIM_SEM20,
	SHIM_SEM21, SHIM_SEM22, SHIM_SEM23, SHIM_SEM24
};
static bool_t shim_sem_used[ESP_SHIM_NUM_SEM];

void *
esp_shim_sem_create(uint32_t max, uint32_t init)
{
	uint_t	i;
	ID		semid = 0;

	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_SEM; i++) {
		if (!shim_sem_used[i]) {
			shim_sem_used[i] = true;
			semid = shim_sem_id[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (semid == 0) {
		syslog(LOG_ERROR, "esp_shim: sem pool exhausted");
		return(NULL);
	}
	while (pol_sem(semid) == E_OK) ;	/* 再利用時のカウントクリア */
	while (init-- > 0U) {
		(void) sig_sem(semid);
	}
	(void) max;		/* 上限は緩和（ASP3側はTMAX_MAXSEM） */
	return((void *)(intptr_t)semid);
}

void
esp_shim_sem_delete(void *sem)
{
	ID		semid = (ID)(intptr_t)sem;
	uint_t	i;

	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_SEM; i++) {
		if (shim_sem_id[i] == semid) {
			shim_sem_used[i] = false;
			break;
		}
	}
	SHIM_UNLOCK();
}

int32_t
esp_shim_sem_take(void *sem, uint32_t block_time_tick)
{
	return(twai_sem((ID)(intptr_t)sem,
					esp_shim_tick_to_tmo(block_time_tick)) == E_OK ? 1 : 0);
}

int32_t
esp_shim_sem_give(void *sem)
{
	return(sig_sem((ID)(intptr_t)sem) == E_OK ? 1 : 0);
}

/*
 *		ミューテックスプール（再帰対応ラッパ）
 */
typedef struct {
	ID			mtxid;		/* 0なら未使用スロット */
	ID			owner;		/* 再帰用：所有タスク */
	uint32_t	count;		/* 再帰カウント */
	bool_t		recursive;
} SHIM_MTX;

static const ID shim_mtx_id[ESP_SHIM_NUM_MTX] = {
	SHIM_MTX1, SHIM_MTX2, SHIM_MTX3, SHIM_MTX4,
	SHIM_MTX5, SHIM_MTX6, SHIM_MTX7, SHIM_MTX8
};
static SHIM_MTX shim_mtx[ESP_SHIM_NUM_MTX];

void *
esp_shim_mutex_create(bool_t recursive)
{
	uint_t		i;
	SHIM_MTX	*m = NULL;

	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_MTX; i++) {
		if (shim_mtx[i].mtxid == 0) {
			shim_mtx[i].mtxid = shim_mtx_id[i];
			shim_mtx[i].owner = 0;
			shim_mtx[i].count = 0U;
			shim_mtx[i].recursive = recursive;
			m = &shim_mtx[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (m == NULL) {
		syslog(LOG_ERROR, "esp_shim: mutex pool exhausted");
	}
	return((void *)m);
}

void
esp_shim_mutex_delete(void *mtx)
{
	SHIM_MTX	*m = (SHIM_MTX *)mtx;

	if (m != NULL) {
		m->mtxid = 0;
	}
}

int32_t
esp_shim_mutex_lock(void *mtx)
{
	SHIM_MTX	*m = (SHIM_MTX *)mtx;
	ID			self;

	if (m == NULL) {
		return(0);
	}
	(void) get_tid(&self);
	if (m->recursive && m->owner == self) {
		m->count++;
		return(1);
	}
	if (loc_mtx(m->mtxid) != E_OK) {
		return(0);
	}
	m->owner = self;
	m->count = 1U;
	return(1);
}

int32_t
esp_shim_mutex_unlock(void *mtx)
{
	SHIM_MTX	*m = (SHIM_MTX *)mtx;

	if (m == NULL) {
		return(0);
	}
	if (m->recursive && m->count > 1U) {
		m->count--;
		return(1);
	}
	m->owner = 0;
	m->count = 0U;
	return(unl_mtx(m->mtxid) == E_OK ? 1 : 0);
}

/*
 *		キュー（DTQプール＋ヒープ確保item）
 */
typedef struct {
	ID			dtqid;		/* 0なら未使用スロット */
	uint32_t	item_size;
} SHIM_QUE;

static const ID shim_dtq_id[ESP_SHIM_NUM_DTQ] = {
	SHIM_DTQ1, SHIM_DTQ2, SHIM_DTQ3, SHIM_DTQ4
};
static SHIM_QUE shim_que[ESP_SHIM_NUM_DTQ];

void *
esp_shim_queue_create(uint32_t len, uint32_t item_size)
{
	uint_t		i;
	SHIM_QUE	*q = NULL;

	if (len > ESP_SHIM_DTQ_CNT) {
		syslog(LOG_NOTICE, "esp_shim: queue len %u > pool depth %u",
			   (uint_t)len, (uint_t)ESP_SHIM_DTQ_CNT);
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_DTQ; i++) {
		if (shim_que[i].dtqid == 0) {
			shim_que[i].dtqid = shim_dtq_id[i];
			shim_que[i].item_size = item_size;
			q = &shim_que[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (q == NULL) {
		syslog(LOG_ERROR, "esp_shim: queue pool exhausted");
	}
	return((void *)q);
}

void
esp_shim_queue_delete(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;

	if (q != NULL) {
		while (prcv_dtq(q->dtqid, &data) == E_OK) {
			esp_shim_free((void *)data);
		}
		q->dtqid = 0;
	}
}

int32_t
esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick,
					bool_t to_front)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	void		*buf;

	if (q == NULL) {
		return(0);
	}
	buf = esp_shim_malloc(q->item_size);
	if (buf == NULL) {
		return(0);
	}
	memcpy(buf, item, q->item_size);
	(void) to_front;	/* 先頭送信は非対応（通常送信で代用） */
	if (tsnd_dtq(q->dtqid, (intptr_t)buf,
				 esp_shim_tick_to_tmo(block_time_tick)) != E_OK) {
		esp_shim_free(buf);
		return(0);
	}
	return(1);
}

int32_t
esp_shim_queue_send_from_isr(void *que, void *item)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	void		*buf;

	if (q == NULL) {
		return(0);
	}
	buf = esp_shim_malloc(q->item_size);
	if (buf == NULL) {
		return(0);
	}
	memcpy(buf, item, q->item_size);
	if (psnd_dtq(q->dtqid, (intptr_t)buf) != E_OK) {
		esp_shim_free(buf);
		return(0);
	}
	return(1);
}

int32_t
esp_shim_queue_recv(void *que, void *item, uint32_t block_time_tick)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;

	if (q == NULL) {
		return(0);
	}
	if (trcv_dtq(q->dtqid, &data,
				 esp_shim_tick_to_tmo(block_time_tick)) != E_OK) {
		return(0);
	}
	memcpy(item, (void *)data, q->item_size);
	esp_shim_free((void *)data);
	return(1);
}

uint32_t
esp_shim_queue_msg_waiting(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	T_RDTQ		rdtq;

	if (q == NULL || ref_dtq(q->dtqid, &rdtq) != E_OK) {
		return(0U);
	}
	return((uint32_t)rdtq.sdtqcnt);
}

/*
 *		タスクプール
 */
typedef struct {
	ID			tskid;
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	void		*thread_sem;	/* _wifi_thread_semphr_get用（遅延生成） */
} SHIM_TSK;

static SHIM_TSK shim_tsk[ESP_SHIM_NUM_TSK];
static void *shim_main_thread_sem;	/* プール外タスク用 */

/*
 *  共通エントリ（esp_shim.cfgのCRE_TSKから呼ばれる．exinf=スロット番号）
 */
void
esp_shim_task_entry(EXINF exinf)
{
	SHIM_TSK	*t = &shim_tsk[(uint_t)exinf];

	t->fn(t->arg);
	/*
	 *  FreeRTOSのタスクはvTaskDelete(NULL)で終わるのが作法だが，
	 *  関数リターンで終わった場合もスロットを解放する
	 */
	SHIM_LOCK();
	t->used = false;
	SHIM_UNLOCK();
	ext_tsk();
}

int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	static const ID tskid_tbl[ESP_SHIM_NUM_TSK] = {
		SHIM_TSK1, SHIM_TSK2, SHIM_TSK3, SHIM_TSK4, SHIM_TSK5, SHIM_TSK6
	};
	uint_t		i;
	SHIM_TSK	*t = NULL;

	if (stack_size > ESP_SHIM_TSK_STKSZ) {
		syslog(LOG_NOTICE, "esp_shim: task '%s' stack %u > pool %u",
			   name, (uint_t)stack_size, (uint_t)ESP_SHIM_TSK_STKSZ);
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (!shim_tsk[i].used) {
			shim_tsk[i].used = true;
			shim_tsk[i].tskid = tskid_tbl[i];
			shim_tsk[i].fn = entry;
			shim_tsk[i].arg = param;
			t = &shim_tsk[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (t == NULL) {
		syslog(LOG_ERROR, "esp_shim: task pool exhausted ('%s')", name);
		return(0);
	}
	syslog(LOG_NOTICE, "esp_shim: task '%s' -> tskid %d (prio %u)",
		   name, (int_t)t->tskid, (uint_t)freertos_prio);
	(void) freertos_prio;	/* 優先度は一律（ESP_SHIM_WIFI_TASK_PRI） */
	if (act_tsk(t->tskid) != E_OK) {
		t->used = false;
		return(0);
	}
	if (task_handle != NULL) {
		*task_handle = (void *)t;
	}
	return(1);
}

void
esp_shim_task_delete(void *task_handle)
{
	SHIM_TSK	*t = (SHIM_TSK *)task_handle;
	ID			self;

	(void) get_tid(&self);
	if (t == NULL || t->tskid == self) {
		/* 自タスクの終了 */
		SHIM_LOCK();
		{
			uint_t	i;
			for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
				if (shim_tsk[i].tskid == self) {
					shim_tsk[i].used = false;
				}
			}
		}
		SHIM_UNLOCK();
		ext_tsk();
		/* ここには戻らない */
	}
	(void) ter_tsk(t->tskid);
	SHIM_LOCK();
	t->used = false;
	SHIM_UNLOCK();
}

void
esp_shim_task_delay(uint32_t tick)
{
	/*
	 *  DIAGNOSTIC（実施26／タイミング感度調査）：blobが要求した
	 *  tick引数と，実際にdly_tskで経過した実時間（us）を記録する．
	 *  NuttX側は`_task_ms_to_tick`＝`MSEC2TICK(ms)`（10ms/tick）・
	 *  `_task_delay`＝`TICK2USEC(tick)`のため，blobが
	 *  `_task_ms_to_tick`を経由せず直接`_task_delay(N)`を
	 *  「N＝ms」のつもりで呼ぶ箇所があると，NuttXは実時間で
	 *  約10倍待つことになる（ASP3は`tick＝1ms`で正確に変換）．
	 *  実際にどちらの呼び方をしているか，実測して確認する．
	 */
	uint32_t	t0 = (uint32_t)esp_shim_time_us();
	uint32_t	t1;

	(void) dly_tsk((RELTIM)(tick * 1000U));
	t1 = (uint32_t)esp_shim_time_us();
	wifi_taskdelay_capture(tick, t0, t1 - t0);
}

void *
esp_shim_task_get_current(void)
{
	ID		self;
	uint_t	i;

	(void) get_tid(&self);
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (shim_tsk[i].used && shim_tsk[i].tskid == self) {
			return((void *)&shim_tsk[i]);
		}
	}
	return((void *)&shim_main_thread_sem);	/* プール外タスクの代表 */
}

void
esp_shim_task_yield(void)
{
	(void) rot_rdq(TPRI_SELF);
}

/*
 *  スレッド毎セマフォ（_wifi_thread_semphr_get）
 */
void *
esp_shim_thread_semphr_get(void)
{
	void	*cur = esp_shim_task_get_current();

	if (cur == (void *)&shim_main_thread_sem) {
		if (shim_main_thread_sem == NULL) {
			shim_main_thread_sem = esp_shim_sem_create(1U, 0U);
		}
		return(shim_main_thread_sem);
	}
	else {
		SHIM_TSK	*t = (SHIM_TSK *)cur;
		if (t->thread_sem == NULL) {
			t->thread_sem = esp_shim_sem_create(1U, 0U);
		}
		return(t->thread_sem);
	}
}

/*
 *		ets_timer（タイマタスク＋期限ソートリスト）
 */
typedef struct shim_timer {
	struct shim_timer	*next;
	void				*key;			/* blob側のETSTimer* */
	void				(*fn)(void *);
	void				*arg;
	int64_t				deadline_us;	/* 0なら停止中 */
	uint32_t			period_us;		/* 0ならワンショット */
} SHIM_TIMER;

static SHIM_TIMER *shim_timer_list;		/* 全タイマ（生成順） */

static SHIM_TIMER *
shim_timer_find(void *key, bool_t create)
{
	SHIM_TIMER	*t;

	for (t = shim_timer_list; t != NULL; t = t->next) {
		if (t->key == key) {
			return(t);
		}
	}
	if (!create) {
		return(NULL);
	}
	t = (SHIM_TIMER *)esp_shim_calloc(1U, sizeof(SHIM_TIMER));
	if (t != NULL) {
		t->key = key;
		SHIM_LOCK();
		t->next = shim_timer_list;
		shim_timer_list = t;
		SHIM_UNLOCK();
	}
	return(t);
}

void
esp_shim_timer_setfn(void *ptimer, void (*pfunc)(void *), void *parg)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, true);

	if (t != NULL) {
		SHIM_LOCK();
		t->fn = pfunc;
		t->arg = parg;
		t->deadline_us = 0;
		SHIM_UNLOCK();
	}
}

/*
 *  DIAGNOSTIC（実施50，一時的）：esp_shim_timer_arm_usの呼出し頻度・
 *  要求us値を軽量カウンタで観測する（syslogバースト・ロス回避のため
 *  実施20と同じ「カウントのみ・まとめてダンプ」方式）．
 */
volatile uint32_t	esp_shim_timer_arm_count;
volatile uint32_t	esp_shim_timer_arm_us_min = 0xffffffffU;
volatile uint32_t	esp_shim_timer_arm_us_last;

void
esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, true);

	esp_shim_timer_arm_count++;
	esp_shim_timer_arm_us_last = us;
	if (us < esp_shim_timer_arm_us_min) {
		esp_shim_timer_arm_us_min = us;
	}

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = esp_shim_time_us() + (int64_t)us;
		t->period_us = repeat ? us : 0U;
		SHIM_UNLOCK();
		(void) sig_sem(SHIM_TIMER_SEM);		/* タイマタスクの再計算 */
	}
}

void
esp_shim_timer_disarm(void *ptimer)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, false);

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = 0;
		SHIM_UNLOCK();
	}
}

void
esp_shim_timer_done(void *ptimer)
{
	SHIM_TIMER	*t;
	SHIM_TIMER	**pp;

	SHIM_LOCK();
	for (pp = &shim_timer_list; *pp != NULL; pp = &(*pp)->next) {
		if ((*pp)->key == ptimer) {
			t = *pp;
			*pp = t->next;
			SHIM_UNLOCK();
			esp_shim_free(t);
			return;
		}
	}
	SHIM_UNLOCK();
}

/*
 *  タイマタスク本体（esp_shim.cfgのCRE_TSKで生成・起動）
 */
volatile uint32_t	esp_shim_timer_task_loops;
volatile uint32_t	esp_shim_timer_task_fn_calls;
volatile uint32_t	esp_shim_timer_task_wait_min = 0xffffffffU;
volatile uint32_t	esp_shim_timer_task_wait_last;

void
esp_shim_timer_task(EXINF exinf)
{
	for (;;) {
		SHIM_TIMER	*t;
		int64_t		now = esp_shim_time_us();
		int64_t		next = 0;
		void		(*fn)(void *) = NULL;
		void		*arg = NULL;

		esp_shim_timer_task_loops++;

		/*
		 *  期限到来タイマを1つ選ぶ（コールバックはロック外で実行）
		 */
		SHIM_LOCK();
		for (t = shim_timer_list; t != NULL; t = t->next) {
			if (t->deadline_us == 0) {
				continue;
			}
			if (t->deadline_us <= now) {
				fn = t->fn;
				arg = t->arg;
				if (t->period_us != 0U) {
					t->deadline_us = now + (int64_t)t->period_us;
				}
				else {
					t->deadline_us = 0;
				}
				break;
			}
			if (next == 0 || t->deadline_us < next) {
				next = t->deadline_us;
			}
		}
		SHIM_UNLOCK();

		if (fn != NULL) {
			esp_shim_timer_task_fn_calls++;
			fn(arg);
			continue;			/* 他の期限到来タイマを続けて処理 */
		}

		if (next == 0) {
			(void) twai_sem(SHIM_TIMER_SEM, TMO_FEVR);
		}
		else {
			int64_t wait = next - now;
			if (wait < 1000) {
				wait = 1000;
			}
			esp_shim_timer_task_wait_last = (uint32_t)wait;
			if ((uint32_t)wait < esp_shim_timer_task_wait_min) {
				esp_shim_timer_task_wait_min = (uint32_t)wait;
			}
			(void) twai_sem(SHIM_TIMER_SEM, (TMO)wait);
		}
	}
}

/*
 *		Wi-Fi割込みディスパッチ
 *
 *  blobは_set_intr（ソース→CPU割込み線のルーティング）と_set_isr
 *  （線番号へのハンドラ登録）を要求する．blobが指定する線番号を
 *  そのまま尊重し（1〜ESP_SHIM_MAX_WIFI_INTNO），cfgで静的に
 *  DEF_INHした共通入口から関数ポインタ表経由で呼び出す．
 */
static struct {
	void	(*fn)(void *);
	void	*arg;
} shim_isr_tbl[ESP_SHIM_MAX_WIFI_INTNO + 1];

void
esp_shim_set_isr(int32_t cpu_intno, void *handler, void *arg)
{
	syslog(LOG_NOTICE, "esp_shim: set_isr intno=%d handler=%p",
		   (int_t)cpu_intno, handler);
	if (cpu_intno >= 1 && cpu_intno <= ESP_SHIM_MAX_WIFI_INTNO) {
		SHIM_LOCK();
		shim_isr_tbl[cpu_intno].fn = (void (*)(void *))handler;
		shim_isr_tbl[cpu_intno].arg = arg;
		SHIM_UNLOCK();
	}
	else {
		syslog(LOG_ERROR, "esp_shim: set_isr intno %d out of range",
			   (int_t)cpu_intno);
	}
}

volatile uint32_t esp_shim_int_count[ESP_SHIM_MAX_WIFI_INTNO + 1];

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
	/*
	 *  DIAGNOSTIC（実施59，一時的）：MAC割込み線（intno==1）が上がった
	 *  瞬間に，blobのMAC ISRが読み出す前のMAC割込みイベント／ステータス
	 *  レジスタ（0x600a4c48＝hal_mac_interrupt_get_eventが読む先）を採取
	 *  し，どのビットで140/秒の割込みが上がっているのかを特定する．
	 *  RTC固定番地（0x500000B0〜）にOR蓄積・最新値・非零回数・総数を残す
	 *  （Direct BootはRTC RAMをゼロクリアしないため，JTAGでゼロクリア後に
	 *  フリーランさせて差分／蓄積を読む）．あわせてRX制御（0x600a4080）・
	 *  RX最終ディスクリプタ（0x600a408c）も最新値を残し，RX DMAが
	 *  ディスクリプタを進めているかを見る．
	 */
	if (intno == 1) {
		volatile uint32_t *rtc = (volatile uint32_t *)0x500000B0U;
		uint32_t ev = *(volatile uint32_t *)0x600A4C48U;	/* MAC int event */
		rtc[0] |= ev;						/* [B0] OR蓄積 */
		rtc[1]  = ev;						/* [B4] 最新値 */
		if (ev != 0U) {
			rtc[2]++;						/* [B8] 非零回数 */
		}
		rtc[3]++;						/* [BC] intno==1総数 */
		rtc[4]  = *(volatile uint32_t *)0x600A4080U;	/* [C0] RX制御 */
		rtc[5]  = *(volatile uint32_t *)0x600A408CU;	/* [C4] RX最終dscr */
		rtc[6] |= *(volatile uint32_t *)0x600A408CU;	/* [C8] RX最終dscr OR */
	}
	if (shim_isr_tbl[intno].fn != NULL) {
		shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
	}
}

/*
 *  cfg（esp_shim.cfg）でDEF_INHする入口（blobが使う線の分だけ用意）
 */
void esp_shim_inthdr_1(void) { shim_int_dispatch(1); }
void esp_shim_inthdr_2(void) { shim_int_dispatch(2); }
void esp_shim_inthdr_3(void) { shim_int_dispatch(3); }

/*
 *		初期化
 */
void
esp_shim_initialize(void)
{
	static bool_t initialized = false;

	if (!initialized) {
		initialized = true;
		heap_initialize();
		(void) act_tsk(SHIM_TIMER_TSK);

		/*
		 *  PSA Crypto初期化．
		 *
		 *  esp_supplicant/crypto_mbedtls.cのhmac_vector()（PTK/MIC
		 *  導出のHMAC-SHA1等で使用）はPSA Crypto API（psa_import_key
		 *  /psa_mac_sign_setup等）を直接呼ぶ．本来はESP-IDF起動シーケ
		 *  ンス（esp_system_startup.cのSECONDARY初期化，優先度104＝
		 *  mbedtls/port/esp_psa_crypto_init.cのESP_SYSTEM_INIT_FN経由）
		 *  でpsa_crypto_init()が自動的に呼ばれるが，本ポートはDirect
		 *  Boot（ESP-IDF起動シーケンス非経由）のためこの初期化が走ら
		 *  ない．未初期化のままPSA API群を呼ぶと全て失敗し（PBKDF2は
		 *  レガシーmbedtls_md経路のため無関係で正常動作するが，PTK
		 *  導出のsha1_prf→hmac_sha1_vector→hmac_vectorはPSA経由のため
		 *  全滅），呼び出し元（sha1_prf等）は戻り値未チェックのため
		 *  ptk->kck/kek/tkに未初期化のスタック内容（ポインタ値等）が
		 *  そのまま書き込まれる．結果，STAが送るmsg2のMICが常に不正
		 *  となりAPがmsg1を再送し続ける（4-wayハンドシェイクタイム
		 *  アウト，reason=15）．実機JTAGでptk->kck/kek/tkの中身が
		 *  ポインタらしき値（sm->snonceやsrc_addr等のアドレス）である
		 *  ことを確認して特定．
		 *
		 *  WiFi初期化前（esp_wifi_init呼び出し前）に一度だけ呼ぶ．
		 *
		 *  Bluetooth単体ビルド（ESP32C3_WIFI=OFF）はmbedtls/PSA Cryptoを
		 *  リンクしないため，この初期化自体が不要（WPA2固有の問題）．
		 */
#if defined(TOPPERS_ESP32C3_WIFI) || defined(TOPPERS_ESP32C6_WIFI)
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP32C3_WIFI || TOPPERS_ESP32C6_WIFI */
	}
}
