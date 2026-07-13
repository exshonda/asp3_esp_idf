/*
 *  Bluetooth統合（ESP32-C6 Phase D-1／BLE実施01）の静的構成
 *  （bt.cfgと一致させること）
 *
 *  設計はC3のasp3/target/esp32c3_espidf/bt/bt_cfg.hを土台にする
 *  （docs/bt-shim.md）．C6/C5世代のbt.c本体（controller/esp32c6/bt.c）
 *  はext_funcs_t経由でtask/intr/mallocのみを要求し（malloc/freeは
 *  bt_osi_mem.c→heap_caps_*経由でwifi/esp_shim_libc.cへ委譲，queueや
 *  semaphoreはext_funcs_tに存在しない），npl_os_freertos.c（NimBLE NPL）
 *  が要求するqueue/semaphore/taskはC3向けに用意済みの
 *  bt/stub/include/freertos/*.h（実体はwifi/esp_shim.cの既存プールへの
 *  委譲）をそのまま流用する（詳細はdocs/ble-c5c6.md「BLE実施01」）．
 *
 *  esp_timer_*（npl_os_freertos.cのcallout実装．BLE_NPL_USE_ESP_TIMER=1
 *  で選択）はC3のbt/bt_shim.cと同じ設計（専用タイマタスク＋固定プール）
 *  を本ファイル専用に使う．
 */
#ifndef BT_CFG_H
#define BT_CFG_H

/*
 *  BLE実施01・実機観測：C6/C5世代コントローラはBLE_NPL_USE_ESP_TIMER=1
 *  選択時，npl_os_freertos.cのcallout機構（LLスケジューリング用タイマ）
 *  を内部で多用する．C3旧世代（実測要求1個のみ）と異なり，
 *  r_ble_controller_init内部だけで4個超のesp_timer_handle_tを要求し，
 *  4個ではプール枯渇（"bt: esp_timer pool exhausted"）→
 *  r_ble_controller_init failed 257 に直結することを確認．
 *  余裕を見て16個に拡大する．
 */
#define BT_TIMER_NUM       16    /* esp_timer_handle_tプール */
#define BT_TIMER_TASK_PRI  5
#define BT_TIMER_STKSZ     4096

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void bt_timer_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* BT_CFG_H */
