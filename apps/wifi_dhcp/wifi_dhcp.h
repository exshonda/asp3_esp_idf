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

/*
 *  実施45診断計装：connect経路のCPU例外を捕捉しRTC-RAM(0x50000000)へ
 *  凍結するフォルトハンドラ（docs/c5-bringup.md 実施06/08の手法を移植）。
 *  DEF_EXCで全ターゲット共通のEXCNO_*（core_kernel.h）に登録する
 *  （cfgの#ifdefは独自プリプロセッサでCMakeの-Dを見ないため，チップ非
 *  依存の本ハンドラは無条件登録とする．フォルトが起きない限り無害）。
 */
extern void fault_capture_handler(void *p_excinf);
#endif

#endif /* WIFI_DHCP_H */
