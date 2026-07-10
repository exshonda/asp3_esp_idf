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
 *  kernel.hのターゲット依存部（ESP32-C5用）
 *
 *  pico2_riscv_gcc版からの流用。
 */

#ifndef TOPPERS_TARGET_KERNEL_H
#define TOPPERS_TARGET_KERNEL_H

#ifdef USE_TIM_AS_HRT

/*
 *  高分解能タイマのタイマ周期（32ビットフリーラン＝周期は定義しない）
 */
/* TCYC_HRTCNTは定義しない．*/

/*
 *  高分解能タイマのカウント値の進み幅（1MHz＝1μs）
 */
#define TSTEP_HRTCNT 1U

#endif /* USE_TIM_AS_HRT */

/*
 *  チップで共通な定義
 */
#include "chip_kernel.h"

#endif /* TOPPERS_TARGET_KERNEL_H */
