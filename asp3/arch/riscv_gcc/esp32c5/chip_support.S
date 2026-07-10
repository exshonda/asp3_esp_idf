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
 *    カーネルのチップ依存部のアセンブリ言語部（ESP32-C5用．標準RISC-V CLIC）
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip_support.S）からの
 *  コピー・C5対応。C6はvectoredモード（32エントリのジャンプ命令配列，
 *  mcause&0x1fが直接テーブル索引）だったが，ESP32-C5はCLIC搭載チップの
 *  ためトラップベクタ方式・優先度制御方式とも全面的に書き換えた
 *  （設計判断はdocs/c5-port-design.md §4・§8.2，clic_kernel_impl.h冒頭
 *  コメント参照）。
 *
 *  ■ トラップベクタ方式（non-vectoredを第一候補として採用）
 *  CLICのnon-vectoredモード（全CPU割込み線のCLIC_INT_ATTR.shv=0．
 *  clic_kernel_impl.hのclic_initialize()で設定）を採用したため，
 *  例外・割込みとも単一のエントリ（trap_vector_table，mtvecが指す
 *  base．64byteアライン要求＝hal: riscv/vectors_clic.Sのコメント参照．
 *  C6の256byteアラインとは異なる）へ分岐する。ASP3共通部
 *  （arch/riscv_gcc/common/core_support.S）は，まさにこの状況のために
 *  あらかじめ`USE_RISCV_DIRECT_TRAP`という分岐を持っており
 *  （core_int_entry冒頭でmcauseのbit31を見てcore_exc_entryへ分岐する
 *  処理），本ポートで初めてこのマクロを利用する（clic_kernel_impl.hで
 *  定義）。trap_vector_tableは単なる`core_int_entryへの無条件ジャンプ`
 *  でよい（例外／割込みの弁別はcore_int_entry側が行う）。
 *
 *  ■ 割込み番号の取得
 *  CLIC搭載チップのmcauseはbit31（Interrupt）に加え，下位12bit
 *  （0xfff．hal: riscv/include/esp_private/vectors_const.hの
 *  VECTORS_MCAUSE_REASON_MASK＝CLIC専用の0x00000fff．標準PLIC/INTC系は
 *  0x1f）がexception code（＝CLIC内部番号）を保持する。CLIC内部番号は
 *  外部割込みで16〜47（RV_EXTERNAL_INT_OFFSET=16）のため，ASP3の
 *  INTNO(1〜31)へは16を引いて変換する（この変換はここ＝chip_support.S
 *  にのみ閉じ込め，他のCLIC操作＝優先度設定等はclic_kernel_impl.hの
 *  CLIC_LINEマクロで対称的に変換する）。
 *
 *  ■ 割込み優先度マスクの昇格・復元
 *  C6はPLICMX_THRESH_OFFへのメモリアクセス（lw/sw）だったが，C5は
 *  mintthresh CSR（0x347．標準RISC-V CLIC．hal: riscv/include/riscv/
 *  csr_clic.h参照）へのcsrr/csrw命令アクセスへ全面書換えした。
 *  受け付けた割込みの優先度（CLIC_INT_CTRL_REG(line)のCTLバイト＝
 *  NLBITS_TO_BYTE形式）をそのままmintthreshへ書くことで，「同一優先度
 *  以下をブロックし，より高い優先度のみプリエンプトを許す」という
 *  C6と同じ意味論を実現できる（NLBITS_TO_BYTE形式は下位ビットが1で
 *  埋まっているため，同じ優先度の値同士を比較すると「同じなので
 *  ブロック」，1段階上の優先度は数値として大きくなり「許可」となる．
 *  +1のような補正は不要．docs/c5-port-design.md §8.2参照）。
 *
 *  ■ 【実機確認待ち】docs/c5-port-design.md §8.1・§8.2参照
 *  ESP32-C5はsoc_caps.hでSOC_INT_HW_NESTED_SUPPORTED=1を定義しており，
 *  esp-hal-3rdparty自身は（!SOC_INT_HW_NESTED_SUPPORTEDガードにより）
 *  C5向けにはソフトウェアでの優先度閾値昇格処理を行っていない＝CLIC
 *  ハードウェアが割込み受付時に自動的にmintstatus.milを昇格し，
 *  mcauseのMPILフィールド（bit[23:16]）に退避した前回値をmret時に
 *  自動復元する可能性が高い。本実装はC6同様「ソフトウェアでmintthresh
 *  を昇格・復元する」設計を踏襲しており，ハードウェアの自動昇格
 *  （mintstatus.mil）と本実装のソフトウェア昇格（mintthresh）は別の
 *  レジスタであるため理論上は干渉しないはずだが，これは机上の推測で
 *  あり実機JTAGでの確認が必須である。
 */

#define TOPPERS_MACRO_ONLY
#define TOPPERS_ASM_MACRO
#define UINT_C(val)     (val) /* uint_t型の定数を作るマクロ */
#define ULONG_C(val)    (val) /* ulong_t型の定数を作るマクロ */
#define CAST(type, val) (val) /* 型キャストを行うマクロ */

#include "kernel_impl.h"
#include "offset.h"
#include "target_asm.inc"

/*
 *  トラップベクタ（non-vectored．64byteアライン）
 *
 *  CLICモード（mtvecのmode=3）では，例外・非ベクタド割込み（本ポートは
 *  全割込みが非ベクタド）とも(mtvec & ~0x3f)へ分岐する。単一エントリ
 *  からcore_int_entryへ無条件ジャンプするだけでよい（例外／割込みの
 *  弁別はcore_int_entry冒頭のUSE_RISCV_DIRECT_TRAP分岐が行う）。
 */
    AO_PUSH    /* 設定の保存 */
    AO_NORVC
    ABALIGN(64)
    AGLOBAL(trap_vector_table)
ALABEL(trap_vector_table)
    j core_int_entry
    AO_POP     /* 設定の復帰 */

/*
 *  割込み入口処理での割込みコントローラ操作
 *
 *  受け付けた割込みに応じて割込み優先度マスクをセットする
 *  以下の内容をレジスタに入れてリターンする
 *   a0:割込み番号（ASP3のINTNO＝1〜31．CLIC内部番号から-16した値）
 *   a1:割込み前の優先度＝割込み前のmintthresh CSRの生値
 *   a2:割込みハンドラアドレス
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_int)
ALABEL(irc_begin_int)
    /*
     *  割込み番号の取得（mcauseの下位12bitがCLIC内部番号．
     *  外部割込みオフセット16を引いてASP3のINTNOへ変換する）
     */
    csrr t0, mcause
    li   t3, 0xfff         /* 0xfffはandiの即値範囲(12bit符号付)を超えるためliで作る */
    and  t0, t0, t3        /* t0 = CLIC内部番号（線番号，16〜47） */
    addi a0, t0, -16       /* a0 = ASP3のINTNO（1〜31） */

    /*
     *  割込み前のmintthreshを保存（a1．irc_end_intで書き戻す）し，
     *  受け付けた割込み自身の優先度（CLIC_INT_CTRL_REG(line)のCTL
     *  バイト＝NLBITS_TO_BYTE形式）へ昇格する（同一優先度以下を
     *  ブロックし，より高い優先度のみ許可．+1のような補正は不要．
     *  chip_support.S冒頭コメント参照）
     */
    csrr a1, 0x347
    li   t2, CLIC_CTRL_BASE
    slli t1, t0, 2         /* t1 = line*4（t0はまだCLIC内部番号のまま） */
    add  t1, t2, t1        /* &CLIC_INT_CTRL_REG(line) */
    lbu  t0, 3(t1)         /* CTLバイト（優先度）．t0を再利用 */
    csrw 0x347, t0
    fence                  /* mintthresh反映をMIE再許可前に保証 */

    /*
     *  割込みハンドラアドレスの取得
     */
    la     t1, inh_table           /* 割込みハンドラテーブルの読込み */
    slli   t2, a0, INTPTR_T_SHIFT
    add    t2, t1, t2
    lx     a2, 0(t2)               /* 割込みハンドラの番地 */
    jr     ra

/*
 *  割込み出口処理での割込みコントローラ操作
 *
 *  a0:割込み番号
 *  a1:割込み前の優先度＝割込み前のmintthresh CSRの生値
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_int)
ALABEL(irc_end_int)
    /*
     *  割込み前の優先度マスク（mintthresh）を復元する
     */
    csrw 0x347, a1
    fence
    jr   ra

/*
 *  CPU例外発生前の割込み優先度の取得
 *
 *  CPU例外の発生で割込み優先度が変わることはないため，現在の割込み優先
 *  度を外部表現（負値）に変換して返す．内部表現はmintthreshのNLBITS_
 *  TO_BYTE形式から上位3bitを取り出したもの（>> 5）．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_get_intpri)
ALABEL(irc_get_intpri)
    csrr t0, 0x347
    srli t0, t0, 5                 /* CLIC_NLBITS_SHIFT=8-3=5．内部表現に変換 */
    neg  a0, t0                    /* 外部表現に変換 */
    jr   ra

/*
 *  CPU例外ハンドラ呼出し前の割込みコントローラ操作
 *
 *  irc_end_excで用いる情報（CPU例外発生前のmintthreshの生値）を返す．
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_begin_exc)
ALABEL(irc_begin_exc)
    csrr a0, 0x347
    jr   ra

/*
 *  CPU例外ハンドラ呼出し後の割込みコントローラ操作
 */
    ATEXT
    AALIGN(4)
    AGLOBAL(irc_end_exc)
ALABEL(irc_end_exc)
    csrw 0x347, a0
    fence
    jr   ra
