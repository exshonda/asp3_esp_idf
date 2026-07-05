/*
 *  Wi-Fiスキャンデモ（ASP3＋esp_wifi blob＋os_adapter shim）
 *
 *  esp_wifi_init→start→scanを実行し，見つかったAPのSSID一覧を
 *  syslogへ出力する（Phase B-2aの動作確認．docs/wifi-shim.md）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_scan.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_trace.h"

/*
 *  スキャン完了通知（esp_event_shim経由）
 */
static ID	main_tskid;
static volatile bool_t scan_done;

/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
static volatile uint32_t promisc_rx_count;

/*
 *  DIAGNOSTIC (temporary): g_ic（libnet80211.aのグローバル状態構造体．
 *  nmで実機アドレス確認済み＝0x408476b0）のoffset 497/499を直接
 *  ピークする．wifi_start_process/wifi_set_promis_processが
 *  wifi_hw_start呼出しをガードする条件がここにある（実施12/13参照）．
 */
#define DIAG_G_IC_BASE 0x408476b0UL
static uint8_t
diag_g_ic_byte(unsigned int off)
{
	return(*(volatile uint8_t *)(DIAG_G_IC_BASE + off));
}

#define DIAG_G_WIFI_NVS_ADDR 0x40800890UL
static uint32_t
diag_wifi_nvs_ptr(void)
{
	return(*(volatile uint32_t *)DIAG_G_WIFI_NVS_ADDR);
}

static uint8_t
diag_wifi_nvs_byte0(void)
{
	uint32_t	p = diag_wifi_nvs_ptr();
	if (p == 0U) {
		return(0xEEU);	/* NULLポインタの目印 */
	}
	return(*(volatile uint8_t *)p);
}

static void
promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
	(void) buf; (void) type;
	promisc_rx_count++;
}

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base; (void) data;
	if (id == WIFI_EVENT_SCAN_DONE) {
		scan_done = true;
		(void) wup_tsk(main_tskid);
	}
}

void
main_task(EXINF exinf)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	uint16_t			num;
	wifi_ap_record_t	*recs;
	esp_err_t			err;
	uint16_t			i;

	(void) exinf;
	(void) get_tid(&main_tskid);

	syslog(LOG_NOTICE, "wifi_scan: initializing shim");
	esp_shim_initialize();
	wifi_trace_reset();
	wifi_regi2c_reset();	/* DIAGNOSTIC (temporary，実施23／Priority 2) */
	wifi_regi2c_patch_install();	/* DIAGNOSTIC（実施23）：PHY初期化前に必ずインストール */
	wifi_taskdelay_reset();	/* DIAGNOSTIC（実施26／タイミング感度調査） */

	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);

	esp_shim_coex_adapter_register();

#ifdef WIFI_SCAN_PREINIT_SPIN
	/*
	 *  DIAGNOSTIC（一時的，実施31）：esp_wifi_init()直前でスピンし，
	 *  JTAGで全域レジスタダンプを取るための同期点．実験後にrevert．
	 */
	syslog(LOG_NOTICE, "wifi_scan: PREINIT_SPIN reached, halting here");
	while (true) {
		(void) tslp_tsk(1000000);
	}
#endif /* WIFI_SCAN_PREINIT_SPIN */

	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init -> %d", (int_t)err);
	if (err != 0) {
		return;
	}
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());

	(void) esp_wifi_set_mode(WIFI_MODE_STA);
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-set_mode g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
	(void) esp_wifi_set_ps(WIFI_PS_NONE);

	err = esp_wifi_start();
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_start -> %d", (int_t)err);
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());
	if (err != 0) {
		return;
	}

	/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
	{
		err = esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous_rx_cb -> %d",
			   (int_t)err);
		err = esp_wifi_set_promiscuous(true);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous(true) -> %d",
			   (int_t)err);
		(void) tslp_tsk(3000000);	/* 3秒間，周辺の電波を受信できるか観測 */
		syslog(LOG_NOTICE, "wifi_scan: DIAG promisc_rx_count=%d",
			   (int_t)promisc_rx_count);
		(void) esp_wifi_set_promiscuous(false);
	}

	wifi_regsnap_reset();	/* DIAGNOSTIC (temporary, Priority 2) */
	err = esp_wifi_scan_start(NULL, false);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_scan_start -> %d", (int_t)err);

	while (!scan_done) {
		(void) tslp_tsk(1000000);	/* SCAN_DONEを待つ（最大繰返し） */
	}
	wifi_trace_dump_counts();	/* DIAGNOSTIC（実施20）：syslogバースト・ロス回避のため先に集計版 */
	wifi_regi2c_dump_count();	/* DIAGNOSTIC（実施23）：同上，先に集計版 */
	wifi_trace_dump();	/* DIAGNOSTIC (temporary): scan完了後まで延長して捕捉 */
	wifi_regsnap_dump();	/* DIAGNOSTIC (temporary, Priority 2) */
	wifi_regi2c_dump();	/* DIAGNOSTIC（実施23／Priority 2） */
	wifi_taskdelay_dump();	/* DIAGNOSTIC（実施26／タイミング感度調査） */

	num = 20;
	recs = (wifi_ap_record_t *)
				esp_shim_calloc(num, sizeof(wifi_ap_record_t));
	if (recs == NULL) {
		return;
	}
	err = esp_wifi_scan_get_ap_records(&num, recs);
	syslog(LOG_NOTICE, "wifi_scan: %d APs found (err=%d)",
		   (int_t)num, (int_t)err);
	for (i = 0; i < num; i++) {
		syslog(LOG_NOTICE, "  [%d] %s (rssi=%d ch=%d)",
			   (int_t)i, (const char *)recs[i].ssid,
			   (int_t)recs[i].rssi, (int_t)recs[i].primary);
	}
	esp_shim_free(recs);
	syslog(LOG_NOTICE, "wifi_scan: done");
}
