# 開発者オンボーディング

このリポジトリで**開発を始めるまでの一本道**をまとめる。
「何ができているか（到達点）・構成の全体像」は [`README.md`](../README.md) が正本で、
本書はそれと重複せず **「手を動かす順序」と「最初に踏みやすい罠」** を扱う。

対象読者＝ASP3（TOPPERS系RTOS）と ESP32 のどちらかに馴染みがあれば読める程度。
記載のコマンドは**すべて実際に実行して確認したもの**。

---

## 0. 5分で掴む全体像

- **ASP3 カーネル**を ESP32-C3 / C5 / C6（RV32IMC系）で動かし、Espressif の
  **Wi-Fi／BLE スタックと協調**させるリポジトリ。
- ESP-IDF は FreeRTOS 前提のため「SDKをそのまま使う」協調はできない。よって
  **下層（hal/soc・PHY・Wi-Fi/BTのバイナリ blob）だけを使い、OSアダプタ（os_adapter）の
  shim を ASP3 プリミティブで実装して載せる**（Zephyr/NuttX と同じ考え方）。
  **ESP-IDF のスタートアップと FreeRTOS はリンクしない。**
- ディレクトリの分担：

  | パス | 役割 |
  |---|---|
  | `asp3/` | **純ASP3**（カーネルポート）。`asp3_core`＝カーネル本体（submodule） |
  | `esp/` | **ESP統合**。`common/`＝3チップ共有、`c3/` `c5/` `c6/`＝チップ固有 |
  | `esp-idf/` | **ESP-IDF（submodule）＝ESPコンポーネントの唯一の供給元** |
  | `apps/` | デモアプリ |
  | `docs/` `.steering/` | 設計・調査記録と証跡 |

> **なぜ供給元が1つなのか**：以前は `hal`（esp-hal-3rdparty）と `lwip` も submodule
> だったが、供給元が混在すると同じヘッダの別版が混ざって ABI 不整合を起こす。
> 2026-07-21 に **esp-idf 一本へ統一し、`hal`／`lwip` submodule は撤廃**した
> （経緯＝`docs/hal-vs-espidf-decision.md`）。

---

## 1. 前提ツール

| ツール | 要件 |
|---|---|
| Git | submodule を使う |
| CMake ≥ 3.16 ＋ Ninja | ビルド |
| Python 3 | `cfg.py`（コンフィギュレータ）は**標準ライブラリのみ**＝`pip install` 不要 |
| RISC-V toolchain | **`riscv32-esp-elf` esp-14.2.0_20260121 固定**（§3） |
| QEMU（任意） | Espressif fork の `qemu-system-riscv32`。**C3のみ**対応 |
| esptool（実機のみ） | 書き込み。`idf_tools.py` で導入するか PATH に置く |

---

## 2. 取得（submodule は「使う分だけ」）

```bash
git clone https://github.com/exshonda/asp3_esp_idf
cd asp3_esp_idf
git submodule update --init asp3/asp3_core        # カーネル本体（小さい）
git submodule update --init --depth 1 esp-idf     # ESP-IDF（v5.5.4タグ固定）
```

`esp-idf/` は再帰 submodule に Wi-Fi/BT/PHY の blob を持つが、**23個すべては要らない**。
下表は**実際のビルドが触れたものを実測**して求めた最小集合（`ninja -t deps` と
リンク行から採取）。

```bash
cd esp-idf
# --- Wi-Fi + TCP/IP（W1）を作るなら（3チップ共通） ---
git submodule update --init --depth 1 \
  components/esp_wifi/lib components/esp_phy/lib components/esp_coex/lib \
  components/lwip/lwip components/mbedtls/mbedtls

# --- BLE（W2）を作るなら：共通3つ＋チップ固有のコントローラblob 1つ ---
git submodule update --init --depth 1 \
  components/bt/host/nimble/nimble components/esp_phy/lib components/esp_coex/lib
git submodule update --init --depth 1 components/bt/controller/lib_esp32c3_family        # C3
git submodule update --init --depth 1 components/bt/controller/lib_esp32c5/esp32c5-bt-lib # C5
git submodule update --init --depth 1 components/bt/controller/lib_esp32c6/esp32c6-bt-lib # C6
cd ..
```

> `--depth 1` を付けないと ESP-IDF の全履歴（数GB）を取ることになる。

---

## 3. ツールチェーン（★最初の罠）

**ESP-IDF v5.5.4 が指定する `riscv32-esp-elf` esp-14.2.0_20260121 を使う。**

```bash
# esp-idf 同梱のインストーラで導入（IDF_TOOLS_PATH を尊重する）
python3 esp-idf/tools/idf_tools.py install riscv32-esp-elf
# 既定の導入先は ~/.espressif 。別の場所に入れているなら export しておく：
#   export IDF_TOOLS_PATH=/path/to/espressif
```

ビルド時は**必ず本リポジトリの toolchain ファイルを指定する**：

```
-DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake
```

### なぜ固定なのか（2つとも実測に基づく）

1. **指定を忘れると黙って別のGCCへ落ちる**。asp3_core 側の
   `toolchain-riscv64.cmake` はプレフィクスを PATH 解決するため、Ubuntu 標準の
   汎用 GCC が拾われる。rv32 マルチリブがあるので**ビルドは通ってしまい気づけない**
   （実際にこのリポジトリの 320構成中 164構成がこれに当たっていた）。
   誤った版は `asp3/cmake/esp_toolchain_check.cmake` が configure 時に **FATAL** で止める。
2. **esp-15 系は使わない**。C3 の BLE bond が
   **esp-14＝5/6 成功・esp-15＝0/5 失敗**（供給・blob を同一にしコンパイラだけ変えた
   真cold A/B）。かつて「供給が原因」と記録されていたが**誤帰属**で、真因は
   コンパイラだった（証跡＝`.steering/…/evidence-c3-10*`）。

---

## 4. 最初のビルドと検証（QEMU・実機不要）

移植検証テスト `test_porting`（カーネル基本機能8項目をTAPで機械判定）を QEMU で回すのが
一番速い健全性チェック。**C3 のみ** QEMU 対応。

```bash
cmake -S asp3/asp3_core -B build/c3_tp -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=ON \
  -DASP3_APPLDIR=$PWD/asp3/asp3_core/test/porting -DASP3_APPLNAME=test_porting \
  -DASP3_EXTRA_APP_C_FILES=$PWD/asp3/asp3_core/test/porting/tap.c
cmake --build build/c3_tp
cmake --build build/c3_tp --target run
```

> ★`tap.c` を `ASP3_EXTRA_APP_C_FILES` で足すこと。忘れると
> `undefined reference to 'tap_plan'` でリンクに失敗する。

期待する出力（**これが出れば環境は正しい**）：

```
1..8
ok 1 - syslog_output
ok 2 - tick_timer_basic
ok 3 - task_create_activate
ok 4 - semaphore_signal_wait
ok 5 - eventflag_set_wait
ok 6 - alarm_handler
ok 7 - isr_delayed_dispatch
ok 8 - wake_from_idle
# 8/8 passed
```

> QEMU が PATH に無い場合は `-DQEMU_SYSTEM_RISCV32_ESP=/path/to/qemu-system-riscv32`。
> **判定は TAP の機械判定で行う**（目視で「動いていそう」は判定にしない）。
> docs 内に残る「6/6 passed」は項目が6個だった時代の記録。

---

## 5. 実機ビルド

実機の配線・書き込み器の準備は各自の環境に依存するため、ここでは**ビルドと書き込みの
形**だけ示す。`--target run` が esptool を呼ぶ。

### Wi-Fi ＋ TCP/IP（DHCP＋ping）

`<SSID>` / `<PASSWORD>` は**ビルド時注入のみ**。**リポジトリに実値を書かないこと**
（docs・ログ・コミットにも残さない）。

```bash
cmake -S asp3/asp3_core -B build/c3_w1 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_WIFI=ON -DESP32C3_LWIP=ON \
  '-DASP3_EXTRA_COMPILE_DEFS=WIFI_SSID="<SSID>";WIFI_PASSWORD="<PASSWORD>"' \
  -DASP3_APPLDIR=$PWD/apps/wifi_dhcp -DASP3_APPLNAME=wifi_dhcp
cmake --build build/c3_w1
cmake --build build/c3_w1 --target run     # 書き込み
```

### BLE（NimBLE＋GATT）

```bash
cmake -S asp3/asp3_core -B build/c3_ble -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_BT=ON -DESP32C3_BT_SM=ON \
  -DASP3_APPLDIR=$PWD/apps/ble_host_smoke -DASP3_APPLNAME=ble_host_smoke
cmake --build build/c3_ble
```

### チップ間の読み替え

| | C3 | C5 | C6 |
|---|---|---|---|
| `-DASP3_TARGET` / `TARGET_DIR` | `esp32c3_espidf` | `esp32c5_espidf` | `esp32c6_espidf` |
| Wi-Fi | `-DESP32C3_WIFI=ON -DESP32C3_LWIP=ON` | `-DESP32C5_…` | `-DESP32C6_…` |
| BLE | `-DESP32C3_BT=ON -DESP32C3_BT_SM=ON` | `-DESP32C5_…` | `-DESP32C6_…` |
| BLEアプリ | `apps/ble_host_smoke` | `apps/ble_host_smoke_c5` | `apps/ble_host_smoke_c6` |
| QEMU | あり（`-DESP32C3_QEMU`） | **なし** | **なし** |
| 既定コンソール | USB Serial/JTAG | **UART0** | USB Serial/JTAG |

- `_BT_NIMBLE` は `ble_host_smoke*` を指定すれば自動で ON になる。
- C5/C6 に `ESP32C{5,6}_QEMU` は存在しないので `-DESP32C3_QEMU=OFF` 相当の指定は不要。

---

## 6. どこまで動くか

到達点の表（W1＝Wi-Fi＋DHCP＋ping、W2＝BLE GATT接続。3チップとも**真cold**で達成）と
**既知の制限**は [`README.md`](../README.md) にある。着手前に「既知の制限・未検証」節は
必ず読むこと。要点だけ：

- **bondストアはRAM保持**（`PERSIST=0`）＝電源断で鍵が消える（NVS化は未着手）。
- **スマホcentralは全組合せが通るわけではない**（BlueZ では3チップとも成立）。
  Android/RPA と「切断が届かず広告が止まる（`DISC=0`）」は
  `docs/ble-c3-smp-death-plan.md` が正本。
- **seam ブート（C5）はビルドと起動のみ確認済み**で採用していない（既定は Direct Boot）。

---

## 7. 作業の進め方（このリポジトリの流儀）

- **CI が無い。触ったら手動でビルドを通す。**
  過去に esptool の PATH 依存で**黙って壊れていた**実績がある（特に seam 構成）。
  影響しそうな構成は一通り作り直すこと。
- **判定は機械的に**：TAP（`ok`／`not ok`・`# 8/8 passed`）やログマーカーで判定し、
  目視の印象で「動いた」と言わない。
- **「真cold」＝物理電源断（POR）からの起動だけ**。esptool の hard-reset や
  USB-JTAG コンソールを開いたときのリセットは真coldではない
  （この区別を怠った測定が過去に多数あり、誤結論の主因になった）。
- **記録を残す**：各作業単位は `.steering/<日付>-<主題>/` に
  **背景・事前予測・結果・証跡**の形で残す。**測定の前に予測を書く**のが肝で、
  外れた場合それ自体が情報になる。
- **相関を因果と早合点しない**。「Aを入れたら直った」は**Aを外した対照**を採るまで
  因果ではない。詳細な規律は [`CLAUDE.md`](../CLAUDE.md)「実機調査の規律」。

### 変更してよい場所・いけない場所

| 場所 | 可否 |
|---|---|
| `asp3/asp3_core/`（submodule） | ❌ 直接編集しない。asp3_core 側で直して submodule を bump |
| `esp-idf/`（submodule） | ❌ 直接編集しない。差分は `esp/common/` か `esp/c{3,5,6}/` にシムを置く |
| カーネル内の動的メモリ確保 | ❌ 使わない（blob が要るヒープはカーネル外に実装する） |
| `esp/`・`asp3/target/`・`apps/` | ⭕ 通常の作業場所 |

---

## 8. 次に読むもの

| 目的 | 文書 |
|---|---|
| 到達点・既知の制限 | `README.md` |
| 作業の規律・禁則 | `CLAUDE.md` |
| 統合の経緯と設計判断 | `asp3/asp3_core/docs/dev/esp-idf-integration.md` |
| なぜ esp-idf 単一供給にしたか | `docs/hal-vs-espidf-decision.md`（実行完了済みの判断メモ） |
| Wi-Fi shim の設計 | `docs/wifi-shim.md` |
| BLE shim の設計 | `docs/bt-shim.md` |
| TCP/IP 統合 | `docs/tcpip-integration.md` |
| 調査の生ログ・証跡 | `.steering/20260716-c3c5c6-esp-idf-supply-migration/` |
