# asp3_esp_idf — TOPPERS/ASP3 Core ＋ Espressif ESP-IDF 統合

[TOPPERS/ASP3 Core](https://github.com/exshonda/asp3_core)（μITRON4.0系
シングルコアRTOS）を **ESP32-C3 / ESP32-C5 / ESP32-C6**（RV32IMC系）で動かし，
EspressifのWi-Fi／Bluetooth（BLE）スタックと協調動作させるリポジトリ。
「各社SDKとの協調動作」の第5弾（pico-sdk・FSP・STM32Cube・MCUXpresso
に続く。経緯は asp3_core の `docs/dev/esp-idf-integration.md`）。

## 他のSDK統合と異なる点

ESP-IDFはFreeRTOS前提のOSフレームワークであり，ドライバ層
（`esp_driver_*`）自体がFreeRTOS APIを呼ぶため「SDKドライバをそのまま
使う」協調はできない。したがって **下層（hal/soc・PHY・Wi-Fi/BTのバイナリ
blob）だけを使い，OSアダプタ（os_adapter）のshimをASP3プリミティブで
実装して載せる**（Zephyr/NuttXと同じ考え方）。ESP-IDFのスタートアップ・
FreeRTOSはリンクしない。

- ブートはASP3自前の **Direct Boot**（二段ブートローダ・esptoolイメージ
  形式とも不要。フラッシュ先頭のマジックナンバーでROMが直接起動）
- コンソールは **USB Serial/JTAG**（C3/C6の既定）または **UART0**（C5の既定）

## 構成

**`asp3/` は純ASP3（カーネルポート），`esp/` はESP統合**という分離になっている
（2026-07-21）。ESPコンポーネントの供給元は **esp-idf submodule 一本**。

```
asp3_esp_idf/
├── asp3/                          … ★純ASP3（カーネルポートのみ）
│   ├── asp3_core                  … 純カーネル（submodule）
│   ├── asp3_esp_idf.cmake         … パス解決ヘルパ
│   ├── cmake/                     … toolchainファイル・toolchain検査
│   ├── arch/riscv_gcc/esp32c5/    … C5チップ依存部（将来asp3_coreへupstream予定）
│   └── target/esp32c{3,5,6}_espidf/  … target_*.c/h・.ld・flash_header.S・target.cmake
├── esp/                           … ★ESP統合
│   ├── common/                    … 3チップ共有（hal_stub・net・bt/stub・共有shim）
│   └── c3/ c5/ c6/                … チップ固有（wifi・bt・sdkconfig_stub・esp_*.cmake）
├── esp-idf/                       … ESP-IDF（submodule．**v5.5.4タグ**＝唯一の供給元）
├── apps/                          … デモアプリ（18個．下記）
└── docs/
```

> ★かつて存在した `hal/`（esp-hal-3rdparty）と `lwip/` submodule は
> **撤廃済み**（2026-07-20）。lwIPは esp-idf 同梱のフォークを使う。
> `.gitmodules` は `asp3_core` と `esp-idf` の2つだけ。

`apps/` の内訳（**チップ固有アプリは `_c{3,5,6}` サフィックスで明示**）：

- Wi-Fi：`wifi_scan` / `wifi_connect` / `wifi_dhcp`（3チップ共有）
- TCP/IP：`tcp_socket_echo` / `tcp_socket_client` / `udp_socket_echo`（3チップ共有）
- BLE：`bt_smoke_c{3,5,6}`（コントローラ／HCI）・`ble_host_smoke_c{3,5,6}`（NimBLE＋独自GATT）
  ／`common_ble/`（GATTサービス定義の共有 `.inc`）
- 負荷・診断：`load_test_c{3,5,6}` / `agc_probe_c6`（C6 AGC調査＝★完結）
  / `boot_pmu_probe_c5`（C5 boot方式調査＝★完結）

## 供給元

**ESPコンポーネント（ヘッダ・ソース・blob・ROM ld）は常に `esp-idf/` submodule
から供給される。** かつて存在した hal フォールバック（`ASP3_ESPIDF_SUPPLY`・
`ASP3_WIFI_BLOB_HAL`・`ASP3_LWIP_ESPIDF`）は submodule ごと**撤去済み**で，
切替オプション自体が存在しない。

> ★`esp-idf/` submodule は**真のv5.5.4タグ**（`735507283d`．`git -C esp-idf
> describe --tags` で実測）。過去のコメントや変数名が指す外部ツリー
> `~/tools/esp-idf` は **v5.5.4ではない**（実測 `v5.5`＝v5.5.0）ので混同しない。

唯一残る供給の選択肢は **C5/C6 の `ASP3_BT_IDF_V554`**（既定ON＝submodule v5.5.4／
OFF＝外部 v6.1 ツリー `ESP_IDF61_DIR`）。これは hal ではなく **v6.1 との A/B 用**。

主なoption（既定値）：

| option | 既定 | 意味 |
|---|---|---|
| `ESP32C{3,5,6}_WIFI` / `_LWIP` / `_BT` | OFF | 機能の有効化 |
| `ESP32C{3,5,6}_BT_NIMBLE` | OFF（`ble_host_smoke_c*` 指定時は自動ON） | NimBLEホスト |
| `ESP32C{3,5,6}_BT_SM` | ON（`_BT_NIMBLE=ON`のとき） | SMPペアリング/bonding |
| `ESP32C3_QEMU` | **ON** | C3のみQEMU対応。**実機は必ず`OFF`**（下記★） |
| `ESP32C6_COLD_CPU_PLL` / `ESP32C6_PMU_INIT_LATE` | ON | C6の真cold（POR）起動に必須 |
| `ASP3_BT_IDF_V554`（C5/C6） | ON | OFF＝外部v6.1ツリー（`-DESP_IDF61_DIR=<path>`） |
| `ASP3_SEAM_BOOT`（C5） | OFF | OFF＝Direct Boot（採用方式）。ON＝IDF二段ブートローダ経由（下記） |
| `ASP3_C5_PMU_INIT`（C5） | OFF | stock `pmu_init()` をそのまま実行（実機非回帰の確認待ち） |

★★**C3の実機ビルドは `-DESP32C3_QEMU=OFF` が必須**。既定ONのままだと
`TOPPERS_USE_QEMU` が定義され `csrw mie` が出るが，**実機のC3は mie CSR 非実装**
のため起動時に Illegal instruction で即クラッシュする。

## ビルド

`ASP3_TARGET_DIR` で asp3_core 本体の CMake を駆動する（pico-sdk型の
外部ターゲット規約）。

★**ツールチェーンは `asp3/cmake/toolchain-esp32-riscv32.cmake` を使う**
（ESP-IDF v5.5.4 が指定する `riscv32-esp-elf` **esp-14.2.0_20260121** を
絶対パスで固定する）。asp3_core の `cmake/toolchain-riscv64.cmake` は
プレフィクスをPATH解決するため，**指定を忘れるとUbuntu標準の汎用GCCへ黙って落ちる**
（rv32マルチリブがあるのでビルドは通ってしまい気づけない。実際に本リポジトリの
320構成中164構成がこれに当たっていた）。誤ったtoolchainは
`asp3/cmake/esp_toolchain_check.cmake` がconfigure時にFATALで止める。

```bash
git clone --recursive https://github.com/exshonda/asp3_esp_idf
cd asp3_esp_idf
```

> **blobの取得**：`esp-idf/` は再帰submoduleにWi-Fi/BT/PHYのblobを持つ。
> 全部（23個）を取る必要はなく，使う分だけ `git submodule update --init` すればよい
> （`components/esp_wifi/lib`・`esp_phy/lib`・`esp_coex/lib`・
> `bt/controller/lib_esp32c3_family` 等）。

### 実機（Wi-Fi＋TCP/IP：DHCP＋ping）

`<SSID>` / `<PASSWORD>` は**ビルド時注入のみ**（リポジトリに実値を書かない）。

```bash
# ESP32-C3
cmake -S asp3/asp3_core -B build/c3_w1 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_WIFI=ON -DESP32C3_LWIP=ON \
  '-DASP3_EXTRA_COMPILE_DEFS=WIFI_SSID="<SSID>";WIFI_PASSWORD="<PASSWORD>"' \
  -DASP3_APPLDIR=$PWD/apps/wifi_dhcp -DASP3_APPLNAME=wifi_dhcp
cmake --build build/c3_w1
cmake --build build/c3_w1 --target run    # esptoolで書込み
```

C5／C6は `-DASP3_TARGET=esp32c{5,6}_espidf`・`-DASP3_TARGET_DIR=…`・
`-DESP32C{5,6}_WIFI=ON -DESP32C{5,6}_LWIP=ON` に読み替える
（C5/C6に `ESP32C{5,6}_QEMU` は無いので `-DESP32C3_QEMU=OFF` 相当の指定は不要）。

### 実機（BLE：NimBLE＋独自GATT）

```bash
# ESP32-C3
cmake -S asp3/asp3_core -B build/c3_ble -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_BT=ON -DESP32C3_BT_SM=ON \
  -DASP3_APPLDIR=$PWD/apps/ble_host_smoke_c3 -DASP3_APPLNAME=ble_host_smoke_c3
cmake --build build/c3_ble
```

C5／C6は `-DESP32C{5,6}_BT=ON -DESP32C{5,6}_BT_SM=ON`，アプリを
`apps/ble_host_smoke_c{5,6}` に読み替える
（`_BT_NIMBLE` は `ble_host_smoke_c*` を指定すれば自動でONになる）。

### QEMU（ESP32-C3のみ）

Espressif版QEMU（esp32c3マシンを持つfork）が必要。C5/C6にQEMU対応は無い（実機専用）。

```bash
cmake -S asp3/asp3_core -B build/esp32c3-qemu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=ON
cmake --build build/esp32c3-qemu

qemu-system-riscv32 -M esp32c3 -nographic -semihosting \
  -drive file=build/esp32c3-qemu/asp_flash.bin,if=mtd,format=raw
```

### 書込み・コンソール

コンソールは書込みと同じUSBポート（USB Serial/JTAG）に115200bpsで出る
（C3/C6の既定）。C5の既定はUART0。切替は `-DESP32C{3,5,6}_CONSOLE=uart0|usbjtag`，
ポートは `-DESP32C{3,5,6}_PORT=/dev/ttyACM*`。

esptool は `asp3/cmake/esp_find_esptool.cmake` がESP-IDFのpython env
（`~/.espressif/python_env/*/bin`）から自動検出する。見つからない場合や
別の実体を使う場合は `-DESP32C{3,5,6}_ESPTOOL=<path>`。

### C5 の seam ブート（`ASP3_SEAM_BOOT=ON`・既定OFF）

Direct Boot ではなく **ESP-IDF の二段ブートローダ経由**で起動する構成。
**C5では採用していない**（`evidence-c5-03` で Direct Boot 継続と決着）が，
S3/無印は Xtensa で ROM Direct Boot を持たず同方式が必須になるため，
**その参照実装として温存**している。手順・罠は
`.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c5-11-seam-rerun-on-current-tree.md`
（★seam は**真POWERONでしか起動しない**・観測は**uart0コンソール必須**）。

## 到達点

判定の定義：**W1**＝Wi-FiでDHCPによりIP取得＋ping成功／**W2**＝BLEでGATT接続成立。
**真cold**＝物理電源断（POR）からの起動。esptoolのhard-resetやUSB-JTAGコンソールの
ACM openによるリセットは真coldではない（この区別を怠って測ったログが過去に多数ある）。

| チップ | W1（Wi-Fi＋DHCP＋ping） | W2（BLE GATT接続） |
|---|---|---|
| **ESP32-C3** | **達成・真cold**（IP取得＋ping 21/21・49/49，NG0．独立2/2） | **達成・真cold**（BlueZ central） |
| **ESP32-C5** | **達成・真cold**（IP取得＋ping 54回／60回．独立2回．`evidence-c5-12`） | **達成・真cold**（独自GATT read/notify/write＋暗号必須read＝D-2c/D-2d） |
| **ESP32-C6** | **達成・真cold**（IP取得＋ping 36/36，失敗0） | **達成・真cold**（D-2c/D-2d．スマホcentralで確認） |

証跡は `.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-*.md`。

その他の到達点：

| 項目 | 状態 |
|---|---|
| カーネル移植（C3/C5/C6） | 完了（`test_porting` PASS・実機testexec．`docs/hal-integration.md`） |
| Wi-Fi scan（B-2a） | C3/C5/C6とも実機で完走 |
| TCP/IP（lwIP：DHCP・ping・TCP・UDP・BSDソケット） | 完了（`docs/tcpip-integration.md`） |
| BLE コントローラ＋HCI（D-1） | C3/C5/C6とも達成 |
| **esp-idf供給への全面移行** | **完了**（3チップ・全構成で hal 参照0を `ninja -t deps` で実測．`hal`/`lwip` submodule撤廃） |
| toolchain の IDF 標準統一 | 完了（`esp-14.2.0_20260121` を絶対パス固定＋configure時FATAL検査） |
| C6 真cold（POR）ハング | 根治済（`ESP32C6_COLD_CPU_PLL`／`ESP32C6_PMU_INIT_LATE`．`evidence-c6-04`） |

★**C3のBLE bondはtoolchain依存だった**：かつて「C3のBTはhal供給でないとbondできない」と
記録されていたが，これは**誤帰属**。真cold A/B（供給・blob同一・コンパイラのみ変更）で
**esp-14.2.0_20260121＝bond 5/6成功・esp-15.2.0＝0/5失敗**を実測し，
供給ではなくコンパイラが原因と確定した（`evidence-c3-10`）。

## 既知の制限・未検証

- **スマホcentralは全組合せが通るわけではない**（BlueZ `hci0` では3チップとも成立）。
  C3×Android／iPhone の詳細と，切断が届かずリンクを握ったまま広告が止まる問題
  （`DISC=0`）の調査は `docs/ble-c3-smp-death-plan.md` と
  `.steering/…/evidence-rc-c3-*.md` を参照。**Android/RPA対応は要件**。
- **bondストアはRAM backed**（`PERSIST=0`）＝電源断で鍵が消える。NVS化は未着手。
- `test_porting` は asp3_core 側で **6項目→8項目**（`tap_plan(8U)`）に増えている。
  docs内に残る「6/6 passed」は6項目時代の記録。
- **seam（C5）は «ビルドと起動» のみ確認済み**で，採用していないため継続的な検証は無い。
  CIが無いリポジトリのため，触ったら手動でビルドを通すこと（過去に esptool の
  PATH依存で黙って壊れていた実績がある）。

## ライセンス

ASP3カーネルはTOPPERSライセンス。ESP-IDF（Apache-2.0）・
Wi-Fi/BTバイナリblob（Espressif配布条件）・lwIP（BSD．esp-idf同梱フォーク）・
NimBLE（Apache-2.0）はそれぞれのライセンスに従う。
