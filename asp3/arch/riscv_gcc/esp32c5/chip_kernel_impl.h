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
 *    kernel_impl.hのチップ依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip_kernel_impl.h）から
 *  のコピー・C5対応。このヘッダファイルは，target_kernel_impl.h
 *  （または，そこからインクルードされるファイル）のみからインクルード
 *  される．他のファイルから直接インクルードしてはならない．
 *
 *  C6のCPU側割込み制御（Espressif呼称"PLIC"．実体はC3のINTMTXと同型）
 *  を，ESP32-C5が採用する標準RISC-V CLICへ置き換えた（低レベル操作は
 *  intmtx_kernel_impl.hから改名したclic_kernel_impl.hに実装．設計判断
 *  はdocs/c5-port-design.md §4・§8.2参照）。ASP3論理INTNO(1〜31)は
 *  C6と同じ番号体系のまま維持し，CLIC内部番号(17〜47)への変換は
 *  clic_kernel_impl.hに閉じ込める。
 */

#ifndef TOPPERS_CHIP_KERNEL_IMPL_H
#define TOPPERS_CHIP_KERNEL_IMPL_H

/*
 *  ESP32-C5のハードウェア資源の定義
 */
#include "esp32c5.h"

/*
 *  RISC-Vの定義
 */
#include "riscv.h"

/*
 *  ブートハート（シングルコア．LPコアは対象外）
 */
#define TOPPERS_BOOT_HARTID    0

/*
 *  mie/mip CSRの扱い（C6と同じくTOPPERS_OMIT_MIE_INITは定義しない）
 *
 *  C3はmie/mip CSRを実装せずアクセスすると不正命令例外になるため，
 *  当初はTOPPERS_OMIT_MIE_INITを定義してC3系列に類推していたが，C6の
 *  実機検証で「mieは実装されており正常に読み書きできる」ことが判明
 *  している（asp3_core/arch/riscv_gcc/esp32c6/chip_kernel_impl.h参照）。
 *  ESP32-C5はCLIC搭載チップであり，標準RISC-Vのmie/mstatus.MIEが
 *  CLICモードでどう振る舞うか（グローバル外部割込み許可として機能する
 *  か，CLIC下では別の意味を持つか）は【実機確認待ち】
 *  （docs/c5-port-design.md §8.1 1番）。C6の教訓を踏まえ，本ポートでも
 *  安易にC3系列の仮定（不正命令になる）を置かず，C6と同様に
 *  TOPPERS_OMIT_MIE_INITを定義しない（start.Sのmie/mipクリアを有効化
 *  し，chip_initialize()内でmie相当の許可設定を行う。詳細は
 *  chip_kernel_impl.cのchip_initialize()コメント参照）。
 */

/*
 *  デフォルトの非タスクコンテキスト用のスタック領域の定義
 */
#ifndef DEFAULT_ISTKSZ
#define DEFAULT_ISTKSZ  0x1000U    /* 4KB */
#endif /* DEFAULT_ISTKSZ */

/*
 *  割込み番号の最小値と最大値
 *
 *  ASP3の割込み番号INTNO = CPU割込み線番号（1〜31．C6と同じ番号体系．
 *  CLIC内部番号への変換はclic_kernel_impl.h参照）．
 */
#define TMIN_INTNO  UINT_C(1)
#define TMAX_INTNO  UINT_C(31)

/*
 *  割込みハンドラ番号の最大値
 */
#define TMAX_INHNO  UINT_C(31)

/* 外部表現への変換 */
#define EXT_IPM(pri)  (-(PRI)(pri))

/* 内部表現への変換 */
#define INT_IPM(ipm)  ((uint_t)(-(ipm)))

/*
 *  割込み番号の範囲の判定
 */
#define VALID_INTNO(intno) \
  ((TMIN_INTNO <= (intno)) && ((intno) <= TMAX_INTNO))

/*
 *  割込み要求ラインのための標準的な初期化情報を生成する
 */
#define USE_INTINIB_TABLE

/*
 *  割込み要求ライン設定テーブルを生成する
 */
#define USE_INTCFG_TABLE

/*
 *  CLIC依存部（旧intmtx_kernel_impl.h．C5は標準RISC-V CLIC採用のため
 *  改名．docs/c5-port-design.md §4参照）
 */
#include "clic_kernel_impl.h"

#ifndef TOPPERS_MACRO_ONLY

/*
 *  割込み属性の設定のチェック
 */
Inline bool_t
check_intno_cfg(INTNO intno)
{
	return(intcfg_table[intno] != 0U);
}

/*
 *  割込み優先度マスクの設定
 */
Inline void
t_set_ipm(PRI intpri)
{
	intmtx_set_thresh(INT_IPM(intpri));
}

/*
 *  割込み優先度マスクの参照
 */
Inline PRI
t_get_ipm(void)
{
	return(EXT_IPM(intmtx_get_thresh()));
}

/*
 *  割込み要求禁止フラグが操作できる割込み番号の範囲の判定
 */
#define VALID_INTNO_DISINT(intno)  VALID_INTNO(intno)

/*
 *  割込み要求禁止フラグのセット
 */
Inline void
disable_int(INTNO intno)
{
	intmtx_disable_int(intno);
}

/*
 *  割込み要求禁止フラグのクリア
 */
Inline void
enable_int(INTNO intno)
{
	intmtx_enable_int(intno);
}

/*
 *  割込み要求のチェック
 */
Inline bool_t
probe_int(INTNO intno)
{
	return(intmtx_probe_int(intno));
}

/*
 *  割込み要求のクリア（clr_int用．FROM_CPUソースを割り当てた割込み
 *  線のみ）
 */
#define VALID_INTNO_CLRINT(intno)  VALID_INTNO(intno)

Inline bool_t
check_intno_clear(INTNO intno)
{
	return(intmtx_valid_raise(intno));
}

Inline void
clear_int(INTNO intno)
{
	intmtx_clear_int(intno);
}

/*
 *  割込みの要求（ras_int用．FROM_CPUソースを割り当てた割込み線のみ．
 *  levelソースのため，クリアされるまで要求が保持される）
 */
#define VALID_INTNO_RASINT(intno)  VALID_INTNO(intno)

Inline bool_t
check_intno_raise(INTNO intno)
{
	return(intmtx_valid_raise(intno));
}

Inline void
raise_int(INTNO intno)
{
	intmtx_raise_int(intno);
}

/*
 *  チップ依存の初期化
 */
extern void chip_initialize(void);

/*
 *  チップ依存の終了処理
 */
extern void chip_terminate(void);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CHIP_KERNEL_IMPL_H */
