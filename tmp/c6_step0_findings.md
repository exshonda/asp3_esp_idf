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

---

## 追記（2026-07-06 続き）：ASP3が起動できない根本原因＝ESP-IDFのロック付きPMP。修正確定

Step0で「ハンドオフ後ASP3のstart.S bss/dataクリアで停止」まで絞った後，
GPIO3ビット・チェックポイント法（`target_kernel_impl.c`の`diag_mark(n)`が
GPIO0/1/2＝XIAO D0/D1/D2へ到達番号を保持出力→ハング後にLogic8で読む）で
段階を二分し，**根本原因を特定・修正まで確定**した。

### 決定的な観測列（GPIO値＝最後に到達したチェックポイント）

- diag_markに**UART0書き込みを入れていると値1で停止**した。これは実際の
  クラッシュではなく，**diag_mark内のUART0 FIFO書き込み自体がハンドオフ後
  文脈でハング**していた観測アーティファクト（ASP3のバナーが一度も
  出なかった理由もこれ）。GPIO保持出力のみに変えると先へ進んだ。
- GPIO-onlyのdiag_markで**値6**＝`hardware_init_hook`完走（WDT無効化・PCR・
  RTC全て通過），`software_init_hook`（値7）未到達。すなわち停止は
  **start.Sのbss/dataクリア**（0x40800010へのストア）。
  （hardware_init_hookが値6まで通れたのは呼び出し先がインライン化され
   スタック未使用＝RO領域への最初の実書き込みがbssクリアだったため。
   起動時SP=0x40801780もRO領域内だが未書き込みだった。）

### PMPダンプ（ジャンプ元ESP-IDF文脈で pmpcfg/pmpaddr を読み出し）

```
PMP cfg0=808d809f cfg1=8d808b8d cfg2=8d8b8089 cfg3=00009b8b
addr3=10200000 (<<2 => 0x40800000)   addr4=1020612d (<<2 => 0x408184b4)
addr5=10220000 (<<2 => 0x40880000)
entry4 cfg=0x8d = R1 W0 X1 TOR L1  → [0x40800000,0x408184b4) 読取専用+実行・ロック
entry5 cfg=0x8b = R1 W1 X0 TOR L1  → [0x408184b4,0x40880000) 読み書き可・ロック
```

ASP3の.data(0x40800000)/.bss(0x40800010-0x40803230)/istack(0x40800780,SP=0x40801780)
は**すべてentry4のRO領域内**。ロック付き(L=1)なのでM-modeでも書込み禁止が
強制され，bssクリアのストアがアクセスfault → ASP3はまだmtvec未設定のため
ESP-IDFの古い例外ハンドラへトラップ → ハング。

### 修正（確定）

`asp3/target/esp32c6_espidf/esp32c6.ld` の RAM を，ESP-IDFがRWで開けている
entry5領域へ移す：

```
RAM(rwx) : ORIGIN = 0x40819000, LENGTH = 412k   /* 旧: 0x40800000 / 448k */
```

これで .data=0x40819000・.bss=0x40819010..0x4081c230・istack=0x40819780 が
すべてRW領域に入り，**ハンドオフ後ASP3が bss/dataクリアを突破して
`software_init_hook` まで到達（GPIO値7）することを実機で確認**。

### 意義・整合

- 実施22「PMPはハードウェアロック」・実施33「NuttXハンドオフは成功」と符合。
  NuttXはASP3のRAM領域(0x40800000)をRO-lockしていなかったため起動できたが，
  ESP-IDF（＝Arduino相当のground truth）はロック付きROで保護するため起動不可
  だった，という差の説明がついた。
- これはハンドオフ手法（ESP-IDFのPMP環境）に固有の障害で，ASP3通常の
  Direct Boot動作（PMPロック無し）とは無関係。

### 残作業（本来のStep0＝388Hz再現テストへ）

1. チェックポイントを target_initialize→chip_initialize→カーネル起動 へ拡張し
   ASP3完全起動を確認。
2. sample1 → `apps/wifi_scan` に切替え，ハンドオフ後に esp_wifi_scan を実行して
   388Hz再現の有無を判定（本来のStep0ゲート）。
3. **UART0書き込みもハングする**問題（別途PMP/クロック要因の可能性）＝
   観測性の課題。scan結果の観測にはGPIO併用かUART復活の調査が要る。

### 注意（この時点のリポジトリ状態＝診断用の一時変更）

以下は**診断専用の一時変更**であり，調査完了後にrevertすること：
- `asp3/target/esp32c6_espidf/target_kernel_impl.c`：`diag_mark()`とチェック
  ポイント呼び出し（GPIO保持出力）＋TIMG0 PCRクロック有効化の検証コード
- `asp3/target/esp32c6_espidf/esp32c6.ld`：RAMを0x40819000/412kへ（通常は
  0x40800000/448k）
- `tmp/c6_handoff_source/`：ジャンプ元のGPIO準備・PMPダンプ・(GPIO_SELFTESTは撤去済)

---

## 追記2（2026-07-06）：ハンドオフ後ASP3は完全起動するがWi-Fiは使用不可＝Step0手法の行き止まり（確定）

PMP修正でASP3がハンドオフ後に起動できるようになった後，wifi_scanを走らせた：

### ASP3は完全起動する（PMP修正の最終確認）

sample1では **バナー "TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C6" 表示＋
task1/task2実行＋HRT割込み＋コンソール出力** まで確認＝ハンドオフ後に
ASP3カーネルが完全動作する。（副次的に，先に「UART書き込みがハング」と
見えたのはsio初期化前のdiag_mark生FIFO書き込み特有で，正規のsio経由なら
コンソール出力は正常。）

### しかしWi-Fiは init/skip どちらでもクラッシュ＝残留ESP-IDFポインタ

wifi_scanでの結果（コンソール観測）：

| 版 | 結果 |
|---|---|
| `esp_wifi_init`あり | `wifi_scan: esp_wifi_init`後 **Illegal instruction pc=0x420a0e36**（ra=`wifi_api_lock`） |
| `esp_wifi_init`スキップ（`HANDOFF_SKIP_WIFI_INIT`）→ scan直行 | **同一 Illegal instruction pc=0x420a0e36**（ra=`wifi_api_lock`） |

- pc=0x420a0e36 は **ESP-IDFの旧.text範囲（0x42000020〜0x420a253c）内**の
  アドレス＝ESP-IDF時代の関数ポインタ。
- `wifi_api_lock`がosi関数テーブル経由でmutexロックを呼ぶ際，その
  ポインタがESP-IDFのosi関数アドレスを保持したまま。ESP-IDFが古いRAM
  領域(0x40800000系＝ASP3は0x40819000へ移したので上書きしない)に残した
  osiテーブルを共有グローバルが指し続ける。ジャンプでflashを再マップ
  したためそのアドレスはゴミ → クラッシュ。
- **ASP3のbssクリアでもesp_wifi_init/skipでもリセットできない共有状態**
  （ROM/固定アドレスのグローバル）なので，init有無に関わらず同一箇所で死ぬ。

### 結論（確定）

**ESP-IDF→ASP3ハンドオフは「ハードウェアは温められるがWi-Fiソフト
状態は引き継げない」。2スタックのblob状態が共有グローバル（ESP-IDFの
コードポインタ）で不可分に絡むため，ASP3はハンドオフ後にWi-Fiを
使用できない。よって戦略ドキュメントStep0（ハンドオフでesp_wifi_scanを
走らせ静的/ランタイムを判定）はこの手法では達成不可。**

実施33（NuttX→agc_probe）が成功したのはagc_probeがWi-Fi APIを一切
呼ばない受動観測アプリだったため（osiポインタを踏まない）。Wi-Fi APIを
呼ぶ瞬間に残留ポインタで死ぬ，という差も整合する。

### 次の方向（戦略ドキュメントの本命）

ハンドオフは断念し，**ESP-IDF/Arduinoをground truth源**として使う：
ネイティブESP-IDF（`components/esp_wifi/esp_adapter.c`のosiラッパ）に
`(wrapper_id, timeout生値, 頻度)`ロギングを仕込み，`WiFiScan`中の
「正常なosi呼び出しパターン」を採取 → ASP3の388Hzストームと直接比較する。

### 実機操作のスクリプト化

`tmp/c6hw.sh`（全ポートOFF→port4,5 ON→窓でesptool，を一括化）：
`off`/`on`/`cycle`/`flash <bin> [offset]`/`flashfull <idfbuilddir>`。
観測は Monitor で `/dev/ttyUSB0`(CP2102,115200)。
