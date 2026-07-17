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
 *  ESP32-C3 Wi-Fi用lwIP netif（ASP3．NO_SYS=0．BSDソケット互換化）
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない（ファイル名の
 *  "esp32c3"は歴史的経緯＝C3が最初の移植先．実体はC3／C5／C6で共用．
 *  詳細はnetif_esp32c3.c先頭コメント参照）．
 *
 *  esp_wifi_internal_tx/reg_rxcb の上に薄いethernet netifを実装し，
 *  lwIP自身が生成するtcpip_thread（cfg生成のNET_TSK．port/
 *  sys_arch.c参照）にlwIPコア呼出しを集約する（設計・経緯は
 *  docs/tcpip-integration.md）．アプリはnetif_esp32c3_start()と
 *  netif_esp32c3_notify_link()のみを呼び，netif_add／dhcp_start／
 *  socket()等のlwIP API自体はtcpip_thread側（またはソケットAPI経由）
 *  でのみ扱われる．
 */
#ifndef NETIF_ESP32C3_H
#define NETIF_ESP32C3_H

#include <stdbool.h>
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  起動（アプリから一度だけ呼ぶ．esp_wifi_init等より前でよい）．
 *  内部でtcpip_init()を呼びtcpip_thread（NET_TSK）を起動する．
 */
extern void netif_esp32c3_start(void);

/*
 *  リンク状態通知（Wi-Fiイベントハンドラから呼ぶ．tcpip_callback()で
 *  tcpip_thread文脈へ委譲される＝lwIP API自体はここでは呼ばない）
 *
 *    up   : STA_CONNECTED後に呼ぶ．netif起動＋DHCP開始
 *    down : STA_DISCONNECTED後に呼ぶ．netif停止＋DHCP停止
 */
extern void netif_esp32c3_notify_link(bool up);

/*
 *  DHCPで取得したIPアドレス（未取得時は0）．ログ・デモアプリ用の
 *  読み出し専用ポーリングAPI（単一32bit語の読み出しのみ）．
 */
extern uint32_t netif_esp32c3_get_ipaddr(void);

/*
 *  デフォルトゲートウェイへのraw APIping送信を開始する（DHCP取得後に
 *  net_task文脈で自動実行される．手動起動が必要な場合の補助API）
 */
extern void netif_esp32c3_ping_gateway(void);

#ifdef __cplusplus
}
#endif

#endif /* NETIF_ESP32C3_H */
