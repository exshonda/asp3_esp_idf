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
 *  ESP32-C3 Wi-Fi用lwIP netif実装（ASP3．NO_SYS=1．Phase C）
 *
 *  net_task（cfgで生成される唯一のタスク）だけがlwIPコアAPIを呼ぶ
 *  ＝単一実行文脈の原則を守る．wifi_rx_cb（Wi-Fiドライバのタスク
 *  文脈で呼ばれる）は受信フレームをボックス化してnet_taskのキューに
 *  渡すのみで，pbuf操作等のlwIP呼出しは一切行わない．
 *  リンク状態の通知（Wi-Fiイベントハンドラ→net_task）もフラグ経由の
 *  ポーリングに留め，dhcp_start等はnet_task内から呼ぶ．
 *  設計・経緯はdocs/tcpip-integration.md．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "kernel_cfg.h"

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

#include "esp_wifi.h"
#include "esp_private/wifi.h"
#include "esp_mac.h"

#include "esp_shim.h"
#include "net_cfg.h"
#include "netif_esp32c3.h"
#include "ping.h"

static struct netif	s_netif;
static volatile int32_t	s_link_cmd;	/* 0=無し 1=up 2=down（net_taskが消費） */
static bool_t		s_dhcp_started;
static bool_t		s_ip_reported;

/*
 *  受信フレームのボックス（wifi_rx_cb→net_task）．esp_shimヒープ上に
 *  1個ずつ確保し，net_task側で解放する．
 */
struct rx_item {
	void		*buffer;
	uint16_t	len;
	void		*eb;
};

/*
 *  ---- 送信（net_task文脈．linkoutputはpbufを解放しない＝呼出し元の
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

	(void) netif;
	for (q = p; q != NULL; q = q->next) {
		if ((size_t)(total + q->len) > sizeof(txbuf)) {
			return(ERR_BUF);
		}
		memcpy(&txbuf[total], q->payload, q->len);
		total += q->len;
	}
	return((esp_wifi_internal_tx(WIFI_IF_STA, txbuf, total) == ESP_OK)
		   ? ERR_OK : ERR_IF);
}

/*
 *  ---- 受信コールバック（Wi-Fiドライバのタスク文脈．lwIP APIは
 *  一切呼ばない．bufferの実体はeb解放まで有効＝net_task側でコピー
 *  してからesp_wifi_internal_free_rx_bufferを呼ぶ）----
 */
static esp_err_t
wifi_rx_cb(void *buffer, uint16_t len, void *eb)
{
	struct rx_item	*item;

	item = (struct rx_item *) esp_shim_malloc(sizeof(*item));
	if (item == NULL) {
		if (eb != NULL) {
			esp_wifi_internal_free_rx_buffer(eb);
		}
		return(ESP_FAIL);
	}
	item->buffer = buffer;
	item->len = len;
	item->eb = eb;

	if (psnd_dtq(NET_RXQ, (intptr_t) item) != E_OK) {
		/*  キュー満杯．即座にドロップ（bufferはebと共に解放）  */
		esp_shim_free(item);
		if (eb != NULL) {
			esp_wifi_internal_free_rx_buffer(eb);
		}
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
 *  ---- ping（lwip契約のcontrib/apps/ping．raw API版＝sys_timeout駆動
 *  なのでnet_taskのsys_check_timeouts()から自動的に動く）----
 */
void
net_ping_result(int ok)
{
	syslog(LOG_NOTICE, "net: ping gateway -> %s", ok ? "OK" : "timeout");
}

void
netif_esp32c3_ping_gateway(void)
{
	ping_init(netif_ip4_gw(&s_netif));
}

/*
 *  ---- リンクup/down処理（net_task文脈でのみ呼ぶ）----
 */
static void
handle_link_up(void)
{
	syslog(LOG_NOTICE, "net: link up, starting DHCP");
	(void) esp_wifi_internal_reg_rxcb(WIFI_IF_STA, wifi_rx_cb);
	netif_set_link_up(&s_netif);
	netif_set_up(&s_netif);
	(void) dhcp_start(&s_netif);
	s_dhcp_started = true;
	s_ip_reported = false;
}

static void
handle_link_down(void)
{
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
 *  ---- 公開API（Wi-Fiイベントハンドラ等，net_task以外から呼ぶ．
 *  lwIPには一切触れずフラグを立てるのみ）----
 */
void
netif_esp32c3_notify_link(bool up)
{
	s_link_cmd = up ? 1 : 2;
}

uint32_t
netif_esp32c3_get_ipaddr(void)
{
	return(ip4_addr_get_u32(netif_ip4_addr(&s_netif)));
}

/*
 *  ---- net_task：lwIPコアの唯一の実行文脈 ----
 */
void
net_task(EXINF exinf)
{
	ip4_addr_t	anyaddr;

	(void) exinf;
	IP4_ADDR(&anyaddr, 0, 0, 0, 0);

	lwip_init();
	(void) netif_add(&s_netif, &anyaddr, &anyaddr, &anyaddr, NULL,
					  netif_esp32c3_init, ethernet_input);
	netif_set_default(&s_netif);

	for (;;) {
		intptr_t	msg;
		ER			er;

		er = trcv_dtq(NET_RXQ, &msg, NET_POLL_TMO);
		if (er == E_OK) {
			struct rx_item	*item = (struct rx_item *) msg;
			struct pbuf		*p;

			p = pbuf_alloc(PBUF_RAW, item->len, PBUF_POOL);
			if (p != NULL) {
				(void) pbuf_take(p, item->buffer, item->len);
				if (s_netif.input(p, &s_netif) != ERR_OK) {
					pbuf_free(p);
				}
			}
			if (item->eb != NULL) {
				esp_wifi_internal_free_rx_buffer(item->eb);
			}
			esp_shim_free(item);
		}

		if (s_link_cmd == 1) {
			s_link_cmd = 0;
			handle_link_up();
		}
		else if (s_link_cmd == 2) {
			s_link_cmd = 0;
			handle_link_down();
		}

		if (s_dhcp_started && !s_ip_reported
			&& !ip4_addr_isany_val(*netif_ip4_addr(&s_netif))) {
			char	ip_buf[16], gw_buf[16];

			(void) ip4addr_ntoa_r(netif_ip4_addr(&s_netif), ip_buf, sizeof(ip_buf));
			(void) ip4addr_ntoa_r(netif_ip4_gw(&s_netif), gw_buf, sizeof(gw_buf));
			syslog(LOG_NOTICE, "net: DHCP bound ip=%s gw=%s", ip_buf, gw_buf);
			s_ip_reported = true;
			netif_esp32c3_ping_gateway();
		}

		sys_check_timeouts();
	}
}
