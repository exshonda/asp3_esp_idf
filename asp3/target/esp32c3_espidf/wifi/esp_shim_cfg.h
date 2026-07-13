/*
 *  Wi-Fi os_adapter shimの静的プール構成（esp_shim.cfgと一致させること）
 */
#ifndef ESP_SHIM_CFG_H
#define ESP_SHIM_CFG_H

/*
 *  プールの規模
 */
/*
 *  プール規模．
 *
 *  NimBLE（Phase D-2）を積む BT ビルド（TOPPERS_ESP32C3_BT）では，NPL
 *  （npl_os_freertos.c）が eventq→xQueueCreate（DTQ），mutex→
 *  xSemaphoreCreateRecursiveMutex（MTX），sem→xSemaphoreCreateCounting
 *  （SEM），ホストタスク→esp_shim_task_create（TSK）へ写像するため，
 *  コントローラ使用分と衝突しないよう各プールを小幅に拡張する．
 *
 *  esp_shim は WiFi ビルドとも共有だが，拡張分は TOPPERS_ESP32C3_BT_NIMBLE
 *  （NimBLE ホストを積むビルドのみ立つ．esp_bt.cmake で定義）限定にして，
 *  WiFi ビルドおよび BT コントローラ単体（bt_smoke）の RAM を従来通りに保つ．
 *  （esp_shim.cfg／esp_shim.c の配列・CRE_* と一致させること）
 *
 *  ★BLE実施02（C6）：C6/C5はesp_shim.h／esp_shim_cfg.h／esp_shim.cfgを
 *  target.cmake経由でC3側のこのファイルをそのまま再利用する（chip非依存
 *  部）．TOPPERS_ESP32C6_BT_NIMBLEをOR条件で追加し，C3の既存条件・値は
 *  一切変更しない（strictly additive．docs/ble-c5c6.md「BLE実施02」）．
 */
#if defined(TOPPERS_ESP32C3_BT_NIMBLE) || defined(TOPPERS_ESP32C6_BT_NIMBLE)
#define ESP_SHIM_NUM_SEM    28    /* 24→28：NimBLE分+4 */
#define ESP_SHIM_NUM_MTX    12    /* 8→12：NimBLE分+4 */
#define ESP_SHIM_NUM_DTQ    8     /* 4→8：NimBLE eventq分+4 */
#define ESP_SHIM_NUM_TSK    8     /* 6→8：NimBLEホストタスク分+ */
#else
#define ESP_SHIM_NUM_SEM    24    /* セマフォプール */
#define ESP_SHIM_NUM_MTX    8     /* ミューテックスプール */
#define ESP_SHIM_NUM_DTQ    4     /* キュープール（各深さESP_SHIM_DTQ_CNT） */
#define ESP_SHIM_NUM_TSK    6     /* タスクプール（各スタックESP_SHIM_TSK_STKSZ） */
#endif
#define ESP_SHIM_DTQ_CNT    256   /* WiFiドライバタスクのイベントキューは200深を要求 */
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
