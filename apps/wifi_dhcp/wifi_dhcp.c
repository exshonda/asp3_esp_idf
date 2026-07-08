/*
 *  Wi-Fi DHCP接続デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  wifi_connect（Phase B-2b）にlwIP（Phase C）を重ね，WPA2 AP接続後に
 *  DHCPでIPアドレスを取得し，デフォルトゲートウェイへraw APIでping
 *  する（実通信の確認）．本アプリ自体はlwIP APIには一切触れず，
 *  netif_esp32c3_notify_link()でnet_task（net/netif_esp32c3.c）へ
 *  リンク状態を伝えるのみ（単一実行文脈の原則．設計はdocs/
 *  tcpip-integration.md）．接続先は -DWIFI_SSID=... -DWIFI_PASSWORD=...
 *  で指定する．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_dhcp.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

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
		netif_esp32c3_notify_link(true);
		(void) wup_tsk(main_tskid);
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		{
			wifi_event_sta_disconnected_t *d =
					(wifi_event_sta_disconnected_t *) data;
			syslog(LOG_NOTICE, "event: STA_DISCONNECTED reason=%d",
				   (int_t)(d != NULL ? d->reason : 0));
			conn_state = -1;
			netif_esp32c3_notify_link(false);
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
	uint32_t			ip;

	(void) exinf;
	(void) get_tid(&main_tskid);

#ifdef WIFI_IDLE_SECS
	/*
	 *  ブートカウンタ（RTC FAST RAM 0x50000008）を毎起動でインクリメント．
	 *  アイドル計測窓の途中でチップがリセット/再起動すると >1 になるので，
	 *  「真のフリーズ（boot=1・カウンタ凍結）」と「リセットループ（boot>1）」
	 *  を事後に区別できる（計測アーチファクト排除）．
	 */
	{
		volatile uint32_t *rtc_boot = (volatile uint32_t *)0x50000008u;
		volatile uint32_t *rtc_bmagic = (volatile uint32_t *)0x5000000Cu;
		if (*rtc_bmagic != 0xB007B007u) {	/* コールドスタート初期化 */
			*rtc_bmagic = 0xB007B007u;
			*rtc_boot = 0u;
		}
		*rtc_boot = *rtc_boot + 1u;
	}
#endif

	netif_esp32c3_start();
	esp_shim_initialize();
	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);
	esp_shim_coex_adapter_register();

	syslog(LOG_NOTICE, "wifi_dhcp: esp_wifi_init");
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
	syslog(LOG_NOTICE, "wifi_dhcp: SSID='%s'", WIFI_SSID);

	err = esp_wifi_start();
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_start -> %d", (int_t)err);
		return;
	}

	/*
	 *  STA_START後にmain_task文脈からconnectする（イベントハンドラ＝
	 *  WiFiタスク文脈からのconnect呼出しを避ける．Phase B-2bの知見）
	 */
	(void) tslp_tsk(2000000);
	err = esp_wifi_connect();
	syslog(LOG_NOTICE, "wifi_dhcp: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "wifi_dhcp: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "wifi_dhcp: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "wifi_dhcp: FAILED (timeout)");
		return;
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ（net_task側で処理．
	 *  本タスクは読み出し専用ポーリングのみ）
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "wifi_dhcp: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "wifi_dhcp: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "wifi_dhcp: done (ping result logged by net_task)");

#ifdef WIFI_IDLE_SECS
	/*
	 *  【S3→C3 アイドルフリーズ再現テスト】
	 *  GOT IP後，送受信を一切行わず WIFI_IDLE_SECS 秒だけ接続維持する
	 *  純アイドル経路．生存秒カウンタを RTC FAST RAM（0x50000000＝
	 *  リンカ未使用・リセット非依存）へ毎秒書き，syslogでも二重に出す．
	 *  カウンタが増え続ける限り生存＝停止した秒が凍結時刻．
	 *  通常ビルドでは -DWIFI_IDLE_SECS 未定義のため一切影響しない．
	 *  詳細: docs/s3-idle-freeze-findings-for-c3.md
	 */
	{
		volatile uint32_t *rtc_alive = (volatile uint32_t *)0x50000000u;
		volatile uint32_t *rtc_magic = (volatile uint32_t *)0x50000004u;
		int idle_t;

		*rtc_magic = 0xA11EA11Eu;	/* 生存中マジック */
		*rtc_alive = 0u;
		syslog(LOG_NOTICE, "wifi_dhcp: IDLE test start (%d s), pure idle",
			   (int_t)WIFI_IDLE_SECS);
		for (idle_t = 1; idle_t <= WIFI_IDLE_SECS; idle_t++) {
			(void) tslp_tsk(1000000);	/* 1秒（送受信なし） */
			*rtc_alive = (uint32_t)idle_t;
			syslog(LOG_NOTICE, "idle t=%d s alive", (int_t)idle_t);
		}
		syslog(LOG_NOTICE, "wifi_dhcp: IDLE test SURVIVED %d s",
			   (int_t)WIFI_IDLE_SECS);
	}
#endif /* WIFI_IDLE_SECS */
}
