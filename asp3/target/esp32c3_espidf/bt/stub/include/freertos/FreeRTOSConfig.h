/*
 *  FreeRTOSConfig.h（コンパイル専用スタブ．ESP-IDF供給版BT用）
 *
 *  ★C5（esp32c5_espidf/bt/stub/include/freertos/FreeRTOSConfig.h）の転写。
 *  C5 のコメント自身が「C3 の bt/stub/include/freertos/ 配下と同じ
 *  『ヘッダ単位のシム』方針」「値は C3 スタブと同一」と述べており，
 *  本ファイルはその C3 側の対応物＝**新規設計ではない**。
 *
 *  ★なぜ hal 供給では要らず esp-idf 供給で要るのか（本ラウンドで実測）：
 *
 *    hal(esp-hal-3rdparty) の esp_system/include/esp_task.h は NuttX 向けに
 *    再パッケージされており，FreeRTOS 依存を `platform/os.h` へ**置換**して
 *    いる（実測：hal esp_task.h:24 `#include "platform/os.h"` ＋
 *    `ESP_TASK_PRIO_MAX (OS_TASK_PRIO_MAX)`）。一方，本家 ESP-IDF v5.5.4 は
 *
 *        :24  #include "freertos/FreeRTOS.h"
 *        :25  #include "freertos/FreeRTOSConfig.h"
 *        :27  #define ESP_TASK_PRIO_MAX (configMAX_PRIORITIES)
 *
 *    と FreeRTOS 実体を要求する。**esp_bt.h:14 が esp_task.h を引く**ため，
 *    BT の供給を esp-idf へ揃えた時点で本ヘッダが必要になる
 *    （＝WiFi 側で esp_event.h が freertos/*.h を要求したのと同型の版差）。
 *
 *  ASP3 は FreeRTOS をリンクしない（カーネルは ASP3．FreeRTOS API は
 *  wifi/esp_shim.c のプリミティブへ委譲）ため，**本家の
 *  components/freertos/config/include/freertos/FreeRTOSConfig.h は使えない**
 *  （Kconfig 生成物と FreeRTOS 実体を前提とする）。同ディレクトリの既存
 *  スタブ（FreeRTOS.h/task.h/queue.h/semphr.h/timers.h/portable.h/portmacro.h）
 *  と同じ方針でヘッダ単位のシムを置く。
 *
 *  esp_task.h が本ヘッダから実際に要求するのは `configMAX_PRIORITIES`
 *  （ESP_TASK_PRIO_MAX の定義に使用）のみ。esp_task.h は :24 で
 *  freertos/FreeRTOS.h（＝同ディレクトリの既存スタブ．configMAX_PRIORITIES=25
 *  を定義）を先に読むため実質は既に定義済みだが，本ヘッダ単独 include にも
 *  耐えるよう #ifndef ガード付きで同値を持たせて自己完結させる。
 *
 *  ★値がランタイム挙動に影響しないことの根拠（既存 FreeRTOS.h と同じ）：
 *  esp_shim_task_create は優先度を一律（ESP_SHIM_WIFI_TASK_PRI）へ写像する
 *  ため，ESP_TASK_PRIO_MAX - N の実値はタスクの実優先度に反映されない。
 */
#ifndef TOPPERS_BT_FREERTOSCONFIG_H
#define TOPPERS_BT_FREERTOSCONFIG_H

/*
 *  ESP-IDF 既定に倣う（同ディレクトリの FreeRTOS.h:40 と同値）。
 *  同ヘッダが先に読まれている場合は再定義しない。
 */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES	25
#endif

/*  tick=1ms（esp_shim 側の前提．FreeRTOS.h:30 と同値） */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ	1000
#endif

#endif /* TOPPERS_BT_FREERTOSCONFIG_H */
