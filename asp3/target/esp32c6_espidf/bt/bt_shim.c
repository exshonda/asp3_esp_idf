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
 *  Bluetooth統合（ESP32-C6/C5世代コントローラ．Phase D-1／BLE実施01）の
 *  周辺プリミティブ実装
 *
 *  C6/C5のbt.c本体（controller/esp32c6/bt.c）は3つの関数テーブル
 *  （osi_coex_funcs_t／ext_funcs_t／npl_funcs_t）をblobへ登録する設計だが，
 *  bt.c自身がosi_coex_funcs_t（coex無効時は全no-op）とnpl_funcs_t
 *  （npl_os_freertos.cが自前提供）を埋めるため，ASP3が実装する必要が
 *  あるのはext_funcs_tが指す下位プリミティブ（platform/os.hのesp_os_*
 *  ＝task/intr）と，bt.c以外のポーティング層（esp_timer/esp_pm/
 *  esp_ipc/esp_partition/esp_random）だけである（詳細は
 *  docs/ble-c5c6.md「BLE実施01」）．
 *
 *  設計はC3のasp3/target/esp32c3_espidf/bt/bt_shim.cを土台にする：
 *    - esp_timer_*／esp_pm_lock_*／esp_ipc_call_blocking／
 *      esp_partition_*：C3と同一設計（チップ非依存ロジック）をそのまま
 *      移植．
 *    - 割込み：C6はソースルーティング（INTMTX）とCPU割込み線制御
 *      （PLIC_MX）が別レジスタブロックに分離している
 *      （wifi/esp_wifi_adapter.cのset_intr_wrapperと同じレジスタ配置）．
 *      S3由来の教訓（docs/s3-bt-intr-source-overwrite-fix-for-c3.md）
 *      を踏まえ，最初からスロット配列化し多重登録に対応する．
 *    - BBクロック：C3は手動SYSCON_WIFI_CLK_EN_REGビットポークだったが，
 *      C6はbt.c自身がmodem_clock_module_enable(PERIPH_BT_MODULE)を呼ぶ
 *      設計（WiFiと同じmodem_clockサブシステム経由）．ASP3が追加で
 *      要るのは実施91のICGアンロック（esp_shim_modem_icg_init，
 *      wifi/esp_shim.cへ移設済み）をBTパスでも呼ぶことだけ．
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
#include "platform/os.h"		/* esp_os_task_handle_t／esp_os_task_function_t */

#include "esp_shim.h"
#include "bt/bt_cfg.h"

#define BT_LOCK()	uint32_t bt_lock_ = esp_shim_int_disable()
#define BT_UNLOCK()	esp_shim_int_restore(bt_lock_)

/*
 *  ------------------------------------------------------------------
 *  BLEベースバンド/modem クロックの下準備（実施91のICGアンロックを
 *  BTパスにも適用）
 *  ------------------------------------------------------------------
 *  esp_shim_modem_icg_init()の実体はwifi/esp_shim.c（ESP32C6_WIFI／
 *  ESP32C6_BTの両方でリンクされる共有ファイル）にある．PMU
 *  HP_ACTIVE ICG_MODEM_REGがPOR既定code=0のままだとregi2cマスタ/
 *  modem APBクロックが永久ゲートされ，cold boot時にPHY初期化が
 *  ハングする（実施91．docs/wifi-shim-c6.md）．bt.c自身が呼ぶ
 *  modem_clock_module_enable(PERIPH_BT_MODULE)より前，
 *  esp_bt_controller_init()より前に本関数をアプリから呼ぶこと
 *  （C3のesp_shim_bt_clock_init()と同じ呼出し規約）．
 */
extern void esp_shim_modem_icg_init(void);

void
esp_shim_bt_clock_init(void)
{
	esp_shim_modem_icg_init();
}

/*
 *  ------------------------------------------------------------------
 *  npl_os_*→npl_freertos_* 互換シム
 *  ------------------------------------------------------------------
 *  controller/esp32c6/bt.cは npl_os_funcs_init/get/deinit・
 *  npl_os_mempool_init/deinit・npl_os_set_controller_npl_info を呼ぶ
 *  （実機リンクで確認）が，本hal（esp-hal-3rdparty）submoduleの
 *  porting/npl/freertos/src/npl_os_freertos.cが実際に定義している
 *  シンボル名は npl_freertos_* である（nimble_port_freertos.h参照）．
 *  ~/tools/esp-idf-v6.1/components/bt/controller/esp32c6/bt.c（正本）
 *  を確認したところ，そちらは npl_freertos_* を直接呼んでおり，本hal
 *  submoduleのbt.cだけが npl_os_* 名を期待する——nimble_port_os.h
 *  （本ファイル冒頭のnimble_port_os.h互換シムと同じコメント参照）と
 *  同種の上流ドリフトである．hal/submoduleは編集しないため，
 *  target側で1:1の橋渡しを行う（詳細はdocs/ble-c5c6.md「BLE実施01」）．
 */
#include "nimble/nimble_port_freertos.h"

void
npl_os_funcs_init(void)
{
	npl_freertos_funcs_init();
}

void
npl_os_funcs_deinit(void)
{
	npl_freertos_funcs_deinit();
}

struct npl_funcs_t *
npl_os_funcs_get(void)
{
	return(npl_freertos_funcs_get());
}

int
npl_os_mempool_init(void)
{
	return(npl_freertos_mempool_init());
}

void
npl_os_mempool_deinit(void)
{
	extern void npl_freertos_mempool_deinit(void);

	npl_freertos_mempool_deinit();
}

int
npl_os_set_controller_npl_info(ble_npl_count_info_t *ctrl_npl_info)
{
	return(npl_freertos_set_controller_npl_info(ctrl_npl_info));
}

/*
 *  ------------------------------------------------------------------
 *  esp_os_create_task_pinned_to_core／esp_os_task_delete
 *  （platform/os.h．bt.cのtask_create_wrapper/task_delete_wrapperが
 *  ext_funcs_t._task_create/_task_delete経由で直接呼ぶ）
 *  ------------------------------------------------------------------
 *  実体はwifi/esp_shim.cの既存タスクプール（esp_shim_task_create/
 *  delete）へ委譲する．戻り値規約の変換に注意：esp_shim_task_createは
 *  「1=成功／0=失敗」（int32_t）だが，esp_os_create_task_pinned_to_core
 *  はESP-IDF/NuttX共通の「0=成功／負値=失敗」（esp_err_t風）を返す
 *  規約（NuttX実装 hal/nuttx/src/platform/os.c参照）．
 */
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									 uint32_t stack_size, void *param,
									 uint32_t freertos_prio, void **task_handle);
extern void esp_shim_task_delete(void *task_handle);

int
esp_os_create_task_pinned_to_core(esp_os_task_function_t task_func,
								   const char *name,
								   uint32_t stack_depth,
								   void *arg,
								   int priority,
								   esp_os_task_handle_t *handle,
								   int core_id)
{
	void	*h = NULL;
	int32_t	rc;

	(void) core_id;		/* C6/C5はSOC_CPU_CORES_NUM=1（シングルコア） */
	rc = esp_shim_task_create((void (*)(void *)) task_func, name,
							  stack_depth, arg, (uint32_t) priority, &h);
	if (rc == 0) {
		return(-1);
	}
	if (handle != NULL) {
		*handle = (esp_os_task_handle_t) h;
	}
	return(0);
}

void
esp_os_task_delete(esp_os_task_handle_t handle)
{
	esp_shim_task_delete((void *) handle);
}

/*
 *  ------------------------------------------------------------------
 *  esp_os_intr_alloc／esp_os_intr_free（platform/os.h．bt.cの
 *  esp_intr_alloc_wrapper/esp_intr_free_wrapperがext_funcs_t経由で
 *  直接呼ぶ）
 *  ------------------------------------------------------------------
 *  ESP32-C6はソースルーティング（INTMTX）とCPU割込み線制御（PLIC_MX）
 *  が別レジスタブロックに分離している（wifi/esp_wifi_adapter.cの
 *  set_intr_wrapperと同一のレジスタ配置．docs/wifi-shim-c6.md）．
 *
 *  ★（S3由来の教訓を最初から適用．docs/s3-bt-intr-source-overwrite-
 *  fix-for-c3.md・docs/bt-shim.md）BTコントローラがesp_os_intr_allocを
 *  複数回呼ぶ可能性を排除できないため，「1回しか呼ばれない」という
 *  未検証の前提を置かず，最初からスロット配列化し呼出し順でCPU線を
 *  分離する（C3のbt_shim.cと同じ設計）．
 */
#define BT_INTMTX_BASE_ADDR   0x60010000U   /* ソースルーティング（C6） */
#define BT_PLICMX_BASE_ADDR   0x20001000U   /* CPU割込み線制御（C6） */
#define BT_PLICMX_ENABLE_REG  (BT_PLICMX_BASE_ADDR + 0x000U)
#define BT_PLICMX_TYPE_REG    (BT_PLICMX_BASE_ADDR + 0x004U)
#define BT_PLICMX_PRI_REG(n)  (BT_PLICMX_BASE_ADDR + 0x010U + (n) * 4U)
#define BT_INTR_CPU_LINE      1		/* スロット0の線．スロットnは線(1+n) */
#define BT_INTR_MAX_SLOT      2

struct intr_handle_data_t {
	int	source;
	int	cpu_line;	/* 0=未割当て */
};

static struct intr_handle_data_t	bt_intr_slot[BT_INTR_MAX_SLOT];
static uint32_t						bt_intr_nalloc;

/*
 *  （診断計装）esp_os_intr_allocの呼出し回数とsourceの時系列を記録する．
 *  C3のbt_shim.cと同じ形式（bits[31:24]=マーカ0xA1，[23:16]=累積回数，
 *  [15:8]=1回目のsource，[7:0]=2回目のsource）だが，アドレスはC3固有の
 *  0x60008054（SYSTEM/RTC_CNTL域）をそのまま流用したのが誤りだった
 *  （実機実測：0x07ffffffという無関係な値が読め，C6ではこの番地が
 *  RTC STORE系ではない）．C6のusb-reset生存レジスタはLP_AONブロック
 *  （0x600B1000，docs/wifi-shim-c6.md実測で確認済み）．STORE1
 *  （+0x04）は他の調査で「noise・タイミング依存」と記録済みのため，
 *  未使用のSTORE7相当（+0x1C）を使う．
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

esp_err_t
esp_os_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
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

	/*  呼出し順でスロット割当て（1個目→線1，2個目→線2）  */
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
	/*  PLIC_MX：優先度2固定・LEVEL型（WiFi MAC割込みと同じ流儀）  */
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_PRI_REG(line), 2U);
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_TYPE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_TYPE_REG)
				& ~(1UL << line));
	esp_shim_set_isr((int32_t) line, (void *) handler, arg);
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG)
				| (1UL << line));

	*ret_handle = (intr_handle_t) slot;
	return(ESP_OK);
}

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
esp_os_intr_free(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG)
				& ~(1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_timer_*（npl_os_freertos.cのcallout実装．BLE_NPL_USE_ESP_TIMER=1
 *  で選択．本ビルドはCONFIG_BT_LE_SLEEP_ENABLE=0のためbt.c自体からは
 *  実行時には未使用の見込みだが，リンクは必要）
 *  ------------------------------------------------------------------
 *  C3のbt/bt_shim.cと同一設計（専用タイマタスク＋期限走査．
 *  BT_TIMER_NUM=4と小さいため毎回全走査で十分）．
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
 *  esp_timer_get_time()はwifi/esp_shim_libc.cで既に実装済み（Wi-Fi統合時
 *  にphy_init.c用として追加．BTも同じPHYを使うためそのまま共用する．
 *  ここでは重複定義しない）．
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
 *  esp_ipc_call_blocking（C6/C5もSOC_CPU_CORES_NUM=1のため同期直接呼出し）
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
 *  スタブ．CONFIG_BT_LE_CONTROLLER_LOG_STORAGE_ENABLE未定義＝該当コード
 *  は実行時未到達の見込みだがリンクは必要）
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
extern uint32_t esp_shim_random(void);

uint32_t
esp_random(void)
{
	return(esp_shim_random());
}
