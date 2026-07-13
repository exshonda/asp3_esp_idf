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
 *  【実施32】CPUクロックの明示的切替え（XTAL→PLL_F240M÷3，レジスタ
 *  設定はbootloader_clock_configure(80MHz)相当だが実測は96MHz——
 *  下記・esp32c5.hのCORE_CLK_MHZコメント参照）
 *
 *  背景：docs/c5-bringup.md 実施32。JTAG実測で，ASP3のDirect BootはPCR
 *  SOC根クロックmux（PCR_SYSCLK_CONF.soc_clk_sel）がXTAL（=0）のまま
 *  一度も切り替わっておらず，実CPUクロックはXTAL48MHz直結（実測47.96〜
 *  48.00MHz，mcycle対SYSTIMER 16MHzの二点法・WiFi有無2ビルド×独立2回で
 *  再現）と判明した——旧「192MHz実機確定」（実施03）は誤りだったか，
 *  あるいは何らかの経緯で後退したかは未確定（本ラウンドでは追跡しない）。
 *  stock（2nd-stageブートローダのbootloader_clock_configure()経由）は
 *  同じPCRレジスタでsoc_clk_sel=3(PLL_F240M)を使用する。
 *
 *  本関数はbootloader_clock_configure()のCPUクロック切替え部分
 *  （rtc_clk_init→rtc_clk_cpu_freq_set_config）のうち，ASP3の較正キー
 *  (A)判定実験（実施30のP4ハンドオフ＝ext_mem_init直後・sys_rtc_init前，
 *  すなわちbootloader_clock_configure完了直後の状態）に対応する構成，
 *  すなわちCONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ=80（stock実測sdkconfigで
 *  確認済み）と同じ「PLL_F240Mソース・CPUディバイダ3・AHBディバイダ6
 *  ＝CPU80MHz/AHB40MHz」を再現する（rtc_clk_cpu_freq_to_pll_240_mhz()
 *  のAHBディバイダは要求CPU周波数に関わらず常に6固定であることをソース
 *  で確認済み）。app側のesp_clk_init()相当（240MHzへのさらなる昇格・
 *  esp_perip_clk_init）は対象外（次段，(B)scan/RXキーの移植で扱う）。
 *
 *  安全設計（JTAG実測を踏まえた最小構成）：
 *  - BBPLLの較正状態はI2C_ANA_MST_ANA_CONF0_REG bit24(CAL_DONE)で確認
 *    してから分岐する。ASP3の現在の較正ハング状態でCAL_DONE=1（2/2実測，
 *    ROMが既に較正を完了させている）ことをJTAGで確認済みのため，通常は
 *    再較正ループを通らない（`while(!cal_done);`という無条件待ちループを
 *    ハング状態のまま埋め込むリスクを避ける）。もしCAL_DONE=0の場合の
 *    フォールバックとして，タイムアウト付きの較正シーケンス
 *    （clk_ll_bbpll_calibration_start/stop相当）も用意する
 *    （無限待ちを避けるため上限ループ回数を設ける——rtc_clk.cの
 *    while(!is_done)は無条件待ちだが，ASP3では安全側に倒す）。
 *  - PMU_IMM_HP_CK_POWER_REG（0x600B00CC）はWT（write-trigger）型の
 *    自己クリアレジスタで，clk_ll_bbpll_enable()相当のtie-highパルス
 *    （XPD_BBPLL/XPD_BBPLL_I2C/XPD_BB_I2C/GLOBAL_BBPLL_ICG）を無条件に
 *    一度発行する（安全：自己クリア・読み戻し不能だが書込み自体に副作用
 *    リスクなし，実施23が試験した0x600B0014＝FSM影武者レジスタとは別の
 *    実効レジスタ）。
 *  - ディバイダ設定→ソース切替え→bus_clk_update（自己クリア型トリガ）の
 *    順序はrtc_clk_cpu_freq_to_pll_240_mhz()のとおり保持する。
 */
#define ESP32C5_R32_PCR_SYSCLK_CONF        (ESP32C5_PCR_BASE + 0x110U)
#define ESP32C5_R32_PCR_CPU_FREQ_CONF      (ESP32C5_PCR_BASE + 0x118U)
#define ESP32C5_R32_PCR_AHB_FREQ_CONF      (ESP32C5_PCR_BASE + 0x11CU)
#define ESP32C5_R32_PCR_BUS_CLK_UPDATE     (ESP32C5_PCR_BASE + 0x144U)
#define ESP32C5_R32_PMU_IMM_HP_CK_POWER    0x600B00CCU
#define ESP32C5_R32_I2C_ANA_MST_ANA_CONF0  0x600AF818U

#define ESP32C5_R32_PMU_TIE_HIGH_GLOBAL_BBPLL_ICG  (1U << 25)
#define ESP32C5_R32_PMU_TIE_HIGH_XPD_BB_I2C        (1U << 28)
#define ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL_I2C     (1U << 29)
#define ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL         (1U << 30)
#define ESP32C5_R32_I2C_MST_BBPLL_CAL_DONE         (1U << 24)

/*
 *  【実施34】CPUディバイダ（stockのCONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240，
 *  `stock_scan/sdkconfig`実測で確認済み＝ESP-IDF既定の最終動作周波数）。
 *  PLL_F240Mソースは（BBPLL 480MHz較正が正しければ）ネット240MHzのため，
 *  cpu_divider=1で240MHz・cpu_divider=3で80MHz（bootloader相当，中間
 *  検証用）。AHBディバイダはrtc_clk_cpu_freq_to_pll_240_mhz()のとおり
 *  要求CPU周波数に関わらず常に6固定（40MHz，制約cpu_div<=ahb_div かつ
 *  ahb_div%cpu_div==0はcpu_div=1/3のいずれでも満たす）。
 *  段階検証（docs/c5-bringup.md実施34）：まず3（80MHz，安全側）でBBPLL
 *  修正の効果を実測確認してから1（240MHz，ESP-IDF標準・最終構成）へ
 *  切替えた。
 */
#define ESP32C5_R34_CPU_DIVIDER   1U   /* 【実施34最終値】240MHz＝ESP-IDF標準（stock既定CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240と同一）。
										 * 80MHz(div=3)段階でBBPLL修正後の実測79.9934/80.0095MHzを確認済み（2/2）。 */
#define ESP32C5_R34_AHB_DIVIDER   6U

static void esp32c5_r34_bbpll_configure_480mhz(void);

/*
 *  【実施35】esp_clk_init()／esp_clk_tree_initialize()の残余効果の移植
 *  （docs/c5-bringup.md実施35）。実施32/34はesp_clk_init()のうち「CPU
 *  周波数切替え」（rtc_clk_cpu_freq_set_config，末尾の1項目）だけを
 *  移植済みで，それより前の項目（RTC_FAST_CLK源選択・RTC_SLOW_CLK源
 *  選択＋較正値保存）は未移植のまま残っていた。実機JTAG差分確認
 *  （stock=ESP-IDF標準ブート列 vs ASP3冷間，2/2再現）で下記2件の
 *  静的差分を確認済み：
 *
 *  1. LP_CLKRST_LP_CLK_CONF.fast_clk_sel（0x600B0400 bits[3:2]）：
 *     ASP3＝1（XTAL_D2，リセット既定値のまま未変更）／stock＝0（RC_FAST，
 *     esp_clk_init()のrtc_clk_fast_src_set(RC_FAST)相当，既定
 *     CONFIG_RTC_FAST_CLK_SRC_RC_FASTで確認）。
 *  2. RTC_SLOW_CLK_CAL_REG（=LP_AON_STORE1，0x600B1004）：
 *     ASP3＝0x00000000（一度も書込まれていない）／stock＝0x00358b20
 *     （esp_clk_init()のselect_rtc_slow_clk()内，rtc_clk_cal()による
 *     実測較正値をesp_clk_slowclk_cal_set()が保存．Q13.19固定小数点，
 *     マイクロ秒／サイクル）。
 *
 *  advisor査読：promisc_rx_count=0（実施33/34）という「タイミング定数の
 *  誤りというより受信経路そのものが無反応」という症状の性質からは，
 *  2（RTC_SLOW_CLK較正値）よりも1（RTC_FAST_CLK源，PMU/LPドメインの
 *  電源状態機械に絡む可能性）を先に試すべきとの指摘を受け，本ラウンドは
 *  1→2の順で1候補ずつ加算する。
 *
 *  【実施35・候補1の実機結果】esp32c5_r35_rtc_fast_clk_select()を単独で
 *  加算した冷間Direct Boot・独立2回のRTSリセット試行で，JTAG読み戻しに
 *  よりfast_clk_sel=0（RC_FAST，stock一致）へ切り替わったことを確認した
 *  上で，AP検出は依然`0 APs found`のまま（2/2）——**候補1単独では
 *  refute**。次段（候補2）を試す前提として，候補1は「正しさの上では
 *  stockに合わせる価値がある」ため実装は残したままとする（無害・
 *  他の副作用も観測されず）。
 *

 *  なお，PMU LP_ACTIVE.clk_power（0x600B00AC，xpd_fosc等）はJTAG差分
 *  確認の結果ASP3=stock=0x40000000で完全一致（ROM/PMUのPOR既定値が
 *  既にesp_clk_init()の目標値と一致していたと判明）——**候補から除外
 *  済み**（porting不要，rigor docの「既に一致する候補への移植は
 *  無駄働き」を回避）。
 */
#define ESP32C5_R35_LP_CLKRST_LP_CLK_CONF          0x600B0400U
#define ESP32C5_R35_LP_CLKRST_FAST_CLK_SEL_M       (0x3U << 2)
#define ESP32C5_R35_LP_CLKRST_FAST_CLK_SEL_RC_FAST (0x0U << 2)

/*
 *  候補1：RTC_FAST_CLK源をリセット既定のXTAL_D2からRC_FASTへ切替え
 *  （clk_ll_rtc_fast_set_src(SOC_RTC_FAST_CLK_SRC_RC_FAST)相当）。
 *  他ビット（slow_clk_selは既にstockと同じRC_SLOW=0）を保持する
 *  masked read-modify-writeとする。
 */
static void
esp32c5_r35_rtc_fast_clk_select(void)
{
	uint32_t	v;

	v = sil_rew_mem((void *)ESP32C5_R35_LP_CLKRST_LP_CLK_CONF);
	v &= ~ESP32C5_R35_LP_CLKRST_FAST_CLK_SEL_M;
	v |= ESP32C5_R35_LP_CLKRST_FAST_CLK_SEL_RC_FAST;
	sil_wrw_mem((void *)ESP32C5_R35_LP_CLKRST_LP_CLK_CONF, v);
}

/*
 *  候補2：RTC_SLOW_CLK_CAL_REG（esp_clk_slowclk_cal_set()相当）。
 *
 *  正規の実装（select_rtc_slow_clk()のrtc_clk_cal()）は，主XTAL
 *  （SYSTIMER相当の高精度基準）を用いてRTC_SLOW_CLKの実測サイクル数を
 *  数百サイクル分カウントし，Q13.19固定小数点のus/サイクル値を算出する
 *  もので，実装コストと実機ハングリスクが候補1より高い。task手順の
 *  「固定値書込みでまず因果を確認→効けば正式な較正実装へ」の2段構えに
 *  従い，本段階では**stockの実測値をそのまま定数として注入**し，
 *  この経路が実際にAP検出を左右するかどうかをまず安価に確認する
 *  （効かなければ正式実装は不要，効けば次段でrtc_clk_cal()相当の実測
 *  ルーチンへ格上げする）。
 *
 *  注入値0x00358b20は，stock ESP-IDF v6.1 examples/wifi/scan
 *  （esp32c5，本ラウンドでC5#1へ再ビルド・再フラッシュして採取，
 *  RTSリセット後T=0.3s/6.0sの独立読み取り2点・独立2回のRTSリセット
 *  試行で全て同一値，安定）の実測値であり，ASP3自身のRC_SLOW発振器の
 *  真の周波数とは一致しない可能性がある（同一チップでも発振器個体差・
 *  温度依存があるため，本来は自己測定が正しい）——本段階はあくまで
 *  「この値域のレジスタが0か非ゼロかでAP検出が変わるか」という因果の
 *  有無を安価に確認する目的の暫定実装であることに注意。
 */
#define ESP32C5_R35_RTC_SLOW_CLK_CAL_REG   0x600B1004U /* LP_AON_STORE1 */
#define ESP32C5_R35_RTC_SLOW_CLK_CAL_FIXED 0x00358b20U /* stock実測値（本ラウンド採取） */

static void
esp32c5_r35_rtc_slowclk_cal_set_fixed(void)
{
	sil_wrw_mem((void *)ESP32C5_R35_RTC_SLOW_CLK_CAL_REG,
				ESP32C5_R35_RTC_SLOW_CLK_CAL_FIXED);
}

/*
 *  候補3：PMU HP_MODEM電源/クロックバンク（＋HP_ACTIVE.HP_REGULATOR0）
 *  の未初期化（docs/c5-bringup.md実施35）。
 *
 *  背景：候補1・候補2を実装しても実機でAP検出は変わらなかった（0 APs
 *  found，2/2）ため，advisor指摘に従いesp_clk_init()の外側まで対象を
 *  広げ，実施29の11ブロックJTAGスナップショット相当の広域差分比較を
 *  実施した（本ラウンド，`blockdump.py`）。その結果，PMU
 *  HP_MODEMバンク（`pmu_hp_system_init()`がPMU_MODE_HP_MODEMモードで
 *  書込む7レジスタ，0x600B0034〜0x600B005C）が**ASP3では一度も書込まれず
 *  ほぼ全ゼロのまま**（対してstockは`pmu_init()`実行後の実測値が全て
 *  非ゼロ）という，実施21〜24が検討していなかった新規の大きな差分を
 *  発見した——実施23「決定実験C」はHP_ACTIVEバンクの一部フィールド
 *  （CK_POWER等）のみを対象としており，HP_MODEMバンク（PMUがモデム
 *  電源状態へ遷移した際に実際に使われるICG/クロック/電源設定）は
 *  当時の較正キー(A)調査の範囲外だった。HP_ACTIVE.HP_REGULATOR0
 *  （0x600B0028）にも軽微な差分がある。
 *
 *  正式には`pmu_hp_system_param_default(PMU_MODE_HP_MODEM, ...)`の
 *  デフォルトパラメータテーブル（`pmu_param.c`，eFuse依存項目を含む）を
 *  再現すべきだが，実装コストが高いため，task手順の2段構えに従い
 *  **本段階ではstockの実測値をそのまま定数として注入**し，このバンクが
 *  AP検出を左右するかをまず安価に確認する。
 *
 *  実施30のP1/P4ハンドオフ実験との整合性の注記：P1（stockの
 *  `system_early_init()`直前，すなわちstockの`pmu_init()`実行**後**）は
 *  RX恒久ゼロだった。もしHP_MODEMバンクの初期化だけで十分なら，P1でも
 *  RXが生きるはずだが実際には生きなかった——つまりHP_MODEMバンクの
 *  初期化は**単独では不十分**（必要条件の一つに過ぎない可能性が高い）。
 *  本候補は，候補1・2（esp_clk_init由来）と**併せて**初めてP2/P3相当の
 *  状態に近づく，という仮説の下で追加する（1つずつ加算する原則には
 *  反するが，候補1・2は個別にAP検出への効果ゼロと確定済みのため，
 *  「まだ試していない別のカテゴリの差分」を追加する本段は妥当な次段
 *  と判断——advisor指摘のとおり）。
 */
#define ESP32C5_R35_PMU_HP_ACTIVE_HP_REGULATOR0   0x600B0028U
#define ESP32C5_R35_PMU_HP_MODEM_DIG_POWER        0x600B0034U
#define ESP32C5_R35_PMU_HP_MODEM_ICG_HP_FUNC      0x600B0038U
#define ESP32C5_R35_PMU_HP_MODEM_ICG_HP_APB       0x600B003CU
#define ESP32C5_R35_PMU_HP_MODEM_ICG_MODEM        0x600B0040U
#define ESP32C5_R35_PMU_HP_MODEM_HP_SYS_CNTL      0x600B0044U
#define ESP32C5_R35_PMU_HP_MODEM_HP_CK_POWER      0x600B0048U
#define ESP32C5_R35_PMU_HP_MODEM_BACKUP           0x600B0050U
#define ESP32C5_R35_PMU_HP_MODEM_BACKUP_CLK       0x600B0054U
#define ESP32C5_R35_PMU_HP_MODEM_SYSCLK           0x600B0058U
#define ESP32C5_R35_PMU_HP_MODEM_HP_REGULATOR0    0x600B005CU

/*  stock実測値（本ラウンド，`stock_scan`実行中，RTSリセット後T=6.0s，
 *  独立2回のRTSリセット試行いずれも同一値）。  */
#define ESP32C5_R35_FIXED_HP_ACTIVE_HP_REGULATOR0 0xc004afd0U
#define ESP32C5_R35_FIXED_HP_MODEM_DIG_POWER      0x20000000U
#define ESP32C5_R35_FIXED_HP_MODEM_ICG_HP_FUNC    0x00100000U
#define ESP32C5_R35_FIXED_HP_MODEM_ICG_HP_APB     0x00000200U
#define ESP32C5_R35_FIXED_HP_MODEM_ICG_MODEM      0x40000000U
#define ESP32C5_R35_FIXED_HP_MODEM_HP_SYS_CNTL    0x31000000U
#define ESP32C5_R35_FIXED_HP_MODEM_HP_CK_POWER    0x70000000U
#define ESP32C5_R35_FIXED_HP_MODEM_BACKUP         0x00100010U
#define ESP32C5_R35_FIXED_HP_MODEM_BACKUP_CLK     0xffffffffU
#define ESP32C5_R35_FIXED_HP_MODEM_SYSCLK         0xb8000000U
#define ESP32C5_R35_FIXED_HP_MODEM_HP_REGULATOR0  0xc0048000U

/*  pmu_ll_imm_update_dig_icg_modem_code(true)／
 *  pmu_ll_imm_update_dig_icg_switch(true)相当のラッチパルス
 *  （実施34のesp32c5_r34_modem_icg_enable_min()と同一レジスタ，
 *  自己クリア型・重複発行は無害）。  */
#define ESP32C5_R35_PMU_IMM_MODEM_ICG      0x600B00DCU
#define ESP32C5_R35_PMU_IMM_SLEEP_SYSCLK   0x600B00D0U

static void
esp32c5_r35_pmu_hp_modem_bank_fixed(void)
{
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_ACTIVE_HP_REGULATOR0,
				ESP32C5_R35_FIXED_HP_ACTIVE_HP_REGULATOR0);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_DIG_POWER,
				ESP32C5_R35_FIXED_HP_MODEM_DIG_POWER);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_ICG_HP_FUNC,
				ESP32C5_R35_FIXED_HP_MODEM_ICG_HP_FUNC);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_ICG_HP_APB,
				ESP32C5_R35_FIXED_HP_MODEM_ICG_HP_APB);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_ICG_MODEM,
				ESP32C5_R35_FIXED_HP_MODEM_ICG_MODEM);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_HP_SYS_CNTL,
				ESP32C5_R35_FIXED_HP_MODEM_HP_SYS_CNTL);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_HP_CK_POWER,
				ESP32C5_R35_FIXED_HP_MODEM_HP_CK_POWER);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_BACKUP,
				ESP32C5_R35_FIXED_HP_MODEM_BACKUP);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_BACKUP_CLK,
				ESP32C5_R35_FIXED_HP_MODEM_BACKUP_CLK);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_SYSCLK,
				ESP32C5_R35_FIXED_HP_MODEM_SYSCLK);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_MODEM_HP_REGULATOR0,
				ESP32C5_R35_FIXED_HP_MODEM_HP_REGULATOR0);

	/*  ラッチパルス（自己クリア型，実施34と同一レジスタへの重複発行は
	 *  無害）。  */
	sil_wrw_mem((void *)ESP32C5_R35_PMU_IMM_MODEM_ICG, (1U << 31));
	sil_wrw_mem((void *)ESP32C5_R35_PMU_IMM_SLEEP_SYSCLK, (1U << 28));
}

/*
 *  候補4：PMU HP_SLEEPバンク（`pmu_hp_system_init()`のPMU_MODE_HP_SLEEP
 *  版，`pmu_init()`の完全な3モード反復のうち最後の1つ）。
 *
 *  背景：候補1〜3を実装しても実機でAP検出は変わらなかったため，
 *  advisor指摘に従い「クロスカーネル・ハンドオフ実験の再構築」を実施
 *  （docs/c5-bringup.md実施35）。stockホストからノーリセットで（本
 *  ビルド自身の候補1〜3処理は`HANDOFF_SKIP_CLOCK_INIT`でスキップして）
 *  ジャンプしたところ，**同一のASP3ゲストコードでAP検出（20/24/21/23
 *  APs，独立2回）に成功**——実施29の headjump 結果を再現した。この
 *  「動くソフトウェア（ハンドオフ）」対「動かないソフトウェア（候補
 *  1〜3込みcold boot）」という**同一ソフトウェア条件下**の広域レジスタ
 *  再比較で，候補3（HP_MODEMバンク）は正しく再現できていたことが確認
 *  できた一方，**HP_SLEEPバンク（`pmu_hp_system_init()`が反復する3
 *  モードのうち残る1つ）が未移植のまま**であることが判明した——
 *  `pmu_init()`は`PMU_MODE_HP_ACTIVE`・`PMU_MODE_HP_MODEM`・
 *  `PMU_MODE_HP_SLEEP`の3モードを同一ループで設定するが，候補3は
 *  HP_MODEMとHP_ACTIVE.HP_REGULATOR0のみを対象としており，HP_SLEEP
 *  バンク（0x600B0068〜0x600B0098，9レジスタ）を見落としていた。
 *
 *  Direct Bootは実際のsleepモードへ遷移しないため一見無関係に見えるが，
 *  `pmu_hp_system_init()`はモードに関わらず末尾で
 *  `pmu_ll_hp_set_sleep_protect_mode(..., PMU_SLEEP_PROTECT_HP_LP_SLEEP)`
 *  を呼ぶ等，3モード全体が一体のPMU設定として扱われる実装になっている
 *  ため，HP_SLEEPバンクが未設定のままだとPMU全体の設定が不完全な状態に
 *  留まっている可能性がある——候補3と同じくstock実測値を固定注入する
 *  形で追加する。
 */
#define ESP32C5_R35_PMU_HP_SLEEP_DIG_POWER      0x600B0068U
#define ESP32C5_R35_PMU_HP_SLEEP_ICG_HP_FUNC    0x600B006CU
#define ESP32C5_R35_PMU_HP_SLEEP_ICG_HP_APB     0x600B0070U
#define ESP32C5_R35_PMU_HP_SLEEP_HP_SYS_CNTL    0x600B0078U
#define ESP32C5_R35_PMU_HP_SLEEP_HP_CK_POWER    0x600B007CU
#define ESP32C5_R35_PMU_HP_SLEEP_BACKUP         0x600B0084U
#define ESP32C5_R35_PMU_HP_SLEEP_BACKUP_CLK     0x600B0088U
#define ESP32C5_R35_PMU_HP_SLEEP_SYSCLK         0x600B008CU
#define ESP32C5_R35_PMU_HP_SLEEP_HP_REGULATOR0  0x600B0090U
#define ESP32C5_R35_PMU_HP_SLEEP_XTAL           0x600B0098U

/*  stock実測値（実施35のクロスカーネル・ハンドオフ実験，成功後の
 *  ASP3ゲスト実行中，T=6.0s，独立2回のRTSリセット試行いずれも同一値）。  */
#define ESP32C5_R35_FIXED_HP_SLEEP_DIG_POWER     0x08200000U
#define ESP32C5_R35_FIXED_HP_SLEEP_ICG_HP_FUNC   0x00000000U
#define ESP32C5_R35_FIXED_HP_SLEEP_ICG_HP_APB    0x00000000U
#define ESP32C5_R35_FIXED_HP_SLEEP_HP_SYS_CNTL   0x39000000U
#define ESP32C5_R35_FIXED_HP_SLEEP_HP_CK_POWER   0x1c000000U
#define ESP32C5_R35_FIXED_HP_SLEEP_BACKUP        0x21100200U
#define ESP32C5_R35_FIXED_HP_SLEEP_BACKUP_CLK    0xffffffffU
#define ESP32C5_R35_FIXED_HP_SLEEP_SYSCLK        0x30000000U
#define ESP32C5_R35_FIXED_HP_SLEEP_HP_REGULATOR0 0x08048000U
#define ESP32C5_R35_FIXED_HP_SLEEP_XTAL          0x00000000U

static void
esp32c5_r35_pmu_hp_sleep_bank_fixed(void)
{
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_DIG_POWER,
				ESP32C5_R35_FIXED_HP_SLEEP_DIG_POWER);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_ICG_HP_FUNC,
				ESP32C5_R35_FIXED_HP_SLEEP_ICG_HP_FUNC);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_ICG_HP_APB,
				ESP32C5_R35_FIXED_HP_SLEEP_ICG_HP_APB);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_HP_SYS_CNTL,
				ESP32C5_R35_FIXED_HP_SLEEP_HP_SYS_CNTL);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_HP_CK_POWER,
				ESP32C5_R35_FIXED_HP_SLEEP_HP_CK_POWER);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_BACKUP,
				ESP32C5_R35_FIXED_HP_SLEEP_BACKUP);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_BACKUP_CLK,
				ESP32C5_R35_FIXED_HP_SLEEP_BACKUP_CLK);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_SYSCLK,
				ESP32C5_R35_FIXED_HP_SLEEP_SYSCLK);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_HP_REGULATOR0,
				ESP32C5_R35_FIXED_HP_SLEEP_HP_REGULATOR0);
	sil_wrw_mem((void *)ESP32C5_R35_PMU_HP_SLEEP_XTAL,
				ESP32C5_R35_FIXED_HP_SLEEP_XTAL);

	/*  ラッチパルス（候補3と同一，重複発行は無害）。  */
	sil_wrw_mem((void *)ESP32C5_R35_PMU_IMM_MODEM_ICG, (1U << 31));
	sil_wrw_mem((void *)ESP32C5_R35_PMU_IMM_SLEEP_SYSCLK, (1U << 28));
}

static void
esp32c5_r32_cpu_clock_switch(void)
{
	uint32_t	v;
	uint32_t	i;

	/*
	 *  BBPLL/BB-I2Cのアナログ電源をIMM（即時）レジスタで確実にtie-high
	 *  する（clk_ll_bbpll_enable()相当，自己クリア型・無条件で安全）。
	 */
	sil_wrw_mem((void *)ESP32C5_R32_PMU_IMM_HP_CK_POWER,
				ESP32C5_R32_PMU_TIE_HIGH_GLOBAL_BBPLL_ICG
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BB_I2C
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL_I2C
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL);

	/*
	 *  【実施34で変更】実施32はCAL_DONEビットが既に1（ROMが較正済み）の
	 *  場合は較正ループを完全にスキップしていたが，実施34のJTAG実測
	 *  （`r34_bbpll_read.py`）で「ROMは較正済みだが40MHz XTALプロファイル
	 *  を誤って使っており，実際には576MHz相当へロックしていた」ことが
	 *  判明した（div7_0=12・dr1=dr3=0を実測——48MHzプロファイルは
	 *  div7_0=10・dr1=dr3=1）。CAL_DONE=1は「較正が完了した」ことしか
	 *  示さず「正しい周波数へ較正された」ことは保証しないため，CAL_DONEの
	 *  値に関わらず無条件で`esp32c5_r34_bbpll_configure_480mhz()`
	 *  （regi2cによる48MHzプロファイルの明示書込み＋再較正）を実行する。
	 *  呼出し時点でCPUはまだXTAL直結（本関数はここでのみPLLソースへ
	 *  切り替える）ため，BBPLL再較正中の一時的な不定状態がCPU動作へ
	 *  影響することはない。
	 */
	esp32c5_r34_bbpll_configure_480mhz();

	/*
	 *  ディバイダ設定（rtc_clk_cpu_freq_to_pll_240_mhz(freq)相当）：
	 *  ソース切替え前に設定する（rtc_clk.cと同じ順序）。
	 */
	v = sil_rew_mem((void *)ESP32C5_R32_PCR_CPU_FREQ_CONF);
	v = (v & ~0xFFU) | (ESP32C5_R34_CPU_DIVIDER - 1U);
	sil_wrw_mem((void *)ESP32C5_R32_PCR_CPU_FREQ_CONF, v);

	v = sil_rew_mem((void *)ESP32C5_R32_PCR_AHB_FREQ_CONF);
	v = (v & ~0xFFU) | (ESP32C5_R34_AHB_DIVIDER - 1U);
	sil_wrw_mem((void *)ESP32C5_R32_PCR_AHB_FREQ_CONF, v);

	/*  ソース切替え：soc_clk_sel[17:16]=3(PLL_F240M)  */
	v = sil_rew_mem((void *)ESP32C5_R32_PCR_SYSCLK_CONF);
	v = (v & ~(0x3U << 16)) | (3U << 16);
	sil_wrw_mem((void *)ESP32C5_R32_PCR_SYSCLK_CONF, v);

	/*  bus_clk_update：R/W/WTC型自己クリアトリガ．HWが処理完了後に
	 *  自動で0へ戻るのを待つ（clk_ll_bus_update()相当，タイムアウト付き）  */
	sil_wrw_mem((void *)ESP32C5_R32_PCR_BUS_CLK_UPDATE, 1U);
	for (i = 0U; i < 2000000U; i++) {
		v = sil_rew_mem((void *)ESP32C5_R32_PCR_BUS_CLK_UPDATE);
		if ((v & 1U) == 0U) {
			break;
		}
	}
}

/*
 *  【実施34】BBPLLの正規較正（regi2c I2C_BBPLLブロック，block=0x66）
 *
 *  背景：docs/c5-bringup.md 実施32/33/34。実施32はROMが既に較正済み
 *  （I2C_ANA_MST_ANA_CONF0.CAL_DONE=1）だったBBPLLをそのまま流用したが，
 *  実測CPU周波数は理論値80MHzに対し実測96MHz（＝1.2倍）というズレを
 *  残していた。実施34の事前JTAG読み（advisor指示のcheap disambiguator，
 *  実施14の手動regi2cリプレイ手法を再利用，`r34_bbpll_read.py`）で，
 *  block 0x66のreg3(OC_DIV_7_0)=0x0C(12)・reg5(DR1[2:0]/DR3[6:4])=0x00
 *  （dr1=0,dr3=0）を実測——これはhal `clk_tree_ll.h`の
 *  `clk_ll_bbpll_set_config()`が定義する**40MHz XTALプロファイル**
 *  （div7_0=12,dr1=0,dr3=0）そのものである。C5の実際のXTALは48MHz
 *  （プロファイルはdiv7_0=10,dr1=1,dr3=1）——ROMは40MHz用の分周設定で
 *  480MHzのつもりで較正してしまい，実際には480×(48/40)=576MHz相当へ
 *  ロックしていたと判明した（576/480=1.2＝実測96MHz/理論80MHzの比と
 *  正確に一致，advisorの予測どおり）。
 *
 *  本関数は`rtc_clk_bbpll_configure(48MHz, 480MHz)`
 *  （`hal/esp_hw_support/port/esp32c5/rtc_clk.c`）相当を，regi2cの
 *  実体（`hal/esp_hal_regi2c/esp32c5/regi2c_impl.c`の
 *  `_regi2c_impl_write`/`_regi2c_impl_write_mask`——実体はGPIO I2Cでは
 *  なくMMIOレジスタ`I2C_ANA_MST_I2C0/1_CTRL_REG`経由の疑似バス転送．
 *  実施14で確認済みのプロトコル）を`sil_*w_mem`で手動再現する形で
 *  移植した。ROM regi2c関数へのリンク（実施32のコメントで検討した
 *  代替案）は，B-0/B-1（WiFi無効）ビルドでROM ldを含まないため使えず，
 *  実施14の手動プロトコル（MMIO直叩き）はビルド形態に依存しないため
 *  こちらを採用した。
 *
 *  安全設計：
 *  - 全てのbusy-wait（regi2cトランザクション完了・CAL_DONE成立）に
 *    タイムアウト上限を設ける（無条件whileは採用しない，実施32と同じ
 *    方針）。
 *  - regi2cマスタクロック前提（`MODEM_LPCON_CLK_CONF` bit2
 *    `clk_i2c_mst_en`・`MODEM_SYSCON_CLK_CONF` bit12
 *    `clk_i2c_mst_sel_160m`，実施14で確認済みの前提条件）は，本関数
 *    自身が冒頭で（他のどの初期化より先に，WiFi有無に関わらず）
 *    OR書込みで有効化する——`ANALOG_CLOCK_ENABLE()`はBOOTLOADER_BUILD
 *    では恒久有効化戦略（no-op，`bootloader_hardware_init()`が起動時に
 *    一度有効化してそのまま）であり，ASP3のDirect Bootも同じ戦略
 *    （一度有効化して恒久的に維持）を採る。
 *  - `clk_ll_bbpll_calibration_start()`→`clk_ll_bbpll_set_config()`
 *    →`while(!is_done)`→`calibration_stop()`の順序をそのまま保持する
 *    （rtc_clk.cの`rtc_clk_bbpll_configure()`と同一順序——calibration_
 *    startが先，regi2cのdiv/dr/lref/href書込みはcalibration実行中に
 *    行う）。
 */
#define ESP32C5_R34_I2C_ANA_MST_BASE        0x600AF800U
#define ESP32C5_R34_I2C_ANA_MST_I2C0_CTRL   (ESP32C5_R34_I2C_ANA_MST_BASE + 0x00U)
#define ESP32C5_R34_I2C_ANA_MST_I2C1_CTRL   (ESP32C5_R34_I2C_ANA_MST_BASE + 0x04U)
#define ESP32C5_R34_I2C_ANA_MST_ANA_CONF1   (ESP32C5_R34_I2C_ANA_MST_BASE + 0x1CU)
#define ESP32C5_R34_I2C_ANA_MST_ANA_CONF2   (ESP32C5_R34_I2C_ANA_MST_BASE + 0x20U)
#define ESP32C5_R34_REGI2C_BUSY             (1U << 25)
#define ESP32C5_R34_REGI2C_WR_CNTL          (1U << 24)

#define ESP32C5_R34_MODEM_LPCON_CLK_CONF    0x600AF018U
#define ESP32C5_R34_MODEM_LPCON_CLK_I2C_MST_EN  (1U << 2)
#define ESP32C5_R34_MODEM_SYSCON_CLK_CONF   0x600A9C04U  /* ESP32C5_R31_...と同一アドレス。
														   * TOPPERS_ESP32C5_WIFI非依存で
														   * 使うため本関数専用に再定義
														   * （実施31の定義はWiFi限定
														   * ガード内のため不可視）。 */

#define ESP32C5_R34_BBPLL_BLOCK              0x66U
#define ESP32C5_R34_BBPLL_MST_SEL_BIT        (1U << 9)   /* ANA_CONF2内，REGI2C_BBPLL_MST_SEL */
#define ESP32C5_R34_BBPLL_RD_MASK            (~(1U << 7) & 0x00FFFFFFU)  /* ANA_CONF1へ書く値 */

#define ESP32C5_R34_BBPLL_REG_OC_REF_DIV     2U  /* OC_DCHGP[6:4]|OC_REF_DIV[3:0]，full byte */
#define ESP32C5_R34_BBPLL_REG_OC_DIV_7_0     3U  /* full byte */
#define ESP32C5_R34_BBPLL_REG_DR             5U  /* OC_DR3[6:4]/OC_DR1[2:0]，masked */
#define ESP32C5_R34_BBPLL_REG_DHLREF         6U  /* OC_DLREF_SEL[7:6]/OC_DHREF_SEL[5:4]，masked */

/*
 *  【実施34・決定実験で発見】I2C_ANA_MST（regi2cマスタ）はMODEM_LPCON.
 *  clk_i2c_mst_enを立てるだけでは実際には動かない——ANA_CONF0/1/2の
 *  読み書きが恒久的に0x00000000のまま（regi2c_write()のbusy waitは
 *  「即座にクリアされる」ため一見成功して見えるが，実際にはANA_MST
 *  ブロック自体に機能クロックが供給されていないため何も起きていない）
 *  ことをJTAG実測で確認した。実施13が`esp_shim_modem_icg_init()`
 *  （`wifi/esp_wifi_adapter.c`）で発見・修正した「WIFIBBクロックの
 *  ICGゲート」と同じ機構——PMU HP_ACTIVEのicg_modem.code（0x600B000C
 *  bits[31:30]）がDirect Boot起動直後は0のままで，MODEM_SYSCON/
 *  MODEM_LPCONのCLK_CONF_POWER_ST（ICGコード別に「どのコード値で
 *  クロックを通すか」を指定するビットマップ）にはコード0が含まれない
 *  ため，機能クロックがゲートされたまま——が，I2C_MASTER ICGドメイン
 *  （regi2cマスタ自体）にもそのまま適用されると判明した。
 *  `esp_shim_modem_icg_init()`はWiFiビルドの`esp_wifi_init()`経路
 *  でのみ・本関数（BBPLL較正）よりずっと後に実行されるため，
 *  `hardware_init_hook()`の最初期で動くBBPLL較正には間に合わない。
 *  本関数はその最小部分集合（I2C_MASTER／MODEM_APB／LP_APB ICG
 *  ドメインのみ，WIFI/BT/FE/ZB等は対象外）を，WiFi有無に関わらず
 *  BBPLL較正の直前に独立して適用する。
 */
#define ESP32C5_R34_PMU_HP_ACTIVE_ICG_MODEM   0x600B000CU  /* icg_modem.code，bits[31:30] */
#define ESP32C5_R34_PMU_IMM_MODEM_ICG         0x600B00DCU  /* update_dig_icg_modem_en，bit31 */
#define ESP32C5_R34_PMU_IMM_SLEEP_SYSCLK      0x600B00D0U  /* update_dig_icg_switch，bit28 */
#define ESP32C5_R34_MODEM_SYSCON_CLK_CONF_POWER_ST  0x600A9C0CU  /* CLK_MODEM_APB_ST_MAP，bits[31:28] */
#define ESP32C5_R34_MODEM_LPCON_CLK_CONF_POWER_ST   0x600AF020U  /* CLK_LP_APB_ST_MAP[31:28]/CLK_I2C_MST_ST_MAP[27:24] */

static void
esp32c5_r34_modem_icg_enable_min(void)
{
	uint32_t	v;

	/*  icg_modem.code = 2（esp_shim_modem_icg_init()と同じ値）  */
	v = sil_rew_mem((void *)ESP32C5_R34_PMU_HP_ACTIVE_ICG_MODEM);
	v = (v & ~(0x3U << 30)) | (2U << 30);
	sil_wrw_mem((void *)ESP32C5_R34_PMU_HP_ACTIVE_ICG_MODEM, v);

	/*  ST_MAP（コード値2＝BIT(2)=0x4のときクロックを通す）を
	 *  MODEM_APB・LP_APB・I2C_MASTERの3ドメインへ設定  */
	v = sil_rew_mem((void *)ESP32C5_R34_MODEM_SYSCON_CLK_CONF_POWER_ST);
	v = (v & ~(0xFU << 28)) | (0x4U << 28);
	sil_wrw_mem((void *)ESP32C5_R34_MODEM_SYSCON_CLK_CONF_POWER_ST, v);

	v = sil_rew_mem((void *)ESP32C5_R34_MODEM_LPCON_CLK_CONF_POWER_ST);
	v = (v & ~(0xFU << 28)) | (0x4U << 28);			/* LP_APB_ST_MAP */
	v = (v & ~(0xFU << 24)) | (0x4U << 24);			/* I2C_MST_ST_MAP */
	sil_wrw_mem((void *)ESP32C5_R34_MODEM_LPCON_CLK_CONF_POWER_ST, v);

	/*  即時反映パルス2本（両方必要，実施13で確認済み）  */
	sil_wrw_mem((void *)ESP32C5_R34_PMU_IMM_MODEM_ICG, (1U << 31));
	sil_wrw_mem((void *)ESP32C5_R34_PMU_IMM_SLEEP_SYSCLK, (1U << 28));
}

static uint32_t
esp32c5_r34_regi2c_select_ctrl(void)
{
	uint32_t	conf2;
	uint32_t	i2c_sel;

	conf2 = sil_rew_mem((void *)ESP32C5_R34_I2C_ANA_MST_ANA_CONF2);
	i2c_sel = (conf2 & ESP32C5_R34_BBPLL_MST_SEL_BIT) ? 0U : 1U;
	sil_wrw_mem((void *)ESP32C5_R34_I2C_ANA_MST_ANA_CONF1,
				ESP32C5_R34_BBPLL_RD_MASK);
	return (i2c_sel == 0U) ? ESP32C5_R34_I2C_ANA_MST_I2C0_CTRL
							: ESP32C5_R34_I2C_ANA_MST_I2C1_CTRL;
}

static void
esp32c5_r34_regi2c_wait_idle(uint32_t ctrl_reg)
{
	uint32_t	i;

	for (i = 0U; i < 200000U; i++) {
		if ((sil_rew_mem((void *)ctrl_reg) & ESP32C5_R34_REGI2C_BUSY) == 0U) {
			break;
		}
	}
}

/*
 *  regi2c_impl_write()相当：block内reg_addへ1byte全体を書く
 *  （実施14手動プロトコル・`_regi2c_impl_write()`と同一の
 *  ビットレイアウト：[7:0]=slave_id(block)，[15:8]=reg_addr，
 *  [24]=WR_CNTL，[23:16]=data）。
 */
static void
esp32c5_r34_regi2c_write(uint32_t block, uint32_t reg_add, uint32_t data)
{
	uint32_t	ctrl_reg;
	uint32_t	v;

	ctrl_reg = esp32c5_r34_regi2c_select_ctrl();
	esp32c5_r34_regi2c_wait_idle(ctrl_reg);
	v = (block & 0xFFU) | ((reg_add & 0xFFU) << 8)
		| ESP32C5_R34_REGI2C_WR_CNTL | ((data & 0xFFU) << 16);
	sil_wrw_mem((void *)ctrl_reg, v);
	esp32c5_r34_regi2c_wait_idle(ctrl_reg);
}

/*
 *  regi2c_impl_write_mask()相当：block内reg_addのbits[msb:lsb]だけを
 *  read-modify-writeする。
 */
static void
esp32c5_r34_regi2c_write_mask(uint32_t block, uint32_t reg_add,
							   uint32_t msb, uint32_t lsb, uint32_t data)
{
	uint32_t	ctrl_reg;
	uint32_t	v;
	uint32_t	cur;
	uint32_t	width;
	uint32_t	mask;

	ctrl_reg = esp32c5_r34_regi2c_select_ctrl();
	esp32c5_r34_regi2c_wait_idle(ctrl_reg);
	v = (block & 0xFFU) | ((reg_add & 0xFFU) << 8);
	sil_wrw_mem((void *)ctrl_reg, v);
	esp32c5_r34_regi2c_wait_idle(ctrl_reg);
	cur = (sil_rew_mem((void *)ctrl_reg) >> 16) & 0xFFU;

	width = msb - lsb + 1U;
	mask = (width >= 8U) ? 0xFFU : ((1U << width) - 1U);
	cur &= ~(mask << lsb);
	cur |= (data & mask) << lsb;

	v = (block & 0xFFU) | ((reg_add & 0xFFU) << 8)
		| ESP32C5_R34_REGI2C_WR_CNTL | ((cur & 0xFFU) << 16);
	sil_wrw_mem((void *)ctrl_reg, v);
	esp32c5_r34_regi2c_wait_idle(ctrl_reg);
}

/*
 *  BBPLLをXTAL=48MHzプロファイルで再構成し，480MHzへ較正し直す
 *  （`rtc_clk_bbpll_configure(SOC_XTAL_FREQ_48M, CLK_LL_PLL_480M_FREQ_
 *  MHZ)`相当）。呼出し前提：CPUはまだXTAL直結（PLL未使用）——このPLLの
 *  出力はCPUクロックとして未参照のため，較正中の一時的な不定状態が
 *  CPU動作へ影響しない。
 */
static void
esp32c5_r34_bbpll_configure_480mhz(void)
{
	uint32_t	v;
	uint32_t	i;
	bool_t		done;

	/*
	 *  【実施34】I2C_MASTER/MODEM_APB/LP_APB ICGドメインの最小有効化
	 *  （esp_shim_modem_icg_init()の部分集合，上記コメント参照）。
	 *  regi2cマスタクロック前提の有効化より先に行う——ICGゲートが
	 *  閉じたままだと，直後のclk_i2c_mst_en書込み自体は「効いている
	 *  ように見える」が（MODEM_LPCON_CLK_CONFの静的configビットは常に
	 *  書換え可能），実際のANA_MST機能クロックは供給されないままで
	 *  regi2cトランザクションが無反応・無効なまま進んでしまう。
	 */
	esp32c5_r34_modem_icg_enable_min();

	/*
	 *  regi2cマスタクロック前提の有効化（ANALOG_CLOCK_ENABLE()相当，
	 *  bootloaderと同じ「一度有効化して恒久維持」戦略）。
	 *  MODEM_SYSCON_CLK_CONF.clk_i2c_mst_sel_160mは実施31の
	 *  esp32c5_r31_bootloader_random_cycle()も同じビットを立てるが，
	 *  あちらはWiFi限定・本関数より後に実行されるため，ここで独立に
	 *  先立って設定する（冪等，副作用なし）。
	 */
	v = sil_rew_mem((void *)ESP32C5_R34_MODEM_LPCON_CLK_CONF);
	sil_wrw_mem((void *)ESP32C5_R34_MODEM_LPCON_CLK_CONF,
				v | ESP32C5_R34_MODEM_LPCON_CLK_I2C_MST_EN);
	v = sil_rew_mem((void *)ESP32C5_R34_MODEM_SYSCON_CLK_CONF);
	sil_wrw_mem((void *)ESP32C5_R34_MODEM_SYSCON_CLK_CONF, v | (1U << 12));

	/*
	 *  【実施34・decision experiment】clk_i2c_mst_enをORで立てた直後に
	 *  ANA_MSTブロック（I2C_ANA_MST_ANA_CONF0等）へ即アクセスすると，
	 *  クロックドメインが実際に立ち上がる前の書込みが無視される
	 *  （実測：ANA_CONF0が恒久0x00000000のまま＝calibration_stop()の
	 *  最終書込みすら反映されない）ことをtest_porting非WiFiビルドの
	 *  JTAG読みで確認した——WiFiビルドでこれまで問題が出なかったのは，
	 *  `wifi_clock_enable_wrapper()`がこのビットを立ててから実際に
	 *  regi2cを使うまでに他の初期化処理を挟むため，結果的に十分な
	 *  settle時間が空いていたためと考えられる。ここでは`sil_*`のみに
	 *  依存する短いbusy spinでクロックドメイン立上りのcushionを設ける
	 *  （正確な時間は不明だが，数百〜数千サイクル程度あれば十分と
	 *  みて安全側に大きめの値を採る）。
	 */
	for (i = 0U; i < 10000U; i++) {
		sil_rew_mem((void *)ESP32C5_R34_MODEM_LPCON_CLK_CONF);
	}

	/*  BBPLL/BB-I2Cアナログ電源のtie-high（clk_ll_bbpll_enable()相当，
	 *  実施32から流用）。  */
	sil_wrw_mem((void *)ESP32C5_R32_PMU_IMM_HP_CK_POWER,
				ESP32C5_R32_PMU_TIE_HIGH_GLOBAL_BBPLL_ICG
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BB_I2C
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL_I2C
				| ESP32C5_R32_PMU_TIE_HIGH_XPD_BBPLL);

	/*  clk_ll_bbpll_calibration_start()：CONF0のSTOP_FORCE_HIGHを
	 *  クリア・STOP_FORCE_LOWをセット（較正回路を「実行中」へ）。  */
	v = sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
	sil_wrw_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0, v & ~(1U << 2));
	v = sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
	sil_wrw_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0, v | (1U << 3));

	/*  clk_ll_bbpll_set_config(480, 48)：XTAL=48MHzプロファイル
	 *  （div_ref=1,div7_0=10,dr1=1,dr3=1,dchgp=5,href=3,lref=1）を
	 *  block 0x66へ書込む。  */
	esp32c5_r34_regi2c_write(ESP32C5_R34_BBPLL_BLOCK,
							  ESP32C5_R34_BBPLL_REG_OC_REF_DIV,
							  (5U << 4) | 1U);			/* dchgp=5,div_ref=1 -> 0x51 */
	esp32c5_r34_regi2c_write(ESP32C5_R34_BBPLL_BLOCK,
							  ESP32C5_R34_BBPLL_REG_OC_DIV_7_0,
							  10U);						/* div7_0=10 (48MHzプロファイル) */
	esp32c5_r34_regi2c_write_mask(ESP32C5_R34_BBPLL_BLOCK,
								   ESP32C5_R34_BBPLL_REG_DR, 2U, 0U, 1U);	/* dr1=1 */
	esp32c5_r34_regi2c_write_mask(ESP32C5_R34_BBPLL_BLOCK,
								   ESP32C5_R34_BBPLL_REG_DR, 6U, 4U, 1U);	/* dr3=1 */
	esp32c5_r34_regi2c_write_mask(ESP32C5_R34_BBPLL_BLOCK,
								   ESP32C5_R34_BBPLL_REG_DHLREF, 7U, 6U, 1U);	/* lref=1 */
	esp32c5_r34_regi2c_write_mask(ESP32C5_R34_BBPLL_BLOCK,
								   ESP32C5_R34_BBPLL_REG_DHLREF, 5U, 4U, 3U);	/* href=3 */

	/*  while(!clk_ll_bbpll_calibration_is_done())：タイムアウト付き
	 *  （無条件whileは採用しない，安全設計方針）。  */
	done = false;
	for (i = 0U; i < 2000000U; i++) {
		v = sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
		if ((v & ESP32C5_R32_I2C_MST_BBPLL_CAL_DONE) != 0U) {
			done = true;
			break;
		}
	}
	(void)done;	/* タイムアウトしても後続のcalibration_stopは実行する
				 * （rtc_clk.cは無条件待ちだが，ASP3はここで打ち切り，
				 * 較正回路を安全な停止状態へ戻すことを優先する）。 */

	/*  esp_rom_delay_us(10)相当："wait for true stop"のcushion。
	 *  ROM未リンクのB-0/B-1でも使えるよう，sil_*のみに依存する短い
	 *  busy spin（正確な10usである必要はなく，桁が合っていれば十分）
	 *  で代替する。  */
	for (i = 0U; i < 5000U; i++) {
		sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
	}

	/*  clk_ll_bbpll_calibration_stop()：STOP_FORCE_LOWをクリア・
	 *  STOP_FORCE_HIGHをセット（較正回路を確定的に停止状態へ）。  */
	v = sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
	sil_wrw_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0, v & ~(1U << 3));
	v = sil_rew_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0);
	sil_wrw_mem((void *)ESP32C5_R32_I2C_ANA_MST_ANA_CONF0, v | (1U << 2));
}

/*
 *  【実施33で分離・export】全ウォッチドッグ（MWDT0/1・RTC WDT・
 *  スーパーWDT）を無効化する．
 *
 *  背景：docs/c5-bringup.md 実施33。hardware_init_hook()で起動最初期に
 *  disableしているにも関わらず，esp_wifi_scan_start()到達前後で
 *  約数秒周期のSUPER_WDT_RESET（rst:0x12）が発生し続ける現象を発見した。
 *  JTAGでLP_WDT_SWD_CONFIG_REG(0x600B1C1C)のDISABLEビット(bit30)を
 *  ハング中に繰り返し読むと終始1（無効化済みに見える）のに実機は
 *  リセットし続けるという矛盾から出発し，**真因はSWD_WPROTECT解錠キー
 *  の誤り**（`ESP32C5_LP_WDT_SWD_WKEY`が旧値0x8F1D312A——hal
 *  `lp_wdt_reg.h`のLP_WDT_SWD_WKEYフィールドの正しい値は0x50D83AA1，
 *  TIMG/RWDTと共通）と確定した。誤ったキーでは`SWD_WPROTECT`の解錠に
 *  失敗し，続く`SWD_CONFIG`へのDISABLEビット書込みが書込み保護で
 *  無視される（＝実行時は一度も真に無効化されていなかった）。DISABLE
 *  ビットがJTAG読出しで1に見えたのは，JTAG haltがWDTのカウント自体を
 *  一時停止させる副作用（r21ハーネスのSWD無効化burstと同型）により，
 *  無効化されていない実行時の挙動と、halt中の読出し値が一致しない
 *  ためだった（advisor指摘どおり）。esp32c5.hのキー定数修正により
 *  hardware_init_hook()の一度きりのdisableで完全に解消することを確認
 *  済み（本関数の追加のFEED書込みは，鍵が万一再び誤っていた場合の
 *  保険として残す．無害＝WT型自己完結）。
 */
void
esp32c5_reassert_wdt_disable(void)
{
	esp32c5_disable_mwdt(ESP32C5_TIMG0_BASE);
	esp32c5_disable_mwdt(ESP32C5_TIMG1_BASE);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(ESP32C5_TIMG0_BASE),
				ESP32C5_TIMG_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTFEED(ESP32C5_TIMG0_BASE), 1U);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(ESP32C5_TIMG0_BASE), 0U);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(ESP32C5_TIMG1_BASE),
				ESP32C5_TIMG_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTFEED(ESP32C5_TIMG1_BASE), 1U);
	sil_wrw_mem((void *)ESP32C5_TIMG_WDTWPROTECT(ESP32C5_TIMG1_BASE), 0U);

	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT,
				ESP32C5_RTC_CNTL_WDT_WKEY);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTCONFIG0, 0U);
	sil_wrw_mem((void *)ESP32C5_LP_WDT_FEED, 1U);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_WDTWPROTECT, 0U);

	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT,
				ESP32C5_RTC_CNTL_SWD_WKEY);
	sil_orw((void *)ESP32C5_RTC_CNTL_SWD_CONF,
			ESP32C5_RTC_CNTL_SWD_AUTO_FEED_EN | (1U << 30));
	sil_orw((void *)ESP32C5_LP_WDT_SWD_CONFIG, ESP32C5_LP_WDT_SWD_FEED_BIT);
	sil_wrw_mem((void *)ESP32C5_RTC_CNTL_SWD_WPROTECT, 0U);
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
	esp32c5_reassert_wdt_disable();

	/*
	 *  CPUクロックの切替え
	 *
	 *  【実施32で確定・実装】docs/c5-bringup.md 実施32。「C6はROMが既に
	 *  適切に設定している」という旧来の仮定（下記，取り消し線的に残す）
	 *  はC5では成立しないとJTAG実測で判明した——ASP3のDirect Bootは
	 *  PCR_SYSCLK_CONF.soc_clk_sel=XTAL(0)のまま一度も切り替わらず，
	 *  実CPUクロックはXTAL48MHz直結（実測47.96〜48.00MHz，旧「192MHz
	 *  実機確定」は反証）。stockのbootloader_clock_configure()相当の
	 *  切替え（XTAL→PLL_F240M÷3，レジスタ設定はbootloader_clock_
	 *  configure(80MHz)相当）を明示的に実行する。実測CPUクロックは
	 *  96MHz（esp32c5.hのCORE_CLK_MHZコメント参照——理論値80MHzとの
	 *  不一致はBBPLL実周波数の推定違いによるとみられる，実測を正とする）。
	 *  CORE_CLK_MHZ/SIL_DLY_TIM1/TIM2もesp32c5.hで96MHz実測基準へ訂正済み。
	 *  esp_rom_set_cpu_ticks_per_us()より前に実行し，以降のus単位の
	 *  ROM遅延ループ（esp_rom_delay_us等）が正しい実クロックで動作する
	 *  ようにする。
	 *
	 *  （旧コメント，参考として残す）：C6はROMブートローダが起動時点で
	 *  既にPCRをSPLL÷3÷1＝160MHzへ設定済みと判明しソフトウェアでの
	 *  追加操作は不要だった。
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
	 *
	 *  esp32c5_r32_cpu_clock_switch()自体はsil_*w_memのみに依存し
	 *  ROMシンボルを一切参照しないため，WiFi無効ビルド（B-0/B-1，
	 *  test_porting等）でもリンク可能・実行して問題ない。むしろ
	 *  CORE_CLK_MHZ（core_syssvc.hのget_utm()等で使用）が全ビルド共通で
	 *  96MHz（実測値）を前提とするよう訂正した以上，実クロックも全ビルド
	 *  でこの設定に揃えておく方が一貫性がある。WiFi無効ビルドかどうかに関わらず
	 *  無条件で呼び出す。
	 */
	/*
	 *  【実施32・切り分け実験の結果を踏まえ本採用】advisorの指摘に従い，
	 *  クロック切替えとticks_per_us訂正を分離した対照実験を実施した：
	 *  クロック切替えを止めXTAL48MHzのまま`esp_rom_set_cpu_ticks_per_us
	 *  (48)`のみ正しく設定した場合，較正は独立2回のRTSリセット試行＋
	 *  各キャプチャ内の複数自然リブートすべてでFAIL（raw_adc恒久ゼロ）
	 *  だった（`r32_control_trial1/2.log`）——delay較正の正しさだけでは
	 *  不十分で，**PLLへのクロック切替え自体が較正キー(A)の必要条件**
	 *  であることが確定した（docs/c5-bringup.md 実施32 10節）。
	 */

	/*
	 *  【実施35】クロスカーネル・ハンドオフ実験（実施26/27/29系，本
	 *  ラウンド＝実施35で再構築）では，ホスト（stock）が既にCPU/BBPLL/
	 *  PMUクロック状態を正しく確立した後にリセット無しでこのゲストへ
	 *  制御が渡る。その状態で下記のクロック/PMU書換え系（候補1〜3＋
	 *  実施32/34のCPUクロック切替え・BBPLL再較正）を無条件実行すると，
	 *  実行中のCPUクロック源を生きたまま再切替え・BBPLLを再較正する
	 *  ことになり，ハンドオフ直後の実行中クロックにグリッチ/ハング
	 *  リスクがある（実施26のHANDOFF_SKIP_WIFI_INITと同種の配慮）。
	 *  `HANDOFF_SKIP_CLOCK_INIT`定義時はこれらをスキップし，ホストが
	 *  確立した状態をそのまま引き継ぐ。通常のDirect Boot（cold boot）
	 *  では定義しないため，実施32〜35の効果はそのまま有効。
	 */
#ifndef HANDOFF_SKIP_CLOCK_INIT
	/*
	 *  【実施35・出荷時ガード】候補2〜4はいずれも実機2/2でAP検出に
	 *  無効と確定した（causal refute，docs/c5-bringup.md実施35 3〜4・
	 *  6・9節）。候補3・4は特に，stock実測値をそのまま定数注入して
	 *  おり，`pmu_hp_system_init()`実体（`pmu_init.c:175`）を見ると
	 *  HP_MODEM/HP_SLEEPの`regulator0.dbias`はeFuse由来の基板固有電圧
	 *  トリム（`get_act_hp_dbias()`）であるべき値——HP_ACTIVE側では
	 *  PVT自動dbiasループがこの固定注入を実測でライブに上書きする
	 *  ことを確認済み（6節）だが，HP_MODEM/HP_SLEEP側で同種の上書きが
	 *  実際に起きるかは**未確認**（advisor指摘）。C5#1では実害なしと
	 *  確認済みだが，**他基板でこのコードをそのまま「出荷」すると
	 *  C5#1のトリム値を焼き付ける副作用の恐れがある**——refuteされた
	 *  実験コードを既定で無効化し，`ESP32C5_R35_ENABLE_REFUTED_
	 *  CANDIDATES`定義時のみ再現実験用に有効化できるようにする
	 *  （関数自体は次段の参考のため残置，デフォルトでは未実行）。
	 *  候補1（RTC_FAST_CLK源をRC_FASTへ）はstockの実際の選択と一致する
	 *  正しい修正でありrefuteの対象外のため，常時有効のまま残す。
	 */
#ifdef ESP32C5_R35_ENABLE_REFUTED_CANDIDATES
	/*
	 *  【実施35候補3】PMU HP_MODEMバンク（＋HP_ACTIVE.HP_REGULATOR0）の
	 *  固定値注入。stockの実行順序（`sys_rtc_init()`=`esp_rtc_init()`=
	 *  `pmu_init()`は`system_early_init()`＝esp_clk_init等より前）に
	 *  合わせ，候補1・2よりも前に置く。
	 */
	esp32c5_r35_pmu_hp_modem_bank_fixed();

	/*
	 *  【実施35候補4】PMU HP_SLEEPバンクの固定値注入。候補3と同じ
	 *  `pmu_hp_system_init()`呼出しが設定する残る1モード分。
	 */
	esp32c5_r35_pmu_hp_sleep_bank_fixed();
#endif /* ESP32C5_R35_ENABLE_REFUTED_CANDIDATES */

	/*
	 *  【実施35候補1】RTC_FAST_CLK源をRC_FASTへ切替え（esp_clk_init()の
	 *  rtc_clk_fast_src_set()相当）。stockのesp_clk_init()内での実行
	 *  順序（RTC_FAST/SLOW_CLK設定はCPU周波数切替えより前）に合わせ，
	 *  esp32c5_r32_cpu_clock_switch()の前に置く。stockの実際の選択
	 *  （RC_FAST）と一致する正しい修正のため，refuteの対象外として
	 *  常時有効。
	 */
	esp32c5_r35_rtc_fast_clk_select();

#ifdef ESP32C5_R35_ENABLE_REFUTED_CANDIDATES
	/*
	 *  【実施35候補2・因果確認段階】RTC_SLOW_CLK_CAL_REGへstock実測値を
	 *  固定注入する（esp_clk_slowclk_cal_set()相当，正式なrtc_clk_cal()
	 *  実測は未実装）。候補1（直上）は単独ではAP検出に効果がなかった
	 *  ため，候補2を追加する。診断専用の暫定注入であり，board/temp
	 *  依存の実測値をそのまま焼き付けるため既定では無効。
	 */
	esp32c5_r35_rtc_slowclk_cal_set_fixed();
#endif /* ESP32C5_R35_ENABLE_REFUTED_CANDIDATES */

	esp32c5_r32_cpu_clock_switch();
#endif /* !HANDOFF_SKIP_CLOCK_INIT */

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
