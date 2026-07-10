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
 *    kernel_impl.hのターゲット依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/target/esp32c6_espidf/target_kernel_impl.h相当）
 *  からのコピー・C5対応。esp32c6.h→esp32c5.hへ差替え。
 *
 *  このヘッダファイルは，kernel_impl.h（または，そこからインクルード
 *  されるファイル）のみからインクルードされる．
 */

#ifndef TOPPERS_TARGET_KERNEL_IMPL_H
#define TOPPERS_TARGET_KERNEL_IMPL_H

/*
 *  ボード依存の定義（クロック・SIL_DLY_TIM）
 */
#include "esp32c5.h"

#include <sil.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 *  ターゲットシステム依存の初期化
 */
extern void	target_initialize(void);

/*
 *  ターゲットシステムの終了
 */
extern void	target_exit(void) NoReturn;

/*
 *  エラー発生時の処理
 */
extern void	Error_Handler(void);

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  チップ依存モジュール（CLIC＋INTMTX）
 */
#include "chip_kernel_impl.h"

#endif /* TOPPERS_TARGET_KERNEL_IMPL_H */
