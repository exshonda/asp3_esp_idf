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
 *     カーネルの割込みコントローラ依存部（ESP32-C5用．標準RISC-V CLIC）
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/intmtx_kernel_impl.h）の
 *  CPU側制御部分を，標準RISC-V CLIC（Core Local Interrupt Controller）
 *  向けに全面書き換えしたもの。ファイル名もintmtx_kernel_impl.hから
 *  clic_kernel_impl.hへ変更した（設計判断はdocs/c5-port-design.md §4・
 *  §8.2参照）。
 *
 *  このヘッダファイルは，target_kernel_impl.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない。
 *
 *  ■ C6（INTMTX/PLIC_MX方式）との違い
 *   - ソースルーティング（INTMTX，ペリフェラル割込みソース→CPU割込み
 *     線）自体は**アドレス・ロジックともC6・C3と同一のまま存続**
 *     （hal: soc/esp32c5/register/soc/reg_base.hでDR_REG_INTMTX_BASE=
 *     0x60010000がC6と同一値であることを確認済み）。
 *   - CPU割込み線側の制御（ENABLE／PRI／THRESHのメモリマップトレジスタ
 *     配列）は，標準RISC-V CLICのCLIC_INT_CTRL_REG(i)（1ワードに
 *     IP/IE/ATTR(TRIG/SHV/MODE)/CTL(優先度)を格納）＋CSR化された
 *     mintthresh（0x347）へ全面置換した。
 *   - 割込み優先度マスクは**CSRアクセス（csrr/csrw）**で行う
 *     （C6はメモリマップトレジスタPLICMX_THRESH_OFFへのsil_wrw_mem）。
 *
 *  ■ ASP3論理INTNOとCLIC内部番号の対応（本ファイルに変換を閉じ込める）
 *  標準RISC-V CLICは内部番号0〜15を例外／内部割込み用に予約し，
 *  外部（ペリフェラル）割込みは16〜47（RV_EXTERNAL_INT_OFFSET=16・
 *  RV_EXTERNAL_INT_COUNT=32．hal: riscv/include/riscv/csr_clic.h）に
 *  配置する。ASP3の論理INTNO（1〜31．C6と同じ番号体系を維持し，
 *  target層・Wi-Fi shim層からは今まで通りINTNO=1〜31のみを見せる設計
 *  ＝docs/c5-port-design.md §8.2の設計判断）とCLIC内部番号の変換は
 *  CLIC_LINE(intno)マクロに閉じ込める：
 *      CLIC内部番号 = ASP3 INTNO + 16    （INTNO=1〜31 → CLIC 17〜47）
 *  CLIC内部番号16（INTNO=0相当）は使用しない（C6同様，割込み入口処理の
 *  Spurious番兵として空けておく）。
 *
 *  ■ 割込み優先度とNLBITS
 *  hal: soc/esp32c5/include/soc/clic_reg.hはNLBITS=3を定義している
 *  （CLIC_INT_INFO_REG既定値もCTLBITS=3と一致）。CLIC_INT_CTRL_REGの
 *  CTLフィールド（8bit）は上位NLBITS=3bit（8段階=0〜7）が実効優先度，
 *  下位(8-NLBITS)=5bitは1で埋める規約（hal: riscv/include/riscv/
 *  csr_clic.hのNLBITS_TO_BYTEマクロと同じ規約をここでも踏襲する）。
 *  ASP3の優先度内部表現は0〜7の8段階（0=常に受け付けない番兵・1〜7が
 *  有効範囲．chip_kernel.h参照）であり，ちょうどCLICのNLBITS=3の8段階
 *  にそのまま一致する（設計上の要検討事項は解消．docs/c5-port-design.md
 *  §8.2の3番）。
 *
 *  ■ HW_NESTED_SUPPORTED（実機確認待ち・設計メモ）
 *  hal: soc/esp32c5/include/soc/soc_caps.hはSOC_INT_HW_NESTED_SUPPORTED=1
 *  を定義しており，esp-hal-3rdparty自身のvectors.S（!SOC_INT_HW_NESTED_
 *  SUPPORTEDガード）はC5では優先度閾値のソフトウェア昇格処理そのものを
 *  スキップしている＝CLICハードウェアが割込み受付時に自動的に優先度
 *  閾値を昇格し，mret時にmcauseから復元する可能性が高い。本実装は
 *  C6同様の「ソフトウェアでmintthreshを昇格／復元する」設計を踏襲する
 *  （安全側＝ハードウェアが自動的にやっていたとしても二重の昇格は
 *  無害なはずだが，これは机上の推測であり，本物のハードウェア自動
 *  昇格とソフトウェア昇格が干渉しないかは実機でのみ確認できる）。
 *  【実機確認待ち】docs/c5-port-design.md §8.1・§8.2参照。
 */

#ifndef TOPPERS_CLIC_KERNEL_IMPL_H
#define TOPPERS_CLIC_KERNEL_IMPL_H

/*
 *  非ベクタド（direct）トラップ方式の選択
 *
 *  ASP3共通部（arch/riscv_gcc/common/core_support.S）のcore_int_entryは
 *  USE_RISCV_DIRECT_TRAP定義時，冒頭でmcauseのbit31（Interrupt）を見て
 *  例外ならcore_exc_entryへ分岐する処理をあらかじめ備えている（従来は
 *  どのチップからも使われていなかった＝本ポートが初の利用）。CLICの
 *  non-vectoredモード（全割込みが単一エントリへ分岐．chip_support.S
 *  冒頭コメント参照）はまさにこの分岐が必要な構成のため定義する。
 *  マクロオンリー（アセンブリ）コンテキストでも見える必要があるため，
 *  TOPPERS_MACRO_ONLYガードの外（ファイル冒頭）で定義する。
 */
#define USE_RISCV_DIRECT_TRAP

#include <sil.h>
#include "esp32c5.h"

/*
 *  割込みマトリクス（ソースルーティング．アドレス方式はC6・C3と同一だが
 *  MAP値の意味がC5では異なる＝下記）
 *
 *  ソースnのMAPレジスタ：INTMTX_BASE + 4n．【C5実機で確定】このMAP値は
 *  «CLIC内部番号»として直接使われる（C6/C3のようにCLIC側で+16変換は
 *  されない）ため，MAPにはCLIC_LINE(intno)＝INTNO+16（外部割込みは17〜47）
 *  を書く必要がある．INTNOそのままを書くとCLIC内部の未許可線へ配送され
 *  割込みがCPUへ届かない（esp32c5_intmtx_route（chip_kernel_impl.c）と
 *  docs/c5-bringup.md実施02参照）．
 *
 *  生ステータスレジスタのオフセットはC6（0x134/0x138/0x13c）と異なる
 *  （hal: soc/esp32c5/register/soc/interrupt_matrix_reg.hで
 *  INTERRUPT_CORE0_INT_STATUS_REG_{0,1,2}_REG=BASE+{0x150,0x154,0x158}
 *  を確認済み．C5はソース数が84本（C6は77本）のためSTATUS_REG_2は
 *  有効ビットが20bit=bit[19:0]のみ）。
 *
 *  【実機確認待ち・要注意】hal のレジスタ説明コメントには「0:
 *  triggered/1: no interrupt triggered」という，C6ヘッダ（未記載＝
 *  プレースホルダ）とは異なる記述がある（interrupt_matrix_reg.h内
 *  INTERRUPT_CORE0_INT_STATUS_0のコメント）。C3/C6の実装はビット=1を
 *  「ソースがアクティブ」として扱っており，本ファイルもその慣例
 *  （SoCファミリ内の他の同種ステータスレジスタと同じ極性）を踏襲する
 *  が，このコメントが自動生成ドキュメントの誤りではなく実際の仕様なら
 *  probe_int()の判定が反転する（レジスタドキュメントの誤読はesp32c6.h
 *  で実際に起きた事故＝docs/c5-port-design.md冒頭の教訓）。実機JTAGで
 *  必ず検証すること。
 */
#define INTMTX_BASE           ESP32C5_INTMTX_BASE
#define INTMTX_STATUS0_OFF    0x150   /* ソース0〜31の生ステータス（RO） */
#define INTMTX_STATUS1_OFF    0x154   /* ソース32〜63の生ステータス（RO） */
#define INTMTX_STATUS2_OFF    0x158   /* ソース64〜83の生ステータス（RO．有効20bit） */

/*
 *  CPU割込み線の本数（ASP3のINTNO＝1〜31．C6・C3と同じ番号体系）
 */
#define INTMTX_TNUM_INT    UINT_C(31)

/*
 *  RISC-Vコアで共通な定義
 */
#include "core_kernel_impl.h"

/*
 *  CLIC外部割込みのオフセット（hal: riscv/include/riscv/csr_clic.hの
 *  RV_EXTERNAL_INT_OFFSET=16と同じ値．ASP3 INTNO(1〜31)をCLIC内部番号
 *  (17〜47)へ変換する）
 */
#define CLIC_EXT_INT_OFFSET   UINT_C(16)
#define CLIC_LINE(intno)      ((intno) + CLIC_EXT_INT_OFFSET)

/*
 *  CLIC per-line制御レジスタ（1ワードにIP/IE/ATTR/CTLを格納．
 *  hal: soc/esp32c5/include/soc/clic_reg.hのCLIC_INT_CTRL_REG(i)と同じ）
 *  バイト単位アクセス用オフセット（同ヘッダのBYTE_CLIC_INT_*_REGマクロ
 *  と同じレイアウト．IP/IE/ATTR/CTLがそれぞれ独立したバイトのため，
 *  read-modify-writeを行わずバイト単位で安全に操作できる）
 */
#define CLIC_CTRL_BASE        ESP32C5_CLIC_CTRL_BASE
#define CLIC_LINE_IP_OFF(i)    (CLIC_CTRL_BASE + (i) * 4U + 0U)  /* pending（RO/特殊W） */
#define CLIC_LINE_IE_OFF(i)    (CLIC_CTRL_BASE + (i) * 4U + 1U)  /* 許可（1byte） */
#define CLIC_LINE_ATTR_OFF(i)  (CLIC_CTRL_BASE + (i) * 4U + 2U)  /* SHV/TRIG/MODE（1byte） */
#define CLIC_LINE_CTL_OFF(i)   (CLIC_CTRL_BASE + (i) * 4U + 3U)  /* 優先度（1byte，上位3bitが実効） */

#define CLIC_ATTR_MODE_MACHINE   UINT_C(0xC0)  /* MODE=3(machine)<<6 | TRIG=level(00)<<1 | SHV=0 */

/*
 *  優先度⇔CLIC CTL/THRESHバイト値の変換（NLBITS=3．hal: riscv/include/
 *  riscv/csr_clic.hのNLBITS_TO_BYTE/BYTE_TO_NLBITSと同じ規約）
 *
 *  ASP3の内部表現（0〜7．0=常に受け付けない番兵，1〜7が有効優先度）を
 *  そのままCLICの8段階（NLBITS=3）へ1:1で写像する。
 */
#define CLIC_NLBITS         3U
#define CLIC_NLBITS_SHIFT   (8U - CLIC_NLBITS)                     /* =5 */
#define CLIC_NLBITS_TO_BYTE(level) \
	(((uint32_t)(level) << CLIC_NLBITS_SHIFT) | ((1U << CLIC_NLBITS_SHIFT) - 1U))
#define CLIC_BYTE_TO_NLBITS(byte)  ((uint32_t)(byte) >> CLIC_NLBITS_SHIFT)

/*
 *  mintthresh CSR（標準RISC-V CLIC．hal: riscv/include/riscv/csr_clic.h
 *  の"The ESP32-C5 (MP), C61, H4 and P4 (since REV2) use the standard
 *  CLIC specification...defines the mintthresh CSR"の通り，C5は
 *  INTTHRESH_STANDARD=1（hal: soc/esp32c5/include/soc/interrupt_reg.h）
 *  でCSR番号0x347が使われる。メモリマップトレジスタではないため
 *  csrr/csrw命令でアクセスする（C6のPLICMX_THRESH_OFFのsil_wrw_mem等
 *  メモリアクセスから全面書換え．docs/c5-port-design.md §4参照）。
 */
#define MINTTHRESH_CSR   0x347

/*
 *  mtvecのCLICモード値（標準RISC-V．hal: riscv/include/riscv/csr_clic.h
 *  の"Setting mode field to 3 treats MTVT + 4 * interrupt_id as the
 *  service entry address for HW vectored interrupts"の通り．C6の
 *  vectoredモード(MTVEC_MODE_VECTORD=1．asp3_core/arch/riscv_gcc/
 *  common/riscv.h)とは異なるCLIC専用モード）
 */
#define MTVEC_MODE_CSR   0x3U

/*
 *  mtvt CSR番号（ハードウェアベクタド割込みテーブルのベースアドレス．
 *  本ポートはnon-vectoredで運用するため理論上未使用．
 *  chip_kernel_impl.cのchip_initialize()参照）
 */
#define MTVT_CSR         0x307U

#ifndef TOPPERS_MACRO_ONLY

/*
 *  mintthreshレジスタの読み書き（CSRアクセス．numericのCSR番号を直接
 *  指定．binutilsはシンボル未定義のCSRでも数値直接指定に対応している）
 */
Inline uint32_t
clic_read_mintthresh(void)
{
	uint32_t val;
	Asm("csrr %0, 0x347" : "=r"(val));
	return(val);
}

Inline void
clic_write_mintthresh(uint32_t val)
{
	Asm("csrw 0x347, %0" : : "r"(val));
}

/*
 *  割込み禁止（CLIC_INT_CTRL_REG(i)のIEバイトへ0を書く．IP/ATTR/CTLとは
 *  独立したバイトのためread-modify-write不要）
 */
Inline void
intmtx_disable_int(INTNO intno)
{
	sil_wrb_mem((void *)CLIC_LINE_IE_OFF(CLIC_LINE(intno)), 0U);
}

/*
 *  割込み許可
 */
Inline void
intmtx_enable_int(INTNO intno)
{
	sil_wrb_mem((void *)CLIC_LINE_IE_OFF(CLIC_LINE(intno)), 1U);
}

/*
 *  各CPU割込み線に割り当てたペリフェラルソースのビットマスク
 *  （INTR_STATUS_0／1／2に対応する3ワード．esp32c5_intmtx_routeが
 *  更新する．prb_intの実現に用いる．C6と同じ管理方式）
 */
extern uint32_t intmtx_srcmask[32][3];

/*
 *  各CPU割込み線に割り当てたFROM_CPUソースの番号（0〜3．割り当てが
 *  ないときは0xFF．ras_int／clr_intの実現に用いる．C6と同じ）
 */
extern uint8_t intmtx_from_cpu[32];

/*
 *  割込みペンディングのチェック（INTMTX側の生ステータスを読む．CLIC
 *  移行の影響を受けない．C6と同じロジック．レジスタオフセットのみC5用
 *  に変更．極性は上記ファイル冒頭コメントの【実機確認待ち・要注意】参照）
 */
Inline bool_t
intmtx_probe_int(INTNO intno)
{
	if ((intmtx_from_cpu[intno] != 0xFFU)
		&& (sil_rew_mem((void *)(ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0
				+ (uint32_t)intmtx_from_cpu[intno] * 4U)) != 0U)) {
		return(true);
	}
	return(((sil_rew_mem((void *)(INTMTX_BASE + INTMTX_STATUS0_OFF))
				& intmtx_srcmask[intno][0])
			| (sil_rew_mem((void *)(INTMTX_BASE + INTMTX_STATUS1_OFF))
				& intmtx_srcmask[intno][1])
			| (sil_rew_mem((void *)(INTMTX_BASE + INTMTX_STATUS2_OFF))
				& intmtx_srcmask[intno][2])) != 0U);
}

/*
 *  割込みの要求（ras_int用．FROM_CPUソースが割り当てられた割込み線
 *  のみ．INTPRIペリフェラルはCLIC移行の影響を受けない．C6と同じ）
 */
Inline void
intmtx_raise_int(INTNO intno)
{
	sil_wrw_mem((void *)(ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0
							+ (uint32_t)intmtx_from_cpu[intno] * 4U), 1U);
}

/*
 *  割込み要求のクリア（clr_int用）
 */
Inline void
intmtx_clear_int(INTNO intno)
{
	sil_wrw_mem((void *)(ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0
							+ (uint32_t)intmtx_from_cpu[intno] * 4U), 0U);
}

/*
 *  ras_int／clr_intが使用できる割込み番号か（FROM_CPUソースが割り
 *  当てられているか）のチェック
 */
Inline bool_t
intmtx_valid_raise(INTNO intno)
{
	return(intmtx_from_cpu[intno] != 0xFFU);
}

/*
 *  割込み要求ラインに対する割込み優先度の設定（priは内部表現1〜7．
 *  CLIC_INT_CTRL_REG(i)のCTLバイトへ書く．IP/IE/ATTRとは独立した
 *  バイトのためread-modify-write不要）
 */
Inline void
intmtx_set_priority(INTNO intno, uint_t pri)
{
	sil_wrb_mem((void *)CLIC_LINE_CTL_OFF(CLIC_LINE(intno)),
				(uint8_t)CLIC_NLBITS_TO_BYTE(pri));
}

/*
 *  割込み優先度マスクの設定（mは内部表現0〜7．mintthresh CSRへCSR
 *  アクセスで書く．C6のメモリマップトレジスタ(PLICMX_THRESH_OFF)への
 *  sil_wrw_memから全面書換え）
 */
Inline void
intmtx_set_thresh(uint_t m)
{
	clic_write_mintthresh(CLIC_NLBITS_TO_BYTE(m));
}

/*
 *  割込み優先度マスクの参照（内部表現で返す）
 */
Inline uint_t
intmtx_get_thresh(void)
{
	return((uint_t)CLIC_BYTE_TO_NLBITS(clic_read_mintthresh()));
}

/*
 *  ペリフェラル割込みソースのCPU割込み線への割り当て
 *  （ターゲット依存部の初期化で使用する．intmtx_srcmask／
 *  intmtx_from_cpuの更新を伴うためchip_kernel_impl.cに実体を置く）
 */
extern void esp32c5_intmtx_route(uint_t intsrc, INTNO intno);

/*
 *  割込みマトリクス・CLICの初期化
 */
extern void clic_initialize(void);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CLIC_KERNEL_IMPL_H */
