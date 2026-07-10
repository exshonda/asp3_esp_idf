/*
 *  【実施10】FreeRTOS queue.h 互換スタブ（コンパイル専用）。
 *  IDF v6.1 esp_event.h の QueueHandle_t 型解決用。ASP3 は FreeRTOS 非使用。
 */
#ifndef TOPPERS_C5_WIFI_FREERTOS_QUEUE_H
#define TOPPERS_C5_WIFI_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

#endif /* TOPPERS_C5_WIFI_FREERTOS_QUEUE_H */
