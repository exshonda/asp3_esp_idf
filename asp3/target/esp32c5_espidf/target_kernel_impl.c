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

#ifdef TOPPERS_ESP32C5_WIFI
/*
 *  【実施31】較正キー(A)候補の加算移植：stockの2nd-stageブートローダが
 *  bootloader_init()の中で無条件に実行する bootloader_random_enable()/
 *  bootloader_random_disable()（SAR ADCをエントロピー源として駆動する
 *  reset→enable→sampling→disableサイクル．ESP-IDF v6.1
 *  components/bootloader_support/src/bootloader_random_esp32c5.c）を，
 *  レジスタ順序・自己クリア型パルスを保持したまま生MMIO直叩きで移植した
 *  もの。ASP3のDirect Bootは2nd-stageブートローダを一切経由しないため，
 *  このサイクルが実機で一度も実行されていない。
 *
 *  実施31のJTAG実測で新規発見：PCR_SARADC_CLKM_CONF_REG(0x6009608c)の
 *  CLKM_EN(bit22)が，ASP3較正ハング状態で2/2ブート共に0x00004000
 *  （CLKM_EN=0）である一方，stock（実施29 r29_snapshot，2/2）は
 *  0x00404000（CLKM_EN=1）——bootloader_random_enable()の
 *  adc_ll_enable_func_clock(true)が立て，disable()側では一度も
 *  クリアされない（BOOTLOADER_BUILD分岐はregi2c_saradc_disable()を
 *  呼ばずADCの regi2c 電源自体は解除しない設計）ため恒久的に残る，
 *  という新規・再現性ありの静的差分。実施14-25/実施20の網羅比較
 *  （PCR_SARADC_CONF等）はこのCLKM_CONFレジスタを対象に含んでいな
 *  かった（見落とし）。
 *
 *  【移植範囲の意図的な限定】bootloader_random_enable/disable本体は
 *  adc_ll_regi2c_init()/adc_ll_set_calibration_param()等，regi2c
 *  （I2C_SAR_ADCブロック，host 0x69）経由のバイアス・較正コード設定も
 *  含むが，regi2cアドレス空間は実施16で8ブロック×reg 0x00-0x1F全域を
 *  ASP3/stock間で比較済み・新規差分ゼロと確定している。したがって
 *  regi2c媒介部分は「既に最終値が一致することが分かっている」ため本
 *  移植では意図的に対象外とし，PCR/PMU/APB_SARADC/LPPERIの純粋MMIO
 *  操作（レジスタ順序・自己クリア型パルスは保持）のみを移植した。
 *  bootloader_hardware_init()のregi2cマスタクロック有効化
 *  （MODEM_LPCON.clk_conf.clk_i2c_mst_en）は既に本ファイルの
 *  phy_enable_wrapper（esp_wifi_adapter.c）が0x600af018=0x7で等価に
 *  設定済み（実施09時点で実機確認済み）のため，configure_clock()の
 *  clk_i2c_mst_sel_160mビットのみ追加移植する。
 *  bootloader_ana_reset_config()（BOD/glitch reset設定）と
 *  regi2c媒介部分は，本ラウンドの結果次第で次段の候補として残す
 *  （docs/c5-bringup.md実施31）。
 */
#define ESP32C5_R31_PCR_SARADC_CONF        0x60096088U
#define ESP32C5_R31_PCR_SARADC_CLKM_CONF   0x6009608CU
#define ESP32C5_R31_PCR_TSENS_CLK_CONF     0x60096090U
#define ESP32C5_R31_MODEM_SYSCON_CLK_CONF  0x600A9C04U
#define ESP32C5_R31_PMU_RF_PWC             0x600B0158U
#define ESP32C5_R31_LPPERI_RNG_CFG         0x600B2824U
#define ESP32C5_R31_APB_SARADC_CTRL        0x6000E000U
#define ESP32C5_R31_APB_SARADC_CTRL2       0x6000E004U
#define ESP32C5_R31_APB_SARADC_PATT_TAB1   0x6000E018U
#define ESP32C5_R31_APB_SARADC_PATT_TAB2   0x6000E01CU

static void
esp32c5_r31_set_patt_table(uint32_t pattern_index, uint32_t unit,
							uint32_t channel, uint32_t atten)
{
	/*  adc_ll_digi_set_pattern_table()（hal/esp32c5/include/hal/adc_ll.h）
	 *  のビット詰め計算をそのまま転写。  */
	uint32_t	addr;
	uint32_t	off = (pattern_index % 4U) * 6U;
	uint32_t	val = (atten & 0x3U) | ((channel & 0x7U) << 2)
					  | ((unit & 0x1U) << 5);
	uint32_t	tab;

	addr = ((pattern_index / 4U) == 0U) ? ESP32C5_R31_APB_SARADC_PATT_TAB1
										 : ESP32C5_R31_APB_SARADC_PATT_TAB2;
	tab = sil_rew_mem((void *)addr);
	tab &= ~(0xFC0000U >> off);
	tab |= ((val & 0x3FU) << 18) >> off;
	sil_wrw_mem((void *)addr, tab);
}

static void
esp32c5_r31_bootloader_random_cycle(void)
{
	uint32_t	v;

	/*  bootloader_hardware_init()の一部：regi2cマスタクロックのソース
	 *  選択（160MHz）。clk_i2c_mst_enはphy_enable_wrapperが既に等価に
	 *  設定するため対象外（上記コメント参照）。  */
	v = sil_rew_mem((void *)ESP32C5_R31_MODEM_SYSCON_CLK_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_MODEM_SYSCON_CLK_CONF, v | (1U << 12));

	/*  bootloader_random_enable()（BOOTLOADER_BUILD分岐）  */

	/*  adc_ll_reset_register()：自己クリア型パルス×2  */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CONF, v | (1U << 1));	/* RST_EN=1 */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CONF, v & ~(1U << 1));	/* RST_EN=0 */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CONF, v | (1U << 3));	/* REG_RST_EN=1 */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CONF, v & ~(1U << 3));	/* REG_RST_EN=0 */

	/*  temperature_sensor_ll_reset_module()：同じPCRリセットドメイン内の
	 *  自己クリア型パルス（tsensレジスタのbackup/restoreはASP3が
	 *  tsensを未使用のため省略——docs/c5-bringup.md実施31に明記）  */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_TSENS_CLK_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_TSENS_CLK_CONF, v | (1U << 23));
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_TSENS_CLK_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_TSENS_CLK_CONF, v & ~(1U << 23));

	/*  adc_ll_enable_bus_clock(true) / adc_ll_enable_func_clock(true)  */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CONF, v | (1U << 2));	/* REG_CLK_EN=1 */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF, v | (1U << 22));	/* CLKM_EN=1 */

	/*  adc_ll_digi_clk_sel(ADC_DIGI_CLK_SRC_XTAL) → CLKM_SEL[21:20]=0 ；
	 *  adc_ll_digi_controller_clk_div(0,0,0) → DIV_NUM[19:12]/DIV_B[11:6]/
	 *  DIV_A[5:0]を全て0に  */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF);
	v &= ~((0x3U << 20) | (0xFFU << 12) | (0x3FU << 6) | 0x3FU);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF, v);
	/*  APB_SARADC側のADC_CTRL_CLK（digital domain clock）も立てる  */
	v = sil_rew_mem((void *)ESP32C5_R31_APB_SARADC_CTRL);
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_CTRL, v | (1U << 6));

	/*  regi2c_ctrl_ll_i2c_sar_periph_enable()：PMU_RF_PWC_REGの
	 *  reset→enable→work切替パルス  */
	v = sil_rew_mem((void *)ESP32C5_R31_PMU_RF_PWC);
	sil_wrw_mem((void *)ESP32C5_R31_PMU_RF_PWC, v & ~(1U << 26));		/* PERIF_I2C_RSTB=0 */
	v = sil_rew_mem((void *)ESP32C5_R31_PMU_RF_PWC);
	sil_wrw_mem((void *)ESP32C5_R31_PMU_RF_PWC, v | (1U << 27));		/* XPD_PERIF_I2C=1 */
	v = sil_rew_mem((void *)ESP32C5_R31_PMU_RF_PWC);
	sil_wrw_mem((void *)ESP32C5_R31_PMU_RF_PWC, v | (1U << 26));		/* PERIF_I2C_RSTB=1 */

	/*  [regi2c媒介のadc_ll_regi2c_init()/adc_ll_set_calibration_param()
	 *  は上記コメントの理由により意図的に対象外]  */

	/*  パターンテーブル設定：ADC_UNIT_1 ch7 atten12dB（index0），
	 *  ADC_UNIT_2 ch1 atten12dB（index1）  */
	esp32c5_r31_set_patt_table(0U, 0U, 7U, 3U);
	esp32c5_r31_set_patt_table(1U, 1U, 1U, 3U);
	v = sil_rew_mem((void *)ESP32C5_R31_APB_SARADC_CTRL);
	v &= ~(0x7U << 15);
	v |= (1U << 15);					/* patt_len=2 → (2-1)=1 */
	v &= ~(0xFFU << 7);
	v |= (15U << 7);					/* digi_set_clk_div(15) */
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_CTRL, v);

	v = sil_rew_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2);
	v &= ~(0xFFFU << 12);
	v |= (200U << 12);					/* trigger_interval(200) */
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2, v);
	v = sil_rew_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2);
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2, v | (1U << 24));	/* timer_en=1 */

	/*  rng_ll_enable_sample/rtc_timer/rng_timer  */
	v = sil_rew_mem((void *)ESP32C5_R31_LPPERI_RNG_CFG);
	v |= (1U << 0);						/* SAMPLE_ENABLE */
	v |= (0x3U << 10);					/* RTC_TIMER_EN=3 */
	v |= (1U << 9);						/* RNG_TIMER_EN */
	sil_wrw_mem((void *)ESP32C5_R31_LPPERI_RNG_CFG, v);

	/*  stockはこの区間でflash読出し／復号等のRNG消費処理を挟んでから
	 *  disable()を呼ぶ。ASP3のDirect BootにはPHY較正上有意な代替処理
	 *  が無いため，実際にADC変換が複数回走るのに十分な時間として
	 *  5ms相当のbusy waitで近似する（実施31の意図的な簡略化点）。  */
	esp_rom_delay_us(5000U);

	/*  bootloader_random_disable()（BOOTLOADER_BUILD分岐：
	 *  regi2c_saradc_disable()は呼ばれないため，PMU_RF_PWC/regi2c電源は
	 *  解除せずそのまま——stockの実機観測（CLKM_EN=1が恒久的に残る）と
	 *  一致させる）  */
	v = sil_rew_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2);
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_CTRL2, v & ~(1U << 24));	/* trigger_disable */
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_PATT_TAB1, 0xFFFFFFU);
	sil_wrw_mem((void *)ESP32C5_R31_APB_SARADC_PATT_TAB2, 0xFFFFFFU);

	/*  [regi2c媒介のadc_ll_regi2c_adc_deinit()は対象外]  */

	/*  ANALOG_CLOCK_DISABLE()はBOOTLOADER_BUILDではno-op（regi2c_ctrl.h
	 *  参照）——何もしない  */

	/*  adc_ll_digi_controller_clk_div(4,0,0) + adc_ll_digi_clk_sel(XTAL)  */
	v = sil_rew_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF);
	v &= ~((0x3U << 20) | (0xFFU << 12) | (0x3FU << 6) | 0x3FU);
	v |= (4U << 12);
	sil_wrw_mem((void *)ESP32C5_R31_PCR_SARADC_CLKM_CONF, v);
}
#endif /* TOPPERS_ESP32C5_WIFI */

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

	/*
	 *  【実施31】較正キー(A)候補：stock 2nd-stageブートローダの
	 *  bootloader_random_enable/disable()（SAR ADC駆動サイクル）の
	 *  加算移植。esp_rom_set_cpu_ticks_per_us()の後（esp_rom_delay_us
	 *  のus較正が有効になった後）に置く。詳細は上のコメント・
	 *  docs/c5-bringup.md実施31参照。
	 */
	esp32c5_r31_bootloader_random_cycle();
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
	/*
	 *  コンソール（INTNO_SIO＝線17）はビルド時選択に応じて
	 *  USB Serial/JTAG または UART0 のソースを割り当てる
	 *  （C6の target/esp32c6_gcc/target_kernel_impl.c と同じ条件分岐．
	 *  usbjtagコンソールでUART0を割り当てるとTX完了割込みがCPUへ届かず
	 *  割込み駆動出力＝ログタスク経由のタスク出力が停止する）
	 */
#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG
	esp32c5_intmtx_route(ESP32C5_INTSRC_USB_SERIAL_JTAG, 17U);
#else /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
	esp32c5_intmtx_route(ESP32C5_INTSRC_UART0, 17U);
#endif /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
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
