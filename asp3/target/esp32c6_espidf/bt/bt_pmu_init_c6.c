/*
 *  ESP32-C6 BLE：cold PLL-lock 用の pmu_init 移植シム（§20）
 *
 *  ★背景（docs/ble-c5c6-plan.md §19.8＋memory c5-wifi-modem-domain-unpowered）：
 *  C6 BT の phy_init（register_chipv7_phy→ram_set_chan_freq_sw_start）が
 *  «cold» で RF-synth-PLL ロック（0x600a00cc bit8）に失敗してハングする
 *  真因は，stock IDF が起動シーケンスで呼ぶ pmu_init()（PMU HP_ACTIVE
 *  モードの power/clock/analog 記述子＋DIG/RTC LDO＋bandgap o-code）を
 *  ASP3 の Direct Boot が丸ごと飛ばしていること．PMU が POR 既定のまま＝
 *  MODEM/RF アナログドメインが正しく給電されず，register_chipv7_phy の
 *  regi2c によるアナログ PLL 設定が着弾しない（C5 兄弟で «0x600a0000 への
 *  書込みが stick しない» と実測）．
 *
 *  ★修正＝stock の pmu_init() を «そのまま» 呼ぶ．pmu_init.c/pmu_param.c/
 *  ocode_init.c は hal submodule（IDF v6.1 とバイト一致確認済）を
 *  cmake の source list へ追加してリンクする（hal を «編集» はしない＝
 *  禁则遵守．差分＝この薄いシム＋cmake の list 追加だけ）．
 *
 *  ★呼出しは hardware_init_hook 内（カーネル/タイマ起動より前，stock が
 *  pmu_init を呼ぶ «早期» と同位相）．main_task（後期）ではなく早期に
 *  呼ぶことで，pmu_hp_system_init_default の HP_ACTIVE 電源記述子適用が
 *  «稼働中のカーネルを撹乱しない»（advisor 指摘）．
 *
 *  ★ocode（bandgap o-code）の reset-reason ゲート回避：pmu_init() 内の
 *  esp_ocode_calib_init() は «esp_rom_get_reset_reason==POWERON» のときだけ
 *  走る．RTS ピンリセット（自己テスト）では reset_reason≠POWERON のため
 *  走らず，真の電源断（親テスト）とで挙動が食い違う．bandgap o-code は
 *  アナログ基準電圧＝PLL にも効きうるため，両テストを同条件にすべく
 *  ここで «明示的に» esp_ocode_calib_init() を呼ぶ（set_ocode_by_efuse は
 *  冪等＝POR で二重呼出しになっても無害．本 board は efuse blk_version=
 *  v0.3>=1 のため set_ocode_by_efuse 経路＝CPU 周波数切替えを伴わない＝
 *  稼働中でも安全）．
 */
#include <stdint.h>
#include "esp_private/esp_pmu.h"
#include "esp_private/ocode_init.h"
#ifdef TOPPERS_ESP32C6_COLD_RECALIB_BBPLL
#include "soc/rtc.h"
#endif

/*  pmu_init() は esp_private/esp_pmu.h で宣言済み（void pmu_init(void)）．
 *  esp_ocode_calib_init() は esp_private/ocode_init.h で宣言済み． */

#ifdef TOPPERS_ESP32C6_PMU_DIAG
/*
 *  【evidence-c6-04・診断専用（既定OFF）】PMU_instance()->hal を
 *  LP_AON STORE<slot>（0x600B1000 + slot*4）へミラーする．
 *
 *  ★目的：「hardware_init_hook（.data 初期化 «前»）から pmu_init() を
 *  呼ぶと，PMU_instance() が返す .data 上の pmu_context.hal がゴミで，
 *  pmu_hp_system_init() の `ctx->hal->dev` が PMU(0x600B0000) を指さない」
 *  を **実機で直接測る**（推論でなく実測にする）．
 *   - hardware_init_hook（slot=8）と software_init_hook（slot=9）の両方で
 *     記録し，真cold で 8≠9 なら機序が確定する．
 *   - warm では SRAM に前ブートの .data が残るため 8==9 になるはず
 *     ＝これが cold/warm 分岐の説明になる．
 *
 *  ★hal ポインタを «deref しない»：ゴミなら不正アドレスで例外になり得る
 *  （この時点では mtvec 未設定＝chip_initialize は target_initialize で走る）．
 *  読むのは .data 上の 1 ワード（マップ済み SRAM）だけ＝安全．
 *
 *  ★slot 8/9 を使う理由：bt_smoke_c6 は STORE0（0x600B1000＝D-1 stage）と
 *  STORE7（0x600B101C＝intr trace）しか使わない．STORE1 は wifi の cal 値．
 *  STORE8/9 は ble_host_smoke_c6 が GAP connect/disconnect に使うため，
 *  本診断は bt_smoke_c6 専用（既定OFF なので恒久ビルドに影響しない）．
 */
void
esp_shim_bt_pmu_diag(uint32_t slot)
{
	pmu_context_t	*ctx = PMU_instance();

	*(volatile uint32_t *)(0x600B1000UL + (slot * 4U)) = (uint32_t)(ctx->hal);
}
#endif /* TOPPERS_ESP32C6_PMU_DIAG */

#ifdef TOPPERS_ESP32C6_COLD_RECALIB_BBPLL
/*
 *  【evidence-c6-04】stock の recalib_bbpll() 相当を Direct Boot に補う．
 *
 *  ★動機：真cold の実機 JTAG で，ハング位置を
 *  **PC=ram_set_chan_freq_sw_start+0x1e＝RF synth PLL ロック待ちスピン
 *  （0x600a00cc bit8 が永久に立たない）** と確定した（resume を挟んだ
 *  独立2サンプルとも同一 PC）．＝PHY が RF シンセをロックできない．
 *
 *  ★stock（esp_system/port/soc/esp32c6/clk.c:349 recalib_bbpll，
 *  CONFIG_ESP_SYSTEM_BBPLL_RECALIB=y で esp_rtc_init から呼ばれる）は
 *  起動時に **BBPLL を一旦止めて較正し直す**：
 *      // In earlier version of ESP-IDF, the PLL provided by bootloader is
 *      // not stable enough. Do calibration again here so that we can use
 *      // better clock for the timing tuning.
 *      if (old_config.source == SOC_CPU_CLK_SRC_PLL) {
 *          rtc_clk_cpu_freq_set_xtal();       // BBPLL 停止
 *          rtc_clk_cpu_freq_set_config(&old_config); // enable→configure＝再較正
 *      }
 *  ASP3 は target_kernel_impl.c で「ROM が既に 160MHz に設定済みだから
 *  触らない」と **明示的に «やらない» 判断**をしている＝
 *  **「stock がやっていることを我々がやめた」型**（C6 実施90-91 と同型）．
 *
 *  ★★注意：これは «仮説» であって «原因» ではない．BBPLL（システム PLL）と
 *  RF シンセ（PHY 内の別 PLL）は別物であり，«BBPLL 再較正が RF シンセの
 *  ロックを回復させる» 機序は **未証明**．ただし両者は regi2c アナログ
 *  マスタと bandgap 基準を共有するため無関係ではない．判別は安い＝
 *  真cold で 0x600B1000 が 0xb1d00005→0xb1d00008 に変われば決まる．
 *
 *  ★★XIP ハザード：stock は本処理を **IRAM_ATTR** に置く
 *  （"Placed in IRAM because disabling BBPLL may influence the cache"）が，
 *  ASP3 では rtc_clk.c は flash XIP（0x42xxxxxx）に載る．∴ BBPLL 停止中に
 *  命令フェッチが壊れて **死ぬ可能性がある**．«効かなかった» と «死んだ» を
 *  区別できないと誤結論になるので，**復帰後に LP_AON STORE5(0x600B1014) へ
 *  完了マーカ 0xBB110000|1 を書く**：
 *    - STORE5 が書けていれば «recalib は生還した»（＝以降の判定が有効）
 *    - STORE5 が 0 のままなら «recalib 中に死んだ»（＝IRAM 化が要る）
 *  ★STORE5 は bt_smoke_c6 が未使用（STORE0 と STORE7 のみ使う）．
 */
void
esp_shim_cold_recalib_bbpll(void)
{
	rtc_cpu_freq_config_t	old_config;

	rtc_clk_cpu_freq_get_config(&old_config);

	if (old_config.source == SOC_CPU_CLK_SRC_PLL) {
		rtc_clk_cpu_freq_set_xtal();
		rtc_clk_cpu_freq_set_config(&old_config);
	}

	/*  生還マーカ（上のコメント参照）．source!=PLL だった場合も «通過» は
	 *  記録し，値の下位で分岐が判るようにする．
	 *  ★STORE5 = 0xBB11_0000 | (source & 0xF) | ((freq_mhz & 0xFFF) << 4)
	 *    ＝ROM が Direct Boot 到達時点で残した «実際の» CPU クロック源と
	 *      周波数を真cold で直読みする（ASP3 の «ROM が SPLL/160MHz 設定済み»
	 *      という前提は warm で測られた疑いがある＝要検証）．
	 *    soc_cpu_clk_src_t: XTAL=0, PLL=1, RC_FAST=2 （C6）           */
	*(volatile uint32_t *)0x600B1014UL =
		0xBB110000UL | ((uint32_t)old_config.source & 0xFU)
		| (((uint32_t)old_config.freq_mhz & 0xFFFU) << 4);
}
#endif /* TOPPERS_ESP32C6_COLD_RECALIB_BBPLL */


void
esp_shim_bt_pmu_init(void)
{
	/*  stock の起動シーケンス（esp_system/port/soc/esp32c6/clk.c の
	 *  esp_perip_clk_init 経路）が呼ぶのと同じ PMU 初期化本体． */
	pmu_init();

	/*  reset-reason ゲートを跨いで bandgap o-code を確実に適用する
	 *  （上記コメント参照．POR では pmu_init 内で既に呼ばれ二重＝無害）． */
	esp_ocode_calib_init();
}
