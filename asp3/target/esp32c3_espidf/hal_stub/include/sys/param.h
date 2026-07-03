/*
 *  BSD sys/param.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  riscv/csr.h（esp-hal-3rdparty）が<sys/param.h>を要求するが，
 *  実際にMIN/MAX等のマクロは使用していない（grep確認済み）ため
 *  空スタブで足りる．必要になった時点でMIN/MAX等を追加する。
 */
#ifndef TOPPERS_HAL_STUB_SYS_PARAM_H
#define TOPPERS_HAL_STUB_SYS_PARAM_H

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif /* TOPPERS_HAL_STUB_SYS_PARAM_H */
