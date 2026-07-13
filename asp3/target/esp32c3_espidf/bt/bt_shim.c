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
 *  Bluetooth統合（Phase D-1）の周辺プリミティブ実装
 *
 *  bt.c（BTコントローラ本体）が直接呼ぶFreeRTOS API自体は
 *  bt/stub/include/freertos/*.h（wifi/esp_shim.cへの委譲）で提供する．
 *  本ファイルはそれ以外の依存（esp_timer/esp_pm/esp_ipc/esp_partition）
 *  をまとめて提供する．設計・経緯はdocs/dev/esp-idf-integration.md
 *  Phase D／docs/bt-shim.md参照．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "kernel_cfg.h"

#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_ipc.h"
#include "esp_partition.h"
#include "esp_intr_alloc.h"
#include "esp_random.h"

#include "esp_shim.h"
#include "bt/bt_cfg.h"

#define BT_LOCK()	uint32_t bt_lock_ = esp_shim_int_disable()
#define BT_UNLOCK()	esp_shim_int_restore(bt_lock_)

/*
 *  ------------------------------------------------------------------
 *  BLEベースバンド(BB)クロックの有効化（emi.c:164の真因対策）
 *  ------------------------------------------------------------------
 *  実ESP-IDF/NuttXはブート時に esp_perip_clk_init()（esp_system/port/soc/
 *  esp32c3/clk.c）で SET_PERI_REG_MASK(SYSTEM_WIFI_CLK_EN_REG,
 *  SYSTEM_WIFI_CLK_EN) を実行し，WiFi/BT共有ペリフェラルの各クロックを
 *  有効化する。ASP3のDirect Bootはこの系初期化を通らないため，
 *  SYSTEM_WIFI_CLK_EN のうちBLEベースバンド(0x60031xxx MMIO)の機能
 *  クロックビット（bit6,11,12,16,17＝0x00031840）が未設定のままとなる。
 *  その結果，esp_bt_controller_init() 内でコントローラタスクが走らせる
 *  ROM関数 r_emi_em_base_init() のBBレジスタ書込み(0x60031204+)が
 *  バス側でドロップされ，EM基底レジスタが0のままとなって
 *  「BLE assert emi.c 164」に至る。
 *
 *  2ボードJTAG差分（基準機A=NuttX同一blob vs 被験機B=ASP3）で確定：
 *  em_base_initストア停止時に，Aは0x60031204が書込み可・Bは不可，
 *  唯一の差はSYSCON WIFI_CLK_EN(0x60026014)（A=0xff87f850 / B=0xff84e030）。
 *  Bで本マスクをSETすると0x60031204が書込み可へ変わることを実機確認。
 *  詳細は docs/bt-shim.md。
 *
 *  esp_bt_controller_init() より前に呼ぶこと（コントローラタスク生成前に
 *  BBクロックを立てる）。レジスタ直叩きはbt.cのINTMTX処理と同様に
 *  ASP3のsil_*で行う（hal submoduleは編集しない方針）。
 */
#define BT_SYSCON_WIFI_CLK_EN_REG	0x60026014U	/* SYSCON_WIFI_CLK_EN_REG */
/*
 *  必要最小のBLEベースバンド機能クロックビットのみを立てる．
 *  0x00031840＝2ボード差分で確定したA(NuttX,成功)とB(ASP3,失敗)の
 *  CLK_EN差分ビット（bit6,11,12,16,17）．SYSTEM_WIFI_CLK_EN全体
 *  （0x00FB9FCF）をORするとWiFi/SDIO等の無関係クロックまで有効化され
 *  （CLK_EN=0xffffffff），NuttXの正規値0xff87f850から乖離してphy_init
 *  以降で不安定になるため，差分ビットのみに絞る．
 */
#define BT_BB_CLK_EN_MASK			0x00031840U

/*
 *  （D-2b(1)(i) 実験・REFUTED 2026-07-08）advertising(0x200a)後にBT_BB
 *  (割込みsource5,level)が~百万回/秒の割込みストームを起こしCPU飽和→
 *  ホストタスク飢餓でble_gap_adv_startが返らない．BT_BBのINTMTXクリアは
 *  効く（EIP_STATUS bit1はISR後に落ちる）がBBが即座に再アサートする．
 *  最小マスクにWIFI_BT_COMMON(0x0078078F＝WiFi/BT共有modem/FE/PHY
 *  データパスクロック)を加えても（→0x007B1FCF）ストームは止まらず
 *  adv_startも返らなかった＝単純なCOMMONクロックビット欠落説は反証．
 *  最小マスク(EM-init用・D-1 VHCI動作実績)に戻す．詳細＝docs/bt-shim.md
 *  Phase D-2b(1)．次候補: lpclk/modem別レジスタ, ISRハンドラ監査, 2板差分．
 */

void
esp_shim_bt_clock_init(void)
{
	uint32_t	v;

	v = sil_rew_mem((void *)(uintptr_t) BT_SYSCON_WIFI_CLK_EN_REG);
	sil_wrw_mem((void *)(uintptr_t) BT_SYSCON_WIFI_CLK_EN_REG,
				v | BT_BB_CLK_EN_MASK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_timer_*（BTコントローラのモデムスリープ用タイマ．本ビルドは
 *  sleep_mode=0固定のため実行時には未使用の見込みだが，リンクは必要
 *  ＝esp_shim.cのets_timer機構とは別の小さな専用実装とする）
 *  ------------------------------------------------------------------
 */
struct esp_timer {
	bool_t			used;
	bool_t			active;
	int64_t			deadline_us;
	uint64_t		period_us;	/* 0=one-shot */
	esp_timer_cb_t	callback;
	void			*arg;
};

static struct esp_timer	bt_timer_pool[BT_TIMER_NUM];

esp_err_t
esp_timer_create(const esp_timer_create_args_t *create_args,
				  esp_timer_handle_t *out_handle)
{
	uint_t	i;
	struct esp_timer	*t = NULL;

	BT_LOCK();
	for (i = 0U; i < BT_TIMER_NUM; i++) {
		if (!bt_timer_pool[i].used) {
			bt_timer_pool[i].used = true;
			t = &bt_timer_pool[i];
			break;
		}
	}
	BT_UNLOCK();

	if (t == NULL) {
		syslog(LOG_ERROR, "bt: esp_timer pool exhausted");
		return(ESP_ERR_NO_MEM);
	}
	t->active = false;
	t->callback = create_args->callback;
	t->arg = create_args->arg;
	t->period_us = 0U;
	*out_handle = (esp_timer_handle_t) t;
	return(ESP_OK);
}

esp_err_t
esp_timer_delete(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	BT_LOCK();
	t->active = false;
	t->used = false;
	BT_UNLOCK();
	return(ESP_OK);
}

static esp_err_t
timer_start(struct esp_timer *t, uint64_t timeout_us, uint64_t period_us)
{
	BT_LOCK();
	t->deadline_us = esp_shim_time_us() + (int64_t) timeout_us;
	t->period_us = period_us;
	t->active = true;
	BT_UNLOCK();
	(void) sig_sem(BT_TIMER_SEM);	/* タイマタスクへ再計算を促す */
	return(ESP_OK);
}

esp_err_t
esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us)
{
	return(timer_start((struct esp_timer *) timer, timeout_us, 0U));
}

esp_err_t
esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period)
{
	return(timer_start((struct esp_timer *) timer, period, period));
}

esp_err_t
esp_timer_stop(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	BT_LOCK();
	t->active = false;
	BT_UNLOCK();
	return(ESP_OK);
}

/*
 *  esp_timer_get_time()はesp_shim_libc.cで既に実装済み（Wi-Fi統合時に
 *  phy_init.c用として追加．BTも同じPHYを使うためそのまま共用する．
 *  ここでは重複定義しない）．
 */

/*
 *  NimBLE NPL（npl_os_freertos.cのコールアウト＝esp_timer経路）が要求する
 *  参照系API．struct esp_timerのdeadline_us/activeをそのまま返す．
 */
esp_err_t
esp_timer_get_expiry_time(esp_timer_handle_t timer, uint64_t *expiry)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	if (t == NULL || expiry == NULL) {
		return(ESP_ERR_INVALID_ARG);
	}
	if (!t->active) {
		return(ESP_ERR_INVALID_STATE);
	}
	*expiry = (uint64_t) t->deadline_us;
	return(ESP_OK);
}

bool
esp_timer_is_active(esp_timer_handle_t timer)
{
	struct esp_timer	*t = (struct esp_timer *) timer;

	return(t != NULL && t->active);
}

/*
 *  タイマタスク：期限順の線形走査（BT_TIMER_NUM=4と小さいため
 *  ソート済みリストは使わず毎回全走査で十分）
 */
void
bt_timer_task(EXINF exinf)
{
	(void) exinf;

	for (;;) {
		int64_t		now;
		int64_t		nearest = -1;
		uint_t		i;
		TMO			tmo;

		now = esp_shim_time_us();
		for (i = 0U; i < BT_TIMER_NUM; i++) {
			struct esp_timer	*t = &bt_timer_pool[i];

			if (t->used && t->active) {
				if (t->deadline_us <= now) {
					esp_timer_cb_t	cb = t->callback;
					void			*arg = t->arg;

					if (t->period_us != 0U) {
						t->deadline_us += (int64_t) t->period_us;
					}
					else {
						t->active = false;
					}
					if (cb != NULL) {
						cb(arg);
					}
				}
				else if (nearest < 0 || t->deadline_us < nearest) {
					nearest = t->deadline_us;
				}
			}
		}

		if (nearest < 0) {
			tmo = TMO_FEVR;
		}
		else {
			now = esp_shim_time_us();
			tmo = (nearest > now) ? (TMO)(nearest - now) : (TMO) 0;
		}
		(void) twai_sem(BT_TIMER_SEM, tmo);
	}
}

/*
 *  ------------------------------------------------------------------
 *  esp_pm_lock_*（電源管理．Wi-Fi同様PS_NONE相当＝no-op）
 *  ------------------------------------------------------------------
 */
struct esp_pm_lock {
	int	dummy;
};

static struct esp_pm_lock	bt_pm_lock_dummy;

esp_err_t
esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg,
				   const char *name, esp_pm_lock_handle_t *out_handle)
{
	(void) lock_type; (void) arg; (void) name;
	*out_handle = &bt_pm_lock_dummy;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_delete(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_acquire(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

esp_err_t
esp_pm_lock_release(esp_pm_lock_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_ipc_call_blocking（ESP32-C3は単一コアのため同期直接呼出し）
 *  ------------------------------------------------------------------
 */
esp_err_t
esp_ipc_call_blocking(uint32_t cpu_id, esp_ipc_func_t func, void *arg)
{
	(void) cpu_id;
	func(arg);
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_partition_*（NVS/較正データ．Wi-Fi shim同様「常に存在しない」
 *  スタブ．CONFIG_BT_CTRL_LE_LOG_STORAGE_EN未定義＝該当コードは実行時
 *  未到達の見込みだがリンクは必要）
 *  ------------------------------------------------------------------
 */
const esp_partition_t *
esp_partition_find_first(esp_partition_type_t type,
						  esp_partition_subtype_t subtype, const char *label)
{
	(void) type; (void) subtype; (void) label;
	return(NULL);
}

esp_err_t
esp_partition_erase_range(const esp_partition_t *partition,
						   uint32_t offset, uint32_t size)
{
	(void) partition; (void) offset; (void) size;
	return(ESP_FAIL);
}

esp_err_t
esp_partition_write(const esp_partition_t *partition, uint32_t dst_offset,
					 const void *src, uint32_t size)
{
	(void) partition; (void) dst_offset; (void) src; (void) size;
	return(ESP_FAIL);
}

esp_err_t
esp_partition_mmap(const esp_partition_t *partition, uint32_t offset,
				   uint32_t size, esp_partition_mmap_memory_t memory,
				   const void **out_ptr, esp_partition_mmap_handle_t *out_handle)
{
	(void) partition; (void) offset; (void) size; (void) memory;
	(void) out_ptr; (void) out_handle;
	return(ESP_ERR_NOT_FOUND);
}

esp_err_t
esp_partition_munmap(esp_partition_mmap_handle_t handle)
{
	(void) handle;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_random（bt.cが直接呼ぶ公開API名．実体はesp_shim_random）
 *  ------------------------------------------------------------------
 */
uint32_t
esp_random(void)
{
	return(esp_shim_random());
}

/*
 *  ------------------------------------------------------------------
 *  esp_intr_alloc/free/enable/disable（bt.cが直接呼ぶ標準割込み確保
 *  API．esp_wifi_adapter.cのset_intr_wrapper／esp_shim_set_isrと
 *  同じ仕組み（INTMTXルーティング＋esp_shim.cfgでDEF_INH済みの
 *  CPU割込み線）を流用する．
 *
 *  ★（D-2b再開ラウンド＝S3真因の移植．docs/s3-bt-intr-source-overwrite-
 *  fix-for-c3.md・docs/bt-c3-resume-plan.md）旧実装は「BTコントローラは
 *  単一のISRソースしか要求しない（実測：esp_intr_alloc呼出しは1箇所）」
 *  という前提で単一static handle＋固定CPU線1だったが，S3実機で同一の
 *  bt.c（hal/components/bt/controller/esp32c3/bt.c＝S3と共有）＋同系列
 *  blobがesp_intr_allocを**2回（source8=RWBLE→source5=BT_BB）**呼ぶ
 *  ことが確定した．単一handleだと2回目の登録が1回目のhandler/argを
 *  上書きし，source8の割込みがsource5用handlerへ配送され続ける
 *  （handlerはsource8のstatus/clearに触れない）＝status0のspurious
 *  ストームの正体（S3では修正で消滅を確認）．本実装はS3同様スロット
 *  配列化し，呼出し順でCPU線1,2へ分離する（線2はesp_shim.cfgの
 *  DEF_INH(2)＋esp_shim_inthdr_2が既存＝カーネルコンフィグ変更不要）．
 *  Wi-Fi未使用時のみ線1,2は空き（ESP32C3_BT+ESP32C3_WIFI同時ONは
 *  現状未対応のため衝突しない）．
 * ------------------------------------------------------------------
 */
#define BT_INTMTX_BASE_ADDR   0x600C2000U
#define BT_INTMTX_ENABLE_REG  (BT_INTMTX_BASE_ADDR + 0x104U)
#define BT_INTMTX_PRI_REG(n)  (BT_INTMTX_BASE_ADDR + 0x114U + (n) * 4U)
#define BT_INTR_CPU_LINE      1		/* スロット0の線．スロットnは線(1+n) */
#define BT_INTR_MAX_SLOT      2

struct intr_handle_data_t {
	int	source;
	int	cpu_line;	/* 0=未割当て */
};

static struct intr_handle_data_t	bt_intr_slot[BT_INTR_MAX_SLOT];
static uint32_t						bt_intr_nalloc;

/*
 *  （診断計装）esp_intr_allocの呼出し回数とsourceの時系列をRTC STORE1
 *  (0x60008054，usb-reset生存)へ記録し，esptool read-memで事後読みする．
 *  旧計装（単一スカラへの上書き記録）は「最後のsource」しか見えず
 *  呼出し回数を判別できなかった（docs/bt-c3-resume-plan.md 2.3節）．
 *    bits[31:24]=0xA1（マーカ），[23:16]=呼出し累積回数，
 *    [15:8]=1回目のsource，[7:0]=2回目のsource
 *  例：0xA1020805＝2回呼出し・source8→source5（S3実測パターン）．
 */
#define BT_INTR_TRACE_REG	0x60008054UL

esp_err_t
esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
			   intr_handle_t *ret_handle)
{
	struct intr_handle_data_t	*slot;
	uint32_t					line;
	uint32_t					trace;

	(void) flags;

	bt_intr_nalloc++;
	trace = sil_rew_mem((const uint32_t *) BT_INTR_TRACE_REG);
	if ((trace >> 24) != 0xA1U) {
		trace = 0xA1000000U;		/* 前回boot残値を破棄 */
	}
	trace = (trace & 0xFF00FFFFU)
			| ((bt_intr_nalloc <= 0xFFU ? bt_intr_nalloc : 0xFFU) << 16);
	if (bt_intr_nalloc == 1U) {
		trace = (trace & 0xFFFF00FFU) | (((uint32_t) source & 0xFFU) << 8);
	}
	else if (bt_intr_nalloc == 2U) {
		trace = (trace & 0xFFFFFF00U) | ((uint32_t) source & 0xFFU);
	}
	sil_wrw_mem((uint32_t *) BT_INTR_TRACE_REG, trace);

	if (bt_intr_nalloc == 1U) {
		/*  （D-2b(1) ISRストーム診断）shim_int_dispatchの蓄積reg
		    0xC0/0xC4を0初期化（前回boot残値の混入防止）．診断のみ・無害．  */
		sil_wrw_mem((void *) 0x600080C0UL, 0U);
		sil_wrw_mem((void *) 0x600080C4UL, 0U);
	}

	/*  呼出し順でスロット割当て（1個目→線1，2個目→線2）．3回以上は
	    想定外＝最終スロットへ上書き（traceのcountで検出できる）  */
	if (bt_intr_nalloc <= (uint32_t) BT_INTR_MAX_SLOT) {
		slot = &bt_intr_slot[bt_intr_nalloc - 1U];
	}
	else {
		slot = &bt_intr_slot[BT_INTR_MAX_SLOT - 1];
	}
	line = (uint32_t) BT_INTR_CPU_LINE + (uint32_t)(slot - bt_intr_slot);
	slot->source = source;
	slot->cpu_line = (int) line;

	/*  INTMTX：source→CPU線 のルーティング  */
	sil_wrw_mem((void *)(uintptr_t)(BT_INTMTX_BASE_ADDR + (uint32_t) source * 4U),
				line);
	/*
	 *  （D-2b(A)）BTコントローラISRをINTMTX優先度1（最低）にし，
	 *  カーネルタイマISR（CPU線16・優先度2）がプリエンプトできるように
	 *  する（経緯はdocs/bt-shim.md「Phase D-2b」(A)(iii)）．
	 */
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_PRI_REG(line), 1U);
	esp_shim_set_isr((int32_t) line, (void *) handler, arg);
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG)
				| (1UL << line));

	*ret_handle = slot;
	return(ESP_OK);
}

/*
 *  handleからCPU線を引く（per-handle操作．旧実装は固定線1決め打ちで，
 *  複数handleが実在する場合に誤った線を操作していた）．
 */
static uint32_t
bt_intr_line_of(intr_handle_t handle)
{
	struct intr_handle_data_t	*h = (struct intr_handle_data_t *) handle;

	if (h != NULL && h->cpu_line != 0) {
		return((uint32_t) h->cpu_line);
	}
	return((uint32_t) BT_INTR_CPU_LINE);
}

esp_err_t
esp_intr_free(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG)
				& ~(1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

esp_err_t
esp_intr_enable(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG)
				| (1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_INTMTX_ENABLE_REG)
				& ~(1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

/*
 *  coex_pti_v2はlibbtbb.a（bt_bb_v2.o）に実体があるため自前実装は
 *  不要（当初ROM関数だと誤認していた．libbtbbをリンクすれば解決）．
 */
