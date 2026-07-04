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

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>	/* bool_t／sns_ctx() */
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
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_FREERTOS_TASK_H */
