/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *    kernel.hのチップ依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip_kernel.h）からの
 *  コピー・C5対応．ESP32-C6はCPU側割込み制御が独自"PLIC"命名のINTMTX
 *  方式だったが，ESP32-C5は標準RISC-V CLICを採用する（設計判断は
 *  clic_kernel_impl.h冒頭コメント参照）．割込み優先度の範囲・サポート
 *  機能の定義自体はC6と同じ7段階（1〜7）のままでよい（CLICのNLBITS=3で
 *  ちょうど8段階＝0〜7が表現でき，1〜7がASP3の有効優先度域と一致する
 *  ため．clic_kernel_impl.h参照）．
 *
 *  このヘッダファイルは，target_kernel.h（または，そこからインクルー
 *  ドされるファイル）のみからインクルードされる．他のファイルから直接
 *  インクルードしてはならない．
 */

#ifndef TOPPERS_CHIP_KERNEL_H
#define TOPPERS_CHIP_KERNEL_H

/*
 *  割込み優先度の範囲
 *
 *  CLICのCLIC_INT_CTRL_REG(i)のCTLフィールドはNLBITS=3（clic_reg.h）に
 *  より実効8段階（0〜7）．優先度0は割込み優先度マスク全解除状態でも
 *  マスクされる値として空け，-1〜-7を内部表現1〜7に対応付ける
 *  （clic_kernel_impl.h参照）．
 */
#define TMIN_INTPRI  (-7)    /* 割込み優先度の最小値（最高値）*/
#define TMAX_INTPRI  (-1)    /* 割込み優先度の最大値（最低値）*/

/*
 *  サポートできる機能の定義
 *
 *  ena_int／dis_int／clr_int／ras_int／prb_intをサポートする
 *  （clr_int／ras_intは，ソフトウェアでアサートできるFROM_CPUソースを
 *  割り当てた割込み線のみ．clic_kernel_impl.h参照）．
 */
#define TOPPERS_TARGET_SUPPORT_ENA_INT    /* ena_int */
#define TOPPERS_TARGET_SUPPORT_DIS_INT    /* dis_int */
#define TOPPERS_TARGET_SUPPORT_CLR_INT    /* clr_int */
#define TOPPERS_TARGET_SUPPORT_RAS_INT    /* ras_int */
#define TOPPERS_TARGET_SUPPORT_PRB_INT    /* prb_int */

/*
 *  コアで共通な定義
 */
#include "core_kernel.h"

#endif /* TOPPERS_CHIP_KERNEL_H */
