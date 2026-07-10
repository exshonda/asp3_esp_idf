/*
 *  【実施10】ESP-IDF v6.1 移行に伴うFreeRTOS互換ヘッダ（コンパイル専用スタブ）
 *
 *  hal(v8) の esp_event.h は freertos を include しなかったが，IDF v6.1 の
 *  esp_event.h（esp_wifi.h が transitive に取り込む）は
 *    #include "freertos/FreeRTOS.h" / "freertos/task.h" / "freertos/queue.h"
 *  を要求する。ASP3 は FreeRTOS を使わないため，esp_event.h/esp_wifi.h の
 *  «型»（TickType_t/BaseType_t/UBaseType_t/TaskHandle_t/QueueHandle_t）を
 *  満たすだけの最小スタブを供給する。基本整数型は hal_stub の
 *  platform/os.h（TickType_t/UBaseType_t/BaseType_t を定義済み）に委譲する。
 *  BTの freertos スタブ（esp32c3_espidf/bt/stub）とは独立（あちらは
 *  esp_partition.h 等 BT 固有ヘッダを同居させるため include 全体は流用不可）。
 */
#ifndef TOPPERS_C5_WIFI_FREERTOS_H
#define TOPPERS_C5_WIFI_FREERTOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <platform/os.h>	/* TickType_t / UBaseType_t / BaseType_t */

#ifndef pdFALSE
#define pdFALSE		0
#endif
#ifndef pdTRUE
#define pdTRUE		1
#endif
#ifndef pdPASS
#define pdPASS		1
#endif
#ifndef pdFAIL
#define pdFAIL		0
#endif

#ifndef portMAX_DELAY
#define portMAX_DELAY		0xffffffffUL
#endif
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS	1
#endif
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ	1000
#endif
#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms)	((TickType_t)(ms))
#endif

typedef void (*TaskFunction_t)(void *);

/*
 *  IDF の FreeRTOS.h は portmacro.h を transitive に取り込み，
 *  portMUX_TYPE / portMUX_INITIALIZER_UNLOCKED / portENTER_CRITICAL 系を
 *  供給する。esp_hw_support/periph_ctrl.c 等は FreeRTOS.h だけを include
 *  してこれらを使うため，本スタブでも末尾で portmacro.h を取り込む。
 */
#include "freertos/portmacro.h"

#endif /* TOPPERS_C5_WIFI_FREERTOS_H */
