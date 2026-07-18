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
#include "target_timer.h"		/* esp32c3_systimer_read */
#ifdef TOPPERS_ESP32C3_WIFI
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP32C3_WIFI */

/*
 *  サービスコールのエラーのログ出力（sample1 の SVC_PERROR 相当）．
 *  E_CTX／E_TMOUT は本シムの «想定内»（CPUロック時のフォールバック・受信
 *  タイムアウト）なので除外し，«想定外» のエラーだけを file:line 付きで
 *  記録する．ESP32C3_BT_APIERR_TRACE=ON のときのみ有効（既定OFF＝非回帰）．
 */
#ifdef TOPPERS_ESP32C3_BT_APIERR_TRACE
/*  ★ercd を «そのまま返す»＝ er = SVC_PERROR(sig_sem(...)) で er にエラー
    コードが入り，かつ想定外エラーだけログする（挙動は不変・ログ追加のみ）．  */
ER
esp_shim_svc_perror(const char *file, int_t line, const char *expr, ER ercd)
{
	if (ercd < 0 && ercd != E_CTX && ercd != E_TMOUT && ercd != E_QOVR) {
		t_perror(LOG_ERROR, file, line, expr, ercd);
	}
	return(ercd);
}
#define SVC_PERROR(expr)	esp_shim_svc_perror(__FILE__, __LINE__, #expr, (expr))
#else
#define SVC_PERROR(expr)	(expr)
#endif

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

/*
 *  ネスト対応クリティカルセクション（FreeRTOS portENTER/EXIT_CRITICAL用）
 *
 *  esp_shim_int_disable()の退避値を「muxに格納」する方式は，同一muxを
 *  入れ子で取得する（BTコントローラのGLOBAL_INT_DISABLE系＝osiの
 *  _global_intr_disable/restoreはRW/LLDスタックが深くネストする）と，
 *  内側の取得が外側の退避値（MIE=1）をMIE=0で上書きし，最外の解放でも
 *  MIEが復元されず割込み禁止（CPUロック）のまま残る．その文脈で
 *  block-foreverのtwai_semを呼ぶとブロック不可でE_CTX→take失敗→
 *  btdm_controller_taskがexit→RTC_SW_SYS_RESET（docs/bt-shim.md
 *  「リセット点の局所化」）．
 *  ESP-IDF FreeRTOSと同じく，割込み状態はコア単位（単一コアなので大域）に
 *  ネストカウンタで退避し，最外(0→1)で退避・最外(1→0)でのみ復元する．
 *  muxは単一コアでは無意味なため参照しない．
 */
static volatile uint32_t	esp_shim_crit_nest = 0U;
static volatile uint32_t	esp_shim_crit_saved = 0U;

void
esp_shim_enter_critical(void)
{
	uint32_t state;

	/* mstatus.MIEを読みつつクリア（この時点以降はISRが入らない＝以降の
	   カウンタ/退避値更新はアトミック） */
	Asm("csrrci %0, mstatus, 8" : "=r"(state));
	if (esp_shim_crit_nest == 0U) {
		esp_shim_crit_saved = state & 8U;
	}
	esp_shim_crit_nest++;
}

void
esp_shim_exit_critical(void)
{
	if (esp_shim_crit_nest > 0U) {
		esp_shim_crit_nest--;
		if ((esp_shim_crit_nest == 0U) && (esp_shim_crit_saved != 0U)) {
			Asm("csrsi mstatus, 8");
			/*
			 *  最外解除でMIEを復帰した＝サービスコール発行可能になった．
			 *  BTクリティカルセクション内でE_CTXのため保留リングへ退避
			 *  された送信（D-2c ACL経路等）をここでDTQへflushし，待機中の
			 *  受信タスク（NimBLEホスト等）を起床させる．保留が無ければ
			 *  高速パスで即return（非回帰）．
			 *  ★D-2d bond修正：セマフォ側の保留give（E_CTXで消えた
			 *  controller の give）も同時に精算する．  */
			esp_shim_queue_flush_pending();
			esp_shim_sem_flush_pending();
			esp_shim_wakeup_flush_pending();	/* #5：タイマ起床の保留精算 */
		}
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
	return((int64_t)(esp32c3_systimer_read()
					 / ESP32C3_SYSTIMER_TICKS_PER_US));
}

uint32_t
esp_shim_random(void)
{
	/*
	 *  HW乱数生成器（RNG_DATA_REG）．無線が有効になるとRFノイズ由来の
	 *  真性乱数になる（無効時はエントロピー低）．
	 *
	 *  アドレスは SYSCON_RND_DATA_REG = DR_REG_SYSCON_BASE(0x60026000)
	 *  + 0x0B0 = 0x600260B0（esp-hal-3rdparty:
	 *  soc/esp32c3/register/soc/syscon_reg.h の SYSCON_RND_DATA_REG／
	 *  RNG_DATA_REG，apb_ctrl_reg.h の APB_CTRL_RND_DATA_REG も同値＝
	 *  SYSCONの旧名）．旧実装は0x6002607C（-0x34のオフセット違い）を
	 *  読んでおり，これは常に0を返す別レジスタだった＝WPA2 4-way
	 *  ハンドシェイクのSNonceが常時全ゼロになり，AP側がゼロnonceを
	 *  リプレイ攻撃/nonce再利用とみなして黙ってmsg1を再送し続ける
	 *  （4-wayハンドシェイクタイムアウト，reason=15）原因だった．
	 *  実機JTAG（gdbでSYSCON_RND_DATA_REGを複数回読み比較）で確認済み．
	 */
	return(sil_rew_mem((void *)0x600260B0U));	/* SYSCON_RND_DATA_REG */
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
#ifdef TOPPERS_ESP32C3_BT_NIMBLE
	, SHIM_SEM25, SHIM_SEM26, SHIM_SEM27, SHIM_SEM28
#endif
};
static bool_t shim_sem_used[ESP_SHIM_NUM_SEM];

/*  ★D-2d bond修正：E_CTX（mstatus.MIE==0＝BTクリティカルセクション/ISR文脈）
    からの sig_sem 消失を防ぐ «保留give»．キューの pend_ring と «同型»．
    controller blob は osi の _semphr_give_from_isr（→stub xSemaphoreGiveFromISR
    →esp_shim_sem_give）を MIE==0 文脈から叩くが，本ポートは sense_lock()==
    (MIE==0) のため sig_sem が E_CTX を返し give が «黙って消える»．D-2c で
    キューには pend_ring 救済を入れたがセマフォには入れ忘れていた＝これが
    暗号確立後の «2個目の暗号化ACL» が host に届かない真因（docs/bt-shim.md
    「D-2d bond診断」）．E_CTX時は保留カウントへ退避し，MIE復帰(exit_critical)
    や機会的flushで sig_sem を精算する．  */
static volatile uint32_t	shim_sem_pend[ESP_SHIM_NUM_SEM];
static volatile uint32_t	shim_sem_pend_total;
volatile uint32_t	shim_sem_ectx_total;	/* 累計E_CTX give数（診断・非static） */
volatile uint32_t	shim_sem_take_ectx_total;	/* 累計E_CTX take数（#4診断・非static） */
volatile uint32_t	shim_que_debt_conflict;		/* #9診断：token取得後に空きslot無し窓の検出数 */

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
			/*  ★#6：念のため確保時にも保留give残を清算（delete で清算済みの
			    はずだが，未清算のスロットを掴んでも亡霊give を持ち込まない）．  */
			shim_sem_pend_total -= shim_sem_pend[i];
			shim_sem_pend[i] = 0U;
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
			/*  ★#6：スロット解放時に保留give（shim_sem_pend[i]）を清算する．
			    残したままスロットが再利用されると，次の flush_pending が新利用者へ
			    «亡霊give» を注入しバイナリセマフォ化けを起こす．total から先に
			    引いてからスロットを0にする（アンダーフロー回避）．  */
			shim_sem_pend_total -= shim_sem_pend[i];
			shim_sem_pend[i] = 0U;
			break;
		}
	}
	SHIM_UNLOCK();
}

int32_t
esp_shim_sem_take(void *sem, uint32_t block_time_tick)
{
	ER	er = twai_sem((ID)(intptr_t)sem, esp_shim_tick_to_tmo(block_time_tick));

	if (er == E_CTX) {
		/*
		 *  ★#4：非タスク文脈（真のISR/CPUロック中）からの take．
		 *  ASP3 には «ISRから使えるセマフォ非ブロック取得» が存在しない
		 *  ——twai_sem も pol_sem も CHECK_TSKCTX_UNL（= sense_context()/
		 *  sense_lock() で弾かれるタスク文脈専用．kernel/semaphore.c:196,270）．
		 *  したがってISRからの取得は «失敗» を返すしかない（coex/BT の
		 *  _semphr_take_from_isr はこの経路に落ちる）．give 側と異なり «取得» は
		 *  延期不能なので保留救済もできない（保留できるのは «give» のみ）．
		 *  ★安直に pol_sem へ置換しても直らない（pol_sem も同じ CHECK）．
		 *  真の解決は blob の take-from-ISR 経路を絶つか，ISR安全な影カウンタを
		 *  持つ再設計だが，blob が実際にISRから take するかは未確認．
		 *  ここでは発生を計数し «本経路が実機で踏まれるか» を観測可能にする
		 *  （0 のままなら死経路＝無害＝再設計不要．非0 なら要再設計）．
		 */
		shim_sem_take_ectx_total++;
		return(0);
	}
	return(er == E_OK ? 1 : 0);
}

int32_t
esp_shim_sem_give(void *sem)
{
	ID	semid = (ID)(intptr_t)sem;
	ER	er = SVC_PERROR(sig_sem(semid));

	if (er == E_OK || er == E_QOVR) {
		return(1);	/* 成立（E_QOVR=既に上限＝実質signaled） */
	}
	if (er == E_CTX) {
		/*
		 *  ★D-2d bond修正：mstatus.MIE==0（BTクリティカルセクション/ISR）
		 *  文脈では sig_sem が E_CTX＝give が消える．保留カウントへ退避し，
		 *  MIE復帰(exit_critical)や機会的flush(esp_shim_sem_flush_pending)で
		 *  sig_sem を精算する（キュー pend_ring と同型）．これが無いと
		 *  controller が ISR/クリティカルから出す give（暗号後の2個目ACL
		 *  処理の起床等）が失われる（docs/bt-shim.md「D-2d bond診断」）．
		 */
		uint_t	i;

		SHIM_LOCK();
		for (i = 0U; i < ESP_SHIM_NUM_SEM; i++) {
			if (shim_sem_id[i] == semid) {
				shim_sem_pend[i]++;
				shim_sem_pend_total++;
				shim_sem_ectx_total++;
				break;
			}
		}
		SHIM_UNLOCK();
		return(1);	/* 保留＝«成立» 扱い（呼出し元は戻り値を捨てる） */
	}
	return(0);
}

/*
 *  保留セマフォgiveのflush：MIE==1（サービスコール発行可能）文脈で呼び，
 *  E_CTX退避されていた give を sig_sem で精算する．呼出し点＝
 *  esp_shim_exit_critical()の最外解除直後・アプリ定常ループ（機会的）．
 *  保留0なら即return＝非回帰．
 */
void
esp_shim_sem_flush_pending(void)
{
	uint_t	i;

	if (shim_sem_pend_total == 0U) {
		return;		/* 高速パス（ロック無し読み） */
	}
	for (i = 0U; i < ESP_SHIM_NUM_SEM; i++) {
		while (shim_sem_pend[i] > 0U) {
			SHIM_LOCK();
			if (shim_sem_pend[i] == 0U) {
				SHIM_UNLOCK();
				break;
			}
			shim_sem_pend[i]--;
			shim_sem_pend_total--;
			SHIM_UNLOCK();
			(void) sig_sem(shim_sem_id[i]);
		}
	}
}

/*
 *  ★#5：タイマ再計算の起床（sig_sem）を «critical外で必ず発火» させる救済．
 *  ble_npl_hw_enter_critical 等（MIE=0）の中から esp_timer_start/ets_timer_arm が
 *  呼ばれると，直後の sig_sem(*_TIMER_SEM) が E_CTX で消え，タイマタスクが
 *  TMO_FEVR で寝たまま新しい期限を拾わない（Fable#3）．D-2d の give 救済と同型に，
 *  E_CTX の起床要求を semID で保留し，esp_shim_exit_critical / 機会flush で精算する．
 *  真のISRからは sig_sem 自体が成立（CHECK_UNL）し E_CTX にならないため，本救済は
 *  «critical由来» のみを拾う＝漏れ無し．保留は semID の集合（重複記録しない）．
 */
#define SHIM_WAKEUP_PEND_MAX	4U
static volatile ID		shim_wakeup_pend[SHIM_WAKEUP_PEND_MAX];
static volatile uint_t	shim_wakeup_pend_n;

void
esp_shim_signal_or_pend(ID semid)
{
	uint_t	i;

	if (sig_sem(semid) != E_CTX) {
		return;		/* E_OK/E_QOVR＝成立（非critical）．他エラーもそのまま */
	}
	/*  E_CTX＝MIE=0（critical）で起床要求が消えた．semIDを保留（sig_semは冪等的
	    なので重複記録は不要）．満杯でも «最低1回は起床» するので捨ててよい．  */
	SHIM_LOCK();
	for (i = 0U; i < shim_wakeup_pend_n; i++) {
		if (shim_wakeup_pend[i] == semid) {
			SHIM_UNLOCK();
			return;
		}
	}
	if (shim_wakeup_pend_n < SHIM_WAKEUP_PEND_MAX) {
		shim_wakeup_pend[shim_wakeup_pend_n++] = semid;
	}
	SHIM_UNLOCK();
}

void
esp_shim_wakeup_flush_pending(void)
{
	if (shim_wakeup_pend_n == 0U) {
		return;		/* 高速パス（ロック無し読み） */
	}
	for (;;) {
		ID	semid;

		SHIM_LOCK();
		if (shim_wakeup_pend_n == 0U) {
			SHIM_UNLOCK();
			break;
		}
		semid = shim_wakeup_pend[--shim_wakeup_pend_n];
		SHIM_UNLOCK();
		(void) sig_sem(semid);
	}
}

/*
 *  現在のセマフォ資源数（FreeRTOS uxSemaphoreGetCount相当．NimBLE NPL用）
 */
uint32_t
esp_shim_sem_get_count(void *sem)
{
	T_RSEM		rsem;

	if (ref_sem((ID)(intptr_t)sem, &rsem) != E_OK) {
		return(0U);
	}
	return((uint32_t)rsem.semcnt);
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
#ifdef TOPPERS_ESP32C3_BT_NIMBLE
	, SHIM_MTX9, SHIM_MTX10, SHIM_MTX11, SHIM_MTX12
#endif
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
 *		キュー（DTQプール＋固定プールitem．S3 commit dd7a76d移植）
 *
 *  旧実装は送受信のたびに esp_shim_malloc/free でitemバッファを
 *  確保していた．これは
 *   (1) ISR文脈（queue_send_from_isr．WiFi MAC ISRから呼ばれる）での
 *       malloc失敗がtx-done完了通知の取りこぼしに直結し，WiFi動的TX
 *       バッファが永久未回収になって送信自己ロックを招く，
 *   (2) 持続高レート送受信でのalloc/freeチャーンがシムヒープ
 *       （192KB・first-fit）を断片化させる，
 *  という潜在欠陥だった（ESP32-S3側の実機調査JTAG_DEBUG追記30で
 *  特定．docs/s3-throughput-findings-for-c6.md参照．C3のシムは
 *  S3と同一コードパターンで同じ欠陥を持っていたため，S3の対策
 *  （固定プール化）をそのまま移植する）．
 *
 *  対策：生成時にdepth*item_sizeのプールとスロット空きスタックを
 *  1回だけ確保し，送受信ではmallocせずスロット番号のみをDTQで運ぶ．
 */
typedef struct {
	ID			dtqid;		/* 0なら未使用スロット */
	ID			semid;		/* 空きスロット数を表すカウンティングセマフォ
							 * （shim_qsem_idからdtqidと同じindexで1:1対応）．
							 * esp_shim_queue_send()のブロッキング契約
							 * （portMAX_DELAY＝満杯時は待つ）をtwai_semで
							 * 実現する（D-2c／S3 BT-4調査 §8.5/§10）． */
	uint32_t	item_size;
	uint8_t		*pool;		/* depth*item_size．生成時に1回だけ確保 */
	uint16_t	*free_stk;	/* 空きスロット番号スタック（LIFO） */
	uint32_t	depth;
	volatile uint32_t free_top;	/* 空きスロット数 */
	/*
	 *  ★E_CTX文脈（CPUロック状態＝mstatus.MIE==0）からの送信用の
	 *  「保留リング」（S3 BT-4調査 steering §13のC3移植）．本ポートの
	 *  sense_lock()はmstatus.MIE==0で真になるため（core_kernel_impl.h），
	 *  BTクリティカルセクション（esp_shim_enter_critical＝MIEクリア保持）
	 *  内では twai_sem/pol_sem/psnd_dtq/sig_sem を含む全サービスコールが
	 *  E_CTXになる．そこでこの文脈では：スロットを直接確保（カーネル
	 *  呼出し無し）→itemをコピー→スロット番号をpend_ringへ積んで成功を
	 *  返し，MIE復帰時（esp_shim_exit_criticalの最外解除，または次の
	 *  queue_send/recv冒頭）にpsnd_dtqへ流し込む（flush）．sem_debtは
	 *  「トークンを消費せずに確保したスロット数」で，解放時に sig_sem を
	 *  1回スキップして返済する（トークンは不可分＝どの解放で返済しても
	 *  会計は一致する）．
	 */
	uint16_t	*pend_ring;	/* 保留スロット番号リング（容量depth） */
	uint32_t	pend_rd;	/* リング読み出しindex（SHIM_LOCK下で更新） */
	uint32_t	pend_wr;	/* リング書き込みindex（SHIM_LOCK下で更新） */
	volatile uint32_t	pend_cnt;	/* 保留数 */
	volatile uint32_t	sem_debt;	/* 未返済トークン数 */
} SHIM_QUE;

/*  全キュー合計の保留数（exit_critical側の高速チェック用．SHIM_LOCK下で
 *  更新，読み出しはロック無し＝0/非0の判定にのみ使う）． */
static volatile uint32_t	shim_que_pend_total;

static const ID shim_dtq_id[ESP_SHIM_NUM_DTQ] = {
	SHIM_DTQ1, SHIM_DTQ2, SHIM_DTQ3, SHIM_DTQ4
#ifdef TOPPERS_ESP32C3_BT_NIMBLE
	, SHIM_DTQ5, SHIM_DTQ6, SHIM_DTQ7, SHIM_DTQ8
#endif
};
/*  shim_dtq_idと同じindexで1:1対応する「空きスロット数」セマフォ． */
static const ID shim_qsem_id[ESP_SHIM_NUM_DTQ] = {
	SHIM_QSEM1, SHIM_QSEM2, SHIM_QSEM3, SHIM_QSEM4
#ifdef TOPPERS_ESP32C3_BT_NIMBLE
	, SHIM_QSEM5, SHIM_QSEM6, SHIM_QSEM7, SHIM_QSEM8
#endif
};
static SHIM_QUE shim_que[ESP_SHIM_NUM_DTQ];

void *
esp_shim_queue_create(uint32_t len, uint32_t item_size)
{
	uint_t		i;
	uint32_t	k, depth = len;
	SHIM_QUE	*q = NULL;
	uint8_t		*pool;
	uint16_t	*stk;
	uint16_t	*pring;

	if (depth > ESP_SHIM_DTQ_CNT) {
		syslog(LOG_NOTICE, "esp_shim: queue len %u > pool depth %u",
			   (uint_t)len, (uint_t)ESP_SHIM_DTQ_CNT);
		depth = ESP_SHIM_DTQ_CNT;
	}
	/*  プール・空きスタック・保留リングを生成時に1回だけ確保（以後mallocしない）。 */
	pool = (uint8_t *) esp_shim_malloc((size_t)depth * item_size);
	stk = (uint16_t *) esp_shim_malloc((size_t)depth * sizeof(uint16_t));
	pring = (uint16_t *) esp_shim_malloc((size_t)depth * sizeof(uint16_t));
	syslog(LOG_NOTICE, "esp_shim: queue create depth=%u item=%u pool=%uB",
		   (uint_t)depth, (uint_t)item_size, (uint_t)(depth * item_size));
	if (pool == NULL || stk == NULL || pring == NULL) {
		esp_shim_free(pool);
		esp_shim_free(stk);
		esp_shim_free(pring);
		syslog(LOG_ERROR, "esp_shim: queue pool alloc failed");
		return(NULL);
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_DTQ; i++) {
		if (shim_que[i].dtqid == 0) {
			q = &shim_que[i];
			q->dtqid = shim_dtq_id[i];
			q->semid = shim_qsem_id[i];
			q->item_size = item_size;
			q->pool = pool;
			q->free_stk = stk;
			q->depth = depth;
			for (k = 0U; k < depth; k++) {
				stk[k] = (uint16_t)k;
			}
			q->free_top = depth;
			q->pend_ring = pring;
			q->pend_rd = 0U;
			q->pend_wr = 0U;
			q->pend_cnt = 0U;
			q->sem_debt = 0U;
			break;
		}
	}
	SHIM_UNLOCK();

	if (q == NULL) {
		esp_shim_free(pool);
		esp_shim_free(stk);
		esp_shim_free(pring);
		syslog(LOG_ERROR, "esp_shim: queue pool exhausted");
	} else {
		/*
		 *  空きスロット数セマフォ（q->semid）の初期値をdepthに合わせる．
		 *  スロット再利用（旧queue_deleteでdrain済み）に備えてまず0まで
		 *  読み捨ててから，depth回sig_semしてカウントを積む．
		 */
		while (pol_sem(q->semid) == E_OK) {
			;	/* 前回利用分の残トークンを捨てる */
		}
		for (k = 0U; k < depth; k++) {
			(void) sig_sem(q->semid);
		}
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
			;	/* スロットはプール管理のため個別freeしない */
		}
		while (pol_sem(q->semid) == E_OK) {
			;	/* 空きスロット数セマフォも0まで読み捨てる（再利用に備える） */
		}
		SHIM_LOCK();
		shim_que_pend_total -= q->pend_cnt;	/* 保留リング残も破棄 */
		q->pend_cnt = 0U;
		SHIM_UNLOCK();
		esp_shim_free(q->pool);
		esp_shim_free(q->free_stk);
		esp_shim_free(q->pend_ring);
		q->pool = NULL;
		q->free_stk = NULL;
		q->pend_ring = NULL;
		q->dtqid = 0;
	}
}

/*  空きスロットを1つ取得（無ければ0xFFFFFFFF）。SHIM_LOCK下で呼ぶこと。 */
static uint32_t
shim_que_slot_alloc(SHIM_QUE *q)
{
	if (q->free_top == 0U) {
		return(0xFFFFFFFFU);
	}
	q->free_top--;
	return((uint32_t)q->free_stk[q->free_top]);
}

/*  スロットを返却。SHIM_LOCK下で呼ぶこと。 */
static void
shim_que_slot_free(SHIM_QUE *q, uint32_t slot)
{
	q->free_stk[q->free_top] = (uint16_t)slot;
	q->free_top++;
}

/*
 *  スロットを1つ解放し，対応する空きスロット数セマフォへトークンを1つ
 *  返却する（esp_shim_queue_send()のブロッキング待ちを解除するため）．
 *  SHIM_LOCK外から呼ぶこと（sig_semはSHIM_LOCK内で呼ぶべきでないため）．
 *  sem_debt（トークンを消費せずに確保されたスロット数＝E_CTX保留送信分）
 *  が残っている場合は，sig_semを1回スキップして返済する．
 */
static void
shim_que_slot_free_notify(SHIM_QUE *q, uint32_t slot)
{
	bool_t	repay = false;

	SHIM_LOCK();
	shim_que_slot_free(q, slot);
	if (q->sem_debt > 0U) {
		q->sem_debt--;
		repay = true;
	}
	SHIM_UNLOCK();
	if (!repay) {
		(void) sig_sem(q->semid);
	}
}

/*
 *  トークン（q->semid）を消費せずにスロットを確保してitemをコピーする．
 *  E_CTX文脈（pol_sem不可）からの送信の共通前段：sem_debt++で会計を
 *  保存し（解放時にsig_semを1回スキップして返済），コピーまで済ませた
 *  スロット番号を返す．スロット枯渇（真の満杯）なら0xFFFFFFFF．
 */
static uint32_t
shim_que_slot_alloc_debt_copy(SHIM_QUE *q, const void *item)
{
	uint32_t	slot;

	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	if (slot != 0xFFFFFFFFU) {
		q->sem_debt++;	/* トークン未消費で確保＝解放時にsig_semを1回スキップ */
		memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	}
	SHIM_UNLOCK();
	return(slot);
}

/*  保留経路の利用実績カウンタ（初回のみsyslogに痕跡を残す＝E_CTX
 *  フォールバック発動の実機証跡）． */
static volatile uint32_t	shim_que_pend_used;

/*
 *  確保・コピー済みのスロット番号を保留リングへ公開する．CPUロック状態
 *  （mstatus.MIE==0，BTクリティカルセクション等）＝psnd_dtqすら発行
 *  できない文脈向けの最終手段で，MIE復帰後のflushでDTQへ流し込まれる．
 */
static void
shim_que_pend_push_slot(SHIM_QUE *q, uint32_t slot)
{
	SHIM_LOCK();
	q->pend_ring[q->pend_wr] = (uint16_t)slot;
	q->pend_wr = (q->pend_wr + 1U >= q->depth) ? 0U : q->pend_wr + 1U;
	q->pend_cnt++;		/* スロット数で上限が押さえられておりdepthを超えない */
	shim_que_pend_total++;
	shim_que_pend_used++;
	SHIM_UNLOCK();
	if (shim_que_pend_used == 1U) {
		syslog(LOG_NOTICE, "esp_shim: pend path engaged (dtqid=%d)",
			   (int)q->dtqid);
	}
}

/*
 *  E_CTX文脈（CPUロック状態）からの送信の実体：カーネル呼出しを一切
 *  使わずスロットを確保してitemをコピーし，スロット番号を保留リングへ
 *  積む．成功なら1，スロット枯渇（真の満杯）なら0を返す．
 */
static int32_t
shim_que_pend_push(SHIM_QUE *q, const void *item)
{
	uint32_t	slot = shim_que_slot_alloc_debt_copy(q, item);

	if (slot == 0xFFFFFFFFU) {
		return(0);	/* 真の満杯 */
	}
	shim_que_pend_push_slot(q, slot);
	return(1);
}

/*
 *  保留リングのflush：MIE==1（サービスコール発行可能な文脈）で呼び，
 *  保留中のスロット番号をpsnd_dtqでDTQへ流し込む．呼出し文脈はタスク・
 *  非タスクいずれでも良い（psnd_dtqは待ちに入らない送信）．呼出し点：
 *    - esp_shim_exit_critical()の最外解除直後
 *    - esp_shim_queue_send()/esp_shim_queue_recv()の冒頭（機会的）
 */
void
esp_shim_queue_flush_pending(void)
{
	uint_t		i;
	uint32_t	slot;
	SHIM_QUE	*q;
	ER			er;

	if (shim_que_pend_total == 0U) {
		return;		/* 高速パス（ロック無し読み） */
	}
	for (i = 0U; i < ESP_SHIM_NUM_DTQ; i++) {
		q = &shim_que[i];
		while (q->dtqid != 0 && q->pend_cnt > 0U) {
			SHIM_LOCK();
			if (q->pend_cnt == 0U) {
				SHIM_UNLOCK();
				break;
			}
			slot = (uint32_t)q->pend_ring[q->pend_rd];
			q->pend_rd = (q->pend_rd + 1U >= q->depth) ? 0U : q->pend_rd + 1U;
			q->pend_cnt--;
			shim_que_pend_total--;
			SHIM_UNLOCK();
			er = SVC_PERROR(psnd_dtq(q->dtqid, (intptr_t)slot));
			if (er != E_OK) {
				syslog(LOG_ERROR, "esp_shim: flush psnd_dtq er=%d q=%d",
					   (int)er, (int)q->dtqid);
				shim_que_slot_free_notify(q, slot);
			}
		}
	}
}

/*
 *  esp_shim_queue_send()：タスク文脈からのキュー送信．
 *
 *  ★D-2c/S3 BT-4調査 §8.5/§10/§13で確定したバグの修正：旧実装は
 *  block_time_tick（portMAX_DELAY等）を無視し，かつBTクリティカル
 *  セクション内（MIE==0＝CPUロック相当）ではtsnd_dtqがE_CTXになるため
 *  即失敗（0）を返していた．NimBLEのnpl_freertos_eventq_put（呼出し元）は
 *  この契約違反を検出できずBLE_LL_ASSERT／event->queued残置で接続後の
 *  ATT/GATT要求（ACL）を取りこぼしていた（ServicesResolved未解決）．
 *
 *  修正：空きスロット数セマフォ（twai_sem）でブロッキング契約を実現し，
 *  E_CTX文脈では pol_sem→保留リング（shim_que_pend_push）へフォール
 *  バックする（MIE復帰時のflushでDTQへ）．
 */
int32_t
esp_shim_queue_send(void *que, void *item, uint32_t block_time_tick,
					bool_t to_front)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	uint32_t	slot;
	TMO			tmo;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	esp_shim_queue_flush_pending();	/* 機会的flush（保留残の滞留防止） */
	tmo = esp_shim_tick_to_tmo(block_time_tick);
	(void) to_front;	/* 先頭送信は非対応（通常送信で代用） */
	er = twai_sem(q->semid, tmo);
	if (er == E_CTX) {
		/*
		 *  ディスパッチ保留／CPUロック状態．まずpol_sem（待たない取得）を
		 *  試し，本ポートのsense_lock()はMIE==0で真になるため（BT
		 *  クリティカルセクション内）pol_semもE_CTXになる場合は，カーネル
		 *  呼出しを一切使わない保留リングで送信を成立させる．
		 */
		er = pol_sem(q->semid);
		if (er == E_CTX) {
			return(shim_que_pend_push(q, item));
		}
	}
	if (er != E_OK) {
		return(0);	/* 真の満杯（トークン取得できず） */
	}
	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	SHIM_UNLOCK();
	if (slot == 0xFFFFFFFFU) {
		/*  ★#9 診断（Fable#7）：トークン取得後に空きスロット無し＝sem_debt 窓
		    （ISR/E_CTX 送信がトークン未消費でスロットを取り token>実空き）で
		    «到達しないはず» が到達．現状はトークンを戻して 0（失敗）を返す＝
		    block_time_tick が無限待ちなら «待つはず» の送信が失敗＝契約違反
		    （D-2c 同型）．発火を計数し実機で本窓が踏まれるか観測可能にする
		    （0 のまま＝死経路・無害／非0＝要「残tmoで再ブロック」修正）．  */
		shim_que_debt_conflict++;
		if (block_time_tick != 0U) {
			syslog(LOG_WARNING,
				   "esp_shim: #9 queue send: token acquired but no free slot "
				   "(sem_debt window); blocking send returned fail");
		}
		(void) sig_sem(q->semid);
		return(0);
	}
	memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	er = SVC_PERROR(tsnd_dtq(q->dtqid, (intptr_t)slot, tmo));
	if (er == E_CTX) {
		/*  DTQ容量＝プールdepthのため空きは必ずあり，psnd_dtq（待たない
		    送信）へフォールバックする．  */
		er = SVC_PERROR(psnd_dtq(q->dtqid, (intptr_t)slot));
	}
	if (er != E_OK) {
		shim_que_slot_free_notify(q, slot);
		return(0);
	}
	return(1);
}

int32_t
esp_shim_queue_send_from_isr(void *que, void *item)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	uint32_t	slot;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	er = pol_sem(q->semid);
	if (er == E_CTX) {
		/*
		 *  非タスク文脈（Wi-Fi ISR等）またはCPUロック状態：pol_semは
		 *  使えないので，トークン未消費でスロットを確保・コピーし
		 *  （sem_debtで会計保存），まずpsnd_dtqでの直接送信を試みる．
		 */
		slot = shim_que_slot_alloc_debt_copy(q, item);
		if (slot == 0xFFFFFFFFU) {
			return(0);	/* 真の満杯（非ブロッキング仕様は不変） */
		}
		er = SVC_PERROR(psnd_dtq(q->dtqid, (intptr_t)slot));
		if (er == E_CTX) {
			/*  CPUロック状態（MIE==0，BTクリティカルセクション等）：
			    psnd_dtqも発行できないため保留リングへ退避．  */
			shim_que_pend_push_slot(q, slot);
			return(1);
		}
		if (er != E_OK) {
			shim_que_slot_free_notify(q, slot);
			return(0);
		}
		return(1);
	}
	if (er != E_OK) {
		return(0);	/* 満杯（非ブロッキング仕様は不変） */
	}
	SHIM_LOCK();
	slot = shim_que_slot_alloc(q);
	SHIM_UNLOCK();
	if (slot == 0xFFFFFFFFU) {
		(void) sig_sem(q->semid);
		return(0);
	}
	memcpy(q->pool + (size_t)slot * q->item_size, item, q->item_size);
	if (psnd_dtq(q->dtqid, (intptr_t)slot) != E_OK) {
		shim_que_slot_free_notify(q, slot);
		return(0);
	}
	return(1);
}

int32_t
esp_shim_queue_recv(void *que, void *item, uint32_t block_time_tick)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;
	ER			er;

	if (q == NULL) {
		return(0);
	}
	esp_shim_queue_flush_pending();	/* 機会的flush（保留残の滞留防止） */
	er = trcv_dtq(q->dtqid, &data, esp_shim_tick_to_tmo(block_time_tick));
	if (er == E_CTX) {
		/*  ディスパッチ保留／CPUロック状態ではprcv_dtq（待たない受信）へ．  */
		er = prcv_dtq(q->dtqid, &data);
	}
	if (er != E_OK) {
		return(0);
	}
	memcpy(item, q->pool + (size_t)(uint32_t)data * q->item_size, q->item_size);
	shim_que_slot_free_notify(q, (uint32_t)data);
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
	return((uint32_t)rdtq.sdtqcnt + q->pend_cnt);	/* flush前の保留分も計上 */
}

/*
 *  キューを空にする（FreeRTOS xQueueReset相当．NimBLE eventq_reset用）．
 *  保留分もDTQへ出してから，格納中の全itemをprcv_dtqで取り出し，スロット
 *  をプールへ返却する．
 */
void
esp_shim_queue_reset(void *que)
{
	SHIM_QUE	*q = (SHIM_QUE *)que;
	intptr_t	data;

	if (q != NULL) {
		esp_shim_queue_flush_pending();	/* 保留分もDTQへ出してから空にする */
		while (prcv_dtq(q->dtqid, &data) == E_OK) {
			shim_que_slot_free_notify(q, (uint32_t)data);
		}
	}
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
/*  ★#8 診断（Codex#4）：プール外タスクは «単一共有» thread-sem を使う（下記
    esp_shim_thread_semphr_get）．ESP-IDF は pthread TLS で «タスク毎» に持つ設計
    ＝2つ以上のプール外タスクが同時に esp_wifi 同期 API を叩くと完了 give を
    奪い合い «沈黙の同期化け» になる．現アプリは単一タスクからしか叩かない前提．
    前提が破れた瞬間（相異なる2つ目のプール外 tid が sentinel に解決）を大声で
    検出する（halt はしない＝潜在条件で WiFi ホットパス．LOG_EMERG＋カウンタ）．  */
static ID			shim_main_thread_owner;		/* 最初のプール外 tid（0=未） */
volatile uint32_t	shim_main_thread_conflict;	/* 相異なる2つ目以降の検出数（診断・非static） */

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
		SHIM_TSK1, SHIM_TSK2, SHIM_TSK3, SHIM_TSK4,
		SHIM_TSK5, SHIM_TSK6
#ifdef TOPPERS_ESP32C3_BT_NIMBLE
		, SHIM_TSK7, SHIM_TSK8
#endif
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
	(void) dly_tsk((RELTIM)(tick * 1000U));
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
		/*  ★#8：この共有 sentinel を «相異なる2つ目のプール外 tid» が要求したら
		    «複数タスクから esp_wifi 同期 API»＝サポート外の並行使用＝同期化けの
		    危険．halt はせず LOG_EMERG＋カウンタで大声で知らせる（現構成は単一
		    タスク＝発火せず非回帰）．  */
		ID	self = 0;

		(void) get_tid(&self);
		if (shim_main_thread_owner == 0) {
			shim_main_thread_owner = self;
		}
		else if (shim_main_thread_owner != self) {
			shim_main_thread_conflict++;
			syslog(LOG_EMERG,
				   "esp_shim: #8 UNSUPPORTED: esp_wifi sync API from 2nd non-pool "
				   "task (owner tid=%d this tid=%d); shared thread-sem = sync "
				   "corruption risk (need per-tid sem for multi-task WiFi)",
				   (int_t) shim_main_thread_owner, (int_t) self);
		}
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

	/*
	 *  ★走査は SHIM_LOCK 下で行う．無ロックだと，別文脈の
	 *  esp_shim_timer_done() がノードを unlink→esp_shim_free した瞬間に，
	 *  走査中の t->next が解放済み領域参照になる（use-after-free）．
	 *  SHIM_LOCK は MIE を落とし，単一コアではタスクプリエンプトも抑止する
	 *  ため，走査中のリスト改変（挿入/削除/free）を防げる．
	 */
	SHIM_LOCK();
	for (t = shim_timer_list; t != NULL; t = t->next) {
		if (t->key == key) {
			SHIM_UNLOCK();
			return(t);
		}
	}
	SHIM_UNLOCK();

	if (!create) {
		return(NULL);
	}
	/*
	 *  確保はロック外で行う（calloc を割込み禁止下で回さない）．確保後は
	 *  ロック下で «再走査» してから挿入する：アンロック中に他文脈が同じ key
	 *  を生成し得るため，重複ノード挿入を防ぐ（旧実装はこのTOCTOUで重複
	 *  し得た）．競合で既存があれば自分の確保を捨てて既存を返す．
	 */
	t = (SHIM_TIMER *)esp_shim_calloc(1U, sizeof(SHIM_TIMER));
	if (t != NULL) {
		SHIM_TIMER	*e;

		SHIM_LOCK();
		for (e = shim_timer_list; e != NULL; e = e->next) {
			if (e->key == key) {
				SHIM_UNLOCK();
				esp_shim_free(t);
				return(e);
			}
		}
		t->key = key;
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

void
esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat)
{
	SHIM_TIMER	*t = shim_timer_find(ptimer, true);

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = esp_shim_time_us() + (int64_t)us;
		t->period_us = repeat ? us : 0U;
		SHIM_UNLOCK();
		esp_shim_signal_or_pend(SHIM_TIMER_SEM);	/* #5：起床（critical内E_CTXは保留精算） */
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
void
esp_shim_timer_task(EXINF exinf)
{
	for (;;) {
		SHIM_TIMER	*t;
		int64_t		now = esp_shim_time_us();
		int64_t		next = 0;
		void		(*fn)(void *) = NULL;
		void		*arg = NULL;

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
			else if (wait > (int64_t) TMAX_RELTIM) {
				wait = (int64_t) TMAX_RELTIM;	/* ★Low#6：>TMAX_RELTIM の busy-spin 回避 */
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
		if (cpu_intno > ESP_SHIM_MAX_DEFINH_INTNO) {
			/*  ★Low#9：esp_shim.cfg は inthdr 1〜ESP_SHIM_MAX_DEFINH_INTNO のみ
			    DEF_INH する．これ超の線は登録できても静的入口が無く、有効化
			    されると未dispatchで発火する＝将来 blob が線4+を使ったら大声で気づく．  */
			syslog(LOG_WARNING,
				   "esp_shim: set_isr intno=%d > %d: no static DEF_INH entry point",
				   (int_t) cpu_intno, ESP_SHIM_MAX_DEFINH_INTNO);
		}
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

/*
 *  （D-2b(1) ISRストーム診断）実行時フラグ．既定0=無効なので通常ビルド
 *  （WiFi/bt_smoke_hw）は完全に非回帰．appが1を書くとBLEコントローラ
 *  ISR（CPU線1）発火ごとにRTC STORE4-7へストーム率とlevel割込みの
 *  clear残存を記録し，esptool read-mem（JTAG不要）で事後読みする．
 *    STORE4 0x600080B8 = esp_shim_int_count[1]（ストーム累積回数）
 *    STORE5 0x600080BC = INTMTX CPU_INT_EIP_STATUS(0x600C2110) 入口
 *    STORE6 0x600080C0 = INTMTX CPU_INT_TYPE(0x600C2108, bit1=線1のedge/level)
 *    STORE7 0x600080C4 = INTMTX CPU_INT_EIP_STATUS 出口（blob ISR実行後）
 *  出口でbit1が残る＝ソース未clear＝再発火＝ストーム（(i)クロックゲート
 *  or (ii)シムISR-clear欠落の判別材料）．詳細＝docs/bt-shim.md Phase D-2b．
 */
volatile uint32_t esp_shim_isr_storm_probe;

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
	if (esp_shim_isr_storm_probe != 0U && intno == 2) {
		/*  （D-2b再開ラウンド）source多重登録修正（bt_shim.c）で2個目の
		    BTソース（予測=source5:BT_BB）を線2へ分離した．線2の発火数を
		    STORE2(0x60008058)へミラー＝esptool read-memで事後読み．
		    線2が実際にディスパッチされる＝INTMTXルーティング着弾の
		    機能的証拠（INTMTXレジスタ自体はusb-resetで初期化されるため
		    事後読み不可）．  */
		sil_wrw_mem((uint32_t *) 0x60008058UL, esp_shim_int_count[2]);
	}
	if (esp_shim_isr_storm_probe != 0U && intno == 1) {
		/*  （D-2b(1)(a) BBステータス計装）BT_BB(source5)割込みの原因を判定
		    する blob ISR（r_bt_bb_isr_hack）が読むBB割込みステータス
		    レジスタ 0x6001108c を，ISR実行**前**（再アサートされた生の原因）に
		    読んでRTCへ記録．どのビットが立ち続けるか＝ストームの原因．
		      STORE4(0xB8)=ストーム累積回数
		      STORE6(0xC0)=BB status 0x6001108c（ISR入口の生値）
		      STORE7(0xC4)=これまで観測した全statusのsticky OR（取りこぼし防止）
		    ※0xBC(STORE5)はROMがusb-reset時に上書きするため使用不可．  */
		/*  ストームは99.997%がBB status(0x6001108c)=0のspurious＝真の源は
		    blobの0x6001108cハンドラ外の低レベルBB線．blob ISR実行**後**に
		    INTMTX EIP_STATUS(0x600C2110)のbit1(線1)が残るか＝BT_BB線が
		    ack不能で立ちっぱなしか(=ハード/クロック要因)を判定する．
		      0xB8=総ディスパッチ数
		      0xC0=blob ISR実行後EIP_STATUSのsticky OR（bit1残存＝ack不能）
		      0xC4=入口BB status 0x6001108c のsticky OR（原因bit）  */
		uint32_t bbst = sil_rew_mem((const uint32_t *) 0x6001108CUL);
		sil_wrw_mem((uint32_t *) 0x600080B8UL, esp_shim_int_count[1]);
		sil_wrw_mem((uint32_t *) 0x600080C4UL,
					sil_rew_mem((const uint32_t *) 0x600080C4UL) | bbst);
		if (shim_isr_tbl[intno].fn != NULL) {
			shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
		}
		sil_wrw_mem((uint32_t *) 0x600080C0UL,
					sil_rew_mem((const uint32_t *) 0x600080C0UL)
					| sil_rew_mem((const uint32_t *) 0x600C2110UL));
		return;
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
#ifdef TOPPERS_ESP32C3_WIFI
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP32C3_WIFI */
	}
}
