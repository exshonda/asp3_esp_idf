/*
 *  BSD machine/endian.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  wpa_supplicant/src/utils/common.hが__linux__/__GLIBC__未定義時に
 *  参照する．ESP32-C3（RISC-V）はリトルエンディアン固定。
 */
#ifndef TOPPERS_HAL_STUB_MACHINE_ENDIAN_H
#define TOPPERS_HAL_STUB_MACHINE_ENDIAN_H

#define LITTLE_ENDIAN   1234
#define BIG_ENDIAN      4321
#define BYTE_ORDER      LITTLE_ENDIAN

#endif /* TOPPERS_HAL_STUB_MACHINE_ENDIAN_H */
