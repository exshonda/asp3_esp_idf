/*
 *  BTコントローラ（bt.c）用のFreeRTOS互換ヘッダ（コンパイル専用スタブ）
 *
 *  bt.cはosi関数テーブル越しではなく，FreeRTOS APIをインラインで直接
 *  呼ぶ（Wi-Fi blobとの本質的な違い．docs/dev/esp-idf-integration.md
 *  Phase D参照）．実体はwifi/esp_shim.cの既存プリミティブへ委譲する
 *  （新しいプリミティブの発明はしない．ヘッダ単位でのシムという
 *  差し替え粒度が新規なだけ）．
 */
#ifndef TOPPERS_BT_FREERTOS_H
#define TOPPERS_BT_FREERTOS_H

#include <platform/os.h>	/* TickType_t/UBaseType_t/BaseType_t */

#define pdFALSE		0
#define pdTRUE		1
#define pdPASS		1
#define pdFAIL		0

#define portMAX_DELAY		0xffffffffUL	/* ESP_SHIM_BLOCK_FOREVERと同値 */
#define portTICK_PERIOD_MS	1		/* esp_shim側もtick=1msの前提 */

typedef void (*TaskFunction_t)(void *);

/*
 *  クリティカルセクション（ESP32-C3は単一コアのためスピンロックは
 *  意味を持たない．mux変数自体をesp_shim_int_disable()の退避値の
 *  格納先として再利用する）
 */
#ifndef TOPPERS_MACRO_ONLY
extern uint32_t esp_shim_int_disable(void);
extern void esp_shim_int_restore(uint32_t state);
#endif

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED	0

#define portENTER_CRITICAL(mux)		(*(mux) = esp_shim_int_disable())
#define portEXIT_CRITICAL(mux)		(esp_shim_int_restore(*(mux)))
#define portENTER_CRITICAL_ISR(mux)	portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_ISR(mux)	portEXIT_CRITICAL(mux)
#define portENTER_CRITICAL_SAFE(mux)	portENTER_CRITICAL(mux)
#define portEXIT_CRITICAL_SAFE(mux)	portEXIT_CRITICAL(mux)

/*
 *  ASP3は割込み出口で自動的に再スケジューリングするため，ISR文脈からの
 *  明示的なyield要求は不要（no-op）．bt.cは引数無し／有り両方の
 *  呼び方をするため可変長引数マクロにする．
 */
#define portYIELD_FROM_ISR(...)	((void) 0)

#endif /* TOPPERS_BT_FREERTOS_H */
