# C5 evidence-04 — stock `pmu_init()` を ASP3 の C5 起動経路へ**そのまま積む**

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `d0:cf:13:f0:c8:94`, hub **port5**, `ttyACM5` / `ttyUSB2`）
toolchain: Espressif `riscv32-esp-elf` esp-15.2.0

evidence-03 §4.3「次の一手＝seam化ではなく `pmu_init()` 相当の移植」への回答。
記録済み真因 [[c5-wifi-modem-domain-unpowered]] が「次ラウンド」として書いていた方向。

---

## 1. 採った方式：**移植/縮約ではなく stock ソースの verbatim コンパイル**

タスク指定の第一候補（「まず『stock ソースをそのまま積む』道を検討」）が**そのまま通った**。
縮約・書き写しは一切していない。

| 積んだ stock ソース | 役割 |
|---|---|
| `esp-idf/components/esp_hw_support/port/esp32c5/pmu_init.c` | `pmu_init()` 本体 |
| 〃 `/pmu_param.c` | HP/LP 各モードの power/clock/digital/analog/retention 記述子（`get_act_hp_dbias()` は eFuse 実読み） |
| 〃 `/ocode_init.c` | `esp_ocode_calib_init()`（`pmu_init.c:224` から POWERON時のみ） |
| `esp-idf/components/esp_hw_support/regi2c_ctrl.c` | 上の REGI2C アクセス実体 |
| `esp-idf/components/esp_rom/patches/esp_rom_hp_regi2c_esp32c5.c` | ★C5 は `ESP_ROM_WITHOUT_REGI2C`（ROMに regi2c 実体が無い．どの `esp32c5.rom*.ld` にもシンボル不在を実測）＝stock も同パッチを積む |
| `esp-idf/components/hal/lp_timer_hal.c` | `rtc_time_get()`（`calibrate_ocode()` 経路）用．stock も `CONFIG_SOC_LP_TIMER_SUPPORTED` で積む |

**これが可能になったのは供給移行（evidence-02）の直接の成果**：供給が esp-idf submodule(v5.5.4)
へ全面移行済（`ninja -t deps` の hal 参照 0）なので、`esp_hw_support/port/esp32c5/` の
ソースがそのままインクルードパス上で解決する。

### 1.1 ビルド上で実際に要った手当て（3点だけ．いずれもソース無改変）

1. `-I .../esp_hw_support/include/esp_private` — `pmu_init.c:18` の `#include "regi2c_ctrl.h"`
   （ディレクトリ名なし）用。IDF本体も `PRIV_INCLUDE_DIRS port/include include/esp_private`
   （`esp_hw_support/CMakeLists.txt:209`）で同じ解決をしている。
2. `esp_rom_hp_regi2c_esp32c5.c` の追加（上表．未定義参照 `esp_rom_regi2c_write_mask` 等で発覚）。
3. `lp_timer_hal.c` の追加（未定義参照 `lp_timer_hal_get_cycle_count` で発覚）。

### 1.2 ★stock でも落ちるもの＝積まなかったもの

- **`pmu_pvt.c` / PVTブロック（`pmu_init.c:228-242`）は積まない**。`CONFIG_ESP_ENABLE_PVT` は
  `esp_hw_support/Kconfig:321` で `depends on SOC_PMU_PVT_SUPPORTED` だが、**C5 の
  `soc_caps.h` に当該マクロは存在しない**（実測：`grep PVT soc/esp32c5/include/soc/soc_caps.h`
  ＝0件）＝**stock の C5 ビルドでも PVT はコンパイルされない**。
  ※`target_kernel_impl.c` の実施35 コメントにある「PVT自動dbiasループが固定注入を上書きする」
  は別版(v6.1)由来の観測とみられる。本ラウンドの供給（v5.5.4）では該当しない。

### 1.3 呼出し位置＝stock の実行順序に合わせた

stock: `call_start_cpu0()` 末尾 → `esp_rtc_init()`（=`pmu_init()`, `esp_system/port/soc/esp32c5/clk.c:82`）
→ **その後** `esp_clk_init()`（RTC_FAST/SLOW clk 選択＋CPU周波数切替え）。

ASP3: `hardware_init_hook()` の `esp32c5_reassert_wdt_disable()` 直後、
`esp32c5_r35_rtc_fast_clk_select()` / `esp32c5_r32_cpu_clock_switch()`（＝`esp_clk_init()` の
ASP3側対応物）より**前**に `pmu_init()` を置いた。

`ocode` の重い経路との順序競合が無いことを事前に確認済み：本チップ（rev v1.0＝
`efuse_hal_chip_revision()`=100、efuse blk rev v0.3＝`blk_ver`>=2）では
`esp_ocode_calib_init()` は `set_ocode_by_efuse(1)`（eFuse値をregi2cで書くだけ）を通り、
PLLを止める `calibrate_ocode()` には**入らない**（`ocode_init.c:84-90`）。

### 1.4 可逆性

`option(ASP3_C5_PMU_INIT ...)` **既定 OFF**。OFF時は `TOPPERS_ESP32C5_PMU_INIT` が定義されず、
`pmu_init()` の呼出しも stock ソースのコンパイルも一切発生しない（＝非回帰は構成上自明）。

---

## 2. ★予測（**実機を触る前に固定**．測定後に改竄しない）

### 2.1 ビットモデルの妥当性検証（予測の前提）

レジスタのビット配置は `soc/esp32c5/register/soc/pmu_struct.h` から起こしたが、
**モデル自体が正しいことを、r35 が独立に実測した stock 値**
（`target_kernel_impl.c` の `ESP32C5_R35_FIXED_*`＝クロスカーネル・ハンドオフ実験で
stock 実機から採った値）**と突き合わせて検証した＝9/9 一致**：

| レジスタ | pmu_param.c から計算したモデル値 | r35 が stock 実機で実測した値 |
|---|---|---|
| `HP_MODEM_BACKUP` (0x600B0050) | `00100010` | `00100010` |
| `HP_SLEEP_BACKUP` (0x600B0084) | `21100200` | `21100200` |
| `HP_MODEM_SYSCLK` (0x600B0058) | `b8000000` | `b8000000` |
| `HP_MODEM_DIG_POWER` (0x600B0034) | `20000000` | `20000000` |
| `HP_MODEM_HP_CK_POWER` (0x600B0048) | `70000000` | `70000000` |
| `HP_SLEEP_SYSCLK` (0x600B008C) | `30000000` | `30000000` |
| `HP_SLEEP_DIG_POWER` (0x600B0068) | `08200000` | `08200000` |
| `HP_SLEEP_HP_CK_POWER` (0x600B007C) | `1c000000` | `1c000000` |
| `HP_MODEM_ICG_MODEM` (0x600B0040) | `40000000` | `40000000` |

⇒ **`pmu_param.c` のデフォルト記述子＝stock 実機のPMU値**であることが独立検証された。
つまり「stock ソースをそのまま積む」＝「r35 が固定注入しようとした値へ、eFuse トリムまで
正しく到達する」。スクリプト＝`scratchpad/predict.py`。

### 2.2 予測（HP_ACTIVE バンク＝stock 実測が存在しない領域）

| アドレス | レジスタ | evidence-03 が実測した現状 | **予測（pmu_init 後）** |
|---|---|---|---|
| 0x600B0000 | `HP_ACTIVE_DIG_POWER` | `00000000` | **`00000000`（変化なし）**：記述子の全フィールドが0＝`.val=0` の書込み |
| 0x600B0004 | `HP_ACTIVE_ICG_HP_FUNC` | 未公表 | `ffffffff` |
| 0x600B0008 | `HP_ACTIVE_ICG_HP_APB` | 未公表 | `ffffffff` |
| **0x600B000C** | **`HP_ACTIVE_ICG_MODEM`** | **`80000000`** | **`80000000`（変化なし）** ← ★下記2.3 |
| **0x600B0014** | **`HP_ACTIVE_HP_CK_POWER`** | 未公表 | **`70000000`** ← ★C6の教訓が言う「真のRF/アナログ電源」のC5対応レジスタ |
| 0x600B001C | `HP_ACTIVE_BACKUP` | 未公表 | `010200a0` または `010000a0`（`hp_modem2active_backup_clk_sel` が `SOC_CPU_CLK_SRC_PLL_F160M`＝`PMU_CLK_SRC_VAL` が比較する `SOC_MOD_CLK_PLL_F160M` とは別enum．数値衝突次第） |
| 0x600B0020 | `HP_ACTIVE_BACKUP_CLK` | 未公表 | `ffffffff` |
| 0x600B0024 | `HP_ACTIVE_SYSCLK` | 未公表 | `08000000` |
| 0x600B0034〜0x600B0064 | **HP_MODEM バンク** | POR相当 | **§2.1 の r35 stock 値へ一致** |
| 0x600B0068〜0x600B0098 | **HP_SLEEP バンク** | POR相当 | **§2.1 の r35 stock 値へ一致** |
| 0x600B009C〜 | **LP バンク**（`pmu_lp_system_init_default`） | POR相当 | 変化する |
| PHYDIG 0x600A0450 / 0x600A047C | | `0` / `0` | **変化しない**（pmu_init は MODEM_SYSCON/LPCON のICGビットマップを触らない＝ブロックは無給電のまま） |
| MODEM_SYSCON 0x600A9C00〜 | | POR相当 | **変化しない**（同上） |

### 2.3 ★予測の目玉：evidence-03 の `PMU_HP_ACTIVE_ICG_MODEM_REG` の解釈は**誤り**である

evidence-03 §4.2 は `0x600B000C = 80000000` を「**POR のまま＝MODEM ICG 未設定**」と
ラベルしている。だが `pmu_struct.h` の
`pmu_hp_icg_modem_reg_t { uint32_t reserved0:30; uint32_t code:2; }` ＝ code は **bit[31:30]**、
かつ `esp_pmu.h:32-34` が `PMU_HP_ICG_MODEM_CODE_ACTIVE = 2`。
⇒ **`0x80000000` は code=0b10=2＝`PMU_HP_ICG_MODEM_CODE_ACTIVE`＝pmu_init が HP_ACTIVE へ
書くのと同じ値**。`pmu_reg.h:103` は「default: 0」と書いているので、この値は POR ではなく
**何か（ROM）が既に code=2 を書いた後の状態**である。

⇒ **予測：pmu_init を入れてもこのレジスタは `80000000` のまま動かない。**
もし動いたら、私のビット解釈かレジスタヘッダのどちらかが誤り＝重要な反証情報として記録する。

※この予測が当たると、[[c5-wifi-modem-domain-unpowered]]／実施13 の
「Direct Boot では `icg_modem.code` が 0 のまま残る」という記述とも矛盾する。
その矛盾自体を実測で決着させるのが本ラウンドの副産物。

### 2.4 成功基準に対する事前の見立て（＝外れてもよい．記録が目的）

**pmu_init だけでは自前シムを外せない**と予測する。理由：`esp_shim_modem_icg_init()` が
やっている仕事のうち PMU 側（`pmu_ll_hp_set_icg_modem`）は §2.3 のとおり既に一致している
可能性が高い一方、**`modem_syscon_ll_set_modem_apb_icg_bitmap()` /
`modem_lpcon_ll_set_i2c_master_icg_bitmap()` / `..._lp_apb_icg_bitmap()` は
MODEM_SYSCON/MODEM_LPCON 側のレジスタであり、`pmu_init()` の守備範囲外**
（stock ではこれらは `modem_clock.c` / `esp_perip_clk_init()` 側が設定する）。
⇒ C6 の先行事例（「部分 landed だが不十分」）と**同型の結果**になると見込む。

---

## 3. 実測（§4 以降に追記）

（測定結果は commit 後に追記する。予測の改竄防止のため、本節より上は測定前に commit 済み。）
