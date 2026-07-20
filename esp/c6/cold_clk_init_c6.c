/*
 *  ESP32-C6：真cold(POR) 用のクロック初期化シム（evidence-c6-04）
 *
 *  ★WiFi・BT の «両方» が必要とするため target.cmake から
 *  (ESP32C6_WIFI OR ESP32C6_BT) で積む共有ファイル．
 *  （当初 bt/bt_pmu_init_c6.c に置いたが，同じ真cold ハングは WiFi 経路
 *   （esp_wifi_adapter.c）でも起きる＝BT 専用にしてはいけない．
 *   rtc_clk.c は esp_wifi.cmake・esp_bt_idf61.cmake の双方が既にリンク済み．）
 *
 *  ★hal / esp-idf / asp3_core は «編集しない»（CLAUDE.md 禁則）．
 *  本ファイルは stock の呼出しを «同じ順序で» 呼ぶ薄いシムに徹する．
 */
#include <stdint.h>
#include "soc/rtc.h"
#include "esp_rom_sys.h"
#include "hal/clk_tree_ll.h"
#include "hal/regi2c_ctrl_ll.h"

/*  esp_rom_uart.h は hal/uart_ll.h を要求し本ターゲットの include path に
 *  無いため，必要な1本だけ自前で宣言する（ROM シンボル．ld で解決）． */
extern void esp_rom_output_tx_wait_idle(uint8_t uart_no);
/*  実体＝wifi/esp_shim.c（WiFi/BT 共有）．stock rtc_clk_init.c:63 の
 *  rtc_clk_modem_clock_domain_active_state_icg_map_preinit() と等価． */
extern void esp_shim_modem_icg_init(void);

#ifdef TOPPERS_ESP32C6_COLD_CPU_PLL
/*
 *  ★★evidence-c6-04【真cold ハングの真因の修正】
 *  CPU/SOC ルートクロックを PLL@160MHz へ «明示的に» 設定する
 *  ＝stock の 2nd-stage bootloader が rtc_clk_init() でやっていることの移植．
 *
 *  ★★実測（真cold・LP_AON 直読み・sentinel で真cold 証明済み）：
 *      STORE5 = 0xbb110280  ->  source=0(XTAL), freq=40MHz
 *  ＝**POR 直後に ROM が Direct Boot へ渡してくる CPU は XTAL@40MHz であり，
 *    PLL@160MHz «ではない»**．
 *
 *  ★これは ASP3 の前提を真っ向から否定する．target_kernel_impl.c は
 *      「ROMブートローダがDirect Boot到達前に既にSOC_CLK_SEL=SPLL・
 *        480MHz÷3÷1＝160MHzへ設定済みであることを確認した …
 *        追加のレジスタ操作は不要かつ行うべきでない」
 *  と明記して **意図的にクロック設定をしない** 判断をしているが，
 *  **その «確認» は warm でしか行われていなかった**（本プロジェクトの
 *  C6 作業は evidence-c6-03 まで全て warm＝memory の «真cold未検証» 留保どおり）．
 *  warm では PCR が前ブートの SOC_CLK_SEL=PLL を保持するので前提は真，
 *  **真cold(POR) では PCR が既定へ戻り前提は偽**＝これが cold/warm 分岐の真因．
 *
 *  ★機序：SOC_ROOT_CLK=XTAL のままだと **PLL 由来の modem/PHY クロック系
 *  （MODEM_APB 等）が所定の周波数にならず，PHY の RF シンセ PLL がロック
 *  できない** ⇒ 真cold JTAG で確定したハング
 *  （PC=ram_set_chan_freq_sw_start+0x1e＝0x600a00cc bit8 の永久スピン）．
 *  ＋ CORE_CLK_MHZ=160 前提の esp_rom_set_cpu_ticks_per_us(160)（実施48）も
 *  真cold では «実クロック 40MHz に対して 4倍長い» 遅延を生む＝前提が崩れる．
 *
 *  ★stock の該当箇所（esp-idf/components/esp_hw_support/port/esp32c6/
 *  rtc_clk_init.c:98-106．2nd-stage bootloader が呼ぶ＝Direct Boot には無い）：
 *      rtc_clk_cpu_freq_get_config(&old_config);
 *      bool res = rtc_clk_cpu_freq_mhz_to_config(cfg.cpu_freq_mhz, &new_config);
 *      rtc_clk_cpu_freq_set_config(&new_config);
 *
 *  ★呼出し位置＝software_init_hook（.data/.bss 初期化後・sta_ker 前・
 *  sio_initialize 前）．
 *   - rtc_clk.c は static s_cur_pll_freq（.bss）を使う ⇒ **.bss クリア後**必須
 *     ＝hardware_init_hook では駄目（本ラウンドで pmu_init が踏んだ罠と同型）．
 *   - sio_initialize 前 ⇒ UART の分周比は切替え後のクロックで計算される．
 *   - sta_ker 前 ⇒ 稼働中カーネル・タイマを撹乱しない．
 *  ★warm 非回帰：既に PLL@160 なら rtc_clk_cpu_freq_set_config() は
 *  BBPLL を止めず freq 設定のみ＝実質 no-op．
 */
/*  ASP3 の CORE_CLK_MHZ（asp3_core/arch/riscv_gcc/esp32c6/esp32c6.h:73＝160）と
 *  一致させる．本ファイルはカーネルヘッダを include しない（hal/IDF 側の
 *  ヘッダのみ）ため値を再掲する．両者がずれたらビルドではなく実機の
 *  タイミングが壊れるので，変更時は必ず両方を合わせること．            */
#define ESP_SHIM_CPU_FREQ_MHZ	160

void
esp_shim_cold_cpu_clk_init(void)
{
	rtc_cpu_freq_config_t	old_config, new_config;

	rtc_clk_cpu_freq_get_config(&old_config);

	/*  ROM が残した «実際の» 状態を真cold で記録する（診断・恒久で無害）．
	 *  STORE5 = 0xBB11_0000 | (src & 0xF) | ((freq_mhz & 0xFFF) << 4)     */
	*(volatile uint32_t *)0x600B1014UL =
		0xBB110000UL | ((uint32_t)old_config.source & 0xFU)
		| (((uint32_t)old_config.freq_mhz & 0xFFFU) << 4);

	/*
	 *  ★★①regi2c（解析I2C）マスタを «PLL 設定より前に» 使える状態にする．
	 *
	 *  stock は rtc_clk_init() の **最初の行**（rtc_clk_init.c:63）で
	 *  rtc_clk_modem_clock_domain_active_state_icg_map_preinit() を呼び，
	 *  その理由をコメントで明言している：
	 *    "... disable the clock gating of these clock domains in the
	 *     PMU_ACTIVE state, because the system clock source (PLL) in the
	 *     system boot up process needs to use the i2c master peripheral."
	 *  ＝**PLL の設定自体が regi2c マスタを使う**ので，ICG 解除が先．
	 *
	 *  ★ASP3 は等価な esp_shim_modem_icg_init() を «持っている» が，
	 *  呼ぶのが esp_shim_bt_clock_init()／esp_wifi_adapter.c（＝main_task の
	 *  BT/WiFi 初期化時＝**ずっと後**）だけで，**順序が stock と逆**だった．
	 *  ∴ ここで先に呼ぶ（冪等＝後段の BT/WiFi 経路の呼出しと共存する）．
	 *
	 *  ★実測（evidence-c6-04）：これが «無い» 状態で PLL へ切替えると
	 *  真cold で **BBPLL 較正の regi2c 完了待ちが永久スピン**した
	 *  （JTAG 実測：PC=rtc_clk_cpu_freq_set_config+0x168＝0x600af818 の
	 *   done ビット待ちループ．§11 の «regi2c done ビットが永久に立たない»
	 *   と同一機構）．＝stock のコメントどおりの現象を実機で再現した．
	 */
	esp_shim_modem_icg_init();
	_regi2c_ctrl_ll_master_enable_clock(true);
	regi2c_ctrl_ll_master_configure_clock();

	/*
	 *  ★★②MSPI(flash) 分周比を «切替えの前に» 直す — これが無いと死ぬ．
	 *
	 *  stock rtc_clk_init.c:93-96 の原文：
	 *    // On ESP32C6, MSPI source clock's default HS divider leads to 120MHz,
	 *    // which is unusable before calibration. Therefore, before switching
	 *    // SOC_ROOT_CLK to HS, we need to set MSPI source clock HS divider to
	 *    // make it run at 80MHz after the switch. PLL = 480MHz, so divider is 6.
	 *    clk_ll_mspi_fast_set_hs_divider(6);
	 *
	 *  ★実測（evidence-c6-04）：この1行を «入れずに» SOC_ROOT_CLK を PLL へ
	 *  切替えたところ，**真cold で切替え直後に即死**した（切替え前マーカ
	 *  STORE5=0xbb110280 は書けたが，切替え後マーカ STORE3 が 0 のまま＝
	 *  ASP3 は flash XIP 実行なので MSPI が 480/4=120MHz になって命令
	 *  フェッチが壊れた）．＝**stock のコメントどおりの現象を実機で再現**．
	 *  ∴ 分周比 6（480/6=80MHz）を先に設定してから切替える．
	 *
	 *  UART の TX FIFO を吐き切ってから触る（stock 同様．ここでは sio 未初期化
	 *  だが ROM が UART0 を使っている可能性があるため保守的に合わせる）．
	 */
#ifndef TOPPERS_ESP32C6_COLD_CPU_PLL_NO_MSPI
	esp_rom_output_tx_wait_idle(0);
	clk_ll_mspi_fast_set_hs_divider(6);
#endif

	/*  ★★③CPU/SOC ルートクロックを PLL@160MHz へ．                    */
	if (rtc_clk_cpu_freq_mhz_to_config((uint32_t) ESP_SHIM_CPU_FREQ_MHZ, &new_config)) {
		rtc_clk_cpu_freq_set_config(&new_config);
		/*  ROM の較正用大域変数へ実クロックを通知（実施48）．
		 *  rtc_clk_cpu_freq_set_config() 内でも設定されるが，
		 *  CORE_CLK_MHZ 前提を明示的に再確認する（冪等・無害）．        */
		esp_rom_set_cpu_ticks_per_us((uint32_t) ESP_SHIM_CPU_FREQ_MHZ);
	}

	/*  切替え «後» の状態を STORE4 相当ではなく STORE3 へ記録
	 *  （bt_smoke_c6 未使用．0xCC11_0000 | src | freq<<4）．             */
	rtc_clk_cpu_freq_get_config(&old_config);
	*(volatile uint32_t *)0x600B100CUL =
		0xCC110000UL | ((uint32_t)old_config.source & 0xFU)
		| (((uint32_t)old_config.freq_mhz & 0xFFFU) << 4);
}
#endif /* TOPPERS_ESP32C6_COLD_CPU_PLL */
