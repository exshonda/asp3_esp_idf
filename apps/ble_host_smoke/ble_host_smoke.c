/*
 *  NimBLE host スモークテスト（Phase D-2）
 *
 *  D-1（apps/bt_smoke）で確立したBLEコントローラ起動シーケンスの上に，
 *  NimBLE ホストスタックを載せる．狙いは ble_hs の sync コールバック到達
 *  （＝ホストがコントローラと同期し起動完了）．
 *
 *  ホスト初期化の配線について：
 *    本ポート（hal/components/bt）の nimble_port_init() は
 *    CONFIG_BT_CONTROLLER_ENABLED のとき *内部で* esp_bt_controller_init/
 *    enable を BT_CONTROLLER_INIT_CONFIG_DEFAULT() で行う．しかし D-1 で
 *    実機検証したコントローラ設定は apps/bt_smoke の手書き cfg であり，
 *    その値を正確に使いたい（かつコントローラ二重初期化を避けたい）ため，
 *    ここでは:
 *      esp_bt_controller_init(&cfg)  ← bt_smoke と同じ手書き cfg
 *      esp_bt_controller_enable(BLE)
 *      esp_nimble_init()             ← ホストのみ初期化（内部で
 *                                       esp_nimble_hci_init も呼ぶ．
 *                                       コントローラには触れない）
 *      ble_hs_cfg.sync_cb = on_sync
 *      nimble_port_freertos_init(ble_host_task)
 *    とする（nimble_port_init() は呼ばない＝コントローラ二重初期化回避）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "ble_host_smoke.h"

#include "esp_bt.h"
#include "esp_shim.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"

/*
 *  BLEベースバンドのクロックを立てる（emi.c:164対策．bt/bt_shim.c）．
 *  esp_bt_controller_init() より前に呼ぶ必要がある．詳細はdocs/bt-shim.md．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  ble_hs sync 到達マーカ（JTAGで容易に観測できるよう固定アドレスへも
 *  記録する）．
 */
volatile uint32_t	g_ble_sync_done;

/*  RTC_CNTL scratch（STORE0 近傍）．リセットを跨いで残り，JTAG読出し容易  */
#define BLE_SYNC_MARK_ADDR	((void *) 0x60008050UL)
#define BLE_SYNC_MARK_VAL	0x5ADE51C0UL

static void
on_sync(void)
{
	g_ble_sync_done = 1U;
	sil_wrw_mem(BLE_SYNC_MARK_ADDR, BLE_SYNC_MARK_VAL);
	syslog(LOG_NOTICE, "ble_host_smoke: ble_hs SYNC, host up");
}

static void
on_reset(int reason)
{
	syslog(LOG_ERROR, "ble_host_smoke: ble_hs RESET, reason=%d", (int_t) reason);
}

/*
 *  NimBLE ホストタスク本体．default event queue を処理し続ける．
 */
static void
ble_host_task(void *param)
{
	(void) param;
	syslog(LOG_NOTICE, "ble_host_smoke: nimble host task started");
	nimble_port_run();		/*  戻らない（nimble_port_stop まで）  */
	nimble_port_freertos_deinit();
}

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg;
	esp_err_t					err;

	(void) exinf;

	esp_shim_initialize();
	esp_shim_coex_adapter_register();

	/*
	 *  emi.c:164対策：BLEベースバンド(BB)のクロックを有効化する
	 *  （bt_smoke と同じ．詳細はdocs/bt-shim.md）．
	 */
	esp_shim_bt_clock_init();

	/*
	 *  BLEコントローラ設定（apps/bt_smoke と同一の手書き値．
	 *  BT_CONTROLLER_INIT_CONFIG_DEFAULT() は使わない）．
	 */
	memset(&cfg, 0, sizeof(cfg));
	cfg.magic = ESP_BT_CTRL_CONFIG_MAGIC_VAL;
	cfg.version = ESP_BT_CTRL_CONFIG_VERSION;
	cfg.controller_task_stack_size = ESP_TASK_BT_CONTROLLER_STACK;
	cfg.controller_task_prio = ESP_TASK_BT_CONTROLLER_PRIO;
	cfg.controller_task_run_cpu = 0;
	cfg.bluetooth_mode = ESP_BT_MODE_BLE;
	cfg.ble_max_act = 6;
	cfg.sleep_mode = 0;
	cfg.sleep_clock = 0;
	cfg.ble_st_acl_tx_buf_nb = 0;
	cfg.ble_hw_cca_check = 0;
	cfg.ble_adv_dup_filt_max = 30;
	cfg.coex_param_en = false;
	cfg.ce_len_type = 0;
	cfg.coex_use_hooks = false;
	cfg.hci_tl_type = ESP_BT_CTRL_HCI_TL_VHCI;
	cfg.hci_tl_funcs = NULL;
	cfg.txant_dft = 0;
	cfg.rxant_dft = 0;
	cfg.txpwr_dft = 11;
	cfg.cfg_mask = 1;
	cfg.scan_duplicate_mode = 0;
	cfg.scan_duplicate_type = 0;
	cfg.normal_adv_size = 100;
	cfg.mesh_adv_size = 0;
	cfg.coex_phy_coded_tx_rx_time_limit = 0;
	cfg.hw_target_code = 0x01010000UL;
	cfg.slave_ce_len_min = 5;
	cfg.hw_recorrect_en = 1;
	cfg.cca_thresh = 75;
	cfg.scan_backoff_upperlimitmax = 0;
	cfg.dup_list_refresh_period = 0;
	cfg.ble_50_feat_supp = false;
	cfg.ble_cca_mode = 0;
	cfg.ble_data_lenth_zero_aux = 0;
	cfg.ble_chan_ass_en = 1;
	cfg.ble_ping_en = 1;
	cfg.ble_llcp_disc_flag = 0;
	cfg.run_in_flash = false;
	cfg.dtm_en = false;
	cfg.enc_en = true;
	cfg.qa_test = false;
	cfg.connect_en = true;
	cfg.scan_en = true;
	cfg.ble_aa_check = false;
	cfg.adv_en = true;

	syslog(LOG_NOTICE, "ble_host_smoke: esp_bt_controller_init");
	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_bt_controller_init -> %d",
			   (int_t) err);
		return;
	}

	syslog(LOG_NOTICE, "ble_host_smoke: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_bt_controller_enable -> %d",
			   (int_t) err);
		return;
	}

	/*
	 *  NimBLE ホストスタックを初期化（コントローラは既に起動済みなので
	 *  esp_nimble_init を使い，nimble_port_init は使わない）．
	 *  esp_nimble_init は内部で esp_nimble_hci_init（VHCI ブリッジ登録・
	 *  バッファ確保）を行う．
	 */
	syslog(LOG_NOTICE, "ble_host_smoke: esp_nimble_init (host)");
	err = esp_nimble_init();
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_nimble_init -> %d", (int_t) err);
		return;
	}

	/*
	 *  sync/reset コールバックを登録してからホストタスクを起動する．
	 */
	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	syslog(LOG_NOTICE, "ble_host_smoke: nimble_port_freertos_init");
	nimble_port_freertos_init(ble_host_task);

	syslog(LOG_NOTICE, "ble_host_smoke: init done, waiting for ble_hs SYNC");

	/*
	 *  sync 到達を待つ（実機観測用．JTAGでは g_ble_sync_done か
	 *  0x60008050 の 0x5ADE51C0 を見る）．
	 */
	{
		int	retry;
		for (retry = 0; retry < 100 && g_ble_sync_done == 0U; retry++) {
			(void) tslp_tsk(100000);	/* 100ms */
		}
		if (g_ble_sync_done != 0U) {
			syslog(LOG_NOTICE, "ble_host_smoke: Phase D-2 milestone reached");
		}
		else {
			syslog(LOG_NOTICE, "ble_host_smoke: no SYNC yet (check on HW)");
		}
	}
	syslog(LOG_NOTICE, "ble_host_smoke: done");
}
