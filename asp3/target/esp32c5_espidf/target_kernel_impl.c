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
 *  ターゲット依存モジュール（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/target/esp32c6_espidf/target_kernel_impl.c
 *  相当）からのコピー・C5対応。C6ブリングアップ専用のGPIO/UART/
 *  RTC-RAM診断計装（docs/c5-port-design.md §8.3「C6資産の非移植方針」）
 *  は持ち込まず，WDT無効化・基本初期化のみのクリーンな実装とした。
 *  ROMブートローダ通過後の状態を継承し（UART0は115200bpsで設定済み，
 *  SYSTIMERはリセット後デフォルトでクロック供給済み），追加で必要な
 *  処理はリセット後デフォルトで有効なウォッチドッグタイマの無効化の
 *  みである（無効化しないと数秒でリブートする．C3/C6と同様）。
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include <sil.h>
#ifdef TOPPERS_OMIT_TECS
#include "chip_serial.h"
#endif
#ifdef TOPPERS_ESP32C5_WIFI
#include "esp_rom_sys.h"	/* esp_rom_set_cpu_ticks_per_us宣言．
							   Wi-Fi有効時のみROM ld（esp32c5.rom.api.ld）
							   がシンボル実体を提供するため呼出しもガードする
							   （下記hardware_init_hook参照） */
#endif

/*
 *  エラー時の処理
 */
extern void Error_Handler(void);

/*
 *  終了時のフック（weak定義を本ファイル末尾に置く）
 */
extern void software_term_hook(void);

/*
 *  MWDT（タイマグループのウォッチドッグ）の無効化
 */
static void
esp32c5_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(timg_base),
				ESP32C5_TIMG_WDT_WKEY);         /* 書込み保護の解除 */
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTCONFIG0(timg_base), 0U);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(timg_base), 0U);
}

/*
 *  起動時のハードウェア初期化処理
 */
void
hardware_init_hook(void)
{
	/*
	 *  ウォッチドッグタイマの無効化
	 *  （MWDT0/1・RTC WDT・スーパーWDT．リセット後デフォルトで有効）
	 *
	 *  【実機確認待ち】docs/c5-port-design.md §8.1 6番。TIMG_WDT_WKEY
	 *  はhalヘッダのdefault値0x50D83AA1を実引用しているが（esp32c5.h
	 *  参照），LP_WDT系の解錠キーはC3/C6実績の転用（暫定値）であり，
	 *  ヘッダに正しく記載されていない可能性がある（C6のesp32c6.hに
	 *  実際に誤記があった前例あり）。実機で解錠成功（WDTリセットが
	 *  発生しないこと）を必ず確認すること。
	 */
	esp32c5_disable_mwdt(ESP32C5_TIMG0_BASE);
	esp32c5_disable_mwdt(ESP32C5_TIMG1_BASE);

	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT,
				ESP32C5_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT, 0U);

	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT,
				ESP32C5_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C5_RTC_CNTL_SWD_CONF,
			ESP32C5_RTC_CNTL_SWD_AUTO_FEED_EN | (1U << 30));
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT, 0U);

	/*
	 *  CPUクロックの切替え
	 *
	 *  【実機確認待ち】docs/c5-port-design.md §8.1 3番。C6はROMブート
	 *  ローダが起動時点で既にPCRをSPLL÷3÷1＝160MHzへ設定済みと判明し
	 *  ソフトウェアでの追加操作は不要だったが，C5のPCR相当レジスタの
	 *  実際の初期値は未確認である（esp32c5.hのCORE_CLK_MHZ参照）。
	 *  C6と同様「ROMが既に適切に設定している」ことを仮定し，本関数では
	 *  クロック切替えレジスタを一切書き換えない（誤った書換えによる
	 *  ハングを避けるため）。
	 *
	 *  esp_rom_set_cpu_ticks_per_us()（ROMの較正用大域変数
	 *  s_ticks_per_usへCORE_CLK_MHZを明示的に通知．C6はphy_init較正で
	 *  これが必要だった＝実機検証済みの根本原因修正．
	 *  docs/wifi-shim-c6.md実施48参照）は，B-0/B-1スコープ（Wi-Fi無し）
	 *  では呼び出さない：esp_rom_set_cpu_ticks_per_us自体はROM
	 *  リンカスクリプト（esp32c5.rom.api.ld＝ets_update_cpu_frequency
	 *  へのPROVIDEエイリアス）が提供するシンボルであり，B-0/B-1の
	 *  リンクにはROM ldを一切含めていないため未定義参照になる。
	 *  B-0/B-1ではesp_rom_delay_us()等ROMの較正値に依存する処理を
	 *  使わないため実害はない。
	 *
	 *  フェーズ2b（TOPPERS_ESP32C5_WIFI＝Wi-Fi統合．esp_wifi.cmakeが
	 *  ROM ld一式をリンクするためシンボルが解決可能になる）では，
	 *  phy_init等のタイミングにROM較正値が必要になるため，C6と同じ
	 *  理由でここで呼び出す。
	 */
#ifdef TOPPERS_ESP32C5_WIFI
	esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ);
#endif /* TOPPERS_ESP32C5_WIFI */
}

void
software_init_hook(void)
{
	/* Initialize sio for fput */
#ifdef TOPPERS_OMIT_TECS
	sio_initialize(0);
	sio_opn_por(SIOPID_FPUT, 0);
#endif
}

/*
 *  ターゲット依存部 初期化処理
 */
void
target_initialize(void)
{
	/*
	 *  チップ依存の初期化（mtvec/mtvt・CLIC・コア依存部）
	 */
	chip_initialize();

	/*
	 *  ペリフェラル割込みソースをCPU割込み線へ割り当てる
	 *  （SYSTIMER_TARGET0とFROM_CPU_0（タイマ割込みの強制用）は
	 *  同じ線に多重マップする．target_timer.h参照．FROM_CPU_1は
	 *  テストプログラム用のras_int対象＝INTNO1．target_test.h参照．
	 *  コンソール（INTNO_SIO）はUART0のソースを割り当てる。
	 *  線16〜18を使用（線1〜15はフェーズ2bのWi-Fi shim・
	 *  esp_wifi_adapter.cのset_intr_wrapperが動的に使う予定のため
	 *  予約＝C6のtarget_kernel_impl.cと同じ退避パターン）
	 */
	esp32c5_intmtx_route(ESP32C5_INTSRC_SYSTIMER_TARGET0, 16U);
	esp32c5_intmtx_route(ESP32C5_INTSRC_FROM_CPU_0, 16U);
	esp32c5_intmtx_route(ESP32C5_INTSRC_UART0, 17U);
	esp32c5_intmtx_route(ESP32C5_INTSRC_FROM_CPU_1, 18U);
}

/*
 *  ターゲット依存部 終了処理
 */
void
target_exit(void)
{
	/*
	 *  software_term_hookの呼出し
	 */
	software_term_hook();

	/* チップ依存部の終了処理 */
	chip_terminate();

#ifdef TOPPERS_USE_QEMU
	/*
	 *  セミホスティングでQEMUを終了させる（SYS_EXIT）．
	 *  RV32ではa1にADP_Stopped_ApplicationExitを直接渡す
	 *  （パラメタブロックはRV64用）．
	 */
	{
		register uint32_t a0 __asm__("a0") = 0x18;      /* SYS_EXIT */
		register uint32_t a1 __asm__("a1") = 0x20026;   /* ApplicationExit */
		__asm__ volatile(
			".balign 16\n\t"
			".option push\n\t"
			".option norvc\n\t"
			"slli zero, zero, 0x1f\n\t"
			"ebreak\n\t"
			"srai zero, zero, 7\n\t"
			".option pop"
			: : "r"(a0), "r"(a1) : "memory");
	}
#endif /* TOPPERS_USE_QEMU */

	while (1) ;
}

/*
 *  エラー発生時の処理
 */
void
Error_Handler(void)
{
	while (1) ;
}

/*
 *  デフォルトのsoftware_term_hook（weak定義）
 */
__attribute__((weak))
void software_term_hook(void)
{
}
