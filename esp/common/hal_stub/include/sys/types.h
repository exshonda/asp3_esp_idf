/*
 *  esp-hal／mbedtls／wpa_supplicant用のsys/types.hスタブ
 *
 *  ツールチェーン（riscv64-unknown-elf-gcc）にnewlibヘッダが無い環境の
 *  ためのスタブ．size_t／ssize_t／off_t等，最小限の型のみ定義する。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_SYS_TYPES_H
#define TOPPERS_HAL_STUB_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef long ssize_t;
typedef long off_t;
typedef int pid_t;
typedef int uid_t;
typedef int gid_t;
typedef unsigned long mode_t;

/*
 *  time_t／suseconds_t：<time.h>スタブとも共有するため，二重定義を
 *  避けるガードマクロ（TOPPERS_HAL_STUB_TIME_T_DEFINED）で統一する。
 */
#ifndef TOPPERS_HAL_STUB_TIME_T_DEFINED
#define TOPPERS_HAL_STUB_TIME_T_DEFINED
typedef long time_t;
typedef long suseconds_t;
#endif

#endif /* TOPPERS_HAL_STUB_SYS_TYPES_H */
