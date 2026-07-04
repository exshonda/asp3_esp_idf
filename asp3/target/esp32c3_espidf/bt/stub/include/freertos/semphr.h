/*
 *  BTコントローラ（bt.c）用のFreeRTOS semphr.h互換ヘッダ（コンパイル専用）
 *
 *  xSemaphoreCreateMutex()もxSemaphoreCreateCounting()も同じ
 *  esp_shim_sem_create()（counting／binary両対応）に統一する
 *  （FreeRTOSのSemaphoreHandle_tは生成元を問わず同じ型で以後
 *  take/give/deleteされるため，型で分岐する必要が無いよう単純化．
 *  非再帰ミューテックスとしての用途には十分．bt.cが真の再帰
 *  ロックを要求する箇所は無いことをビルド時に確認する）．
 */
#ifndef TOPPERS_BT_FREERTOS_SEMPHR_H
#define TOPPERS_BT_FREERTOS_SEMPHR_H

#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;

#ifndef TOPPERS_MACRO_ONLY
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);

static inline SemaphoreHandle_t
xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(uxMaxCount, uxInitialCount);
}

static inline SemaphoreHandle_t
xSemaphoreCreateMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 1U);
}

static inline void
vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	esp_shim_sem_delete(xSemaphore);
}

static inline BaseType_t
xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	return (BaseType_t) esp_shim_sem_take(xSemaphore, xTicksToWait);
}

static inline BaseType_t
xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	return (BaseType_t) esp_shim_sem_give(xSemaphore);
}

static inline BaseType_t
xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
					   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_sem_take(xSemaphore, 0);
}

static inline BaseType_t
xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
					   BaseType_t *pxHigherPriorityTaskWoken)
{
	if (pxHigherPriorityTaskWoken != NULL) {
		*pxHigherPriorityTaskWoken = pdFALSE;
	}
	return (BaseType_t) esp_shim_sem_give(xSemaphore);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_SEMPHR_H */
