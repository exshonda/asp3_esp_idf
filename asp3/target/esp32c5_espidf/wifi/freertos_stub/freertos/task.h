/*
 *  【実施10】FreeRTOS task.h 互換スタブ（コンパイル専用）。
 *  IDF v6.1 esp_event.h/esp_wifi.h の型解決用。ASP3 は FreeRTOS 非使用。
 */
#ifndef TOPPERS_C5_WIFI_FREERTOS_TASK_H
#define TOPPERS_C5_WIFI_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

#ifndef tskNO_AFFINITY
#define tskNO_AFFINITY	((BaseType_t) 0x7fffffff)
#endif

#endif /* TOPPERS_C5_WIFI_FREERTOS_TASK_H */
