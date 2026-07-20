/*
 *  nvs.h コンパイル用スタブ（NVS未実装）
 *
 *  esp_phy/src/phy_init.c は `#ifndef __NuttX__` の較正データ
 *  永続化関数（esp_phy_load_cal_data_from_nvs／
 *  esp_phy_store_cal_data_to_nvs／esp_phy_erase_cal_data_in_nvs）を
 *  常時コンパイルする．呼び出し口（esp_phy_load_cal_and_init内）は
 *  `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE` 未定義（本ビルドの
 *  sdkconfig.h＝hal/nuttx/esp32c3/include/sdkconfig.hに既定で無い）
 *  のため通らず，`register_chipv7_phy(init_data, cal_data,
 *  PHY_RF_CAL_FULL)` を直接呼ぶ「毎回フル較正・永続化なし」経路のみ
 *  実行される（esp_shim_blobglue.c 実装コメント参照）．
 *
 *  本ヘッダは上記関数のコンパイルに必要な最小の型・宣言のみを提供し，
 *  実装（esp_shim_blobglue.c）は「NVS未初期化」として振る舞う
 *  （ESP_ERR_NVS_NOT_INITIALIZED固定）．nvs_openが失敗を返すため，
 *  これらの関数自体は（万一到達しても）較正データを書き込まず，
 *  安全側＝毎回フル較正にフォールバックする．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_NVS_H
#define TOPPERS_HAL_STUB_NVS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t	nvs_handle_t;

typedef enum {
	NVS_READONLY = 0,
	NVS_READWRITE
} nvs_open_mode_t;

#define ESP_ERR_NVS_BASE                0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED     (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND           (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH      (ESP_ERR_NVS_BASE + 0x04)

extern esp_err_t nvs_open(const char *name, nvs_open_mode_t open_mode,
						  nvs_handle_t *out_handle);
extern void nvs_close(nvs_handle_t handle);
extern esp_err_t nvs_get_u32(nvs_handle_t handle, const char *key,
							 uint32_t *out_value);
extern esp_err_t nvs_set_u32(nvs_handle_t handle, const char *key,
							 uint32_t value);
extern esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key,
							  void *out_value, size_t *length);
extern esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key,
							  const void *value, size_t length);
extern esp_err_t nvs_erase_all(nvs_handle_t handle);
extern esp_err_t nvs_commit(nvs_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_HAL_STUB_NVS_H */
