/*
 *  NimBLE host スモークテスト（ESP32-C6．Phase D-2a／BLE実施02）
 *
 *  apps/ble_host_smoke（C3）をC6/C5世代コントローラ向けに調整した版．
 *  apps/bt_smoke_c6（Phase D-1）で確立したコントローラ起動シーケンスの
 *  上に NimBLE ホストスタックを載せ，ble_hs の sync 到達（＝ホスト↔
 *  コントローラ間HCIハンドシェイク完了）を狙う．届けば接続可能
 *  アドバタイズ（D-2b）も試す．
 */
#ifndef BLE_HOST_SMOKE_C6_H
#define BLE_HOST_SMOKE_C6_H

#include <kernel.h>

#define MAIN_PRIORITY		10
#define STACK_SIZE			8192

/*
 *  割込みレート監視タスク（最低優先度に近い＝アイドル時も回る）．
 *  esp_shim_int_count[1]/[2]（BTコントローラのCPU線1/2発火数，
 *  wifi/esp_shim.cが常時カウント）をLP_AON STORE4/5へ定期ミラーする
 *  （JTAGがRF活動中に死ぬ可能性に備え，esptool read-memで事後読み
 *  できるようにする．C3のD-2bストーム調査の教訓）．
 */
#define MONITOR_PRIORITY	15
#define MONITOR_STACK_SIZE	1024

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
extern void storm_monitor_task(EXINF exinf);
#endif

#endif /* BLE_HOST_SMOKE_C6_H */
