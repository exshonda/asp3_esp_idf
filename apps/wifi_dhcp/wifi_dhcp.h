/*
 *  Wi-Fi DHCP接続デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 */
#ifndef WIFI_DHCP_H
#define WIFI_DHCP_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

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
#endif

#endif /* WIFI_DHCP_H */
