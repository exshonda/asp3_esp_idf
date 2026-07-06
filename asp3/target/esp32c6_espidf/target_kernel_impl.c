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
 *  ターゲット依存モジュール（ESP32-C6用）
 *
 *  pico2_riscv版からの流用．RP2350のクロック・GPIO初期化は不要
 *  （ROMブートローダ通過後の状態を継承．UART0は115200bpsで設定済み，
 *  SYSTIMERはリセット後デフォルトでクロック供給済み）．代わりに
 *  ESP32-C6固有の処理として，リセット後デフォルトで有効なウォッチ
 *  ドッグタイマの無効化を行う（無効化しないと数秒でリブートする）．
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include <sil.h>
#ifdef TOPPERS_OMIT_TECS
#include "chip_serial.h"
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
 *  DIAGNOSTIC（一時的・ハンドオフStep0調査）：ASP3起動シーケンスの
 *  どの段階まで到達するかを切り分けるための，クロック非依存の生UART0
 *  書き込み．min_uart_target（tmp/）と同じくUART0のTX FIFO
 *  （0x60000000）へ直接1バイトを書くだけ．ASP3のSIO初期化やクロック
 *  再設定に一切依存しない．各チェックポイントで固有の文字を数回出力し，
 *  ESP-IDF→ASP3ハンドオフ後にどこでクラッシュ／停止するかを観測する．
 *  調査完了後にrevertする．
 */
#define DIAG_UART0_FIFO      0x60000000U
#define DIAG_GPIO_OUT_W1TS   0x60091008U	/* GPIO_OUT_W1TS_REG（1でセット） */
#define DIAG_GPIO_OUT_W1TC   0x6009100CU	/* GPIO_OUT_W1TC_REG（1でクリア） */
#define DIAG_GPIO_MASK       0x7U			/* GPIO0/1/2＝XIAO D0/D1/D2 */
/*
 *  チェックポイントの到達番号(1..6)を，
 *   (a) GPIO0/1/2（XIAO D0/D1/D2）へ3ビットで「保持出力」する
 *       → ハング後もLogic8で最終レベルを読めば最後に到達した段階が判る．
 *       GPIOの方向設定（IOMUX/マトリクス）はジャンプ元のESP-IDF側で
 *       済ませてある前提（ここではOUTビットの更新のみ）．
 *   (b) UART0(0x60000000)へも数字を数回出力（観測できる場合の保険）．
 *  ESP-IDF→ASP3ハンドオフ後にどこでクラッシュ／停止するかを観測する
 *  診断専用．調査完了後にrevertする．
 */
static void
diag_mark(unsigned int n)
{
	/* GPIO0/1/2 を 3ビットの n で更新（保持出力） */
	*(volatile uint32_t *)DIAG_GPIO_OUT_W1TC = DIAG_GPIO_MASK;
	*(volatile uint32_t *)DIAG_GPIO_OUT_W1TS = (n & DIAG_GPIO_MASK);

	/* DIAGNOSTIC: UART0書き込みがASP3ハンドオフ文脈でハングする疑いの
	 * 検証のため，UART出力を一時的に無効化（GPIO保持出力のみ）．
	 * これで cp が先へ進めばUART FIFO書き込みが犯人と確定する． */
	(void) DIAG_UART0_FIFO;
}

/*
 *  MWDT（タイマグループのウォッチドッグ）の無効化
 */
static void
esp32c6_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTWPROTECT(timg_base),
				ESP32C6_TIMG_WDT_WKEY);         /* 書込み保護の解除 */
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTCONFIG0(timg_base), 0U);
	sil_wrw_mem((void *)ESP32C6_TIMG_WDTWPROTECT(timg_base), 0U);
}

/*
 *  起動時のハードウェア初期化処理
 */
void
hardware_init_hook(void)
{

	/*
	 *  DIAGNOSTIC（仮説検証）：ESP-IDFハンドオフ後，最初のTIMG0レジスタ
	 *  アクセスでハングする（cp1で停止＝GPIO値1）．C6では未クロックの
	 *  ペリフェラルへのアクセスはバスストール＝ハングを起こす．ESP-IDFが
	 *  TIMG0のペリフェラルクロックをゲートした可能性を検証するため，
	 *  TIMG0アクセス直前にPCRでTIMG0クロックを有効化・リセット解除する．
	 *  これで cp1 より先（>=2）へ進めば「ESP-IDFのクロックゲート」が犯人と確定．
	 */
	{
		volatile uint32_t *pcr_tg0 = (volatile uint32_t *)0x6009603CU; /* PCR_TIMERGROUP0_CONF_REG */
		uint32_t v = *pcr_tg0;
		v |= 0x1U;		/* PCR_TG0_CLK_EN */
		v &= ~0x2U;		/* PCR_TG0_RST_EN 解除 */
		*pcr_tg0 = v;
	}

	/*
	 *  ウォッチドッグタイマの無効化
	 *  （MWDT0/1・RTC WDT・スーパーWDT．リセット後デフォルトで有効）
	 */
	esp32c6_disable_mwdt(ESP32C6_TIMG0_BASE);
	esp32c6_disable_mwdt(ESP32C6_TIMG1_BASE);

	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTWPROTECT,
				ESP32C6_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_WDTWPROTECT, 0U);

	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_SWD_WPROTECT,
				ESP32C6_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C6_RTC_CNTL_SWD_CONF,
			ESP32C6_RTC_CNTL_SWD_AUTO_FEED_EN);
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_SWD_WPROTECT, 0U);

	/*
	 *  CPUクロックの切替えは不要（実機診断により判明）
	 *
	 *  当初はC3同様にPCR経由でSPLLへ明示的に切り替えるソフトウェア
	 *  操作が必要と想定していたが，実機診断（PCR_SYSCLK_CONF／
	 *  PCR_CPU_FREQ_CONFの読出し＋壁時計を用いた実測）の結果，ROM
	 *  ブートローダがDirect Boot到達前に既にSOC_CLK_SEL=SPLL・
	 *  分周比480MHz÷3÷1＝160MHzへ設定済みであることを確認した
	 *  （C3のBBPLLと同様，SPI_FAST_FLASH_BOOT経路でROMが既に有効化・
	 *  設定したものを流用しており，追加のレジスタ操作は不要かつ
	 *  行うべきでない）．CORE_CLK_MHZ＝160・SIL_DLY_TIM1/2は実測較正
	 *  済み（esp32c6.h・docs/dev/esp32c6-target.md参照）．
	 */
}

void
software_init_hook(void)
{
	diag_mark(1U);	/* DIAGNOSTIC: software_init_hook入口＝bss/dataクリア通過（PMP fix確認済） */
	/* Initialize sio for fput */
#ifdef TOPPERS_OMIT_TECS
	sio_initialize(0);
	diag_mark(2U);	/* DIAGNOSTIC: sio_initialize通過 */
	sio_opn_por(SIOPID_FPUT, 0);
	diag_mark(3U);	/* DIAGNOSTIC: sio_opn_por通過＝software_init_hook完了 */
#endif
}

/*
 *  ターゲット依存部 初期化処理
 */
void
target_initialize(void)
{
	diag_mark(4U);	/* DIAGNOSTIC: sta_ker到達→target_initialize入口 */
	/*
	 *  チップ依存の初期化（mtvec・割込みマトリクス・コア依存部）
	 */
	chip_initialize();
	diag_mark(5U);	/* DIAGNOSTIC: chip_initialize（mtvec/割込み）通過 */

	/*
	 *  ペリフェラル割込みソースをCPU割込み線へ割り当てる
	 *  （SYSTIMER_TARGET0とFROM_CPU_0（タイマ割込みの強制用）は
	 *  同じ線に多重マップする．target_timer.h参照．FROM_CPU_1は
	 *  テストプログラム用のras_int対象＝INTNO1．target_test.h参照．
	 *  コンソール（INTNO_SIO）はビルド時選択に応じてUART0または
	 *  USB Serial/JTAGのソースを割り当てる．
	 *  線16〜18を使用（線1〜15はWi-Fi shim・esp_wifi_adapter.cの
	 *  set_intr_wrapperが動的に使うため予約＝C3のesp32c3_espidf/
	 *  target_kernel_impl.cと同じ退避パターン．docs/wifi-shim.md参照）
	 */
	esp32c6_intmtx_route(ESP32C6_INTSRC_SYSTIMER_TARGET0, 16U);
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_0, 16U);
#ifdef TOPPERS_ESP32C6_CONSOLE_USBJTAG
	esp32c6_intmtx_route(ESP32C6_INTSRC_USB_SERIAL_JTAG, 17U);
#else /* TOPPERS_ESP32C6_CONSOLE_USBJTAG */
	esp32c6_intmtx_route(ESP32C6_INTSRC_UART0, 17U);
#endif /* TOPPERS_ESP32C6_CONSOLE_USBJTAG */
	esp32c6_intmtx_route(ESP32C6_INTSRC_FROM_CPU_1, 18U);
	diag_mark(6U);	/* DIAGNOSTIC: target_initialize完了（割込みルーティング済み・この後カーネルがタスク起動） */
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
