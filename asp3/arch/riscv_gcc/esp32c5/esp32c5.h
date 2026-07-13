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
 *  ESP32-C5のハードウェア資源の定義
 *
 *  esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/esp32c6.h）からのコピー・
 *  C5対応．レジスタアドレスはesp-hal-3rdparty（asp3_esp_idf/hal
 *  submodule，components/soc/esp32c5/register/soc/）から採用．
 *  経緯・設計判断はdocs/c5-port-design.md（本リポジトリ）参照。
 *
 *  ■ 割込みコントローラについて（C6との最大の違い）
 *  ESP32-C6は独自"PLIC"命名（実体はC3のINTMTX方式）だったが，ESP32-C5は
 *  soc_caps.hで SOC_INT_CLIC_SUPPORTED=1 を定義する**標準RISC-V CLIC**を
 *  採用する（PLIC定義は本ファイルには存在しない）。CPU側制御は
 *  CLIC_INT_CTRL_REG(i)（i=CLIC内部番号．IP/IE/ATTR(TRIG/SHV/MODE)/CTL
 *  (優先度)を1ワードに格納）・優先度マスクはCSR化されたmintthresh
 *  （0x347）で行う。詳細な設計判断はclic_kernel_impl.h冒頭コメント参照。
 *  ソース→CPU割込み線のルーティング（INTMTX，DR_REG_INTMTX_BASE=
 *  0x60010000＝ソースnのMAPレジスタ=BASE+4n）自体はC6・C3と同一方式で
 *  存続する（hal: soc/esp32c5/register/soc/reg_base.hで確認済み。
 *  DR_REG_INTERRUPT_CORE0_BASEはDR_REG_INTMTX_BASEのエイリアス）。
 *
 *  ■ ASP3論理INTNO(1〜31)とCLIC外部番号(16〜47)の対応
 *  clic_kernel_impl.hに変換を閉じ込め，target層・アプリからは従来通り
 *  INTNO=1〜31のみを見せる（docs/c5-port-design.md §8.2の設計判断）。
 */

#ifndef TOPPERS_ESP32C5_H
#define TOPPERS_ESP32C5_H

/*
 *  メモリマップ（Direct Bootでの配置．リンカスクリプトと一致させること）
 *
 *  C5はSOC_IROM_LOW=0x42000000を確認済み（soc/esp32c5/include/soc/soc.h）。
 *  SOC_DROM_LOWがIROMと同一かどうかは本移植時点で断定できなかった
 *  （docs/c5-port-design.md §3・§6.2の【要確認】項目）。分岐A（C6型・
 *  単一FLASH領域）を第一候補として採用し，esp32c5.ldも単一FLASH領域で
 *  構成する。実機投入時にSOC_DROM_LOWの実値を再確認し，分離型なら
 *  リンカスクリプトをC3方式（3領域）へ作り直す必要がある。
 *  【実機確認待ち】docs/c5-port-design.md §8.1 8番
 */
#define ESP32C5_IROM_BASE       0x42000000
#define ESP32C5_DROM_BASE       0x42000000  /* 【未確認】IROMと同一と仮定（分岐A） */
#define ESP32C5_DRAM_BASE       0x40800000
#define ESP32C5_DRAM_SIZE       0x00060000  /* 384KB（C6の512KBより少ない．
                                             * soc/esp32c5/include/soc/soc.h：
                                             * SOC_IRAM_HIGH-SOC_IRAM_LOW=
                                             * 0x40860000-0x40800000で確認済み）*/

/*
 *  CPUクロック周波数（MHz．core_syssvc.hの性能カウンタ換算等で使用）
 *
 *  【実機確認待ち】docs/c5-port-design.md §8.1 3番。C6はROMブート
 *  ローダが起動時点で既に160MHz(SPLL÷3÷1)に設定済みだったが，C5の
 *  PCR相当レジスタの実際の初期値は実機ダンプでの確認が必要（C5の
 *  soc_cpu_clk_src_tはPLL_F160M/PLL_F240Mを露出しており240MHz運用の
 *  可能性も示唆される）。ここではC6と同じ160MHzを暫定値として仮置き
 *  する（起動できない・タイミングが合わない場合はまずこの値を疑うこと）。
 */
/*
 *  【実施32で訂正＝96MHz】実施03は「CPU=192MHz実機確定」としていたが，
 *  実施32のJTAG実測（同じmcycle CSR vs SYSTIMER 16MHz基準の二点法，
 *  WiFi有無2ビルド×独立2回＝計4回，すべて47.96〜48.00MHzで再現）で
 *  実際にはXTAL48MHz直結（PCR_SYSCLK_CONF.soc_clk_sel=0固定，
 *  PCR_CPU_FREQ_CONF/PCR_AHB_FREQ_CONF＝div1，stock実測soc_clk_sel=3
 *  [PLL_F240M]との対比で確認）と判明した——bootloader_clock_configure()
 *  相当のCPUクロック切替え（XTAL→PLL）をASP3が一度も行っていなかった
 *  ため。実施03がどう192MHzを得たかは未解明（advisor指摘により本ラウンド
 *  では追跡せず）。
 *
 *  hardware_init_hook()に明示的なCPUクロック切替え（soc_clk_sel=3
 *  [PLL_F240M]・cpu_div=3・ahb_div=6，レジスタ値としては2nd-stage
 *  ブートローダのbootloader_clock_configure(CONFIG_BOOTLOADER_CPU_CLK_
 *  FREQ_MHZ=80)と完全に同一設定）を追加したところ，レジスタ設定は
 *  意図通り（cpu_div_num=2→÷3・ahb_div_num=5→÷6，stock実測と同一）
 *  だったにもかかわらず，実際に切り替わった後のmcycle実測は
 *  95.97〜96.01MHz（2s/2s/6s/8sの4窓で安定・収束傾向なし＝真値と判断，
 *  期待値80MHzとは一致しない）。ASP3は`rtc_clk_bbpll_configure()`相当の
 *  BBPLL周波数設定（regi2c I2C_BBPLLブロックへの目標480MHz書込み）を
 *  実行しておらず，ROMが較正済みのBBPLL（CAL_DONE=1を実施32冒頭で確認
 *  済み）をそのまま流用したため，ROMが実際にロックした周波数が
 *  ESP-IDFの前提する480MHzとは異なる（実測から逆算すると"PLL_F240M"
 *  ネット＝288MHz相当，BBPLL本体は576MHz相当と推定）と考えられる。
 *  本ラウンドでは，理論値80MHzを追わずBBPLLの実際の周波数設定を修正
 *  する（regi2c書込みが必要でリスク増）よりも，**実測値96MHzをそのまま
 *  正としてCORE_CLK_MHZを校正する**方針を採った（advisorの「29ラウンド
 *  regression huntにしない・実測を信頼する」指針と同じ考え方）。
 *  docs/c5-bringup.md 実施32参照。
 */
#define CORE_CLK_MHZ            96  /* 【実施32実測確定】CPUクロック切替え後のmcycle実測95.97〜96.01MHz（4窓で安定）。理論値80MHzとは不一致だが実測を正とする */

/*
 *  微少時間待ちのための定義（nsec単位）
 *
 *  実施03は「1反復=20.84ns(=4cyc@192MHz)」としてTIM1/TIM2=20と較正して
 *  いたが，実施32でCORE_CLK_MHZが192→96へ訂正されたため，同じ
 *  「1反復=4サイクル」という（周波数に依らない）マイクロアーキテクチャ
 *  定数を前提に，実時間コストを96MHzへ比例換算した：4cyc/96MHz=41.67ns。
 *  【注意・未実測】この42ns（切上げ）はJTAG hw-bp注入による実機再較正
 *  ではなく，実施03の「1反復=4サイクル」という値からの机上比例外挿
 *  である。実施32の時間内では改めての実機タイミング較正（実施03 §3と
 *  同じ手法）は実施していない——次段でのフォローアップ推奨（申し送り
 *  参照）。アンダーシュート回避のため切り上げ（41.67→42）を採用。
 */
#define SIL_DLY_TIM1    42  /* 【実施32・机上比例外挿，未実測】エントリ(addi+分岐)≒1反復≒4cyc/96MHz=41.67ns→切上げ42 */
#define SIL_DLY_TIM2    42  /* 【実施32・机上比例外挿，未実測】1反復=4cyc/96MHz=41.67ns→切上げ42 */

/*
 *  ペリフェラルのベースアドレス
 *
 *  以下はコーディネータがhal（soc/esp32c5/register/soc/reg_base.h・
 *  soc/esp32c5/include/soc/clic_reg.h）で確認済みの実値（C6と同一の
 *  ものはそのまま採用．異なるものは個別に注記）。
 */
#define ESP32C5_INTMTX_BASE     0x60010000  /* 割込みマトリクス（ソースルーティング．C6と同一） */
#define ESP32C5_CLIC_BASE       0x20800000  /* CLICグローバル設定（NLBITS・INFO・THRESH等） */
#define ESP32C5_CLIC_INT_CONFIG (ESP32C5_CLIC_BASE + 0x0)  /* mnlbits等．冷間ブート実測0x6（mnlbits=3．ROM/HW設定）．0への正規化はmil固着凍結を解消せず撤回済み＝書き替えない（実施27） */
#define ESP32C5_CLIC_INT_THRESH (ESP32C5_CLIC_BASE + 0x8)  /* メモリマップト側threshold（実測0．未使用＝mintthresh CSRを使用） */
#define ESP32C5_CLIC_CTRL_BASE  0x20801000  /* CLIC per-line制御（CLIC_INT_CTRL_REG(i)=BASE+4i） */
#define ESP32C5_SYSTIMER_BASE   0x6000A000  /* システムタイマ（C6と同一） */
#define ESP32C5_USBJTAG_BASE    0x6000F000  /* USB Serial/JTAGコントローラ（C6と同一） */
#define ESP32C5_UART0_BASE      0x60000000  /* UART0（C6と同一） */
#define ESP32C5_TIMG0_BASE      0x60008000  /* タイマグループ0（MWDT．C6と同一） */
#define ESP32C5_TIMG1_BASE      0x60009000  /* タイマグループ1（MWDT．C6と同一） */
#define ESP32C5_LP_WDT_BASE     0x600B1C00  /* 低電力ドメインWDT（C6と同一） */
#define ESP32C5_PCR_BASE        0x60096000  /* Peripheral Clock and Reset（C6と同一） */
#define ESP32C5_INTPRI_BASE     0x600C5000  /* ソフトウェア割込み（FROM_CPU_n）等．C6と同一 */
#define ESP32C5_EFUSE_BASE      0x600B4800  /* eFuse（C6の0x600B0800から+0x4000移動．要注意） */

/*
 *  ソフトウェア割込み（ras_int／clr_int用．INTPRIペリフェラル内．
 *  hal: soc/esp32c5/register/soc/intpri_reg.hでC6と同一オフセットを確認済み）
 */
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0  (ESP32C5_INTPRI_BASE + 0x90)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_1  (ESP32C5_INTPRI_BASE + 0x94)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_2  (ESP32C5_INTPRI_BASE + 0x98)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_3  (ESP32C5_INTPRI_BASE + 0x9c)

/*
 *  割込みソース番号（割込みマトリクスへの入力．esp-hal-3rdpartyの
 *  soc/esp32c5/include/soc/interrupts.h＝periph_interrupt_t enumの実数値．
 *  C6と値が異なる（C5はHUKソース等が追加されenum全体が1つずつ後方へ
 *  ずれている）ため，C6の値を転記せずC5のヘッダから数え直した。
 *  実機での動作確認要）
 */
#define ESP32C5_INTSRC_UART0             47
#define ESP32C5_INTSRC_USB_SERIAL_JTAG   54
#define ESP32C5_INTSRC_SYSTIMER_TARGET0  61
#define ESP32C5_INTSRC_FROM_CPU_0        23
#define ESP32C5_INTSRC_FROM_CPU_1        24
#define ESP32C5_INTSRC_FROM_CPU_2        25
#define ESP32C5_INTSRC_FROM_CPU_3        26
#define ESP32C5_TNUM_INTSRC              84  /* ETS_MAX_INTR_SOURCE（C6は77） */

/*
 *  SYSTIMERレジスタ（unit0＋target0のみ使用．C3・C6と同一レイアウト＝
 *  ベースアドレスのみ異なる）
 *
 *  【実機確定】docs/c5-bringup.md 実施03。JTAGでUNIT0カウンタを2s／4s
 *  間隔でスナップショット実読（壁時計bracket）した結果，カウント率＝
 *  16.001MHz（16.004／15.999／16.001の3計測）と確定＝ticks/us=16で正しい。
 *  C5のSYSTIMERはXTAL48MHz÷3=16MHzで駆動（C6は40MHz÷2.5=16MHzと分周が
 *  違うが結果の16MHzは一致）。CPUクロック(192MHz)とは独立の固定16MHz。
 */
#define ESP32C5_SYSTIMER_CONF           (ESP32C5_SYSTIMER_BASE + 0x00)
#define ESP32C5_SYSTIMER_UNIT0_OP       (ESP32C5_SYSTIMER_BASE + 0x04)
#define ESP32C5_SYSTIMER_TARGET0_HI     (ESP32C5_SYSTIMER_BASE + 0x1C)
#define ESP32C5_SYSTIMER_TARGET0_LO     (ESP32C5_SYSTIMER_BASE + 0x20)
#define ESP32C5_SYSTIMER_TARGET0_CONF   (ESP32C5_SYSTIMER_BASE + 0x34)
#define ESP32C5_SYSTIMER_UNIT0_VALUE_HI (ESP32C5_SYSTIMER_BASE + 0x40)
#define ESP32C5_SYSTIMER_UNIT0_VALUE_LO (ESP32C5_SYSTIMER_BASE + 0x44)
#define ESP32C5_SYSTIMER_COMP0_LOAD     (ESP32C5_SYSTIMER_BASE + 0x50)
#define ESP32C5_SYSTIMER_INT_ENA        (ESP32C5_SYSTIMER_BASE + 0x64)
#define ESP32C5_SYSTIMER_INT_RAW        (ESP32C5_SYSTIMER_BASE + 0x68)
#define ESP32C5_SYSTIMER_INT_CLR        (ESP32C5_SYSTIMER_BASE + 0x6C)
#define ESP32C5_SYSTIMER_INT_ST         (ESP32C5_SYSTIMER_BASE + 0x70)

#define ESP32C5_SYSTIMER_CONF_UNIT0_WORK_EN    (1U << 30)
#define ESP32C5_SYSTIMER_CONF_TARGET0_WORK_EN  (1U << 24)
#define ESP32C5_SYSTIMER_OP_UPDATE             (1U << 30)
#define ESP32C5_SYSTIMER_OP_VALUE_VALID        (1U << 29)
#define ESP32C5_SYSTIMER_TARGET0_PERIOD_MODE   (1U << 30)
#define ESP32C5_SYSTIMER_INT_TARGET0           (1U << 0)
#define ESP32C5_SYSTIMER_TICKS_PER_US   16U  /* 【実機確定】実施03 JTAG実測16.00MHz（48MHz÷3） */

/*
 *  USB Serial/JTAGレジスタ（C6と同一レイアウト．ベースアドレスのみ
 *  異なる＝hal: soc/esp32c5/register/soc/usb_serial_jtag_reg.hで
 *  EP1=+0x0・EP1_CONF=+0x4がC6と一致することを確認済み）
 */
#define ESP32C5_USBJTAG_EP1(base)		((uint32_t *)((base) + 0x00U))
#define ESP32C5_USBJTAG_EP1_CONF(base)	((uint32_t *)((base) + 0x04U))
#define ESP32C5_USBJTAG_EP1_CONF_WR_DONE		UINT_C(0x00000001)
#define ESP32C5_USBJTAG_EP1_CONF_IN_DATA_FREE	UINT_C(0x00000002)
#define ESP32C5_USBJTAG_EP1_CONF_OUT_DATA_AVAIL	UINT_C(0x00000004)

/*
 *  ウォッチドッグタイマ
 *
 *  TIMG_WDT_WKEY：hal soc/esp32c5/register/soc/timer_group_reg.hの
 *  TIMG_WDT_WKEYフィールドdefault値=1356348065=0x50D83AA1を確認済み
 *  （C6と同一値．ヘッダに明記されたdefaultのため転記ではなく実引用）。
 *
 *  LP_WDT_WDT_WKEY・LP_WDT_SWD_WKEY：hal soc/esp32c5/register/soc/
 *  lp_wdt_reg.hはC6と同様にdefault=0（未記載＝"need_des"相当）で実際の
 *  解錠キーをヘッダから確定できない。C3/C6で実績のある0x50D83AA1・
 *  0x8F1D312Aを暫定値として使うが，C6のesp32c6.hが一度実際に誤記
 *  （SWDキー）を含んでいた前例があるため，値を鵜呑みにせず実機で解錠
 *  成功を必ず確認すること。
 *  【実機確認待ち】docs/c5-port-design.md §8.1 6番
 */
#define ESP32C5_TIMG_WDTCONFIG0(base)   ((base) + 0x48)
#define ESP32C5_TIMG_WDTWPROTECT(base)  ((base) + 0x64)
#define ESP32C5_TIMG_WDT_WKEY           0x50D83AA1U  /* hal timer_group_reg.h defaultで確認済み */

#define ESP32C5_RTC_CNTL_WDTCONFIG0     ESP32C5_LP_WDT_CONFIG0
#define ESP32C5_RTC_CNTL_WDTWPROTECT    ESP32C5_LP_WDT_WPROTECT
#define ESP32C5_RTC_CNTL_WDT_WKEY       ESP32C5_LP_WDT_WDT_WKEY
#define ESP32C5_RTC_CNTL_SWD_CONF       ESP32C5_LP_WDT_SWD_CONFIG
#define ESP32C5_RTC_CNTL_SWD_WPROTECT   ESP32C5_LP_WDT_SWD_WPROTECT
#define ESP32C5_RTC_CNTL_SWD_WKEY       ESP32C5_LP_WDT_SWD_WKEY
#define ESP32C5_RTC_CNTL_SWD_AUTO_FEED_EN  (1U << 18)

#define ESP32C5_LP_WDT_CONFIG0          (ESP32C5_LP_WDT_BASE + 0x00)
#define ESP32C5_LP_WDT_WPROTECT         (ESP32C5_LP_WDT_BASE + 0x18)
#define ESP32C5_LP_WDT_WDT_WKEY         0x50D83AA1U  /* 【未確認・暫定値】C3/C6実績の転用．実機要確認 */
#define ESP32C5_LP_WDT_SWD_CONFIG       (ESP32C5_LP_WDT_BASE + 0x1c)
#define ESP32C5_LP_WDT_SWD_WPROTECT     (ESP32C5_LP_WDT_BASE + 0x20)
#define ESP32C5_LP_WDT_SWD_WKEY         0x8F1D312AU  /* 【未確認・暫定値】同上 */
#define ESP32C5_LP_WDT_SWD_AUTO_FEED_EN (1U << 18)

#endif /* TOPPERS_ESP32C5_H */
