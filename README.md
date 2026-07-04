# asp3_esp_idf — TOPPERS/ASP3 Core ＋ Espressif esp-hal 統合

[TOPPERS/ASP3 Core](https://github.com/exshonda/asp3_core)（μITRON4.0系
シングルコアRTOS）を **ESP32-C3**（RV32IMC）で動かし，Espressifの
hal/soc層（esp-hal-3rdparty）およびWi-Fiと協調動作させるリポジトリ。
「各社SDKとの協調動作」の第5弾（pico-sdk・FSP・STM32Cube・MCUXpresso
に続く。経緯は asp3_core の `docs/dev/esp-idf-integration.md`）。

## 他のSDK統合と異なる点

ESP-IDFはFreeRTOS前提のOSフレームワークであり，ドライバ層
（`esp_driver_*`）自体がFreeRTOS APIを呼ぶため「SDKドライバをそのまま
使う」協調はできない。Zephyr/NuttXと同じ **esp-hal-3rdparty方式**＝
下層のhal/soc層のみを使い，Wi-FiバイナリblobはOSアダプタ
（os_adapter）のshimをASP3プリミティブで実装して載せる。

- ブートはASP3自前の **Direct Boot**（二段ブートローダ・esptoolイメージ
  形式とも不要。フラッシュ先頭のマジックナンバーでROMが直接起動）
- コンソールは **USB Serial/JTAG**（ネイティブUSBボードの/dev/ttyACM*
  直結）またはUART0

## 構成

```
asp3_esp_idf/
├── asp3/
│   ├── asp3_core                 … 純カーネル（submodule）
│   ├── asp3_esp_idf.cmake        … パス解決ヘルパ
│   └── target/esp32c3_espidf/    … ターゲット依存部（本リポジトリ側）
├── hal/                          … esp-hal-3rdparty（submodule．Phase B-1〜）
├── lwip/                         … lwIP（submodule．Phase C〜）
├── apps/                         … デモアプリ（wifi_scan/wifi_connect/wifi_dhcp）
└── docs/
```

## ビルド（QEMU）

Espressif版QEMU（esp32c3マシンを持つfork）とriscv64-unknown-elf-gccが必要。

```bash
git clone --recursive https://github.com/exshonda/asp3_esp_idf
cd asp3_esp_idf

cmake -S asp3/asp3_core -B build/esp32c3-qemu -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=ON
cmake --build build/esp32c3-qemu

qemu-system-riscv32 -M esp32c3 -nographic -semihosting \
  -drive file=build/esp32c3-qemu/asp_flash.bin,if=mtd,format=raw
```

## ビルド（実機・ESP32-C3ボード）

```bash
cmake -S asp3/asp3_core -B build/esp32c3 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
  -DASP3_TARGET=esp32c3_espidf \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c3_espidf \
  -DESP32C3_QEMU=OFF -DESP32C3_PORT=/dev/ttyACM0
cmake --build build/esp32c3
cmake --build build/esp32c3 --target run    # esptoolで書込み
```

コンソールは書込みと同じUSBポート（USB Serial/JTAG）に115200bpsで出る。
UARTブリッジ配線のあるボードは `-DESP32C3_CONSOLE=uart0`。

Wi-Fi＋TCP/IP（DHCP＋ping）のデモを実行する場合は以下を追加する
（`docs/wifi-shim.md`・`docs/tcpip-integration.md`参照）：

```bash
  -DESP32C3_WIFI=ON -DESP32C3_LWIP=ON \
  '-DASP3_EXTRA_COMPILE_DEFS=WIFI_SSID="...";WIFI_PASSWORD="..."' \
  -DASP3_APPLDIR=$PWD/apps/wifi_dhcp -DASP3_APPLNAME=wifi_dhcp
```

## 状態

| Phase | 内容 | 状態 |
|---|---|---|
| B-0 | リポジトリ骨格・ASP3_TARGET_DIRビルド（QEMU/実機） | **完了** |
| B-1 | esp-hal-3rdparty統合（LL層でコンソール・タイマを実装．`docs/hal-integration.md`） | **完了**（QEMU/実機 test_porting 6/6・実機testexec） |
| B-2a | Wi-Fi scan（init〜start〜scan） | **完了**（実機でAP16-17個をSSID/RSSI/ch受信） |
| B-2b | WPA2 AP接続（L2） | **完了**（実機でSTA_CONNECTED成立。`docs/wifi-shim.md`） |
| C | TCP/IP統合（lwIP，DHCP＋ping＋TCP＋UDP＋BSDソケット） | **完了**（実機でDHCP取得・ゲートウェイping・TCP（raw API・BSDソケット両方向）・UDPソケット通信成功。`docs/tcpip-integration.md`） |

## ライセンス

ASP3カーネルはTOPPERSライセンス。esp-hal-3rdparty（Apache-2.0）・
Wi-Fiバイナリblob（Espressif配布条件）はそれぞれのライセンスに従う。
