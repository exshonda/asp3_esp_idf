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
 *    アセンブリ言語用マクロ定義（ESP32-C5用）
 *
 *  pico2_riscv/rp2350版からの流用．内容差分なし（追加コンテキストなし）．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#ifndef TOPPERS_CHIP_ASM_INC
#define TOPPERS_CHIP_ASM_INC

/*
 *  CPU例外番号(exccode)の取得（CLIC版）
 *  CLICのmcauseは上位にMPP/MPIE/MPIL等を持つため，下位12bit(exccode)のみを
 *  残す（RV32：slli/srli 20）．共通部core_support.Sがexc_table索引前に呼ぶ．
 *  未マスクだとexc_table索引が範囲外→不正ジャンプ→多重フォルト→CPU_LOCKUP．
 *  一時レジスタ不要．FMP3のesp32p4/chip_asm.incと同型．
 */
.macro core_get_exccode_asm reg
    csrr  \reg, mcause
    slli  \reg, \reg, 20
    srli  \reg, \reg, 20
.endm

/*
 *  ディスパッチ時の追加コンテキストの保存
 */
.macro save_additional_regs_disp
.endm

/*
 *  ディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_disp
.endm

/*
 *  割込み・例外ハンドラ入口処理での追加コンテキストの保存
 */
.macro save_additional_regs_int
.endm

/*
 *  割込み・例外ハンドラ入口処理で保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int
.endm

/*
 *  割込み・例外ハンドラからのディスパッチ時の追加コンテキストの保存
 */
.macro save_additional_regs_int_disp
.endm

/*
 *  割込み・例外ハンドラからのディスパッチ時に保存した追加コンテキストの復帰
 */
.macro restore_additional_regs_int_disp
.endm

/*
 *  タスク実行開始時(start_r)のTLS(スレッドローカルストレージ)初期化．
 *  ESP-IDF v6.1 picolibcのrand()等_Thread_local変数はtp(スレッドポインタ)相対でアクセスされるが，
 *  ASP3はタスク毎にtpを設定しないためtp=0のまま残り，TLS依存libc関数呼出し
 *  でtp+offset(≈0)へのLoad access faultになりうる．
 *
 *  ESP-IDF FreeRTOSポート(uxInitialiseStackTLS)と同じ方式：リンカ提供のTLS
 *  初期化イメージ(.tdataの値+.tbssのゼロ)を，タスクスタック最下位
 *  (p_tinib->stk)へコピーし，その先頭をtpとする．新規タスク開始時点
 *  (activate_context: sp=stk+stksz)ではスタックは丸ごと未使用のため安全．
 *  .tbssはNOLOADのため_thread_local_bss_startからは読まずサイズ計算のみに
 *  用いる．
 *
 *  レジスタ: \tcb_reg(a0=p_runtsk)は読むだけで不変．t0-t3/tpを破壊．
 */
#ifdef TOPPERS_SUPPORT_TLS
.macro init_additional_regs_start_r tcb_reg
    lw    t0, TCB_p_tinib(\tcb_reg)        /* t0 = p_tinib(RV32固定のためlw．
                                             * lxはcore_asm.incが本ファイル
                                             * 末尾の#includeより後で#define
                                             * するためここではまだ未定義) */
    lw    tp, TINIB_stk(t0)                /* tp = p_tinib->stk(基準点，以後不変) */
    mv    t0, tp                           /* コピー先カーソル */
    la    t1, _thread_local_data_start     /* .tdata コピー元 */
    la    t2, _thread_local_data_end
.Ltls_data_loop\@:
    beq   t1, t2, .Ltls_bss_start\@
    lb    t3, 0(t1)
    sb    t3, 0(t0)
    addi  t1, t1, 1
    addi  t0, t0, 1
    j     .Ltls_data_loop\@
.Ltls_bss_start\@:
    la    t1, _thread_local_bss_start      /* .tbss はサイズ計算のみ(読まない) */
    la    t2, _thread_local_bss_end
.Ltls_bss_loop\@:
    beq   t1, t2, .Ltls_done\@
    sb    zero, 0(t0)
    addi  t0, t0, 1
    addi  t1, t1, 1
    j     .Ltls_bss_loop\@
.Ltls_done\@:
.endm
#else /* TOPPERS_SUPPORT_TLS */
.macro init_additional_regs_start_r tcb_reg
.endm
#endif /* TOPPERS_SUPPORT_TLS */

/*
 *  コア依存部で共通な定義
 */
#include "core_asm.inc"

#endif /* TOPPERS_CHIP_ASM_INC */
