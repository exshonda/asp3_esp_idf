/*
 *  NimBLE host スモークテスト（ESP32-C5．Phase D-2a／BLE実施05）
 *
 *  apps/ble_host_smoke_c6（C6のD-2a/D-2b）をC5向けに調整した版．
 *  apps/bt_smoke_c5（Phase D-1．BLE実施03/04でD-1達成）で確立した
 *  コントローラ起動シーケンスの上に NimBLE ホストスタックを載せ，
 *  ble_hs の sync 到達（D-2a）を狙う．届けば接続可能アドバタイズ
 *  （D-2b）も試す．
 */
#ifndef BLE_HOST_SMOKE_C5_H
#define BLE_HOST_SMOKE_C5_H

#include <kernel.h>

#define MAIN_PRIORITY		10
#define STACK_SIZE			8192

/*
 *  割込みレート監視タスク（最低優先度に近い＝アイドル時も回る）．
 *  esp_shim_int_count[1]/[2]（BTコントローラのCPU線1/2発火数，
 *  wifi/esp_shim.cが常時カウント）をLP_AON STOREへ定期ミラーする
 *  （JTAGがRF活動中に死ぬ可能性＋コンソールが「no time event」氾濫で
 *  埋まる可能性の両方に備え，esptool read-memで事後読みできるように
 *  する．C3/C6のD-2bストーム調査＋C5実施04の氾濫の教訓）．
 */
#define MONITOR_PRIORITY	15
#define MONITOR_STACK_SIZE	1024

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
extern void storm_monitor_task(EXINF exinf);
#endif

#endif /* BLE_HOST_SMOKE_C5_H */
