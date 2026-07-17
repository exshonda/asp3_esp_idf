# evidence-stock-01 — stock ESP-IDF bleprph を対照に立てる（C0＝C6／主対照＝C5×Android）

日付: 2026-07-17 ／ 担当: stock-IDF 対照エージェント
ミッション: **ユーザー指示「ESP-IDFで同様のBLEのアプリを作成して実験したい」**
DUT: **C6 `14:C1:9F:E0:5A:9C`（`1-6.2.3`／電源 `-p 2`）** ・ **C5#1 `d0:cf:13:f0:a7:44`（`1-6.4`／電源 `-p 3-4`）**
★**C3（`1-6.1` / `-p 1`）は別エージェントが作業中＝一切触っていない。hub `1-5` も触っていない。**
repo HEAD（本ファイル作成時）: `9d48f8f` ／ branch: `claude/c5-espidf-supply-migration`
esp-idf submodule: **`735507283d` = 真の v5.5.4 タグ**（`git submodule status` で clean を確認）

---

## 0. なぜこの実験が決定的か（設計の核）

本日までの全 A/B は **「我々のポートの中」** を振っていた（供給・toolchain・ISA・app の flush）。
**stock IDF アプリは「我々のポート全体」を変数から外す**（FreeRTOS・IDF 自前の起動・idf.py・
IDF 自身の NimBLE config・我々の shim/OSAL が全部消える）。

| 結果 | 意味 |
|---|---|
| **stock × C5 × Android = OK** | 問題は **「我々の統合」**（shim/OSAL/起動/config）にある。チップでも blob でもない |
| **stock × C5 × Android = NG** | 問題は **「上流」**（Espressif のスタック/コントローラ/blob）＝我々のバグではない |

**どちらでも探索空間が半分に切れる ⇒ 結果を「期待しない」。**

---

## 1. どう作ったか（★stock を stock のまま）

### 1.1 ベース＝IDF 自身の BLE peripheral サンプル

`esp-idf/examples/bluetooth/nimble/bleprph`（**submodule 内＝無改変**）。
**`tmp/stock_bleprph/` へ `cp -r` した out-of-tree コピー**で作業（submodule を汚さないため。
CLAUDE.md 禁則＋ミッション指示）。`diff -r` で **コピー直後はバイト同一**であることを確認済み。

**`git submodule status esp-idf` は本ラウンドを通じて clean**（`735507283d` のまま・dirty 行 0）。

### 1.2 ★stock から変えた点の「全」列挙（＝これで全部。★§8 のユーザー指摘で 2 件増えた）

`diff -r <submodule の例> tmp/stock_bleprph` の実測：**stock ファイルへの編集は `main.c` の 1 行だけ**、
残りは **新規ファイル 3 個**（`sdkconfig.ctl` / `sdkconfig.ctl.esp32c5` / `sdkconfig.ctl.esp32c6`）。
適用は `-D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ctl"`
（**IDF の規則＝各エントリについて `<file>` → `<file>.<target>` の順に後勝ち**＝
`kconfig.cmake:173-178` を読んで確認。**推測ではない**）。

| # | 変更 | 種別 | なぜ必要か（★正当化できないものは 1 件も無い） |
|---|---|---|---|
| **D-1** | `CONFIG_EXAMPLE_BONDING=y` | 例の stock Kconfig | 既定 n ＝ **bond しない**⇒ 試験対象が存在しないビルドになる |
| **D-2** | `CONFIG_EXAMPLE_ENCRYPTION=y` | 例の stock Kconfig | 既定 n ＝ **暗号必須特性が生えない**⇒ 同上 |
| **D-3** | `CONFIG_EXAMPLE_EXTENDED_ADV=n`＋`CONFIG_BT_NIMBLE_EXT_ADV=n` | 例/IDF の stock Kconfig | ★§8.2（**測定不能を解消**・**交絡除去**・**名前を Kconfig 化**） |
| **D-4** | `CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="IDFCTL-C5"/"IDFCTL-C6"` | IDF の stock Kconfig | ★§8.1（**別プロジェクトとの名前衝突回避**＝ユーザー指摘） |
| **D-5** | `main.c:617` の 1 行 | ★**唯一のソース変更** | ★§8.3（**Kconfig が効かない**ことを実測。これが無いと D-4 が無効） |

**`gatt_svr.c`・`CMakeLists.txt`・`sdkconfig.defaults` ほか stock ファイルは 1 バイトも変えていない**
（`diff -r` で確認）。**我々の config は 1 個も輸入していない**（D-1〜D-4 はすべて **例/IDF 自身の Kconfig**）。

### 1.3 ★stock 既定のうち「我々と違う」もの（＝結果の解釈に効く。隠さない）

`CONFIG_EXAMPLE_*` の既定をそのままにした結果、**stock は我々と次の点で違う**：

| 項目 | stock（本ビルド） | 我々（`ble_host_smoke_c5/c6`） | 効きうるか |
|---|---|---|---|
| **`ble_hs_cfg.sm_sc`** | **0（レガシーペアリング）** | **1（LE Secure Connections）** | ★**効きうる**（§6 の交絡） |
| **`sm_our/their_key_dist`** | **ENC のみ** | **ENC \| ID（IRK 配布）** | ★**効きうる**（RPA 経路） |
| **`MYNEWT_VAL_BLE_EXT_ADV`** | ~~1~~ → **0（D-3 で stock 既定から変更）** | **0（レガシー adv）** | ★**§8.2 で «一致» させた**＝**交絡を1つ潰した** |
| `sm_io_cap` | NO_IO(3) | NO_IO | 同一 |
| `sm_mitm` | 0 | 0 | 同一 |
| `sm_bonding` | 1 | 1 | 同一 |

★**これは「stock を stock のまま使え」というミッション指示の直接の帰結**であって、
私の判断で我々の設定を輸入しなかった。**ただし交絡として §6 に明示的に登録する。**

### 1.4 toolchain（★「使ったはず」で済ませず実測）

`idf.py` が自分で選んだものを **build.ninja の実パスから測った**：

```
esp32c6: /home/honda/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/.../riscv32-esp-elf-gcc
esp32c5: 同上
         -> riscv32-esp-elf-gcc (crosstool-NG esp-14.2.0_20260121) 14.2.0
```

**IDF v5.5.4 の `tools/tools.json` が `recommended` とする版 = `esp-14.2.0_20260121`** と一致
＝**idf.py は IDF 標準 toolchain を選んだ**（親の想定どおり。**実測で確認**）。

`-march` は **両ターゲットとも `rv32imac_zicsr_zifencei`**（**我々の C6 は `rv32imc`**＝review D11 の非対称は
stock では発生しない。stock は C5/C6 で対称）。

★**測定の罠（自己申告①）**：`-march` を `build.ninja` に grep したら **0 件**だった。
**0 を「無い」と読まずに検出器を較正した**ところ、**IDF v5.5.4 はコンパイルフラグを
`@response` ファイル（`build_<t>/toolchain/cflags`）に間接化している**＝
**`build.ninja` に `march` の文字列は原理的に存在しない**。
`toolchain/cflags` と **リンク済み `.map` の 2 系統**で値を確認した。
⇒ **`build.ninja` の FLAGS 行を見る手法は IDF ネイティブビルドには当たらない**（我々の cmake には当たる）。

---

## 2. ビルド結果と、私が踏んだ「焼き間違い」の芽

### 2.1 両ターゲットともビルド成功

| target | app | bootloader offset | 備考 |
|---|---|---|---|
| esp32c6 | `bleprph.bin` 0x9a890 B | **0x0** | |
| esp32c5 | `bleprph.bin` 0xacc30 B | **0x2000** | esptool は `--no-stub` |

★**C5 と C6 で bootloader offset が違う**（0x2000 / 0x0）＝焼き込みコマンドを使い回さないこと。

### 2.2 ★IDF アプリは bootloader + partition table + app の3点セット

**ASP3 の Direct Boot（0x0 に単一イメージ）とレイアウトが根本的に違う**⇒
**上書きすると ASP3 は戻らない**。§3 の復旧手順を**先に検証してから**焼いた。

### 2.3 ★★測定の罠（自己申告②）＝「両方 esp32c5 ビルド」になりかけた

`idf.py -B build_esp32c6 …` と `-B build_esp32c5 …` を続けて実行したが、
**`-B` は sdkconfig を分離しない**：sdkconfig はプロジェクト直下に **1 個**しかなく、
**2 回目の `set-target esp32c5` が C6 の sdkconfig を黙って上書きした**。
実測（`grep CONFIG_IDF_TARGET`）：

```
プロジェクトの sdkconfig            -> CONFIG_IDF_TARGET="esp32c5"   ← C6 の設定は消えた
build_esp32c6/config/sdkconfig.json -> "IDF_TARGET": "esp32c6"       ← 焼いた物自体は C6
```

⇒ **成果物は正しかったが、「次に `-B build_esp32c6 build` を打った瞬間に C5 設定で再構成される」
状態だった**＝**`ESP32C3_QEMU` 既定 ON 事故（「両側に同じ誤りが入るので diff では捕まらない」）と
同じ型の芽**。**`-D SDKCONFIG=sdkconfig.<target>` で分離して両方を作り直した**。
以後 **ターゲットは «build ディレクトリのキャッシュ» と «per-target sdkconfig» の2系統で確認**する。

★**この罠に気づけたのは、config を «読んだ» のではなく «焼いたものが意図どおりか» を
独立に確認したから**（ミッションの指示どおり）。

---

## 3. ★ASP3 イメージへ「戻せる」ことの検証（★これが無いと実機が塞がる）

**ミッション最重要の前提条件。上書きの «前» に完了させた。**

### 3.1 現状の吸い出し（full flash dump）

| DUT | chip / rev | flash | dump | md5 |
|---|---|---|---|---|
| C6 `14:C1:9F:E0:5A:9C` | ESP32-C6FH4 rev **v0.2** | **4MB** | `tmp/asp3_flash_backup/c6_asp3_full_4MB.bin` | `2f74e55a7f4c3eb8d729a070797412cf` |
| C5 `d0:cf:13:f0:a7:44` | ESP32-C5 rev **v1.0** | **8MB** | `tmp/asp3_flash_backup/c5_asp3_full_8MB.bin` | `c29621d518f49d7f27d6f1b0221f8dee` |

★**MAC を esptool の `flash_id` で読んで個体を同定した**（memory の作法＝`~/usb_devices.md` で
同定しない・**MAC で引く**）。**C6 rev v0.2 = board C 本体**（memory の「存在しない v0.3 仮説」に注意）。

### 3.2 ★何が載っていたかを特定（dump と既存 build を突き合わせ）

**373 個の `build/*/asp_flash.bin` と dump 先頭を全件照合**した結果、**一意に 1 個ずつ**当たった：

| DUT | 載っていた ASP3 ビルド | サイズ |
|---|---|---|
| **C6** | **`build/gd_c6_ble/asp_flash.bin`**（07-17 15:36 ビルド） | 4MB（full-flash パディング済） |
| **C5** | **`build/c5_tc_A2_ble/asp_flash.bin`**（07-17 14:21 ビルド） | 4MB（C5 の 4MB 以降は **全 0xFF** を実測確認） |

⇒ **復旧経路は 2 系統ある**（dump／既存 build dir）。**どちらも一意に定まる。**

### 3.3 ★★復旧「手順」の実地リハーサル（＝上書き前に、実際に走らせた）

**dump を持っているだけでは «戻せる» の証明にならない**ので、
**同一内容を書き戻す no-op write** で **書き込み経路そのもの**を検証した：

```
esptool.py --chip esp32c6 -p <C6 by-id> -b 921600 write_flash 0x0 c6_asp3_full_4MB.bin
  -> Wrote 4194304 bytes ... Hash of data verified.
esptool.py --chip esp32c6 -p <C6 by-id> -b 921600 verify_flash 0x0 c6_asp3_full_4MB.bin
  -> -- verify OK (digest matched)      ★独立の読み戻し検証

esptool.py --chip esp32c5 -p <C5 by-id> -b 921600 write_flash 0x0 c5_asp3_full_8MB.bin
  -> Wrote 8388608 bytes ... Hash of data verified.
esptool.py --chip esp32c5 -p <C5 by-id> -b 921600 verify_flash 0x0 c5_asp3_full_8MB.bin
  -> -- verify OK (digest matched)
```

⇒ **C5・C6 とも「戻す」コマンド列は実機で実行済み・verify 済み**。
★**復旧に stock IDF 側の partition/bootloader を消す操作は要らない**
（ASP3 は 0x0 から full-flash イメージ＝**上書きで IDF のレイアウトごと消える**）。

**復旧コマンド（ユーザー／次エージェント用・そのまま貼れる）**：

```bash
ESPTOOL=~/.espressif/python_env/idf5.5_py3.12_env/bin/esptool.py
cd /home/honda/TOPPERS/ASP3CORE/asp3_esp_idf
# C6 を ASP3 へ戻す
$ESPTOOL --chip esp32c6 -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_14:C1:9F:E0:5A:9C-if00 \
    -b 921600 write_flash 0x0 tmp/asp3_flash_backup/c6_asp3_full_4MB.bin
# C5 を ASP3 へ戻す
$ESPTOOL --chip esp32c5 -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D0:CF:13:F0:A7:44-if00 \
    -b 921600 write_flash 0x0 tmp/asp3_flash_backup/c5_asp3_full_8MB.bin
```

★`/dev/ttyACM*` は電源断で番号が入れ替わる（別プロジェクトの S3 が同居）⇒ **by-id のみ**。
★`esptool | head -N` 厳禁（SIGPIPE が write-flash を切る）。

---

## 4. ★bond の永続性（NVS）は ASP3 と違うか — **実測：違わない（NULL）**

**親の想定「stock IDF は NVS 永続化が «有効» かもしれない＝ASP3 と bond の永続性が違いうる」は
実測で死んだ。**

| 指標（すべて **出力側**で測定） | esp32c6 | esp32c5 |
|---|---|---|
| `build_<t>/config/sdkconfig.h` に `CONFIG_BT_NIMBLE_NVS_PERSIST` が **define されている数** | **0** | **0** |
| ELF の `ble_store_config_persist_*` シンボル数 | **0** | **0** |
| （較正）ELF の `ble_store_config*` シンボル数 | **33** | **33** |
| （較正）`nvs_flash_init` | 1 | 1 |

★**「0 を読んだら測定対象が存在するか先に確かめろ」を実行した**：
- `ble_store_config_persist_*` は **ソースに実在**する（`ble_store_config.c:309,387,406,464,541` が呼ぶ）
  ⇒ **0 は «検出器が空振り» ではなく «リンクされていない» の意**。
- 同じ grep が `ble_store_config*` に **33 件当たっている**＝**検出器は生きている**。
- Kconfig 側の原文：`components/bt/host/nimble/Kconfig.in`
  ```
  config BT_NIMBLE_NVS_PERSIST
      bool "Persist the BLE Bonding keys in NVS"
      default n            ★★ IDF v5.5.4 の既定は n
  ```
- 機構：`esp_nimble_cfg.h:1097-1101` が `#ifdef CONFIG_BT_NIMBLE_NVS_PERSIST` で
  `MYNEWT_VAL_BLE_STORE_CONFIG_PERSIST` を 1/0 に振る（**memory が ASP3/C5 について記録した機構と同一**）。

⇒ **stock IDF bleprph も RAM-backed。ASP3 と同じ。**

★**含意は 2 つある**：
1. **測定手順は ASP3 と同一でよい**＝**電源断のたびにデバイスは鍵を失う**⇒
   **毎セル「両方の」スマホを forget → BT OFF/ON** が **stock でも必須**（例外なし）。
2. ★**memory の「C5 の `PERSIST=0`」は ASP3 の移植バグではなく IDF の既定そのもの**
   （「upstream syscfg.yml の既定は 1 なので «既定1のはず» と読むと誤る」という
   memory の警告と整合＝**Kconfig 既定 n が syscfg 既定 1 を上書きしている**）。

---

## 5. ★★stock IDF と我々の NimBLE config の差分（＝**未測定の軸**だった。ここが本ラウンドの静的成果）

review-ble-c5-vs-c6.md は **C5 vs C6（どちらも我々）で `MYNEWT_VAL_*` 414/414 一致**を示したが、
**stock vs 我々**は**誰も測っていなかった**。測った。

### 5.1 測り方（★review §5-a の罠を踏まないように）

**`-dM -E` は «定義テキスト» であって «値» ではない**（review §5-a：終端状態は `#if` が見た値ではない）。
実際 `-dM` 比較では `stock=CONFIG_BT_NIMBLE_EXT_ADV / ours=(0)` のような **未展開の名前**が並び、
**そのまま数えると嘘になる**。⇒ **2 パス法**：

1. `-dM -E` で **名前の集合**を取る（414）。
2. **実 TU（`ble_sm.c`）を `#include` したプローブ**を各ビルドの**実フラグ**で `-E -P` し、
   **マクロを実際に展開させて値を取る**。
3. **`#if` と同じ規則で数値化**（未定義識別子 → 0、括弧は無視）してから比較。

★**罠（自己申告③）**：最初 `-P` を付けず、**GCC が展開の途中に linemarker を挿入**して
値が改行で割れ、`stock=(` のような**壊れた値**を 40 件生成した。**生出力を見て修正**した
（`-P` 追加後は **414/414 が解決**）。**「見た目の差分」を報告する寸前だった。**

比較対象：`tmp/stock_bleprph/build_esp32c5`（compile_commands.json の実コマンド）
vs `build/nr_c5_ble`（review が使ったのと同じ我々の既定ビルド／`CMakeFiles/rules.ninja` の実コマンド）。
**両者とも toolchain は `esp-14.2.0_20260121`＝同一**（実測）。

### 5.2 結果：**stock vs 我々は同一ではない — 30 個（C5）／29 個（C6）が意味的に相違**

**`MYNEWT_VAL_*` 414 個中**（数値正規化後）：

| 分類 | 代表例 | stock | 我々 |
|---|---|---|---|
| **拡張アドバタイズ一式** | `BLE_EXT_ADV` | **1** | **0** |
| | `BLE_EXT_ADV_MAX_SIZE` | 1650 | 31 |
| | `BLE_LL_CFG_FEAT_LL_EXT_ADV` | 1 | 0 |
| **周期アドバタイズ一式** | `BLE_PERIODIC_ADV` ほか 5 個 | 1 | 0 |
| **★LL 暗号 feature** | **`BLE_LL_CFG_FEAT_LE_ENCRYPTION`** | **1** | **0** |
| | `BLE_LL_CFG_FEAT_LE_PING` | 1 | 0 |
| **GATT クライアント** | `BLE_GATTC` | 1 | 0 |
| **バッファ/メモリ** | `MSYS_1_BLOCK_SIZE` / `_COUNT` | 128 / 24 | 256 / 12 |
| | `BLE_TRANSPORT_EVT_SIZE` | 257 | 70（**C5 のみ**） |
| | `BLE_MAX_CONNECTIONS` | 3 | 4 |
| | `BLE_HS_FLOW_CTRL_ITVL` | 2000 | 1000 |
| その他 | `BLE_ERR_NAME`/`BLE_UTIL_API`/`BT_NIMBLE_MEM_OPTIMIZATION` ほか | 1 | 0 |

### 5.3 ★この差分から言えること／言えないこと

**言えること（数字のみ）**：

- **N-a：stock-vs-我々の差分は «ほぼチップ非依存»**
  ＝ **C5 で 30 個・C6 で 29 個**、**C5 のみの追加は `BLE_TRANSPORT_EVT_SIZE` の 1 個だけ**
  （C6 のみの追加は **0 個**）。
  ⇒ ★**「stock と我々の config が違うこと」それ自体は «C5 だけ落ちて C6 は通る» を説明しない**
  （**両チップにほぼ同じ差分が入っているのに C6 は通っている**）。
- **N-b：★SM/privacy/store 系の knob は stock と我々で «一致»**
  ＝ `BLE_SM_*` / `PRIVACY` / `RPA` / `BLE_STORE_*` / `BLE_HS_*` にマッチする **36 個中 34 個が同値**。
  相違は **`BLE_HS_FLOW_CTRL_ITVL`（2000/1000・SM ではない）** と
  **`BLE_STORE_MAX_EADS`（stock=未定義→0 / 我々=1・EADS＝暗号化広告データ store で bond とは別）** の 2 個のみ。
  ⇒ **`BLE_SM_SC`・`BLE_SM_LEGACY`・`BLE_SM_OUR_KEY_DIST`・`BLE_HOST_BASED_PRIVACY` 等は
  コンパイル時 knob としては stock と我々で同じ**。

**言えないこと（★重要）**：

- ★**N-b は「SM の設定が同じ」ではない**。**コンパイル時 knob が同じ**というだけで、
  **実行時の `ble_hs_cfg.sm_sc` は stock=0 / 我々=1**（§1.3）＝**振る舞いは違う**。
  **「414 中 34/36 一致」を «SM は同一» と読んではいけない。**
- **30 個の差分のどれかが Android 非対称に効くか**は **本ラウンドでは未測定**。
  N-a は「差分だけでは C5/C6 非対称を説明しない」と言うだけで、
  **「差分がチップと相互作用しない」ことは示さない**（例：`BLE_EXT_ADV=1` が C5 blob の
  ある経路とだけ噛む、という筋は否定できない）。

---

## 6. ★事前登録（★実機の «前» に commit する。これがこのファイルの主目的）

### 6.1 予測（数値）

| # | 予測 | 確率 | 根拠（正直に） |
|---|---|---|---|
| **P1** | **C0＝stock × C6 × Android が OK** | **85%** | 我々のポートですら C6×Android は通る（`0x5dc00011`）。bleprph は Espressif の主力サンプルで、C6 は出荷済みチップ＝上流で試験されているはず。**下げる理由**＝stock 既定の **EXT_ADV=1**（我々はレガシー adv）＝**電波の出方が我々と違う**ので、端末側の対応次第では «stock アプリ固有» の失敗がありうる |
| **P2** | **★主対照＝stock × C5 × Android が OK** | **60%** | **上げる理由**＝(1) Espressif は bleprph を C5 でも試験して出荷しているはず。(2) 我々の C5 は 4 セル中**唯一 pend_ring の周期 flush を持たない**（review D1）＝我々側に既知の欠落がある。(3) 我々の統合は shim/OSAL/直接ブート/独自 config と面が広い。**下げる理由**＝(4) ★**C5 blob（`libble_app.a`）は C6 と実体が違い、IRK change/restore 系 4 関数を欠く**（review D22）＝**stock も同じ blob を使う**ので、blob 起因なら **stock も落ちる**。(5) C5 は最新チップで IDF 側の成熟度が最も低い |
| **P3** | **stock が NG の場合、その失敗シグネチャが我々と «同型»**（接続成立 → 対向が pairing 中に切断） | **70%** | 同じ blob・同じ NimBLE host なら同じ落ち方をするのが自然。ただし stock は sc=0/ENC-only/EXT_ADV＝**経路が違う**ので別シグネチャもありうる |

### 6.2 ★分岐が何を意味するか（結果を見る «前» に固定する）

| C0（C6） | 主対照（C5） | 読み |
|---|---|---|
| **OK** | **OK** | ★**問題は «我々の統合» にある**（チップでも blob でもない）。**ただし §6.3 の交絡を潰すまで «我々のバグ» と断定してはならない** |
| **OK** | **NG** | ★**問題は «上流»（Espressif のスタック/コントローラ/blob）**。**ただし P3＝シグネチャ同型が必要**（違うシグネチャなら «別のバグ» であって我々の件の説明にならない） |
| **NG** | — | ★**stock アプリ側の問題＝C5 の結果は帰属に使えない**。**C5 を測る前に C0 を直す**（第一手＝`EXAMPLE_EXTENDED_ADV=n` でレガシー adv に戻して C0 再取得） |
| **NG** | **NG** | ★**「両方失敗＝環境＝帰属不能」**（ホストの `Pairable` OFF で既知良好すら bond できなかった前例と同型）。**A/B を比べずにベンチを直すのが正解** |

### 6.3 ★「この結果が出たら仮説を捨てる」反証条件

- **R1**：**C0 が NG なら、本ラウンドの C5 の結果は一切帰属に使わない**（陽性対照が死んでいる）。
- **R2**：**stock × C5 が OK でも、以下の交絡を潰すまで «我々の統合が原因» と書かない**
  （**stock は我々と 3 点で違う**＝§1.3）：
  1. **`sm_sc` 0 vs 1**（レガシーペアリング vs LE Secure Connections）
  2. **key dist ENC のみ vs ENC|ID**（IRK を配らない vs 配る）
  3. **EXT_ADV 1 vs 0**
  ⇒ **潰す手＝「stock コードのまま」`EXAMPLE_USE_SC=y` + `EXAMPLE_RESOLVE_PEER_ADDR=y`
  （＋必要なら `EXAMPLE_EXTENDED_ADV=n`）の第2アーム**。**これでも OK なら交絡は死に、
  帰属が «我々の統合» に確定する。NG になれば «config が効く» と分かる**（どちらでも収穫）。
- **R3**：**stock × C5 が NG でも、シグネチャが我々と違えば «同じバグ» と言わない**（P3 の検定）。
- ★**R4（反証条件そのものの検算）**：
  **「C0 OK ⇒ アプリ・ボード・端末・手順が健全」は仮説であって測定ではない。**
  独立に検算する手段を用意した＝**stock はコンソールログを持つ**（ASP3 の LP_AON マーカと違い、
  `CONNECT`/`PAIRING_COMPLETE`/`ENC_CHANGE`/`subscribe` を**デバイス側から逐語で読める**）。
  **C0 の «OK» は「ユーザーがスマホで見た」だけでなく「デバイスのログが bond 成立を言う」の
  2 系統で確認する**。**2 つが食い違ったら、どちらかの計器が壊れている**と読む。
  ★**ASP3 側のマーカ（`0x5DC0`/`0x5DE0` 共用・後勝ち）より stock のログの方が «証拠の質» が高い**
  （review §6：C5 は ENC が PAIR を上書きするので `0x5DC0` の不在は何も証明しない）
  ⇒ **本ラウンドは «計器の質» の面でも過去セルより強い**。

### 6.4 取らない対照と、その理由（no silent caps）

- **BlueZ（hci0）を使った事前検証はしない**：**別エージェントが C3 実機で作業中**であり、
  hci0 を掴むと**相手の測定を壊しうる**。⇒ **代わりにデバイス側コンソールログで健全性を測る**（R4）。
- **iPhone セルは取らない**：ミッションの指定は **Android**（C5×Android の失敗が最もきれい＝
  toolchain 交絡が 2 アームで排除済み）。**1 ビルド 1 端末**の原則にも従う。
- **C3 は対象外**（別エージェントが作業中・`-p 1` を叩かない）。

---

## 7. C0 セル ＝ stock × C6 × Android（★準備完了・ユーザー確認待ち）

### 7.1 載っているもの（★「焼いたものが意図したものか」を独立に確認した）

| 項目 | 実測 |
|---|---|
| DUT | **C6 `14:C1:9F:E0:5A:9C`**（ESP32-C6FH4 rev v0.2・`1-6.2.3`） |
| **広告名** | **`IDFCTL-C6`** |
| **BLE アドレス** | **`14:C1:9F:E0:5A:9E`**（＝base MAC `…5A:9C` **+2**。★**名前でなく MAC が個体の証拠**） |
| ビルド | `tmp/stock_bleprph/build_esp32c6`（stock IDF v5.5.4 bleprph＋§1.2 の delta） |
| 焼込み | bootloader `0x0` / partition `0x8000` / app `0x10000`（**3点セット**） |
| **読み戻し検証** | **3/3 とも `verify OK (digest matched)`**（`Hash of data verified` とは**別に**独立実行） |

### 7.2 ★真cold の証明（＝by-id 消滅の読み戻し。`rst:0x1` は証明にならない）

```
uhubctl -l 1-6 -p 2 -a off  ->  [Segmentation fault]      ★rc は証拠にしない
   OFF 読み戻し: c6uart=GONE  c6jtag=GONE                  ★これが唯一の証拠
                 C3=PRESENT  C5=PRESENT                    ★他エージェント/他プロジェクトは無傷
uhubctl -l 1-6 -p 2 -a on
   ON  読み戻し: c6uart=PRESENT
```
★**uhubctl は segfault したが電源は落ちた**（memory の既知挙動）＝**読み戻しだけが証拠**。
★**C6 は hub `1-6` port **2** の «ネストハブ» の下**（`1-6.2.3`＝usbjtag / `1-6.2.4`＝CP2102N）
＝**`-p 2` で両方まとめて落ちる**（`udevadm` で USB パスを実測して確定）。

### 7.3 ★★C0 の «前提» を実測で満たした（＝真cold で app が生きている）

**BlueZ の fresh discovery（キャッシュ全削除 → 18s）で、真cold 直後の DUT を確認**：

```
14:C1:9F:E0:5A:9E  name='IDFCTL-C6'      <<< C0 DUT（stock bleprph・真cold）
D0:CF:13:F0:A7:44  name='ASP3-C5-BLE'    <<< C5（まだ ASP3。未 flash）
84:0D:8E:18:83:7C  name='FMP-ESP32-BLE'    <<< 別プロジェクト（触るな）
F4:12:FA:5B:4A:58  name='FMP-ESP32S3-BLE'  <<< 別プロジェクト（触るな）
```

⇒ ★**stock bleprph は C6 で «真cold から» 起動して広告する**（**warm でしか見ていない**という
`c6-cold-boot-init-gap.md` の再発型を回避＝**真cold で在席を実証**）。
⇒ ★**広告名は 4 者すべてが一意に区別できる**（`IDFCTL-C6` / `ASP3-C5-BLE` / `FMP-ESP32-BLE` / `FMP-ESP32S3-BLE`）。

### 7.4 計装（★デバイス側の独立証拠＝反証条件 R4 の実行手段）

- **UART0 コンソール（CP2102N `125a266b…`）を capture 保持中**
  → `tmp/stock_bleprph/cell_logs/c0_c6_android_final.log`
- ★**open が DUT をリセットしないことを «較正して» 確認した**：
  `dtr=True/rts=True` で open → **0 バイト・boot banner 無し**。
  **ただし «沈黙» は «死んでいる» と区別できない**ので、**既知のリセットを与えて検出器を較正**
  （→ **3664 バイト・`ESP-ROM:esp32c6-20220919`・`rst:0x15`・`GAP procedure initiated` を取得**）。
  ⇒ **ポートは生きている・open はリセットしない・沈黙は «正常な無イベント»** の 3 点が確定。
- **stock は ASP3 の LP_AON マーカより «証拠の質» が高い**：`CONNECT`/`PAIRING_COMPLETE`/
  `ENC_CHANGE`/`subscribe` を**逐語のログで**読める（review §6 の「C5 は ENC が PAIR を上書きするので
  `0x5DC0` の不在は何も証明しない」という**観測能力の欠損が無い**）。

### 7.5 ★ユーザーへ渡す手順（★別プロジェクトのボードを掴まないこと）

1. ★**Android・iPhone の «両方» で、以下を forget（ペアリング解除）してから始める**：
   - **`IDFCTL-C6`**
   - ★**`ASP3-C6-BLE`**（**同じ MAC `14:C1:9F:E0:5A:9E` の «前の名前»**＝
     **スマホは MAC で鍵を持つので、名前が違っても古い bond が残っている**）
   → **forget 後に BT を OFF→ON**（GATT キャッシュ掃除。C3 で実際に誤診しかけた前例あり）
   ★**理由**＝§4 のとおり **stock も RAM-backed**＝**真cold でデバイスは鍵を失っている**。
   **スマホだけが鍵を持つ不一致**が「9 時間溶かした事故」の正体。
2. **Android で BLE スキャン**（nRF Connect 等）→ ★**`IDFCTL-C6`（`14:C1:9F:E0:5A:9E`）を選ぶ**。
   ★**`FMP-ESP32-BLE` / `FMP-ESP32S3-BLE` は別プロジェクト＝絶対に触らない。**
3. **接続**する。
4. **サービス `59462f12-9543-9999-12c8-58b459a2712d`** の
   **特性 `33333333-2222-2222-1111-111100000000`** を **READ** する。
   → ★**未ペアなら «弾かれて» ペアリング要求が出るのが正しい**（`READ_ENC` のため）。
5. **ペアリングを承認**（**Just Works**＝NO_IO・MITM 無し⇒**PIN は出ない想定**）。
6. ★**判定＝bond 後にその特性が READ できるか**（＝我々の `0xABF4` 相当＝**D-2d 相当**）。
   **NOTIFY を subscribe** できればなお良い（`0x0`/`0x1` を CCCD へ）。
7. **終わったら「終わった」とだけ伝えてください**（**結果の解釈はこちらで数値から行う**）。

★**ユーザーが試す «前» にログを読まない**。読むのは「終わった」の後。
★**esptool を今 C6 に当てない**（`--before usb-reset` 等は **download mode に落として広告を止める**）。

### 7.6 予測（§6.1 で事前登録済み・★結果は未取得）

| 予測 | 事前登録値 | 結果 |
|---|---|---|
| **P1**（C0＝stock × C6 × Android が OK） | **85%** | ★**的中＝OK**（§10） |
| **P2**（主対照＝stock × C5 × Android が OK） | **60%** | **未測定（§11 で準備完了）** |

---

## 8. ★★ユーザー指摘への対応（＝広告名の衝突。**実害が出る «前» に潰した**）

### 8.1 指摘と、私が «測って» 確認したこと

ユーザー指摘（逐語）：「**S3/ESP32でもESP-IDFのBLEを動かしているので広告の名前は変えて**」

★**「たぶん `nimble-bleprph` だろう」で済ませず、実際にスキャンして一覧を取った**：

| MAC | 広告名 | 正体 |
|---|---|---|
| `F4:12:FA:5B:4A:58` | **`FMP-ESP32S3-BLE`** | **別プロジェクト（hub `1-5` port 3）** |
| `84:0D:8E:18:83:7C` | **`FMP-ESP32-BLE`** | **別プロジェクト（ESP32・USB 接続でない可能性）** |
| `D0:CF:13:F0:A7:44` | `ASP3-C5-BLE` | 我々の C5（ASP3） |

⇒ **別プロジェクトは `FMP-*` を名乗っており、stock の `nimble-bleprph` とは «たまたま» 衝突しなかった**。
★**しかし「衝突しなかった」は運**であり、**`nimble-bleprph` のまま焼けば「ESP-IDF の既定名」同士で
将来衝突しうる**（**相手も IDF の BLE を動かしている**）。**ユーザー指摘は正しい。**
⇒ **一意名 `IDFCTL-C6` / `IDFCTL-C5` を採用**（**チップも名前で判別可能**＝C0 と主対照を取り違えない）。
⇒ ★**判定は名前だけに頼らず MAC を併用**（`14:C1:9F:E0:5A:9E` / C5 は `D0:CF:13:F0:A7:44`）。

### 8.2 ★副産物：EXT_ADV を切った（＝**測定不能を1つ潰した**）

**stock 既定は `EXAMPLE_EXTENDED_ADV=y`（`default y if SOC_ESP_NIMBLE_CONTROLLER`）**＝
**拡張アドバタイズ**。これを **stock のまま**にすると、**次の実害があると実測で判明**：

- ★**本ベンチ唯一のスキャナ `hci0` は HCI **4.2**（Intel Wireless-AC 3168）**
  ＝**LE Extended Advertising（BT 5.0 機能）を原理的に «見られない»**。
- **実際、EXT_ADV のままの C6 はスキャンに «出てこなかった»**。
  ★**これを「C6 が真cold で死んだ」と読みかけた**——**危うく «存在しないバグ» を報告するところだった**
  （**0 を読んだら検出器の能力を先に疑う**）。**`hciconfig hci0 version` で 4.2 を確認して機序が確定**。
- ⇒ **EXT_ADV のままでは «DUT が広告しているか» を我々が検証できない**＝
  **C0 が NG のとき「死んでいる」と「スマホに見えていない」を区別できない**
  ＝**C0 が «信頼できる陽性対照» でなくなる**。

**加えて 2 つの利点**：
1. **我々の ASP3 app はレガシー adv**＝**EXT_ADV を切ると «電波の出方» の交絡が消える**
   （§1.3 の表から 1 行が «同一» に変わった）。
2. **§8.3 の名前問題が Kconfig で解ける**（EXT_ADV 時の名前は **`main.c` にハードコード**）。

★**親は「C0 が NG なら第一手＝`EXAMPLE_EXTENDED_ADV=n`」と事前に想定していた**（§6.2）。
**私はそれを «NG が出る前に» 適用した**——**理由は「NG を避けたい」ではなく
「NG が出ても解釈できないから」**＝**対照の設計上の要請**。

### 8.3 ★★Kconfig だけでは名前が変わらなかった（**危うく衝突名で渡すところだった**）

`CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="IDFCTL-C6"` を設定し、**sdkconfig.h に反映も確認した**。
**しかしバイナリには `nimble-bleprph` が «残っていた»**。
★**「非0 も «何にマッチしたか» 確かめろ」に従って追跡**した結果：

```
main/main.c:617:    rc = ble_svc_gap_device_name_set("nimble-bleprph");   ← 実行時に上書き
main/main.c:183:    name = ble_svc_gap_device_name();                     ← レガシー adv はこれを読む
```

⇒ **stock は Kconfig 既定を «実行時に» 潰していた**＝**Kconfig を設定しただけでは
デバイスは `nimble-bleprph` を名乗り続ける**。
★**もし «sdkconfig.h に入ったから OK» で止めていたら、衝突名のままユーザーへ渡していた**
（＝**«入力» を見て «出力» を見ない**という第7再発そのもの）。
⇒ **`main.c:617` を「stock 自身の Kconfig を尊重する」1 行に変更**（D-5）＝
**同じ関数・同じ API・文字列だけが違う**。**バイナリから `nimble-bleprph` が 0 件になったことを実測**。

### 8.4 ★「他に何を変えたか」の洗い直し（親の指示）

**§1.2 の D-1〜D-5 が全部**。`diff -r` で **stock ファイルは `main.c` 以外 1 バイトも変わっていない**
ことを機械的に確認した（`sdkconfig.defaults` への当初の追記は **撤回して stock へ戻した**＝
delta は **新規ファイル 3 個 + `main.c` 1 行**）。**「必要だから」で無自覚に増やしていない。**

---

## 9. 未測定・取らなかった対照（no silent caps）

- **C0 の結果そのもの**（ユーザーのスマホ操作待ち）。**主対照（C5）は C0 の «後»**（ミッション指示）。
- **C5 はまだ ASP3 のまま**（`ASP3-C5-BLE` として広告中＝スキャンで実測）。**未 flash。**
- **iPhone セルは取らない**（ミッション指定は Android・1 ビルド 1 端末）。
- **`hci0` での bond 試験はしない**（**別エージェントが C3 実機で作業中**＝BlueZ を掴むと相手を壊しうる）。
  **スキャンのみに留めた**（実行時に **接続 0 件・Discovering=False** を read-only で確認してから実施）。
- **§5 の 30 個の config 差分のうち «どれが効くか» は未測定**（N-a は「差分だけでは
  C5/C6 非対称を説明しない」と言うだけ）。
- ★**EXT_ADV を切ったことで「stock 既定そのもの」を測ってはいない**＝
  **「stock の «既定構成» が Android で動くか」は本ラウンドの問いではない**（問いは
  「Espressif のスタック単体で Android × C5 は壊れるか」）。**この限定を明示しておく。**


---

## 10. ★★C0 実測 ＝ **PASS**（stock × C6 × {Android, iPhone} とも OK）

**ユーザー観測（逐語）**：
> 「**IDFCTL-C6 x iphone : all ok (1回目 disconnectでtimeoutエラー)**
>  **IDFCTL-C6 x Android : all ok**」

★**ユーザーは «両端末» で実施した**（私は Android のみ想定していた）。**両方 OK。**

### 10.1 デバイス側の独立証拠（＝反証条件 R4 の実行。ユーザー観測と 2 系統で一致）

`tmp/stock_bleprph/cell_logs/c0_c6_android_final.log`（17623 B・**真cold の 1 ブート内**＝
**`ESP-ROM`/`rst:` バナー 0 件**＝**セッション中デバイスは一度もリセットしていない**）。

| t(s) | event | peer(RPA) | state | 詳細 |
|---|---|---|---|---|
| 0.0 | connection established | `48:0b:eb:68:37:4e` | enc=0 **bond=0** | status=0 |
| 6.1 | **encryption change** | 同 | **enc=1 bond=1** | **status=0** |
| — | **Characteristic read** | 同 | — | **attr_handle=29＝暗号必須特性の READ 成功** |
| 21.1 | disconnect | 同 | enc=1 bond=1 | reason=531 |
| 25.2/28.9/31.6 | **再接続 ×3** | 同 | **enc=1 bond=1** | ★**LTK 再利用＝bond が効いている** |
| 100.8 | connection established | `77:93:ee:cd:4e:3f` | enc=0 bond=0 | status=0 |
| 106.7 | **encryption change** | 同 | **enc=1 bond=1** | **status=0** |
| 110.4 | **encryption change** | `48:21:22:11:9b:7c` | enc=0 bond=0 | ★**status=7** |
| 111.4 | connection established | `46:15:60:01:91:3d` | enc=0 bond=0 | status=0 |

**判定に使った数字**：`encrypted=1 authenticated=0 **bonded=1**` が **13 回**／`bonded=0` が 11 回。
**暗号必須特性の READ 成功**＋**LTK 再利用の再接続**＝**D-2d 相当を stock が達成**。

★**検出器の較正（危うく誤読するところだった）**：最初 `pairing complete` を grep して **0 件**だったが、
**bleprph はそんなログを «一度も出さない»**（`main.c` に存在しない）＝**0 は «死んだ検出器»**。
**bond の証拠は `bonded=` フィールド**である。**「0 を読んだら測定対象の存在を先に確かめろ」の実行例。**

### 10.2 ★「1ビルド1端末」はこのアプリに当てはまらない（＝**ASP3 app 固有の規則だった**）

**実測**：**1 つのログに «両端末の» セッションが両方入っている**（上表）。
理由＝**stock の判定はコンソールログ＝追記式**で、**ASP3 の LP_AON マーカのような
«共用レジスタの last-wins»（review §6）ではない**。**`BT_NIMBLE_MAX_BONDS=3`** なので
**2 端末が同時に bond しても溢れない**（実測：13 回の `bonded=1`）。
⇒ ★**「1ビルド1端末」は ASP3 app の計器の欠損に由来するローカル規則であり、stock には不要。**

★**ただし stock 側に «別の» 限界がある（正直に）**：
**stock は IRK を配らない（`sm_our/their_key_dist` = ENC のみ）**⇒ **peer は RPA のまま解決されない**
（実測：**全セッションで `peer_id_addr` == `peer_ota_addr`**＝**解決されていない**）。
⇒ ★**「どのセッションがどの端末か」をログから同定できない。**
観測された peer は **4 個の RPA**（`48:0b:eb…`/`77:93:ee…`/`48:21:22…`/`46:15:60…`
＝**すべて type=1 かつ上位2bit=0b01＝RPA**）だが、
**「2 端末が RPA を回した」のか「別のセントラルが混入した」のかは区別できない**
（★**単一 run から機序の物語を作らない**）。
⇒ **端末への帰属は «ユーザーの逐語観測» に依存する**。**ログは «両方 bond した» ことまでを支持する。**

### 10.3 ★★新発見：**`ENC status=7`（ENOTCONN）は stock でも出る**＝**この値だけでは «失敗» を意味しない**

**t=110.4 で `encryption change event; status=7`（`BLE_HS_ENOTCONN`＝`ble_hs.h:90` で確認）**が
**発生している。しかもこのセッションをユーザーは「all ok」と報告している。**

★**これは我々の C5×Android の «病態» と «同一の数値»**：

| | 値 | 意味 |
|---|---|---|
| 我々 C5×Android（NG） | `ENC=0x5de00007` | tag `0x5DE0`＋**status=7＝ENOTCONN** |
| **stock C6（「all ok」のセッション内）** | **`encryption change status=7`** | **同じ ENOTCONN** |

⇒ ★★**`status=7` 単独は «バグの指紋» ではない**。**短命な接続が SM 完了前に切れれば正常系でも出る。**

★**含意（仮説であって測定ではない。物語にしない）**：
我々の C5 マーカは **ENC/PAIR 共用・last-wins**（review §6・D8）なので、
**「bond 成功 → その後の短命な再接続が ENOTCONN を残す → 成功が上書きされる」**という筋が
**原理的にありうる**。**ただし C5×Android はユーザー観測が「登録されていない」＝bond 不成立**なので、
**C5 の NG 判定そのものは覆らない**（`evidence-c5-08 §8.3` も「bond 不成立の根拠はマーカではなく
ユーザー観測と ENC status=7 の 2 つ」としている）。
⇒ **本発見が壊すのは «判定» ではなく «signature の一意性»**＝**反証条件 R3 の運用が難しくなる**
（§11.4 に記す）。**事前登録は書き換えない。**

### 10.4 ★★「iPhone の1回目 disconnect で timeout エラー」は **stock でも出る**

**ユーザーは同じ挙動を我々の ASP3 app（C5/C6）でも観測し「このままで良い」と判断していた。**
**同じ症状が «我々のコードが 1 行も入っていない» stock IDF でも出た。**

⇒ ★**これは «我々のポート由来ではない» ことの初の «上流対照による» 外部証拠**である。
**「直すべきものが我々の側に無い」を対照実験で支持できた。**

**デバイス側の実測（言えることだけ）**：
- **全 7 回の disconnect の reason は `531`**＝`0x213`＝**`BLE_HS_HCI_ERR(0x13)`**
  （`ble_hs.h:174`＝`0x200 + x`）＝**`BLE_ERR_REM_USER_CONN_TERM`＝スマホ側からの切断**。
- ⇒ **デバイス側にタイムアウトの痕跡は無い**（`timeout` を示す reason は 1 件も無い）。
- ★**機序は語らない**（**RX/TX を計装していない**）。**「同じ症状が stock でも出る」までが実測。**

---

## 11. 主対照 ＝ **stock × C5 × Android**（★準備完了・ユーザー確認待ち）

### 11.1 載っているもの（★独立に読み戻して確認）

| 項目 | 実測 |
|---|---|
| DUT | **C5#1 `d0:cf:13:f0:a7:44`**（ESP32-C5 rev v1.0・`1-6.4`／電源 `-p 3-4`） |
| **広告名** | **`IDFCTL-C5`**（★**名前でチップを判別できる**＝C6 と取り違えない） |
| **BLE アドレス** | ★**`D0:CF:13:F0:A7:46`**（**実測**。base MAC `…A7:44` **+2**＝C6 と同じ規則） |
| 焼込み | ★**bootloader `0x2000`**（**C6 の `0x0` と違う**）/ partition `0x8000` / app `0x10000` |
| **読み戻し検証** | **3/3 とも `verify OK (digest matched)`** |
| EXT_ADV | ★**OFF を ELF で確認**（`ble_gap_ext_adv_start`=**0** / `ble_gap_adv_start`=**1**）＝**C6 の罠を C5 で踏まない** |
| 衝突名 | `nimble-bleprph` = **0 件**（binary 実測） |

★**ASP3 の BLE アドレスは `…A7:44`（`ASP3-C5-BLE` としてスキャンで実測済）だったが、
stock は `…A7:46`**＝**我々のポートと stock で BLE アドレスが違う**（**新しい実測事実**。
bond 不成立との関係は**未測定＝主張しない**）。

### 11.2 ★上書き前に dump を取り直した（コーディネータ指示）

```
再 dump md5 : c29621d518f49d7f27d6f1b0221f8dee
既存   md5 : c29621d518f49d7f27d6f1b0221f8dee   -> IDENTICAL
```
⇒ **独立な 2 回の dump が一致**＝**ASP3 の C5 イメージは上書き前に 2 重に確保**。
**復旧手順は §3.3 で実機 verify 済み**（`write_flash`＋`verify_flash` を実際に走らせてある）。

### 11.3 真cold の証明＋他プロジェクト無傷の確認

```
uhubctl -l 1-6 -p 3-4 -a off
   OFF 読み戻し: c5jtag=GONE  c5uart=GONE           ★これが唯一の証拠
   無傷        : C3(-p1)=PRESENT  C6=PRESENT  hub1-5_S3=PRESENT
uhubctl -l 1-6 -p 3-4 -a on
   ON          : c5jtag=PRESENT c5uart=PRESENT
```
**真cold ブートログを capture が捕捉**（`Device Address: d0:cf:13:f0:a7:46`・
`GAP procedure initiated: advertise`）＝**stock は C5 でも «真cold から» 起動して広告する**。
**BlueZ fresh discovery で在席実証**：`D0:CF:13:F0:A7:46 name='IDFCTL-C5' Paired=False`。
（`hci0` は **接続 0 件を read-only で確認してから** スキャンのみ。**C3 エージェントと共有中**）

### 11.4 ★判定の読み（§6.2 の事前登録どおり。書き換えない）

- **OK ⇒ 問題は «我々の統合» にある**（チップでも blob でもない）。
  ★**ただし «我々のバグ» と呼ばない**——**R2 の交絡が開いている**
  （**実行時 `sm_sc` stock=0/我々=1**・**stock は ENC のみ配布／我々は ENC|ID**）。
  **追加アーム（stock 自身の `EXAMPLE_USE_SC=y` + `EXAMPLE_RESOLVE_PEER_ADDR=y`）で閉じるまで保留。**
- **NG ⇒ 問題は «上流»**（Espressif のスタック/コントローラ/blob）＝**我々のバグではない**。
  ★**P3（signature 一致）を確認する**が、**§10.3 により `status=7` の一致は «弱い» 証拠**
  （**正常系でも出る**と実証された）。⇒ **`bonded=` が最後まで 0 のままか**・
  **暗号必須特性の READ が通らないか**という **«到達点» で判定する**（値の一致ではなく）。

### 11.5 ユーザーへ渡す手順

1. ★**Android・iPhone の «両方» で forget**：**`IDFCTL-C5`** ／ ★**`ASP3-C5-BLE`**
   （**BLE アドレスは違う（`…:44` vs `…:46`）が、念のため両方消す**）／
   **C0 で bond した `IDFCTL-C6` も消しておくと安全**（**stock は RAM-backed＝
   デバイス側は真cold で鍵を失っている＝スマホだけが鍵を持つ不一致が事故の元**）
   → **forget 後に BT OFF→ON**。
2. **BLE スキャン → `IDFCTL-C5`（`D0:CF:13:F0:A7:46`）を選ぶ**。
   ★**`FMP-ESP32-BLE` / `FMP-ESP32S3-BLE` は別プロジェクト＝繋ぐな。**
   ★**`IDFCTL-C6` は C0 用＝今回は繋がない。**
3. **接続** → **特性 `33333333-2222-2222-1111-111100000000`**
   （**サービス `59462f12-9543-9999-12c8-58b459a2712d`**）を **READ**。
4. **弾かれてペアリング要求が出たら承認**（Just Works・PIN 無し想定）。
5. ★**判定＝bond 後にその特性が READ できるか**。
6. **終わったら「終わった」とだけ伝えてください。**

★**ユーザーが試す «前» にログを読まない。**


---

## 12. C3 対照 ＝ **ビルド完了・事前登録**（★実機はまだ触っていない。C5 セルの «後»）

ユーザー指示「**C3も用意させて**」。**C5（主対照）がユーザーの律速なので、C5 → C3 の順**。
**本節は «実機を触らない作業» のみ**（ビルド＋予測の事前登録）。**C3 の flash はまだしていない。**

### 12.1 ★C3 固有の «6 件目» の変更（D-6）＝**ボードに配線が物理的に無い**

★**「はず」で進めず実測した**：

```
C3 の stock 既定コンソール : CONFIG_ESP_CONSOLE_UART_DEFAULT=y   （＝UART0）
C3 の secondary console    : 無し（C5/C6 は既定で SECONDARY_USB_SERIAL_JTAG=y だった）
C3(1-6.1) の USB 実体      : usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:57:BA:BC のみ
                             ＝CP2102N ブリッジは «存在しない»
```

⇒ **stock 既定のままだと、コンソールは «誰も聞いていない UART0 ピン» へ出る＝デバイス側証拠が 0**
＝★**「見えない検出器で 0 を読む」罠そのもの**（**C6 の EXT_ADV で踏みかけたのと同型**）。
⇒ **D-6：`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`（C3 のみ）**。
**正当化＝ボードにその配線が物理的に無い**。**C5/C6 には不要だった**（実測差）。

**⇒ stock からの変更は D-1〜D-6 の «6 件»**（§1.2 の 5 件 ＋ D-6）。**D-6 は C3 限定。**

### 12.2 ビルド実測（★すべて «出力» 側で確認）

| 指標 | C3 実測 |
|---|---|
| target（build cache） | `esp32c3` |
| console | `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG 1`（`UART_DEFAULT` は消えた＝choice が排他） |
| **EXT_ADV** | ★**`ble_gap_ext_adv_start`=0 / `ble_gap_adv_start`=1**＝**レガシー adv**（**C6 の «見えない» 罠を C3 で踏まない**） |
| 衝突名 | `nimble-bleprph` **0 件** ／ 一意名 **`IDFCTL-C3`** |
| BONDING/ENCRYPTION | **2/2**（sdkconfig.h に define 済） |
| toolchain | **`esp-14.2.0_20260121`**（IDF 標準・C5/C6 と同一） |
| `-march` | **`rv32imc_zicsr_zifencei`**（★**C3 の IDF 標準。我々の C3 と既に一致**＝`evidence-c6-15 §13-8` と整合） |
| **NVS persist** | **0＝RAM-backed**（**C5/C6/ASP3 と同じ**＝§4 の NULL が C3 でも成立） |
| flash layout | **bootloader `0x0`**（**C6 と同じ／C5 の `0x2000` とは違う**） |

### 12.3 ★C3 で測る価値（＝我々のポートで «唯一» 何も効かなかった軸）

**我々のポートでの C3 の既知**（本日実測・アーム B＝esp-idf＋IDF標準 GCC14）：

| セル | 我々の実測 | 病態 |
|---|---|---|
| **C3 × iPhone** | **NG** | `PAIR=0x5dc00000`(status 00)・`ENC=0x5de00000`(**成功**)・**`DISC=0`＝wedge**＝**デバイス側は成功しているのに iPhone が timeout し、切断が届かない** |
| **C3 × Android** | **bond OK だが再接続できない** | `PAIR=0x5dc00011`・`DISC=0xd15c1303`（**切断は届く**）・`CONN`=4（**デバイスは受け入れている**）＝**切っているのはスマホ側** |

★★**C3×iPhone は «供給でも toolchain でも動かなかった唯一の軸»**（hal でも esp-idf でも・
GCC14 でも GCC15 でも NG）＝**本プロジェクトで最も頑固な現象**。
★**その «変えた軸» はすべて我々の統合より «下» にある**（供給＝blob/ソース・toolchain＝コード生成）。
**全アームで不変だったのは «我々の統合»（shim/OSAL/直接ブート/config）**であり、
**stock はそれを丸ごと外す**⇒ **この実験は C3 で最も «情報量» が大きい**。

### 12.4 ★★事前登録（実機の «前» に commit。★P1/P2/P3 は書き換えない）

| # | 予測 | 確率 | 根拠（正直に） |
|---|---|---|---|
| **P4** | **stock × C3 × iPhone が «wedge しない»（＝OK）** | **70%** | **上げる理由**＝(1) **wedge の症状（«デバイスは成功したのに切断イベントが届かない»）は «イベント配送» の症状**であり、**それを担うのは我々の shim/OSAL**（C3 app は pend_ring flush を持つが、**機構自体が我々のもの**）。(2) **既に振った軸（供給・toolchain）はすべて我々の統合より下**＝**不変だったのは我々の統合だけ**。(3) **C3 は最も枯れたチップ**で bleprph×iPhone は上流で広く使われている。**下げる理由**＝(4) **C3 の blob は C5/C6 と別系統**（`lib_esp32c3_family`＝旧世代）＝**上流バグの余地はある**。(5) ★**「最も頑固」＝我々が «まだ当てていない» 軸が上流にある可能性**を否定できない |
| **P5** | **stock × C3 × Android が «再接続まで» OK** | **65%** | **上げる理由**＝(1) **stock C6 は LTK 再利用の再接続を 3 回実測**（§10.1）＝**stock の再接続経路は動く**。(2) 我々の C3 は **bond までは成功**＝RF/controller は基本的に働く。**下げる理由**＝(3) **C3 blob は別系統**。(4) **再接続失敗はスマホ側が切っている**＝**デバイス側の «LTK を出せない» に起因しうる**が、**未計装＝機序は不明**。(5) ★**「1 端末 1 RPA」ではない**（§10.2）＝**stock は IRK を配らないので、再接続時の RPA を解決できない**＝**stock 側にも «再接続が難しい» 構造的理由がありうる**（★**これは我々に不利な «下げ» 要因ではなく、stock の NG を «上流» と読む妨げになる交絡**＝§12.5 R6） |
| **P6** | **P4 が NG（wedge する）場合、その症状が我々と同型**（**disconnect が届かない**） | **60%** | 同じ blob・同じ NimBLE host なら同型が自然。ただし stock は **sc=0/ENC-only/console 経路も違う**＝別症状もありうる |

### 12.5 ★反証条件（C3 分。★反証条件も仮説なので検算する）

- **R5**：★**C3 セルの前に «陽性対照» が要る**。**C0（C6）は既に PASS**しているので
  **アプリ・手順・端末は健全と実証済み**＝**C3 の NG を «C3 固有» に帰属してよい**。
  ★**ただし C3 は «別ビルド»（D-6＝console 経路が違う）**なので、**«C3 のビルドが壊れている» 可能性は
  C0 では棄却できない**⇒ **C3 の app が起動・広告することを «スキャンで» 実証してから渡す**
  （**§11.3 で C5 にやったのと同じ**）。
- **R6**：★**stock × C3 × Android の «再接続 NG» を «上流» と読んではいけない**——
  **stock は IRK を配らない（ENC のみ）**ので、**RPA を回すスマホからの再接続は
  «stock の構成» でも難しい**（**我々は ENC\|ID を配っている＝この点で我々の方が «有利» な構成**）。
  ⇒ **再接続については «stock NG / 我々 NG» でも «同じ原因» とは言えない**
  ＝**この比較は `EXAMPLE_RESOLVE_PEER_ADDR=y` の追加アームを待つ**（R2 と同じ交絡）。
  ★**逆に «stock が再接続 OK» なら**、**IRK 無しですら通る**ということなので **強い結果**になる。
- **R7**：★**「iPhone が timeout エラーを出す」こと自体を NG の判定に使わない**——
  **§10.4 で «stock C6 の «all ok» セッションでも iPhone は 1 回目 disconnect で timeout を出す» と実証済**。
  ⇒ **C3×iPhone の判定は «bond が成立し、暗号必須特性が READ できるか»**（＝**到達点**）で行う。
  **`DISC=0`（wedge）は我々のマーカ固有の観測であり、stock ではログの «disconnect 行の不在» で見る。**
  ★**ただし «不在» は弱い証拠**（§10.3 で `status=7` の一意性が壊れた前例）⇒
  **«何が起きたか» を positive に示す行（`bonded=`・`Characteristic read`）を主証拠にする。**

### 12.6 未測定（no silent caps）

- **C3 の実機作業は «一切していない»**（**dump も flash も真cold もこれから**）。
  ⇒ **上書き前の dump・オンチップ md5 による «載っているビルド» の同定・
  復旧手順の実地検証** は **C5 セルの後に行う**（C5/C6 でやったのと同じ手順）。
- ★**C3 の «コンソール open が DUT をリセットするか» は stock アプリで «自分で» 確認していない**
  （別エージェントが ASP3 で `dtr=True/rts=True` は非リセットと実測確立しているが、
  **アプリが違えば挙動も違いうる**）⇒ **C3 セルで «既知リセットによる較正» を行う**（§7.4 と同型）。
- ★**ROM 出力は USB 列挙より前に失われる**（別エージェントの実測）
  ⇒ **「コンソールで真cold を観測した」とは書かない**。**真cold の証拠は by-id 消滅の読み戻しのみ。**
