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
extern uint32_t esp_shim_sem_get_count(void *sem);
/*  再帰対応ミューテックス（owner/countをesp_shim側が追跡．NimBLE NPL用）  */
extern void *esp_shim_mutex_create(bool_t recursive);
extern void esp_shim_mutex_delete(void *mtx);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_unlock(void *mtx);

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

/*
 *  バイナリセマフォ（NimBLE esp_nimble_hci.c が使用）．FreeRTOS の
 *  バイナリセマフォは生成直後は空（取得前に give が必要）＝max=1／init=0．
 */
static inline SemaphoreHandle_t
xSemaphoreCreateBinary(void)
{
	return (SemaphoreHandle_t) esp_shim_sem_create(1U, 0U);
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

static inline UBaseType_t
uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore)
{
	return (UBaseType_t) esp_shim_sem_get_count(xSemaphore);
}

/*
 *  NimBLE NPLの再帰ミューテックス（os_mutex＝同一タスク再取得可）．
 *  esp_shim_mutex_*（owner/count追跡）へ委譲する．TakeRecursiveの
 *  タイムアウト引数は無視（下地はloc_mtxで永久ブロック．NimBLEの
 *  os_mutex_pendは実質FOREVER運用）．
 */
static inline SemaphoreHandle_t
xSemaphoreCreateRecursiveMutex(void)
{
	return (SemaphoreHandle_t) esp_shim_mutex_create(true);
}

static inline BaseType_t
xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait)
{
	(void) xTicksToWait;
	return (BaseType_t) esp_shim_mutex_lock(xMutex);
}

static inline BaseType_t
xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex)
{
	return (BaseType_t) esp_shim_mutex_unlock(xMutex);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_SEMPHR_H */
