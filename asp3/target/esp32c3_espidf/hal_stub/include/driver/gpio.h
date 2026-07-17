/*
 *  driver/gpio.h コンパイル用スタブ
 *
 *  esp_phy/src/phy_common.c（esp_phy_set_ant/phy_ant_set_gpio_output＝
 *  アンテナ切替GPIO設定．esp_wifi_set_ant()相当のオプションAPIで，
 *  本ビルド（wifi_scanデモ）は呼び出さない＝到達不能コード）が
 *  #include "driver/gpio.h" するために必要な最小スタブ．
 *
 *  実物（esp-hal-3rdparty components/upper_hal_gpio/include/driver/
 *  gpio.h）はdriver/gpio_etm.h→esp_etm.h（Event Task Matrixドライバ）
 *  まで芋づる式に依存が伸びる．本スタブが提供するのは
 *  phy_common.cが実際に使う型・プロトタイプ（gpio_config_t／
 *  gpio_config()宣言）のみ＝gpio_config()は呼び出し側ごと
 *  --gc-sections（-ffunction-sections有効）でリンク対象から
 *  除外される想定のため実体は提供しない（未参照シンボルは
 *  リンクエラーにならない．実際にesp_phy_set_ant()等を呼ぶ
 *  アプリコードが現れた場合はリンクエラーとして顕在化し，
 *  その時点で本物のGPIOドライバ統合を検討する）．
 *
 *  gpio_mode_t／gpio_pullup_t／gpio_pulldown_t／gpio_int_type_t／
 *  gpio_num_tは実物のhal/gpio_types.h（esp_hal_gpio/include，
 *  target.cmakeで既にインクルードパス追加済み）からそのまま使う．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_DRIVER_GPIO_H
#define TOPPERS_HAL_STUB_DRIVER_GPIO_H

#include <stdint.h>
#include "esp_err.h"
#include "hal/gpio_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint64_t		pin_bit_mask;
	gpio_mode_t		mode;
	gpio_pullup_t	pull_up_en;
	gpio_pulldown_t	pull_down_en;
	gpio_int_type_t	intr_type;
} gpio_config_t;

extern esp_err_t gpio_config(const gpio_config_t *pGPIOConfig);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_HAL_STUB_DRIVER_GPIO_H */
