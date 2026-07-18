/*
 *  ------------------------------------------------------------------
 *  ESP32-C6 Bluetooth（BLE）統合  bt_shim（IDF v6.1 matched-set 版）
 *  ------------------------------------------------------------------
 *  Phase D-1／BLE実施13（docs/ble-c5c6-plan.md §13）．
 *
 *  本ファイルは ESP32C6_BT_IDF61=ON（IDF v6.1 matched-set swap）専用の
 *  シムである．既定（ESP32C6_BT_IDF61=OFF・hal submodule版）は従来どおり
 *  bt/bt_shim.c を使う（両者は esp_bt_idf61.cmake / esp_bt.cmake の
 *  トグルで排他選択＝同時にはリンクされない）．
 *
 *  ★hal版（bt_shim.c）との決定的な違い：
 *    - IDF v6.1 の controller/esp32c6/bt.c は **C3の旧世代bt.cと同じ
 *      プログラミングモデル**——FreeRTOS API（xTaskCreatePinnedToCore／
 *      vTaskDelete）と標準 esp_intr_alloc/esp_intr_free を直接呼ぶ
 *      （実測確認済み．hal版のように platform/os.h の esp_os_* を経由
 *      **しない**）．そのため本シムは
 *        (i)  esp_os_* タスク/割込み関数を持たない（タスクは C3 の
 *             bt/stub/include/freertos/task.h の static inline
 *             xTaskCreatePinnedToCore→esp_shim_task_create 経由で
 *             解決される．esp_bt_idf61.cmake が C3 stub を include）．
 *        (ii) npl_os_*→npl_freertos_* 橋渡しシムを持たない（v6.1の
 *             bt.c は npl_freertos_* を直接呼ぶ．上流ドリフト無し）．
 *      提供するのは esp_intr_alloc/free/enable/disable（C3型・標準名）と
 *      esp_timer_*／esp_pm_lock_*／esp_ipc_call_blocking／esp_partition_*／
 *      esp_random（チップ非依存の下位プリミティブ．hal版から無変更移植）．
 *    - 割込みルーティングは C6 のハードウェア（INTMTX＋PLIC_MX）を使う
 *      （C5の CLIC とは異なる＝C5の bt_shim.c を流用できない．hal版
 *      bt_shim.c の esp_os_intr_alloc 本体を標準名 esp_intr_alloc へ
 *      改名・移植する）．
 *
 *  ★§11 のクロック2件修正（regi2c sel_160m ＋ WIFIPWR）＋実施91の
 *    ICG アンロックは esp_shim_bt_clock_init() に維持する（v6.1 swap
 *    でも前提．シリコン級の下準備で冪等な read-modify-write のため
 *    安全）．docs/ble-c5c6-plan.md §11.
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

#include "hal/regi2c_ctrl_ll.h"	/* §11: PHY-init regi2cハング根治 */

#define BT_LOCK()	uint32_t bt_lock_ = esp_shim_int_disable()
#define BT_UNLOCK()	esp_shim_int_restore(bt_lock_)

/*
 *  ------------------------------------------------------------------
 *  BLEベースバンド/modem クロックの下準備（§11＋実施91）
 *  ------------------------------------------------------------------
 *  hal版 bt/bt_shim.c の esp_shim_bt_clock_init() から無変更移植．
 *  esp_shim_modem_icg_init()の実体は wifi/esp_shim.c（ESP32C6_WIFI／
 *  ESP32C6_BT の両方でリンクされる共有ファイル）．bt.c自身が呼ぶ
 *  modem_clock_module_enable(PERIPH_BT_MODULE)より前，
 *  esp_bt_controller_init()より前にアプリから呼ぶこと．
 */
extern void esp_shim_modem_icg_init(void);

void
esp_shim_bt_clock_init(void)
{
	esp_shim_modem_icg_init();

	/*
	 *  ★§11：解析I2C（regi2c）マスタのクロック源選択（160M）を明示的に
	 *  行う．WiFiパスは esp_phy_enable 前に
	 *  _regi2c_ctrl_ll_master_enable_clock(true) と
	 *  regi2c_ctrl_ll_master_configure_clock()（＝MODEM_LPCON
	 *  i2c_mst_clk_conf.clk_i2c_mst_sel_160m=1）の両方を呼ぶが，BTパスは
	 *  clk源選択（sel_160m）を一度も行わないため regi2c doneビットが
	 *  永久に立たず chip_i2c_readReg が無限スピンする（board C実機JTAG
	 *  実測．docs/ble-c5c6-plan.md §11）．以下2行で根治．
	 */
	_regi2c_ctrl_ll_master_enable_clock(true);
	regi2c_ctrl_ll_master_configure_clock();

	/*
	 *  ★§11 第2ハング根治：MODEM_LPCON_CLK_CONF(0x600af018) の
	 *  CLK_WIFIPWR_EN(bit0) を有効化する（統合modemの共有電源/PLL
	 *  クロックゲート．BLEのRFシンセもこのカウンタに依存）．bt.cの
	 *  modem_clock_module_enable(PERIPH_BT_MODULE)は本ビットを立てない．
	 *  ※read-modify-writeで後段の他ビットを温存する．
	 */
	*(volatile uint32_t *)0x600af018U |= 0x1U;	/* MODEM_LPCON_CLK_WIFIPWR_EN */
}

/*
 *  ------------------------------------------------------------------
 *  esp_intr_alloc/free/enable/disable（v6.1のbt.cが直接呼ぶ標準割込み
 *  確保API＝C3のbt.cと同じプログラミングモデル）
 *  ------------------------------------------------------------------
 *  ESP32-C6はソースルーティング（INTMTX）とCPU割込み線制御（PLIC_MX）が
 *  別レジスタブロックに分離している（hal版 bt_shim.c の esp_os_intr_alloc
 *  と同一のレジスタ配置．C5の CLIC とは異なる）．
 *
 *  ★多重登録安全化（S3/C3由来の教訓を最初から適用）：BTコントローラが
 *  esp_intr_allocを複数回呼ぶ可能性を排除できないため，最初からスロット
 *  配列化し呼出し順でCPU線を分離する．
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
 *  （診断計装）esp_intr_allocの呼出し回数とsourceの時系列をLP_AON
 *  STORE7相当（0x600B101C＝usb-reset生存）へ記録する（hal版 bt_shim.c
 *  と同一形式・同一アドレス）．
 *    bits[31:24]=0xA1（マーカ），[23:16]=呼出し累積回数，
 *    [15:8]=1回目のsource，[7:0]=2回目のsource
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

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
esp_intr_free(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG)
				& ~(1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

/*
 *  esp_intr_enable/disable：v6.1のesp32c6 bt.cは直接呼ばないが，
 *  他のIDFソースがリンク時に参照し得るため PLIC_MX enableビットの
 *  set/clear で提供する（C5 bt_shim.c と同じ防御的実装）．
 */
esp_err_t
esp_intr_enable(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG)
				| (1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(intr_handle_t handle)
{
	sil_wrw_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG,
				sil_rew_mem((void *)(uintptr_t) BT_PLICMX_ENABLE_REG)
				& ~(1UL << bt_intr_line_of(handle)));
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_timer_*（npl_os_freertos.cのcallout実装．BLE_NPL_USE_ESP_TIMER=1
 *  で選択）—— hal版 bt/bt_shim.c から無変更移植（チップ非依存）
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
	/*  #5：critical（MIE=0）内から arm されると sig_sem が E_CTX で消えるため，
	    起床要求を semID で保留し exit_critical/機会flush で精算する（wifi
	    esp_shim.c の救済を共用．真ISRからは sig_sem 成立で即発火）．  */
	esp_shim_signal_or_pend(BT_TIMER_SEM);	/* タイマタスクへ再計算を促す */
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
 *  esp_timer_get_time()はwifi/esp_shim_libc.cで既に実装済み（共用）．
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
 *  タイマタスク：期限順の線形走査（bt.cfg の BT_TIMER_TSK が起動）
 */
void
bt_timer_task(EXINF exinf)
{
	(void) exinf;

	for (;;) {
		int64_t			now;
		int64_t			nearest = -1;
		uint_t			i;
		TMO				tmo;
		esp_timer_cb_t	cb = NULL;
		void			*arg = NULL;

		now = esp_shim_time_us();
		/*
		 *  ★プール走査は BT_LOCK 下で行う（deadline_us/active の読み書きを
		 *  timer_start/stop/delete と直列化）．無ロックだと，(a) 「期限到来」
		 *  判定後にプリエンプトされ host が stop→start で再 arm した直後に
		 *  active=false を書いて再 arm タイマを黙って殺す，(b) RV32 では
		 *  deadline_us(int64) の読みが2命令に割れ torn read になる．
		 *  コールバックはロック外で呼ぶ（cb は timer API を再入し得るため）．
		 *  1個処理したら continue で再走査（cb が他タイマを arm し得る）．
		 */
		BT_LOCK();
		for (i = 0U; i < BT_TIMER_NUM; i++) {
			struct esp_timer	*t = &bt_timer_pool[i];

			if (t->used && t->active) {
				if (t->deadline_us <= now) {
					cb = t->callback;
					arg = t->arg;
					if (t->period_us != 0U) {
						t->deadline_us += (int64_t) t->period_us;
					}
					else {
						t->active = false;
					}
					break;
				}
				else if (nearest < 0 || t->deadline_us < nearest) {
					nearest = t->deadline_us;
				}
			}
		}
		BT_UNLOCK();

		if (cb != NULL) {
			cb(arg);
			continue;			/* 他の期限到来タイマを続けて処理 */
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
 *  esp_pm_lock_*（電源管理．PS_NONE相当＝no-op）
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
 *  esp_ipc_call_blocking（SOC_CPU_CORES_NUM=1＝同期直接呼出し）
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
 *  esp_partition_*（NVS/較正データ．「常に存在しない」スタブ）
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
