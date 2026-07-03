/*
 *  Wi-Fiスキャンデモ（ASP3＋esp_wifi blob＋os_adapter shim）
 */
#ifndef WIFI_SCAN_H
#define WIFI_SCAN_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
#endif

#endif /* WIFI_SCAN_H */
