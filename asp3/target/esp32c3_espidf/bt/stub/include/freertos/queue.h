/*
 *  BTコントローラ（bt.c）用のFreeRTOS queue.h互換ヘッダ（コンパイル専用）
 *
 *  実体はwifi/esp_shim.cのesp_shim_queue_*（任意item_sizeをヒープ
 *  ボックス化で扱う既存実装）へそのまま委譲する．
 */
#ifndef TOPPERS_BT_FREERTOS_QUEUE_H
#define TOPPERS_BT_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef void *QueueHandle_t;

#ifndef TOPPERS_MACRO_ONLY
extern void *esp_shim_queue_create(uint32_t len, uint32_t item_size);
extern void esp_shim_queue_delete(void *que);
extern int32_t esp_shim_queue_send(void *que, void *item,
									uint32_t block_time_tick, bool_t to_front);
extern int32_t esp_shim_queue_send_from_isr(void *que, void *item);
extern int32_t esp_shim_queue_recv(void *que, void *item,
									uint32_t block_time_tick);
extern uint32_t esp_shim_queue_msg_waiting(void *que);

static inline QueueHandle_t
xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
	return (QueueHandle_t) esp_shim_queue_create(uxQueueLength, uxItemSize);
}

static inline void
vQueueDelete(QueueHandle_t xQueue)
{
	esp_shim_queue_delete(xQueue);
}

static inline BaseType_t
xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		   TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_send(xQueue, (void *) pvItemToQueue,
											 xTicksToWait, false);
}

static inline BaseType_t
xQueueSendFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
				   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_queue_send_from_isr(xQueue,
													  (void *) pvItemToQueue);
}

static inline BaseType_t
xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_recv(xQueue, pvBuffer, xTicksToWait);
}

static inline BaseType_t
xQueueReceiveFromISR(QueueHandle_t xQueue, void *pvBuffer,
					  BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_queue_recv(xQueue, pvBuffer, 0);
}

static inline UBaseType_t
uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_queue_msg_waiting(xQueue);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_QUEUE_H */
