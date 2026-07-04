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
 *  ESP32-C3 Wi-Fi用lwIP netif（ASP3．NO_SYS=1．Phase C）
 *
 *  esp_wifi_internal_tx/reg_rxcb の上に薄いethernet netifを実装し，
 *  net_task（cfg生成の唯一タスク）1つの実行文脈にlwIPコア呼出しを
 *  集約する（設計・経緯はdocs/tcpip-integration.md）．
 *  アプリはnetif_esp32c3_notify_link()のみを呼び，netif_add／
 *  dhcp_start等のlwIP API自体には触れない．
 */
#ifndef NETIF_ESP32C3_H
#define NETIF_ESP32C3_H

#include <stdbool.h>
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  リンク状態通知（Wi-Fiイベントハンドラから呼ぶ．net_task文脈へは
 *  フラグ経由で反映される＝lwIP API自体はnet_task内でのみ呼ぶ）
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
