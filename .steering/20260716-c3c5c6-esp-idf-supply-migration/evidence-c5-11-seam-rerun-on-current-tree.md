# evidence-c5-11 — seam を現ツリーで実機再現（esp/ 分離後・esptool 修正後）

2026-07-21。目的＝**「今のツリーで seam が動く」ことの確定**（新知見の獲得ではない）。
seam の採否は `evidence-c5-03-boot-method-decision.md` で **Direct Boot 継続**と決着済みで、
本ラウンドはその結論を変えない。

## 0. なぜ再現したか

- 本セッションで **ESP統合を `esp/{common,c3,c5,c6}` へ物理移動**した（大規模なパス変更）。
- あわせて **seam のビルドが黙って壊れていた**のを修正した（`esptool` の PATH 依存）。
- ⇒ 「ビルドは通るが起動するか」は未確認だった。**S3 のブート方式（IDF 二段
  ブートローダ → app_main 直前で ASP3 へハンドオフ）の RISC-V 側 参照実装**として
  seam を温存する以上、腐っていないことを一度確定させる価値がある。

## 1. 手順（★このPCの実パスへ読み替え済み）

evidence-c5-03 §7 の手順は `~/tools/espressif/...` を前提にしており、**このPCには存在しない**
（別PC前提＝既知の罠）。実際に動いた手順：

```bash
# 1) bootloader / partition-table（最小 IDF プロジェクト）
export IDF_PATH=<repo>/esp-idf            # submodule v5.5.4
export IDF_TOOLS_PATH=$HOME/.espressif
source $HOME/.espressif/python_env/idf5.5_py3.12_env/bin/activate
export PATH="$HOME/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20260121/riscv32-esp-elf/bin:$IDF_PATH/tools:$PATH"
idf.py set-target esp32c5 && idf.py build
#   -> build/bootloader/bootloader.bin (22128B) / build/partition_table/partition-table.bin (3072B)

# 2) ASP3 seam app（★コンソールは uart0＝既定にすること。理由は §3）
cmake -S asp3/asp3_core -B build/seam_uart -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake \
  -DASP3_TARGET=esp32c5_espidf -DASP3_TARGET_DIR=<repo>/asp3/target/esp32c5_espidf \
  -DASP3_SEAM_BOOT=ON
#   -> build/seam_uart/asp_seam.bin (85152B)

# 3) 書込み（★erase-flash 必須＝0x0 の Direct Boot マジックを消す）
esptool --chip esp32c5 --port <jtag by-id> --before usb-reset --after no-reset erase-flash
esptool --chip esp32c5 --port <jtag by-id> --before no-reset --after no-reset write-flash \
  0x2000 bootloader.bin 0x8000 partition-table.bin 0x10000 asp_seam.bin

# 4) ★真POWERON で起動（§2）＋ UART0(CP2102N) で捕捉
sudo uhubctl -l 1-6 -p 2-3 -a off; sleep 3; sudo uhubctl -l 1-6 -p 2-3 -a on
#   捕捉は dtr=False/rts=False で open（CP2102N が EN を駆動＝クリーンな EN リセット）
```

★`erase-flash` は **stub が要る**（`--no-stub` だと
`ESP32-C5 ROM does not support function erase_flash`）。

## 2. ★踏んだ罠：seam は POWERON でしか起動しない（実測）

最初 USB-JTAG 経由リセット（`--before usb-reset`）で起動を試み、**WDT リセットループ**になった：

```
rst:0x15 (USB_UART_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
rst:0x7 (TG0_WDT_HPSYS),boot:0x18 (SPI_FAST_FLASH_BOOT)   ← 以後ループ
Core0 Saved PC:0x40038598
```
**IDF bootloader のログが一行も出ない**＝ROM が bootloader へ渡していない（`entry 0x...` 無し）。
flash 内容を読み戻すと bootloader は 0x2000 に**バイト一致で正しく存在**した
⇒ 書込みの失敗ではなく **download latch**。

**真POWERON（uhubctl 両ポート断→復電）にしたら一発で起動**。
evidence-c5-03 の成功ログも `rst:0x1 (POWERON)` である＝当時から同じ条件だった
（当時は明記されていなかったので、ここに残す）。

## 3. ★コンソールは uart0 でなければ観測できない

seam 起動は POWERON 依存（§2）で、POWERON は CP2102N の EN 経由で撃つ。
一方 `-DESP32C5_CONSOLE=usbjtag` にすると ASP3 の出力は USB-JTAG 側へ出るが、
**C5 は usbjtag console を掴むと wedge する**（既知＝memory `c5-uart-capture-open-resets-dut`）。
⇒ seam の観測は **uart0 コンソール（既定）** で行うこと。
実際 usbjtag ビルドでは bootloader ログ（UART0）は取れるが ASP3 出力が取れず、
ACM 側も無出力だった。

## 4. 結果（★実機GREEN）

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
entry 0x4084bbaa
I (23) boot: ESP-IDF v5.5.4 2nd stage bootloader
I (24) boot: chip revision: v1.0
I (27) boot.esp32c5: SPI Speed      : 80MHz / Mode : DIO / Flash Size : 2MB
I (65) boot:  2 factory          factory app      00 00 00010000 00100000
I (75) esp_image: segment 0: paddr=00010020 vaddr=42010020 size=01930h (  6448) map
I (84) esp_image: segment 1: paddr=00011958 vaddr=00000000 size=0e6e0h ( 59104)
I (100) esp_image: segment 2: paddr=00020040 vaddr=42000040 size=04c30h ( 19504) map
I (104) boot: Loaded app from partition at offset 0x10000

TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 21 2026, ...)
task1 is running (001). … (継続)
```

⇒ **IDF 二段ブートローダ → ASP3 起動 → タスク実行**まで到達。
イメージ規約（フラッシュセグメントちょうど2つ＝seg0/seg2 が DROM,IROM・seg1 は PADDING、
`.flash.appdesc`、checksum/hash valid）も現ツリーで満たされている。

**∴ esp/ 分離・esptool 修正を経た現ツリーで seam は生きている。**

## 5. 後始末

C5 を **Direct Boot（既定・既知良好）へ復旧**した：
`erase-flash` → `write-flash 0x0 asp_flash.bin`（uart0 コンソール版）。
真cold で `TOPPERS/ASP3 Kernel Release 3.7.2` バナー＋`task1 is running` 53回を確認済み。

★復旧イメージは**先に作ってから** erase した（順序が逆だと戻せない）。

## 6. 申し送り

- `run.cmake:22` のコメントが案内する `scripts/seam_c5/build_bootloader.sh` は
  **リポジトリに存在しない**（`scripts/` ディレクトリ自体が無い）。本書 §1 が実際に動く手順。
  当該コメントは本書を指すよう直すべき。
- seam の採否は変わらない（Direct Boot 継続）。本書は「腐っていないことの確認」であり、
  evidence-c5-03 の判断を上書きするものではない。
