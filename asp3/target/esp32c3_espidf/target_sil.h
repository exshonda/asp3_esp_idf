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
 *  sil.hのターゲット依存部（ESP32-C3用）
 *
 *  pico2_riscv_gcc版からの流用。
 */

#ifndef TOPPERS_TARGET_SIL_H
#define TOPPERS_TARGET_SIL_H

/*
 *  エンディアンの定義
 */
#define SIL_ENDIAN_LITTLE

/*
 *  チップで共通な定義
 */
#include "chip_sil.h"

#endif /* TOPPERS_TARGET_SIL_H */
