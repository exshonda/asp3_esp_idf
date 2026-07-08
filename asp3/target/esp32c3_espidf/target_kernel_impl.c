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
 *  ターゲット依存モジュール（ESP32-C3用）
 *
 *  pico2_riscv版からの流用．RP2350のクロック・GPIO初期化は不要
 *  （ROMブートローダ通過後の状態を継承．UART0は115200bpsで設定済み，
 *  SYSTIMERはリセット後デフォルトでクロック供給済み）．代わりに
 *  ESP32-C3固有の処理として，リセット後デフォルトで有効なウォッチ
 *  ドッグタイマの無効化を行う（無効化しないと数秒でリブートする）．
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include <sil.h>
#ifdef TOPPERS_OMIT_TECS
#include "chip_serial.h"
#endif

/*
 *  ROMの遅延較正大域変数 s_ticks_per_us を更新するROM API
 *  （esp32c3.rom.api.ld で ets_update_cpu_frequency へPROVIDE済み）．
 *  ヘッダ依存を避けるためここでプロトタイプのみ宣言する．
 */
extern void esp_rom_set_cpu_ticks_per_us(uint32_t ticks_per_us);

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
esp32c3_disable_mwdt(uint32_t timg_base)
{
	sil_wrw_mem((void *)ESP32C3_TIMG_WDTWPROTECT(timg_base),
				ESP32C3_TIMG_WDT_WKEY);         /* 書込み保護の解除 */
	sil_wrw_mem((void *)ESP32C3_TIMG_WDTCONFIG0(timg_base), 0U);
	sil_wrw_mem((void *)ESP32C3_TIMG_WDTWPROTECT(timg_base), 0U);
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
	 */
	esp32c3_disable_mwdt(ESP32C3_TIMG0_BASE);
	esp32c3_disable_mwdt(ESP32C3_TIMG1_BASE);

	sil_wrw_mem((void *)ESP32C3_RTC_CNTL_WDTWPROTECT,
				ESP32C3_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C3_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C3_RTC_CNTL_WDTWPROTECT, 0U);

	sil_wrw_mem((void *)ESP32C3_RTC_CNTL_SWD_WPROTECT,
				ESP32C3_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C3_RTC_CNTL_SWD_CONF,
			ESP32C3_RTC_CNTL_SWD_AUTO_FEED_EN);
	sil_wrw_mem((void *)ESP32C3_RTC_CNTL_SWD_WPROTECT, 0U);

	/*
	 *  CPUクロックをPLL（160MHz）へ切り替える
	 *
	 *  Direct Boot（二段ブートローダ無し）では，CPUはリセット既定の
	 *  XTAL/2＝20MHzのまま起動する（実機dlynse計測で確認）．BBPLL
	 *  （480MHz）はROMブートローダがブート時に有効化したものを流用
	 *  する（SPI_FAST_FLASH_BOOT経路ではROMがPLLを使用している）．
	 */
	sil_mskw((void *)ESP32C3_SYSTEM_CPU_PER_CONF,
			 ESP32C3_CPU_PER_CONF_PLL_160M, ESP32C3_CPU_PER_CONF_CLK_MASK);
	sil_mskw((void *)ESP32C3_SYSTEM_SYSCLK_CONF,
			 ESP32C3_SYSCLK_CONF_SEL_PLL, ESP32C3_SYSCLK_CONF_SEL_MASK);

	/*
	 *  ROMの遅延較正大域変数 s_ticks_per_us をCPU実クロック(160MHz)へ
	 *  更新する（emi.c:164解消後に顕在化したRF較正リセットの真因対策）．
	 *
	 *  Direct BootではCPUはリセット既定のXTAL/2＝20MHzで起動し，ROMは
	 *  その20MHzで s_ticks_per_us を較正する（実機JTAGでB=0x14=20を確認．
	 *  基準機NuttXは0xa0=160）．上でPLL160MHzへ切替えても，その事実を
	 *  ROMの s_ticks_per_us へ通知するAPI（esp_rom_set_cpu_ticks_per_us＝
	 *  ets_update_cpu_frequency）が呼ばれないと，esp_rom_delay_us(N) は
	 *  N×20 サイクルしかビジーウェイトせず，本来の約1/8の時間しか待たない．
	 *  register_chipv7_phy等のRF/PHY較正はPLLロック待ち・アナログ整定
	 *  待ちにこの遅延を多用するため，遅延不足で較正が破綻しチップが
	 *  リセットする（rst:0x15）．C6でも同一機構を確認・修正済み
	 *  （docs/wifi-shim-c6.md 実施48/49，memory/project_c6_agc_investigation.md）．
	 *  NuttXはrtc_clk.cのクロック切替関数内で毎回呼ぶが，ASP3はクロック
	 *  切替をこのhook内で直接行うため同じ位置で明示的に呼ぶ．
	 *  WiFi/BT双方（RF/PHYを使う）に必要なので無条件で呼ぶ．
	 */
	esp_rom_set_cpu_ticks_per_us(160U);

#ifdef TOPPERS_ESP32C3_WIFI
	/*
	 *  Wi-Fi/BTモデムクロックの全面有効化
	 *
	 *  ESP-IDFは起動時のesp_perip_clk_init()（esp_system/port/soc/
	 *  esp32c3/clk.c）でSYSTEM_WIFI_CLK_EN_REGにSYSTEM_WIFI_CLK_EN
	 *  （0x00FB9FCF）をセットしてモデム系クロックを起こす．Direct Boot
	 *  ではこの初期化が走らずMACクロックが不完全（bit6/11/12/16欠落）で，
	 *  esp_wifi_start()内のhal_initがMAC MMIO（0x60033D14）応答待ちで
	 *  停止する（実機JTAGで特定）．ここで同じ値をセットする．
	 *  C3のperiph_ll_wifi_module_enable_clk_clear_rstのマスクは0（no-op）
	 *  で，モデムクロックはこの起動時設定に依存しているため必須．
	 */
	sil_orw((void *)0x60026014U, 0x00FB9FCFU);	/* SYSTEM_WIFI_CLK_EN_REG */
#endif /* TOPPERS_ESP32C3_WIFI */
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
	 *  チップ依存の初期化（mtvec・割込みマトリクス・コア依存部）
	 */
	chip_initialize();

	/*
	 *  ペリフェラル割込みソースをCPU割込み線へ割り当てる
	 *  （SYSTIMER_TARGET0とFROM_CPU_0（タイマ割込みの強制用）は
	 *  同じ線に多重マップする．target_timer.h参照．FROM_CPU_1は
	 *  テストプログラム用のras_int対象＝INTNO1．target_test.h参照．
	 *  コンソール（INTNO_SIO＝線17）はビルド時選択に応じてUART0または
	 *  USB Serial/JTAGのソースを割り当てる．線1〜15はWi-Fi blobが
	 *  _set_intrで指定する線番号用に開放している）
	 */
	esp32c3_intmtx_route(ESP32C3_INTSRC_SYSTIMER_TARGET0, 16U);
	esp32c3_intmtx_route(ESP32C3_INTSRC_FROM_CPU_0, 16U);
#ifdef TOPPERS_ESP32C3_CONSOLE_USBJTAG
	esp32c3_intmtx_route(ESP32C3_INTSRC_USB_SERIAL_JTAG, 17U);
#else /* TOPPERS_ESP32C3_CONSOLE_USBJTAG */
	esp32c3_intmtx_route(ESP32C3_INTSRC_UART0, 17U);
#endif /* TOPPERS_ESP32C3_CONSOLE_USBJTAG */
	esp32c3_intmtx_route(ESP32C3_INTSRC_FROM_CPU_1, 18U);
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
