/*
 *  BSDソケットUDP echoデモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 */
#ifndef UDP_SOCKET_ECHO_H
#define UDP_SOCKET_ECHO_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

/*
 *  UDPソケットechoサーバタスク（main_taskとは別文脈．任意のアプリ
 *  タスクからBSDソケットAPIが使えることを示すのが目的）
 */
#define UDP_PRIORITY	10
#define UDP_STACK_SIZE	4096

/*
 *  echoポート（7番はlwip contribのtcpecho_raw，8番は本リポジトリの
 *  TCPソケット版echoデモが既に使用しているため，本デモ（UDPソケット
 *  版）は9番を使う）
 */
#define UDP_PORT		9

/*
 *  接続先（-DWIFI_SSID=... -DWIFI_PASSWORD=... で上書き）
 */
#ifndef WIFI_SSID
#define WIFI_SSID		"your-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD	"your-password"
#endif

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
extern void udp_task(EXINF exinf);
#endif

#endif /* UDP_SOCKET_ECHO_H */
