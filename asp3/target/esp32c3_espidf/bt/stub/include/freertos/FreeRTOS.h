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
#define configTICK_RATE_HZ	1000		/* tick=1ms（NimBLE NPLの時間換算用） */

typedef void (*TaskFunction_t)(void *);

/*
 *  クリティカルセクション（ESP32-C3は単一コアのためスピンロックは
 *  意味を持たない．割込み状態はmuxではなく大域ネストカウンタで退避する
 *  ＝BTコントローラが同一muxを入れ子で取得（GLOBAL_INT_DISABLE系）しても
 *  最外の解放でMIEが正しく復元される．旧実装＝退避値をmuxに格納する方式は
 *  内側取得が外側のMIE退避値を上書きし，割込み禁止のまま残る欠陥が
 *  あった＝コントローラ実行ループのsemphr_takeがE_CTXでtake失敗→
 *  タスクexit→RTC_SW_SYS_RESET．docs/bt-shim.md参照）
 */
#ifndef TOPPERS_MACRO_ONLY
extern void esp_shim_enter_critical(void);
extern void esp_shim_exit_critical(void);
#endif

typedef uint32_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED	0

#define portENTER_CRITICAL(mux)		(esp_shim_enter_critical(), (void)(mux))
#define portEXIT_CRITICAL(mux)		(esp_shim_exit_critical(),  (void)(mux))
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
