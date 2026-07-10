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
 *    sil.hのチップ依存部（ESP32-C5用）
 *
 *  pico2_riscv/rp2350版からの流用．RP2350のバスファブリックが提供する
 *  アトミックアクセスエイリアス（+0x1000:XOR／+0x2000:SET／+0x3000:CLR）
 *  はESP32-C5には存在しないため，通常のread-modify-writeに置き換えた
 *  （シングルコア・呼出し側で割込み禁止が前提のSIL規約どおり）．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#ifndef TOPPERS_CHIP_SIL_H
#define TOPPERS_CHIP_SIL_H

/*
 *  コアで共通な定義
 */
#include "core_sil.h"

/*
 *  ビット操作（read-modify-write）
 */
#define sil_mskb( mem, val, msk ) sil_wrb_mem(mem, (sil_reb_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orb( mem, val )  sil_wrb_mem(mem, sil_reb_mem(mem) | (val))
#define sil_andb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & (val))
#define sil_clrb( mem, val ) sil_wrb_mem(mem, sil_reb_mem(mem) & ~(val))
#define sil_mskh( mem, val, msk ) sil_wrh_mem(mem, (sil_reh_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orh( mem, val )  sil_wrh_mem(mem, sil_reh_mem(mem) | (val))
#define sil_andh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & (val))
#define sil_clrh( mem, val ) sil_wrh_mem(mem, sil_reh_mem(mem) & ~(val))
#define sil_mskw( mem, val, msk ) sil_wrw_mem(mem, (sil_rew_mem(mem) & ~(msk)) | ((val) & (msk)))
#define sil_orw( mem, val )  sil_wrw_mem(mem, sil_rew_mem(mem) | (val))
#define sil_andw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & (val))
#define sil_clrw( mem, val ) sil_wrw_mem(mem, sil_rew_mem(mem) & ~(val))

#include "esp32c5.h"

#endif /* TOPPERS_CHIP_SIL_H */
