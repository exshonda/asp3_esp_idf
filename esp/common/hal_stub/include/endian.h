/*
 *  BSD/glibc endian.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．ESP32-C3
 *  （RISC-V）はリトルエンディアン固定なので，htoleおよびleXXtohは恒等，
 *  htobeおよびbeXXtohはバイトスワップとして実装する（インライン関数．
 *  リンク不要）。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_ENDIAN_H
#define TOPPERS_HAL_STUB_ENDIAN_H

#include <stdint.h>
#include <machine/endian.h>

static inline uint16_t htole16(uint16_t x) { return x; }
static inline uint16_t le16toh(uint16_t x) { return x; }
static inline uint32_t htole32(uint32_t x) { return x; }
static inline uint32_t le32toh(uint32_t x) { return x; }

static inline uint16_t htobe16(uint16_t x)
{
	return (uint16_t)((x << 8) | (x >> 8));
}
static inline uint16_t be16toh(uint16_t x) { return htobe16(x); }

static inline uint32_t htobe32(uint32_t x)
{
	return ((x & 0x000000ffU) << 24) | ((x & 0x0000ff00U) << 8) |
	       ((x & 0x00ff0000U) >> 8)  | ((x & 0xff000000U) >> 24);
}
static inline uint32_t be32toh(uint32_t x) { return htobe32(x); }

#endif /* TOPPERS_HAL_STUB_ENDIAN_H */
