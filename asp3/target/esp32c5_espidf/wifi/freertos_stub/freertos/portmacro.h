/*
 *  【実施10】FreeRTOS portmacro.h 互換スタブ（コンパイル専用）。
 *
 *  IDF v6.1 の esp_phy/src/phy_init.c は PHY 用スピンロックを
 *  portMUX_TYPE + portENTER_CRITICAL(_ISR)/portEXIT_CRITICAL(_ISR) で実装する
 *  （hal(v8) の phy_init.c は newlib _lock_acquire/_release を使っていた
 *  ＝この差分が IDF 移行で顕在化）。ASP3 はシングルコアのためスピンは
 *  無意味で，割込み禁止/復元（esp_shim_int_disable/restore）で十分。
 *  退避した割込み状態は mux（portMUX_TYPE）自身へ格納する（phy_init の
 *  使用は非ネストの ENTER…EXIT 対のためこれで正しい）。
 */
#ifndef TOPPERS_C5_WIFI_FREERTOS_PORTMACRO_H
#define TOPPERS_C5_WIFI_FREERTOS_PORTMACRO_H

#include "freertos/FreeRTOS.h"

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>	/* sns_ctx() */

extern uint32_t esp_shim_int_disable(void);
extern void esp_shim_int_restore(uint32_t state);

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED	0

static inline void
vPortEnterCritical(portMUX_TYPE *mux)
{
	*mux = esp_shim_int_disable();
}

static inline void
vPortExitCritical(portMUX_TYPE *mux)
{
	esp_shim_int_restore(*mux);
}

static inline BaseType_t
xPortInIsrContext(void)
{
	return (BaseType_t) sns_ctx();
}

#define portENTER_CRITICAL(mux)			vPortEnterCritical(mux)
#define portEXIT_CRITICAL(mux)			vPortExitCritical(mux)
#define portENTER_CRITICAL_ISR(mux)		vPortEnterCritical(mux)
#define portEXIT_CRITICAL_ISR(mux)		vPortExitCritical(mux)
#define portENTER_CRITICAL_SAFE(mux)	vPortEnterCritical(mux)
#define portEXIT_CRITICAL_SAFE(mux)		vPortExitCritical(mux)

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_C5_WIFI_FREERTOS_PORTMACRO_H */
