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
| **P2**（主対照＝stock × C5 × Android が OK） | **60%** | ★**的中＝bond «到達»**（§14。**ただし READ leg は未測定＝§14.4**） |

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

## 12. C3 対照 ＝ **ビルド完了・事前登録**（★★**実機は «禁止中»**。C5 セルの «後»）

> ★★**C3 の実機は現在 «別エージェントが使用中»**（`-p 1`＝アーム A のマーカー読み／
> `PAIRING_COMPLETE` が GCC15 で発火するかの未測定確認）。
> **親が «引き渡す» と言うまで `-p 1` も C3 の flash も触らない。**
> **本節の作業は «ビルドのみ»＝実機は 1 度も触っていない**（§12.6 に検証を記す）。
> ★**相手は «今» `hci0` を BlueZ bond テストに使う**⇒ **私は スキャンのみ・接続を張らない・
> スキャン前に «接続0» を read-only で確認する**（実行記録は §12.6）。

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

- ★**C3 の実機作業は «一切していない»＝検証済み**（**別エージェントが使用中のため禁止**）：
  - **私が uhubctl で触ったポートは `-p 2`（C6）と `-p 3-4`（C5）のみ**。**`-p 1` は 1 度も叩いていない**。
  - **3 回の電源断すべてで `C3(-p1)=PRESENT` を読み戻して確認**（＝**C3 は落としていない**）。
  - **`tmp/asp3_flash_backup/` に C3 の dump は 0 件**（＝**C3 の flash を read すらしていない**）。
  - **hub `1-5`（別プロジェクト）も 1 度も触っていない**（`S3=PRESENT` を読み戻し確認）。
  - **C3 のビルドは compile-only**（`build_esp32c3/bleprph.bin` は生成済だが **焼いていない**）。
  - **`hci0` は スキャンのみ**。**毎回 «接続 0 件» を read-only で確認してから StartDiscovery**
    （最終確認時も `active connections: NONE` / `Discovering: False` を確認）。
    **`RemoveDevice` は `14:C1:9F*` / `D0:CF:13*` に «限定»**＝**C3 のエントリには触れていない**。
- **引き渡し後にやること**（C5/C6 で実証済の手順をそのまま）：**上書き前の dump ×2**・
  **オンチップ md5 で «載っているビルド» の同定**（戻し先候補＝相手の `rb_B_ble`・`w1_A`/`w1_B`。
  ★**消さない**）・**復旧手順の実地 verify**・**真cold（by-id 消滅の読み戻し）**・**在席スキャン**。
  ⇒ **上書き前の dump・オンチップ md5 による «載っているビルド» の同定・
  復旧手順の実地検証** は **C5 セルの後に行う**（C5/C6 でやったのと同じ手順）。
- ★**C3 の «コンソール open が DUT をリセットするか» は stock アプリで «自分で» 確認していない**
  （別エージェントが ASP3 で `dtr=True/rts=True` は非リセットと実測確立しているが、
  **アプリが違えば挙動も違いうる**）⇒ **C3 セルで «既知リセットによる較正» を行う**（§7.4 と同型）。
- ★**ROM 出力は USB 列挙より前に失われる**（別エージェントの実測）
  ⇒ **「コンソールで真cold を観測した」とは書かない**。**真cold の証拠は by-id 消滅の読み戻しのみ。**


---

## 13. ★★【重要・訂正】主対照は **«測定されていない»**（チップ取り違え）

### 13.1 何が起きたか（★記録を消さず、訂正注記で残す＝本リポジトリの流儀）

**2026-07-17 20:2x**、私は「stock × C5 × Android を実施した」という連絡を受け、
`main_c5_android.log` を読み、**「両端末 OK」「P2 的中」「問題は我々の統合」「②が死ぬ」**
という筋で分析を進めた。**直後にユーザーから訂正**：

> 「**間違えた!! 先ほどのAndroidの試験はC6に対して実施．C5を対象にやり直す**」

⇒ ★★**「stock × C5 × Android = OK」は «測定されていない»。**
⇒ ★★**以下はすべて «無効»＝本 evidence に結論として書かない**：
- **「stock × C5 × Android = OK」** — **未測定。**
- **「P2（60%）的中」** — ★**取り消す。P2 は «未測定» に戻す**（§6.1 の表を訂正済）。
- **「問題は «我々の統合» にある／チップでも blob でもない」** — **未測定。**
- **「②（blob の IRK 欠落）が死んだ」** — ★**その論証は «stock×C5 が OK» に乗っていた＝土台が消えた。**
  （**blob が stock と我々で «同一 inode» という測定自体は有効**＝§13.3 に «事実だけ» 残す。
  **しかし «だから②が死ぬ» という推論は成立しない**。）

★**commit 前に訂正が届いたため、evidence に誤った結論は 1 行も入っていない**
（`git log` 上、C5 の結論を含む commit は存在しない）。**本節はその事実の記録でもある。**

### 13.2 ★C5 のログで «帰属» を検算した（★クリアする «前» に読んだ）

**問い**＝**「ユーザーは C5 に触れていないのか？」**

| 指標 | 実測（`archive/c5_MISATTRIBUTED_session_203329.log`＝**消さずに保存**） |
|---|---|
| **C5 の接続イベント** | ★**«在る»**（**8 個の distinct peer RPA**） |
| 最長セッション | `42:91:91:ce:74:f3`：**550s..835s（285秒）**・**`bonded=1` ×25**・**暗号必須特性の READ ×1** |
| 同時期の C6 | `6e:14:33:a2:db:a1`：**1614s..1716s（102秒）**・`bonded=1` ×2（**C0 の «後» の新規セッション**） |

⇒ **言えること**：**C5 «にも» 接続があり、何かが `bonded=1` に到達し、暗号必須特性を READ した。
C6 «にも» C0 後の新規セッションがあった。**
⇒ ★★**言えないこと（＝憶測しない）**：**«誰が繋いだか»**。
**stock は IRK を配布せず（`sm_their_key_dist`=ENC のみ）、peer は «未解決の RPA» のまま**
（**全セッションで `peer_id_addr == peer_ota_addr`**）
⇒ ★**セッションを端末に帰属できない**（**私が §10.2 で自ら登録した制約**）。
**「C5 に接続があった＝ユーザーの iPhone だった」とは読まない。**
**BlueZ・別のセントラル・他人の端末を排除できない。**
⇒ ★**「ユーザーは C5 に触れていない」を数字で裏付けることは «できなかった»**
（**接続が在ったので、その直接証拠は得られない**）。**それが実測の限界であり、そう書く。**

### 13.3 blob の測定（★事実のみ。§13.1 のとおり «②の生死» には使わない）

| 指標 | 実測 |
|---|---|
| stock C5 がリンクする blob | `esp-idf/components/bt/controller/lib_esp32c5/esp32c5-bt-lib/libble_app.a` |
| 我々の C5 がリンクする blob | **同一パス**（`/home/honda/TOPPERS/asp3_esp_idf` は **同一 repo への symlink**） |
| **同一性** | ★**same inode `15244326`**＝**«内容が同じ» ではなく «同じファイル»** |
| 準空アーカイブか（`libcore.a` 事故の型） | **否**＝**1,860,394 B / 155 member**（review D21 の記録と一致）。私の指標での defined sym = **2564**（★**review の「4310 sym」とは «指標» が違う**＝`nm` の数え方の差。**「不一致」と書かない**＝第8再発の教訓） |
| review D22 の 4 関数（C5 に無いとされる） | `r_ble_ll_resolv_change_irk`/`_irk_change`/`_restore_irk`/`_get_index` ＝ **0/0/0/0**（**確認**） |
| 検出器の較正 | `r_ble_ll_resolv_gen_rpa`=1・`r_ble_ll_resolv_rpa`=3・`r_ble_ll_get_peer_irk`=1 ＝**空振りではない** |

★**この測定が意味するのは «供給が同一» までである**。**②の生死は «stock×C5×Android» の実測を待つ。**

### 13.4 ★★根本原因と «普遍の» 再発防止（★親の段取りミスと明記してよい、との指示に基づく）

**原因**＝★**同一セッションに `IDFCTL-C6` と `IDFCTL-C5` が «両方» 広告していた**
⇒ **人間（ユーザー）が取り違えた**。

★**D-4（一意名）は «必要だったが十分ではなかった»**：
**名前を分けても «2枚同時に見えている» 限り、人間の取り違えは防げない。**
（**私は「名前を分ければ判別できる」と考えた＝設計の誤り。**
 **親も «名前分離で足りる» と考えた＝段取りの誤り。**）

★★**普遍の要件＝«1セル1ボード»**：
**測るボード «以外» は電源断し、«スキャンに居ないことを実証» してから渡す。**

| 規則 | 由来 | 普遍性 |
|---|---|---|
| **「1ビルド1端末」** | **ASP3 の LP_AON マーカが «共用・last-wins»** | ★**ASP3 の計器固有の artifact**（§10.2 で実測して否定＝stock は追記ログなので不要） |
| ★**「1セル1ボード」** | **人間が取り違える** | ★★**普遍**（**計器と無関係。今回 実際に発生**） |

**実行した再発防止（＝手順の本体）**：
```
sudo uhubctl -l 1-6 -p 2 -a off        （segfault したが rc は証拠にしない）
  by-id 読み戻し : c6jtag=GONE  c6uart=GONE
  無傷          : C3(-p1)=PRESENT  C5=PRESENT  hub1-5_S3=PRESENT
BlueZ fresh discovery（キャッシュ全削除 → 22s）:
  ★ IDFCTL-C6 = ABSENT（★«電源を切ったから見えないはず» ではなく «見えないことを測った»）
    IDFCTL-C5 = PRESENT / Paired=False
```

### 13.5 主対照の再取得 ＝ **準備完了**（★Android のみ・1セル1ボード）

| 項目 | 実測 |
|---|---|
| アーム | ★**stock arm-1（現行 `IDFCTL-C5` ビルド）＝再ビルドしていない** |
| 焼込み同一性 | **3/3 `verify OK (digest matched)`**（真cold の «前» に実行）・app md5 `4d539921…`＝**初回 flash と同一** |
| EXT_ADV | **OFF**（ELF：`ble_gap_ext_adv_start`=0 / `ble_gap_adv_start`=1）＝**HCI 4.2 の `hci0` でも見える** |
| **真cold** | `-p 3-4` off → **by-id 消滅を読み戻し**（`c5jtag=GONE c5uart=GONE`・`C3(-p1)=PRESENT`）→ on |
| **ログ/マーカのクリア** | ★**前セッションのログは `archive/` へ退避**し、**新規ファイルで capture**。
**fresh ログの `connection established` = 0**（＝**残骸を «今回の結果» と読む事故を構造的に排除**） |
| bond ストア | **RAM-backed（§4）**＝**真cold で鍵は消えている**（fresh ログが 0 接続であることと整合） |
| 在席 | **`D0:CF:13:F0:A7:46` `IDFCTL-C5` `Paired=False`**（スキャンで実証。**esptool は使っていない**） |
| ★**C6** | ★**電源断済み・スキャンで ABSENT を実証**＝**取り違えようがない** |

### 13.6 ユーザーへ渡す手順（★Android のみ）

1. ★★**`IDFCTL-C6` は電源断済み＝スキャンに出ません（実証済み）。取り違えようがありません。**
   **今 スキャンに出る我々のボードは `IDFCTL-C5` «1枚だけ» です。**
2. **Android で forget**：**`IDFCTL-C5`** ／ **`ASP3-C5-BLE`** ／ **`IDFCTL-C6`**
   （**同一個体で名前だけ違う bond が残りうる**）→ **BT を OFF→ON**。
3. **BLE スキャン → `IDFCTL-C5`（`D0:CF:13:F0:A7:46`）へ接続**。
   ★**`FMP-ESP32-BLE`/`FMP-ESP32S3-BLE` は別プロジェクト＝繋がないでください。**
4. **サービス `59462f12-9543-9999-12c8-58b459a2712d`** の
   **特性 `33333333-2222-2222-1111-111100000000`** を **READ**。
   → **未ペアなら弾かれてペアリング要求が出るのが正しい**（Just Works・PIN 無し想定）。
5. **承認** → ★**判定＝bond 後にその特性が READ できるか**。
6. ★**iPhone は今回やりません**（**1セル1端末で確実に取る**）。
7. **終わったら「終わった」とだけ伝えてください。**

### 13.7 R2 アーム（★ビルド済・**この主対照の «後»**）

**私が §6.3 R2 で事前登録した交絡（`sm_sc` stock=0/我々=1・key dist stock=ENC のみ/我々=ENC\|ID）を
閉じるアーム**を **ビルドだけ済ませてある**（**実機は主対照の後**）：

- **D-7（R2 アーム限定の変更）**＝**stock 自身の Kconfig 2 個**（**我々の config は不輸入**）：
  `CONFIG_EXAMPLE_USE_SC=y`（→`ble_hs_cfg.sm_sc = 1`）／
  `CONFIG_EXAMPLE_RESOLVE_PEER_ADDR=y`（→`sm_our/their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID`）
  ＝**`main.c:593-601` を読んで確認**（推測でない）。**これで stock の実行時 SM 設定が我々と一致する**
  （NoIO / bonding=1 / mitm=0 / **sc=1** / **ENC\|ID**）。
- 名前＝**`IDFCTL-C5SC`**（★**arm-1 と «名前で» 区別**。**ただし BLE アドレスは同じ `…A7:46`**
  ＝**スマホ側に arm-1 の bond が残ると汚染するので forget 必須**）。
- ビルド実測：`sdkconfig.h` に `CONFIG_EXAMPLE_USE_SC 1`・`CONFIG_EXAMPLE_RESOLVE_PEER_ADDR 1`
  （**arm-1 側は 両方 «0 件»＝対照が効いている**）・**bin md5 が arm-1 と異なる**（`dd2f262e…` vs `4d539921…`）
  ＝**delta が出力に届いた**。
- ★**R2 アームの «運用上の検算»**：`RESOLVE_PEER_ADDR=y` なら **IRK が交換される**⇒
  **ログの `peer_id_addr` が `peer_ota_addr` と «違う» 値になる**（＝**解決された**）はず。
  **arm-1 では全セッションで両者が一致していた**（§13.2）⇒ ★**この違いは «アームが効いているか» の
  独立指標として使える**（**config を読むのではなく «出力» で確認できる**）。
- ★**予測は主対照の «結果を見る前に» 別番号で事前登録する**（**P1-P6 は書き換えない**）。
  **主対照が未測定な今、R2 の予測を書くのは順序が逆**＝**書かない**。


---

## 14. ★★主対照 実測 ＝ **stock × C5 × Android は bond に «到達する»**（P2 的中）

**ユーザー観測（逐語）**：
> 「**android : 2回目以降のconnect でBONDに自動的にならない．nrf connect から Bond 要求をだすとBondになる**」
> 「**システムのBLEには登録されている**」

**セル条件**：`IDFCTL-C5` **のみ電波**（**C6 は電源断＋スキャンで ABSENT を実測**＝§13.4）・
真cold・fresh ログ（開始時 `connection established`=0）。

### 14.1 デバイス側ログ（`archive/c5_arm1_android_VERDICT_204339.log`・19396 B・**リセット 0 回**）

| 指標 | 実測 |
|---|---|
| connection established | **7** |
| **`bonded=1` イベント** | ★**8**（`bonded=0` は 21） |
| **bond に到達した distinct peer** | ★**4**（`56:f6:a3…`・`77:db:e4…`・`48:bb:46…`・`51:fe:6f…`） |
| encryption change status=0 | **4** ／ status=7 | **3** |
| disconnect reason | **531 ×7**（全部 `0x213`＝`REM_USER_CONN_TERM`＝**スマホ側切断**） |
| ★**暗号必須特性の READ** | ★★**0 件（一度も発火せず）** |

### 14.2 ★判定（★«到達したか» で下す。値の一致では判定しない）

**ユーザー観測とデバイス側ログは «一致» する（食い違いなし）**：
- ユーザー「**システムの BLE に登録されている**」／「**Bond 要求を出すと Bond になる**」
- ログ「**`bonded=1` ×8・4 個の peer が bond 到達・`ENC status=0` ×4**」

⇒ ★★**stock × C5 × Android ＝ bond に «到達する»。P2（60%）的中。**

**我々との対比（★同じ観測者・同じ端末・同じチップ・同じ blob・同じ真 v5.5.4 ソース）**：

| | **我々の ASP3 × C5 × Android** | **stock × C5 × Android** |
|---|---|---|
| ユーザー観測 | 「ペアリング要求は来るが**切れる**・**登録されない**」 | ★**「システムの BLE に登録されている」** |
| デバイス側 | **`ENC=0x5de00007`＝`ENOTCONN`**・`PAIRING_COMPLETE` 不発 | ★**`bonded=1` ×8・`ENC status=0` ×4** |
| **bond 到達** | ★**一度も到達しない**（GCC14/GCC15 の 2 アームで **9 レジスタ完全一致**） | ★**到達する** |

⇒ **これは «質的» な差**である。**stock は目的地に到達し、我々は到達しない。**

★**C3 × iPhone の「登録されている」と混同しない**：**あちらは «デバイス側も成功していたのに
iPhone が timeout»**（`PAIR`/`ENC` とも status 0）＝**別の現象**。**こちらは «bond が成立した» の報告。**

### 14.3 ★★結論の «射程»（★私が事前登録した R2 が «まだ開いている»＝守る）

**«今» 書いてよいこと**：
1. ★**stock は C5×Android で bond に到達する／我々は到達しない。**
   **同じチップ・同じ blob・同じ真 v5.5.4 ソース。**
2. ★**チップと blob は容疑者から外れた。**
   **根拠（実測）**＝**stock と我々は «同じ blob ファイル» をリンクしている**：
   **inode `15244326`**・**1,860,394 B**・**155 member**・
   **review D22 の IRK 4 関数は 0/0/0/0**（検出器較正済＝`gen_rpa`=1/`resolv_rpa`=3/`get_peer_irk`=1）。
   ⇒ **その blob で stock は bond できた**＝**«blob は C5×Android で bond できる»**。
3. ★**②（blob の IRK 関数欠如が Android bond を壊す）は «弱まった»**——
   ★**しかし «死んだ» とは書かない**：**stock は IRK を配布していない**（`key_dist`=ENC のみ）
   ＝**IRK 経路を «通っていない»**⇒**「IRK を配る構成でも blob が足りるか」は未測定**。
   ⇒ ★★**D-7 がこれを終わらせる**（§14.5）。

**«まだ» 書いてはいけないこと**：
- ★**「問題は我々の統合にある」**——**R2 が開いている**
  （**実行時 `sm_sc` stock=0/我々=1**・**key dist stock=ENC のみ/我々=ENC\|ID**）。
  **原因の帰属は D-7 待ち。**

### 14.4 ★未測定の申告（no silent caps）＝**私の手順書の不備**

★**「bond 後に暗号必須特性が READ できるか」は «未回答»**：
**ログの `Characteristic read` = 0 件**＝**ユーザーは READ を «実行していない»**
（**私が §13.6 の手順で «判定条件» として書いたが、ユーザーは «自動 bond にならない» 方に注力した**）。
⇒ ★**D-2d 相当の «完全な» 到達（bond → 暗号必須特性 READ 成功）は本セルでは «取れていない»。**
⇒ **P2 は «bond 到達» の基準で的中と判定した**（コーディネータ指示どおり）が、
**READ leg は «未測定» として残す**。**D-7 セルで «両方» を明示的に聞く**（§14.6）。

### 14.5 ★「2回目以降 自動 bond にならない」＝**別の現象として切り出す**（★断定しない）

**仮説 H-auto**＝**「stock は IRK を配らない ⇒ peer の RPA を解決できない ⇒
保存済み bond と紐付けられない ⇒ 毎回 «新しい peer» として扱われ、自動暗号化が起きない」**

★**ログで «H-auto の予測» を検定した（3 つとも当たった）**：

| H-auto の予測 | 実測 | 結果 |
|---|---|---|
| (a) 再接続のたびに peer アドレスが «違う» | **7 接続で 7 個すべて distinct な RPA** | ★**当たり（7/7）** |
| (b) 解決されていない（`peer_id_addr == peer_ota_addr`） | ★**7/7 で一致＝«解決 0 件»** | ★**当たり** |
| (c) 毎回 «新しい peer» として bond するなら、`MAX_BONDS=3` を超える | **bond 到達 peer = 4 個 > 3** ⇒ **eviction が起きたはず**（`store_status_cb = ble_store_util_status_rr`＝**round-robin で最古を追い出す**・**ログ出力は無い＝silent**） | ★**当たり（整合）** |

⇒ ★**3 予測とも整合する**が、★★**«だから H-auto が正しい» とは書かない**
（**整合は証明ではない**。**単一 run から機序の物語を作らない**）。
⇒ ★**H-auto の «直接の» 検定＝D-7**（**IRK を配れば解決できるはず**）。

★**この現象は «我々との比較» には «まだ» 使えない**：
**我々は ENC\|ID を配っている＝この点で我々の方が «有利» な構成**
⇒ **«stock は再接続が弱い» は «我々の NG» の説明にはならない**（**別の問い**）。

### 14.6 ★★D-7 アーム ＝ **準備完了**（★R2 を閉じる ＋ H-auto を検定する。**2 つを兼ねる**）

**D-7（7 件目の変更・R2/D-7 アーム限定）**＝**stock 自身の Kconfig 2 個**（**我々の config は不輸入**）：
`CONFIG_EXAMPLE_USE_SC=y`（→`ble_hs_cfg.sm_sc = 1`）＋
`CONFIG_EXAMPLE_RESOLVE_PEER_ADDR=y`（→`sm_our/their_key_dist |= BLE_SM_PAIR_KEY_DIST_ID`）
＝`main.c:593-601` を **読んで確認**。⇒ **stock の実行時 SM 設定が我々と一致**
（NoIO / bonding=1 / mitm=0 / **sc=1** / **ENC\|ID**）。

| 項目 | 実測 |
|---|---|
| 広告名 / BLE addr | **`IDFCTL-C5SC`** / `D0:CF:13:F0:A7:46`（★**arm-1 と «同じアドレス»**＝**forget 必須**） |
| 焼込み | **3/3 `verify OK (digest matched)`**・app md5 **`dd2f262e…`**（**arm-1 `4d539921…` と異なる＝delta が出力に届いた**） |
| sdkconfig.h | `CONFIG_EXAMPLE_USE_SC 1` / `CONFIG_EXAMPLE_RESOLVE_PEER_ADDR 1`（**arm-1 側は 両方 0 件＝対照が効いている**） |
| 真cold | `-p 3-4` off → **by-id 消滅を読み戻し** → on。**fresh ログの `connection established`=0** |
| ★**1セル1ボード** | ★**C6 は電源断のまま＝スキャンで ABSENT を実測**／**我々のボードで電波に出ているのは `IDFCTL-C5SC` «1枚だけ»**（**実測**） |
| C3 | ★**触っていない**（`-p 1`=PRESENT のまま。**`ASP3-C3-BLE` は別エージェントのボード＝名前が違うので混同しない**） |

★**D-7 の «出力側» チェック**：`RESOLVE_PEER_ADDR=y` なら **IRK が交換される**⇒
**ログの `peer_id_addr` が `peer_ota_addr` と «違う» 値になる**はず（**arm-1 は 7/7 で一致していた**）
⇒ ★**「アームが生きているか」を config ではなく «出力» で確認できる**。

### 14.7 ★★D-7 の予測（★事前登録。主対照を読み出した «後»＝順序は正しい。**P1-P6 は書き換えない**）

| # | 予測 | 確率 | 根拠（正直に） |
|---|---|---|---|
| **P7** | **D-7 × C5 × Android が «bond に到達» する** | **70%** | **上げる理由**＝(1) **arm-1 は bond 到達済**＝**土台は動く**。(2) `USE_SC`/`RESOLVE_PEER_ADDR` は **stock 自身のオプション**で Espressif が試験している。(3) Android は SC を好む。**下げる理由**＝(4) ★★**D-7 は «IRK 経路» を初めて通す**——**C5 blob は `r_ble_ll_resolv_change_irk`/`_restore_irk`/`_get_index` を «欠く»（0/0/0/0 を実測）**＝**②が «噛む» としたら «ここ»**。(5) **我々の ASP3 は同じ SM 設定（sc=1+ENC\|ID）で bond に到達しない**＝**もし SM 設定が原因なら D-7 も落ちる** |
| **P8** | **P7 が到達した場合に、«2回目以降の自動 bond» が働く**（＝H-auto が正しい） | **60%**（**P7 到達を条件とする条件付き確率**） | **上げる理由**＝(1) **H-auto の 3 予測が全部当たっている**（§14.5）。(2) **IRK を配れば controller が RPA を解決できる**のが設計。(3) **解決できれば peer は 1 個の identity に畳まれ、`MAX_BONDS=3` の churn も消える**。**下げる理由**＝(4) ★**C5 blob の IRK 関数欠如**（**解決は controller 側**＝review N5 で **host privacy は両方 OFF** と実測済）＝**まさにその経路**。(5) **整合は証明ではない**＝H-auto 自体が未証明 |

**★分岐の意味（結果を見る «前» に固定する）**：

| P7（bond 到達） | P8（自動 bond） | 読み |
|---|---|---|
| **到達** | **働く** | ★★**R2 が閉じる**＝**stock は «我々と同じ SM 設定» でも通る** ⇒ **原因の帰属が «我々の統合» に確定**（**探索は我々のコードに限定＝大きい前進**）。**かつ H-auto が支持され、arm-1 の «自動 bond にならない» は «IRK 未配布» で説明がつく（＝バグではない）** |
| **到達** | **働かない** | ★**R2 は «bond leg» については閉じる**（**我々の統合に帰属**）。**ただし H-auto は «死ぬ»**＝**自動 bond の失敗は IRK では説明できない**⇒**別の筋（blob の IRK 関数欠如／`MAX_BONDS` churn／Android 側の挙動）を測る** |
| **到達しない** | — | ★★**原因は «SM 設定そのもの»（sc=1 と/または ENC\|ID）＝我々の config が容疑者の筆頭**（**これも大きい**——**しかも «我々が変えられる» 場所**）。★**その場合は 2 つのフラグを «1 つずつ» 振って切り分ける**（**同時に振らない**）＝**まず `USE_SC=y` のみ、次に `RESOLVE_PEER_ADDR=y` のみ**。★**同時に ②（blob の IRK 経路）が «復活» する**——**`RESOLVE_PEER_ADDR` 単独で落ちるなら ② が生きる** |

★**どちらに転んでも «我々の統合» か «我々の config» に絞れる＝収穫。結果を «期待しない»。**

### 14.8 ユーザーへ渡す手順（★判定条件を «2つとも» 明示する＝前回の不備を直す）

1. ★★**他は全部電源断です。我々のボードで電波に出ているのは `IDFCTL-C5SC` «1枚だけ»**
   （**`IDFCTL-C6` は電源断＝スキャンに出ないことを実測済み**）。
   （`ASP3-C3-BLE` は**別エージェントのボード**・`FMP-*` は**別プロジェクト**＝**どちらも繋がないでください**）
2. **Android で forget**：**`IDFCTL-C5SC`** ／ **`IDFCTL-C5`**（★**同じアドレス `D0:CF:13:F0:A7:46`＝
   前アームの bond が残っていると汚染します**）／ **`ASP3-C5-BLE`** → **BT を OFF→ON**。
3. **スキャン → `IDFCTL-C5SC`（`D0:CF:13:F0:A7:46`）へ接続**。
4. ★★**判定してほしいこと＝«2 つ» あります**：
   - ★**(1) bond した後に «暗号必須特性» が READ «できるか»**
     ＝**サービス `59462f12-9543-9999-12c8-58b459a2712d`** の
     **特性 `33333333-2222-2222-1111-111100000000`** を **READ**
     （**未ペアなら弾かれてペアリング要求が出るのが正しい**→**承認**→**もう一度 READ**）。
     ★**前回これが «未回答» でした（私の手順書の不備です）。今回はここを必ず見てください。**
   - ★**(2) 一度 disconnect して «再接続» した時に、«自動的に» 暗号化/bond されるか**
     （＝**前回「2回目以降 自動的に BOND にならない」と報告された点が変わるか**）。
5. **終わったら「終わった」とだけ伝えてください**（**解釈はこちらで数値から行います**）。


---

## 15. ★★★D-7 実測 ＝ **R2 が閉じた**（P7・P8 とも的中）／**原因は «我々の統合» に確定**

**ユーザー観測（逐語・2件で1セル）**：
> 「**3.0が読めた．**」（判定(1)） ／ 「**再接続で自動でbondになる**」（判定(2)）

**セル条件**：`IDFCTL-C5SC` **のみ電波**（C6 電源断＋ABSENT 実測）・真cold・fresh ログ（開始時 0 接続）。
**アーム＝D-7**（stock 自身の `EXAMPLE_USE_SC=y` + `EXAMPLE_RESOLVE_PEER_ADDR=y`
＝**実行時 SM 設定が我々と一致**：NoIO/bonding=1/mitm=0/**sc=1**/**ENC\|ID**）。

### 15.1 ★アームが «生きている» ことの出力側証明（★これを先に確認した）

★**config を読んで «効いているはず» とはしない**。**出力（ログ）で確認した**：

| conn | `peer_ota_addr` | `peer_id_addr` | 解決 | enc=1 までの時間 |
|---|---|---|---|---|
| **#1** @0.0s | `4c:3e:b4:d2:34:be`(t1=RPA) | `4c:3e:b4:d2:34:be`(t1) | **未解決** | **12.6s＝フルペアリング** |
| （#1 のペアリング後） | `4c:3e:b4:d2:34:be`(t1) | ★**`a8:d1:62:39:5d:fc`(t0=public)** | ★**解決** | — |
| **#2** @39.7s | ★**`77:72:23:ac:a8:1e`(t1)＝別の RPA** | ★**`a8:d1:62:39:5d:fc`(t0)＝同一 identity** | ★**接続時点で解決** | ★**0.1s＝LTK 再利用** |

- ★**#1 が «未解決» なのは «構造上の正解»**＝**IRK はペアリング «中» に配布されるので、初回接続時点では
  その peer の IRK は存在し得ない**。**失敗ではない。**⇒ **bond 後の接続の解決率＝1/1**
  （**arm-1 のベースラインは 0/7**）。
- ⇒ ★★**D-7 は «md5 が違う» だけでなく «挙動» を変えた**＝**IRK が実際に交換され、
  controller が RPA を解決した**。**アームは生きている。**

### 15.2 判定(1)(2) の実測（★ユーザーの言葉と «別々に» 記録する）

| | ユーザーの言葉 | **デバイス側ログ（私の測定）** | 一致 |
|---|---|---|---|
| **判定(1)** | 「**3.0が読めた．**」 | ★**`Characteristic read; attr_handle=29` ×19**（**暗号必須特性の READ が成立**）・`bonded=1` ×4・`ENC status=0` ×2・status=7 は **0 件** | ★**一致** |
| **判定(2)** | 「**再接続で自動でbondになる**」 | ★**conn#2＝別 RPA → 同一 identity へ解決 → 0.1s で `enc=1 bond=1`**（**#1 の 12.6s＝ペアリングとの対比が判別子**） | ★**一致** |

★**「3.0」という値そのものはデバイス側ログに出ない**（bleprph は読まれた値をログしない）
⇒ **「3.0」は nRF Connect の «表示»**であり、**私が測ったのは «READ が成功した» という事実**。
**2 つは別の観測点であり、食い違ってはいない**（**片方が他方を含意しない**ことは明記する）。
接続=2・切断=2（**reason=531 ×2＝`REM_USER_CONN_TERM`＝スマホ側切断**）・**リセット 0 回**。

### 15.3 ★★★R2 が閉じた ⇒ **帰属の確定**

**同じチップ（ESP32-C5 rev v1.0・同一個体 `d0:cf:13:f0:a7:44`）・
同じ blob（★**same inode `15244326`**・1,860,394 B・155 member・**実測**）・
同じ真 v5.5.4 ソース（submodule `735507283d`・clean）・
★同じ SM config（`sc=1` + `ENC|ID` 配布＝**R2 が閉じた**）・同じ toolchain（`esp-14.2.0_20260121`）** で：

| | **我々の ASP3 × C5 × Android** | **stock D-7 × C5 × Android** |
|---|---|---|
| **bond 到達** | ★**一度も到達しない**（`ENC=0x5de00007`＝`ENOTCONN`・PAIRING 不発・**GCC14/GCC15 の 2 アームで 9 レジスタ完全一致**） | ★**到達**（`bonded=1`） |
| **暗号必須特性 READ** | **未到達** | ★**成功（×19）** |
| **再接続の自動 bond** | — | ★**成功（0.1s・LTK 再利用）** |

⇒ ★★★**「C5 × Android の失敗の原因は «我々の統合»（shim/OSAL/起動/アプリ）にある。
チップでも blob でも Espressif のスタックでも SM config でもない。」**

⇒ ★**ただし «統合のどこか» は未特定**。**機序は語らない**（**未計装**）。**ここまでが実測の射程。**

### 15.4 ★★②（blob の IRK 関数欠如）＝ **死んだ**

**②**（`review-ble-c5-vs-c6.md` §4 の仮説・25%・「**変えられない**」＝他エージェントのファイル。**読むだけ**）：
> **C5 の blob は `r_ble_ll_resolv_change_irk`・`r_ble_ll_resolv_irk_change`・
> `r_ble_ll_resolv_restore_irk`・`r_ble_ll_resolv_get_index` を欠く（C6 は持つ）
> ⇒ Android（RPA・鍵配布に ID を含む）の bond が失敗する**

**私は §14.3 で «弱まったが死んだとは書かない» と留保した**（**stock arm-1 は IRK を «配っていない»
＝IRK 経路を通っていなかった**）。**D-7 がその留保を解消した**：

| 前提 | 実測 |
|---|---|
| D-7 は IRK 経路を **実際に通った** | ★**`peer_id_addr`≠`peer_ota_addr`＝IRK 交換・RPA 解決を出力で確認**（§15.1） |
| その blob は 4 関数を **欠いている** | **0/0/0/0**（**検出器較正済**＝`gen_rpa`=1/`resolv_rpa`=3/`get_peer_irk`=1） |
| それでも **成立した** | ★**IRK 解決 ＋ RPA ローテーション跨ぎの自動再接続が成功**（§15.1-15.2） |

⇒ ★★**「C5 の blob は、それら 4 関数なしに IRK 解決と自動再接続をこなせる」＝②は死んだ。**
★**（レビューが自ら「シンボルの不在は機能の不在を証明しない」と留保していた点が、実測で裏付けられた形。**
**レビューの留保が正しかった。）**

### 15.5 H-auto の判定 ＝ **直接の対照で支持された**（★ただし «我々の NG» は説明しない）

| アーム | IRK 配布 | 解決 | 自動 bond |
|---|---|---|---|
| **arm-1**（ENC のみ） | **無** | **0/7** | ★**ならない**（ユーザー：「2回目以降 自動的に BOND にならない」・7 接続すべて別 RPA・bond 到達 peer 4 個 > `MAX_BONDS=3`） |
| **D-7**（ENC\|ID） | **有** | ★**1/1（bond 後）** | ★**なる**（0.1s） |

⇒ ★**H-auto（IRK 未配布 → RPA 未解決 → 毎回新 peer → 自動 bond しない）は «整合» ではなく
«直接の対照» で支持された**（**1 変数だけを振った A/B**）。
⇒ ★★**しかし «我々の NG» は説明しない**——**我々は既に `ENC|ID` を配布しており、
この点では stock arm-1 «より» 有利な設定である**。**別の問いである。**（§14.5 の留保を維持）

### 15.6 ★config 差分の «残り»（★数値を «測り直した»＝§5.2 の 30 は supersede）

★**§5.2 の「30（C5）」は «EXT_ADV=y» の旧ビルドで測った値**（**ユーザーの命名指摘より前**）。
**現行ビルド（EXT_ADV=n）で測り直した**：

| 比較 | 意味的差分 | うち SM/privacy/store 系 |
|---|---|---|
| **arm-1（現行）vs 我々** | **19 / 414** | **1**（`BLE_STORE_MAX_EADS` のみ） |
| **D-7 vs 我々** | **19 / 414** | **1**（同上） |

★**「30 → 19」は «矛盾» ではなく «別のビルドを測った»**（**第8再発＝指標と対象を明示する**）。
**D-7 が MYNEWT_VAL を変えないのは正しい**——**`USE_SC`/`RESOLVE_PEER_ADDR` は
«実行時の `ble_hs_cfg`» を変えるのであって、コンパイル時 knob ではない**。

### 15.7 ★次の探索範囲（★«①が原因» とは書かない）

**容疑者から «外れた»**（実測）：**チップ**・**blob**（same inode）・**Espressif の NimBLE ホスト**・
**controller**・**toolchain**・**SM config（sc/key dist）**・**bond の永続性**（両方 RAM-backed）。

**残る面**（＝**我々の統合**）：
1. **shim/OSAL**（`esp_shim.c`/`bt_shim.c`＝**FreeRTOS の代わりに ASP3 へ橋渡しする層**）
2. **起動**（Direct Boot・クロック初期化）
3. **app**（`ble_host_smoke_c5.c`）
4. **残り 19 個の MYNEWT_VAL 差**（★**うち SM/privacy/store 系は 1 個だけ**＝
   `BLE_STORE_MAX_EADS`＝**暗号化広告データ store で bond とは別系**
   ⇒ ★**「config 差で SM が壊れている」筋は «ほぼ» 死んでいる**）

★**①（C5 app に pend_ring の周期 flush が無い・**変えられる/2行**）の «順位は上がる»**——
**原因が «我々の統合» に確定し、①はその中に在る**から。
★★**しかし «①が原因» とは書かない**：**レビューの4セル表は «C3 idf は flush を «持っていて» NG» を示す
＝flush は十分条件ではない**。**かつ «C5×iPhone は flush 無しで成功する» ＝欠如は単独で非対称を説明しない。**
⇒ ★**次に測るべきは、レビューが «修正より先に» と設計した E1（滞留が実在するか）**
＝**`shim_que_pend_total` の high-water を既存の `storm_monitor_task` からミラーするだけ**
（**hot path に触らない**）。**high-water == 0 なら①は死ぬ＝fix を書く前に殺せる。**

### 15.8 ★★予測の全記録（★**一つも書き換えていない**）

| # | 予測 | 登録値 | 結果 |
|---|---|---|---|
| **P1** | C0＝stock × C6 × Android が OK | **85%** | ★**的中**（両端末 OK） |
| **P2** | 主対照＝stock × C5 × Android が OK | **60%** | ★**的中**（bond 到達。**READ leg は未測定＝§14.4**） |
| **P3** | stock が NG の場合に署名が我々と同型 | **70%** | **判定不能**（**stock は NG にならなかった**＝前件が偽） |
| **P4** | stock × C3 × iPhone が wedge しない | **70%** | **未測定**（§12・C3 は実機未着手） |
| **P5** | stock × C3 × Android が再接続まで OK | **65%** | **未測定** |
| **P6** | P4 が NG の場合に署名が同型 | **60%** | **未測定** |
| **P7** | **D-7 × C5 × Android が bond 到達** | **70%** | ★**的中** |
| **P8** | **P7 到達を条件に «自動 bond» が働く** | **60%** | ★**的中** |

★**改訂は 1 件も行っていない**（**改訂するなら «測定前・理由つき・別番号» の規則**）。
★**P3 は «前件が偽» なので «外れ» ではなく «判定不能»**と記す（**当たったことにしない**）。


---

## 16. stock × C3 × Android ＝ **準備完了**（ユーザー決定「stock x C3も試す」）

### 16.1 ★★実験設計の判断：**arm-1（stock 既定）ではなく D-7 相当（SC=1 + IRK）を使う**

**親から «どちらを先にやるかは実験設計の判断» として委譲された。決めた理由（★C5 の実測に基づく）**：

1. ★★**判定(2)（再接続の自動 bond）が arm-1 では «原理的に» 測れない**——
   **C5 で実測した**：**arm-1（ENC のみ・IRK 未配布）は «構造上» 自動 bond できない**
   （**解決 0/7・7 接続すべて別 RPA・ユーザー観測「2回目以降 自動的に BOND にならない」**）。
   ⇒ **C3 で arm-1 を使えば «既知の artifact» を再生産するだけ**＝**判定(2) は C3 について何も語らない。**
2. **R2 を «最初から» 閉じられる**。**C5 では arm-1 → D-7 の 2 段階に «ユーザーのセルを 1 つ余分に» 使い、
   その間 sc/key-dist の交絡が開いたままだった**。**C3 で繰り返す必要はない。**
3. **D-7 は stock で «動く» ことが実証済み**（C5：bond＋READ ×19＋0.1s の自動再接続）
   ⇒ **C3 で落ちたら «C3 固有» を指す**（**config の artifact ではない**）。
4. ★**最も «きれいな» 比較になる**：**同じ stock コード・同じ SM config・同じ toolchain・同じ供給で、
   «チップだけ» が違う**：
   > **D-7 × C5 × Android ＝ OK（実測）** vs **D-7 × C3 × Android ＝ ？**

**★コスト/リスク（申告）**：
- **新アーム＝ビルドが必要**。★**これは親が警告した «同じアームを再ビルドするな»（＝二変数実験）には
  当たらない**：**`main.c`(19:57) / `gatt_svr.c`(19:29) は C3 arm-1 ビルド(20:19) «より前» から不変**
  ＝**差分は per-target sdkconfig だけ**（**mtime で実測**）。
- ★**P4/P5/P6 は §12 で «arm-1 の C3 構成» に対して登録した**⇒ **本セルには適用されない**。
  ★**書き換えない**＝**«登録済み・未測定» のまま残し、D-7 C3 には «別番号» を登録する**（§16.4）。
- **arm-1 C3（`IDFCTL-C3`・ビルド済＋一度 flash 済）は fallback として保持**（`build_esp32c3`）。

### 16.2 準備の実測（★1セル1ボードを «測って» 満たす）

| 項目 | 実測 |
|---|---|
| **他ボードの排除** | ★**C5 を電源断→by-id 消滅を読み戻し→BlueZ キャッシュ全消去 → 22s 再探索で `IDFCTL-C5SC`=**ABSENT**・`IDFCTL-C6`=**ABSENT** を «測定»**（«切ったから見えないはず» ではない） |
| **上書き前の dump** | ★**取得済**＝`tmp/asp3_flash_backup/c3_asp3_full_4MB.bin`（md5 `6d7640fe…`） |
| **載っていたビルドの同定** | ★**`build/rb_A_ble/asp_flash.bin` に一意一致**（373 候補と照合。**アーム A＝親の言うとおり**） |
| **戻し先の保全** | **`rb_A_ble`/`rb_B_ble`/`w1_A`/`w1_B` の 4 つとも present を確認＝消していない** |
| **復旧手順の実地検証** | ★**上書き «前» に no-op write-back → `verify OK (digest matched)`**（C5/C6 と同じ型） |
| 焼込み | **D-7 C3 アーム**・**3/3 `verify OK`**・app md5 **`723746e3…`**（arm-1 C3 は `a1fac378…`） |
| **★D-6 が効いているか** | ★★**実機で «測った»**：`dtr=False/rts=False` で open → **3733 B のブートダンプ**＋`rst:0x15 (USB_UART_CHIP_RESET)` ⇒ **USB-JTAG CDC にコンソールが «実際に出ている»**＝**D-6 は有効**（**D-6 が無ければここは 0 B＝盲目の検出器**） |
| **open がリセットするか** | ★**stock アプリで «自分で» 確認**：`dtr=True/rts=True` → **ブートバナー 0 件＝リセットしない**（**別エージェントの C3 実測と一致**）／`F/F` → **リセットする**（上記）。**ポートは較正済み＝沈黙は «アプリが生きて静か» と読める** |
| **EXT_ADV** | **OFF**：ELF `ble_gap_ext_adv_start`=**0**/`ble_gap_adv_start`=**1**＋**実行時ログが `GAP procedure initiated: advertise`（«extended advertise» ではない）** ⇒ **HCI 4.2 の `hci0` でも見える**（**C6 の «見えない» 罠を回避**） |
| **BLE アドレス（実測）** | ★**`60:55:F9:57:BA:BE`**＝**base MAC `…BA:BC` +2**（**C5/C6 と同じ規則**）。★**ASP3 は `…BA:BC`（base そのもの）で広告していた**＝**我々と stock で BLE アドレスが違う**（C5 と同じ差） |
| 真cold | `-p 1` off → **by-id 消滅を読み戻し**（`c3=GONE`）→ on。**C5/C6 は GONE のまま** |
| ログ | **新規ファイル**・**開始時 `connection established`=0**・**リセットバナー 0** |
| **在席** | ★**`60:55:F9:57:BA:BE` `IDFCTL-C3SC` `Paired=False`**／**我々のボードで電波に出ているのは «1枚だけ»（実測）** |

★**ROM 出力は USB 列挙より前に失われる**⇒ **「コンソールで真cold を観測した」とは書かない**。
**真cold の証拠は by-id 消滅の読み戻しのみ。**

### 16.3 ★C3 の «我々の» 既知（比較対象。別エージェントの実測＝読むだけ）

| セル | 我々の ASP3 | 病態 |
|---|---|---|
| **C3 × iPhone** | **NG** | `PAIR=0x5dc00000`・`ENC=0x5de00000`（**デバイス側は成功**）・★**`DISC=0`＝wedge**（**切断が届かず、リンクを握って広告が止まる＝復旧は電源断のみ**） |
| **C3 × Android** | **bond OK・再接続できない** | `DISC=0xd15c1303`（**切断は届く**）・`CONN`=4 |

★**C5×Android の失敗（`ENOTCONN`・bond 未到達）とは «別の病態»**
⇒ ★**「C5 で我々のせいだったから C3 も」と外挿しない。**

### 16.4 ★★予測の事前登録（★**P1-P8 は書き換えない**。**D-7 C3 は別番号**）

| # | 予測 | 確率 | 根拠（正直に） |
|---|---|---|---|
| **P9** | **D-7 × C3 × Android が bond 到達＋暗号必須特性 READ 成功**（判定(1)） | **80%** | **上げる**＝(1) **我々の ASP3 ですら C3×Android は bond 到達する**（`0x5dc00011`）＝**C3 の bond 経路は元々動く**。(2) **D-7 は C5 で完動**。(3) **C3 は最も枯れたチップ**。**下げる**＝(4) **C3 の blob は別系統（旧世代 `lib_esp32c3_family`）**＝**D-7（SC+IRK）を C3 blob で通すのは本ラウンド初** |
| **P10** | **P9 到達を条件に、判定(2)＝再接続の自動 bond が働く** | **65%**（条件付き） | **上げる**＝(1) **C5 D-7 で 0.1s の LTK 再利用を実測**＝**機構は stock で動く**。(2) **IRK 配布で RPA 解決できれば «毎回新 peer» が消える**（C5 で実証）。**下げる**＝(3) ★**我々の ASP3 C3×Android は «再接続できない»**＝**C3 固有の再接続問題が «上流にも» ある可能性**。(4) **C3 blob は別系統＝IRK 解決の実装が C5 と同じとは限らない**（★**C5 blob は 4 関数を欠いていても解決できた＝シンボルの有無で予断しない**） |
| **P11** | **判定(3)＝切断後に «再接続できる»（wedge しない）** | **85%** | **上げる**＝(1) **我々の ASP3 でも C3×Android は «切断が届く»**（`DISC=0xd15c1303`）＝**wedge は iPhone 固有**。(2) **C5 D-7 は 2 接続とも正常に切断・再接続**。**下げる**＝(3) **未測定**。★**wedge が Android でも起きたら «我々の ASP3 の Android は切断が届く» と矛盾＝stock 固有の問題を疑う** |

**★分岐の意味（測定前に固定）**：

| 結果 | 読み |
|---|---|
| **P9 到達 ＋ P10 働く ＋ P11 再接続可** | ★**「C3×Android について stock は完動」**⇒ **我々の C3×Android «再接続できない» は «我々の統合» に帰属**（**C5 と同じ結論が別の病態でも成立**）。★**ただし «統合のどこか» は未特定** |
| **P9 到達 ＋ P10 働かない** | ★**再接続の自動 bond が «C3 では stock でも» 働かない**⇒ **C3 固有の «上流» 問題**（**C5 では働いた＝チップ差**）。★**我々の «再接続できない» を我々のバグと呼べなくなる**＝**大きい** |
| **P9 未到達** | ★**stock ですら C3×Android で bond できない**⇒ **上流／または D-7 構成が C3 blob と噛み合わない**。★**その場合 arm-1 C3（fallback・ビルド済）に落として «SC+IRK が原因か» を切り分ける**（**フラグは 1 つずつ**） |
| **P11 で wedge** | ★**Android でも wedge＝我々の ASP3 の «Android は切断が届く» と矛盾**⇒ **stock 固有 or 端末差**。**iPhone セルの前に必ず解明する** |

★**どちらに転んでも収穫。結果を «期待しない»。**

### 16.5 ユーザーへ渡す手順（★Android 先・iPhone は «別セル» で後）

★**セルの順序の理由（我々の ASP3 実測）**：**C3 × iPhone は wedge する**（`DISC=0`＝リンクを握って広告停止）
⇒ **復旧は電源断のみ ⇒ それが RAM bond を消す** ⇒ ★**iPhone を先にやると Android のセルが壊れる**。
**stock が wedge するかは未知だが «するかもしれない» 以上、同じ順序が安全。**

1. ★★**他のボードは «全部» 電源断です。我々のボードで電波に出ているのは `IDFCTL-C3SC` «1枚だけ»**
   （**`IDFCTL-C5SC`・`IDFCTL-C6`・`ASP3-C3-BLE` が スキャンに «居ない» ことを測定済み**）。
   （`FMP-ESP32-BLE`/`FMP-ESP32S3-BLE` は**別プロジェクト**・`VIPER` は**無関係**＝**繋がないでください**）
2. **Android で forget**：**`IDFCTL-C3SC`** ／ ★**`ASP3-C3-BLE`**（**我々の別アプリ。今は焼かれていませんが、
   スマホ側に古い bond が残っていることがあります**）→ **BT を OFF→ON**。
3. **スキャン → `IDFCTL-C3SC`（★**`60:55:F9:57:BA:BE`**）へ接続**。
4. ★★**判定してほしいこと＝«3 つ»**：
   - ★**(1) bond した後に «暗号必須特性» が READ できるか**
     ＝**サービス `59462f12-9543-9999-12c8-58b459a2712d`** の
     **特性 `33333333-2222-2222-1111-111100000000`** を **READ**
     （**未ペアなら弾かれてペアリング要求が出るのが正しい**→**承認**→**もう一度 READ**）。
   - ★**(2) 一度 disconnect して «再接続» した時に、«自動的に» bond/暗号化されるか。**
   - ★★**(3) そもそも «切断した後に、また接続できるか»**——
     **我々の ASP3 では «詰まって» 復旧できないことがありました**（**C3 の核心**）。
5. **終わったら「終わった」とだけ伝えてください**（**解釈はこちらで数値から行います**）。
6. **iPhone は «この後」別セルで行います**（**同時にやらないでください**——**wedge すると Android の
   測定が壊れるため**）。


---

## 17. ★★stock D-7 × C3 × Android ＝ **3判定とも到達**（P9・P10・P11 すべて的中）

**ユーザー観測（逐語）**：「**android : all ok**」
★**「all ok」が «3判定とも» を指すかは推定**なので、**ログで «到達» を個別に測った**。

**セル条件**：`IDFCTL-C3SC` **のみ電波**（他は電源断＋**キャッシュ消去して再探索で ABSENT を測定**）・
真cold・fresh ログ（開始 0 接続）・**セッション中リセット 0**。

### 17.1 ★アームが «生きている» ことの出力側証明（★C5 で確立した型。**先に確認した**）

| conn | `peer_ota_addr` | `peer_id_addr` | 解決 | enc=1 まで |
|---|---|---|---|---|
| **#1** @0.0s | `71:6f:ee:c5:3a:92`(t1=RPA) | `71:6f:ee:c5:3a:92`(t1) | **未解決** | **6.5s＝フルペアリング** |
| （#1 ペアリング後） | 同 | ★**`a8:d1:62:39:5d:fc`(t0=public)** | ★**解決** | — |
| **#2** @21.7s | ★**`47:e6:a5:d2:80:ef`(t1)＝別 RPA** | ★**`a8:d1:62:39:5d:fc`** | ★**接続時点で解決** | ★**0.2s＝LTK 再利用** |
| **#3** @36.8s | `47:e6:a5:d2:80:ef`(t1) | ★**`a8:d1:62:39:5d:fc`** | ★**解決** | ★**0.2s＝LTK 再利用** |

- **解決イベント＝13/14**（**唯一の «未解決» は conn#1＝構造上の正解**＝IRK はペアリング «中» にしか配られない）。
- ⇒ ★★**アームは生きている**（**IRK が実際に交換され、controller が RPA を解決した**）。
- ★**副産物＝`peer_id` が `a8:d1:62:39:5d:fc`＝§15 の C5 D-7 セルと «同一の identity»**
  ⇒ ★**同じ Android 端末が両セルを実施したことが «実測で» 確定**（**IRK 配布により
  «セッションを端末に帰属できない» 制約が D-7 アームでは «解けた»**）。

### 17.2 3判定の実測（★ユーザーの言葉と «別々に» 記録）

| 判定 | ユーザー | **デバイス側ログ（私の測定）** | 一致 |
|---|---|---|---|
| **(1) bond 後の暗号必須特性 READ** | 「all ok」 | ★**`Characteristic read; attr_handle=29` ×6**・`bonded=1` ×6・`ENC status=0` ×3・**status=7 は 0 件** | ★**支持** |
| **(2) 再接続で自動暗号化** | 「all ok」 | ★**conn#2/#3 とも 0.2s で `enc=1 bond=1`**（**#1 の 6.5s＝ペアリングとの対比が判別子**） | ★**支持** |
| **(3) 切断後に再接続できるか（wedge しないか）** | 「all ok」 | ★**接続 3 / 切断 3**・**切断 reason は全部 `531`＝`0x213`＝`REM_USER_CONN_TERM`＝«届いている»**・**その後 再接続が 2 回成立** | ★**支持** |

⇒ ★**「all ok」は 3 判定とも «ログが独立に裏付けた»**（**推定に頼っていない**）。

### 17.3 ★★★2 チップ目：**C3 の «再接続できない» も «我々の統合» にある**

**我々の ASP3 × C3 × Android の既知**（別エージェントの実測・アーム B＝esp-idf＋GCC14。**読むだけ**）：
**bond は成立**（`PAIR=0x5dc00011`）**だが «再接続できない»**
（`DISC=0xd15c1303`＝**切断は届く**・`CONN`=4＝**デバイスは受け入れる**・**切っているのはスマホ側**・
ユーザー報告＝「**DISCONNECT したあと CONNECT できない**」）。

**stock D-7 × C3 × Android（本セル）**＝★**再接続が 2 回とも成立し、0.2s で自動暗号化された。**

⇒ ★★**「C3 の再接続問題も «我々の統合» にある」**＝**C5 に続いて 2 チップ目。**

★**射程（厳格に）**：
- ★**«統合のどこか» は未特定**——**そこまで。機序は語らない**（**未計装**）。
- ★★**C5 の失敗（`ENOTCONN`・bond 未到達）と C3 の失敗（bond 到達・再接続不可）は «別の病態»**
  ⇒ **「同じ原因」とは書かない。** **言えるのは «どちらも我々の側にある» まで。**
- ★**«2 チップで我々のせい» は «統合が容疑者» を強めるが «同じ箇所» を意味しない。**

### 17.4 予測（★書き換えていない）

| # | 予測 | 登録値 | 結果 |
|---|---|---|---|
| **P9** | D-7 × C3 × Android が bond 到達＋暗号必須特性 READ | **80%** | ★**的中** |
| **P10** | （P9 条件付き）再接続の自動 bond が働く | **65%** | ★**的中** |
| **P11** | 切断後に再接続できる（wedge しない） | **85%** | ★**的中** |

---

## 18. ★★★次セル＝**stock D-7 × C3 × iPhone**（**本プロジェクト最頑固の軸**。準備完了）

### 18.1 なぜ最も価値が高いか

**C3 × iPhone は «供給でも toolchain でも動かなかった唯一の軸»**
（**hal でも esp-idf でも／GCC14 でも GCC15 でも NG**）。
**病態＝`PAIR`/`ENC` とも status 0（★デバイス側は «成功している»）のに iPhone が timeout し、
`DISC=0`＝切断が届かず wedge（リンクを握って広告が止まる／復旧は電源断のみ）。**

| 結果 | 意味 |
|---|---|
| **stock も wedge する** | ★**上流＝我々が直す対象ではない**（**「直せない」ではなく «我々の仕事ではない»**）。**追うのをやめる判断ができる** |
| **stock は wedge しない** | ★**3 つ目も «我々の統合»**（C5・C3-Android に続く） |

★**どちらでも収穫。期待しない。**

### 18.2 準備の実測（★Android セルと «セントラルだけ» が違う）

| 項目 | 実測 |
|---|---|
| アーム | ★**同一（`IDFCTL-C3SC`）・再ビルドしていない** |
| **同一バイナリの証明** | ★**3/3 `verify OK (digest matched)`**（**Android セルを走らせたのと «同じ» オンチップ内容**）・app md5 `723746e3…` ⇒ ★**«同一バイナリの非対称»（C6 で確立した型）が成立する条件を満たした**＝**差は «セントラルだけ»** |
| BLE アドレス | `60:55:F9:57:BA:BE` / 広告名 `IDFCTL-C3SC` / `Paired=False` |
| **真cold** | `-p 1` off → **by-id 消滅を読み戻し**（`c3=GONE`）→ on。**C5/C6 は GONE のまま**・**hub 1-5 は PRESENT＝無傷** |
| ★**Android の鍵が片側だけ残る問題** | ★**デバイスは真cold で RAM bond を失った**（**fresh ログ 0 接続で確認**）⇒ **数分前に bond した Android が鍵を持ったまま自動再接続すると本セルを汚す** ⇒ ★**ユーザーに «両方の» スマホで forget を依頼**（§18.4） |
| ログ | **新規ファイル**・**開始 0 接続**・**リセットバナー 0**（**open は非リセット＝ポートは較正済み**） |
| **1セル1ボード** | ★**`IDFCTL-C5SC`・`IDFCTL-C6`・`ASP3-C3-BLE` の ABSENT を «キャッシュ消去 → 再探索» で測定**／**我々のボードで電波に出ているのは 1 枚だけ** |

### 18.3 ★予測の事前登録（★**P1-P11 は書き換えない**）

| # | 予測 | 確率 | 根拠（正直に） |
|---|---|---|---|
| **P12** | **stock D-7 × C3 × iPhone が «wedge しない»**（＝切断が届き、再接続できる） | **75%** | **上げる**＝(1) ★**本日 2 チップで «我々の統合» に着地した**（C5 bond 未到達・C3 再接続不可）＝**同じ層が iPhone wedge も説明しうる**。(2) ★**wedge の症状（«デバイス側は成功したのに切断イベントが届かない»）は «イベント配送» の症状**であり、**それを担うのは我々の shim/OSAL**。(3) **stock C6 × iPhone は «all ok»**（C0 実測）＝**stock の iPhone 経路は動く**。(4) **C3 は最も枯れたチップ**。**下げる**＝(5) ★★**外挿は禁物＝iPhone wedge は C5/C3-Android とは «別の病態»**（**«2 チップで我々のせい» は «3 つ目も» を意味しない**）。(6) **C3 blob は別系統（旧世代）**＝**上流の余地は残る**。(7) ★**「供給でも toolchain でも動かなかった」＝我々が «まだ当てていない» 軸が上流にある可能性を否定できない** |
| **P13** | **（P12 が wedge しない場合）bond 到達＋暗号必須特性 READ 成功** | **85%**（条件付き） | **上げる**＝**stock C6×iPhone・stock C3×Android とも READ 成功**。**我々の ASP3 ですら C3×iPhone は `PAIR`/`ENC` status 0＝デバイス側は成功していた**。**下げる**＝**iPhone は SC を要求するなど挙動が Android と違う**（**未測定**） |
| **P14** | **（wedge しない場合でも）iPhone が «1回目 disconnect で timeout エラー» を表示する** | **80%** | ★**§10.4 で «stock C6 × iPhone の «all ok» セッションでも出る» と実測済**＝**上流の性質**。**これが出ても NG と判定しない**（**R7**） |

**★分岐の意味（測定前に固定）**：

| 結果 | 読み |
|---|---|
| **wedge しない（P12 的中）** | ★**3 つ目も «我々の統合»**＝**C5（bond 未到達）・C3-Android（再接続不可）・C3-iPhone（wedge）の «3 病態» がすべて我々の側**。★**ただし «同じ箇所» とは言わない**（3 つとも別の病態）。⇒ **探索は我々のコードに限定＝本ラウンドの最大の成果** |
| **wedge する（P12 外れ）** | ★★**上流＝我々の仕事ではない**（**「我々のポートを直しても直らない」**）⇒ **C3×iPhone を追うのをやめる判断ができる**。★**その場合 «我々の統合に着地した C5/C3-Android» の結論は «変わらない»**（**別の病態＝独立**） |

### 18.4 ユーザーへ渡す手順（★iPhone・**wedge したら電源断が要る**と明記）

1. ★★**他のボードは全部電源断です。我々のボードで電波に出ているのは `IDFCTL-C3SC` «1枚だけ»**
   （`IDFCTL-C5SC`・`IDFCTL-C6`・`ASP3-C3-BLE` が **スキャンに居ないことを測定済み**）。
   （`FMP-*`・`VIPER` は**無関係**＝**繋がないでください**）
2. ★★**«両方の» スマホで forget してください**：
   - **iPhone**：**`IDFCTL-C3SC`** ／ **`ASP3-C3-BLE`**
   - ★**Android も**：**`IDFCTL-C3SC`**
     （★**理由＝Android は数分前にこのデバイスと bond して鍵を持っています。
     デバイスは真cold で鍵を失いました。放置すると Android が «自動再接続» してセルを汚します**
     ——**これで 9 時間溶かした事故があります**）
   → **両方とも forget 後に BT を OFF→ON**。
3. **iPhone でスキャン → `IDFCTL-C3SC`（`60:55:F9:57:BA:BE`）へ接続**。
4. ★**判定してほしいこと＝«4 つ»**：
   - **(1) bond 後に暗号必須特性が READ できるか**
     ＝**サービス `59462f12-9543-9999-12c8-58b459a2712d`** の
     **特性 `33333333-2222-2222-1111-111100000000`** を **READ**
     （**未ペアなら弾かれてペアリング要求が出るのが正しい**→**承認**→**もう一度 READ**）
   - **(2) 一度 disconnect して «再接続» した時に自動的に暗号化されるか**
   - ★★**(3) そもそも «切断した後に、また接続できるか»**（★**我々の ASP3 ではここで «詰んで» いました**）
   - ★**(4) «最初の接続で timeout するか»**（★**我々の ASP3 では «ペアリング要求 → OK を押す → timeout»**）
5. ★★**もし «詰まって» 何もできなくなったら、それ自体が重要な結果です。
   無理に復旧しようとせず «詰まった» と伝えてください**
   （★**復旧は電源断のみ＝こちらでやります。電源断は RAM の鍵を消すので、
   こちらが «報告をもらってから» 操作します**）。
6. **終わったら「終わった」または「詰まった」とだけ伝えてください。**


---

## 19. ★★★stock D-7 × C3 × iPhone ＝ **wedge しない**（P12・P13・P14 すべて的中）

**ユーザー観測（逐語）**：「**iphone : all ok (再接続のあとサービスを使用して切断したらtimeoutエラー)**」
★**「all ok」を鵜呑みにせず、脚ごとに独立に測った。**

**セル条件**：**Android セルと «同一のオンチップ内容»（3/3 `verify OK`・md5 `723746e3…`）
＝差は «セントラルだけ»**・真cold・fresh ログ（開始 0 接続）・**セッション中リセット 0**。

### 19.1 ★★判定(3)＝**wedge しない**（＝本セルの核心。**物証＝切断が «届いた» か**）

| | **我々の ASP3 × C3 × iPhone** | **stock D-7 × C3 × iPhone** |
|---|---|---|
| **切断イベント** | ★**`DISC=0`＝«一度も届かない»**（**リンクを握って広告停止＝復旧はリセットのみ**） | ★**接続 3 / 切断 3・reason は全部 `531`＝`0x213`＝`REM_USER_CONN_TERM`＝«届いている»** |
| **再接続** | ★**不能（wedge・回復不能ループ）** | ★**2 回成立**（切断後に `GAP procedure initiated: advertise` で再広告 → 再接続） |

⇒ ★★★**stock は wedge しない。**

### 19.2 4 判定の実測（★ユーザーの言葉と «別々に»）

| 判定 | ユーザー | **デバイス側ログ** | 一致 |
|---|---|---|---|
| **(1) 暗号必須特性 READ** | 「all ok」 | ★**`Characteristic read; attr_handle=29` ×5**・`bonded=1` ×8・`ENC status=0` ×3・**status=7 は 0** | ★**支持** |
| **(2) 再接続で自動暗号化** | 「再接続のあと…」 | ★**conn#2/#3 は «接続時点で既に» `enc=1 bond=1`**（§19.3） | ★**支持** |
| **(3) 再接続できるか** | 「再接続のあと…」 | ★**接続 3・切断 3（全部届く）・再接続 2 回**＝**wedge なし** | ★**支持** |
| **(4) 最初の接続で timeout するか** | 「**再接続のあと** サービスを使用して切断したら timeout」 | **conn#1 は 8.1s でペアリング完了 → 即 READ 成功**＝**初回 timeout の痕跡なし** | ★**支持（timeout は «初回» ではない）** |

★**「timeout エラー」は R7 どおり NG と読まない**——**§10.4 で «stock C6 自身の «all ok» セッションの中にも
出ていた» と実測済**＝**上流（Espressif のスタックと iPhone の相互作用）の性質**。**P14 の的中。**

### 19.3 ★私の測定バグ（自己申告④）＝**iPhone では ENC が CONNECT «より前» にログされる**

**私の timing スクリプトは「connect → 次の enc イベントまでの時間」を計算していた**が、
**iPhone の再接続では enc イベントが connect 行 «より前» に出る**ため、
**スクリプトは «次の接続の» enc を拾い、`7.4s FULL PAIRING` という «偽の値» を出した**。

**生ログ（再接続時・真の順序）**：
```
327600  disconnect reason=531                    enc=1 bond=1
327600  GAP procedure initiated: advertise        <- 再広告している
329190  encryption change event; status=0        enc=1 bond=1   <- connect «より前»
329190  subscribe event
329250  connection established; status=0         enc=1 bond=1   <- conn_desc が «既に» 暗号化済
```

★**正しい判別子＝«接続時点の conn_desc の状態»**（**時間差ではない**）：

| セル | `connection established` 時点の状態 | 読み |
|---|---|---|
| **C3 iPhone** | **1 回が `enc=0 bond=0`**（＝conn#1・ペアリング要）／★**2 回が `enc=1 bond=1`**（＝**既に暗号化済＝LTK 再利用**） | **自動暗号化 ✓** |
| **C3 Android** | **3/3 が `enc=0 bond=0`** → その後 **0.2s** で `enc=1`（**通常順序**） | **自動暗号化 ✓**（§17 の読みは維持） |
| **C5 D-7 Android** | **2/2 が `enc=0 bond=0`** → **0.1s** で `enc=1`（**通常順序**） | **自動暗号化 ✓**（§15 の読みは維持） |

⇒ ★**2 つの独立な指標（状態／時間差）が «同じ結論» を出す**＝**再接続は LTK 再利用。**
⇒ ★**順序が反転する «理由» は語らない**（**未計装**）。**観測した事実として記録するだけ。**
★**この罠は C0 のログにも既に出ていた**（§10.1 の `501287 ENC → 501347 CONNECT`）
＝**私は当時 «奇妙» と気づきながら追わなかった**。**今回スクリプトが偽値を出して初めて実害になった。**

### 19.4 予測（★書き換えていない）

| # | 予測 | 登録値 | 結果 |
|---|---|---|---|
| **P12** | stock D-7 × C3 × iPhone が wedge しない | **75%** | ★**的中** |
| **P13** | （条件付き）bond 到達＋暗号必須特性 READ | **85%** | ★**的中** |
| **P14** | iPhone が timeout エラーを表示する（★NG 判定に使わない） | **80%** | ★**的中** |

---

## 20. ★★★総括 — **BLE の既知の失敗 3 軸は «すべて» 我々の統合にある**

### 20.1 3 軸の対照（★すべて «同一個体・同一 blob・同一ソース・同一 toolchain・同一 SM config»）

| 軸 | **我々の ASP3** | **stock（D-7）** | 帰属 |
|---|---|---|---|
| **C5 × Android** | ★**bond に一度も到達しない**（`ENC=0x5de00007`＝`ENOTCONN`・**GCC14/GCC15 の 2 アームで 9 レジスタ完全一致**） | ★**到達＋READ ×19＋0.1s の自動再接続** | ★**我々** |
| **C3 × Android** | ★**bond するが再接続できない**（`DISC=0xd15c1303`＝切断は届く・`CONN`=4） | ★**接続 3・再接続 2・0.2s で自動暗号化** | ★**我々** |
| **C3 × iPhone** | ★★**wedge**（`DISC=0`＝**切断が届かない**・**復旧はリセットのみ＝それが RAM bond を消す＝回復不能ループ**） | ★**wedge せず**（切断 3 回とも `531` で届く・再接続 2 回） | ★**我々** |

### 20.2 ★容疑者から «外れた» もの（★すべて «実測の対照» で外した。推論ではない）

| 容疑者 | 外した根拠（実測） |
|---|---|
| **チップ（ESP32-C5 / C3 のシリコン）** | **同一個体**（`d0:cf:13:f0:a7:44` / `60:55:F9:57:BA:BC`）**で stock は 3 軸とも通る** |
| **blob（`libble_app.a` 等）** | ★**stock と我々は «同じファイル» をリンク＝same inode `15244326`**（C5・1,860,394 B・155 member）。**準空アーカイブではない**（`libcore.a` 事故の型の否定） |
| **Espressif の NimBLE ホスト / controller** | **stock は同じ真 v5.5.4 submodule（`735507283d`・clean）で 3 軸とも通る** |
| **toolchain** | **3 チップとも `esp-14.2.0_20260121`（IDF 標準）で実測**。**別途 GCC14/GCC15 の 2 アームで 9 レジスタ一致（C5）** |
| **SM config（`sm_sc` / key dist）** | ★**D-7 で R2 を閉じた**＝**stock を «我々と同一の» sc=1 + ENC\|ID にしても 3 軸とも通る** |
| **bond の永続性（NVS）** | **stock も RAM-backed**（`NVS_PERSIST` 既定 n・ELF で `ble_store_config_persist_*`=0）＝**我々と同じ** |
| **②：C5 blob の IRK 4 関数欠如** | ★**死んだ**——**D-7 は IRK 経路を «実際に» 通り（`peer_id`≠`peer_ota` を出力で確認）、4 関数が 0/0/0/0 のままの C5 blob で RPA 解決＋自動再接続が成立** |
| **`ENC status=7` が «バグの指紋»** | ★**死んだ**——**stock の «all ok» セッション内にも出る**（C6・C5 arm-1 で実測）。**値の一致で判定してはならない** |
| **拡張アドバタイズ（EXT_ADV）** | **我々と揃えた**（D-3）＝**3 軸とも legacy adv で通る** |

### 20.3 ★残るもの＝**我々の統合**（＝次フェーズの入力）

| 面 | 実測の状況 |
|---|---|
| **shim / OSAL**（`esp_shim.c` / `bt_shim.c`＝**FreeRTOS の代わりに ASP3 へ橋渡し**） | **未計測**。★**stock はこの層が «存在しない»**（FreeRTOS 本体を使う） |
| **起動**（Direct Boot・クロック初期化） | **未計測**。★**stock は IDF 自前のスタートアップ** |
| **app**（`ble_host_smoke_c5.c` 等） | **未計測** |
| **config の残差** | ★**測った＝19/414**（**うち SM/privacy/store 系は `BLE_STORE_MAX_EADS` の 1 個のみ**）⇒ ★**「config 差で SM が壊れている」筋は ほぼ死んでいる** |

### 20.4 ★★射程（★これを超えて書かない）

**書ける**：
- ★**BLE の既知の失敗 3 軸は «すべて» 我々の統合にある**（§20.1・§20.2 の対照による）。
- ★**新たに «強力な探索軸» が手に入った**＝**stock は 3 軸とも通る**
  ⇒ ★**«stock と我々の差» のどこかに原因がある**（**差は §20.3 の 4 面に限定された**）。

**書かない（★禁止）**：
- ★**«統合のどこか»＝未特定・未計装。**
- ★**「C5/C3 の BLE は直せる」**——**原因未特定＝直せる保証は無い。**
- ★**stock との差分から «犯人» を名指しすること**——**それは次フェーズの «仮説» であって今日の実測ではない。**
- ★★**「3 軸とも我々」＝「同じ原因」ではない**——**3 つは «別の病態»**
  （**bond 未到達／再接続不可／wedge**）。**«統合が容疑者» を強く支持するが «同じ箇所» を意味しない。**

### 20.5 ★次の探索範囲と、レビュー仮説①の位置づけ

**①（`review-ble-c5-vs-c6.md` §4・読むだけ）＝「C5 app に pend_ring の周期 flush が無い」**
（**35%・変えられる/2行**）：

- ★**位置づけは «上がる»**——**原因が «我々の統合» に確定し、①はその中に在る**から。
- ★★**しかし «①が原因» とは書かない**：
  - **レビューの 4 セル表＝「C3 idf は flush を «持っていて» NG」**＝**flush は十分条件ではない**。
  - **「C5 × iPhone は flush 無しで成功する」**＝**欠如は単独で非対称を説明しない**。
  - **かつ①は C5 の話であり、C3 の 2 軸（再接続不可・wedge）を説明しない。**
- ⇒ ★**レビューが «修正より先に» と設計した E1（滞留が実在するか）が次**
  ＝**`shim_que_pend_total` の high-water を既存の `storm_monitor_task` からミラーするだけ**
  （**hot path に触らない**）。**high-water == 0 なら①は死ぬ＝fix を書く前に殺せる。**
- ★**新しい探索軸＝「stock と我々の差」**（§20.3）。**stock は 3 軸とも通るので、
  «差» の集合は原因を «含む»。この集合は 4 面に絞られている。**

### 20.6 ★予測の全記録（★**P1-P14 を一つも書き換えていない**）

| # | 予測 | 登録値 | 結果 |
|---|---|---|---|
| **P1** | C0＝stock × C6 × Android が OK | 85% | ★**的中**（両端末 OK） |
| **P2** | stock × C5 × Android が OK | 60% | ★**的中**（bond 到達。**READ leg は未測定**） |
| **P3** | stock が NG なら署名が我々と同型 | 70% | **判定不能**（**前件が偽**＝stock は NG にならなかった。**当たったことにしない**） |
| **P4** | stock × C3 × iPhone が wedge しない（**arm-1 構成に登録**） | 70% | **未測定**（**arm を D-7 に変更したため適用外**。§16.1） |
| **P5** | stock × C3 × Android が再接続まで OK（**arm-1 構成**） | 65% | **未測定**（同上） |
| **P6** | P4 が NG なら署名が同型（**arm-1 構成**） | 60% | **未測定**（同上） |
| **P7** | D-7 × C5 × Android が bond 到達 | 70% | ★**的中** |
| **P8** | （条件付き）自動 bond が働く | 60% | ★**的中** |
| **P9** | D-7 × C3 × Android が bond 到達＋READ | 80% | ★**的中** |
| **P10** | （条件付き）再接続の自動 bond | 65% | ★**的中** |
| **P11** | 切断後に再接続できる（wedge しない） | 85% | ★**的中** |
| **P12** | D-7 × C3 × iPhone が wedge しない | 75% | ★**的中** |
| **P13** | （条件付き）bond 到達＋READ | 85% | ★**的中** |
| **P14** | iPhone が timeout を表示する（NG 判定に使わない） | 80% | ★**的中** |

★**改訂は 1 件も行っていない。** ★**P4-P6 は «別アームに登録した» ので «未測定» のまま残す**
（**書き換えれば «後付け» になる**）。★**P3 は «前件が偽»＝«判定不能»**（**的中と書かない**）。

### 20.7 ★未測定・取らなかった対照（no silent caps）

- ★**stock × C5 × iPhone ＝ 未実施**（**C5 の iPhone は我々の ASP3 では «OK»＝既知の健全セル**
  ⇒ **stock で測る価値は相対的に低いと判断した**。**ただし «測っていない» ことは記録する**）。
- ★**stock × C6 は C0（arm-1・EXT_ADV=y の «初版» ビルド）のみ**＝**D-7 アームでの C6 は未実施**。
  ★**C0 は «命名修正前のビルド»（EXT_ADV=y・名前 `nimble-bleprph-e`）で取った**
  ＝**現行アームと «同一ではない»**（**C0 の結論＝«stock アプリは C6 で動く» は影響を受けないが、
  厳密には別ビルド**）。
- ★**arm-1（`IDFCTL-C5`）の READ の脚は «未実行のまま»**（§14.4＝**私の手順書の不備**）。
- ★**機序は 3 件とも未特定**（**RX/TX・SMP PDU・pend_ring いずれも未計装**）。
- ★**「なぜ iPhone で ENC が CONNECT より前にログされるか」未解明**（§19.3）。
- ★**「なぜ iPhone が切断時に timeout を表示するか」未解明**（**stock でも出る＝上流の性質、までが実測**）。
- ★**我々の ASP3 側の «再測定» はしていない**（**比較対象は別エージェントの記録＝読むだけ**）。
- ★**`hci0` は «スキャンのみ»**（**接続 0 件を read-only で確認してから**）＝**BlueZ での bond 試験はしていない**。
- ★**IRK 未配布の arm-1 では «セッションを端末に帰属できない»**（**D-7 では解決＝帰属できた**）。

### 20.8 ★今日の «取り違え» の記録（★最終形にも残す）

| 項目 | 内容 |
|---|---|
| **何が起きたか** | **同一セッションに `IDFCTL-C6` と `IDFCTL-C5` が «両方» 広告**⇒ **ユーザーが取り違え**⇒ **「stock × C5 × Android = OK」と «誤って» 報告された**（§13） |
| **私の誤り** | ★**「一意名（D-4）を付ければ判別できる」と設計した**＝**名前分離では «人間の取り違え» を防げない** |
| **親の誤り** | ★**«名前分離で足りる» という段取り**（**親自身が «段取りミス» と明記してよいとした**） |
| **救ったもの** | ★**訂正が commit «前» に届いた**＝**evidence に誤った結論は 1 行も入っていない** |
| ★**普遍の教訓** | ★★**「1セル1ボード」＝普遍**（**計器と無関係・人間が取り違える**）。
**測るボード «以外» は電源断し、«スキャンに居ないことを実証» してから渡す**（**以後 全セルで実行**） |
| ★**普遍でない規則** | **「1ビルド1端末」＝ASP3 の計器（LP_AON マーカ＝共用・last-wins）固有の artifact**
（**§10.2 で実測して否定＝stock は追記ログなので 1 ビルドで両端末を測れる**） |

### 20.9 ★私が自己申告した測定バグ（計 4 件・**すべて自分で検出**）

| # | バグ | 検出方法 |
|---|---|---|
| ① | `-march` が build.ninja に 0 件＝**response file 間接化**（検出器の空振り） | **検出器の較正**（値 `rv32` で再検索・`.map` と 2 系統） |
| ② | **`-B` は sdkconfig を分離しない**＝**両方 C5 ビルドになりかけた** | **«焼いたものが意図どおりか» の独立確認** |
| ③ | **`-dM` は «定義テキスト» であって «値» ではない**／**`-P` 無しで linemarker が値を割った** | **生出力を見た**（40 件の «壊れた値» を発見） |
| ④ | **iPhone では ENC が CONNECT より前にログされる**⇒ **timing スクリプトが «偽の 7.4s» を出した** | **生ログの順序を読んだ**（§19.3） |

★**加えて «踏みかけた» 罠**：**EXT_ADV のままでは `hci0`（HCI 4.2）が広告を見られず、
«C6 が真cold で死んだ» と誤報告しかけた**（§8.2）／**`pairing complete` の 0 件を «bond 不成立» と
読みかけた**（**bleprph はそのログを出さない＝死んだ検出器**・§10.1）。


---

## 21. ★ベンチの復旧状態（★ユーザーの実機作業を塞がないこと）

### 21.1 C3 ＝ **ASP3 に戻した（実測で確認）**

| 段階 | 実測 |
|---|---|
| 書き戻し | `esptool --chip esp32c3 write_flash 0x0 tmp/asp3_flash_backup/c3_asp3_full_4MB.bin` ⇒ `Hash of data verified` |
| **バイト検証** | ★**`verify OK (digest matched)`**（独立に読み戻し） |
| **同一性の裏取り** | ★**dump == `build/rb_A_ble/asp_flash.bin`（`True`）**＝**元々載っていたアーム A に戻った** |
| ★**機能検証** | ★★**真cold → BlueZ スキャンで `ASP3-C3-BLE @ 60:55:F9:57:BA:BC` を確認**
＝**«digest が一致した» だけでなく «実際に動いて広告している»**（**ASP3 は base MAC で広告／stock は `…:BE`＝別値なので取り違えない**） |
| **復元先の保全** | ★**`rb_A_ble` / `rb_B_ble` / `w1_A` / `w1_B` の 4 つとも present＝1 つも消していない** |

### 21.2 C5 / C6 ＝ **stock が載ったまま・電源断中**（★**戻すかは要判断**）

| DUT | 現在 | 元のイメージ | 復旧手段（**両方とも検証済み**） |
|---|---|---|---|
| **C5** `d0:cf:13:f0:a7:44` | **stock D-7（`IDFCTL-C5SC`）**・**電源断** | **`build/c5_tc_A2_ble`** | ★**dump `tmp/asp3_flash_backup/c5_asp3_full_8MB.bin`（md5 `c29621d5…`・独立 2 回の dump が一致）**＋**§3.3 で write+verify を実地検証済** |
| **C6** `14:C1:9F:E0:5A:9C` | **stock arm-1（`IDFCTL-C6`・EXT_ADV=y の初版）**・**電源断** | **`build/gd_c6_ble`** | ★**dump `tmp/asp3_flash_backup/c6_asp3_full_4MB.bin`（md5 `2f74e55a…`）**＋**§3.3 で write+verify を実地検証済** |

**戻すコマンド（そのまま貼れる）**：
```bash
ESPTOOL=~/.espressif/python_env/idf5.5_py3.12_env/bin/esptool.py
cd /home/honda/TOPPERS/ASP3CORE/asp3_esp_idf
sudo uhubctl -l 1-6 -p 3-4 -a on    # C5 に給電
sudo uhubctl -l 1-6 -p 2    -a on    # C6 に給電
$ESPTOOL --chip esp32c5 -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_D0:CF:13:F0:A7:44-if00 \
    -b 921600 write_flash 0x0 tmp/asp3_flash_backup/c5_asp3_full_8MB.bin
$ESPTOOL --chip esp32c6 -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_14:C1:9F:E0:5A:9C-if00 \
    -b 921600 write_flash 0x0 tmp/asp3_flash_backup/c6_asp3_full_4MB.bin
```

★**判断が要る理由**：**戻せば «次フェーズ（我々の統合の調査）» の土台が整う**が、
**stock アームを消すと «再測定» には再 flash が必要**（**ビルドは `tmp/stock_bleprph/` に残るので復元可能**）。
⇒ **親／ユーザーの判断を仰ぐ**。**私は C3 のみ «元に戻せ» と指示されたので C3 のみ戻した。**

### 21.3 stock のビルド資産（★再現可能な形で残る）

- **プロジェクト**：`tmp/stock_bleprph/`（**gitignore 済＝リポジトリを汚さない**）
- **再現スクリプト**：`tmp/stock_bleprph_setup.sh`（**tracked**＝**コピー・delta・両ターゲットのビルド・
  toolchain の実測まで**）
- **アーム**：`build_esp32c6`（C0）／`build_esp32c5`（arm-1）／`build_c5_r2`（D-7）／
  `build_esp32c3`（arm-1・fallback）／`build_c3_r2`（D-7）
- **セルのログ**：`tmp/stock_bleprph/cell_logs/archive/`（**全セル分を保存＝上書きしていない**）
