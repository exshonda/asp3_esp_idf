/*
 *  BSD machine/endian.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  wpa_supplicant/src/utils/common.hが__linux__/__GLIBC__未定義時に
 *  参照する．ESP32-C3（RISC-V）はリトルエンディアン固定。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_MACHINE_ENDIAN_H
#define TOPPERS_HAL_STUB_MACHINE_ENDIAN_H

#define LITTLE_ENDIAN   1234
#define BIG_ENDIAN      4321
#define BYTE_ORDER      LITTLE_ENDIAN

#endif /* TOPPERS_HAL_STUB_MACHINE_ENDIAN_H */
