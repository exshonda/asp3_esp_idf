/*
 *  Bluetooth（BLE）コントローラ起動＋VHCIループバックのスモークテスト
 *  （ESP32-C5．Phase D-1／BLE実施03）
 *
 *  apps/bt_smoke_c6と同じ判定基準：esp_bt_controller_init→enable→
 *  VHCI受信コールバック登録→HCI Resetコマンドを送信し，Command
 *  Complete応答が返ることを確認する．ホストスタック（NimBLE）は
 *  D-2aの対象で本デモには含めない．
 *
 *  C6との違い：C5はIDF v6.1由来のbt.c（controller/esp32c5/bt.c）を
 *  使う（asp3/target/esp32c5_espidf/esp_bt.cmake参照）．
 *  esp_bt_controller_config_tの形状・BT_CONTROLLER_INIT_CONFIG_
 *  DEFAULT()マクロはC6と共通のAPI名のためアプリ側は無変更で流用できる．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <sil.h>
#include "bt_smoke_c5.h"

#include "esp_bt.h"
#include "esp_shim.h"

/*
 *  実施13のICGアンロックをBTクロック初期化の一部として呼ぶ
 *  （bt/bt_shim.c）．esp_bt_controller_init() より前に呼ぶ必要がある．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  多重登録トレース（bt/bt_shim.cのesp_intr_allocが記録．
 *  C5 LP_AON STORE7相当＝usb-reset生存，前回boot残値はマーカ0xA1で
 *  判別．アドレスはbt_shim.cのBT_INTR_TRACE_REGと一致させること．
 *  C5のLP_AON STORE1-4は既に別用途（実施35/41）で使用中のため未使用の
 *  STORE7を採用——C6と同一アドレスだが，C5でも衝突しないことを確認済み
 *  （asp3/target/esp32c5_espidf/bt/bt_shim.c参照）．
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

/*
 *  HP_APM M0-M3例外ラッチ（実施42/43と同一レジスタ配置．
 *  target_kernel_impl.cのC5 APM恒久修正がブート時に一度クリアして
 *  いるため，ここで非0が読めればBT有効化後の新規違反）．
 */
#define HP_APM_M0_STATCLR	0x600990CCUL

/*
 *  WiFi/BT共有の割込みディスパッチカウンタ（wifi/esp_shim.c）．
 *  ストーム非発生（正常域）の予防確認に使う．
 */
extern volatile uint32_t esp_shim_int_count[];

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

	syslog(LOG_NOTICE, "bt_smoke_c5: VHCI recv %d bytes", (int_t) len);
	for (i = 0; i < (int) len && i < 16; i++) {
		syslog(LOG_NOTICE, "bt_smoke_c5:   [%d] = 0x%02x", i, (int_t) data[i]);
	}
	/*
	 *  HCI Command Complete（packet type 0x04・event code 0x0E）を
	 *  検出できればコントローラ生存＋VHCI往復の証明になる
	 */
	if (len >= 2 && data[0] == 0x04 && data[1] == 0x0E) {
		syslog(LOG_NOTICE,
			   "bt_smoke_c5: HCI Command Complete received -> "
			   "controller alive, VHCI loopback OK");
		hci_reset_done = true;
	}
	return(0);
}

static const esp_vhci_host_callback_t	vhci_cb = {
	.notify_host_send_available = vhci_notify_host_send_available,
	.notify_host_recv = vhci_notify_host_recv,
};

static void
report_intr_trace(void)
{
	uint32_t	trace = sil_rew_mem((const uint32_t *) BT_INTR_TRACE_REG);

	if ((trace >> 24) == 0xA1U) {
		syslog(LOG_NOTICE,
			   "bt_smoke_c5: intr trace = 0x%08x (nalloc=%d src1=%d src2=%d)",
			   (int_t) trace, (int_t)((trace >> 16) & 0xFFU),
			   (int_t)((trace >> 8) & 0xFFU), (int_t)(trace & 0xFFU));
	}
	else {
		syslog(LOG_NOTICE, "bt_smoke_c5: intr trace not recorded (0x%08x)",
			   (int_t) trace);
	}
}

static void
report_apm_latch(void)
{
	uint_t		i;
	bool_t		any = false;

	for (i = 0U; i < 4U; i++) {
		uint32_t	v = sil_rew_mem((const uint32_t *)(HP_APM_M0_STATCLR + i * 0x10UL));
		if ((v & 1U) != 0U) {
			any = true;
			syslog(LOG_ERROR, "bt_smoke_c5: HP_APM M%d exception latch SET (0x%08x)",
				   (int_t) i, (int_t) v);
		}
	}
	if (!any) {
		syslog(LOG_NOTICE, "bt_smoke_c5: HP_APM M0-M3 exception latch clear (BT path OK)");
	}
}

static void
report_intr_rate(void)
{
	uint32_t	c1_0 = esp_shim_int_count[1];
	uint32_t	c2_0 = esp_shim_int_count[2];

	(void) tslp_tsk(1000000);	/* 1s観測窓 */

	syslog(LOG_NOTICE,
		   "bt_smoke_c5: intr rate/1s line1=%d line2=%d (storm threshold ~ >>1000/s)",
		   (int_t)(esp_shim_int_count[1] - c1_0),
		   (int_t)(esp_shim_int_count[2] - c2_0));
}

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	esp_err_t					err;
	uint8_t						hci_reset[4] = { 0x01, 0x03, 0x0C, 0x00 };
	int							retry;

	(void) exinf;

	esp_shim_initialize();

	/*
	 *  実施13のICGアンロック．esp_bt_controller_init()より前に呼ぶこと
	 *  （cold boot時のPHY初期化ハング対策．bt/bt_shim.c参照）．
	 */
	esp_shim_bt_clock_init();

	syslog(LOG_NOTICE, "bt_smoke_c5: esp_bt_controller_init");

	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c5: esp_bt_controller_init -> %d", (int_t) err);
		return;
	}
	syslog(LOG_NOTICE, "bt_smoke_c5: esp_bt_controller_init OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	syslog(LOG_NOTICE, "bt_smoke_c5: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c5: esp_bt_controller_enable -> %d", (int_t) err);
		report_intr_trace();
		report_apm_latch();
		return;
	}
	syslog(LOG_NOTICE, "bt_smoke_c5: esp_bt_controller_enable OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	report_intr_trace();
	report_apm_latch();
	report_intr_rate();

	err = esp_vhci_host_register_callback(&vhci_cb);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c5: esp_vhci_host_register_callback -> %d",
			   (int_t) err);
		return;
	}

	syslog(LOG_NOTICE, "bt_smoke_c5: controller enabled, sending HCI Reset");
	for (retry = 0; retry < 50 && !esp_vhci_host_check_send_available(); retry++) {
		(void) tslp_tsk(100000);	/* 100ms */
	}
	esp_vhci_host_send_packet(hci_reset, sizeof(hci_reset));

	for (retry = 0; retry < 30 && !hci_reset_done; retry++) {
		(void) tslp_tsk(200000);	/* 200ms */
	}

	if (hci_reset_done) {
		syslog(LOG_NOTICE, "bt_smoke_c5: Phase D-1 milestone reached");
	}
	else {
		syslog(LOG_NOTICE, "bt_smoke_c5: FAILED (no HCI Command Complete)");
	}

	report_apm_latch();
	syslog(LOG_NOTICE, "bt_smoke_c5: heap free=%u", (uint_t) esp_shim_heap_free_size());
	syslog(LOG_NOTICE, "bt_smoke_c5: done");
}
