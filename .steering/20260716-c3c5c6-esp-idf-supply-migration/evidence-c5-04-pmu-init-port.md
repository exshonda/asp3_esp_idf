# C5 evidence-04 — stock `pmu_init()` を ASP3 の C5 起動経路へ**そのまま積む**

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `<MAC-39>`, hub **port5**, `ttyACM5` / `ttyUSB2`）
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

## 3. 実測

**予測は commit `de4e92a` で測定前に固定済み**（本節より上は無改変）。

### 3.0 測定条件

- 全アーム **真の POWERON から**（`tmp/c5_cold_cycle.sh`＝`off 5`→**読み戻しで
  device count=0／Vbus 0.02V／`no device` を実証**→10s→`on 5`）。
- 書込前に毎回 `esptool read-mac` で `<MAC-39>` を照合（全アームで一致）。
- 採取＝`tmp/rts_boot_capture.py /dev/ttyACM5`（native USB-JTAG．single-reset）。`grep -a`。
- **ASP3側コードは完全同一。反転するのは cmake option だけ**。

> **★RTSリセット＝PMUにとってPOR相当であることの in-situ 対照**：baseline 採取で
> 「cold boot 起動直後のダンプ」と「その直後のRTSリセットのダンプ」が**同一キャプチャ内に
> 両方入った**（`probe_pmuinit_OFF_cold.log` に START が2回）。両者の PMU+000〜PMU+080 は
> **バイト単位で一致**。⇒ 以降の RTS 採取を cold と同一視してよいことを、推定ではなく
> **同一セッション内の実測**で裏付けた。

### 3.1 ★PMU レジスタ A/B（移植前→後．真のcold．変化 **39語 / 比較99語**）

生データ＝`tmp/c5_pmu_evidence/probe_pmuinit_{OFF,ON}_cold.log`、差分＝同 `AB_diff.txt`。

| アドレス | レジスタ | **OFF(前)** | **ON(後)** | 予測 |
|---|---|---|---|---|
| **0x600B0014** | **`HP_ACTIVE_HP_CK_POWER`** | **`00000000`** | **`70000000`** | ✅**的中** |
| 0x600B0018 | `HP_ACTIVE_BIAS` | `00000000` | `02000000` | ✅(xpd_bias=1) |
| 0x600B001C | `HP_ACTIVE_BACKUP` | `00000000` | `010200a0` | ✅**的中**（2択のうち「2へマップ」側） |
| 0x600B0020 | `HP_ACTIVE_BACKUP_CLK` | `00000000` | `ffffffff` | ✅的中 |
| 0x600B0024 | `HP_ACTIVE_SYSCLK` | `00000000` | `08000000` | ✅的中 |
| 0x600B0028 | `HP_ACTIVE_HP_REGULATOR0` | `c667f180` | `d804f180` | eFuse実読みのdbias |
| 0x600B0034 | `HP_MODEM_DIG_POWER` | `00000000` | `20000000` | ✅=r35 stock |
| 0x600B0038 | `HP_MODEM_ICG_HP_FUNC` | `ffffffff` | `00100000` | ✅=r35 stock |
| 0x600B003C | `HP_MODEM_ICG_HP_APB` | `ffffffff` | `00000200` | ✅=r35 stock |
| 0x600B0040 | `HP_MODEM_ICG_MODEM` | `00000000` | `40000000` | ✅=r35 stock |
| 0x600B0044 | `HP_MODEM_HP_SYS_CNTL` | `00000000` | `31000000` | ✅=r35 stock |
| 0x600B0048 | `HP_MODEM_HP_CK_POWER` | `00000000` | `70000000` | ✅=r35 stock |
| 0x600B0050 | `HP_MODEM_BACKUP` | `00000000` | `00100010` | ✅=r35 stock |
| 0x600B0054 | `HP_MODEM_BACKUP_CLK` | `00000000` | `ffffffff` | ✅=r35 stock |
| 0x600B0058 | `HP_MODEM_SYSCLK` | `00000000` | `b8000000` | ✅=r35 stock |
| 0x600B005C | `HP_MODEM_HP_REGULATOR0` | `c6678000` | `d8048000` | eFuse実読み（r35定数は`c0048000`．下記3.3） |
| 0x600B0068 | `HP_SLEEP_DIG_POWER` | `00000000` | `08200000` | ✅=r35 stock |
| 0x600B006C/0070 | `HP_SLEEP_ICG_HP_FUNC/APB` | `ffffffff` | `00000000` | ✅=r35 stock |
| 0x600B0078 | `HP_SLEEP_HP_SYS_CNTL` | `00000000` | `39000000` | ✅=r35 stock |
| 0x600B007C | `HP_SLEEP_HP_CK_POWER` | `00000000` | `1c000000` | ✅=r35 stock |
| 0x600B0084 | `HP_SLEEP_BACKUP` | `00000000` | `21100200` | ✅=r35 stock |
| 0x600B0088 | `HP_SLEEP_BACKUP_CLK` | `00000000` | `ffffffff` | ✅=r35 stock |
| 0x600B008C | `HP_SLEEP_SYSCLK` | `00000000` | `30000000` | ✅=r35 stock |
| 0x600B0090 | `HP_SLEEP_HP_REGULATOR0` | `c6678000` | `08048000` | ✅=r35 stock（**完全一致**） |
| 0x600B0098 | `HP_SLEEP_XTAL` | `80000000` | `00000000` | ✅(xpd_xtal=0) |
| 0x600B009C〜0x600B00C8 | **LPバンク**（6語） | 変化 | 変化 | ✅予測どおり変化 |
| 0x600B00F8〜0x600B0110 | **電源ドメイン force 系**（6語） | `0000001c`等 | `00000000`等 | ✅`pmu_power_domain_force_default()` |
| 0x600B0130 | | `00000000` | `00020000` | ✅`sleep_protect_mode` |
| **0x600B0000** | `HP_ACTIVE_DIG_POWER` | `00000000` | **`00000000`** | ✅**「変化しない」を的中** |
| **0x600B000C** | **`HP_ACTIVE_ICG_MODEM`** | **`80000000`** | **`80000000`** | ✅**§2.3の反証予測が的中** |
| 0x600B0004/0008 | `HP_ACTIVE_ICG_HP_FUNC/APB` | `ffffffff` | `ffffffff` | ✅変化なし（既に一致） |
| **PHYDIG 0x600A0450 / 0x047C** | | `0` / `0` | **`0` / `0`** | ✅**「変化しない」を的中** |
| **MODEM_SYSCON 0x600A9C00〜16語** | | POR相当 | **全語同一** | ✅**「変化しない」を的中** |

**予測は全て的中**（外れ 0）。異常マーカー（Guru/panic/WDT/Illegal）＝**0件**、
リブートループ無し。

### 3.2 ★「0x600b0014 系 RF/アナログ電源に到達したか」＝**到達した**

タスクが名指しした C6 先行事例の教訓（「部分 landed だが**真の RF 電源 `0x600b0014` 未達**」）
に対する直接の答え。

**C5 では `0x600B0014` = `PMU_HP_ACTIVE_HP_CK_POWER_REG`**（`pmu_reg.h`／`pmu_struct.h` の
`pmu_hp_hw_regmap_t` は +0x14 が `clk_power`。r35 の
`ESP32C5_R35_PMU_HP_MODEM_HP_CK_POWER = 0x600B0048 = 0x34+0x14` とも整合）＝
**`xpd_bb_i2c`(bit28) / `xpd_bbpll_i2c`(bit29) / `xpd_bbpll`(bit30)＝BB-I2C と BBPLL の
アナログ電源**そのもの。

⇒ **`00000000` → `70000000`（3ビットとも立った）＝C6 が到達できなかったのと同じアドレスに、
C5 では到達した。** HP_MODEM 側（`0x600B0048`）も同じく `70000000` へ到達。

> ※適用範囲の限定（厳守）：本測定は **C5 のもの**。C6 の cold-PLL 解消を主張する根拠には
> **しない**（別チップ・別壁。C6 の 0x600b0014 が同じ意味を持つかも未確認）。主張できるのは
> 「**C5 では pmu_init を正しく走らせれば BBPLL/BB-I2C のアナログ電源記述子まで landing する**」
> という事実のみ。ただし「pmu_init の呼出し元が app 側」という構造はチップ非依存なので、
> **本ラウンドで確立した型（stock verbatim ＋ weak `PMU_instance()` 上書き）は C6 へ持ち込める**。

### 3.3 副産物：**「stock ソースを積む」が「stock 実測値の定数注入」に勝る**ことの実証

`HP_MODEM_HP_REGULATOR0` は r35 の固定定数 `c0048000`（dbias=24）に対し、本ラウンドの
実測は `d8048000`（dbias=27）。差分は **`get_act_hp_dbias()` が eFuse から実読みする
ボード固有の電圧トリム**のフィールドのみ（`HP_SLEEP_HP_REGULATOR0` は dbias が定数1＝
eFuse非依存なので **`08048000` で r35 と完全一致**した）。

⇒ r35 のコメントが自ら警告していた「**他基板でこのコードをそのまま出荷すると
（別個体の）トリム値を焼き付ける副作用の恐れ**」が実測で裏付けられた。
stock ソース版はこの穴が構造的に無い。

---

## 4. ★実機で判明した機序：`hardware_init_hook()` は **.data 初期化より前**に走る

最初の ON ビルドは cold boot で**即クラッシュ**した（推測ではなく addr2line で実体確認）：

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
Guru Meditation Error: Core 0 panic'ed (Load access fault)
PC : 0x420027c6   RA : 0x42002cfe   MTVAL : 0xa59b20b8
MCAUSE : 0x30000005
```
```
0x420027c6: pmu_hp_system_init at pmu_init.c:62
0x42002cfe: pmu_hp_system_init_default at pmu_init.c:182 (inlined by) pmu_init at pmu_init.c:217
```

機序：stock の `PMU_instance()` は **初期化子つき `DRAM_ATTR` static**
（`pmu_context = { .hal = &pmu_hal, ... }`）を返す。`DRAM_ATTR` は `.dram1.N` ＝ ASP3 の
ld では `.data` に集約され（`esp32c5.ld:109`）、**フラッシュ→RAM コピー後に初めて有効**。
ところが `arch/riscv_gcc/common/start.S` は

```
:120   jal  hardware_init_hook      <-- pmu_init() を呼びたい場所
:127   ... bssセクションのクリア ...
:143   ... dataセクションの初期化（ROM化対応） ...
```

＝**`hardware_init_hook()` は .bss クリア／.data 初期化より前**（「RAM初期化より前に立てる
べきHW」用のフックという TOPPERS の設計）。⇒ `ctx->hal` が未初期化のゴミ → Load access fault。
`MTVAL=0xa59b20b8` はまさにその未初期化RAM値。

**対処＝`PMU_instance()` の strong 上書き**（`asp3/target/esp32c5_espidf/pmu_instance.c`）。
stock 側が **`__attribute__((weak))` と明示している拡張点**なので、
**stock ソース（pmu_init.c）は無改変のまま**でよい（CLAUDE.md の「供給元ツリーを直接編集
しない」精神を維持）。静的初期化子に頼らず呼出しの度に代入する実装にしたので、
.data/.bss 初期化の前後どちらから呼ばれても正しい。
リンク結果は `addr2line` で**実体確認**済み（`PMU_instance at pmu_instance.c:63`＝
weak が負けて strong が採用された）。

> **この機序はチップ非依存**＝C3/C6/S3 で同じことをするなら**必ず同じ罠**を踏む。
> 「stock の init 関数を ASP3 の `hardware_init_hook()` から呼ぶ」型そのものの一般的注意点。

---

## 5. ★シム除去 A/B ＝「外せる。ただし **pmu_init のおかげではない**」

タスクの成功基準2（「`esp_shim_modem_icg_init` 等を無効化しても scan が通れば、
その場しのぎを正攻法が置き換えた**強い証拠**」）に対する回答。

`-DASP3_C5_NO_MODEM_ICG_SHIM=ON`（診断専用・既定OFF）を追加。**1実験1機構**＝
無効化するのは `esp_shim_modem_icg_init()` のみ（直後の
`modem_clock_select_lp_clock_source()`＝WIFIPWR系は残す）。
除去が本当に効いているかは**命令レベルで確認**（シム有りは `wifi_clock_enable_wrapper`
冒頭に `lui a5,0x600b0; sw a4,12(a5)`＝0x600B000C 書込みと 0x600A9C0C／0x600AF000 書込みが
並ぶ。シム無しビルドではこれらが消えて `wifi_module_enable` へ直行）。

### 5.1 2×2 マトリクス（全アーム **真のcold**・同一セッション・back-to-back）

| # | `ASP3_C5_PMU_INIT` | シム | **実機結果** |
|---|---|---|---|
| 1 | OFF | ON（現状の出荷構成） | **20 APs found (err=0)**・異常0 |
| 2 | **ON** | ON | **20 APs found (err=0)**・異常0 |
| 3 | **ON** | **OFF** | **20 APs found (err=0)**・異常0 |
| 4 | OFF | **OFF** ←★**決定的対照** | **20 APs found (err=0)**・異常0（**4/4 再現**：cold×2＋RTS×2） |

### 5.2 結論：**因果は pmu_init に帰属しない**

「シムを外しても通った」（#3）だけを見て「**pmu_init がシムを置き換えた**」と結論するのは
**誤り**である。**#4（pmu_init OFF のままシムだけ外す）でも同じく 20 APs 通る**からだ。
⇒ `esp_shim_modem_icg_init()` は **現時点では単に冗長（dead weight）**であり、
その冗長性は **pmu_init とは独立**に成立している。

**なぜ冗長か**は §3.1 が説明する：シムの PMU 側の仕事＝`icg_modem.code=2` は、
**誰も書かなくても起動時点で既に `0x600B000C = 80000000`（code=2=ACTIVE）**（§2.3 の予測が的中）。
シムの MODEM_SYSCON/LPCON 側の仕事も、現在は `modem_clock.c` 側が満たしている。
⇒ 実施13 が書いた「Direct Boot では `icg_modem.code` が 0 のまま残る」は、**現在の
コードベースでは既に成立していない**（実施42/43 の APM 解除や v5.5.4 供給移行など、
実施13 以降の変更のいずれかが前提を変えたとみられる——**どれが効いたかは本ラウンドでは未特定**）。

> ★これは [[c6-bt-handoff-success]] が犯した「**別binary/別セッションの対照は対照にならない**」
> ／「相関を因果と早合点する」誤りと**同型の罠**であり、**#4 を同一セッション・真のcoldで
> 実際に走らせた**ことでのみ回避できた。#4 を省いていれば「pmu_init がシムを置き換えた」
> という**誤った成功譚**を報告していた。

---

## 6. 非回帰（厳守事項）

| 項目 | 結果 |
|---|---|
| **既定** | `ASP3_C5_PMU_INIT` **OFF** のまま（昇格せず．§7） |
| **scan（pmu_init ON・真のcold）** | **`20 APs found (err=0)`**・異常マーカー0件 |
| **scan（pmu_init OFF・対照．同一セッション）** | `20 APs found (err=0)`・異常0（`<SSID-LAB-A2>` ch9 rssi=-56 の実在も確認） |
| **W1（pmu_init ON・真のcold）** | `event: STA_CONNECTED` → `net: DHCP bound ip=192.168.1.40 gw=192.168.1.1` → `IP acquired` → **`ping gateway -> OK` × 30 / NG 0**・異常0 |
| **`ninja -t deps` の hal 参照** | **0**（scan_on / scan_off / w1_on / pmu_on の全ビルド） |
| 素の sample1（WiFi/BT両OFF・既定OFF） | ビルド rc=0（**非回帰**） |
| 素の sample1 ＋ `ASP3_C5_PMU_INIT=ON` | **configure時に FATAL_ERROR で明示的に弾く**（§6.1） |
| 可逆性 | `-DASP3_C5_PMU_INIT=OFF`（既定）で stock ソースのコンパイルも呼出しも完全に消える |

### 6.1 ★自分で入れた回帰を実測で見つけて潰した

当初 `ASP3_C5_PMU_INIT=ON` を WiFi/BT 無しビルドでも通す依存ブランチを書いていたが、
**実際に試したら壊れていた**（未解決include＝`soc/rtc.h`(pmu_init.c:22, ocode_init.c:8)・
`freertos/FreeRTOS.h`(regi2c_ctrl.c:10)・`rom/efuse.h`(efuse_ll.h:15)）。
これらは `esp_wifi_v8.cmake` の include パス群と shim の FreeRTOS スタブが供給しており
WiFi無しビルドには無い。**未検証の依存ブランチを増やすより明示的に弾く方が安全**と判断し、
`ESP32C5_WIFI` 必須の `FATAL_ERROR` にした（pmu_init はモデム/RFの電源用でWiFi/BT無しでは
そもそも用途が無い）。

### 6.2 ★発見した **pre-existing** な不具合（本オプションとは無関係．未修正）

**`-DESP32C5_BT=ON`（bt_smoke_c5）は現ブランチで既にビルド不能**：
```
hal/components/esp_hw_support/include/esp_private/esp_modem_clock.h:46:32:
  error: unknown type name 'shared_periph_module_t'
```
`ASP3_C5_PMU_INIT=OFF` でも**同一エラー**＝**本ラウンドの回帰ではない**（対照実測で確認）。
BT パスが esp-hal-3rdparty(`hal/`) 側の `esp_modem_clock.h` を拾っており、供給移行が
WiFi パスにしか適用されていないことに起因するとみられる。**供給移行の残作業**として要記録。

---

## 7. 結論と次の一手

### 7.1 pmu_init だけで足りたか

**問いの立て方に注意**：C5 Wi-Fi は本ラウンド開始時点で**既に動いていた**（scan 20AP・W1）。
∴「足りたか」＝「Wi-Fi が動くか」ではなく「**stock 相当の電源状態に到達したか**」で判定する。

| 主タスク（stock相当の電源状態） | **達成**。39語が POR/中途半端な値から stock の記述子値へ到達。**RF/アナログ電源 `0x600B0014` にも到達**（C6が未達だった当該アドレス）。eFuseトリムまで正しく読む |
|---|---|
| 従タスク（自前シムを外せるか） | **外せる。ただし pmu_init の効果ではない**（§5．#4対照で反証）。シムは現時点で**独立に冗長** |
| 非回帰 | **達成**（scan 20AP・W1 ping 30/30・hal参照0） |

**pmu_init の守備範囲外として残るもの**（＝「足りない」部分．§3.1 で実測）：
**PHYデジタル（0x600A0450/0x047C）と MODEM_SYSCON は pmu_init では 1 ビットも動かない**。
これは仕様どおり（stock でもこれらは `modem_clock.c` / `esp_perip_clk_init()` 側の担当）で
あって欠陥ではないが、[[c5-wifi-modem-domain-unpowered]] が
「**残壁 = RF/アナログ電源 = pmu_init の残り**」と書いていた見立てに対しては、
**「pmu_init を完全に実行しても PHYデジタルは起動時点では無給電のまま」**という
**限定を付ける実測**になった（当時の残壁＝RX IQ較正ハングは、その後 実施42/43 の
APM/HP-TEE 解除で別の真因が特定され解決済み＝本ラウンドはその整理の追認でもある）。

### 7.2 既定を ON へ昇格するか → **本ラウンドでは昇格しない（OFF 維持）**

- 昇格に賛成の材料：stock 忠実度が上がる／実機非回帰（scan・W1）を cold で確認済み／
  eFuseトリムを正しく読む／将来のスリープ・省電力対応の前提。
- 昇格を見送る材料：**機能上の便益が実測でゼロ**（4アームすべて 20 APs＝挙動不変）。
  **BT 構成が未検証**（§6.2 の pre-existing 不良でそもそもビルドできず A/B 不能）。
  「壊さないことが最優先」の方針下で、便益ゼロ・リスク非ゼロの既定変更は割に合わない。
- ⇒ **既定 OFF のまま、可逆 option として温存**し、**昇格の可否はコーディネータへ返す**。

### 7.3 次の一手（優先順）

1. **`esp_shim_modem_icg_init()` の削除**（pmu_init とは独立の話）。§5 で**冗長性が実測確定**
   したので、この「その場しのぎ」自体を消せる。ただし**なぜ冗長になったか（実施13以降の
   どの変更が前提を変えたか）が未特定**なので、消す前にその特定を推奨（さもないと
   「なぜか動いている」状態を温存することになる）。
2. **§6.2 の C5 BT ビルド不良の修正**（供給移行の残作業）。これが直れば BT でも pmu_init の
   A/B ができ、既定 ON 昇格の判断材料が揃う。
3. C6 へ本ラウンドの型（stock verbatim ＋ weak `PMU_instance()` 上書き ＋ `.data` 初期化前
   問題の回避）を持ち込む。**ただし C5 の結果から C6 の cold-PLL 解消を予断しない**
   （§3.2 の限定）。C6 は現在このPCに未接続＝本ラウンドでは着手不可。

