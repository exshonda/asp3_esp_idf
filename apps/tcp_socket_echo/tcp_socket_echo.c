/*
 *  BSDソケットTCPechoデモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  wifi_dhcp（Phase C）のWi-Fi接続＋DHCP待ちボイラープレートを流用し，
 *  DHCP完了後にBSDソケットAPI（socket/bind/listen/accept/recv/send/
 *  close，lwip/sockets.h＝LWIP_SOCKET＋LWIP_COMPAT_SOCKETS）による
 *  TCP echoサーバをport 8で起動する（port 7はlwip contribのraw API版
 *  tcpecho_rawが既に使用．net/netif_esp32c3.cのtcpip_thread文脈から
 *  起動）．
 *
 *  本デモの主眼は「lwIP内部スレッド（tcpip_thread）ではない，任意の
 *  ASP3アプリタスク（echo_task）からでも標準BSDソケット名がそのまま
 *  使える」ことの実証にある．echo_task自身はesp_wifi/netif_esp32c3
 *  には触れず，netif_esp32c3_get_ipaddr()のポーリングでDHCP完了を
 *  検知してからsocket()を呼ぶ（main_taskのDHCP待ちと同じ方式．新しい
 *  同期プリミティブは導入しない）．接続先は -DWIFI_SSID=...
 *  -DWIFI_PASSWORD=... で指定する．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "tcp_socket_echo.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

#include "lwip/sockets.h"

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

	syslog(LOG_NOTICE, "tcp_socket_echo: esp_wifi_init");
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
	syslog(LOG_NOTICE, "tcp_socket_echo: SSID='%s'", WIFI_SSID);

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
	syslog(LOG_NOTICE, "tcp_socket_echo: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "tcp_socket_echo: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "tcp_socket_echo: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "tcp_socket_echo: FAILED (timeout)");
		return;
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ（net_task側で処理．
	 *  本タスクは読み出し専用ポーリングのみ．echo_taskも独立に同じ
	 *  ポーリングを行いソケットサーバを開始する）
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "tcp_socket_echo: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "tcp_socket_echo: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "tcp_socket_echo: main_task done "
		   "(echo server run by echo_task)");
}

/*
 *  1接続分のecho処理（recv()した内容をそのままsend()で送り返す．
 *  recv()が0＝相手クローズ／負＝エラーでループを抜ける）
 */
static void
echo_session(int conn_sock)
{
	char	buf[256];
	ssize_t	rlen;
	ssize_t	wlen;
	uint32_t	total = 0;

	for (;;) {
		rlen = recv(conn_sock, buf, sizeof(buf), 0);
		if (rlen <= 0) {
			break;
		}
		wlen = send(conn_sock, buf, (size_t) rlen, 0);
		if (wlen != rlen) {
			syslog(LOG_NOTICE, "tcp_socket_echo: send() error (wlen=%d)",
				   (int_t) wlen);
			break;
		}
		total += (uint32_t) rlen;
	}
	syslog(LOG_NOTICE, "tcp_socket_echo: client disconnected "
		   "(%d bytes echoed)", (int_t) total);
	(void) close(conn_sock);
}

/*
 *  echo_task：main_taskとは別のASP3タスク文脈でBSDソケットAPIを直接
 *  呼び出す．lwIP内部スレッド（tcpip_thread＝net_task）を経由せず，
 *  任意のアプリタスクからsocket()以下の標準API名がそのまま使えること
 *  を示すのが目的．
 */
void
echo_task(EXINF exinf)
{
	int					listen_sock;
	int					conn_sock;
	struct sockaddr_in	addr;
	struct sockaddr_in	peer;
	socklen_t			peerlen;
	uint32_t			ip;
	uint32_t			pip;

	(void) exinf;

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ（main_taskのDHCP待ちと
	 *  同じポーリング方式．新しい同期プリミティブは導入しない）
	 */
	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE,
		   "tcp_socket_echo: starting BSD socket echo server (port %d)",
		   (int_t) ECHO_PORT);

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		syslog(LOG_ERROR, "tcp_socket_echo: socket() failed (%d)",
			   (int_t) listen_sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(ECHO_PORT);

	if (bind(listen_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "tcp_socket_echo: bind() failed");
		(void) close(listen_sock);
		return;
	}

	if (listen(listen_sock, 1) < 0) {
		syslog(LOG_ERROR, "tcp_socket_echo: listen() failed");
		(void) close(listen_sock);
		return;
	}

	syslog(LOG_NOTICE, "tcp_socket_echo: listening on 0.0.0.0:%d",
		   (int_t) ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		conn_sock = accept(listen_sock, (struct sockaddr *) &peer, &peerlen);
		if (conn_sock < 0) {
			syslog(LOG_NOTICE, "tcp_socket_echo: accept() failed");
			continue;
		}

		pip = peer.sin_addr.s_addr;
		syslog(LOG_NOTICE,
			   "tcp_socket_echo: client connected %d.%d.%d.%d:%d",
			   (int_t)(pip & 0xff), (int_t)((pip >> 8) & 0xff),
			   (int_t)((pip >> 16) & 0xff), (int_t)((pip >> 24) & 0xff),
			   (int_t) ntohs(peer.sin_port));

		echo_session(conn_sock);
	}
}
