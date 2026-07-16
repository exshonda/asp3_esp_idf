/*
 *  FreeRTOSConfig.h（コンパイル専用スタブ．ESP-IDF供給版BT用）
 *
 *  ★なぜ hal 供給では要らず esp-idf 供給で要るのか
 *  （.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c5-05 §4）：
 *
 *    hal(esp-hal-3rdparty) の esp_system/include/esp_task.h は NuttX 向けに
 *    再パッケージされており，FreeRTOS 依存を `platform/os.h` へ**置換**して
 *    いる（hal版 esp_task.h:24）。一方，本家 ESP-IDF v5.5.4 の esp_task.h は
 *
 *        :24  #include "freertos/FreeRTOS.h"
 *        :25  #include "freertos/FreeRTOSConfig.h"
 *
 *    と FreeRTOS 実体を要求する（両ツリーの esp_task.h は他の点では
 *    バイト同一＝この include 2行だけが版差）。esp_bt.h:14 が esp_task.h を
 *    引くため，供給を esp-idf へ揃えた時点で本ヘッダが必要になる。
 *
 *  ASP3 は FreeRTOS をリンクしない（カーネルは ASP3．FreeRTOS API は
 *  wifi/esp_shim.c のプリミティブへ委譲）ため，**本家の
 *  components/freertos/config/include/freertos/FreeRTOSConfig.h は使えない**
 *  （Kconfig 生成物と FreeRTOS 実体を前提とする）。C3 の bt/stub/include/
 *  freertos/*.h と同じ「ヘッダ単位のシム」方針でスタブを置く。
 *
 *  esp_task.h が本ヘッダから実際に要求するのは `configMAX_PRIORITIES`
 *  （ESP_TASK_PRIO_MAX の定義に使用）のみ。esp_task.h は :24 で
 *  freertos/FreeRTOS.h（＝C3 の既存スタブ．configMAX_PRIORITIES=25 を定義）を
 *  先に読むため実質は既に定義済みだが，本ヘッダ単独 include にも耐えるよう
 *  #ifndef ガード付きで同値を持たせて自己完結させる（値は C3 スタブと同一＝
 *  ESP-IDF 既定に倣う）。
 *
 *  ★値がランタイム挙動に影響しないことの根拠（C3 スタブのコメントと同じ）：
 *  esp_shim_task_create は優先度を一律（ESP_SHIM_WIFI_TASK_PRI）へ写像する
 *  ため，ESP_TASK_PRIO_MAX - N の実値はタスクの実優先度に反映されない。
 */
#ifndef TOPPERS_BT_FREERTOSCONFIG_H
#define TOPPERS_BT_FREERTOSCONFIG_H

/*
 *  ESP-IDF 既定に倣う（C3 の bt/stub/include/freertos/FreeRTOS.h と同値）。
 *  同ヘッダが先に読まれている場合は再定義しない。
 */
#ifndef configMAX_PRIORITIES
#define configMAX_PRIORITIES	25
#endif

/*  tick=1ms（esp_shim 側の前提。C3 スタブと同値） */
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ	1000
#endif

#endif /* TOPPERS_BT_FREERTOSCONFIG_H */
