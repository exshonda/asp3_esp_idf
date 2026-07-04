/*
 *  BSDソケットTCPechoデモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 */
#ifndef TCP_SOCKET_ECHO_H
#define TCP_SOCKET_ECHO_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

/*
 *  ソケットechoサーバタスク（main_taskとは別文脈．任意のアプリタスク
 *  からBSDソケットAPIが使えることを示すのが目的）
 */
#define ECHO_PRIORITY	10
#define ECHO_STACK_SIZE	4096

/*
 *  echoポート（7番はlwip contribのtcpecho_raw（raw API版）が
 *  既に使用しているため，本デモ（BSDソケット版）は8番を使う）
 */
#define ECHO_PORT		8

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
extern void echo_task(EXINF exinf);
#endif

#endif /* TCP_SOCKET_ECHO_H */
