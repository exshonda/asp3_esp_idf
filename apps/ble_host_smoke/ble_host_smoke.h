/*
 *  NimBLE host スモークテスト（Phase D-2）
 *
 *  D-1（bt_smoke）で確立したBLEコントローラの上に NimBLE ホストスタックを
 *  載せ，ble_hs が sync に到達する（＝ホスト起動）ことを狙うビルド．
 *  ランタイム到達確認は実機JTAGで別途行う（本タスクはビルド＋リンク＋
 *  RAM収まりの確認まで）．
 */
#ifndef BLE_HOST_SMOKE_H
#define BLE_HOST_SMOKE_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
#endif

#endif /* BLE_HOST_SMOKE_H */
