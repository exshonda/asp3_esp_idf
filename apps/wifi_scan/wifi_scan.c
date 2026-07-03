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

/*
 *  スキャン完了通知（esp_event_shim経由）
 */
static ID	main_tskid;
static volatile bool_t scan_done;

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

	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);

	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init -> %d", (int_t)err);
	if (err != 0) {
		return;
	}

	(void) esp_wifi_set_mode(WIFI_MODE_STA);
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);

	err = esp_wifi_start();
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_start -> %d", (int_t)err);
	if (err != 0) {
		return;
	}

	err = esp_wifi_scan_start(NULL, false);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_scan_start -> %d", (int_t)err);

	while (!scan_done) {
		(void) tslp_tsk(1000000);	/* SCAN_DONEを待つ（最大繰返し） */
	}

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
