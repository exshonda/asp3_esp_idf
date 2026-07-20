/*
 *  BTコントローラ（bt.c）用のFreeRTOS task.h互換ヘッダ（コンパイル専用）
 */
#ifndef TOPPERS_BT_FREERTOS_TASK_H
#define TOPPERS_BT_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;

/*
 *  コア親和性指定なし（ESP32-C3は単一コアのため意味を持たないが，
 *  bt.cのtask_create_wrapperがcore_id引数の比較・代入に使う）
 */
#define tskNO_AFFINITY	((BaseType_t) 0x7fffffff)

/*
 *  スケジューラ状態（NimBLE NPL npl_freertos_os_startedは
 *  != taskSCHEDULER_NOT_STARTED で判定．ASP3はカーネル起動後は常に
 *  RUNNING．configTICK_RATE_HZはFreeRTOS.hで1000＝1ms/tick）
 */
#define taskSCHEDULER_NOT_STARTED	0
#define taskSCHEDULER_SUSPENDED		1
#define taskSCHEDULER_RUNNING		2

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>	/* bool_t／sns_ctx()／get_tid()／esp_shim_time_us() */
extern int64_t esp_shim_time_us(void);
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									 uint32_t stack_size, void *param,
									 uint32_t freertos_prio, void **task_handle);
extern void esp_shim_task_delete(void *task_handle);
extern void esp_shim_task_delay(uint32_t tick);

static inline BaseType_t
xTaskCreatePinnedToCore(TaskFunction_t pvTaskCode, const char *pcName,
						 uint32_t usStackDepth, void *pvParameters,
						 UBaseType_t uxPriority, TaskHandle_t *pvCreatedTask,
						 BaseType_t xCoreID)
{
	(void) xCoreID;
	return (BaseType_t) esp_shim_task_create(pvTaskCode, pcName, usStackDepth,
											  pvParameters, uxPriority,
											  pvCreatedTask);
}

static inline void
vTaskDelete(TaskHandle_t task)
{
	esp_shim_task_delete(task);
}

static inline void
vTaskDelay(TickType_t ticks)
{
	esp_shim_task_delay(ticks);
}

/*
 *  bt.cのosi_funcs_ro（_task_yield）が直接指す関数ポインタ．ASP3の
 *  ラウンドロビン相当（同一優先度の他タスクへ回す）rot_rdq()を使う．
 */
static inline void
vPortYield(void)
{
	(void) rot_rdq(TPRI_SELF);
}

/*
 *  bt.cはISR文脈かどうかの判定にxPortInIsrContext()を使う．
 *  ASP3標準API sns_ctx()（非タスクコンテキストで真．<kernel.h>で
 *  既に宣言済み）をそのまま使う．
 */
static inline BaseType_t
xPortInIsrContext(void)
{
	return (BaseType_t) sns_ctx();
}

/*
 *  NimBLE NPL（npl_os_freertos.c）が要求する追加API．
 */
static inline TaskHandle_t
xTaskGetCurrentTaskHandle(void)
{
	ID	self = 0;

	(void) get_tid(&self);
	return (TaskHandle_t)(intptr_t) self;
}

static inline BaseType_t
xTaskGetSchedulerState(void)
{
	/*  ASP3はカーネル起動（sta_ker）後は常時スケジューリング動作中  */
	return (BaseType_t) taskSCHEDULER_RUNNING;
}

/*
 *  現在のtick（configTICK_RATE_HZ=1000＝1ms単位）．SYSTIMER（μs）を
 *   msへ換算．レジスタ読取りのみでISRセーフ．
 */
static inline TickType_t
xTaskGetTickCountFromISR(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}

static inline TickType_t
xTaskGetTickCount(void)
{
	return (TickType_t)(esp_shim_time_us() / 1000);
}
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_TASK_H */
