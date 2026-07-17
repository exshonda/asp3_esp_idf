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

### 1.2 ★stock から変えた点の「全」列挙（＝これで全部）

`diff -r <submodule の例> tmp/stock_bleprph` の結果は **`sdkconfig.defaults` 1 ファイルのみ**、
中身は **2 個の CONFIG 追加**（残りはコメント）：

```
CONFIG_EXAMPLE_BONDING=y
CONFIG_EXAMPLE_ENCRYPTION=y
```

- **どちらも「例そのものの」stock Kconfig オプション**（`main/Kconfig.projbuild`）＝
  **我々の config は一切持ち込んでいない**。
- **なぜ必要か**：stock 既定は `BONDING=n` / `ENCRYPTION=n`＝**bond もしないし暗号必須特性も
  生えない**⇒ **試験対象そのものが存在しないビルドになる**。ミッションの
  「GATT surface は «bond して暗号必須特性を読む» が出来ればよい」の最小充足。
- **ソースコード（`main.c`/`gatt_svr.c`/`CMakeLists.txt`）は 1 行も変えていない。**

**再現手順は `tmp/stock_bleprph_setup.sh` に全部書いた**（コピー・delta・両ターゲットのビルド・
toolchain の実測まで）。「私の環境で動いた」で終わらせないため、スクリプトが唯一の正本。

### 1.3 ★stock 既定のうち「我々と違う」もの（＝結果の解釈に効く。隠さない）

`CONFIG_EXAMPLE_*` の既定をそのままにした結果、**stock は我々と次の点で違う**：

| 項目 | stock（本ビルド） | 我々（`ble_host_smoke_c5/c6`） | 効きうるか |
|---|---|---|---|
| **`ble_hs_cfg.sm_sc`** | **0（レガシーペアリング）** | **1（LE Secure Connections）** | ★**効きうる**（§6 の交絡） |
| **`sm_our/their_key_dist`** | **ENC のみ** | **ENC \| ID（IRK 配布）** | ★**効きうる**（RPA 経路） |
| **`MYNEWT_VAL_BLE_EXT_ADV`** | **1（拡張アドバタイズ）** | **0（レガシー adv）** | ★**効きうる**（電波の出方） |
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

## 7. 実機セル（★以下は実測の記録。予測を書いた «後» にのみ追記する）

（この節は C0 実行後に追記）
