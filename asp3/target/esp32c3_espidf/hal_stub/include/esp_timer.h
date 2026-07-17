/*
 *  esp_timer.h コンパイル用スタブ
 *
 *  実物（esp-hal-3rdparty components/esp_timer/include/esp_timer.h）
 *  は esp_etm.h（Event Task Matrix．esp_hal_etmコンポーネント）まで
 *  依存が伸びる本格的な周期タイマAPIヘッダ．esp_phy/src/phy_init.c・
 *  phy_common.cが#includeするが，周期タイマ機能自体
 *  （esp_timer_create／esp_timer_start_periodic等＝phy_track_pll系）
 *  はCONFIG_ESP_PHY_DISABLE_PLL_TRACK=1（esp_wifi.cmake §1c）により
 *  未使用（到達不能コード）＝プロトタイプ宣言のみで足りる．
 *
 *  esp_timer_get_time()のみ実体を提供する（esp_shim_libc.c．
 *  esp_shim_time_us()＝SYSTIMERへ委譲）．他コンポーネント
 *  （wpa_supplicant／mbedtls／esp_wifi）は現行ビルドで
 *  esp_timer.hを#includeしないため，本スタブに置き換えても影響
 *  しない（2026-07-03確認）．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_ESP_TIMER_H
#define TOPPERS_HAL_STUB_ESP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);

typedef struct {
	esp_timer_cb_t	callback;
	void			*arg;
	int				dispatch_method;	/* 実物のesp_timer_dispatch_t．未使用 */
	const char		*name;
	bool			skip_unhandled_events;
} esp_timer_create_args_t;

extern int64_t esp_timer_get_time(void);

/*
 *  以下は宣言のみ（到達不能．CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1参照）
 */
extern esp_err_t esp_timer_create(const esp_timer_create_args_t *create_args,
								  esp_timer_handle_t *out_handle);
extern esp_err_t esp_timer_delete(esp_timer_handle_t timer);
extern esp_err_t esp_timer_start_once(esp_timer_handle_t timer,
									  uint64_t timeout_us);
extern esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,
										  uint64_t period_us);
extern esp_err_t esp_timer_stop(esp_timer_handle_t timer);

/*
 *  以下2本はBLE経路（bt/porting/npl/freertos/src/npl_os_freertos.c．
 *  npl_freertos_callout_is_active／npl_freertos_callout_get_ticks）が
 *  実際に呼ぶ＝到達可能．本スタブはinclude順で本物
 *  （esp-idf/components/esp_timer/include/esp_timer.h）より前に来て
 *  shadowするため，宣言を欠くとimplicit declarationになる
 *  （GCC13では警告／GCC14+ではエラー）．
 *  ★シグネチャは本物の逐語コピー（esp_timer.h:269／:322）．
 *  esp_timer_is_activeの戻り値はintではなくbool＝暗黙宣言との差が
 *  実際にコード生成へ出る（呼出側がboolへ変換するsnezの有無）．
 */
extern bool esp_timer_is_active(esp_timer_handle_t timer);
extern esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer,
										   uint64_t *expiry);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_HAL_STUB_ESP_TIMER_H */
