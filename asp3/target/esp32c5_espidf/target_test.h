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
 *  テストプログラムのターゲット依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/target/esp32c6_espidf/target_test.h相当）から
 *  のコピー・C5対応。ソフトウェアでアサートできるFROM_CPU_1ソースを
 *  割り当てたCPU割込み線3（ASP3のINTNO=18）をINTNO1とする（ras_int／
 *  clr_intはこの線で使用可能．clic_kernel_impl.h参照）。CLIC移行後も
 *  FROM_CPU_n（INTPRIペリフェラル）はCLICの管理対象外（ソースルーティ
 *  ング側の機構）のため，ロジックは変更していない。
 */

#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#define STACK_SIZE (1024)

/*
 *  テストプログラムで使用する割込み（FROM_CPU_1ソースを割り当てた
 *  CPU割込み線3．割り当てはtarget_initializeで行う）
 */
#define INTNO1			18	/* Wi-Fi shimが線1-15を占有する想定のため退避 */
#define INTNO1_INTATR	TA_ENAINT
#define INTNO1_INTPRI	(-2)

/*
 *  INTNO1の割込み要求のクリア
 *
 *  FROM_CPU_1はlevelソース（1を書くとアサート・0を書くまで保持）の
 *  ため，割込みハンドラ内でデアサートする。
 */
#ifndef TOPPERS_MACRO_ONLY
#include <sil.h>
#include "esp32c5.h"
Inline void
intno1_clear(void)
{
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_1, 0U);
}
#endif /* TOPPERS_MACRO_ONLY */

/*
 *  コアで共通な定義
 */
#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
