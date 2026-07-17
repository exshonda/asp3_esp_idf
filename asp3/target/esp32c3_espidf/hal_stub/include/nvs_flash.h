/*
 *  nvs_flash.h コンパイル用スタブ（NVS未実装）
 *
 *  esp_phy/src/phy_init.c が `#ifndef __NuttX__` で#includeするのみ
 *  （本ビルドはnvs_flash_init()自体を呼ばない＝NVS未初期化のまま，
 *  nvs.h参照．esp_shim_blobglue.c参照）．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_NVS_FLASH_H
#define TOPPERS_HAL_STUB_NVS_FLASH_H

#include "nvs.h"

#endif /* TOPPERS_HAL_STUB_NVS_FLASH_H */
