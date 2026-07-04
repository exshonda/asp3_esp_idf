/*
 *  BSDソケットTCPクライアントデモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  wifi_dhcp／tcp_socket_echo（Phase C）のWi-Fi接続＋DHCP待ちボイラー
 *  プレートを流用し，DHCP完了後にBSDソケットAPI（socket/connect/send/
 *  recv/close，lwip/sockets.h＝LWIP_SOCKET＋LWIP_COMPAT_SOCKETS）で
 *  外部サーバ（-DSERVER_ADDR=... -DSERVER_PORT=...）へ接続する
 *  クライアントデモ．apps/tcp_socket_echo（サーバ側）と対になる，
 *  クライアント側の使い方の実証が目的．
 *
 *  接続先サーバは開発機等で `nc -l -p 9000` のように待受けを立てて
 *  おく（何か送り返すechoサーバである必要はない．受信できなくても
 *  送信自体の成功／失敗はログで分かる）．
 *
 *  client_task自身はesp_wifi/netif_esp32c3には触れず，
 *  netif_esp32c3_get_ipaddr()のポーリングでDHCP完了を検知してから
 *  socket()を呼ぶ（main_taskのDHCP待ちと同じ方式）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "tcp_socket_client.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

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

	netif_esp32c3_start();
	esp_shim_initialize();
	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);
	esp_shim_coex_adapter_register();

	syslog(LOG_NOTICE, "tcp_socket_client: esp_wifi_init");
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
	syslog(LOG_NOTICE, "tcp_socket_client: SSID='%s'", WIFI_SSID);

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
	syslog(LOG_NOTICE, "tcp_socket_client: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "tcp_socket_client: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "tcp_socket_client: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "tcp_socket_client: FAILED (timeout)");
		return;
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ（client_taskも独立に同じ
	 *  ポーリングを行いソケット接続を開始する）
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "tcp_socket_client: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "tcp_socket_client: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "tcp_socket_client: main_task done "
		   "(connection run by client_task)");
}

/*
 *  client_task：main_taskとは別のASP3タスク文脈でBSDソケットAPI
 *  （socket/connect/send/recv/close）を直接呼び出す．
 *  SERVER_ADDR:SERVER_PORTへ接続し，メッセージを5回送って応答を
 *  ログ出力する（応答が無いサーバ（例：`nc -l`のみ）でも送信自体の
 *  成否は分かる）．
 */
void
client_task(EXINF exinf)
{
	int					sock;
	struct sockaddr_in	addr;
	char				sendbuf[64];
	char				recvbuf[128];
	ssize_t				slen, rlen;
	int					i;
	uint32_t			ip;

	(void) exinf;

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ
	 */
	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE, "tcp_socket_client: connecting to %s:%d",
		   SERVER_ADDR, (int_t) SERVER_PORT);

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		syslog(LOG_ERROR, "tcp_socket_client: socket() failed (%d)",
			   (int_t) sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

	if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "tcp_socket_client: connect() failed");
		(void) close(sock);
		return;
	}
	syslog(LOG_NOTICE, "tcp_socket_client: connected");

	/*
	 *  recv()が無応答サーバ（`nc -l`等）でブロックし続けないよう
	 *  受信タイムアウトを設定する（LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1
	 *  なのでミリ秒int指定）
	 */
	{
		int timeout_ms = 3000;
		(void) setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
						   &timeout_ms, sizeof(timeout_ms));
	}

	for (i = 0; i < 5; i++) {
		slen = snprintf(sendbuf, sizeof(sendbuf),
						 "hello from asp3 tcp_socket_client #%d\n", i);
		if (send(sock, sendbuf, (size_t) slen, 0) != slen) {
			syslog(LOG_ERROR, "tcp_socket_client: send() error");
			break;
		}
		syslog(LOG_NOTICE, "tcp_socket_client: sent %d bytes", (int_t) slen);

		rlen = recv(sock, recvbuf, sizeof(recvbuf) - 1, 0);
		if (rlen > 0) {
			recvbuf[rlen] = '\0';
			syslog(LOG_NOTICE, "tcp_socket_client: recv %d bytes: %s",
				   (int_t) rlen, recvbuf);
		}
		else if (rlen == 0) {
			syslog(LOG_NOTICE, "tcp_socket_client: peer closed");
			break;
		}
		else {
			syslog(LOG_NOTICE, "tcp_socket_client: recv() no reply (%d)",
				   (int_t) rlen);
		}

		(void) tslp_tsk(2000000);	/* 2秒 */
	}

	(void) close(sock);
	syslog(LOG_NOTICE, "tcp_socket_client: done");
}
