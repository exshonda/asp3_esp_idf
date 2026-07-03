/*
 *  Wi-Fi os_adapter shimの静的プール構成（esp_shim.cfgと一致させること）
 */
#ifndef ESP_SHIM_CFG_H
#define ESP_SHIM_CFG_H

/*
 *  プールの規模
 */
#define ESP_SHIM_NUM_SEM    24    /* セマフォプール */
#define ESP_SHIM_NUM_MTX    8     /* ミューテックスプール */
#define ESP_SHIM_NUM_DTQ    4     /* キュープール（各深さESP_SHIM_DTQ_CNT） */
#define ESP_SHIM_DTQ_CNT    64
#define ESP_SHIM_NUM_TSK    6     /* タスクプール（各スタックESP_SHIM_TSK_STKSZ） */
#define ESP_SHIM_TSK_STKSZ  8192

/*
 *  ヒープサイズ（静的配列．Wi-Fi blobは実測で数十KBを要求する）
 */
#ifndef ESP_SHIM_HEAP_SIZE
#define ESP_SHIM_HEAP_SIZE  (192 * 1024)
#endif

/*
 *  shimタスクの優先度（ASP3．小さいほど高優先度．アプリはより低い
 *  優先度（10前後）で動かすこと）
 */
#define ESP_SHIM_TIMER_TASK_PRI   2   /* ets_timerディスパッチ */
#define ESP_SHIM_WIFI_TASK_PRI    3   /* blobが生成するタスク（一律） */

/*
 *  Wi-Fi用CPU割込み線
 *
 *  blobは_set_intr/_ints_onで自身が決めたCPU割込み線番号を指定して
 *  くるため，blobの選択（小さい番号）と衝突しないよう，ターゲットの
 *  ペリフェラル（SYSTIMER/コンソール/テスト用）は線16以降に退避して
 *  いる（target_timer.h等参照）．DEF_INHを静的登録できるのは既知の線
 *  のみ＝blobが使う線はcfg（esp_shim.cfg）に列挙する．
 */
#define ESP_SHIM_MAX_WIFI_INTNO   15  /* 1〜15をblob用に開放 */

/*
 *  cfg（esp_shim.cfg）から参照する関数（実体はesp_shim.c）
 */
#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void esp_shim_task_entry(EXINF exinf);
extern void esp_shim_timer_task(EXINF exinf);
extern void esp_shim_inthdr_1(void);
extern void esp_shim_inthdr_2(void);
extern void esp_shim_inthdr_3(void);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* ESP_SHIM_CFG_H */
