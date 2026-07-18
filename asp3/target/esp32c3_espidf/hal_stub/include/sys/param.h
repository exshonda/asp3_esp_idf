/*
 *  BSD sys/param.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  riscv/csr.h（esp-hal-3rdparty）が<sys/param.h>を要求するが，
 *  実際にMIN/MAX等のマクロは使用していない（grep確認済み）ため
 *  空スタブで足りる．必要になった時点でMIN/MAX等を追加する。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
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
