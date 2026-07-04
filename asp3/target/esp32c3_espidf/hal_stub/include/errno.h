/*
 *  esp-hal／mbedtls／wpa_supplicant／lwIP用のerrno.hスタブ
 *
 *  ツールチェーン（riscv64-unknown-elf-gcc）にnewlibヘッダが無い環境の
 *  ためのスタブ．参照されるマクロのみ定義する（不足分は必要時に追加）．
 *  errno実体はos_adapter shim（Phase B-2）で提供する。
 *
 *  [lwIP NO_SYS=0化（sockets/netconn API・Phase C）での追加分]
 *  lwip/src/api/{sockets,api_msg,api_lib,err,netifapi,if_api,tcpip}.c は
 *  いずれも `#include "lwip/errno.h"`（lwip同梱のerrno.h＝lwip/src/include/
 *  lwip/errno.h）を参照する．同ファイルはLWIP_PROVIDE_ERRNOが未定義の場合、
 *  LWIP_ERRNO_STDINCLUDE経由で`#include <errno.h>`に委譲する設計になって
 *  おり（newlib非搭載環境向けの想定挙動）、この`<errno.h>`はincludeパス上
 *  newlibが存在しないため本ファイルに解決される．
 *  → 本プロジェクトの既存パターン（string.h/stdio.h等、単一フラット
 *    ヘッダで必要最小限のシンボルのみ提供）に合わせ、lwIP側の内蔵表
 *    （LWIP_PROVIDE_ERRNO）を使わずLWIP_ERRNO_STDINCLUDE経由で本ファイルに
 *    委譲する方式を採用する（lwipopts.h/cc.h側でLWIP_ERRNO_STDINCLUDEを
 *    定義するのは本タスクの対象外＝別変更で対応）．
 *  値はlwip/src/include/lwip/errno.h内蔵テーブル（Linux/glibc準拠の
 *  標準POSIX番号）に合わせてある．
 *
 *  [既知の制限]
 *  `errno`はプロセス（ASP3内では実質シングルアドレス空間）で共有される
 *  単一のグローバル変数であり、タスクごとの保存領域を持たない．複数タスク
 *  が同時にソケットAPIを呼び出すと、あるタスクが立てたerrnoを別タスクが
 *  上書き・誤読する競合が理論上あり得る（例：ASP3はTOPPERS系で協調的
 *  ディスパッチではないため割込み契機のタスク切替えで発生し得る）。
 *  本タスクの対象外のため、per-task errno化は行わずこの制限を明記するに
 *  留める．
 */
#ifndef TOPPERS_HAL_STUB_ERRNO_H
#define TOPPERS_HAL_STUB_ERRNO_H

extern int errno;

#define EIO              5  /* I/O error */
#define ENXIO            6  /* No such device or address */
#define EBADF            9  /* Bad file number */
#define EAGAIN          11  /* Try again */
#define ENOMEM          12  /* Out of memory */
#define EFAULT          14  /* Bad address */
#define EBUSY           16  /* Device or resource busy */
#define ENODEV          19  /* No such device */
#define EINVAL          22  /* Invalid argument */
#define ENFILE          23  /* File table overflow */
#define ENOSPC          28  /* No space left on device */
#define ENOSYS          38  /* Function not implemented */
#define EMSGSIZE        90  /* Message too long */
#define ENOPROTOOPT     92  /* Protocol not available */
#define EOPNOTSUPP      95  /* Operation not supported on transport endpoint */
#define EAFNOSUPPORT    97  /* Address family not supported by protocol */
#define EADDRINUSE      98  /* Address already in use */
#define EADDRNOTAVAIL   99  /* Cannot assign requested address */
#define ECONNABORTED   103  /* Software caused connection abort */
#define ECONNRESET     104  /* Connection reset by peer */
#define ENOBUFS        105  /* No buffer space available */
#define EISCONN        106  /* Transport endpoint is already connected */
#define ENOTCONN       107  /* Transport endpoint is not connected */
#define EHOSTUNREACH   113  /* No route to host */
#define EALREADY       114  /* Operation already in progress */
#define EINPROGRESS    115  /* Operation now in progress */
#define EWOULDBLOCK    EAGAIN  /* Operation would block */

#endif /* TOPPERS_HAL_STUB_ERRNO_H */
