/*
 *  C5負荷試験デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  apps/load_test_c3（実施1，docs/load-test-c3c5c6.md）をC5向けに複製
 *  （実施46）．apps/tcp_socket_echo・apps/udp_socket_echoのWi-Fi接続＋
 *  DHCP待ちボイラープレートをそのまま流用．TCP echo（port 8）とUDP
 *  echo（port 9）を同一バイナリで同時に起動し，monitor_taskが
 *  esp_shim_heap_free_size()と累積送受信バイト数を周期syslog出力する．
 *
 *  docs/s3-throughput-findings-for-c6.md が報告する2つのOSAシム潜在
 *  欠陥（(A)キューmalloc／(B)TXバッファ動的malloc）をC5で検証する
 *  ための持続負荷試験に使う．欠陥Bは -DLOAD_TEST_STATIC_TXBUF で
 *  opt-in（未指定時は動的＝修正前の挙動）．
 *
 *  fault_capture_handler（apps/wifi_dhcp実施45由来）を安全網として
 *  同梱：本ラウンドの主目的はC3で発見された「負荷誘発リンク完全停止」
 *  の再現有無の確認であり，停止がCPU例外由来である可能性は事前に
 *  排除できないため，DEF_EXCで捕捉できる構えにしておく（フォルトが
 *  起きなければ無害．load_test_c5.cfg参照）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "load_test_c5.h"
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

/*
 *  CPU例外フォルト捕捉ハンドラ（apps/wifi_dhcp実施45と同一機構の
 *  安全網移植．docs/c5-bringup.md実施06/08/45参照）．RTC-RAM
 *  0x50000000〜（magic 0xFA017C05）へmepc/mcause/mtval＋汎用レジスタ
 *  ＋callee-saved(s0-s3)/spを保存し，無限ループで凍結する．
 */
#define FAULTCAP_BASE		((volatile uint32_t *)0x50000000U)
#define FAULTCAP_MAGIC		0xFA017C05U

void
fault_capture_handler(void *p_excinf)
{
	register uint32_t reg_sp __asm__("sp");
	register uint32_t reg_s0 __asm__("s0");
	register uint32_t reg_s1 __asm__("s1");
	register uint32_t reg_s2 __asm__("s2");
	register uint32_t reg_s3 __asm__("s3");
	T_EXCINF	*p = (T_EXCINF *) p_excinf;
	uint32_t	mcause, mtval, mepc;
	uint32_t	sp_v = reg_sp, s0_v = reg_s0, s1_v = reg_s1,
				s2_v = reg_s2, s3_v = reg_s3;

	__asm__ volatile("csrr %0, mcause" : "=r"(mcause));
	__asm__ volatile("csrr %0, mtval"  : "=r"(mtval));
	__asm__ volatile("csrr %0, mepc"   : "=r"(mepc));

	FAULTCAP_BASE[25] = sp_v;
	FAULTCAP_BASE[26] = s0_v;
	FAULTCAP_BASE[27] = s1_v;
	FAULTCAP_BASE[28] = s2_v;
	FAULTCAP_BASE[29] = s3_v;

	FAULTCAP_BASE[0]  = FAULTCAP_MAGIC;
	FAULTCAP_BASE[1]  = mcause;
	FAULTCAP_BASE[2]  = mtval;
	FAULTCAP_BASE[3]  = mepc;
	FAULTCAP_BASE[4]  = (uint32_t) p->pc;
	FAULTCAP_BASE[5]  = (uint32_t) p->ra;
	FAULTCAP_BASE[6]  = (uint32_t) p->a0;
	FAULTCAP_BASE[7]  = (uint32_t) p->a1;
	FAULTCAP_BASE[8]  = (uint32_t) p->a2;
	FAULTCAP_BASE[9]  = (uint32_t) p->a3;
	FAULTCAP_BASE[10] = (uint32_t) p->a4;
	FAULTCAP_BASE[11] = (uint32_t) p->a5;
	FAULTCAP_BASE[12] = (uint32_t) p->t0;
	FAULTCAP_BASE[13] = (uint32_t) p->t1;
	FAULTCAP_BASE[14] = (uint32_t) p->t2;
	FAULTCAP_BASE[15] = (uint32_t) p->tp;
	FAULTCAP_BASE[16] = (uint32_t) p->mstatus;
#ifndef __riscv_32e
	FAULTCAP_BASE[17] = (uint32_t) p->t3;
	FAULTCAP_BASE[18] = (uint32_t) p->t4;
	FAULTCAP_BASE[19] = (uint32_t) p->t5;
	FAULTCAP_BASE[20] = (uint32_t) p->t6;
	FAULTCAP_BASE[21] = (uint32_t) p->a6;
	FAULTCAP_BASE[22] = (uint32_t) p->a7;
#endif /* !__riscv_32e */
	FAULTCAP_BASE[23] = (uint32_t) p->intpri;
	FAULTCAP_BASE[24] = p->exncnt;

	for (;;) {
		/*  凍結．JTAG（非侵襲attach）またはesptool read_memで回収する。 */
	}
}

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
	 *  欠陥B対策（S3 commit dd7a76d移植．C3実施1と同一判断基準）：
	 *  TXバッファを静的プール化．既定（tx_buf_type=1・
	 *  static_tx_buf_num=0）は毎パケット~1.7KBをシムヒープから
	 *  malloc/freeし，持続高レート送信で断片化を招く。
	 */
	cfg.tx_buf_type = 0;
	cfg.static_tx_buf_num = 16;
	syslog(LOG_NOTICE,
		   "load_test_c5: LOAD_TEST_STATIC_TXBUF=ON "
		   "(tx_buf_type=0, static_tx_buf_num=16)");
#else
	syslog(LOG_NOTICE,
		   "load_test_c5: LOAD_TEST_STATIC_TXBUF=OFF "
		   "(default dynamic tx_buf)");
#endif

	syslog(LOG_NOTICE, "load_test_c5: esp_wifi_init");
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
	syslog(LOG_NOTICE, "load_test_c5: SSID='%s'", WIFI_SSID);

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
	syslog(LOG_NOTICE, "load_test_c5: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "load_test_c5: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "load_test_c5: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "load_test_c5: FAILED (timeout)");
		return;
	}

	/*
	 *  接続先APの実測情報（チャンネル・RSSI）を記録する（帯域の証跡用．
	 *  wifi_dhcp実施45と同一のログ形式）。
	 */
	{
		wifi_ap_record_t	ap;

		if (esp_wifi_sta_get_ap_info(&ap) == 0) {
			syslog(LOG_NOTICE,
				   "load_test_c5: AP info: channel=%d rssi=%d",
				   (int_t) ap.primary, (int_t) ap.rssi);
		}
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "load_test_c5: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "load_test_c5: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "load_test_c5: main_task done "
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
			syslog(LOG_NOTICE, "load_test_c5: tcp send() error (wlen=%d)",
				   (int_t) wlen);
			g_tcp_send_errors++;
			break;
		}
		total += (uint32_t) rlen;
		g_tcp_bytes_echoed += (uint32_t) rlen;
	}
	syslog(LOG_NOTICE, "load_test_c5: tcp client disconnected "
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
		   "load_test_c5: starting TCP echo server (port %d)",
		   (int_t) TCP_ECHO_PORT);

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp socket() failed (%d)",
			   (int_t) listen_sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(TCP_ECHO_PORT);

	if (bind(listen_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp bind() failed");
		(void) close(listen_sock);
		return;
	}

	if (listen(listen_sock, 1) < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp listen() failed");
		(void) close(listen_sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c5: tcp listening on 0.0.0.0:%d",
		   (int_t) TCP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		conn_sock = accept(listen_sock, (struct sockaddr *) &peer, &peerlen);
		if (conn_sock < 0) {
			syslog(LOG_NOTICE, "load_test_c5: tcp accept() failed");
			continue;
		}

		pip = peer.sin_addr.s_addr;
		g_tcp_sessions++;
		syslog(LOG_NOTICE,
			   "load_test_c5: tcp client connected %d.%d.%d.%d:%d",
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
		   "load_test_c5: starting UDP echo server (port %d)",
		   (int_t) UDP_ECHO_PORT);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		syslog(LOG_ERROR, "load_test_c5: udp socket() failed (%d)",
			   (int_t) sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(UDP_ECHO_PORT);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c5: udp bind() failed");
		(void) close(sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c5: udp listening on 0.0.0.0:%d (UDP)",
		   (int_t) UDP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		rlen = recvfrom(sock, buf, sizeof(buf), 0,
						 (struct sockaddr *) &peer, &peerlen);
		if (rlen < 0) {
			syslog(LOG_NOTICE, "load_test_c5: udp recvfrom() failed");
			continue;
		}
		g_udp_datagrams++;

		pip = peer.sin_addr.s_addr;
		(void) pip;

		wlen = sendto(sock, buf, (size_t) rlen, 0,
					  (struct sockaddr *) &peer, peerlen);
		if (wlen != rlen) {
			syslog(LOG_NOTICE, "load_test_c5: udp sendto() error (wlen=%d)",
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
			   "load_test_c5: MON uptime=%us heap_free=%u "
			   "tcp_bytes=%u tcp_sessions=%u tcp_errs=%u",
			   (uint_t) uptime_s, (uint_t) esp_shim_heap_free_size(),
			   (uint_t) g_tcp_bytes_echoed, (uint_t) g_tcp_sessions,
			   (uint_t) g_tcp_send_errors);
		syslog(LOG_NOTICE,
			   "load_test_c5: MON uptime=%us "
			   "udp_bytes=%u udp_dgrams=%u udp_errs=%u",
			   (uint_t) uptime_s,
			   (uint_t) g_udp_bytes_echoed, (uint_t) g_udp_datagrams,
			   (uint_t) g_udp_send_errors);
	}
}
