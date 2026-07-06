# ESP32-C6 Step 0（遅いハンドオフ再現テスト）実施結果

実施日：2026-07-06
対象：`tmp/c6_wifi_arduino_handoff_strategy.md` の Step 0
（「ネイティブESP-IDFでWi-Fiスキャンを完走させた直後にASP3へジャンプし，
388Hzが再現するか／スキャンが完走するか」を判定するゲート実験）

## 結論（決定的）

**ジャンプ機構は完全に正常。ASP3側の起動処理がハンドオフ後の
ハードウェア状態下でクラッシュしている。**

- ネイティブESP-IDF（`c6_handoff_source`＝shim無し・Arduino相当の
  ground truth側）で `esp_wifi_scan_start` を完走（毎回AP 20個検出）。
- その直後に `asp3_jump_now()`（実施33と同形状：`mie=0`・
  `mmu_hal_unmap_all` → `mmu_hal_map_region(vaddr=0x42000000,
  paddr=0x00200000, len=1MB)` → `cache_hal_invalidate_addr` →
  `0x42000008` へジャンプ）を実行。
- ジャンプ先を **最小ベアメタルUARTライタ**（`min_uart_target`：ASP3を
  一切通さず，クロックも触らず，UART0(0x60000000)のTX FIFOへ'U'を
  書き続けるだけ）にしたところ，**UART0に'U'が20秒間途切れなく
  出力され続けた**（38–39バイト/チャンク）。
- → 制御は確かに `0x42000008` に到達し，UART0出力経路も生きている。

対して，ジャンプ先を `sample1`（Direct Boot起動時は正常動作する
既知良品のASP3）や `wifi_scan` にすると**完全に無音**。両者の差は
「ASP3のstart.S以降を実行するか，しないか」だけ。したがって：

- ❌ ジャンプ失敗ではない
- ❌ MMU再マップ／キャッシュ無効化の問題でもない
- ❌ UART0が死んでいるのでもない
- ✅ **ASP3のstart.S（以降のカーネル起動）が，ESP-IDFの残した
  ハードウェア状態（クロックツリー等）の下では最初のUART出力に
  到達する前にクラッシュ／停止している**

min-targetがクロックを一切再設定せず生FIFOに書くだけで動いた対比から，
ASP3 start.Sのクロック／UART分周比などをROM状態前提で再設定する箇所が
第一容疑。

## 次の一手

ASP3 start.S の各段階（クロック設定前・`.bss`クリア前・スタック設定後
等）に「min-targetと同じクロック非依存の生UART0書き込み」を差し込み，
どこまで到達するかを二分探索する。start.Sは asp3_core submodule 側
（`arch/riscv_gcc/esp32c6` / `common`）にあり **直接編集は禁則**のため，
変更はasp3_coreリポジトリ側で行うか，計装専用の一時ビルドで行う。

## 再現手順（このマシン固有の罠を含む）

### ビルド

```bash
# ジャンプ元（ESP-IDF・ground truth）
export IDF_TOOLS_PATH=/home/honda/tools/espressif
source /home/honda/tools/esp-idf-v6.1/export.sh
cd tmp/c6_handoff_source && idf.py set-target esp32c6 && idf.py build
# → build/bootloader/bootloader.bin, partition-table.bin, c6_handoff_source.bin

# ジャンプ先（最小UARTライタ。診断オラクル）
cd tmp/min_uart_target
riscv32-esp-elf-gcc -march=rv32imc_zicsr_zifencei -mabi=ilp32 -nostdlib \
  -Wl,-T,link.ld -o min.elf min.S
riscv32-esp-elf-objcopy -O binary min.elf min.bin   # 36バイト
```

ジャンプ先を `sample1` にする場合は `build/esp32c6-uart0/asp_flash.bin` の
先頭1MBを切り出して使う（`asp_flash_trunc1M.bin`）。

### フラッシュ配置

| オフセット | 内容 |
|---|---|
| `0x0` | ESP-IDF bootloader |
| `0x8000` | partition-table |
| `0x10000` | `c6_handoff_source.bin`（scan→jump） |
| `0x200000` | ジャンプ先（`min.bin` または ASP3の先頭1MB） |

### このマシン固有の罠（重要）

1. **確実なリセット＝Acroname USBHub3cで全ポートOFF→ボード給電ポート
   のみON**。このボードは複数USB経路から給電されており，
   `usbhub3c_ctl.py off 3`（native USB）単独でも
   `off 5`（CP2102）単独でも電源が落ちない（もう片方＋第3経路が生かす）。
   **全ポートOFFで初めてEspressifデバイスが列挙消滅＝真の電源断**。
   その後 **port4+port5 のみON** でボードが復帰する
   （※ポート対応はセッションで変わるので都度 `usbhub3c_ctl.py names`）。
2. **esptoolの同期窓は電源ON後〜約5秒**（`c6_handoff_source`が起動から
   scan 3秒＋2秒でジャンプ→ハングするため）。ハング後は
   USB-JTAGのリセットも効かず，DTR/RTS操作は "Broken pipe" になる。
   → 全ポートOFF→ON直後に **ttyACM0出現を高速ポーリングして即esptool**
   （`--before default-reset`）を1コマンドで撃つと窓に入る。
   健全なチップならesptoolのUSB-JTAGリセットでダウンロードモードに入れる。
3. 2つのウォッチドッグが自動リブートを起こす：
   - INT_WDT（0.3秒）→ `sdkconfig.defaults` で `CONFIG_ESP_INT_WDT=n`
   - LP Super WDT（`rst:0x12 LP_SWDT_SYS`）→ `asp3_jump.c` で
     `LP_WDT_SWD_DISABLE` ビットを叩いて無効化（`esp_task_wdt_deinit`
     では止まらない別系統）。
   両方を止めると，クラッシュ後は自動リブートせず静止＝単発観測可能。

### 観測

UART0はCP2102（`/dev/ttyUSB0`）で115200bps。'U'(0x55)の連続出力が
見えれば制御到達，無音ならASP3 start.S側の問題。
