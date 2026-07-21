# C5 evidence-03 — ブート方式の**決定**：seam は C5 で成立する。だが **Direct Boot を継続**する

日付: 2026-07-16 ／ branch: `claude/c5-espidf-supply-migration`
DUT: **ESP32-C5 #2**（BASE MAC `<MAC-39>`, hub **port5**, `ttyACM5` / `ttyUSB2`）
toolchain: ASP3側 = Espressif `riscv32-esp-elf` esp-15.2.0 ／ bootloader側 = esp-14.2.0（IDF v5.5.4の指定版）

evidence-02 §8「本ラウンドでは決定しない・判断はコーディネータへ差し戻す」への回答。
PROMPT.md §「★ブート方式は『あなたが決める』」の完了条件＝**採用方式と根拠の記録**。

---

## 0. 結論（先に）

| 問い | 答え |
|---|---|
| **seam は C5 で成立するか** | **成立する**（実機実証）。`hello` banner＋タスク実行＋`wifi_scan` 20 AP まで到達 |
| **`esp_app_desc`/efuse チェックは生じるか** | **生じる**（PROMPT.mdの賭けは当たり）。app_desc 無しでは通らない。実装して通した |
| **seam は stock の pmu_init を landing させるか** | **させない**。★仮説は**反証**された（§4） |
| **★採用判断** | **Direct Boot 継続**（既定 `ASP3_SEAM_BOOT=OFF` のまま）。seam は可逆optionとして温存 |

**判断の骨子**：seam は技術的には成立するが、**本タスクが期待した唯一の非自明な便益
（stock の PMU/アナログ初期化を landing させ、記録済みの真因を構造的に消す）が
実機A/Bで否定された**（§4）。残る便益（実運用フロー一致・OTA/partition 連携）は
C5 の現要件に無く、コスト（flash 3 分割・bootloader 依存・ページ跨ぎ制約・
イメージ規約への追従）は常時かかる。⇒ **今は採らない。必要になった時に option を ON にできる形で残す。**

---

## 1. 実験1：seam は C5 で成立するか → **成立**

### 1.1 実機生ログ（`hello`＝sample1．UART0．真のcold後）

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
entry 0x4084bbaa
I (23) boot: ESP-IDF v5.5.4 2nd stage bootloader
I (24) boot: chip revision: v1.0
I (25) boot: efuse block revision: v0.3
I (65) boot:  2 factory          factory app      00 00 00010000 00100000
I (75) esp_image: segment 0: paddr=00010020 vaddr=42010020 size=01c40h (  7232) map
I (84) esp_image: segment 1: paddr=00011c68 vaddr=00000000 size=0e3d0h ( 58320)
I (99) esp_image: segment 2: paddr=00020040 vaddr=42000040 size=053f0h ( 21488) map
I (104) boot: Loaded app from partition at offset 0x10000
I (104) boot: Disabling RNG early entropy source...

TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 16 2026, 19:55:31)
Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
...
Sample program starts (exinf = 0).
task1 is running (001).   |
task1 is running (002).   |
```

**判定基準（タスク指定）＝「ASP3カーネルのbannerが出れば seam 成立」→ 到達**。
真のcold（`off 5`→読み戻しで device count=0／Vbus 0.00V／`no device` を確認→`on 5`）× **3回**で
`task1 is running` まで**バイト単位で同一**（md5 `86fc874e…` ×3）。

### 1.2 seam で `wifi_scan` も到達（Direct Boot と同じ里塚）

```
I (175) boot: Loaded app from partition at offset 0x10000
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 16 2026, 20:04:31)
wifi_scan: esp_wifi_start -> 0
wifi_scan: esp_wifi_scan_start -> 0
wifi_scan: 20 APs found (err=0)
  [0] <SSID-INST-1X> (rssi=-44 ch=6)
```
異常マーカー（Illegal instruction/Guru/TG0_WDT/panic/access fault）＝**0件**、
観測窓14秒で ESP-ROM banner ＝**1回**（リブートループ無し）。

### 1.3 ★C5固有で実際に要ったもの（＝実測で特定した仕様）

PROMPT.md／evidence-02 §8.3 の想定と**4点違った**。いずれもソース実測→実機で確認。

| # | 引き継ぎ/PROMPTの想定 | **C5の実際** | 根拠 |
|---|---|---|---|
| 1 | bootloader@**0x0** | **0x2000** | `bootloader/Kconfig.projbuild:11`「default 0x2000 if IDF_TARGET_ESP32P4 \|\| **IDF_TARGET_ESP32C5** \|\| IDF_TARGET_ESP32H4」＝先頭2セクタは key manager(AES-XTS)用に予約 |
| 2 | （言及なし） | **フラッシュセグメントはちょうど2つ必須** | C5は `SOC_MMU_DI_VADDR_SHARED=1`（soc_caps.h:326）＝D/I vaddr共有（`SOC_DROM_LOW==SOC_IROM_LOW==0x42000000`, soc.h:148-153）→ bootloaderは共有vaddr版 `unpack_load_app()` を使い **`assert(rom_index == 2)`**（bootloader_utility.c:843）。Direct Boot版ldの「.textにcode+rodata同居＝1セグメント」はこれに抵触 |
| 3 | app_desc を「segment#0 に置く」 | **出力セクション名が厳密に `.flash.appdesc` でなければならない** | esptool `bin_image.py:805-811`「Patch to support ESP32-C6 union bus memmap」＝**この名前のセクションだけ**をフラッシュセグメント列の先頭へ移す特別扱い |
| 4 | app_desc の efuse フィールドは「offset 92/94付近」（S3 HANDOFF） | **176/178** | 4+4+8+32+32+16+16+32+32=176。`seam_appdesc.c` の `_Static_assert` が実際にこの誤りを検出（最初 188 と書いて失敗） |

### 1.4 ★flash cache/MMU の再初期化は **不要**（チップ依存の答え）

タスクが「S3(LX7)は要った／classicは不要＝チップ依存。C5がどちらかは**実測**」と
名指しした点。**C5は classic 側＝bootloader の map だけで足りる**。
`flash_xip_map()` 相当を一切足さずに IROM 命令フェッチが成立した（§1.1 の banner が証拠）。
S3 で必要だった `ICacheモード再設定＋SHUT_CORE0/1_BUS クリア`（seam-S3 HANDOFF 更新4）に
相当する手当ては **C5では一切不要**だった。

---

## 2. 実験2：`esp_app_desc` / efuse blk rev チェックの要否 → **生じる（PROMPT.mdの賭けは当たり）**

### 2.1 静的（ソース実測．測定前に予測を固定）

```c
/* esp_image_format.c:769-778 */
if (segment == 0 && !is_bootloader(metadata->start_addr)) {
#if !CONFIG_IDF_TARGET_ESP32                    ← C5は「esp32 classic」ではない＝実行される
    const esp_app_desc_t *app_desc = (const esp_app_desc_t *)src;
    ret = bootloader_common_check_efuse_blk_validity(
              app_desc->min_efuse_blk_rev_full, app_desc->max_efuse_blk_rev_full);
```
⇒ **予測：C5は efuse blk rev 検証を受ける。**
一方 `mmu_page_size` の magic word 検証（同 :835 `#if SOC_MMU_PAGE_SIZE_CONFIGURABLE`）は
**C5に当該マクロが無いため受けない** ⇒ 予測：magic word エラーは出ない。

### 2.2 実機（予測どおり）

- 有効な app_desc を置いた結果：`I (25) boot: efuse block revision: v0.3` が出て
  **efuseエラー無し・`Loaded app from partition at offset 0x10000` に到達**。
- 値の設計：`min_efuse_blk_rev_full=0`（`IS_FIELD_SET(0)` が偽＝minチェックskip,
  bootloader_common_loader.c:104）／`max_efuse_blk_rev_full=0xFFFF`（revision <= 65535 で必ずpass, 同:110）。
- S3 では app_desc 不在時に `Image requires efuse blk rev >= v5.24 / <= v0.32, but chip is v1.4`
  → `No bootable app partitions` が実際に出た（seam-S3 HANDOFF）。C5でも同じ検証コードが走る。

### 2.3 実装上の落とし穴（実測）

`-Wl,-u,asp3_seam_app_desc` が**必須**。`ASP3_TARGET_C_FILES` は静的ライブラリ（libasp3.a）へ
入るため、どこからも参照されない app_desc は**アーカイブメンバごとロードされない**
（ldの `KEEP` はロード済みセクションのgcを止めるだけで、未ロードのメンバは救えない）。
`-u` 無しでは elf2image が
`Contents of segment at SHA256 digest offset 0xb0 are not zero`（0xb0 = ファイル先頭0x20 + app_elf_sha256 offset 144）
で停止した。

---

## 3. 実験3：便益とコストの秤（C5の実測値）

| 観点 | Direct Boot（既定） | seam |
|---|---|---|
| 実機里塚 | scan 20AP・**W1 GOT IP + ping 34/34**（§5） | `hello` 3/3・scan 20AP（W1未実施） |
| flash レイアウト | **app 1個**（`0x0`） | **3個**（bootloader `0x2000` / ptable `0x8000` / app `0x10000`） |
| イメージ生成 | `objcopy -O binary`（1行） | `esptool elf2image`＋app_desc＋`-u`＋2セグメント分割ld |
| ld の制約 | 単一FLASH領域でよい | **text/rodata を別64KBページへ強制分割**（`assert(rom_index==2)`）・vaddr下位16bitを`0x20`起点に・セクション名 `.flash.appdesc` 固定 |
| 外部依存 | 無し | **esp-idf bootloader のビルド**（IDF指定の esp-14.2.0 toolchain＋idf.py 環境が別途要る） |
| イメージ規約への追従 | 不要 | esptool/bootloader の内部仕様（上表 §1.3）に追従し続ける必要 |
| **2パスリンク** | 不要 | **C5では不要だった**（S3(LX7)は必要）。C5はページ跨ぎをldの `ALIGN(…,0x10000)+0x20` で解けるため |
| OTA/partition 連携 | 不可 | 可（ただし**現要件に無い**） |
| PMU/アナログ初期化 | pmu_init 走らない | **pmu_init 走らない（同じ）** ← §4 |

**秤の結果**：seam の便益のうち「実運用フロー一致・OTA」は C5 の現在の要件に無く、
唯一の非自明な便益として期待された「stock init の landing」は §4 で**否定**された。
一方コストは恒常的。⇒ **今は割に合わない。**

---

## 4. ★実験4：seam は stock の PMU/アナログ初期化を landing させるか → **させない（仮説の反証）**

本タスクの「本当の狙い」に対する直接プローブ。**予測を測定前に固定してから測った。**

### 4.1 予測（ソース実測に基づき、実機を触る前に固定）

`pmu_init()` の**唯一の呼出し元**：
```
esp_system/port/soc/esp32c5/clk.c:82   void esp_rtc_init(void) { pmu_init(); }
esp_system/port/cpu_start.c:566        esp_rtc_init();     // ← call_start_cpu0
```
＝**アプリ側の起動コード**であり 2nd-stage bootloader ではない
（`bootloader_support/src/esp32c5/bootloader_esp32c5.c` の `bootloader_init()` に pmu 参照は**皆無**）。

seam は「bootloader → **ASP3自前エントリ**」であり IDF の `cpu_start.c` を一切通らない。
⇒ **予測：seam でも pmu_init は実行されない。PMUレジスタは Direct Boot と同一になる。**
（もし差が出たらこの構造分析が誤り＝重要な反証情報、と事前に宣言した。）

### 4.2 測定（実機A/B．ASP3側コードは完全同一・ブート方式のみ反転）

`apps/boot_pmu_probe/`＝Wi-Fi初期化を一切呼ばない最小アプリ。PMU 80語＋MODEM_SYSCON 16語＋PHYデジタル3点。

**差は 3 語のみ。いずれも `*_HP_REGULATOR0_REG`（CPU電圧/DBIAS）＝`bootloader_clock_configure()` 由来で MODEM 電源とは無関係：**

| offset | レジスタ名 | Direct Boot | seam |
|---|---|---|---|
| PMU+0x28 | `PMU_HP_ACTIVE_HP_REGULATOR0_REG` | `c667f180` | `de67f180` |
| PMU+0x5c | `PMU_HP_MODEM_HP_REGULATOR0_REG` | `c6678000` | `de678000` |
| PMU+0x9c | `PMU_HP_SLEEP_LP_REGULATOR0_REG` | `c6600000` | `ee600000` |

**記録済み真因に関わるレジスタは全てビット単位で同一＝seam は何も直さない：**

| 観測点 | Direct Boot | seam | 意味 |
|---|---|---|---|
| PMU+0x00 `PMU_HP_ACTIVE_DIG_POWER_REG` | `00000000` | `00000000` | 同一 |
| **PMU+0x0c `PMU_HP_ACTIVE_ICG_MODEM_REG`** | **`80000000`** | **`80000000`** | **POR。MODEM ICG は両方とも未設定** |
| MODEM_SYSCON `0x600A9C00`〜 全16語 | 同一 | 同一 | modem clocking 同一 |
| PHYDIG `0x600A0450` | `00000000` | `00000000` | **PHYデジタルブロックは両方とも等しく無給電** |
| PHYDIG `0x600A047C` | `00000000` | `00000000` | 同上 |

### 4.3 結論

**★仮説「seam なら stock の pmu_init/アナログ初期化が landing する」は反証された。**

記録済み真因（[[c5-wifi-modem-domain-unpowered]]／[[c6-bt-handoff-success]]）の
「**Direct Boot が stock の pmu_init を飛ばす**」という表現は、**ブート方式の問題ではなく
「IDF の app 起動層（`esp_system/port/cpu_start.c`）を通らないこと」の問題**である。
seam はブートローダを実物に替えるだけで app 起動層は依然 ASP3 自前なので、**構造的に何も変わらない**。

⇒ この真因を消したいなら、正しい方向は **seam化ではなく `pmu_init()` 相当の移植**
（`esp_hw_support/port/esp32c5/pmu_init.c` の HP_ACTIVE 電源モード記述子＝
`pmu_param.c` の `PMU_HP_MODEM_POWER_CONFIG` 等）である。これは
[[c5-wifi-modem-domain-unpowered]] が既に「次ラウンド」として書いていた方向と一致し、
**ブート方式の選択とは独立**に実施できる。

> **※適用範囲の限定（厳守）**：本測定は **C5 でのもの**。C6 の cold-PLL 解消を主張する
> 根拠にはしない（別チップ・別壁）。主張できるのは「**stock init は seam では landing しない**」
> という機序のみ。ただしこの機序は「pmu_init の呼出し元が app 側」という
> **チップ非依存の構造**（C6も `esp_system/port/soc/esp32c6/clk.c` の `esp_rtc_init()`＋
> 同じ `cpu_start.c:566` 経由）に由来するため、**C6 で seam を試す動機は本測定により大きく下がる**。

### 4.4 自前シム除去A/Bについて

タスクは「seam なら自前PMUシム（`esp_shim_modem_icg_init` 等）を外しても Wi-Fi が動くか」も求めていた。
§4.2 で **`PMU_HP_ACTIVE_ICG_MODEM_REG` と PHYデジタルが両方とも同一の無給電状態**である
ことが直接測れたため、シム除去は「動かない」以外にあり得ず、**より上流のレジスタ実測で
既に決着している**（除去A/Bは同じ結論の間接確認にしかならない）。
なお seam でも `wifi_scan` が 20 AP 通る（§1.2）＝**シムが仕事をしている**ことは実証済みで、
これも §4.2 と整合する。

---

## 5. 非回帰（厳守事項）

| 項目 | 結果 |
|---|---|
| 既定ブート方式 | **Direct Boot のまま**（`ASP3_SEAM_BOOT` 既定 OFF） |
| Direct Boot の ld / flash_header | `git diff 51c58fa..HEAD -- esp32c5.ld flash_header.S` ＝ **空**（無改変） |
| **Direct Boot ビルドのバイナリ同一性** | 同一ソースパスで pre-change(51c58fa) と現HEADを再ビルド → **`.text` サイズ完全一致（`0x71630`）**、`.data`(`0x1cf8`)・`.bss`(`0x47628`) も一致 |
| Direct Boot `wifi_scan` 実機 | **20 APs found (err=0)**・異常マーカー0件・`boot:0x18 (SPI_FAST_FLASH_BOOT)` |
| Direct Boot **W1** 実機 | `event: STA_CONNECTED` → `net: DHCP bound ip=192.168.1.40 gw=192.168.1.1` → `IP acquired` → **`ping gateway -> OK` × 34 / NG 0**・TG0_WDT 0件 |
| **hal 参照（供給移行の成果）** | `ninja -t deps \| grep -c esp-hal-3rdparty` ＝ **0**（Direct Boot・seam 両方） |
| 可逆性 | `-DASP3_SEAM_BOOT=ON` で seam、既定OFFで Direct Boot。切替は ld・app_desc・イメージ生成のみに閉じ、カーネル/arch/チップ依存部/Wi-Fi供給に一切触れない |

> ※`.text` サイズ比較で最初 0x280 の差が出たが、これは **`__FILE__` パス長の交絡**
> （一時worktreeの長いパスが .rodata に載る）であり、同一パスで再測して一致を確認した。
> **交絡を潰してから同一性を主張すること**の実例。

---

## 6. 実機作業で踏んだ罠（次の人向け）

1. **★C5のlatch**（[[c5-latched-board-state]]）は本ラウンドで**実際に3回**発生した。
   `rst:0x7 (TG0_WDT_HPSYS)` ＋ `Core0 Saved PC:0x40038598` を見たら**コードを疑う前に電源再投入**。
   seam の初回評価で「WDTリブートループ＝seam失敗」と誤断しかけたが、真のcold後は **3/3 で正常起動**した。
   `off 5` → **読み戻しで device count=0／Vbus 0.00V／`no device` を確認** → `on 5`。
2. **★UART採取ハーネスの自傷**：CP2102N(`ttyUSB2`) で `ser.rts = True` を**残したまま**採取すると
   **EN を assert し続けて chip を reset に保持**する。ログが必ず同じ位置で切れる＝ハーネス由来の
   アーティファクトであってDUTの挙動ではない。**パルス後は `rts = False` に戻すこと**。
   （[[c5c6-wifi-blob-unify-v554]] の「double-resetハーネスがpc=0を誘発」と同型の教訓。）
3. **★`-DWIFI_SSID=` を cmake に渡しても効かない**。`WIFI_SSID` は
   `apps/wifi_dhcp/wifi_dhcp.h` の**Cプリプロセッサ文字列マクロ**（`#define WIFI_SSID "your-ssid"`）で、
   **どの cmake ファイルにも plumbing が無い**。cmake の `-D` は誰も読まないキャッシュ変数を作るだけで、
   ビルドは既定の `"your-ssid"` のまま＝`STA_DISCONNECTED reason=201 (NO_AP_FOUND)` になる。
   正しくは `-DCMAKE_C_FLAGS='… -DWIFI_SSID=\"…\" -DWIFI_PASSWORD=\"…\"'`（**内側の引用符をエスケープ**）。
4. **★APが変わっている**：evidence-02 の W1 は `…_A`／ch44(5GHz) だったが、本ラウンドの実測では
   その SSID は**電波に存在せず**、実在するのは **`…_A2`／ch9(2.4GHz)**（scan で rssi=-52 を実測）。
   `reason=201` を「回帰」と誤断しないこと。**まず scan で AP の実在を確認する**。
5. **flashレイアウトを変える実験では 0x0 の Direct Boot マジックを必ず消す**。
   消さずに bootloader を 0x2000 に置くと ROM が 0x0 の古い ASP3 を direct boot してしまう。
   `erase-flash` してから3枚書く。Direct Boot への復帰は `erase-flash` → `write-flash 0x0 asp_flash.bin`。

---

## 7. 成果物

| ファイル | 役割 |
|---|---|
| `asp3/target/esp32c5_espidf/esp32c5_seam.ld` | seam用ld（text/rodata 2セグメント分割・`.flash.appdesc`・ページ跨ぎ） |
| `asp3/target/esp32c5_espidf/seam_appdesc.c` | `esp_app_desc`（`_Static_assert` でoffsetを機械照合） |
| `asp3/target/esp32c5_espidf/target.cmake` | option `ASP3_SEAM_BOOT`（既定OFF）・`-u` 注入 |
| `asp3/target/esp32c5_espidf/run.cmake` | seam時 `elf2image`／既定は従来の `objcopy` |
| `apps/boot_pmu_probe/` | 実験4のA/Bプローブ（Wi-Fi非依存のPMU/MODEMダンプ） |

### seam の再現手順（必要になったとき）

```bash
# 1) bootloader/ptable（IDF指定の esp-14.2.0 が要る点に注意）
export IDF_PATH=<repo>/esp-idf; export IDF_TOOLS_PATH=$HOME/tools/espressif
source ~/tools/espressif/python_env/idf5.5_py3.12_env/bin/activate
export PATH="$HOME/tools/espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$IDF_PATH/tools:$PATH"
idf.py set-target esp32c5 && idf.py build     # 最小プロジェクトでよい

# 2) ASP3 app（seam）
cmake … -DASP3_SEAM_BOOT=ON -DESP32C5_ESPTOOL=<esptool> …   # → build/<tag>/asp_seam.bin

# 3) 書込（★0x0のDirect Bootマジックを消すため erase-flash 必須／bootloaderは0x2000）
esptool --chip esp32c5 --port <port> --after no-reset erase-flash
esptool --chip esp32c5 --port <port> --after no-reset write-flash \
  0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 asp_seam.bin
```

---

## 8. 次の一手（ブート方式とは独立）

- **`pmu_init()` 相当の移植**（§4.3）。これが C5 の RX IQ 較正ハングと C6 の cold-PLL 壁に対する
  **正しい方向**であり、seam 化では代替できないことが本ラウンドで確定した。
  対象＝`esp_hw_support/port/esp32c5/pmu_init.c` の HP_ACTIVE 電源モード記述子
  （`pmu_param.c` `PMU_HP_MODEM_POWER_CONFIG` 等）。
  なお `target_kernel_impl.c` には既に `esp32c5_r35_pmu_hp_modem_bank_fixed()` /
  `esp32c5_r35_pmu_hp_sleep_bank_fixed()` が**未使用のまま存在**しており、着手点になりうる。
- seam を将来採る動機があるとすれば **OTA/partition 連携が要件化したとき**のみ。
  その時は本ラウンドの option をONにすれば足りる（実機実証済み）。
