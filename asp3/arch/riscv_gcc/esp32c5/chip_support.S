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
 *  ■ 【実機確認済み＝docs/c5-bringup.md実施27】docs/c5-port-design.md
 *  §8.1・§8.2参照
 *  実機JTAGで確認した事実：
 *  - CLIC_INT_CONFIG.mnlbitsは冷間ブートでも3（レジスタ値0x6．ROM/HWが
 *    設定．ヘッダの「default:0」とは異なる）。0への正規化は下記凍結を
 *    解消しない（実施27ハンドオフ実験で0の反映まで確認した上で同型
 *    凍結が再現＝mil値が0x5f→0xffに変わるのみ）ため撤回した。
 *  - mnlbits=3のもとで，割込み受付時にハードウェアがmintstatus.milを
 *    CTL由来レベル（本符号化では0x5f等）へ自動昇格し，mret
 *    （mcause.MPILからの復元）でしか降格しない。本実装のソフトウェア
 *    mintthresh昇格・復元とは独立に作用する（＝冒頭の「干渉しない
 *    はず」という机上推測は誤りだった）。
 *  - mretを経由しない割込み出口経路が実在し，そこを通るとmilが昇格
 *    されたまま固着して同レベル以下＝本ポートの全割込みが永久ブロック
 *    される（全線IP=1・IE=1のまま配送されず，カーネル時刻が凍結する）。
 *    実施28の割込みリング計装で経路を実測特定した（2/2再現）：
 *    ASP3共通部core_support.Sの割込み出口のうち，(a)idle復帰の
 *    `j dispatcher_0`，(b)dispatcher経由で切替先タスクのTCB_pc
 *    （dispatch_r/start_r）へ`jr`する遅延ディスパッチ，の2経路がmret
 *    非経由。wifi_scan冷間ブートでは起動後約72ms＝最初のwake-from-idle
 *    （SYSTIMER割込みがidleに命中しタスクを起床→dispatch_r復帰）で
 *    凍結していた。25ラウンド追跡した「PHY較正ハング（トーンADCゼロ）」
 *    の真因。なおtest_porting(6/6 PASS)はビジーポーリング構造のため
 *    この経路（割込みからの遅延ディスパッチ／wake-from-idle）を一度も
 *    通らず，凍結を検出できていなかった。
 *  - 対策（実施28〜．暫定）＝irc_begin_int内のsynthetic mretによる
 *    milの即時降格（chip層に閉じた入口解除型．詳細はdocs/
 *    c5-clic-exit-fix-review.md §4参照）。
 *
 *  ■ ★2026-07-15：恒久対策へ移行＝FMP3と同じ「出口正規化型」
 *  上記synthetic mretはchip層に閉じた「入口解除」型の対処だったが，
 *  ASP3/FMP3間のメンテコスト低減のため，asp3_core共通部
 *  （arch/riscv_gcc/common/core_support.S）側の(a)(b)2経路を
 *  FMP3同様「mepc設定＋MPP=M＋MPIEクリア＋mret」の素のmretへ変換
 *  （asp3_core commit参照．FMP3 esp32p4/core_support.S:603-616／
 *  677-689が参照実装）。これに伴い，本ポートのsynthetic mretは不要に
 *  なったため撤去し，代わりにirc_end_int側でmcause.mpil(bits23:16)
 *  =0を強制する（FMP3 esp32p4/chip_support.S:166-180と同型）。
 *  irc_end_intは全出口分岐より前（core_int_entry_2）で必ず1回呼ばれる
 *  ため，以後どの出口のmretが読むmcause.MPILも0＝milは常に0へ復帰する。
 *  「(a)(b)以外の出口（dispatch_r／start_r．タスクコンテキストからの
 *  自発的dispatch()で到達）はmretを一度も経由しない」という実施28の
 *  分析自体は変わらないが，(a)(b)自身がmret化されたことで，そこに
 *  到達する前に当該トラップのmilは既に片付いている＝どの経路へ続いても
 *  安全（docs/c5-clic-exit-fix-review.md §6「B案」・asp3_esp_idf/
 *  docs/c5-bringup.md該当実施を参照）。
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

#ifdef ESP32C5_CLIC_DEBUG_RING
    /*
     *  【診断用（既定無効）．実施28で追加，実施33でsynthetic mretの
     *  前へ移動．2026-07-15：synthetic mret撤去に伴い当該コメントの
     *  「demote前」の前提は消滅したが，記録内容・位置とも従来のまま
     *  有効（本関数の入口＝割込み受付直後の生の状態を記録する診断で
     *  あることに変わりはない）】
     *  割込み受付リングバッファ記録（begin側）。ASP3_EXTRA_COMPILE_DEFS=
     *  ESP32C5_CLIC_DEBUG_RINGで有効化し，JTAGでc5dbg_ring/c5dbg_idxを
     *  回収して解析する（回収スクリプトの例はdocs/c5-bringup.md実施28
     *  参照）。mcause（exccode=CLIC線番号，MPIL[23:16]=受付直前のmil＝
     *  直前の割込み出口後のmil値），mepc（割込まれた地点），mintstatus
     *  （受付直後の昇格後の値），hrtcntを記録する。t4〜t6はcore_int_entry
     *  が保存済みのため自由に使える（a0/a1/t0〜t3は後続のハンドラアドレス
     *  取得で引き続き使うため触らない）。
     */
    la   t4, c5dbg_idx
    lw   t5, 0(t4)
    addi t6, t5, 1
    sw   t6, 0(t4)
    andi t5, t5, 31        /* 32エントリのリング */
    slli t5, t5, 5         /* 1エントリ32バイト（8ワード） */
    la   t6, c5dbg_ring
    add  t5, t6, t5
    csrr t6, mcause
    sw   t6, 0(t5)
    csrr t6, mepc
    sw   t6, 4(t5)
    csrr t6, 0xFB1         /* mintstatus（受付直後） */
    sw   t6, 8(t5)
    la   t6, _kernel_current_hrtcnt
    lw   t6, 0(t6)
    sw   t6, 12(t5)
#endif /* ESP32C5_CLIC_DEBUG_RING */

    /*
     *  【2026-07-15撤去】mintstatus.milの即時降格（synthetic mret，
     *  実施28で追加）はここに存在したが，asp3_core共通部
     *  （core_support.S）側の出口正規化（idle復帰／遅延ディスパッチの
     *  2経路を素のmret経由へ変換）により不要になったため撤去した。
     *  milの降格は，以後どの出口が実行するmretも共通してirc_end_int
     *  （本ファイル下方）が強制するmcause.mpil=0を読むことで一律に
     *  達成される（FMP3 esp32p4方式．詳細はirc_end_intのコメント，
     *  ファイル冒頭コメント「2026-07-15：恒久対策へ移行」，
     *  docs/c5-clic-exit-fix-review.md §6を参照）。撤去前の設計・
     *  弱点検証（mepc/MPIE/MPP整合性・フォールト非再発窓・mnxti
     *  非互換等）はdocs/c5-clic-exit-fix-review.md §5に記録済み
     *  （経緯保存のためレビュー文書側は書き換えない）。
     */

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
#ifdef ESP32C5_CLIC_DEBUG_RING
    /*
     *  【診断用（既定無効）．実施28で追加】割込み出口直前のリング
     *  バッファ記録（end側）．
     *  core_support.Sのirc_end_int後の出口分岐（mret／j dispatcher_0／
     *  dispatcher経由jr TCB_pc）を決定づける入力（excpt_nest_count・
     *  p_runtsk・p_schedtsk・p_schedtsk->TCB_pc）をbegin側と同じスロット
     *  （idx-1）へ記録する。注意：多重割込み時は外側のend記録が内側の
     *  スロットを上書きする（nestフィールドで検出可能）。
     */
    la   t4, c5dbg_idx
    lw   t5, 0(t4)
    addi t5, t5, -1
    andi t5, t5, 31
    slli t5, t5, 5
    la   t6, c5dbg_ring
    add  t5, t6, t5
    la   t6, excpt_nest_count
    lw   t6, 0(t6)
    sw   t6, 16(t5)        /* デクリメント前のネストカウント */
    la   t6, p_runtsk
    lx   t6, 0(t6)
    sw   t6, 20(t5)
    la   t6, p_schedtsk
    lx   t6, 0(t6)
    sw   t6, 24(t5)
    beqz t6, 1f
    lx   t6, TCB_pc(t6)    /* 切替先タスクの実行再開番地（凍結経路の判別用） */
1:
    sw   t6, 28(t5)
#endif /* ESP32C5_CLIC_DEBUG_RING */
    /*
     *  【2026-07-15追加】mcause.mpil(bits23:16)=0を強制する（FMP3
     *  esp32p4/chip_support.S:166-180と同型）。
     *
     *  本関数（irc_end_int）は，共通部core_int_entry_2（core_support.S）
     *  でどの出口分岐（通常mret復帰／idle復帰／遅延ディスパッチ）よりも
     *  必ず先に1回呼ばれる。asp3_core共通部側の出口正規化（2026-07-15）
     *  で，idle復帰・遅延ディスパッチの2経路も素のmret経由になったため，
     *  ここでmcause.mpilを0にしておけば，以後どの経路が実行するmretも
     *  「mcause.MPILから復元」で必ずmil=0へ降格する。同一優先度以下の
     *  ブロックは直前に昇格したmintthreshが担う（本ファイル冒頭コメント
     *  参照）ため，mil=0にしても保護意味論は不変。
     *
     *  0xFF00FFFF はRV32のmcauseレイアウト共通のマスク（bit31=Interrupt，
     *  bits23:16=mpil，bits11:0=exccode。C5固有ではない標準CLICレイアウト
     *  のためFMP3 esp32p4と同一マスクを使う）。t0/t1はcaller-savedのため
     *  破壊してよい。
     */
    csrr t0, mcause
    li   t1, 0xFF00FFFF
    and  t0, t0, t1
    csrw mcause, t0

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
#ifdef ESP32C5_CLIC_DEBUG_RING
    /*
     *  【診断用（既定無効）．実施28で追加】例外経路の通過回数（例外は
     *  irc_begin_intを経由せずリングに残らないため，見えない経路が
     *  無いかの検出用）
     */
    la   t4, c5dbg_exc_count
    lw   t5, 0(t4)
    addi t5, t5, 1
    sw   t5, 0(t4)
#endif /* ESP32C5_CLIC_DEBUG_RING */
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
