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
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
extern void wifi_diag_cyclic_handler(EXINF exinf);	/* 実施24 */
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */
#ifdef ESP32C5_R38_RXINSTR
extern void r38_rxinstr_cyclic_handler(EXINF exinf);	/* 実施38 */
#endif /* ESP32C5_R38_RXINSTR */
#endif

#endif /* WIFI_SCAN_H */
