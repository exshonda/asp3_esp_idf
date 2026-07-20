/*
 *  Bluetooth統合（ESP32-C5 Phase D-1／BLE実施03）の静的構成
 *  （bt.cfgと一致させること）
 *
 *  C6のasp3/target/esp32c6_espidf/bt/bt_cfg.hから無変更で移植
 *  （chip非依存．docs/ble-c5c6.md「BLE実施03」）．C5世代コントローラ
 *  （controller/esp32c5/bt.c，本ラウンドはIDF v6.1版を使用）も
 *  BLE_NPL_USE_ESP_TIMER=1選択時，npl_os_freertos.cのcallout機構を
 *  内部で多用しBT_TIMER_NUM=4では枯渇する見込みのため，C6実施01の
 *  教訓を先読み適用してBT_TIMER_NUM=16を最初から採用する。
 */
#ifndef BT_CFG_H
#define BT_CFG_H

#define BT_TIMER_NUM       16    /* esp_timer_handle_tプール */
#define BT_TIMER_TASK_PRI  5
#define BT_TIMER_STKSZ     4096

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void bt_timer_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* BT_CFG_H */
