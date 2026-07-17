/*
 *  esp-hal／mbedtls用のinttypes.hスタブ（PRI*マクロのみ．
 *  stdint.hはGCC同梱のfreestandingヘッダを使う）
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
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
