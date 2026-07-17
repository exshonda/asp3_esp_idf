# asp3_esp_idf — TOPPERS/ASP3 Core ＋ Espressif ESP-IDF / esp-hal 統合

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
- コンソールは **USB Serial/JTAG**（ネイティブUSBボードの/dev/ttyACM*
  直結．C3/C6の既定）または **UART0**（C5の既定）

## 構成

```
asp3_esp_idf/
├── asp3/
│   ├── asp3_core                  … 純カーネル（submodule）
│   ├── asp3_esp_idf.cmake         … パス解決ヘルパ
│   ├── arch/riscv_gcc/esp32c5/    … C5チップ依存部（submodule外．将来asp3_coreへupstream予定）
│   └── target/
│       ├── esp32c3_espidf/        … ターゲット依存部（本リポジトリ側）
│       ├── esp32c5_espidf/
│       └── esp32c6_espidf/
├── esp-idf/                       … ESP-IDF（submodule．**v5.5.4タグ**＝既定の供給元）
├── hal/                           … esp-hal-3rdparty（submodule．C3のBT既定供給／可逆fallback）
├── lwip/                          … lwIP（submodule）
├── apps/                          … デモアプリ（17個．下記）
└── docs/
```

`apps/` の内訳：

- Wi-Fi：`wifi_scan` / `wifi_connect` / `wifi_dhcp`
- TCP/IP：`tcp_socket_echo` / `tcp_socket_client` / `udp_socket_echo`
- BLE：`bt_smoke{,_c5,_c6}`（コントローラ／HCI）・`ble_host_smoke{,_c5,_c6}`（NimBLE＋独自GATT）
- 負荷・診断：`load_test_c3` / `load_test_c5` / `load_test_c6` / `agc_probe` / `boot_pmu_probe`

## 供給元（esp-idf submodule か hal か）

ESPコンポーネント（ヘッダ・ソース・blob・ROM ld）の供給元は
`ASP3_ESPIDF_SUPPLY` で選ぶ。**既定はesp-idf submodule＝hal参照0**。

> ★`esp-idf/` submodule は**真のv5.5.4タグ**（`735507283d`．`git -C esp-idf
> describe --tags` で実測）。過去のコメントや変数名が指す外部ツリー
> `~/tools/esp-idf` は **v5.5.4ではない**（実測 `v5.5`＝v5.5.0）ので混同しない。

| チップ | Wi-Fi／plain の既定 | BT/BLE の既定 |
|---|---|---|
| **C3** | esp-idf submodule（`ASP3_ESPIDF_SUPPLY=ON`） | **hal**（`ESP32C3_BT=ON` のとき `ASP3_ESPIDF_SUPPLY` は既定 **OFF**） |
| **C5** | esp-idf submodule | esp-idf submodule（`ASP3_BT_IDF_V554=ON`） |
| **C6** | esp-idf submodule | esp-idf submodule（`ASP3_BT_IDF_V554=ON`） |

**C3のBTだけhalが既定**なのは，真cold・同一central・同一手順のA/Bで
**esp-idf供給はbond失敗2/2・hal供給は成功2/2**だったため
（`.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c3-03-bt-supply-migration.md`）。
C3のesp-idf供給BT自体は**hal参照0でビルドでき広告/接続（D-2b）までは到達する**が
bondできない。`-DASP3_ESPIDF_SUPPLY=ON` で選べる（可逆）。

主なoption（既定値）：

| option | 既定 | 意味 |
|---|---|---|
| `ASP3_ESPIDF_SUPPLY` | ON（C3のBT時のみOFF） | 供給元。OFFでhalへ完全復帰（可逆） |
| `ASP3_BT_IDF_V554` | C5/C6=ON，C3=`ASP3_ESPIDF_SUPPLY`に追従 | BTツリーの供給元。基盤と混ぜない |
| `ASP3_WIFI_BLOB_HAL` | OFF | Wi-Fi/PHY/coexist blobだけhalへ戻す（可逆fallback） |
| `ESP32C{3,5,6}_WIFI` / `_LWIP` / `_BT` | OFF | 機能の有効化 |
| `ESP32C{3,5,6}_BT_NIMBLE` | OFF | NimBLEホスト（`ble_host_smoke*`で使用） |
| `ESP32C{3,5,6}_BT_SM` | ON（`_BT_NIMBLE=ON`のとき） | SMPペアリング/bonding |
| `ESP32C3_QEMU` | ON | C3のみQEMU対応。実機は`OFF` |
| `ESP32C6_COLD_CPU_PLL` / `ESP32C6_PMU_INIT_LATE` | ON | C6の真cold（POR）起動に必須。OFFは逆方向対照用 |
| `ASP3_SEAM_BOOT`（C5） | OFF | OFF＝Direct Boot（実機実績のある唯一の構成） |

★`ASP3_ESPIDF_SUPPLY` の既定は `if()` で計算される（C3は `ESP32C3_BT` に依存）。
`option()` の宣言値だけを読むと**実効値と食い違う**。

## ビルド

`ASP3_TARGET_DIR` で asp3_core 本体の CMake を駆動する（pico-sdk型の
外部ターゲット規約）。ツールチェーンは `riscv64-unknown-elf-gcc`
（Ubuntu標準パッケージの13.2.0で下記すべてビルド確認済み）。

```bash
git clone --recursive https://github.com/exshonda/asp3_esp_idf
cd asp3_esp_idf
```

### QEMU（ESP32-C3のみ）

Espressif版QEMU（esp32c3マシンを持つfork）が必要。C5/C6にQEMU対応は無い（実機専用）。

```bash
cmake -S asp3/asp3_core -B build/esp32c3-qemu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=ON
cmake --build build/esp32c3-qemu

qemu-system-riscv32 -M esp32c3 -nographic -semihosting \
  -drive file=build/esp32c3-qemu/asp_flash.bin,if=mtd,format=raw
```

### 実機（Wi-Fi＋TCP/IP：DHCP＋ping）

`<SSID>` / `<PASSWORD>` は**ビルド時注入のみ**（リポジトリに実値を書かない）。

```bash
# ESP32-C3
cmake -S asp3/asp3_core -B build/c3_w1 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
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
# ESP32-C3（BT既定供給＝hal）
cmake -S asp3/asp3_core -B build/c3_ble -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_BT=ON -DESP32C3_BT_NIMBLE=ON \
  -DASP3_APPLDIR=$PWD/apps/ble_host_smoke -DASP3_APPLNAME=ble_host_smoke
cmake --build build/c3_ble
```

C5／C6は `-DESP32C{5,6}_BT=ON -DESP32C{5,6}_BT_NIMBLE=ON`，アプリを
`apps/ble_host_smoke_c{5,6}` に読み替える。

### 書込み・コンソール

コンソールは書込みと同じUSBポート（USB Serial/JTAG）に115200bpsで出る
（C3/C6の既定）。C5の既定はUART0。切替は `-DESP32C{3,5,6}_CONSOLE=uart0|usbjtag`，
ポートは `-DESP32C{3,5,6}_PORT=/dev/ttyACM*`。

> **別ツールチェーンを使う場合**：Espressif版 `riscv32-esp-elf-gcc`（esp-15.2.0）
> でもビルドできるが，GCC14以降は暗黙の関数宣言がエラーになるため
> `-DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`
> `-DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration` の2つが要る（実測）。

## 到達点

判定の定義：**W1**＝Wi-FiでDHCPによりIP取得＋ping成功／**W2**＝BLEでGATT接続成立。
**真cold**＝物理電源断（POR）からの起動。esptoolのhard-resetやUSB-JTAGコンソールの
ACM openによるリセットは真coldではない（この区別を怠って測ったログが過去に多数ある）。

| チップ | W1（Wi-Fi＋DHCP＋ping） | W2（BLE GATT接続） |
|---|---|---|
| **ESP32-C3** | **達成・真cold**（IP取得＋ping 21/21・49/49，NG0．独立2/2） | **達成・真cold**（BlueZ central．**hal供給のみ**．esp-idf供給はbond失敗） |
| **ESP32-C5** | **達成**（IP取得＋ping 30回連続OK）／**★真coldは未検証** | **達成・真cold**（独自GATT read/notify/write＋暗号必須read＝D-2c/D-2d） |
| **ESP32-C6** | **達成・真cold**（IP取得＋ping 36/36，失敗0） | **達成・真cold**（D-2c/D-2d．スマホcentralで確認） |

証跡は `.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-*.md`
（C3＝`evidence-c3-02`/`-03`，C5＝`evidence-c5-02`/`-05`，C6＝`evidence-c6-06`/`-07`）。

その他の到達点：

| 項目 | 状態 |
|---|---|
| カーネル移植（C3/C5/C6） | 完了（`test_porting` PASS・実機testexec．`docs/hal-integration.md`） |
| Wi-Fi scan（B-2a） | C3/C5/C6とも実機で完走 |
| TCP/IP（lwIP：DHCP・ping・TCP・UDP・BSDソケット） | 完了（`docs/tcpip-integration.md`） |
| BLE コントローラ＋HCI（D-1） | C3/C5/C6とも達成 |
| esp-idf供給への移行（hal参照0） | Wi-Fiは3チップとも達成。BTはC5/C6達成・**C3はhal据置き**（上記） |
| C6 真cold（POR）ハング | 根治済（`ESP32C6_COLD_CPU_PLL`／`ESP32C6_PMU_INIT_LATE`．`evidence-c6-04`） |

## 既知の制限・未検証

- **★C5のW1は真coldで未検証**。IP取得＋pingは実機で成立しているが，記録された
  ブートはいずれも `rst:0x15`（＝POR起動そのものではない）で，採取ハーネス
  （`rts_boot_capture.py`）が観測対象のcold bootを中断していた。電源が落ちたことは
  読み戻しで実証済みだが，**W1を走らせたブートがPOR起動だったことは未証明**
  （C6の真coldハングは「warmしか見ていなかった」ことが真因だった型なので，
  ここは「達成」と書かない）。
- **スマホcentralは全組合せが通るわけではない**（BlueZ `hci0` では3チップとも成立）：

  | チップ×供給 | iPhone | Android |
  |---|---|---|
  | C3 × hal | NG（デバイス側は成功・iPhoneがtimeout・**詰まる**） | OK |
  | C3 × esp-idf | NG | NG（`ETIMEOUT`・詰まる） |
  | C5 × esp-idf | OK | NG（`ENOTCONN`） |
  | C6 × esp-idf | OK | OK |

  **Android/RPA対応は要件**（コード整理後に再検討）。C3の実害はtimeoutではなく
  **切断が届かずリンクを握ったまま広告が止まること**（復旧はリセットのみ）。
  詳細＝`.steering/…/evidence-matrix-ble-central-supply.md`。
- **bondストアはRAM backed**（`PERSIST=0`）＝電源断で鍵が消える。NVS化は未着手。
- `ESP32C3_BT_PVCY_FILTER`（connect-fix draft ii）は**BlueZのbondを壊す**ため
  既定OFF・出荷不可（1/4 vs baseline 7/7）。
- `test_porting` は asp3_core 側で **6項目→8項目**（`tap_plan(8U)`）に増えている。
  docs内に残る「6/6 passed」は6項目時代の記録。**本更新では再実行していない**。
- 上表のうち **ビルド（QEMU/実機の全11構成）は本更新で実測**したが，
  **実機動作・QEMU実行は再実行していない**（既存evidenceの引用）。

## ライセンス

ASP3カーネルはTOPPERSライセンス。ESP-IDF・esp-hal-3rdparty（Apache-2.0）・
Wi-Fi/BTバイナリblob（Espressif配布条件）・lwIP（BSD）はそれぞれの
ライセンスに従う。
