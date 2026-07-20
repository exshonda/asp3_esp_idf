/*
 *  Bluetooth（BLE）コントローラ起動＋VHCIループバックのスモークテスト
 *  （Phase D-1．設計・経緯はdocs/dev/esp-idf-integration.md Phase D／
 *  docs/bt-shim.md）
 *
 *  esp_bt_controller_init→enable→VHCI受信コールバック登録→HCI Reset
 *  コマンドを送信し，Command Complete応答が返ることを確認する．
 *  ホストスタック（NimBLE）はPhase D-2の対象で本デモには含めない．
 *
 *  esp_bt_controller_config_tはBT_CONTROLLER_INIT_CONFIG_DEFAULT()
 *  マクロ（menuconfigのCONFIG_BT_CTRL_*_EFF一式に依存）を使わず，
 *  本ビルドにはKconfigが無いため全フィールドを直接埋める
 *  （値はesp_bt.hのフィールド説明にあるデフォルト値に倣う）。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "bt_smoke_c3.h"

#include "esp_bt.h"
#include "esp_shim.h"

/*
 *  BLEベースバンドのクロックを立てる（emi.c:164対策．bt_shim.c）．
 *  esp_bt_controller_init() より前に呼ぶ必要がある．詳細はdocs/bt-shim.md．
 */
extern void esp_shim_bt_clock_init(void);

static volatile bool_t	hci_reset_done;

static void
vhci_notify_host_send_available(void)
{
	/*  何もしない（送信は本デモでは1回のみ．送信可否は都度
	 *  esp_vhci_host_check_send_available()で確認する）  */
}

static int
vhci_notify_host_recv(uint8_t *data, uint16_t len)
{
	int	i;

	syslog(LOG_NOTICE, "bt_smoke: VHCI recv %d bytes", (int_t) len);
	for (i = 0; i < (int) len && i < 16; i++) {
		syslog(LOG_NOTICE, "bt_smoke:   [%d] = 0x%02x", i, (int_t) data[i]);
	}
	/*
	 *  HCI Command Complete（packet type 0x04・event code 0x0E）を
	 *  検出できればコントローラ生存＋VHCI往復の証明になる
	 */
	if (len >= 2 && data[0] == 0x04 && data[1] == 0x0E) {
		syslog(LOG_NOTICE,
			   "bt_smoke: HCI Command Complete received -> "
			   "controller alive, VHCI loopback OK");
		hci_reset_done = true;
	}
	return(0);
}

static const esp_vhci_host_callback_t	vhci_cb = {
	.notify_host_send_available = vhci_notify_host_send_available,
	.notify_host_recv = vhci_notify_host_recv,
};

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg;
	esp_err_t					err;
	uint8_t						hci_reset[4] = { 0x01, 0x03, 0x0C, 0x00 };
	int							retry;

	(void) exinf;

	esp_shim_initialize();
	esp_shim_coex_adapter_register();

	/*
	 *  emi.c:164対策：BLEベースバンド(BB)のクロックを有効化する．
	 *  実ESP-IDF/NuttXがブート時のesp_perip_clk_init()で立てる
	 *  SYSTEM_WIFI_CLK_EN を，ASP3 Direct Bootは通らないため，
	 *  コントローラ起動前にここで補完する．これが無いと
	 *  esp_bt_controller_init()中のr_emi_em_base_initのBB書込みが落ち，
	 *  「BLE assert emi.c 164」で停止する（2ボードJTAG差分で確定）．
	 */
	esp_shim_bt_clock_init();

	syslog(LOG_NOTICE, "bt_smoke: esp_bt_controller_init");

	/*
	 *  BT_CONTROLLER_INIT_CONFIG_DEFAULT()はmenuconfig生成の
	 *  CONFIG_BT_CTRL_*_EFF一式に依存するため使わず，フィールドを
	 *  直接埋める（値はesp_bt.hのデフォルト説明に倣う）
	 */
	memset(&cfg, 0, sizeof(cfg));
	cfg.magic = ESP_BT_CTRL_CONFIG_MAGIC_VAL;
	cfg.version = ESP_BT_CTRL_CONFIG_VERSION;
	cfg.controller_task_stack_size = ESP_TASK_BT_CONTROLLER_STACK;
	cfg.controller_task_prio = ESP_TASK_BT_CONTROLLER_PRIO;
	cfg.controller_task_run_cpu = 0;
	cfg.bluetooth_mode = ESP_BT_MODE_BLE;
	cfg.ble_max_act = 6;
	cfg.sleep_mode = 0;			/* modem sleep無効（Wi-Fi PS_NONE相当） */
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
	/*
	 *  txpwr_dft：実際のKconfig既定はBT_CTRL_DFT_TX_POWER_LEVEL_P9
	 *  （+9dBm）＝esp_power_level_tで11．誤って0（P_N24=-24dBm相当）
	 *  としていたのを修正．
	 */
	cfg.txpwr_dft = 11;
	/*
	 *  cfg_mask：esp_bt.hの実際のデフォルトは
	 *  CFG_MASK_BIT_SCAN_DUPLICATE_OPTION=(1<<0)=1．0のままだと
	 *  scan_duplicate_type等の解釈にズレが生じる．
	 */
	cfg.cfg_mask = 1;
	cfg.scan_duplicate_mode = 0;
	cfg.scan_duplicate_type = 0;
	cfg.normal_adv_size = 100;
	/*
	 *  mesh_adv_size：BLEメッシュのスキャン重複排除を使わない
	 *  （CONFIG_BT_CTRL_BLE_MESH_SCAN_DUPL_EN未定義）場合の実際の
	 *  Kconfig既定は0（esp_bt.hのMESH_DUPLICATE_SCAN_CACHE_SIZE参照）．
	 *  100は誤り．ただし実機JTAG調査の結果，"BLE assert emi.c 164"の
	 *  原因ではないと確認済み（0/100いずれでも同一アサートが発生．
	 *  詳細はdocs/bt-shim.md）．
	 */
	cfg.mesh_adv_size = 0;
	cfg.coex_phy_coded_tx_rx_time_limit = 0;
	cfg.hw_target_code = 0x01010000UL;	/* BLE_HW_TARGET_CODE_CHIP_ECO0相当 */
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

	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke: esp_bt_controller_init -> %d", (int_t) err);
		return;
	}

#ifdef BT_PROBE_STOP_AFTER_INIT
	/*
	 *  emi.c:164調査用プローブ：init成功直後（enable前）で無限停止する．
	 *  JTAG halt時にBBのEMベースレジスタ（0x60031210=region3）を採取して
	 *  「init時に書かれているか」を判定する．通常ビルドでは未定義．
	 */
	syslog(LOG_NOTICE, "bt_smoke: PROBE stop after init (0x60031210 ready)");
	for (;;) {
		(void) tslp_tsk(1000000);
	}
#endif

	syslog(LOG_NOTICE, "bt_smoke: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke: esp_bt_controller_enable -> %d", (int_t) err);
		return;
	}

	err = esp_vhci_host_register_callback(&vhci_cb);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke: esp_vhci_host_register_callback -> %d",
			   (int_t) err);
		return;
	}

	syslog(LOG_NOTICE, "bt_smoke: controller enabled, sending HCI Reset");
	for (retry = 0; retry < 50 && !esp_vhci_host_check_send_available(); retry++) {
		(void) tslp_tsk(100000);	/* 100ms */
	}
	esp_vhci_host_send_packet(hci_reset, sizeof(hci_reset));

	for (retry = 0; retry < 30 && !hci_reset_done; retry++) {
		(void) tslp_tsk(200000);	/* 200ms */
	}

	if (hci_reset_done) {
		syslog(LOG_NOTICE, "bt_smoke: Phase D-1 milestone reached");
	}
	else {
		syslog(LOG_NOTICE, "bt_smoke: FAILED (no HCI Command Complete)");
	}
	syslog(LOG_NOTICE, "bt_smoke: done");
}
