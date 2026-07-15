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

/*  pmu_init() は esp_private/esp_pmu.h で宣言済み（void pmu_init(void)）．
 *  esp_ocode_calib_init() は esp_private/ocode_init.h で宣言済み． */

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
