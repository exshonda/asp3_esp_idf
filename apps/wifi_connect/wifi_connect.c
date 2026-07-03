/*
 *  Wi-Fi AP接続デモ（ASP3＋esp_wifi blob＋os_adapter shim）
 *
 *  esp_wifi_init→start→connectを実行し，WPA2 APへ接続してイベント
 *  （STA_CONNECTED／DISCONNECTED）を確認する（Phase B-2b）．
 *  接続先は -DWIFI_SSID=... -DWIFI_PASSWORD=... で指定する
 *  （TCP/IPスタックはスコープ外＝L2接続確認まで）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_connect.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

static ID	main_tskid;
static volatile int32_t	conn_state;	/* 0=待機 1=接続 -1=切断 */

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base;

	switch (id) {
	case WIFI_EVENT_STA_START:
		syslog(LOG_NOTICE, "event: STA_START");
		conn_state = 2;		/* 2=STA_START受信（main_taskがconnect） */
		(void) wup_tsk(main_tskid);
		break;
	case WIFI_EVENT_STA_CONNECTED:
		syslog(LOG_NOTICE, "event: STA_CONNECTED");
		conn_state = 1;
		(void) wup_tsk(main_tskid);
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		{
			wifi_event_sta_disconnected_t *d =
					(wifi_event_sta_disconnected_t *) data;
			syslog(LOG_NOTICE, "event: STA_DISCONNECTED reason=%d",
				   (int_t)(d != NULL ? d->reason : 0));
			conn_state = -1;
			(void) wup_tsk(main_tskid);
		}
		break;
	default:
		syslog(LOG_NOTICE, "event: WIFI_EVENT id=%d", (int_t)id);
		break;
	}
}

void
main_task(EXINF exinf)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t		wc;
	esp_err_t			err;
	int					retry;

	(void) exinf;
	(void) get_tid(&main_tskid);

	esp_shim_initialize();
	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);
	esp_shim_coex_adapter_register();

	syslog(LOG_NOTICE, "wifi_connect: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_init -> %d", (int_t)err);
		return;
	}

	(void) esp_wifi_set_mode(WIFI_MODE_STA);
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
	(void) esp_wifi_set_ps(WIFI_PS_NONE);

	/*
	 *  接続先の設定（SSID／パスワード）
	 */
	memset(&wc, 0, sizeof(wc));
	strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
	strncpy((char *)wc.sta.password, WIFI_PASSWORD,
			sizeof(wc.sta.password) - 1);
	(void) esp_wifi_set_config(WIFI_IF_STA, &wc);
	syslog(LOG_NOTICE, "wifi_connect: SSID='%s'", WIFI_SSID);

	err = esp_wifi_start();
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_start -> %d", (int_t)err);
		return;
	}

	/*
	 *  STA_START後にmain_task文脈からconnectする（イベントハンドラ＝
	 *  WiFiタスク文脈からのconnect呼出しを避ける）
	 */
	(void) tslp_tsk(2000000);
	err = esp_wifi_connect();
	syslog(LOG_NOTICE, "wifi_connect: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			wifi_ap_record_t	ap;
			if (esp_wifi_sta_get_ap_info(&ap) == 0) {
				syslog(LOG_NOTICE,
					   "wifi_connect: CONNECTED to '%s' rssi=%d ch=%d",
					   (const char *)ap.ssid, (int_t)ap.rssi,
					   (int_t)ap.primary);
			}
			else {
				syslog(LOG_NOTICE, "wifi_connect: CONNECTED");
			}
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "wifi_connect: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "wifi_connect: FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "wifi_connect: done");
}
