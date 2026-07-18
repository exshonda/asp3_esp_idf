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
 *  Bluetooth統合（ESP32-C5世代コントローラ．Phase D-1／BLE実施03）の
 *  周辺プリミティブ実装
 *
 *  ★ライブラリ選定（BLE実施03の中心判断）：本ラウンドはC6のBLE実施01
 *  （hal submoduleのcontroller/esp32c6/bt.c＝platform/os.hのesp_os_*
 *  経由）とは異なり，controller/esp32c5/bt.cを**hal submoduleではなく
 *  ESP-IDF v6.1（~/tools/esp-idf-v6.1）から採用する**（esp_bt.cmake
 *  参照）．理由：C5のWiFi統合（実施09/10，docs/c5-bringup.md）は
 *  「hal submoduleのlibphy.a（v8世代）はeco2シリコンのPHY RX較正
 *  （phy_iq_est_enable_new）で収束せずハングする．IDF v6.1のlibphy.a
 *  （v9世代）は収束する」ことを実機で確定させ，WiFiのPHY/coexblobを
 *  IDF v6.1へ切替えて解決した．BTコントローラ（controller/esp32c5/
 *  bt.c）もesp_bt_controller_enable()の中でesp_phy_enable(PHY_MODEM_BT)
 *  を呼び，WiFiと**全く同じ**libphy.a／register_chipv7_phy経路を
 *  通る——BTだけhal世代のlibble_app.a＋hal世代のlibphy.aを組み合わせる
 *  と，WiFiが踏んだ「eco2で収束しないPHY較正ループ」を素朴に再現する
 *  リスクが高い．よってBTもPHY/coexと**同じIDF v6.1（v9世代）の
 *  matched setで統一する**（hal世代のlibble_app.aとv6.1世代のlibphy.a
 *  を手で混ぜる「ハイブリッド」案は，Espressifが実際に検証していない
 *  blob-ABI境界を新規に作ることになるため採らない）．
 *
 *  副次的な発見：IDF v6.1のcontroller/esp32c5/bt.cはhal submodule版と
 *  異なり，"platform/os.h"のesp_os_*を経由**しない**——**C3の古い世代
 *  bt.cと同じプログラミングモデル**（FreeRTOS API＝xTaskCreatePinnedToCore
 *  /vTaskDeleteを直接呼び，割込みは標準esp_intr_alloc/esp_intr_free
 *  APIを直接呼ぶ）を採用している．そのためC3の
 *  asp3/target/esp32c3_espidf/bt/bt_shim.c（FreeRTOS直接呼び出し型）が
 *  本ファイルの直接の土台になる（C6のplatform/os.h型bt_shim.cではない）．
 *  freertos/*.hスタブ自体もC3の
 *  asp3/target/esp32c3_espidf/bt/stub/include/freertos/一式をesp_bt.cmake
 *  のインクルードパスでそのまま再利用する（新規コピーはしない）．
 *  また，v6.1のbt.cはnpl_os_*ではなくnpl_freertos_*を直接呼ぶ
 *  （nimble_port_freertos.hも正しくinclude済み）ため，C6のBLE実施01で
 *  必要だった上流ドリフト吸収シム2件（nimble_port_os.hリダイレクト・
 *  npl_os_*→npl_freertos_*橋渡し）は**本ラウンドでは不要**（v6.1の
 *  ソースツリー自体に当該ドリフトが存在しないため）．
 *
 *  クロック：C5はC6と同じくbt.c自身がmodem_clock_module_enable
 *  (PERIPH_BT_MODULE)／modem_clock_module_mac_reset(PERIPH_BT_MODULE)を
 *  呼ぶ設計．ASP3が追加で要るのは実施13のICGアンロック
 *  （esp_shim_modem_icg_init，本ラウンドでwifi/esp_shim.cへ移設．BLE実施03
 *  「変更ファイル」参照）をBTパスでも呼ぶこと．★注意（未検証・スコープ
 *  外に明記）：C5のWiFi bring-upは実施13のICGアンロックだけでなく，
 *  実施21-24で追加したPMU/PVT/HP_ACTIVE bias/regi2cマスタクロック/ocode
 *  較正の一連の初期化（wifi/esp_wifi_adapter.cのwifi_clock_enable_wrapper
 *  内，WiFi専用static関数群）も併せて必要だった．これらはBT側へは
 *  **本ラウンドでは移植していない**（既に動作実績のあるWiFi実装への
 *  リファクタリングによる回帰リスクを避けるため．エビデンスなしに
 *  C6の教訓を機械的に踏襲しないのと同じ理由で，未検証のまま持ち込まない）．
 *  もしBT側のPHY較正が同型のハング（WiFiが実施09までに見せたのと同じ
 *  症状＝無限リトライループ）を示した場合，これが最有力の次段候補になる
 *  （実装済みコードの所在はwifi/esp_wifi_adapter.c参照．必要になれば
 *  同型の共有関数化を行う）．
 *
 *  割込み：C5はソースルーティング（INTMTX，C6/C3と同一アドレス
 *  0x60010000）と標準RISC-V CLIC（CLIC_INT_CTRL_REG，C6独自の
 *  "PLIC_MX"とは異なる）の組合せ（wifi/esp_wifi_adapter.cの
 *  set_intr_wrapperと同一のレジスタ配置・規約．docs/c5-bringup.md）．
 *  C3/C6由来の教訓（S3で確定した多重登録耐性．docs/s3-bt-intr-source-
 *  overwrite-fix-for-c3.md）を踏まえ，最初からスロット配列化する．
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
#include "esp32c5.h"		/* ESP32C5_INTMTX_BASE／ESP32C5_CLIC_CTRL_BASE */
#include "bt/bt_cfg.h"

#define BT_LOCK()	uint32_t bt_lock_ = esp_shim_int_disable()
#define BT_UNLOCK()	esp_shim_int_restore(bt_lock_)

/*
 *  ------------------------------------------------------------------
 *  BLEベースバンド/modem クロックの下準備（実施13のICGアンロックを
 *  BTパスにも適用．esp_shim_modem_icg_init()の実体はwifi/esp_shim.c
 *  ＝ESP32C5_WIFI/ESP32C5_BT両方でリンクされる共有ファイルへ本ラウンドで
 *  移設した）．bt.c自身が呼ぶmodem_clock_module_enable(PERIPH_BT_MODULE)
 *  より前，esp_bt_controller_init()より前にアプリから呼ぶこと（C3/C6の
 *  esp_shim_bt_clock_init()と同じ呼出し規約）．
 * ------------------------------------------------------------------
 */
extern void esp_shim_modem_icg_init(void);

void
esp_shim_bt_clock_init(void)
{
	esp_shim_modem_icg_init();
}

/*
 *  ------------------------------------------------------------------
 *  esp_intr_alloc/free/enable/disable（v6.1のbt.cが直接呼ぶ標準割込み
 *  確保API＝C3のbt.cと同じプログラミングモデル．C5はINTMTX＋標準
 *  RISC-V CLIC．wifi/esp_wifi_adapter.cのset_intr_wrapperと同一の
 *  レジスタ配置・規約をそのまま踏襲する）
 *
 *  ★多重登録安全化（C3/S3由来の教訓を最初から適用．docs/
 *  s3-bt-intr-source-overwrite-fix-for-c3.md・docs/bt-shim.md）：BT
 *  コントローラがesp_intr_allocを複数回呼ぶ可能性を排除できないため，
 *  最初からスロット配列化し呼出し順でASP3 INTNO（延いてはCLIC線）を
 *  分離する．
 * ------------------------------------------------------------------
 */
#define BT_INTMTX_BASE_ADDR    ESP32C5_INTMTX_BASE      /* 0x60010000．ソースルーティング */
#define BT_CLIC_CTRL_BASE_ADDR ESP32C5_CLIC_CTRL_BASE    /* 0x20801000．CLIC per-line制御 */
#define BT_CLIC_EXT_OFFSET     16U                       /* ASP3 INTNO(1〜31)→CLIC内部番号(17〜47) */
#define BT_CLIC_LINE(intno)    ((uint32_t)(intno) + BT_CLIC_EXT_OFFSET)
#define BT_CLIC_IE_OFF(line)   (BT_CLIC_CTRL_BASE_ADDR + (line) * 4U + 1U)
#define BT_CLIC_ATTR_OFF(line) (BT_CLIC_CTRL_BASE_ADDR + (line) * 4U + 2U)
#define BT_CLIC_CTL_OFF(line)  (BT_CLIC_CTRL_BASE_ADDR + (line) * 4U + 3U)
#define BT_CLIC_ATTR_LEVEL_MACHINE   0xC0U
#define BT_CLIC_NLBITS_SHIFT   5U
#define BT_CLIC_PRIO_TO_CTL(pri) \
	(((uint32_t)(pri) << BT_CLIC_NLBITS_SHIFT) | ((1U << BT_CLIC_NLBITS_SHIFT) - 1U))

#define BT_INTR_CPU_INTNO      1		/* スロット0のASP3 INTNO（esp_shim.cfgのDEF_INH(1)） */
#define BT_INTR_MAX_SLOT       2		/* スロットnのINTNOは(1+n)．DEF_INH(2)まで用意済み */

struct intr_handle_data_t {
	int	source;
	int	intno;	/* 0=未割当て */
};

static struct intr_handle_data_t	bt_intr_slot[BT_INTR_MAX_SLOT];
static uint32_t						bt_intr_nalloc;

/*
 *  （診断計装）esp_intr_allocの呼出し回数とsourceの時系列を記録する．
 *  C6のBLE実施01と同じ形式・同じアドレス（LP_AON STORE7相当，
 *  0x600B101C＝usb-reset生存）．C5のtarget_kernel_impl.c／
 *  wifi/esp_wifi_adapter.cが使用中のSTORE1(+0x4，実施35のRTC_SLOW_
 *  CLK_CAL)・STORE2-4(+0x8/+0xC/+0x10，実施41)とは衝突しない未使用
 *  オフセットであることを確認済み。
 *    bits[31:24]=0xA1（マーカ），[23:16]=呼出し累積回数，
 *    [15:8]=1回目のsource，[7:0]=2回目のsource
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

esp_err_t
esp_intr_alloc(int source, int flags, intr_handler_t handler, void *arg,
			   intr_handle_t *ret_handle)
{
	struct intr_handle_data_t	*slot;
	uint32_t					intno;
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

	/*  呼出し順でスロット割当て（1個目→INTNO1，2個目→INTNO2）  */
	if (bt_intr_nalloc <= (uint32_t) BT_INTR_MAX_SLOT) {
		slot = &bt_intr_slot[bt_intr_nalloc - 1U];
	}
	else {
		slot = &bt_intr_slot[BT_INTR_MAX_SLOT - 1];
	}
	intno = (uint32_t) BT_INTR_CPU_INTNO + (uint32_t)(slot - bt_intr_slot);
	line = BT_CLIC_LINE(intno);
	slot->source = source;
	slot->intno = (int) intno;

	/*  INTMTX：source→CLIC内部線番号 のルーティング  */
	sil_wrw_mem((void *)(uintptr_t)(BT_INTMTX_BASE_ADDR + (uint32_t) source * 4U),
				line);
	/*  CLIC：優先度2固定・LEVEL型・MACHINEモード（WiFi MAC割込みと同じ流儀） */
	sil_wrb_mem((void *) BT_CLIC_CTL_OFF(line), (uint8_t) BT_CLIC_PRIO_TO_CTL(2U));
	sil_wrb_mem((void *) BT_CLIC_ATTR_OFF(line), (uint8_t) BT_CLIC_ATTR_LEVEL_MACHINE);
	esp_shim_set_isr((int32_t) intno, (void *) handler, arg);
	sil_wrb_mem((void *) BT_CLIC_IE_OFF(line), 1U);

	*ret_handle = (intr_handle_t) slot;
	return(ESP_OK);
}

static uint32_t
bt_intr_line_of(intr_handle_t handle)
{
	struct intr_handle_data_t	*h = (struct intr_handle_data_t *) handle;

	if (h != NULL && h->intno != 0) {
		return(BT_CLIC_LINE((uint32_t) h->intno));
	}
	return(BT_CLIC_LINE((uint32_t) BT_INTR_CPU_INTNO));
}

esp_err_t
esp_intr_free(intr_handle_t handle)
{
	sil_wrb_mem((void *) BT_CLIC_IE_OFF(bt_intr_line_of(handle)), 0U);
	return(ESP_OK);
}

esp_err_t
esp_intr_enable(intr_handle_t handle)
{
	sil_wrb_mem((void *) BT_CLIC_IE_OFF(bt_intr_line_of(handle)), 1U);
	return(ESP_OK);
}

esp_err_t
esp_intr_disable(intr_handle_t handle)
{
	sil_wrb_mem((void *) BT_CLIC_IE_OFF(bt_intr_line_of(handle)), 0U);
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  esp_timer_*（npl_os_freertos.cのcallout実装．BLE_NPL_USE_ESP_TIMER=1
 *  で選択．C3/C6のbt_shim.cと同一設計＝専用タイマタスク＋固定プール）
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
 *  タイマタスク：期限順の線形走査（BT_TIMER_NUM=16と小さいため
 *  ソート済みリストは使わず毎回全走査で十分）
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
 *  esp_ipc_call_blocking（C5もSOC_CPU_CORES_NUM=1のため同期直接呼出し）
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
 *  スタブ．esp_partition.hはC3のbt/stub/include（esp_bt.cmakeの
 *  インクルードパス）をそのまま再利用する）
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
