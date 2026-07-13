/*
 *  Bluetooth（BLE）コントローラ起動＋VHCIループバックのスモークテスト
 *  （ESP32-C5．Phase D-1／BLE実施03）
 */
#ifndef BT_SMOKE_C5_H
#define BT_SMOKE_C5_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
#endif

#endif /* BT_SMOKE_C5_H */
