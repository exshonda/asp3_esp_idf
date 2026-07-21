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
export IDF_TOOLS_PATH=$HOME/tools/espressif
source $HOME/tools/esp-idf-v6.1/export.sh
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
OCD=$HOME/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32
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

---

## 追記15（2026-07-06）：Codex(GPT-5)相談＋PMU/アナログ電源のnative diff＝Codex最有力仮説(PMU DBIAS/regulator)はrefuted

Codex CLI(codex:codex-rescueプラグイン, GPT-5系)にRF較正前提を相談。Codexは
GitHub経由でESP-IDF/Zephyr/NuttXの実ソースをfile:line付きで照合し、
「Direct BootがPMU regulator/DBIAS/analog power stateを未初期化(rtc_clk_init/
pmu_initを飛ばす)」を最有力(確信度#1)とした。これをJTAGで検証：

### PMU/アナログ電源レジスタ native vs ASP3（scan中halt）
| reg | native | asp3 | |
|---|---|---|---|
| 0x600B0014 HP_ACTIVE_HP_CK_POWER | 70000000 | 70000000 | 一致 |
| 0x600B0018 HP_ACTIVE_BIAS | 02000000 | 02000000 | 一致 |
| 0x600B0028 HP_ACTIVE_HP_REGULATOR0 | d0047180 | d0047180 | 一致 |
| 0x600B0048 HP_MODEM_HP_CK_POWER | 70000000 | 70000000 | 一致 |
| 0x600B004c HP_MODEM_BIAS | 00000000 | 00000000 | 一致 |
| 0x600B005c HP_MODEM_HP_REGULATOR0 | d0040000 | d0040000 | 一致 |
| 0x600AF818 ANA_CONF0(bit24 BBPLL_CAL_DONE) | 2900e444 | 2900e444 | 一致 |

→ **Codex#1(PMU DBIAS/regulator/電源)はrefuted＝完全一致**。PMU電源状態は
本丸ではない。

### 差分は I2C_ANA_MST(regi2cバス)のみ（ただし動的/undocumented）
- 0x600af81c ANA_CONF1: native=fffff7 vs asp3=fffbff。ROMパッチ
  (esp_rom_regi2c)よりANA_CONF1は**regi2c読み出しごとに書くデバイス選択
  マスク(~BIT(n))＝動的**。asp3=~BIT(10)=DIG_REG(0x6d)を最後に読んだ状態，
  native=~BIT(3)＝別ブロック。**レッドヘリング**。
- 0x600af820 ANA_CONF2: native=001f04 vs asp3=01ff04。_MST_SEL(bit8-12=
  BIAS/BBPLL/ULP/SAR/DIG_REG)は**両方とも全セット＝一致**。差はbit13-16
  (undocumented)でasp3=set/native=clear。意味不明・要調査。

### 現状（さらに絞り込み）
PMU電源/DBIAS/regulator も除外。残る本丸候補：regi2cの**較正VALUE自体**
(block 0x6b/0x66/0x6d の内容)がnativeと違うか。Codexが提示したnative側
読み戻し法＝native ELFの g_phyFuns シンボル(info address g_phyFuns)を
JTAGで解決して[23]=read_maskを呼ぶ、または regi2c_ctrl_read_reg_mask()
(0x6bはswitch非対応なのでg_phyFuns経由)。ANA_CONF2 bit13-16の意味も要確認。

### Codex相談の運用メモ
- Codexサンドボックス(bwrap)はloopback初期化失敗でローカルシェル不可＝
  github.fetch_fileでpush済みGitHubファイルのみ読める。→ 診断コミットは
  push必須(eb4d547をpush済み)。参照実装はGitHub repo+path(apache/nuttx,
  zephyrproject-rtos/hal_espressif 等)で渡す。自己完結ブリーフィングが安全。
- codex CLIは ~/.npm-global/bin(sudo無し)＋~/.local/bin にsymlink，~/.bashrc
  にPATH追加済み。認証はChatGPTログイン(exshinya.honda@gmail.com)。

---

## 追記16（2026-07-06）：regi2c較正VALUEのnative比較＝BBPLL(0x66)/DIG_REG(0x6d)は完全一致。残る未比較はRFシンセ(0x6b)のみ

Codex再相談（Q1回答＝0x4087f954はg_phyFunsが指すテーブル実体でビルド固有）を
踏まえ、native側regi2c読み戻しツールを段階的に修正して較正VALUE比較を実施。

### native側読み戻しの試行錯誤（ツール知見）
1. g_phyFunsシンボル経由（extern uint32_t *g_phyFuns→[23]）→全0x77。
   nativeのESP-IDFはROMのSRAM最上部領域をheap回収するため、および/または
   libphyバージョン差（IDF v6.1 vs esp-hal-3rdparty）でテーブルidx23が
   read_maskでない可能性。
2. ROM固定関数 rom_chip_i2c_readReg_org=0x400012b4（esp32c6.rom.phy.ldの
   PROVIDE。Codexの「固定ROMエントリ無し」は rom.phy.ld を見落とし）→全0x77。
   IDFはregi2cバスをトランザクション毎にrefcountでenable/disableするため
   素の呼出しはクロック窓外。
3. **IDF正規経路 regi2c_ctrl_read_reg_mask（native ELFにリンク済み、
   enable/クリティカルセクション込み）→ 0x66/0x6a/0x6d が読めた**。
   0x6bはenable_block switchに無く全0xff（Codex警告通り）。
4. 0x6b手動読み（ANA_CONF1=~BIT(3)推定＋ROM readReg）→全0x77＝失敗。
   RFブロックのデバイスenableはblob管理でANA_CONF1 bit3仮説だけでは不足。

### 較正VALUE比較結果（scan完了後halt、reg0-15）
| block | native | asp3 | 判定 |
|---|---|---|---|
| 0x66 BBPLL | 08502518 0073806b 000396c2 00000000 | 同一 | ✅完全一致 |
| 0x6d DIG_REG | 048004f0 98989898 04000f1f 00804216 | 同一 | ✅完全一致 |
| 0x6a BIAS | reg2-15一致（reg0,1はnative読みアーチファクト0x77） | | ほぼ一致 |
| **0x6b RFシンセ** | **native読取不能（3手法とも）** | 3b510200 b78802c4 39a80081 821e2680 | **未比較＝残る本丸** |

### 副産物
- nativeのANA_CONF1アイドル値~BIT(3)＝**RFデバイスの選択ビットはbit3**
  （の可能性）。ASP3の~BIT(10)は自分の読み戻しループが最後に0x6d=BIT(10)を
  読んだ痕跡＝追記15のANA_CONF1差分はレッドヘリング確定。
- ANA_CONF2 bit13-16差分：bit16=ANA_I2C_SAR_FORCE_PU(regi2c_defs.h)、
  bit13-15はundocumented。RF MST選択ではなく低優先度（Codex Q2）。

### 到達点
較正値も BBPLL/DIG_REG/BIAS(ほぼ) まで**nativeと一致**。残る未比較は
**RFシンセ(0x6b)の較正値のみ**。これが違えばRF較正結果が本丸確定、
同じなら「PHY init後のRX enable/MAC filter/coex/antenna selectの動的状態」
へ（Codex Q3の分岐）。

### 次の一手（native側0x6bを読む）
nativeのg_phyFunsテーブル(*(0x4081d188))とASP3のテーブル(0x4087f954)を
JTAGで両方dumpし、関数ポインタパターン照合でnative側のread_mask相当
エントリindexを特定する（libphyバージョン差でidxが違う仮説の検証）。
または blobが0x6bを読む瞬間のANA_CONF1/2設定をJTAGウォッチで捕捉。

---

## 追記17（2026-07-06）：phyFunsテーブル照合＝idx23は両ビルド同一ROM関数(0x4000412c)。0x6bのnative読取は4手法とも失敗＝blob管理のRF i2cアクセスの解明が次の鍵

### テーブル照合結果（native/ASP3とも g_phyFuns→0x4087f954＝同一アドレス）
- **idx23(read_mask)=0x4000412c＝両ビルド完全同一のROM関数**。native失敗の
  原因は関数ではなく呼び出し時のバス/デバイスenable状態。
- ASP3がパッチした箇所: idx22(0x420031be)/24/26＝wifi_trace.cの
  traced_write系（自前パッチ・想定通り）。
- **nativeがパッチした箇所: idx12(0x420314bc flash・ASP3はROM 0x400059e6)、
  idx13/14(0x4080221e/2c RAM=IRAM)**＝IDFがROM PHY関数をバグフィックス
  パッチしている。ASP3は素のROM版を使用＝**このROMパッチ差が別の火種の
  可能性（要調査：idx12/13/14が何の関数か）**。
- その他の差分はlibphyビルド差（IDF v6.1 vs esp-hal-3rdparty）の
  flash/RAMアドレス違いで正常。

### 0x6bのnative読取（4手法とも0x77＝失敗）
1. g_phyFunsテーブルidx23素呼び → 0x77
2. ROM固定 rom_chip_i2c_readReg_org(0x400012b4) → 0x77
3. 2 + ANA_CONF1=~BIT(3)手動 → 0x77
4. idx23 + ANA_CONF2 |=0x01e000（ASP3同等のbit13-16）→ 0x77
→ ANA_CONF1/CONF2仮説では説明できず。**RFブロック(0x6b, host_id=1)は
   HPのI2C_ANA_MST(0x600AF800)ではなく別経路（LP_I2C_ANA_MST=0x600B2400？
   host_idがHP/LPマスタ選択？）の可能性**。ASP3では常時アクセス可・
   nativeではIDF/blobが管理しCPUの素アクセスを閉じている，と整合する仮説。

### 次セッションの具体手順（0x6b比較の決着）
1. **JTAGウォッチポイント**：native scan中に 0x600AF81C/820（または
   0x600B24xx）への書込みをwatchし、blob自身がRFブロックを読む瞬間の
   enable列を捕捉→再現。
2. ROM 0x4000412c（read_mask）を逆アセンブルし、block/host_idの分岐で
   どのマスタ（HP/LP ana i2c）へ行くか特定。
3. LPPERI/LP_I2C_ANA_MSTのクロックenable状態を native vs ASP3 で比較。
4. 代替：ASP3側0x6b値の妥当性検証（2回読んで安定性、scan前後で変化）．

### セッション总括（本丸の現在地）
- 較正VALUE：BBPLL(0x66)/DIG_REG(0x6d)完全一致・BIAS(0x6a)ほぼ一致。
- **未比較はRFシンセ(0x6b)のみ**（ASP3側は取得済: 3b510200 b78802c4
  39a80081 821e2680）。
- 新しい容疑（テーブル照合の副産物）：**IDFのROM PHY関数パッチ
  （idx12/13/14）をASP3が持っていない**＝素のROM版のバグを踏んでいる
  可能性。0x6b比較と並行して要調査。

---

## 追記18（2026-07-06）★決定的★：RFシンセ(0x6b)に安定した較正/設定差5レジスタを発見（生JTAGトランザクション法を確立）

### 手法の確立（ROM逆アセンブルから）
- read_maskの呼び出し鎖はROMで両ビルド完全同一（idx23→idx20→idx19→フック
  idx13/15/17/18）。0x77失敗の原因は**フック実装差**（native=IDF IRAMパッチ、
  ASP3=libphy実装）＝アプリ文脈からの読みはフック文脈に依存する。
- ROM idx21(writeReg)の逆アセンブルからHWインタフェース特定：
  **I2C_ANA_MST I2Cn_CTRL(0x600AF800+host*4)へ cmd=(reg<<8)|block を書き，
  bit25(busy)待ち，データはCTRL[23:16]に返る**。
- → **OpenOCDのmww/mdw/read_memoryだけで生regi2cトランザクションを駆動**
  できる（フック・アプリ・並行性と無関係）。TCLスクリプト
  ($CLAUDE_JOB_DIR/tmp/read6b.tcl)化。
- 検証：native生読みreg2=0x31はblob自身の直前トランザクション残値と一致。
  ASP3のscan中生読みはblob読み(read_mask)と一致（reg2のみ51/52揺れ）。
- 制約：**RFデバイスはscan中のみ応答**（scan後はパワーダウン/非選択で全0xff）
  →比較は両方scan中にhaltして行う。

### チャネル変動の除外（native3回スナップショット＋ASP3 raw/blob 2点）
reg7はnative内で変動(b5→b8)＝チャネル依存として除外。他は全て安定。

### ★安定差分（RFシンセ block 0x6b・チャネル非依存）★
| reg | native(RX OK) | ASP3(0 AP) | ビット差 |
|---|---|---|---|
| 2 | 0x31 | 0x51 | native bit5 ↔ asp3 bit6 |
| 4 | 0xa4 | 0xc4 | native bit5 ↔ asp3 bit6（同パターン） |
| 11 | 0x29 | 0x39 | asp3 +bit4 |
| 13 | 0x06 | 0x26 | asp3 +bit5 |
| 14 | 0x3f | 0x1e | bit0/bit5 反転 |
他11レジスタは完全一致。

### 解釈と注意
- **「RF較正層の差」の実体を初めて具体値で捕捉**。シンセ設定はRX LO/VCO/
  分周に直結＝受信不能の直接原因である可能性が高い。
- ただし交絡因子：**libphyのバージョンが違う**（native=IDF v6.1、ASP3=
  esp-hal-3rdparty）。差が「較正の失敗」ではなく「libphy版の設計差」の
  可能性も残る。テーブル照合で判明した**IDFのROM PHY関数パッチ
  (idx12/13/14)をASP3が持たない**事実と併せ、次の焦点：
  1. reg2/4のbit5↔bit6・reg11/13/14の意味（undocumented．ROM/libphy逆アセ
     ンブルか、nativeの該当regへのwrite箇所をwifi_regi2cトレースで捕捉）
  2. ASP3側でこれら5レジスタを**nativeの値に生JTAGで上書きしてscan**
     （受信できればRFシンセ設定が根因と確定＝最短の因果検証）
  3. phy_versionの比較（get_phy_version_str）とlibphy版差の切り分け

### 次セッションの最短手順（因果検証）
ASP3をboot→scan中(リセット後~2.5s)にhalt→生JTAGで
reg2=0x31/reg4=0xa4/reg11=0x29/reg13=0x06/reg14=0x3f を書き込み
（cmd=0x05000000|(data<<16)|(reg<<8)|0x6b をI2C1_CTRLへ）→resume→
次scanでAPが出るか。※scanは2秒で終わるためASP3側をループscanに改造
してから行うのが現実的。

---

## 追記19（2026-07-06）：因果検証3連発＝(a)RFシンセ4reg上書き→効果なし・reg14はRO status，(b)FULL較正は実行されていた（RTC計測で実証），(c)blob世代差が最重要容疑に＝IDF v6.1 blob差し替えはABI非互換でesp_wifi_init失敗→hal submodule bumpが本命

### (a) RFシンセ因果検証（追記18の5レジスタ）
ASP3を再scanループ化し，scan中にJTAG生トランザクションで
reg2=0x31/reg4=0xa4/reg11=0x29/reg13=0x06 をnative値へ上書き
（write cmd=0x05000000|(data<<16)|(reg<<8)|0x6b，VERIFYで書込み成功・
数十scan跨ぎで永続も確認）。**reg14(0x3f書込み)は値が変わらず＝リード
オンリーのステータスレジスタ**（native=0x3f vs asp3=0x1e は「状態」の
反映＝症状であって原因ではない可能性）。
→ **4reg一致でもRESCAN 0 APsのまま**＝RFシンセ設定regは根因ではない。
BB regs(0x600a7428/78d0)のnative合わせも効果なし（78d0は既に一致して
いた＝状態依存）。

### (b) 「FULL較正が走っていない」仮説の実測却下（重要な教訓つき）
phy_versionログがASP3に無い＋wrapカウンタ全ゼロから「esp_phy_load_cal_
and_init未実行」を疑ったが，RTC-RAM直接カウンタ（nm/ログ非依存）で実測：
```
phy_enable_wrapper入場=1 / esp_phy_enable復帰マーカ=OK /
register_chipv7_phy入場=1 / ret=1(ESP_CAL_DATA_CHECK_FAIL=FULL calの正常値)
```
→ **FULL較正はASP3でも実行されている**。phy_versionログ欠落はシムの
ログレベルフィルタ，カウンタ全ゼロは古いnmアドレスの読み誤り。
（教訓：nm由来アドレスはビルド毎に変わる．RTC-RAM固定番地計測が確実）

### (c) blob世代差の因果検証＝ABI非互換で不成立→hal bumpが本命
- **全blob(.a)がmd5相違**：libphy/libpp/libnet80211/libcoexist/libcore
  とも esp-hal-3rdparty(ASP3) と IDF v6.1(native) で別物。
- esp_wifi.cmakeに `ASP3_WIFI_BLOB_IDF` 変数を追加しIDF v6.1のlibへ
  リンク差し替え実験。シンボル差2件（esp_wifi_skip_supp_pmkcaching／
  printf）はweakシムで解消しリンク成功。
- 実行結果：**esp_wifi_initが即失敗**「E wifi_init: Failed to deinit
  Wi-Fi (0x30xx)」＝osiアダプタ/glue層（hal側でコンパイルする
  wifi_init.c等）と新blobのABI不一致。blobだけの差し替えは不可。
- → **正攻法＝esp-hal-3rdparty submoduleを新blob世代へbump**
  （glue含め整合が取れる）。C6の受信不能はblob版（旧libphy/libppの
  C6実機での不具合 or 要ROMパッチ）に起因する可能性が最有力に。
  テーブル照合で判明した「IDFはROM PHY関数(idx12/13/14)をパッチ・
  ASP3は素のROM」もこの文脈で整合。

### 本日の最終結論（Step0全体）
1. SWD-key修正でC6 Wi-Fiはinit/start/scan完走まで到達（恒久fix）
2. MODEM_LPCONクロック欠落を修正（恒久fix）
3. 割込み/タイマ/クロック/電源/ICG/PHY較正実行/較正値(BBPLL/DIG_REG/
   BIAS/RFシンセ4reg)を**全てnative一致まで潰した**が受信せず
4. 残る差＝**blob世代**（全lib別物・ROMパッチ有無・RO status reg14）
5. **次の一手＝esp-hal-3rdparty submoduleのbump**（新blob世代へ）。
   それでもRX不能なら，残るは「旧blobでは受信不能」という結論に近い。

### 観測資産（今日確立・再利用可）
- 生JTAG regi2cトランザクション（read/write）：read6b.tcl/write6b.tcl
- RTC-RAM固定番地計測（0x50000080-8C：phy_enable/register_chipv7_phy）
- ASP3再scanループ（wifi_scan.c RESCAN）／native再scanループ
- ABI差シム（weak esp_wifi_skip_supp_pmkcaching/printf）と
  ASP3_WIFI_BLOB_IDF cmake変数（将来のblob実験用）

---

## 追記20（2026-07-06）★方向修正★：blob世代仮説は否定＝NuttXは同一halコミット・同一ボードで6 AP検出済み。RXブロッカーはASP3のshim/glue内に確定

ユーザ指摘「NuttXやZephyrは旧blobで動いている？」の検証結果：

### 事実
- **NuttX(master)のesp-hal-3rdpartyピン = b90b1837cb5ad24747deb4c895246037cc206ce5
  ＝ASP3のhal submoduleと完全同一コミット**（ASP3のhalブランチ名は
  sync/master.c-nuttx-20260428＝NuttX向けsyncそのもの）。
- libphy版：ASP3/NuttX世代=Nov 14 2025ビルド，IDF v6.1=Apr 10 2026
  （phy_version 344）＝約5ヶ月差はあるが…
- **docs/wifi-shim-c6.md:737（過去セッション実機記録）：「NuttXは同じ
  ボードで実際に6件のAPを検出」**（<SSID-2G>含む）。

### 結論
**同一blob・同一hal・同一ボードでNuttXは受信できる**→
- 追記19(c)の「blob世代が本命／hal bumpが次の一手」は**否定**。
- RXブロッカーは**ASP3のshim/glue/使い方**に確定的に局在。
- native-IDF比較でHW状態がほぼ全一致だった事実とも整合（HWは正しく
  セットアップされている．違いはblobを「使う側」の何か）。

### 次セッションの焦点（NuttX vs ASP3の行単位比較）
1. NuttX esp_wifi_adapter.c（クローン済）とASP3 esp_wifi_adapter.c/
   esp_shim.c の**未実装/挙動差のあるosiエントリ**の総当たり比較。
   特にRX経路に効くもの：queue/semaphoreのISRバリアント，
   task_yield_from_isr，event_group系，get_time/timestamp系，
   rand/random，read_mac，nvs系のエラー値，coex系のデフォルト値。
2. 過去実施の未決着項目の再訪：NuttXは`esp_wifi_scan_start()`に
   常に非NULLの明示config（scan_type=ACTIVE等）を渡す
   （wifi-shim-c6.md:1948）—ASP3はNULL。実施でも触れたが決着したか？
3. ビルド構成差：NuttXのesp_wifi関連CONFIG（sdkconfig相当）と
   ASP3のesp_wifi.cmakeのdefine差（CONFIG_ESP_WIFI_*）。
   特にRXバッファ設定（static_rx_buf_num等のwifi_init_config）。
4. Zephyr blobは別系（hal_espressifが独自lib同梱）＝参考程度。

### 教訓
「動く参照」が複数あるとき，どの参照と何が同じ/違うか（blob・hal・
board・glue）のマトリクスを最初に固定すべきだった。native-IDFは
blob版が違う参照，NuttXは同一blobの参照＝**NuttXこそ本命の比較対象**。

---

## 追記21（2026-07-06）★ブレークスルー★：MAC/WDEVレジスタ移植でMAC割込みが発火開始（11固定→170/秒）。RXブロッカーはMAC空間の未設定レジスタと確定

ユーザ提案「動作するバイナリでwifi初期化した後にASP3を動かす」の
実行可能形＝**レジスタ移植**（ソフトhandoffはstale-pointerで不可能
[追記2]のため，native動作中のHW状態をJTAGでdump→ASP3実行中に書き込み）。

### MAC/WDEV空間の特定と差分
- libppのlui命令頻度解析でblobが叩くMMIO＝**0x600A4xxx/0x600A5xxx/
  0x600A6xxx(WiFi MAC/WDEV)/0x600ADxxx**と特定（未文書化・未探索だった）。
- stable-diff（native/ASP3各2スナップショット・scan中）で**42個の安定差分**。
  目を引くもの：
  - 0x600a4300: native=ffffffff / asp3=0（enable/filterマスク全closed？）
  - 0x600a4318: 3ff/0，0x600a42fc: 70/0，0x600a433c: 5/0
  - 0x600a4408..442c: 12エントリのテーブル（asp3は中バイト全0）
  - 0x600a4430/4434/4438，0x600a4dd0/4dd4（native=55777555等/asp3=0）
  - 0x600ad070/470: 77ef vs 3f7cd

### 移植結果（42個全てをASP3実行中にnative値へ書込み）
- **esp_shim_int_count[1]（MAC線ディスパッチ）が11固定→561→913→1265と
  約170/秒で増加開始**＝MAC割込みが実際に発火し始めた（ビーコン数百/s
  と整合するレート）。
- ただし**RESCANは依然0 APs**＝割込みは届くがフレーム→scan結果の
  データパス（DMA/sta_rx_cb/結果蓄積）が未達，または別種割込みの可能性。

### 意味
- **「ASP3のglue/blobが設定しない（できない）MACレジスタが存在し，
  それがRXブロッカー」がほぼ確定**。NuttX同一blobで動く事実（追記20）
  とも整合＝NuttXでは何かがこれらを設定する。
- 次の決定打：**42個の二分探索**で効いたレジスタを特定→そのレジスタを
  設定するはずの経路（blob内部のwdev init？glueのosi？NuttXのみが
  提供する何か？）を逆引き。

### 次セッションの手順
1. 42個を半分ずつ書き込み→int_count[1]の増加有無で二分探索
   （TCL: $CLAUDE_JOB_DIR/tmp/poke_mac.tcl を分割）。
2. 効いたレジスタ特定後：全42移植状態でsta_rx_cb/promisc_rx_countも
   確認（フレームがデータパスに乗るか）。0 APが続く場合は残りの
   カスケード（DMAディスクリプタ等RAM側状態）も疑う。
3. 特定レジスタのアドレスからNuttX/blobの設定経路を逆引き
   （libpp逆アセンブル・NuttXソース）。

---

## 追記22（2026-07-06）★★根源特定★★：MAC RX割込みゲート＝レジスタ0x600a4dd8のbit0 ただ1ビット。ASP3=0x70 / native=0x71

追記21の42レジスタを二分探索（各試行前にhard-resetで素状態へ戻し、
poke後にesp_shim_int_count[1]の増分でレート判定．全自動化＝
$CLAUDE_JOB_DIR/tmp/bisect.py）した結果：

### 二分探索の収束（全て再現性あり）
- 全42 → 153/s（baseline 4.4/s）
- [0-20]無効 / [21-41]有効
- [21-31]無効 / [32-41]有効
- [32-36]有効 / [37-41]無効
- [32,33]有効 / [34,35,36]無効
- **[32]単独＝141/s！ [33]単独＝無効**

### ★根源＝0x600a4dd8 bit0★
- reg32 = **0x600a4dd8**：native=**0x71** / ASP3=**0x70**（差はbit0のみ）。
- ASP3実行中に 0x600a4dd8 へ 0x71 を書く（bit0を立てる）だけで、
  MAC割込み(esp_shim_int_count[1])が11固定→141/秒で発火開始。
- **これがMAC RX割込みのイネーブルゲート**。5ヶ月来「MACが一度も割込みを
  上げない」の直接原因＝この1ビットがASP3で立っていない。

### ただしAPはまだ0（データパスは下流にもう1段）
- bit0を立ててMAC割込みが141/s来ても RESCAN は 0 APs のまま。
- 全42移植でも0 AP（追記21）＝割込みゲートより下流（sta_rx_cb→フレーム
  パース→scan結果蓄積、またはRX DMAディスクリプタ等RAM側状態）に
  もう一つブロッカーがある。ただし**最大の壁（割込み不発）は解けた**。

### 次の焦点
1. **0x600a4dd8 bit0 を「立てるべき」経路の特定**：libpp/libnet80211を
   逆アセンブルしこのアドレスへのstore箇所を探す→どのblob関数が設定するか
   →ASP3のglue/osiがその関数を呼ぶ経路が欠落/失敗していないか。
   NuttX(同一blob・受信可)では立っている＝呼ばれている。
2. データパス下流：全42移植状態で sta_rx_cb(id=8)/promisc_rx_count/
   wifi_set_rx_policy(id=13) のトレースを見て、フレームが上がるか。
3. **最短の実用検証**：bit0を立てる＋残りの効くレジスタ（下流）を
   二分探索で追加特定し、"RESCAN N APs" が出る最小セットを求める。

### ツール追加
- `tmp/c6_jtag_tools/bisect.py`：poke/rate測定の自動化（要 diffs.json）。

---

## 追記23（2026-07-06）：0x600a4dd8 の正体＝COEX PTIレジスタ。下位4bit=WiFi default PTI(coex優先度)がnative=1/ASP3=0。根本＝coex優先度未設定でRXスロット不許可

libpp逆アセンブルで 0x600a4dd8 を触る2関数を特定：
- `hal_coex_pti_init`: `ori a4,a4,32`＝**bit5(0x20)をセット**（native/ASP3とも
  立っている＝これは呼ばれている）。
- `hal_set_wifi_default_pti(a0)`: `andi a5,a5,-16; or a5,a5,(a0&15)`＝
  **下位4bit(PTI値)を引数に設定**するRMW。

native=0x71（PTI=**1**）/ ASP3=0x70（PTI=**0**）。bit4/5/6(0x70)は両方一致。
差は**下位nibble(WiFi default PTI＝coex調停の優先度)だけ**：native=1・ASP3=0。

### 解釈（有力）
COEX（WiFi/BT/802.15.4共存）の調停で**WiFiの優先度(PTI)が0だと無線
RXスロットが許可されず、MAC RX割込みが一切上がらない**。PTIを1にすると
（=native同等）141/秒でMAC割込みが発火＝ビーコン受信レートと整合。
→ 5ヶ月来の「MAC無割込み」の根本は**coex優先度PTI=0**。

`hal_coex_pti_init`(bit5)は呼ばれているのに `hal_set_wifi_default_pti(1)`
相当が効いていない＝ASP3のcoex初期化/enableのどこかでdefault PTI設定が
抜けている。ASP3は`._coex_init=coex_init`/`._coex_enable=coex_enable`と
**本物のlibcoexist関数**を登録している（esp_coex_adapter.cのadapter登録も
NuttXテンプレ）にもかかわらず。

### まだ0 AP（データパスは別の下流ブロッカー）
PTI=1でMAC割込み141/sでもRESCANは0 AP。全42移植でも0 AP。＝割込みゲート
（coex PTI）とは別に、フレーム→scan結果のデータパスにもう1段ある
（sta_rx_cb/RX DMA/scanテーブル、または42差分の範囲外＝RAM状態/別レジスタ）。

### 次の焦点（2系統）
A. **PTI=0の根本修正**：coex_init→coex_enable→(WiFi request/schm)→
   hal_set_wifi_default_pti の連鎖のどこでASP3が止まるか。coex_enableが
   呼ばれているか、coexのschm(スケジューラ)タスクが回っているか、
   single-mode(WiFiのみ)のcoex configがNuttXと違うか。NuttX(同一blob・
   受信可)では立っている＝連鎖が完走している。
B. **データパス下流**：reg32以外で"APs found"に効くレジスタ/状態の特定
   （全42でも0 APなので範囲外の可能性大＝RX DMAディスクリプタ等RAM側）。

---

## 追記24（2026-07-06）：PTI nibbleを叩くだけでは「割込みは開くがsta_rx_cb=0・発火過多(~1400/s)」＝真の修正はcoex初期化の完走

reg32(0x600a4dd8 PTI nibble)をpokeした状態でRTC計測：
- int_count[1]（MAC割込み）＝大量発火（7158/5秒≈1400/s）
- **sta_rx_cb（RXフレーム→ドライバ）＝0（一度も呼ばれない）**
- 発火レート~1400/sはビーコン受信量(~200/s)を大幅超過

→ **PTI nibbleを手で立てるのは不完全/不正な代替**。割込みは開くが、
coexの調停状態が正しくセットされていないため、正常なフレーム受信に
ならず割込みだけが過剰発火（RXエラー/スプリアス）。sta_rx_cbに届かない。

### 修正の方向（確定）
**単一レジスタpokeは対症療法にすぎない。根本＝ASP3のcoex初期化が
完走していない**こと。完走すればPTI=1＋残りの調停状態が一括で正しく
設定される。NuttX(同一blob・受信可)ではcoex初期化が完走している。

### 調査すべきcoex連鎖
esp_wifi_start → (osi)_coex_enable=coex_enable → coex schmタスク →
coex_wifi_request(SCAN) → PTI設定。ASP3のcoexアダプタ
(esp_coex_adapter.c/esp_wifi_adapter.cのcoex系osi)に**NULL/スタブ/
誤った戻り値**があると連鎖が途中で止まる。特に：
- coex schmのタイマ/タスクが回っているか（coex_schm_process_restart
  id=41がトレースに出るか）
- coex_wifi_request/coex_wifi_release の戻り値
- spin_lock/semphr等coexが使うosiプリミティブのISR文脈での挙動
- single-mode(WiFiのみ)時のcoex config（sw_coex有効か）

### 到達点（本日の総括）
**MAC RX割込み不発の根本＝coex優先度PTI未設定**まで特定（0x600a4dd8
下位nibble）。ただし手動pokeは不完全＝coex初期化完走が本当の修正。
これでStep0の「なぜ受信しないか」は**coex初期化の不完全性**に確定的に
帰着した。次セッション＝ASP3 vs NuttXのcoexアダプタ/初期化順の
行単位比較（NuttXクローン=scratchpad）。

---

## 追記25（2026-07-06）★★根本原因確定★★：ASP3がcoexist_funcsをno-opテーブルに差し替えているのがC6 RX不能の根本。ただし単純除去はcoex_init未完でクラッシュ

追記23-24でPTIゲートまで来た後、esp_coex_adapter.cを精査して根本を発見：

### 根本原因（esp_shim_coex_adapter_register / esp_coex_adapter.c）
```c
for (i=0;i<48;i++) dummy_coexist_table[i] = (void*)coex_noop;
coexist_funcs = dummy_coexist_table;   // ← 全coex関数をno-op化
```
「WiFi単独＝coexist非アクティブ」の判断でROMのcoexist_funcsを48個全て
no-opに差し替えている。**これでblobが低レベルでcoexist_funcs経由で行う
coex調停（0x600a4dd8のWiFi PTI設定＝hal_set_wifi_default_pti含む）が
全てno-op化→PTI=0→coexがWiFiにRXスロットを許可しない→MAC RX割込み
不発→0 AP**。5ヶ月来の症状の根本。

### 検証（決定的）
- no-op差し替えを`#if 0`で無効化してビルド→実機 **Illegal instruction
  即クラッシュ**（PTIは0x70のまま＝到達前）。＝**coexist_funcsのNULL/
  未設定メソッド**を踏む。no-opは元々このクラッシュ回避策だった。
- つまり `coex_init()`（ASP3は本物のlibcoexist関数を_coex_initに登録）が
  **ASP3文脈で coexist_funcs を完全に設定していない**。NuttX(同一blob・
  受信可)はno-opせず本物のcoexが動く＝coex_initが完走しcoexist_funcsが
  正しく設定される。

### 正しい修正の方向
「no-opを外す」だけでは不可（クラッシュ）。**coex_init()がcoexist_funcsを
正しく設定する状態を作る**必要がある。候補：
1. coex初期化順序：esp_shim_coex_adapter_register()でのno-op上書きが
   coex_init()の設定を潰している可能性。coex_init後にcoexist_funcsを
   確認し、no-op上書きを止めても中身が有効か。
2. NuttXのcoexブリングアップ手順（クローン=scratchpad
   arch/risc-v/src/common/espressif/ + esp32c6/）と行単位比較。
   NuttXがcoexist_funcsをどう設定するか。
3. クラッシュするメソッド（pm_disconnected_start経由のNULL）だけを
   safe stubにし、PTI設定系は本物を通す部分的アプローチ。

### 到達点（Step0完全解明に最接近）
**「なぜC6 WiFiが受信しないか」＝coexist_funcsのno-op化でPTIゲートが
閉じたまま**、と根本原因を確定。修正はcoex初期化の完全化（NuttX比較）。
これでStep0の診断は事実上完了、残るは実装（fix）フェーズ。

### 教訓
「WiFi単独ならcoex不要」は**C6では誤り**。C6はWiFi-onlyでも受信経路が
coex調停器を通るため、coexist_funcsを殺すとRXが死ぬ。C3で通用した
簡略化がC6で通用しなかった（チップ差）。
