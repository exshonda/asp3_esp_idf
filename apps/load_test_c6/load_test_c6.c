/*
 *  C6負荷試験デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *  apps/load_test_c3 のコピー（実施90，docs/load-test-c3c5c6.md参照．
 *  net/層はチップ非依存のためソースは無改変で流用できる）．
 *
 *  apps/tcp_socket_echo・apps/udp_socket_echoのWi-Fi接続＋DHCP待ち
 *  ボイラープレートをそのまま流用．TCP echo（port 8）とUDP echo
 *  （port 9）を同一バイナリで同時に起動し，monitor_taskが
 *  esp_shim_heap_free_size()と累積送受信バイト数を周期syslog出力する．
 *
 *  docs/s3-throughput-findings-for-c6.md が報告する2つのOSAシム潜在
 *  欠陥（(A)キューmalloc／(B)TXバッファ動的malloc）をC3で検証する
 *  ための持続負荷試験に使う．欠陥Bは -DLOAD_TEST_STATIC_TXBUF で
 *  opt-in（未指定時は動的＝修正前の挙動）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "load_test_c6.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

#include "lwip/sockets.h"

static ID	main_tskid;
static volatile int32_t	conn_state;	/* 0=待機 1=接続 -1=切断 */

/*
 *  監視用の累積カウンタ（tcp_echo_task/udp_echo_task/mon_taskからのみ
 *  アクセス．32bit単純カウンタでオーバーフローは監視ログの相対値で
 *  判断できるため許容）
 */
static volatile uint32_t	g_tcp_bytes_echoed;
static volatile uint32_t	g_udp_bytes_echoed;
static volatile uint32_t	g_tcp_sessions;
static volatile uint32_t	g_udp_datagrams;
static volatile uint32_t	g_tcp_send_errors;
static volatile uint32_t	g_udp_send_errors;

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

#ifdef LOAD_TEST_STATIC_TXBUF
	/*
	 *  欠陥B対策（S3 commit dd7a76d移植）：TXバッファを静的プール化．
	 *  既定（tx_buf_type=1・static_tx_buf_num=0）は毎パケット~1.7KBを
	 *  シムヒープからmalloc/freeし，持続高レート送信で断片化を招く．
	 */
	cfg.tx_buf_type = 0;
	cfg.static_tx_buf_num = 16;
	syslog(LOG_NOTICE,
		   "load_test_c6: LOAD_TEST_STATIC_TXBUF=ON "
		   "(tx_buf_type=0, static_tx_buf_num=16)");
#else
	syslog(LOG_NOTICE,
		   "load_test_c6: LOAD_TEST_STATIC_TXBUF=OFF "
		   "(default dynamic tx_buf)");
#endif

	syslog(LOG_NOTICE, "load_test_c6: esp_wifi_init");
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
	syslog(LOG_NOTICE, "load_test_c6: SSID='%s'", WIFI_SSID);

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
	syslog(LOG_NOTICE, "load_test_c6: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "load_test_c6: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "load_test_c6: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "load_test_c6: FAILED (timeout)");
		return;
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "load_test_c6: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "load_test_c6: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "load_test_c6: main_task done "
		   "(echo servers run by tcp_echo_task/udp_echo_task)");
}

/*
 *  1接続分のTCP echo処理（recv()した内容をそのままsend()で送り返す）
 */
static void
tcp_echo_session(int conn_sock)
{
	char	buf[1460];	/* TCP_MSS前後を1回のrecvで拾えるサイズ */
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
			syslog(LOG_NOTICE, "load_test_c6: tcp send() error (wlen=%d)",
				   (int_t) wlen);
			g_tcp_send_errors++;
			break;
		}
		total += (uint32_t) rlen;
		g_tcp_bytes_echoed += (uint32_t) rlen;
	}
	syslog(LOG_NOTICE, "load_test_c6: tcp client disconnected "
		   "(%d bytes echoed)", (int_t) total);
	(void) close(conn_sock);
}

void
tcp_echo_task(EXINF exinf)
{
	int					listen_sock;
	int					conn_sock;
	struct sockaddr_in	addr;
	struct sockaddr_in	peer;
	socklen_t			peerlen;
	uint32_t			ip;
	uint32_t			pip;

	(void) exinf;

	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE,
		   "load_test_c6: starting TCP echo server (port %d)",
		   (int_t) TCP_ECHO_PORT);

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		syslog(LOG_ERROR, "load_test_c6: tcp socket() failed (%d)",
			   (int_t) listen_sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(TCP_ECHO_PORT);

	if (bind(listen_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c6: tcp bind() failed");
		(void) close(listen_sock);
		return;
	}

	if (listen(listen_sock, 1) < 0) {
		syslog(LOG_ERROR, "load_test_c6: tcp listen() failed");
		(void) close(listen_sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c6: tcp listening on 0.0.0.0:%d",
		   (int_t) TCP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		conn_sock = accept(listen_sock, (struct sockaddr *) &peer, &peerlen);
		if (conn_sock < 0) {
			syslog(LOG_NOTICE, "load_test_c6: tcp accept() failed");
			continue;
		}

		pip = peer.sin_addr.s_addr;
		g_tcp_sessions++;
		syslog(LOG_NOTICE,
			   "load_test_c6: tcp client connected %d.%d.%d.%d:%d",
			   (int_t)(pip & 0xff), (int_t)((pip >> 8) & 0xff),
			   (int_t)((pip >> 16) & 0xff), (int_t)((pip >> 24) & 0xff),
			   (int_t) ntohs(peer.sin_port));

		tcp_echo_session(conn_sock);
	}
}

void
udp_echo_task(EXINF exinf)
{
	int					sock;
	struct sockaddr_in	addr;
	struct sockaddr_in	peer;
	socklen_t			peerlen;
	char				buf[1460];
	ssize_t				rlen;
	ssize_t				wlen;
	uint32_t			ip;
	uint32_t			pip;

	(void) exinf;

	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE,
		   "load_test_c6: starting UDP echo server (port %d)",
		   (int_t) UDP_ECHO_PORT);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		syslog(LOG_ERROR, "load_test_c6: udp socket() failed (%d)",
			   (int_t) sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(UDP_ECHO_PORT);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c6: udp bind() failed");
		(void) close(sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c6: udp listening on 0.0.0.0:%d (UDP)",
		   (int_t) UDP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		rlen = recvfrom(sock, buf, sizeof(buf), 0,
						 (struct sockaddr *) &peer, &peerlen);
		if (rlen < 0) {
			syslog(LOG_NOTICE, "load_test_c6: udp recvfrom() failed");
			continue;
		}
		g_udp_datagrams++;

		pip = peer.sin_addr.s_addr;
		(void) pip;

		wlen = sendto(sock, buf, (size_t) rlen, 0,
					  (struct sockaddr *) &peer, peerlen);
		if (wlen != rlen) {
			syslog(LOG_NOTICE, "load_test_c6: udp sendto() error (wlen=%d)",
				   (int_t) wlen);
			g_udp_send_errors++;
		} else {
			g_udp_bytes_echoed += (uint32_t) rlen;
		}
	}
}

/*
 *  監視タスク：ヒープ残量・累積カウンタを周期syslog出力．
 *  S3の欠陥A（シムヒープ断片化→malloc失敗）・欠陥B（TXバッファ枯渇）
 *  が起きていれば，heap_free単調減少／send_errors増加として現れる．
 */
void
mon_task(EXINF exinf)
{
	uint32_t	uptime_s = 0;

	(void) exinf;

	for (;;) {
		(void) tslp_tsk((TMO) MON_PERIOD_US);
		uptime_s += (MON_PERIOD_US / 1000000U);
		/*
		 *  syslog()の可変引数はTNUM_LOGPAR=6（先頭はformat文字列自体）
		 *  制約により%変換指定子は最大5個までしか安全に運べない
		 *  （asp3/asp3_core/include/t_syslog.h参照）．8個の値を1行に
		 *  詰めると超過分が文字列として化けるため2行に分割する．
		 */
		syslog(LOG_NOTICE,
			   "load_test_c6: MON uptime=%us heap_free=%u "
			   "tcp_bytes=%u tcp_sessions=%u tcp_errs=%u",
			   (uint_t) uptime_s, (uint_t) esp_shim_heap_free_size(),
			   (uint_t) g_tcp_bytes_echoed, (uint_t) g_tcp_sessions,
			   (uint_t) g_tcp_send_errors);
		syslog(LOG_NOTICE,
			   "load_test_c6: MON uptime=%us "
			   "udp_bytes=%u udp_dgrams=%u udp_errs=%u",
			   (uint_t) uptime_s,
			   (uint_t) g_udp_bytes_echoed, (uint_t) g_udp_datagrams,
			   (uint_t) g_udp_send_errors);
	}
}
