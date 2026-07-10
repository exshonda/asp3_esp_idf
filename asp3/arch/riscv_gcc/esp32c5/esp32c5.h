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
 *  【実機確定＝192MHz】実施03のJTAG実測（mcycle CSR vs SYSTIMER 16MHz基準の
 *  二点法：1s/4s計測で191.9993MHz，各raw点も191.99〜192.02MHzと極めて安定）。
 *  C6の160MHz（SPLL÷3）と異なり，C5のROMブートローダは起動時点で既に
 *  CPU=192MHz（=XTAL48MHz×4）に設定済み。レジスタ書換え不要。
 */
#define CORE_CLK_MHZ            192  /* 【実機確定】実施03 mcycle実測192.00MHz（48MHz×4） */

/*
 *  微少時間待ちのための定義（nsec単位）
 *
 *  【実機確定】docs/c5-bringup.md 実施03。sil_dly_nse(N)の壁時計実測
 *  （SYSTIMER 16MHz基準・N=400M/800Mの二点法・pc→sil_dly_nse注入）で
 *  1反復=20.84ns（=4cyc@192MHz＝分岐ペナルティ由来。C6の12から大きく
 *  外れるのは§8.1.5の警告通り単純外挿が効かない好例）と確定。未達
 *  （delay<要求）を避けるためTIM2=20へ切下げ（実測でsil_dly_nse(N)≈
 *  1.04×N＝約4%の安全余裕・アンダーシュート無し）。TIM1はエントリ
 *  （addi＋分岐成立≒1反復≒20ns）に相当させTIM2と同値の20とする。
 */
#define SIL_DLY_TIM1    20  /* 【実機確定】実施03較正．エントリ(addi+分岐)≒1反復≒20ns */
#define SIL_DLY_TIM2    20  /* 【実機確定】実施03較正．1反復=20.84ns(=4cyc@192MHz)，未達回避に20へ切下げ */

/*
 *  ペリフェラルのベースアドレス
 *
 *  以下はコーディネータがhal（soc/esp32c5/register/soc/reg_base.h・
 *  soc/esp32c5/include/soc/clic_reg.h）で確認済みの実値（C6と同一の
 *  ものはそのまま採用．異なるものは個別に注記）。
 */
#define ESP32C5_INTMTX_BASE     0x60010000  /* 割込みマトリクス（ソースルーティング．C6と同一） */
#define ESP32C5_CLIC_BASE       0x20800000  /* CLICグローバル設定（NLBITS・INFO・THRESH等） */
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
