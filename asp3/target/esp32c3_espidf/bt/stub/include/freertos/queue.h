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
extern void esp_shim_queue_reset(void *que);

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

/*
 *  NimBLE NPL（npl_os_freertos.c）が要求する追加API．
 *  すべて既存のesp_shim_queue_*へ委譲する．
 *  ・ToBack＝通常送信，ToFront＝esp_shim側で先頭送信非対応のため通常送信で
 *    代用（NimBLE eventq_put_to_frontは稀用途．順序が厳密に要る箇所は無い）．
 */
static inline BaseType_t
xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue,
				 TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_send(xQueue, (void *) pvItemToQueue,
											 xTicksToWait, false);
}

static inline BaseType_t
xQueueSendToBackFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
						BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_queue_send_from_isr(xQueue,
													  (void *) pvItemToQueue);
}

static inline BaseType_t
xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
				  TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_queue_send(xQueue, (void *) pvItemToQueue,
											 xTicksToWait, true);
}

static inline BaseType_t
xQueueSendToFrontFromISR(QueueHandle_t xQueue, const void *pvItemToQueue,
						 BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_queue_send_from_isr(xQueue,
													  (void *) pvItemToQueue);
}

static inline void
xQueueReset(QueueHandle_t xQueue)
{
	esp_shim_queue_reset(xQueue);
}

static inline UBaseType_t
uxQueueMessagesWaitingFromISR(QueueHandle_t xQueue)
{
	return (UBaseType_t) esp_shim_queue_msg_waiting(xQueue);
}

static inline BaseType_t
xQueueIsQueueEmptyFromISR(QueueHandle_t xQueue)
{
	return (BaseType_t) (esp_shim_queue_msg_waiting(xQueue) == 0U ?
						 pdTRUE : pdFALSE);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_QUEUE_H */
