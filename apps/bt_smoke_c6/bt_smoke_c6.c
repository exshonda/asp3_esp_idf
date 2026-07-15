/*
 *  Bluetooth（BLE）コントローラ起動＋VHCIループバックのスモークテスト
 *  （ESP32-C6．Phase D-1／BLE実施01）
 *
 *  apps/bt_smoke（C3）と同じ判定基準：esp_bt_controller_init→enable→
 *  VHCI受信コールバック登録→HCI Resetコマンドを送信し，Command
 *  Complete応答が返ることを確認する．ホストスタック（NimBLE）は
 *  D-2aの対象で本デモには含めない．
 *
 *  C3との違い：C6/C5世代コントローラはesp_bt_controller_config_tの
 *  形状が別物（新世代）で，Kconfig相当のCONFIG_*群（esp_bt.cmakeで
 *  定義済み）を前提にBT_CONTROLLER_INIT_CONFIG_DEFAULT()マクロが
 *  正しく展開できる設計になっている．C3のように全フィールドを
 *  手動で埋めると，値の取り違え（emi.c:164の教訓）を再び踏むリスクが
 *  あるため，本実装はマクロを使う（設計書・advisorレビューの指摘）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <sil.h>
#include "bt_smoke_c6.h"

#include "esp_bt.h"
#include "esp_shim.h"

/*
 *  実施91のICGアンロックをBTクロック初期化の一部として呼ぶ（bt/bt_shim.c）．
 *  esp_bt_controller_init() より前に呼ぶ必要がある．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  多重登録トレース（bt/bt_shim.cのesp_os_intr_allocが記録．
 *  C6 LP_AON STORE7相当＝usb-reset生存，前回boot残値はマーカ0xA1で
 *  判別．アドレスはbt_shim.cのBT_INTR_TRACE_REGと一致させること）．
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

/*
 *  HP_APM M0-M3例外ラッチ（実施87/88と同一レジスタ配置．
 *  target_kernel_impl.cのesp32c6_r87_apm_unblock()がブート時に一度
 *  クリアしているため，ここで非0が読めればBT有効化後の新規違反）．
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

	syslog(LOG_NOTICE, "bt_smoke_c6: VHCI recv %d bytes", (int_t) len);
	for (i = 0; i < (int) len && i < 16; i++) {
		syslog(LOG_NOTICE, "bt_smoke_c6:   [%d] = 0x%02x", i, (int_t) data[i]);
	}
	/*
	 *  HCI Command Complete（packet type 0x04・event code 0x0E）を
	 *  検出できればコントローラ生存＋VHCI往復の証明になる
	 */
	if (len >= 2 && data[0] == 0x04 && data[1] == 0x0E) {
		syslog(LOG_NOTICE,
			   "bt_smoke_c6: HCI Command Complete received -> "
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
			   "bt_smoke_c6: intr trace = 0x%08x (nalloc=%d src1=%d src2=%d)",
			   (int_t) trace, (int_t)((trace >> 16) & 0xFFU),
			   (int_t)((trace >> 8) & 0xFFU), (int_t)(trace & 0xFFU));
	}
	else {
		syslog(LOG_NOTICE, "bt_smoke_c6: intr trace not recorded (0x%08x)",
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
			syslog(LOG_ERROR, "bt_smoke_c6: HP_APM M%d exception latch SET (0x%08x)",
				   (int_t) i, (int_t) v);
		}
	}
	if (!any) {
		syslog(LOG_NOTICE, "bt_smoke_c6: HP_APM M0-M3 exception latch clear (BT path OK)");
	}
}

static void
report_intr_rate(void)
{
	uint32_t	c1_0 = esp_shim_int_count[1];
	uint32_t	c2_0 = esp_shim_int_count[2];

	(void) tslp_tsk(1000000);	/* 1s観測窓 */

	syslog(LOG_NOTICE,
		   "bt_smoke_c6: intr rate/1s line1=%d line2=%d (storm threshold ~ >>1000/s)",
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
	 *  実施91のICGアンロック．esp_bt_controller_init()より前に呼ぶこと
	 *  （coldブート時のPHY初期化ハング対策．bt/bt_shim.c参照）．
	 */
	esp_shim_bt_clock_init();

#ifdef TOPPERS_ESP32C6_BT_REGI2C_TRACE
	/*
	 *  §18：RF-cal regi2c トレースを controller_init より前に仕込む
	 *  （register_chipv7_phy の synth 位相 write 列を捕捉．
	 *   ESP32C6_BT_REGI2C_TRACE=ON でのみ有効・既定 OFF＝非回帰）．
	 */
	{
		extern void esp_bt_regi2c_trace_install(void);
		esp_bt_regi2c_trace_install();
	}
#endif

	syslog(LOG_NOTICE, "bt_smoke_c6: esp_bt_controller_init");

	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c6: esp_bt_controller_init -> %d", (int_t) err);
		return;
	}
	syslog(LOG_NOTICE, "bt_smoke_c6: esp_bt_controller_init OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	syslog(LOG_NOTICE, "bt_smoke_c6: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c6: esp_bt_controller_enable -> %d", (int_t) err);
		report_intr_trace();
		report_apm_latch();
		return;
	}
	syslog(LOG_NOTICE, "bt_smoke_c6: esp_bt_controller_enable OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	report_intr_trace();
	report_apm_latch();
	report_intr_rate();

	err = esp_vhci_host_register_callback(&vhci_cb);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "bt_smoke_c6: esp_vhci_host_register_callback -> %d",
			   (int_t) err);
		return;
	}

	syslog(LOG_NOTICE, "bt_smoke_c6: controller enabled, sending HCI Reset");
	for (retry = 0; retry < 50 && !esp_vhci_host_check_send_available(); retry++) {
		(void) tslp_tsk(100000);	/* 100ms */
	}
	esp_vhci_host_send_packet(hci_reset, sizeof(hci_reset));

	for (retry = 0; retry < 30 && !hci_reset_done; retry++) {
		(void) tslp_tsk(200000);	/* 200ms */
	}

	if (hci_reset_done) {
		syslog(LOG_NOTICE, "bt_smoke_c6: Phase D-1 milestone reached");
	}
	else {
		syslog(LOG_NOTICE, "bt_smoke_c6: FAILED (no HCI Command Complete)");
	}

	report_apm_latch();
	syslog(LOG_NOTICE, "bt_smoke_c6: heap free=%u", (uint_t) esp_shim_heap_free_size());
	syslog(LOG_NOTICE, "bt_smoke_c6: done");
}
