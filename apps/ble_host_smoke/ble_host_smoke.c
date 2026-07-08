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
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_adv.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

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

/*
 *  D-2b：GAPアドバタイズ観測用グローバル（JTAGでシンボル読出し）．
 */
volatile uint32_t	g_adv_active;		/* 1=接続可能アドバタイズ実行中 */
volatile int32_t	g_adv_rc = -1;		/* 最後の ble_gap_adv_start 戻り値 */
volatile uint32_t	g_gap_conn_count;	/* CONNECT イベント回数 */
volatile uint32_t	g_gap_disc_count;	/* DISCONNECT イベント回数 */
volatile uint32_t	g_gap_event_count;	/* 全 GAP イベント回数 */
volatile uint8_t	g_own_addr_type;

#define BLE_DEVICE_NAME		"ASP3-C3-BLE"

/*  RTC_CNTL scratch（STORE系）．リセットを跨いで残り，JTAG読出し容易  */
#define BLE_SYNC_MARK_ADDR	((void *) 0x60008050UL)
#define BLE_SYNC_MARK_VAL	0x5ADE51C0UL
#define BLE_ADV_MARK_ADDR	((void *) 0x60008054UL)	/* adv開始マーカ */
#define BLE_ADV_MARK_VAL	0x0ADE5000UL
#define BLE_CONN_MARK_ADDR	((void *) 0x60008058UL)	/* connectマーカ */
#define BLE_CONN_MARK_VAL	0xC0117EC7UL

static int gap_event_cb(struct ble_gap_event *event, void *arg);

/*
 *  接続可能アドバタイズ（undirected connectable / general discoverable）を
 *  開始する．sync完了後および切断後に呼ぶ．
 */
static void
start_advertising(void)
{
	struct ble_hs_adv_fields	fields;
	struct ble_gap_adv_params	adv_params;
	int							rc;

	/*  アドレス型を自動決定（privacy=0＝public/static）  */
	rc = ble_hs_id_infer_auto(0, (uint8_t *) &g_own_addr_type);
	if (rc != 0) {
		g_adv_rc = rc;
		syslog(LOG_ERROR, "ble_host_smoke: id_infer_auto rc=%d", (int_t) rc);
		return;
	}

	/*  アドバタイズデータ：flags＋完全ローカル名  */
	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.name = (const uint8_t *) BLE_DEVICE_NAME;
	fields.name_len = (uint8_t) strlen(BLE_DEVICE_NAME);
	fields.name_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		g_adv_rc = rc;
		syslog(LOG_ERROR, "ble_host_smoke: adv_set_fields rc=%d", (int_t) rc);
		return;
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
						   &adv_params, gap_event_cb, NULL);
	g_adv_rc = rc;
	if (rc == 0) {
		g_adv_active = 1U;
		sil_wrw_mem(BLE_ADV_MARK_ADDR, BLE_ADV_MARK_VAL);
		syslog(LOG_NOTICE,
			   "ble_host_smoke: advertising started as '%s' (own_addr_type=%d)",
			   BLE_DEVICE_NAME, (int_t) g_own_addr_type);
	}
	else {
		syslog(LOG_ERROR, "ble_host_smoke: adv_start rc=%d", (int_t) rc);
	}
}

/*
 *  GAPイベントコールバック（接続/切断/アドバタイズ完了を最小処理）．
 */
static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
	(void) arg;
	g_gap_event_count++;

	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		g_gap_conn_count++;
		sil_wrw_mem(BLE_CONN_MARK_ADDR, BLE_CONN_MARK_VAL);
		syslog(LOG_NOTICE,
			   "ble_host_smoke: GAP CONNECT status=%d handle=%d",
			   (int_t) event->connect.status,
			   (int_t) event->connect.conn_handle);
		if (event->connect.status != 0) {
			/*  接続確立失敗＝再アドバタイズ  */
			g_adv_active = 0U;
			start_advertising();
		}
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		g_gap_disc_count++;
		g_adv_active = 0U;
		syslog(LOG_NOTICE, "ble_host_smoke: GAP DISCONNECT reason=%d",
			   (int_t) event->disconnect.reason);
		start_advertising();	/*  切断後に再アドバタイズ  */
		break;
	case BLE_GAP_EVENT_ADV_COMPLETE:
		g_adv_active = 0U;
		syslog(LOG_NOTICE, "ble_host_smoke: GAP ADV_COMPLETE reason=%d",
			   (int_t) event->adv_complete.reason);
		break;
	default:
		break;
	}
	return 0;
}

static void
on_sync(void)
{
	g_ble_sync_done = 1U;
	sil_wrw_mem(BLE_SYNC_MARK_ADDR, BLE_SYNC_MARK_VAL);
	syslog(LOG_NOTICE, "ble_host_smoke: ble_hs SYNC, host up");

	/*  sync完了＝コントローラ同期済み．接続可能アドバタイズを開始する
	    （ble_gap_*はsync後のイベント文脈で呼ぶのが作法．NimBLE設計）．  */
	start_advertising();
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
	 *  D-2b（最小adv）：GAP/GATTサービス（ble_svc_gap_init等）は
	 *  接続可能アドバタイズには必須でないため，まずは登録しない．
	 *  デバイス名はadvデータで広告する（start_advertising）．
	 */

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
