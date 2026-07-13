/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ESP32-C3 Wi-Fi用lwIP netif実装（ASP3．NO_SYS=0．BSDソケット互換化）
 *
 *  lwIP自身が生成するtcpip_thread（＝cfg生成のNET_TSK．port/
 *  sys_arch.c参照）だけがlwIPコアAPIを直接呼ぶ．
 *    - wifi_rx_cb（Wi-Fiドライバのタスク文脈）はpbuf_alloc/pbuf_take
 *      してtcpip_input()に渡すのみ（tcpip_input()は任意の文脈から
 *      安全に呼べる，lwIPが公式に提供するinjectionポイント）．
 *    - リンクup/downはtcpip_callback()でtcpip_thread文脈へ委譲する
 *      （dhcp_start等のraw API呼出しをtcpip_thread内に限定するため）．
 *    - DHCP完了検出はnetif_set_status_callback()（ポーリング不要）．
 *    - netif_add／tcpecho_raw_init等の初期化はtcpip_init()のinit_done
 *      コールバック内（＝tcpip_thread起動直後の文脈）で行う．
 *  設計・経緯はdocs/tcpip-integration.md．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "kernel_cfg.h"

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include "tcpecho_raw.h"

#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_mac.h"

#include "esp_shim.h"
#include "net_cfg.h"
#include "netif_esp32c3.h"
#include "ping.h"

static struct netif	s_netif;
static bool_t		s_dhcp_started;
static bool_t		s_ip_reported;

/*
 *  ---- 負荷誘発リンク停止 診断計装（実施47．docs/c5-bringup.md）----
 *
 *  net/はC3/C5/C6で完全共有（本ファイルも同一ソース）のため，
 *  ★TOPPERS_ESP32C5_NETSTALL_TRACEでガードし，未定義時（C3/C6の通常
 *  ビルド含む全ビルド）は完全no-opにする加算的変更のみとする
 *  （CLAUDE.md「サブエージェントで調査を回すときの鉄則」）。
 *
 *  - g_netstall_tx_calls/errs/last_tx_ret：low_level_output（＝
 *    esp_wifi_internal_txの唯一の呼出し元）の直近戻り値と累積エラー数。
 *    apps/load_test_c5のスナップショットダンプから参照する。
 *  - netstall_trace_ping_result()：net_ping_result（1Hz raw pingの
 *    成否コールバック．tcpip_thread文脈）にフックし，連続失敗で
 *    停止検出→スナップショット発火のトリガに使う（実体はapps/
 *    load_test_c5.c）。
 */
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE
volatile uint32_t	g_netstall_tx_calls;
volatile uint32_t	g_netstall_tx_errs;
volatile int32_t	g_netstall_last_tx_ret;
extern void netstall_trace_ping_result(int ok);
#endif /* TOPPERS_ESP32C5_NETSTALL_TRACE */

/*
 *  ---- 送信（tcpip_thread文脈．linkoutputはpbufを解放しない＝呼出し元の
 *  責務）----
 *
 *  esp_wifi_internal_txは渡したバッファのコピーを取ってから送信する
 *  （呼出し後は再利用可）ため，チェーンpbufを1個の静的バッファへ
 *  線形化してから渡す．
 */
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
	static uint8_t	txbuf[1600];
	struct pbuf		*q;
	uint16_t		total = 0;
	esp_err_t		txret;

	(void) netif;
	for (q = p; q != NULL; q = q->next) {
		if ((size_t)(total + q->len) > sizeof(txbuf)) {
			return(ERR_BUF);
		}
		memcpy(&txbuf[total], q->payload, q->len);
		total += q->len;
	}
	txret = esp_wifi_internal_tx(WIFI_IF_STA, txbuf, total);
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE
	g_netstall_tx_calls++;
	g_netstall_last_tx_ret = (int32_t) txret;
	if (txret != ESP_OK) {
		g_netstall_tx_errs++;
	}
#endif /* TOPPERS_ESP32C5_NETSTALL_TRACE */
	return((txret == ESP_OK) ? ERR_OK : ERR_IF);
}

/*
 *  ---- 受信コールバック（Wi-Fiドライバのタスク文脈）----
 *
 *  pbuf_alloc/pbuf_takeはSYS_ARCH_PROTECTで保護されており任意の文脈
 *  から呼んでよい．tcpip_input()はまさにこの目的（外部文脈からの
 *  安全なパケット注入）でlwIPが提供するAPI．bufferの実体はeb解放まで
 *  有効＝コピー後に解放する．
 */
static esp_err_t
wifi_rx_cb(void *buffer, uint16_t len, void *eb)
{
	struct pbuf	*p;

	p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
	if (p != NULL) {
		(void) pbuf_take(p, buffer, len);
		if (tcpip_input(p, &s_netif) != ERR_OK) {
			pbuf_free(p);
		}
	}
	if (eb != NULL) {
		esp_wifi_internal_free_rx_buffer(eb);
	}
	return(ESP_OK);
}

/*
 *  ---- netif初期化コールバック（netif_addから一度だけ呼ばれる）----
 */
static err_t
netif_esp32c3_init(struct netif *netif)
{
	uint8_t	mac[6];

	(void) esp_read_mac(mac, ESP_MAC_WIFI_STA);
	memcpy(netif->hwaddr, mac, sizeof(mac));
	netif->hwaddr_len = sizeof(mac);
	netif->mtu = 1500;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
	netif->name[0] = 'w';
	netif->name[1] = 'l';
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;
	return(ERR_OK);
}

/*
 *  ---- ping（lwip契約のcontrib/apps/ping．PING_USE_SOCKETS=0固定
 *  ＝raw API版．sys_timeoutベースでtcpip_thread内蔵のタイマ処理から
 *  自動的に動く）----
 */
void
net_ping_result(int ok)
{
	syslog(LOG_NOTICE, "net: ping gateway -> %s", ok ? "OK" : "timeout");
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE
	netstall_trace_ping_result(ok);
#endif /* TOPPERS_ESP32C5_NETSTALL_TRACE */
}

void
netif_esp32c3_ping_gateway(void)
{
	ping_init(netif_ip4_gw(&s_netif));
}

/*
 *  ---- DHCP完了検出（ポーリング不要．netifのアドレスが変化する度に
 *  tcpip_thread文脈で呼ばれる）----
 */
static void
netif_status_cb(struct netif *netif)
{
	char	ip_buf[16], gw_buf[16];

	if (s_ip_reported || ip4_addr_isany_val(*netif_ip4_addr(netif))) {
		return;
	}
	(void) ip4addr_ntoa_r(netif_ip4_addr(netif), ip_buf, sizeof(ip_buf));
	(void) ip4addr_ntoa_r(netif_ip4_gw(netif), gw_buf, sizeof(gw_buf));
	syslog(LOG_NOTICE, "net: DHCP bound ip=%s gw=%s", ip_buf, gw_buf);
	s_ip_reported = true;
	netif_esp32c3_ping_gateway();
}

/*
 *  ---- リンクup/down処理（tcpip_callback()経由でtcpip_thread文脈から
 *  呼ばれる．raw API（dhcp_start等）はこの文脈でのみ呼んでよい）----
 */
static void
handle_link_up(void *ctx)
{
	(void) ctx;
	syslog(LOG_NOTICE, "net: link up, starting DHCP");
	(void) esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx_cb);
	netif_set_link_up(&s_netif);
	netif_set_up(&s_netif);
	(void) dhcp_start(&s_netif);
	s_dhcp_started = true;
	s_ip_reported = false;
}

static void
handle_link_down(void *ctx)
{
	(void) ctx;
	syslog(LOG_NOTICE, "net: link down");
	ping_stop();
	if (s_dhcp_started) {
		dhcp_release_and_stop(&s_netif);
		s_dhcp_started = false;
	}
	netif_set_down(&s_netif);
	netif_set_link_down(&s_netif);
	(void) esp_wifi_internal_reg_rxcb(WIFI_IF_STA, NULL);
	s_ip_reported = false;
}

/*
 *  ---- 公開API（tcpip_thread以外から呼ぶ．lwIPには一切触れず
 *  tcpip_callback()でtcpip_thread文脈へ処理を委譲するのみ）----
 */
void
netif_esp32c3_notify_link(bool up)
{
	(void) tcpip_callback(up ? handle_link_up : handle_link_down, NULL);
}

uint32_t
netif_esp32c3_get_ipaddr(void)
{
	return(ip4_addr_get_u32(netif_ip4_addr(&s_netif)));
}

/*
 *  ---- 初期化（tcpip_init()のinit_doneコールバック．tcpip_thread
 *  起動直後にその文脈で一度だけ呼ばれる．netif_add等のraw API呼出しは
 *  ここで行う）----
 */
static void
tcpip_init_done(void *arg)
{
	ip4_addr_t	anyaddr;

	(void) arg;
	IP4_ADDR(&anyaddr, 0, 0, 0, 0);

	(void) netif_add(&s_netif, &anyaddr, &anyaddr, &anyaddr, NULL,
					  netif_esp32c3_init, tcpip_input);
	netif_set_default(&s_netif);
	netif_set_status_callback(&s_netif, netif_status_cb);

	/*
	 *  TCPエコーサーバ（ポート7．IP_ANY_TYPEでbindするためlink up前でも
	 *  呼べる）
	 */
	tcpecho_raw_init();
}

/*
 *  ---- 起動（アプリから一度だけ呼ぶ．tcpip_init()がtcpip_thread
 *  （NET_TSK）を起動し，その文脈でtcpip_init_done()が実行される）----
 */
void
netif_esp32c3_start(void)
{
	tcpip_init(tcpip_init_done, NULL);
}
