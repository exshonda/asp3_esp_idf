/*
 *  esp-hal／mbedtls用のinttypes.hスタブ（PRI*マクロのみ．
 *  stdint.hはGCC同梱のfreestandingヘッダを使う）
 */
#ifndef TOPPERS_HAL_STUB_INTTYPES_H
#define TOPPERS_HAL_STUB_INTTYPES_H

#include <stdint.h>

#define PRId8   "d"
#define PRIu8   "u"
#define PRIx8   "x"
#define PRId16  "d"
#define PRIu16  "u"
#define PRIx16  "x"
#define PRId32  "ld"
#define PRIu32  "lu"
#define PRIx32  "lx"
#define PRIX32  "lX"
#define PRId64  "lld"
#define PRIu64  "llu"
#define PRIx64  "llx"

#endif /* TOPPERS_HAL_STUB_INTTYPES_H */
