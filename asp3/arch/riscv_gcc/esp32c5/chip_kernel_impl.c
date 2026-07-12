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
 *    カーネルのチップ依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip_kernel_impl.c）から
 *  のコピー・C5対応。ESP32-C5は標準RISC-V CLICを採用するため，
 *  chip_initialize()のmtvec設定・割込みコントローラ初期化を全面的に
 *  書き換えた（設計判断はclic_kernel_impl.h冒頭コメント・
 *  docs/c5-port-design.md §4・§8.2参照）。ソース→CPU割込み線の
 *  ルーティング（esp32c5_intmtx_route）自体はC6・C3と同じINTMTX方式の
 *  ため変更していない。高分解能タイマはSYSTIMER（ターゲット依存部の
 *  target_timer.[ch]）を使用するため，本ファイルではタイマの初期化を
 *  行わない。
 */

#include "kernel_impl.h"
#include "interrupt.h"
#include <sil.h>

/*
 *  各CPU割込み線に割り当てたソースのビットマスクとFROM_CPUソース番号
 *  （esp32c5_intmtx_routeが更新する．C6と同じ管理方式）
 */
uint32_t intmtx_srcmask[32][3];
uint8_t intmtx_from_cpu[32];

/*
 *  ペリフェラル割込みソースのCPU割込み線への割り当て
 *
 *  【C5実機で確定・C6/C3とは異なる】INTMTXのMAPレジスタへ書込む値は
 *  «CLIC内部番号»（＝INTNO+16＝CLIC_LINE(intno)）である．C5のINTMTXは
 *  MAP値をそのままCLIC内部の割込み線番号として用いるため，INTNOを
 *  そのまま書くとCLIC内部16〜46（＝カーネルが許可・ハンドラ登録して
 *  いない線）へ配送されてしまい，ペリフェラル割込みが一切CPUへ届かない
 *  （実機JTAG検証：src54(USB_SERIAL_JTAG)をMAP=17で配線するとCLIC内部17が
 *  pending＝IE=0で未配送．MAP=33へ変更するとCLIC内部33がpending＝IE=1で
 *  配送されコンソール割込み出力が復活．docs/c5-bringup.md実施02）．
 *  なおCLIC内部番号への+16変換はclic_kernel_impl.hのCLIC_LINEマクロが
 *  許可・優先度設定側でも行っており，INTMTX側と一致させる必要がある．
 *  intmtx_srcmask/intmtx_from_cpuの添字はINTNO（1〜31）のまま．
 */
void
esp32c5_intmtx_route(uint_t intsrc, INTNO intno)
{
	sil_wrw_mem((void *)(INTMTX_BASE + intsrc * 4U),
				(uint32_t)CLIC_LINE(intno));
	intmtx_srcmask[intno][intsrc / 32U] |= 1U << (intsrc % 32U);
	if (ESP32C5_INTSRC_FROM_CPU_0 <= intsrc
			&& intsrc <= ESP32C5_INTSRC_FROM_CPU_3) {
		intmtx_from_cpu[intno] = (uint8_t)(intsrc
										- ESP32C5_INTSRC_FROM_CPU_0);
	}
}

/*
 *  割込みマトリクス・CLICの初期化
 */
void
clic_initialize(void)
{
	uint_t i;

	/*
	 *  全ペリフェラルソースの割り当てを解除する（CPU割込み線0へ
	 *  マップ＝線0は使用しないため実質無効）．ROMブートローダが
	 *  設定した割り当てをクリアする意味もある．C5はソース数が84本
	 *  （C6は77本．esp32c5.hのESP32C5_TNUM_INTSRC参照）．
	 */
	for (i = 0U; i < ESP32C5_TNUM_INTSRC; i++) {
		sil_wrw_mem((void *)(INTMTX_BASE + i * 4U), 0U);
	}

	/*
	 *  ソース割り当ての管理テーブルの初期化
	 */
	for (i = 0U; i < 32U; i++) {
		intmtx_srcmask[i][0] = 0U;
		intmtx_srcmask[i][1] = 0U;
		intmtx_srcmask[i][2] = 0U;
		intmtx_from_cpu[i] = 0xFFU;
	}

	/*
	 *  全CLIC外部割込み線（ASP3の擬似INTNO 0〜31＝CLIC内部番号16〜47）
	 *  を禁止・level型・非ベクタド(SHV=0)・machineモードに設定し，
	 *  優先度を0（決して受け付けられない）に初期化する．
	 *
	 *  CLIC_INT_CTRL_REG(i)はIP/IE/ATTR/CTLがそれぞれ独立したバイト
	 *  （clic_kernel_impl.h参照）のため，read-modify-writeせずバイト
	 *  単位で書ける．
	 */
	for (i = 0U; i <= INTMTX_TNUM_INT; i++) {
		uint_t line = CLIC_LINE(i);

		sil_wrb_mem((void *)CLIC_LINE_IE_OFF(line), 0U);
		sil_wrb_mem((void *)CLIC_LINE_ATTR_OFF(line), CLIC_ATTR_MODE_MACHINE);
		sil_wrb_mem((void *)CLIC_LINE_CTL_OFF(line),
					(uint8_t)CLIC_NLBITS_TO_BYTE(0U));
	}

	/*
	 *  割込み優先度マスクを全解除（内部表現0）に設定
	 */
	intmtx_set_thresh(0U);

	/*
	 *  【実施27の実機知見＝CLICグローバル設定（CLIC_INT_CONFIG_REG）は
	 *    書き替えない】
	 *
	 *  CLIC_INT_CONFIG_REG（ESP32C5_CLIC_INT_CONFIG）は，冷間ブート
	 *  （ROMブート経由のDirect Boot）でも実測0x00000006（mnlbits=3＝
	 *  レベル機構有効）であり，レジスタ定義ヘッダの「default: 0」とは
	 *  異なる（ROMまたはハードウェアが設定する）．実施27でここに0を
	 *  書く「正規化」を一時実装し，ハンドオフ実験で実際に0が反映される
	 *  ことまで確認したが，下記のmil固着凍結は解消しなかった（mil=0x5f
	 *  →0xffに変わるだけで同型の凍結が再現）ため，長期実証済みの
	 *  冷間ブート条件（mnlbits=3）を変えるリスクのみと判断して撤回した
	 *  （0書込み時の冷間ブート動作自体は未検証のまま）．
	 *
	 *  mnlbits=3のもとでは，割込み受付時にハードウェアがmintstatus.mil
	 *  をCTLバイト由来レベル（本ポート符号化では0x5f）へ自動昇格し，
	 *  これはmret（mcause.MPILからの復元）でしか降格しないことを実機
	 *  JTAGで確認済み（chip_support.S冒頭コメント参照）．mretを経由
	 *  しない割込み出口経路があるとmilが昇格したまま固着し，同レベル
	 *  以下＝本ポートの全割込みが永久ブロックされる（カーネル時刻凍結）．
	 *  実施27時点でこの凍結が冷間ブートでも起動後約72msで発生している
	 *  ことが実測されており（詳細と対策候補はdocs/c5-bringup.md実施27），
	 *  対策は本関数ではなく割込み出口側で行う必要がある．
	 */
}

/*
 *  チップ依存の初期化
 */
void
chip_initialize(void)
{
	extern void *trap_vector_table;

	/*
	 *  Machine Trap-Vector Baseの設定（CLICモード．MTVEC_MODE_CSR=3．
	 *  hal: riscv/include/riscv/csr_clic.hの定義通り）．
	 *
	 *  本ポートはCLICのnon-vectoredモードを第一候補として採用する
	 *  （docs/c5-port-design.md §8.2の設計判断．採用理由：ASP3の既存
	 *  ディスパッチ機構＝全割込みが単一エントリへ飛びソフトウェアで
	 *  inh_tableを引く設計＝と親和性が高く変更量が少ない）．non-vectored
	 *  モードでは例外・割込みとも(mtvec & ~0x3f)へ分岐する（64byte
	 *  アライン要求．hal: riscv/vectors_clic.Sのコメント「this entry
	 *  must be aligned on 64」参照．C6の256byteアラインとは異なる点に
	 *  注意．trap_vector_table自体のアライン指定はchip_support.S側）．
	 */
	riscv_write_mtvec((ulong_t)&trap_vector_table | MTVEC_MODE_CSR);

	/*
	 *  mtvt（CSR 0x307．ハードウェアベクタド割込みテーブルのベース）
	 *
	 *  本ポートは全割込みをnon-vectored（CLIC_INT_ATTR.shv=0．
	 *  clic_initialize()参照）で運用するため，理論上mtvtは参照されない
	 *  はずである．しかしCLICモード（mtvecのmode=3）自体がmtvtの設定を
	 *  要求しうるという設計上の指摘（docs/c5-port-design.md §4）に
	 *  従い，保険的に共通エントリ（trap_vector_table）を指すよう設定
	 *  しておく（shv=0の限り実際には使われないはずだが，未設定のまま
	 *  不定値を残すよりは安全）．CSR番号0x307はas(1)が数値直接指定を
	 *  受け付けるため，シンボル未定義のままでもアセンブル可能．
	 */
	Asm("csrw 0x307, %0" : : "r"((ulong_t)&trap_vector_table));

	/*
	 *  割込みマトリクス・CLICの初期化
	 */
	clic_initialize();

	/*
	 *  mie CSRのCPU割込み線ビットをすべて許可する
	 *
	 *  【実機確認待ち】docs/c5-port-design.md §8.1 1番。C6では実機で
	 *  「mieは実装されており，mie=0の状態が実機で外部割込みトラップが
	 *  一切成立しない真因だった」ことが判明した実績があり，C6と同じ
	 *  csrw mie, ~0を踏襲する。ただしCLIC搭載チップでのmie/mstatus.MIE
	 *  の実際の意味づけ（標準RISC-Vのグローバル割込みイネーブルとして
	 *  機能するのか，CLICモードでは別の解釈になるのか）はC5では未確認
	 *  であり，C6のコードをそのまま複製している点に注意すること
	 *  （chip_kernel_impl.h冒頭コメント・docs/c5-port-design.md §8.1
	 *  1番も参照）。
	 */
	Asm("csrw mie, %0" : : "r"(~0U));

	/*
	 *  コア依存の初期化
	 */
	core_initialize();
}

/*
 *  チップ依存の終了処理
 */
void
chip_terminate(void)
{
	/*
	 *  コア依存の終了処理
	 */
	core_terminate();
}

/*
 *  割込み要求ラインの属性の設定
 */
Inline void
intmtx_config_int(INTNO intno, ATR intatr, PRI intpri)
{
	assert(VALID_INTNO(intno));
	assert(TMIN_INTPRI <= intpri && intpri <= TMAX_INTPRI);

	/*
	 *  割込み優先度を設定
	 */
	intmtx_set_priority(intno, INT_IPM(intpri));

	/*
	 *  割込みを許可
	 */
	if ((intatr & TA_ENAINT) != 0U) {
		intmtx_enable_int(intno);
	}
}

/*
 *  割込み管理機能の初期化
 */
void
initialize_interrupt(void)
{
	uint_t			i;
	const INTINIB	*p_intinib;

	for (i = 0U; i < tnum_cfg_intno; i++) {
		p_intinib = &(intinib_table[i]);
		intmtx_config_int(p_intinib->intno, p_intinib->intatr,
						  p_intinib->intpri);
	}
}
