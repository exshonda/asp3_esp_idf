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

---

## 追記3（2026-07-06）：ground truth採取＝ネイティブESP-IDFのosi呼び出し頻度

ハンドオフStep0が行き止まりと確定した後，戦略ドキュメント本命の
「ESP-IDF/Arduinoをground truth源として使う」を実施．IDF本体は無改変で，
`tmp/c6_handoff_source`（ネイティブESP-IDF）にリンカ`--wrap`で
FreeRTOSプリミティブを横取りするカウンタを仕込み，WiFiスキャン中の
正常な呼び出し頻度を採取した（`main/main.c`の`__wrap_xQueue*`＋
`main/CMakeLists.txt`の`idf_build_set_property(LINK_OPTIONS ...--wrap...)`）．

### 結果（ネイティブESP-IDF・C6・WiFiスキャン中 2.5秒）

| osi呼び出し（FreeRTOSプリミティブ） | 頻度 |
|---|---|
| `xQueueReceive`（WiFiタスク主ループのqueue_recv） | **59/s** |
| `xQueueGenericSend`（queue_send系） | **77/s** |
| `xQueueSemaphoreTake`（semphr_take系） | **40/s** |

→ **正常値は数十/s（40〜80）**．ASP3 shimの**388Hz**（過去計測の「take」，
`docs/wifi-shim-c6.md`）と比べ **約10倍過剰**．

### ASP3 shim側のタイムアウト変換レビュー（単位バグではない）

`esp_shim_tick_to_tmo`（`asp3/target/esp32c6_espidf/wifi/esp_shim.c`）：
```
block_time_tick==BLOCK_FOREVER -> TMO_FEVR
block_time_tick==0             -> TMO_POL
それ以外                        -> tick(ms) * 1000 = µs
```
`task_ms_to_tick`は1:1（1 tick=1ms）なので block_time_tick はms．
ASP3のTMOはµs単位（このポートの実装）なので `ms*1000=µs` は正しい．
→ **388Hzは単純な単位変換バグではない**．

### 見立て（要ASP3側測定で確認）

388Hzは，WiFiタスクが `semphr_take`/`queue_recv` の**タイムアウトで
空振りループ**している症状の可能性が高い＝RXパケット/割込みイベントが
流れてこないため待ちが毎回タイムアウトして高速リトライ．これは元の
C6根本問題（AGC凍結でスキャンが実データを受信しない＝発見AP 0）と整合し，
**388Hzはその下流症状**という解釈．ground truthの
「native 40/s（イベント正常）vs ASP3 388/s（空振り）」も支持する．

### 次のステップ

1. ASP3のshim（`esp_wifi_adapter.c`の`semphr_take_wrapper`/
   `queue_recv_wrapper`＝外側リポジトリ・編集可）にカウンタを入れ，
   Direct BootのASP3 wifi_scan（スキャンは走るがAP 0）で実測．
   どの呼び出しが388/sかを確定し，native 40/sと突き合わせる．
2. 388/sが「タイムアウト空振り」なら，イベントが来ない理由＝AGC/RX経路
   （元のC6根本問題）へ回帰．「値が同じだが起床しない＝wake取りこぼし」
   なら shim の sem give/isr 経路を監査．

---

## 追記4（2026-07-06）：ASP3側osi測定はDirect Boot wifi_scanのLP Super WDTリブートに阻まれる

ground truth（native）採取後，比較のためASP3側shim（`esp_wifi_adapter.c`の
semphr_take/queue_recv/queue_send/from_isr/timer_arm）にカウンタを追加し，
wifi_scan.cにスキャン中2秒のGT-ASP3頻度ダンプを入れて，**Direct Boot**の
ASP3 wifi_scanで実測を試みた（RAMは通常配置0x40800000/448kへ戻した）．

### 結果：esp_wifi_init中にリブートループ（rst:0x12 LP_SWDT_SYS）

Direct Boot ASP3 wifi_scanは
`initializing shim`→`esp_wifi_init`→`net80211 rom`→`wifi driver task`まで
進むが，`W (122) rtc_clk: invalid RTC_XTAL_FREQ_REG value, assume 40MHz`の
直後に **LP Super Watchdog リセット（rst:0x12）** で約122ms周期のリブート
ループに陥り，スキャン（＝GT-ASP3測定点）に到達しない．

`hardware_init_hook`でSWDをauto-feed＋`SWD_DISABLE(bit30)`にしても解消せず
＝**esp_wifi_init中にblob（rtc_clk/phy）がSuper WDTを再武装**し，ASP3には
ESP-IDFのようなWDT給餌タスクが無いため発火する，と判断．これは元の
未解決C6 Wi-Fi調査（blob統合）の領域．過去の388Hz計測がJTAG mdw経由
だったのも，このコンソール/スキャンの不安定さと整合する．

### 現状の到達点と次の選択肢

- ✅ ground truth（native ESP-IDF）：qRecv 59/s・qSend 77/s・semTake 40/s（確定・コミット済）
- ⬜ ASP3側実測：Direct Bootのsuper-WDTリブートで未達
- 次の選択肢：
  1. blobがsuper-WDTを再武装する箇所を特定し，恒久的に無効化 or 周期給餌タスクを追加
  2. カウンタをRTC RAM(0x50000000・リセット保持)に置き，リブートループの
     各サイクル（~122ms）で累積→次ブート冒頭でダンプして率換算（要実装）
  3. 過去実施と同様JTAG mdwで計装カウンタを直読み（ただし内蔵USB-JTAGは
     チップ稼働中/ハング時に不安定＝要外部プローブ）

---

## 追記5（2026-07-06）：option2（RTC RAM累積）でASP3側osiを測定→真のブロッカーは早期super-WDTリセット

RTC RAM(0x50000000)がLP Super WDTリセット(rst:0x12)を跨いで保持されることを
確認（boot#が2→3→4…と増加）．osiカウンタをRTC RAMに置き，リブートループを
跨いで累積してダンプした（`esp_wifi_adapter.c`のGT_*マクロ＋`wifi_scan.c`の
ブート冒頭ダンプ）．

### 結果：各リブートサイクルでosi各5回のみ→WiFiタスク定常に未到達

```
GT-ASP3 accum boot#=2: semTake=10 semGive=10 qRecv=10 qSend=10 ...
GT-ASP3 accum boot#=3: semTake=15 semGive=15 qRecv=15 qSend=15 ...
boot#=4: 20  boot#=5: 25  boot#=6: 30   （＝+5/サイクル）
各サイクル直前に W (123) rtc_clk: invalid RTC_XTAL_FREQ_REG value, assume 40MHz
→ rst:0x12 (LP_SWDT_SYS)
```

- 1リブートサイクル(≈123ms)あたり osi各種**5回のみ**．WiFiタスクの
  定常スピン（388Hz）には**到達していない**．
- すなわち**現状のコードでは388Hzすら再現しない**．真のブロッカーは
  「**esp_wifi_init中の〜123ms時点でのLP super-WDTリセット**」．
- 直前に `invalid RTC_XTAL_FREQ_REG value`（Direct Bootは2段ブートローダが
  無くRTC_XTAL_FREQ_REGを設定しない）が出る．blobのrtc_clk/LPクロック
  設定が絡む疑い．
- `hardware_init_hook`でSWD auto-feed＋SWD_DISABLE(bit30)を設定しても
  発火する＝**blobがesp_wifi_init中にsuper-WDTを再武装**している．

### 意義

- option2（RTC RAM累積）は「リブートループ中でもASP3側の挙動を測る」
  手法として有効と実証．
- C6 Wi-Fiの当面の最上位ブロッカーは388Hzではなく，
  **esp_wifi_init中の早期super-WDTリセット**であることが判明．
  これを解かないとスキャン（＝388Hz測定点や実データ受信）に到達しない．

### 次のリード

1. super-WDTがどこで再武装されるか特定（blobのrtc_clk/phy init）．
   LP_WDT/RTC_WDTを恒久無効化 or 高優先度ASP3ハンドラで周期給餌．
2. RTC_XTAL_FREQ_REG を2段ブートローダ相当値（40MHz=0x00280028等）に
   設定してから esp_wifi_init を呼ぶ（LPクロック誤設定→WDT誤発火の解消を狙う）．

---

## 追記6（2026-07-06）：RTC_XTAL_FREQ_REGは良性（自己修正）／super-WDT再武装が確定

- `RTC_XTAL_FREQ_REG`リード：`rtc_clk_xtal_freq_get`は値0なら警告後
  自分で40MHzを`clk_ll_xtal_store_freq_mhz`する（自己修正）．よって
  この警告は良性で，super-WDTリセットの原因ではない（設定しても無意味）．
- 決定的切り分け：**sample1のDirect Bootは正常動作**（task1/2ループ）＝
  `hardware_init_hook`のsuper-WDT無効化(auto-feed+DISABLE)は非Wi-Fiでは
  効いている．**wifi_scan（esp_wifi_init）でのみ発火**＝
  **blobがesp_wifi_init中にsuper-WDTを再武装**していることが確定．

### 修正方針（次の実装）

esp_wifi_init中もsuper-WDTを生かし続ける：
- **高優先度ASP3周期ハンドラ（CRE_CYC）で〜50msごとにsuper-WDTを給餌**
  （LP_WDT SWD_FEED または SWD_CONFIGのauto-feed/disableを再設定）．
  cyclicは割込み文脈でタスクより上位に走るため，esp_wifi_initが
  タスク文脈でブロック中でも給餌が届く．
- 実装：`wifi_scan.cfg`（または target cfg）に`CRE_CYC`追加＋給餌
  ハンドラ．これでスキャンに到達すれば，388Hz測定・実データ受信
  （＝元のAGC問題）の調査へ進める．

---

## 追記7（2026-07-06）：super-WDTリセットの根本原因＝SWD書込み保護キーのバグ（修正）

### 発見：ESP32C6_LP_WDT_SWD_WKEYが誤り

`asp3/asp3_core/arch/riscv_gcc/esp32c6/esp32c6.h`（submodule）：
```
ESP32C6_LP_WDT_WDT_WKEY = 0x50D83AA1U   （RTC WDT用・正しい）
ESP32C6_LP_WDT_SWD_WKEY = 0x8F1D312AU   （super-WDT用・★誤り★）
```
esp-idf正本（`components/esp_hal_wdt/esp32c6/include/hal/lpwdt_ll.h`）：
```
LP_WDT_WKEY_VALUE     = 0x50D83AA1
LP_WDT_SWD_WKEY_VALUE = 0x50D83AA1   （両方とも同じ）
```
→ asp3のSWDキー0x8F1D312Aは誤り．正しくは0x50D83AA1．

### 影響と整合

- `hardware_init_hook`のsuper-WDT無効化（SWD_WPROTECT解除→SWD_CONFIG書込み）
  が**誤キーで拒否され，実際には無効化されていなかった**．
- sample1 Direct Bootが正常なのは，super-WDTが既定で発火しないため
  （何も触らなければ問題ない）．**esp_wifi_initがsuper-WDTを起動/武装
  した瞬間，ASP3は誤キーで止められず発火**＝リブートループ，と整合．
- ハンドオフのasp3_jump.cは正しい0x50D83AA1を直書きしていたため効いていた，
  という不整合の説明もつく．

### 修正

`target_kernel_impl.c`（外側リポジトリ）の`hardware_init_hook`で，SWD
書込み保護解除に正しいキー0x50D83AA1を直接使うよう変更（submoduleの
esp32c6.h修正は別途bump時）．

**恒久修正（要submodule側）**：`arch/riscv_gcc/esp32c6/esp32c6.h`の
`ESP32C6_LP_WDT_SWD_WKEY`を0x50D83AA1に訂正．

### ハードウェア確認は保留（実機I/O不安定）

本修正の実機確認中に，多数の電源サイクル・抜き差しの影響で
CP2102(ttyUSB0)がUART出力を返さなくなった（Acronameポート対応も
セッション中に変化＝ボードが一時port3へ）．物理接続（CP2102↔D6/D7・
ボード電源/リセット）の再確認後に再テストが必要．修正自体はesp-idf
正本との照合で高確度．

---

## 追記6（2026-07-06）★決定的★：SWD-key修正を実機確認＝リブートループ解消，Direct BootでWi-Fiスキャンが完走。「388Hzストーム」は再現せず（仮説反転）

追記5のSWD-key修正（0x50D83AA1）を実機で確認し，本来のStep0ゲート
（388Hz storm再現の有無 vs scan完走）に**決定的な答え**を得た。

### 観測環境の復旧

- Direct Bootが不安定化していた問題は，**esptool経由のhard-reset**
  （`esptool --before default-reset --after hard-reset` をボード native
  USB=ttyACM0 に対して実行）でDirect Bootが確実に起動することを確認。
  UART0出力はFT232R(ttyUSB0)で観測（acronameのOn/Offは不要，むしろ
  FT232Rは物理抜き差しでしかリセットできないため電源トグルは使わない）。
- フラッシュ配置＝**素のDirect Boot**（ローダ無し）：`0x0` に
  `asp_flash_trunc1M.bin`（wifi_scanイメージ先頭1MB＝全実体544KB<1MB）。

### 決定的観測（Direct Boot・SWD修正込み wifi_scan）

```
rst:0x15 (USB_UART_HPSYS) ...             ← LP_SWDT_SYS(rst:0x12)リブート消滅
TOPPERS/ASP3 Kernel Release 3.7.2 ...     ← ASP3完全起動
wifi_scan: esp_wifi_init  -> 0            ← C6 ASP3で初のinit完走
wifi_scan: esp_wifi_start -> 0
wifi_scan: esp_wifi_scan_start -> 0
OSIRATE/s semTake=0 qRecv=24 qSend=16 qSendISR=8 timerArm=8
OSIRATE/s semTake=0 qRecv=10 qSend=7  qSendISR=3 timerArm=4
esp_event: WIFI_EVENT id=1                ← WIFI_EVENT_SCAN_DONE(=1)発火＝scan完走
（flood-proof値，次boot時のboot dumpで読出し）
GT-ASP3 prev-scan: reach=3 apcount=0 scanerr=0
```

### 結論（Step0ゲート・確定）

| 問い | 結果 |
|---|---|
| SWD-key修正でリブートループは止まるか | **止まる**（rst:0x12消滅，起動継続） |
| esp_wifi_init/start/scan_start | **全て ->0 成功**（C6 ASP3で初） |
| 388Hz osi storm は再現するか | **再現しない**。osi実測は10〜24/s（ピーク），1run累積でも qRecv~40/s未満・timerArm数/s。native ground truth（40〜80/s）と同等かむしろ低い |
| scanは完走するか | **完走する**（scan_start->0，SCAN_DONE発火，get_ap_records->0=ESP_OK，`reach=3`で全経路実行） |
| APは見つかるか | **0個**（apcount=0，err=0）。同一アンテナ環境でC3は多数検出＝**C6のRX/PHY側で受信できていない** |

→ 従来「388Hz osi-call storm（ランタイムshimバグ）」と見ていたのは
  **修正前の壊れた状態（super-WDTリブートループ）の副産物**だった。
  SWD-key修正で真の姿が判明：**osiは正常，scanは完走，しかしAP=0**。
  バグの所在は osi shim でも static init でもなく，
  **scanは完走するがRXが1フレームも取れない＝PHY/RF/AGC（またはチャネル
  dwellタイミング）側**へ移動した。これは戦略ドキュメントが当初から
  疑っていたAGC/PHY問題と整合する。

### 副次的観測（別issue，388Hzとは無関係）

- scan起動後，ASP3カーネルの `no time event is processed in hrt
  interrupt.`（`asp3_core/kernel/time_event.c:625`，submodule＝禁則）が
  高頻度で出続け，logtaskを飽和させ後段のsyslog（"APs found"行）を
  ロスさせる。HRTが期限到来イベント無しに発火＝**HRTコンパレータが
  過去/即時値で武装される疑い**（timerArm自体は数/sなので，1 armあたり
  複数の空フェッチが出ている）。**これがチャネルdwellのタイミングを
  狂わせAP=0を招いている可能性**があり，次の第一容疑。
- Wi-Fiアクティブ時，blobのROMログ（wifi_regi2c等）とASP3 syslogが
  排他無しでUART0を同時書き込み→文字化け。観測性の課題（機能影響なし）。

### 次の一手（RX/PHY・HRTタイミングへ）

1. `no time event` 氾濫の定量化と原因特定：HRTコンパレータ武装値
   （target_hrt_set_event相当）を実測。過去値武装ならチャネルdwellも
   狂う。ASP3のHRT実装 vs esp_timer shimの時間軸整合を確認。
2. scanのチャネルdwellを固定長・受動scanに変え，特定chに留めてRXが
   取れるか（AP=0がタイミング起因かRF起因かの切り分け）。
3. AGCイネーブル経路（`enable_agc`/`register_chipv7_phy` の--wrap計装は
   既にあり）でPHYキャリブが正しく完了しているか確認。

### この時点のリポジトリ状態（診断用の一時変更・未コミット）

- `apps/wifi_scan/wifi_scan.c`：scan待ちループのOSIRATE毎秒デルタ出力，
  boot dumpのprev-scan結果（RTC[8..10]），scan後の重い診断ダンプを
  `#if ...&&0`で一時無効化。
- `asp3/target/esp32c6_espidf/esp32c6.ld`：ハンドオフ用RAM=0x40819000。
  **素のDirect Bootなら通常の0x40800000/448kで良い**（0x40819000でも
  動作するが本来はrevert対象）。
- `target_kernel_impl.c`：SWD-key修正（0x50D83AA1）＝これは**恒久修正の
  実体**（submodule bump前の暫定置き場），diag_mark等は撤去対象。

---

## 追記7（2026-07-06）★決定的★：JTAG直読みで2根本原因を確定＝(A)Wi-Fi RX割込み未配送で0 AP，(B)HRT alarmレベル再ラッチでスプリアス割込み22%

追記6で「scan完走するがAP=0＋no time event氾濫」まで来た後，UART文字化けを
回避するため**OpenOCD(内蔵USB-JTAG)でCPU haltしRTC RAMを直読み**した。
RTC RAMはCPUリセットを跨いで保持されるので直近runの最終値がそのまま読める。

### 観測手順（確実・flood-proof）

```bash
OCD=/home/honda/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32
export OPENOCD_SCRIPTS=$OCD/share/openocd/scripts
# アプリをrun→scan完走まで待つ→halt→RTC RAM直読み
$OCD/bin/openocd -f board/esp32c6-builtin.cfg \
  -c init -c halt -c "mdw 0x50000000 16" -c resume -c exit
```

### 直読み結果（boot#=12時点）

```
0x50000000: c6057a11 0000000c 0000006e 0000006e 00000205 00000181 00000079 0000009a
             magic     boot#=12  semTk=110 semGv=110 qRecv=517 qSend=385 qSISR=121 tmArm=154
0x50000020: 00000000 00000000 00000003 0000000b 000050c7 000050c7 000011df 00000000
             apcnt=0   scnerr=0  reach=3   wifiInt=11 hrtEnt=  hrtFir=  hrtSpur= -
                                                     20679    20679    4575
```

### 根本原因(A)：Wi-Fi RX割込みが配送されていない → 0 AP

- `wifiInt=11`（Wi-Fi割込み線1〜15の発火総数．12 run累積＝**実質0/run**）．
- アクティブscanでビーコンを受信していれば MAC RX割込みが数百〜数千回
  発火するはず．ほぼ0＝**受信割込みがCPUに届いていない**．
- `esp_wifi_scan_start`はタイマ駆動でSCAN_DONEを出す（reach=3）が，
  RX割込みが無いため受信フレームが0件処理＝**apcount=0**．
- これはC6割込みコントローラの配送問題（既知の`int1`テスト失敗＝
  「CFG_INTの3エントリ目以降がどの物理線/FROM_CPU組合せでも配送されない」．
  `docs/dev/esp32c6-target.md`）と**同系統**と考えられる．
- 注意：RF/PHY(AGC)が受信できていない場合もMAC RX割込みは出ないため，
  wifiInt≒0は「割込み未配送」と「RF未受信」の両方に整合する．切り分けは
  次段（INTMTX生ステータス／MAC RX状態のポーリング）で行う．

### 根本原因(B)：HRT alarmのレベル再ラッチでスプリアス割込み → no time event氾濫

- `hrtSpur=4575 / hrtEntries=20679 ≒ 22%` がスプリアス（ハンドラ入場時に
  systimer alarm int_stはセット済みだが，武装target(`g_hrt_last_target`)は
  未来＝counter<target）．
- 機構：`target_hrt_handler`は`signal_time`の**前**に`systimer_ll_clear_
  alarm_int`する．alarm発火時点でcounterは既に旧target以上のため，
  **oneshot=レベル比較(alarm=counter>=target)のint_rawがclear直後に
  再ラッチ**→FROM_CPU_0とは別に systimer int が再度立ち→もう一度入場
   →`signal_time`で処理対象なし＝"no time event"．
- 12 runで4575回≒**30〜38/s**．これは実観測の"no time event"氾濫率と一致．
- 修正案：`target_hrt_set_event`でtargetを未来へ武装した**後**に
  `systimer_ll_clear_alarm_int`する（counter<targetの状態でクリアすれば
  レベルは非アサート＝再ラッチしない）．race安全のためclear→
  `if(read>=target) force_int()`の順にする．C3は同一構造だが，C3では
  この氾濫が顕在化しなかった（Wi-Fiが動く＝別要因）ため見過ごされていた．

### C3→C6 割込みコントローラの差分（本調査で整理）

| 項目 | C3 | C6 |
|---|---|---|
| ルーティング/CPU線制御 | 単一INTMTX_BASEに同居 | INTMTX(ソース)とPLIC_MX(CPU線)が分離 |
| ペリフェラルソース数 | 62（status 2語） | 77（status 3語） |
| CPU側呼称 | INTMTX | PLIC_MX（当初CLIC誤認） |
| FROM_CPU(ソフト割込) | SYSTEM_BASE | INTPRIペリフェラル |
| 線type(level/edge) | 同 | PLIC_MX offset0x004（Wi-Fi/timer=level必須） |
| 既知未解決 | — | int1＝CFG_INT 3エントリ目以降が未配送 |

### 次の一手

1. (B)の修正（set_eventでのclear順序）を入れ，`no time event`氾濫と
   `hrtSpur`が消えることをJTAGで確認（低リスク・target層のみ）．
2. (A)の切り分け：Wi-Fi RX割込みが「配送されない」のか「そもそもMACが
   割込みを上げていない(RF未受信)」のか．INTMTXの当該ソース生ステータスを
   ポーリングし，pendingなのにCPU未到達なら配送問題（int1と同根）．
3. int1未解決issueの決着がC6 Wi-Fiの本丸．`docs/dev/esp32c6-target.md`と統合．

### 計装ファイル（診断用の一時変更・未コミット）

- `asp3/target/esp32c6_espidf/target_timer.c`/`.h`：HRTハンドラのRTC計測
  （[12]entries [13]fired [14]spurious）と`g_hrt_last_target`保存．
- `apps/wifi_scan/wifi_scan.c`：RTC[8..11]（apcount/err/reach/wifiInt）記録，
  boot dump拡張，OSIRATE毎秒デルタ，重いダンプの`#if 0`，boot後tslp_tsk．

---

## 追記8（2026-07-06）★決定的★：割込み系はシロ＝0 APはPHY/RF/AGC受信側。JTAGソフト発火でディスパッチ健全性を実証

追記7で「wifiInt≒0」を得た後，「割込みが配送されないのか／MACが割込みを
上げていないのか」をJTAG直読み＋ソフト発火で完全に切り分けた。

### JTAGで割込みコントローラのライブ状態を直読み（スキャン中にhalt）

```
PLIC_MX ENABLE = 0x00030002  → 有効CPU線 = 1, 16, 17
PLIC_MX EIP    = 0x00010000  → 線16(timer)のみCPU到達中
mie            = 0xffffffff  ✓
```

INTMTXルーティング表（0x60010000+src*4）を全77ソース読み，非0を同定：

| src | 名称 | →CPU線 | ENABLE |
|---|---|---|---|
| 0 | **ETS_WIFI_MAC_INTR_SOURCE** | **線1** | **有効✓** |
| 2 | ETS_WIFI_PWR_INTR_SOURCE | 線1 | 有効✓ |
| 22 | ETS_FROM_CPU_INTR0（timer force） | 線16 | 有効✓ |
| 23 | ETS_FROM_CPU_INTR1 | 線18 | **無効✗** |
| 41 | ETS_I2S0 | 線17 | 有効 |
| 57 | ETS_SYSTIMER_TARGET0（HRT） | 線16 | 有効✓ |

→ **Wi-Fi MAC(src0)は有効な線1に正しく配線されている**。唯一の未enable＝
線18=FROM_CPU_1（ソフト割込み源．int1問題の実体）だが**Wi-Fi RXとは無関係**。

### INTMTX生STATUSをスキャン中に複数回サンプル

```
STATUS0 = 0x00000000（毎回）  ← bit0=WIFI_MAC は一度もアサートされない
STATUS1 = 0x02000000          ← bit25=src57=SYSTIMER_TARGET0（timer・正常）
esp_shim_int_count[1] = 11（線1の総ディスパッチ数．RX受信なら数百のはず）
```

→ **MACソースが一度も立たない＝MACはRX割込みを上げていない**。

### JTAGソフト発火で「ディスパッチ経路が健全」を実証（コード変更なし）

初期化後（線1 enable済み・MAC ISR登録済み）のアイドル状態で，
未使用のFROM_CPU_2(src24)を線1へ振ってソフト発火：

```
mww 0x60010060 1     # INTMTX: src24(FROM_CPU_2) -> CPU線1
mww 0x600c5098 1     # INTPRI: FROM_CPU_2 アサート
bp 0x42001d5a hw     # esp_shim_inthdr_1 入口にHWブレークポイント
resume
→ Target halted PC=0x42001D5A, debug_reason=1（★BP命中★）
```

**ソフト発火した割込みが線1で配送され，ディスパッチャesp_shim_inthdr_1へ
到達＝mtvec→inh_table[1]→ハンドラの経路は完全に機能する**（無限再発火は
BPで停止．後始末でFROM_CPU_2クリア・ルート解除）。

### 結論（Step0の最終確定）

**C6 Wi-Fiスキャンの0 APは，割込みの配送・ディスパッチ・マスクの問題では
断じてない**（線1は有効・発火させればISRに到達する）。原因は
**MACが一度もRX割込みを上げない＝無線が受信/復調できていない＝
PHY/RF/AGC受信側**．これは戦略ドキュメントが当初から狙っていた
「Wi-Fi/AGC調査」そのものへ収束した。

副次：`no time event`氾濫（HRT alarmのstale再発火，追記7の(B)）は残るが，
Wi-Fi受信不能とは独立の別issue（C3は同一HRT構造でscan成功）．

### 次の一手（PHY/RF/AGC受信経路へ）

1. PHYキャリブレーション完了の確認：`register_chipv7_phy`/`enable_agc`の
   --wrap計装（既設）でAGCイネーブルとキャリブ値が正常か．
2. ネイティブESP-IDFでの同一チャネルscan時のMAC/PHYレジスタ（AGCゲイン・
   RX状態）をJTAGで読み，ASP3版と差分を取る（ground truth比較）．
3. RX経路のクロック/アナログ電源（MODEM_*・PHY電源ドメイン）が
   ASP3側で正しくenableされているか（esp_wifi_adapter.cのclock/phy_enable
   ラッパの実測）．

### 観測手段の確立（重要）

JTAG（OpenOCD内蔵USB-JTAG）でのhalt＋レジスタ/RAM直読み＋ソフト発火
（FROM_CPU＋HWブレークポイント）が，UART文字化けと無縁で最も確実。
以降の調査はこの方式を基本とする。

---

## 追記9（2026-07-06）：no time event氾濫はWi-Fi起因と切り分け確定（HRT固有バグではない）

追記7(B)のno time event氾濫が「HRT固有」か「Wi-Fi起因」かをJTAG計測で決着。
`DIAG_NO_WIFI`（esp_wifi_init以降を一切実行せず素のASP3で~10sアイドル）で
HRT計測を取り，Wi-Fi有効時と比較：

| | hrtEntries | hrtSpur | no time event出力 |
|---|---|---|---|
| Wi-Fi無効（12s素アイドル）| 1260 | **3** | 3回（≒0.25/s）|
| Wi-Fi有効（~13s/run）| 1792 | **375** | ~34/s |

→ **no time event氾濫は完全にWi-Fi起因**（素ASP3では12秒で3回のみ）。
HRTドライバ固有のバグではない（test_porting/testexec通過と整合）。真因は
blobがxQueueReceive等をタイムアウト付きで多用→大半が完了→タイムアウト
キャンセル→HRT張り直し時に旧alarmが1回stale発火（追記7(B)の機構）。
**0 AP（PHY/RF/AGC）とは独立の別issue**。恒久対策は，systimer alarmの
再プログラム時に確実に旧発火を打ち消す手順（disable→set_target→
clear_int→enable 等）を要検討だが，Wi-Fi受信不能の本丸ではないため後回し。

副次：Wi-Fi無効でもHRT割込みは~105/s（1260/12s）だがスプリアスは3のみ＝
残りは正当（counter≥target）。素ASP3のHRTは正常動作。

---

## 追記10（2026-07-06）：ground truth JTAG差分でmodem LPCONクロック欠落を発見・修正（実バグだがRX単独要因ではない）。NuttXクローンで構造差を確認

追記8で0 AP＝PHY/RF/AGCと絞った後，native(受信OK)とASP3(0 AP)の
modem/PHY/PMUレジスタをJTAGで直接比較（ground truth diff）した。

### 手法
`tmp/c6_handoff_source`をLOADER_NO_WIFI無効化＋scan後ジャンプせず再scan
し続けるよう改造→native版が毎回20 AP検出する状態でJTAG halt→レジスタ読み。
ASP3(Direct Boot)版も同じレジスタを読み，diff。

### 発見した差分（native vs ASP3）
| レジスタ | native | ASP3(修正前) | |
|---|---|---|---|
| MODEM_LPCON_CLK_CONF 0x600af018 | 0x7 | **0x0** | WIFIPWR/COEX/I2C_MST clock |
| MODEM_LPCON_COEX_LP_CLK_CONF 0x600af008 | 0x314 | 0x0 | COEX LP clock |
| MODEM_LPCON +0x48 | 0x314 | 0x0 | LP clock |
| PMU_HP_ACTIVE_BACKUP 0x6009601c | 0x500 | 0x300 | retention |

その他（MODEM_SYSCON_CLK/RST/BB_CFG，PMU_ICG_MODEM，AGC_ENABLE等）は一致。

### 修正と結果
`esp_wifi_adapter.c`の`phy_enable_wrapper`（PHY較正の直前）と
`wifi_clock_enable_wrapper`で 0x600af018=0x7（+008/048=0x314）を明示設定。
→ JTAGで全レジスタがnativeと一致することを確認したが，**依然 0 AP・
MAC割込みソース(INTMTX STATUS0 bit0)は立たず**。
→ **modemクロック系レジスタはRXブロッカーではない**（LP/sleep系で，
アクティブscan時はfast clockで動くため）。LPCON=0はDirect Bootが
esp_perip_clk_init/bootloaderのクロック設定をスキップする実バグだが，
0 APの直接原因ではない。

### NuttXクローンによる構造差の確認
apache/nuttxをクローンし arch/risc-v/src/esp32c6/esp_wifi_adapter.c を確認：
- NuttXの`wifi_clock_enable_wrapper`は**`wifi_module_enable()`のみ**，
  `esp_phy_enable_wrapper`も`esp_phy_enable()`のみ＝特別なクロック処理なし。
- NuttXは2段ブートローダ＋OS起動の`esp_perip_clk_init`に依存している。
- C6の`bootloader_hardware_init`は実質I2Cマスタクロック有効化のみ
  （ASP3 shimが既に再現済み）＝**2段ブートローダに変えてもブートローダ
  部分は新規設定を追加しない**。効くのはOS起動時の`esp_perip_clk_init`
  （modem_clock_select_lp_clock_source）だが，これもLP clock系。

### 現在の到達点と残課題
- modem/SYSCON/LPCON/PMUの既知差分は全てnativeに一致させた。
- それでもRX不可＝**RXブロッカーは未diff領域**：WiFi MAC制御レジスタ，
  PHY/BB（baseband）レジスタ，またはregi2c越しのRF較正データ（BBPLL/
  TXRF/RXゲイン等）の可能性。
- 次手候補：(a) WiFi MAC/PHY BBレジスタ空間もnativeとJTAG diff，
  (b) regi2c較正結果（BBPLL lock/cal_end等，wifi_phyinit_snapが読む値）を
  native と比較，(c) RF PLL/チャネル設定レジスタの比較。

### 観測基盤（確立）
native再scanループ版（tmp/c6_handoff_source，一時改造）＋OpenOCD JTAGで
「受信できる版」のレジスタをいつでも読める。ASP3版と直接diff可能。

---

## 追記11（2026-07-06）：RXブロッカーをRFフロントエンド/regi2c較正まで局在化（AGC railed=受信していない症状）

追記10でmodemクロック系を全てnativeに一致させてもRX不可を確認後，
「no time event」ログをkernelで無効化（一時・要revert）してlogtask負荷の
影響を排除（→クリーン出力で0 AP変わらず＝ログは無害を確定）。その上で
本丸のPHY/BBレジスタをnativeとJTAG stable-diff（動的レジスタ除外のため
各版2サンプルで「版内安定かつ版間相違」のみ抽出）した。

### BB領域(0x600a7000-0x600a7fff)の安定差分
```
0x600a704c  native=01808080  asp3=01f5e1f5   AGCゲイン系
0x600a7428  native=0000d500  asp3=0000d400
0x600a78d0  native=84e34d08  asp3=a4e34d08
```

### 解釈：これは症状であって原因ではない
- 0x600a704c＝AGCゲイン系で asp3=0xf5（高ゲインに振り切れ）vs native=0x80（中間）。
- AGCは適応制御。**asp3は信号を受けていないためゲインを最大まで上げている**＝
  受信不能の**症状**。nativeは実信号でゲインが中間に落ち着く。
- → RXブロッカーはBB AGCより**上流のRFフロントエンド**（regi2cで較正される
  RFシンセサイザ/LNA/ミキサ/バイアス）がBBに信号を届けていない，に局在化。
- shimコメント（esp_wifi_adapter.c wifi_clock_enable_wrapper）の
  「regi2c越しの較正（BBPLL/TXRF/BIAS等）が無応答/不定値だとTX/RXとも
  電波が出ない」と一致。I2C_MSTクロック(LPCON bit2)は追記10で有効化したが，
  regi2c越しのRF較正結果自体が正しいかは未検証。

### 次の一手（RF analog層＝最深）
1. regi2c RF較正データの比較：RFシンセサイザPLL lock・チャネル周波数・
   RXゲインテーブルをesp_rom_regi2c_readでnative vs asp3比較（メモリ
   マップ外＝両版に読み出しコードが要る）。
2. wifi_phyinit_snapのregi2c読み（BBPLL lock/cal_end/cal_ovf）をnativeでも
   採取して比較。
3. RF較正の入力前提（XTAL周波数・PLL・電源）がDirect Boot文脈で
   nativeと一致しているか。

### 現状整理（RXブロッカーの局在化の到達点）
osi✗ / 割込み✗ / タイマHRT✗ / static init✗ / modemクロック✗（全て除外）
→ **RFフロントエンド/regi2c較正**（AGCより上流のアナログRX経路）が本丸。
深いRFアナログ層で，較正の正否をregi2cで直接比較するのが次段。

### 診断用の一時変更（要revert）
- `asp3/asp3_core/kernel/time_event.c`：no time eventログ1行をコメントアウト
  （submodule・kernel＝本来禁則．ユーザ許可の下で診断用．クリーン観測に有用）。
- `tmp/c6_handoff_source`：native版を再scanループ化（ground truth読み用）。
- `esp_wifi_adapter.c`：LPCON=0x7等の明示設定（追記10．LPCON修正は恒久価値あり）。

---

## 追記13（2026-07-06）：Zephyr soc_hw_init比較でmodem ICGを検証→既にnative一致＝modemクロック/電源/ICGドメインを完全に潰し切り

Zephyr(zephyrproject-rtos/hal_espressif)をクローンし，
`zephyr/esp32c6/src/soc_init.c`の`soc_hw_init()`＝「非ESP-IDF RTOSに必要な
最小modem初期化」を確認。ここにICG（Internal Clock Gating）設定列があった：
```c
_regi2c_ctrl_ll_master_enable_clock(true);              // ASP3実施済
pmu_ll_hp_set_icg_modem(PMU, HP_ACTIVE, code=2);
modem_syscon_ll_set_modem_apb_icg_bitmap(BIT(2));
modem_lpcon_ll_set_i2c_master_icg_bitmap(BIT(2));       // clk_conf_power_st.clk_i2c_mst_st_map
modem_lpcon_ll_set_lp_apb_icg_bitmap(BIT(2));
pmu_ll_imm_update_dig_icg_modem_code(true);
pmu_ll_imm_update_dig_icg_switch(true);
```
CLK_CONF(源の有効化)とは別に，ICG bitmapが「HP_ACTIVE電源状態でクロックを
流すか」を決める＝有力な候補だった。ASP3のhalに全LL関数が存在したので移植
して実機テスト。

### 結果：ICGは既にnative一致＝差分ではない
- JTAG実測：MODEM_LPCON `clk_conf_power_st`(0x600af020)=**0x66660000**
  （全st_map=6）＝**私のICG init前から**native一致（追記10のLPCON diffに
  0x600af020は出ていなかった＝一致していた）。modem_clock_module_enableが
  既に設定している。私のICG init(BIT(2)=4を書く)はwifi_module_enableに
  上書きされ冗長・無効と判明→revert。
- APは依然0，MAC非アサート。

### 3参照実装での結論
native ESP-IDF（受信OK・ソース有）／NuttX（クローン・adapter単純）／
Zephyr（クローン・soc_hw_init明示）の全てと比較した結果，
**modemクロック/電源/ICGドメインはASP3で完全にnative一致 or RX無関係**と
潰し切った。実バグはLPCON_CLK_CONF欠落のみ（追記10で修正・恒久価値あり）
だがRX単独要因ではない。

### 残る唯一のフロンティア：RF/regi2c較正層
- RXブロッカーはBB AGCより上流のRFアナログ（regi2c較正のシンセ/LNA/
  ミキサ/バイアス）に確定的に局在（追記11のAGC railed症状＋追記13の
  modem全潰し）。
- ASP3のregi2c読み戻しは成功（block 0x6b/0x66/0x6d等に実値＝RFは較正
  されている）。nativeとの読み戻し比較は phy funsテーブルのアドレスが
  ビルド依存（native=g_phyFuns 0x4081d188）で未完＝要ツール修正。
- ここはundocumentedなRFアナログ層で，1差分見つけても意味特定にRF
  ドメイン知識が要る最深部。

### 到達点（消去法・完全版）
osi✗ 割込み✗ タイマ/HRT✗ static init✗ modemクロック✗ 電源/ICG✗
→ **RF/regi2c較正のみが残る本丸**。観測基盤（native再scanループ＋JTAG
stable-diff＋regi2c読み戻し）とZephyr/NuttXソースは次セッションに整備済。

---

## 追記14（2026-07-06）：Zephyr/phy_init.cレビュー＝ASP3は PHY_RF_CAL_FULL（較正モードは正しい）

Zephyr(hal_espressif)がリンクするのと同じ`esp_phy/src/phy_init.c`を
レビュー。`esp_phy_load_cal_and_init()`の較正モード分岐：
- `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`定義時：NVSからcalデータを
  ロード（失敗→FULL，成功→PARTIAL）
- **未定義時（#else）：`register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL)`**

ASP3ビルドは`esp_wifi.cmake`/`esp_shim_blobglue.c`のコメント通り
`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`を**定義しない**→#else経路＝
**PHY_RF_CAL_FULL（完全較正）で register_chipv7_phy を呼ぶ**。

→ 「NVSがゴミcalデータで成功を返しPARTIAL較正になる」説は否定。ASP3は
毎回FULL較正しており較正モードは正しい。したがって問題は較正**モード**では
なく，FULL較正が**実行される環境（アナログbias/クロック等の前提条件）**が
Direct Boot文脈でnativeと異なり，較正結果（RFレジスタ値）が不正になる，
という追記11-13のRFフロントエンド局在化と整合。cal_dataはFULLモードでは
入力無視なのでNVSスタブの挙動も無関係。

### 次セッションの焦点（確定）
FULL較正の入力前提（RFアナログbias・LDO・XTAL・PLL等）をnativeとJTAGで
比較。ASP3のregi2c読み戻し(block 0x6b/0x66/0x6d実値)をnativeと比較する
ためのツール（phy funsテーブルのビルド依存アドレス＝native g_phyFuns
0x4081d188）修正が入口。
