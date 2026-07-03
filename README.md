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

## 状態

| Phase | 内容 | 状態 |
|---|---|---|
| B-0 | リポジトリ骨格・ASP3_TARGET_DIRビルド（QEMU/実機） | 実施中 |
| B-1 | esp-hal-3rdparty統合（hal/soc層のAPI使用） | 未着手 |
| B-2 | Wi-Fi os_adapter shim（init〜scan〜AP接続） | 未着手 |

## ライセンス

ASP3カーネルはTOPPERSライセンス。esp-hal-3rdparty（Apache-2.0）・
Wi-Fiバイナリblob（Espressif配布条件）はそれぞれのライセンスに従う。
