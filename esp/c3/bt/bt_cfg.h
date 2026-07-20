/*
 *  Bluetooth統合（Phase D-1）の静的構成（bt.cfgと一致させること）
 *
 *  bt.c本体が要求する動的プリミティブ（queue/semaphore/task）は
 *  wifi/esp_shim.cの既存プール（ESP_SHIM_NUM_*．esp_shim_cfg.h）を
 *  そのまま使う（bt.cの実測要求：queue 1個・semaphore 2個・task 1個＝
 *  既存プールで十分）．
 *
 *  esp_timer_*（BTコントローラのモデムスリープ用タイマ．本ビルドでは
 *  sleep_mode=0のため実行時には未使用の見込みだが，リンクは必要）は
 *  esp_shim.cのets_timer機構とは別に，本ファイル専用の小さな
 *  タイマタスク＋固定プールで提供する（bt/bt_shim.c参照）．
 */
#ifndef BT_CFG_H
#define BT_CFG_H

#define BT_TIMER_NUM       4     /* esp_timer_handle_tプール */
#define BT_TIMER_TASK_PRI  5
#define BT_TIMER_STKSZ     4096

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void bt_timer_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* BT_CFG_H */
