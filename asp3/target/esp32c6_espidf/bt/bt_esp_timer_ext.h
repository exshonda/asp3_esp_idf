/*
 *  ------------------------------------------------------------------
 *  esp_timer.h 補完ヘッダ（IDF v6.1 BT swap 専用．BLE実施13）
 *  ------------------------------------------------------------------
 *  v6.1 の npl_os_freertos.c は esp_timer_is_active()／
 *  esp_timer_get_expiry_time() を（esp_timer.h を include せずに）直接
 *  呼ぶが，本ビルドの esp_timer.h は hal_stub 版
 *  （asp3/target/esp32c3_espidf/hal_stub/include/esp_timer.h）に解決され，
 *  同2関数のプロトタイプを持たない（PLL-track 用の周期タイマ API のみ
 *  宣言）．GCC14.2 は暗黙宣言を hard error にするため，本ヘッダで2関数の
 *  プロトタイプを補う（実体は bt/bt_shim_idf61.c）．
 *
 *  hal_stub/esp_timer.h は C3 領域（別エージェント担当）で編集不可のため，
 *  C6 target 側の本ヘッダを esp_bt_idf61.cmake が force-include する．
 */
#ifndef TOPPERS_C6_BT_ESP_TIMER_EXT_H
#define TOPPERS_C6_BT_ESP_TIMER_EXT_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_timer.h"		/* esp_timer_handle_t／esp_err_t（stub版でも可） */

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer, uint64_t *expiry);
bool esp_timer_is_active(esp_timer_handle_t timer);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_C6_BT_ESP_TIMER_EXT_H */
