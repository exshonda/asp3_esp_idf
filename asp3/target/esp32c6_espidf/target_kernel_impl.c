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
#include "esp_rom_sys.h"
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
 *  【実施87・診断ガード付き（既定無効）】APM (Access Permission
 *  Management) / HP-TEEブロックのアクセス制御をstock相当へ解除する。
 *
 *  C5実施42/43で確定した機構のC6移植：ASP3のDirect Bootはstockの
 *  APPスタートアップが必ず実行するbootloader_init_mem()
 *  （hal/components/bootloader_support/src/bootloader_mem.c）を一切
 *  経由しないため，
 *    (1) apm_hal_enable_ctrl_filter_all(false)
 *        -- C6ではHP_APM/LP_APM0/LP_APMの3コントローラ（C6にCPU_APMは
 *           存在しない．soc/esp32c6/include/soc/apm_defs.h）
 *    (2) apm_hal_set_master_sec_mode_all(APM_SEC_MODE_TEE)
 *        -- ただしC6はSOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL未定義のため
 *           stock/NuttXはこれを呼ばない（フィルタ解除のみで十分＝
 *           C5実施43のfilteronly変種と同型）．本実装は保守的に
 *           C5恒久版と同じ「両方」を行う．
 *  が一度も実行されず，WiFi/BTモデム（APM master id=4=APM_MASTER_MODEM，
 *  POR既定REE2）のバスマスタアクセスがHP_APMフィルタ（POR既定有効）の
 *  ゲート対象になり得る。
 *
 *  実施87実測（board B 14:C1:9F:E0:61:B0）：
 *    ASP3冷間（2ブート再現）：HP_APM_FUNC_CTRL=0xF・LP_APM0=0x1・
 *      LP_APM=0x3（全てPOR既定=有効），TEE_M4(MODEM)=3(REE2)，
 *      HP_APM M0-M3例外ラッチ=全て0（ただし当日board BのASP3は
 *      phy-init regi2cハングでMAC/modem活動前に停止しており，
 *      ラッチ不在は「modemのDMA試行が無かった」ことと区別できない）
 *    NuttX同一ボード：HP_APM/LP_APM0/LP_APM FUNC_CTRL=全て0
 *      （bootloader_init_mem実行済み），TEE_M4=3のまま（上記(2)の通り）
 *  ＝C5と同型の「初期化ギャップ」はC6にも実在する。因果（deaf-RXへの
 *  寄与）は本ラウンドでは判定不能（board Bのハング＋NuttXでも同板は
 *  RX不能という個体要因のため）——docs/wifi-shim-c6.md 実施87参照。
 *
 *  実施88（docs/wifi-shim-c6.md）：board C（14:C1:9F:E0:5A:9C）上で
 *  因果を確定・恒久化。ASP3冷間スキャン中にHP_APM M1例外ラッチが
 *  独立2ブートとも同一値で再現（STATUS=1／INFO0=0x00130001＝
 *  master_id=4(MODEM)+mode=3(REE2)+region=1／INFO1=0x4081f0a8＝
 *  HP SRAM内アドレス）＝C5実施42と同一機構の物証。本関数を無条件
 *  呼出しへ切替えたA/Bで，独立2ブートとも冷間scanが0APから
 *  14〜23APへ復帰し，ROM常駐`lmacRxDone`（0x40000c50，ファーム
 *  非依存）ヒット数も0/60sから59/60sへ復帰——deaf-RXが解消することを
 *  確認した。旧`ESP32C6_R87_APMFIX`ガードは撤去し無条件化する
 *  （C5実施43と同じ「フィルタ解除＋TEE昇格の両方」を保守的に採用，
 *  既に本ラウンドで検証済みの構成のまま変更しない）。
 */
#define ESP32C6_R87_TEE_MASTER_BASE     0x60098000U   /* TEE_M0..M31_MODE_CTRL_REG */
#define ESP32C6_R87_TEE_MASTER_COUNT    32U
#define ESP32C6_R87_HP_APM_FUNC_CTRL    0x600990C4U   /* hp_apm_reg.h +0xc4 */
#define ESP32C6_R87_LP_APM0_FUNC_CTRL   0x600998C4U   /* lp_apm0_reg.h +0xc4 */
#define ESP32C6_R87_LP_APM_FUNC_CTRL    0x600B38C4U   /* lp_apm_reg.h +0xc4 */
#define ESP32C6_R87_HP_APM_M0_STATCLR   0x600990CCU   /* +0xcc/+0xdc/+0xec/+0xfc */

static void
esp32c6_r87_apm_unblock(void)
{
	uint32_t	i;

	/*  (1) 3コントローラ全ての経路フィルタを解除  */
	sil_wrw_mem((void *)ESP32C6_R87_HP_APM_FUNC_CTRL, 0U);
	sil_wrw_mem((void *)ESP32C6_R87_LP_APM0_FUNC_CTRL, 0U);
	sil_wrw_mem((void *)ESP32C6_R87_LP_APM_FUNC_CTRL, 0U);

	/*  (2) 全32マスタをTEEモード(0)へ  */
	for (i = 0U; i < ESP32C6_R87_TEE_MASTER_COUNT; i++) {
		sil_wrw_mem((void *)(ESP32C6_R87_TEE_MASTER_BASE + i * 4U), 0U);
	}

	/*  HP_APM M0..M3の例外ラッチをクリア（以降の新規違反を検出可能に．
	 *  C6のHP_APMは4経路＝status/clr/info0/info1の4語×4組，stride 0x10） */
	for (i = 0U; i < 4U; i++) {
		sil_wrw_mem((void *)(ESP32C6_R87_HP_APM_M0_STATCLR + i * 0x10U), 1U);
	}
}

/*
 *  起動時のハードウェア初期化処理
 */
void
hardware_init_hook(void)
{

#ifdef ESP32C6_R91_FORCE_COLD_ICG
	/*
	 *  【実施91・診断専用（既定無効）】真の電源断（POR）が本セッションでは
	 *  物理的に実行できないため，PMU HP_ACTIVE ICG_MODEM_REG
	 *  (0x600B000C)をhardware_init_hookの最初期（他の何にも触れる前）で
	 *  強制的にPOR既定値0へ戻し，「esptoolのRTSピン経由リセット
	 *  （非JTAG，自然な起動系列）」のまま冷間状態を模擬する決定実験用。
	 *  JTAG `reset halt`経由の冷間模擬は実施91で別の（本修正とは無関係の
	 *  ）Illegal Instructionクラッシュ（reset cause=JTAG_CPUで決定的に
	 *  再現，PMU書換えの有無に関わらず発生）を誘発することが判明した
	 *  ため，その交絡を避ける目的でこちらを使う。本マクロは診断専用，
	 *  既定では無効（未定義）で恒久ビルドには影響しない。
	 */
	*(volatile uint32_t *)0x600B000CU = 0x00000000U;
#endif /* ESP32C6_R91_FORCE_COLD_ICG */

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

	/*
	 *  BUGFIX：submoduleのesp32c6.hの ESP32C6_LP_WDT_SWD_WKEY(=0x8F1D312A)は
	 *  誤り．esp-idf正本（esp_hal_wdt/esp32c6/lpwdt_ll.h の
	 *  LP_WDT_SWD_WKEY_VALUE）では LP_WDT・LP_WDT_SWD とも書込み保護解除
	 *  キーは 0x50D83AA1．誤キーだとSWD_CONFIG書込みが拒否され，super-WDT
	 *  が無効化されずesp_wifi_init中に発火してリブートループしていた
	 *  （asp3_jump.cは正しく0x50D83AA1を使い効いていた）．
	 *  ここでは正しいキーを直接使う（submodule修正は別途bump時）．
	 *  auto-feed＋SWD_DISABLE(bit30)の両方を設定．
	 */
	sil_wrw_mem((void *)ESP32C6_RTC_CNTL_SWD_WPROTECT, 0x50D83AA1U);
	sil_orw((void *)ESP32C6_RTC_CNTL_SWD_CONF,
			ESP32C6_RTC_CNTL_SWD_AUTO_FEED_EN | (1U << 30));
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
	 *
	 *  実施48（docs/wifi-shim-c6.md）：上記の通りCPUクロック自体は
	 *  ROMが起動時に160MHzへ設定済みだが，その事実をROM自身の
	 *  較正用大域変数（s_ticks_per_us，esp_rom_delay_us内部で
	 *  「要求us×s_ticks_per_us」ティック分ビジーウェイトする際に
	 *  参照される）へ明示的に通知するAPI呼出しが，Direct Bootの
	 *  どこにも存在しなかった（NuttX側はrtc_clk.cのクロック切替え
	 *  関数内で毎回esp_rom_set_cpu_ticks_per_us()を呼んでいるが，
	 *  ASP3はクロック切替え自体を行わないため，この呼出しも
	 *  漏れていた）．放置するとs_ticks_per_usはROM起動ごく初期の
	 *  低速クロック較正値のまま残り，phy_init全体で使われる
	 *  esp_rom_delay_us()が本来の約1/3の時間しか待たなくなる．
	 */
	esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ);

#ifdef TOPPERS_ESP32C6_WIFI
	/*
	 *  実施54（docs/wifi-shim-c6.md）：実施52で追加したcoex_pre_init()
	 *  呼出しが，従来アプリのmain_task内（esp_wifi_init直前）だと
	 *  register_chipv7_phy冒頭のBBPLL/regi2c較正（wait_i2c_sdm_stable，
	 *  block=0x63）を新たに停止させる退行を引き起こした（実施53）．
	 *  NuttXは同じ2行（esp_coex_adapter_register+coex_pre_init）を
	 *  ボード起動のごく初期（wifi初期化のはるか前）で呼んでおり，
	 *  ASP3も同じタイミングへ近付けるためhardware_init_hookから
	 *  呼ぶ．esp_shim_coex_adapter_register()は多重呼出しに対して
	 *  自己ガード済み（初回のみ実行）のため，各アプリのmain_task内の
	 *  既存呼出しと共存できる。
	 */
	extern void esp_shim_coex_adapter_register(void);
	esp_shim_coex_adapter_register();
#endif /* TOPPERS_ESP32C6_WIFI */

	/*
	 *  【実施87で発見・実施88で恒久化】APM/HP-TEEのアクセス制御を
	 *  stock相当へ解除（本関数定義の直上コメント参照．C5実施42/43の
	 *  esp32c5_r42_apm_unblock()のC6移植）。esp_wifi_init/PHY較正より
	 *  前＝hardware_init_hook末尾で呼ぶ（C5恒久版と同位置）。
	 */
	esp32c6_r87_apm_unblock();
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
