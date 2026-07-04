/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  テストプログラムのターゲット依存部（ESP32-C6用）
 *
 *  pico2_riscv_gcc版からの流用。RP2350版はTIMER0 ALARM1（実ハード）を
 *  ras_int（Xh3irqの割込み強制ビット）で使っていたが，ESP32-C6では
 *  ソフトウェアでアサートできるFROM_CPU_1ソースを割り当てたCPU割込み
 *  線3をINTNO1とする（ras_int／clr_intはこの線で使用可能．
 *  intmtx_kernel_impl.h参照）．
 */

#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#define STACK_SIZE (1024)

/*
 *  テストプログラムで使用する割込み（FROM_CPU_1ソースを割り当てた
 *  CPU割込み線3．割り当てはtarget_initializeで行う）
 */
#define INTNO1			3
#define INTNO1_INTATR	TA_ENAINT
#define INTNO1_INTPRI	(-2)

/*
 *  INTNO1の割込み要求のクリア
 *
 *  FROM_CPU_1はlevelソース（1を書くとアサート・0を書くまで保持）の
 *  ため，割込みハンドラ内でデアサートする．
 */
#ifndef TOPPERS_MACRO_ONLY
#include <sil.h>
#include "esp32c6.h"
Inline void
intno1_clear(void)
{
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_1, 0U);
}
#endif /* TOPPERS_MACRO_ONLY */

/*
 *  コアで共通な定義
 */
#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
