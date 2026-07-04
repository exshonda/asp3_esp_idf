/*
 *  BSDソケットTCPクライアントデモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 */
#ifndef TCP_SOCKET_CLIENT_H
#define TCP_SOCKET_CLIENT_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

/*
 *  ソケットクライアントタスク（main_taskとは別文脈．任意のアプリタスク
 *  からBSDソケットAPI（socket/connect/send/recv）で外部サーバへ接続
 *  できることを示すのが目的）
 */
#define CLIENT_PRIORITY		10
#define CLIENT_STACK_SIZE	4096

/*
 *  接続先サーバ（-DSERVER_ADDR=... -DSERVER_PORT=... で上書き．
 *  開発機で `nc -l -p 9000` 等の待受けを起動しておく）
 */
#ifndef SERVER_ADDR
#define SERVER_ADDR		"192.168.1.1"
#endif
#ifndef SERVER_PORT
#define SERVER_PORT		9000
#endif

/*
 *  接続先AP（-DWIFI_SSID=... -DWIFI_PASSWORD=... で上書き）
 */
#ifndef WIFI_SSID
#define WIFI_SSID		"your-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD	"your-password"
#endif

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
extern void client_task(EXINF exinf);
#endif

#endif /* TCP_SOCKET_CLIENT_H */
