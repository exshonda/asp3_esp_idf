# ESP32-C5 実機ブリングアップ実施記録

TOPPERS/ASP3 の ESP32-C5 移植（設計書 `docs/c5-port-design.md`，机上実装済み）の
実機検証記録。branch `claude/c6-wifi-c5-dev-5vc6x9`。各ラウンドを「実施NN」で追記する。
調査の鉄則は `memory/feedback_hardware_investigation_rigor.md`（無ければ「未検証の
判別指標に乗らない・相関を因果と早合点しない・反証実験を先に」）。

## 実機・ツール（固定情報）

- **DUT＝ESP32-C5 #1**（主機）。chip **ESP32-C5 rev v1.0**（esptool実読）。
  - JTAG/usbjtagコンソール＝native USB-Serial-JTAG：**MAC `d0:cf:13:f0:a7:44`**（VID:PID 303a:1001，通常 `/dev/ttyACM2`・変動注意）。
  - UART（CP2102Nブリッジ）：**serial `b04e3bcfa270f0118f4894301045c30f`**（通常 `/dev/ttyUSB1`・変動注意）。
  - **番号は挿抜で変動。必ずMAC/シリアルで照合してから触る**（`~/usb_status.md`）。
  - **触れない同居ボード**：S3-A（JTAG MAC `f4:12:fa:5b:40:2c`）・P4-A（JTAG MAC `30:ed:a0:ea:98:0e`）・その他。
    いずれも同じ303a:1001でPIDだけでは区別不可＝OpenOCD/esptoolは必ずC5のシリアルにピン留めする。
- ツールチェーン：xpack `riscv-none-elf-gcc` 15.2（`~/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin`）。
  CMakeは `-DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf-`。
- esptool：`~/tools/espressif/python_env/idf6.1_py3.12_env/bin/python -m esptool`（**v5.3.1**，esp32c5対応。
  サブコマンドはハイフン形式 `chip-id`/`write-flash`。書込みは `--chip esp32c5 --port <PORT> write-flash 0x0 <asp_flash.bin>`）。
- OpenOCD：`~/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32/`（binと同梱scripts）。
  **必ずピン留め**：`-c "adapter serial D0:CF:13:F0:A7:44" -f board/esp32c5-builtin.cfg`。
  実行前に `udevadm info -q property -n <PORT>` でMAC一致を確認する運用。

---

## 実施01：B-0 first light — Direct Boot成立・XTAL48MHz確定・CLICベクタ確認・コンソール片方向確認（アプリ出力未達＝調査中）

### 背景
基板前に3構成（sample1/test_porting/wifi_scan）がこの環境でビルド成功済み
（wifi_scanはgcc14+厳格化対応でesp_wifi.cmakeに`-Wno-error`群追加，commit `d7f6c21`）。
C5 #1接続・同定後，B-0の最初のマイルストーン＝Direct Boot起動とコンソール疎通を検証した。
コンソール化け（§8.1.4 XTAL 40/48）の交絡を避けるため，初回は**ボーレート非依存の
usbjtagコンソール**（`-DESP32C5_CONSOLE=usbjtag`）でsample1をビルドし，native USB(ttyACM2)へ書込んだ。

### 結果（確定事項）
- **チップ＝ESP32-C5 rev v1.0**（esptool `chip-id`）。Single Core + LP Core，最大240MHz。
- **XTAL＝48MHz**（esptool実読）。→ 設計書 **§8.1.4「40か48か」を48MHzで確定**。
  - 波及：`esp32c5.h:159 TICKS_PER_US=16`（40MHz÷2.5前提の暫定）は48MHzなら**≈19.2**が有力＝B-1で実測較正要（放置で時刻1.2倍ずれ）。
  - `esp32c5.h:71 CORE_CLK_MHZ=160` は依然暫定（起動時実クロックはB-1でPCRダンプ/systimer対比で確定）。
- **Direct Boot成立**。OpenOCD haltで **PC=0x42000xxx**（フラッシュ実行領域）＝ROMがマジック
  `0xaedb041d`を認識しアプリへジャンプ済み。→ 設計書 **§8.1.7 をクリア**（C5 ROMでDirect Boot通用）。
- **CLICモード確認**。**mtvec=0x42004303**（下位2bit=`11`=3＝CLICモード）。archのCLICベクタ設定が実行済み。→ **§8.1.2 の一次確認**。
- **カーネル＋タスク動作**。PCサンプルが `consume_time`(sample1.c:145-146)・
  `esp32c5_usbjtag_snd_chr`・`sil_dly_nse1` を巡回＝sample1のタスクが実行されている。
- **コンソール・チャネル（チップ→ホスト）は生存**。`reset run`直後にttyACM2で
  **`ESP-ROM:esp32c5-eco2-20250121`**（ROMブートバナー）を受信。USB-Serial-JTAG経由の受信路とホスト接続はOK。

### オープン課題（調査中）＝アプリ側usbjtagコンソール出力がホストに届かない
- 症状：ROMバナーは出るが，アプリ（ASP3バナー・タスク出力）は一切出ない。
- 切り分け：JTAGで EP1(`0x6000F000`) へ 'XYZ' を直接書込み＋EP1_CONF(`0x6000F004`) に
  WR_DONE(1) してもホストに届かない。かつ **EP1_CONF読値=0x00000000＝`IN_DATA_FREE`(bit1)=0**
  ＝アプリ文脈でINエンドポイントFIFOが掃けていない。
- ドライバは正常：`esp32c5_usbjtag_putchar`は EP1書込み→EP1_CONFにWR_DONE でフラッシュ（`.h`確認済み）。
  ベース `0x6000F000`・EP1/EP1_CONFオフセット(+0/+4)はC5 halと一致（§9・§8.1.10）。
- **最有力仮説**：アプリのクロック初期化がUSB-Serial-JTAGの動作クロック（USBは**48MHz必須**）を
  落としている→FIFOが掃けず`IN_DATA_FREE=0`→出力不達。ROMは48MHz USBクロックを正しく供給するため
  出力できていた。XTAL48MHz確定とも符合（USBクロック源のPLL設定が40MHz前提だと48MHz実機で狂う余地）。
  - 反証すべき対抗仮説：ホスト側ドレイン不成立／USB PHYルーティング切替／`reset run`によるUSBリセット。
- 参考所見：JTAGのhalt/resume中に一度だけ `CPU lockup reset (reset cause 26)` を観測。ただし
  free-run中のcat（無介入8秒）はROMバナーも出ない＝リセットループの兆候なし。よってlockupは
  **JTAG halt誘発（USBトランザクション中断）の可能性**が高い。B-1で恒常性を再評価する。

### 次の一手（B-0継続）
1. アプリ文脈でのUSB-Serial-JTAGクロック状態（PCR相当のUSB_DEVICE/USBクロックゲート・USB PLL48MHz）を
   ROM時と比較（JTAG mdw）。落ちていれば，ドライバのオープン処理 or `hardware_init_hook()` で有効化。
2. 修正後 usbjtag sample1 を再ビルド・再書込みし，ttyACM2にASP3バナー＋タスク出力が出ることを確認。
3. その後 UART(ttyUSB1) コンソール既定経路も検証（48MHz前提でボーレート整合を確認。化ければ138240で判別＝§8.1.4）。

### 変更ファイル
- `docs/c5-bringup.md`：本ファイル新設（実施01）。
- （ビルドのみ：`build/c5_sample1_usbjtag/` を書込み。ソース変更なし）

---

## 実施02：アプリ出力未達の真因確定＝割込みが一切CPUへ届かない（INTMTX MAP値の意味がC5≠C6）＋コンソール割込みソース誤配線。修正で banner＋タスク出力が実機で出た

### 反証したもの（実施01の最有力仮説はいずれも×）
JTAG mdwでROM時（アプリ入口 `0x42000040` にhw-bp）とアプリ実行時のUSB-Serial-JTAG
レジスタ群を全比較した結果，**アプリはUSBを一切いじっていない**（バイト単位で同一）：
- `PCR_USB_DEVICE_CONF`(0x60096094)=**0x5**（CLK_EN=1・READY=1・RST_EN=0）＝クロック生存。
- `PCR_PLL_DIV_CLK_EN`(0x60096128)=**0x1ff**（48M(bit5)ゲート開）。USB列挙・JTAG・ROMバナー
  受信が成立している事実だけで48MHz USBクロックは正常＝**クロック仮説は棄却**。
- USBブロック(0x6000F000..)はROM時＝アプリ時で同一（`conf0`=0x4200等）。
- WDTも実測で全無効：`LP_WDT_CONFIG0`=0・`MWDT0/1_CONFIG0`=0・`LP_WDT_SWD_CONFIG`=0x40000000
  （bit30 SWD_DISABLE=1）＝**リブートはWDTではない**（SWDキー誤り仮説も棄却）。

### 症状の正体
`target_fput_log`（**ポーリング**出力）のカーネルバナーは全文ホストに届くが，ログタスク
経由の**割込み駆動**出力（"System logging task is started" 以降・タスク出力）が "Sys" で停止。
＝**割込みがCPUへ配送されていない**。実機で段階的に確定：
1. `INTMTX` src54(USB_SERIAL_JTAG)→線17 のMAP書込みは成立（0x600100d8=17）。
2. ペリフェラルは割込みアサート中（`INT_ENA`=0xc・`INT_ST`=0x8 IN_EMPTY）。
3. `INTMTX_STATUS1`(0x60010154) bit22=1＝マトリクスもsrc54のアクティブを認識。
4. **だがSIOのISRが一度も発火しない**（bp 0x420016ae未ヒット・PCは常に `consume_time`）。
5. CLIC内部線を全読み：**内部16(timer)/17(USB)が pending(IP=1) だがIE=0**，一方カーネルが
   許可・ハンドラ登録した**内部32/33はIE=1だがIP=0**。

### 真因（2つ・いずれもエディタブル層）
- **(A) INTMTXのMAP値の意味がC5はC6/C3と異なる**：C5ではMAP値が«CLIC内部番号»として
  **直接**使われる（CLIC側の+16変換は無い）。カーネルは外部割込みを内部17〜47
  （=INTNO+16）で許可・登録するので，MAPにはINTNOそのままでなく **`CLIC_LINE(intno)`=INTNO+16**
  を書く必要がある。従来はINTNOを書いていた＝内部16〜18（未許可線）へ配送され，
  タイマもSIOもFROM_CPUも**一切CPUに届いていなかった**（sample1はtask1が同一優先度を
  占有し続けるため「動いて見えた」だけ＝タイマ割込み不要で走っていた。get_timはHRT直読で進む）。
  - 実機で確定：`mww 0x600100d8 33`（=17+16）した瞬間に内部33がpending(IP=1)化し，
    ログタスクが解け **`task1 is running (013)…` が流れ出した**（決定実験）。
- **(B) コンソール割込みソースの誤配線**：`target_initialize()` が usbjtag コンソールでも
  無条件に `ESP32C5_INTSRC_UART0` を線17へ配線していた（C6の
  `target/esp32c6_gcc/target_kernel_impl.c` は `#ifdef ..._CONSOLE_USBJTAG` で
  USB_SERIAL_JTAG を配線）。(A)だけ直しても誤ソースのままなら届かないため両方必要。

### 修正（3ファイル・submodule非改変）
- `asp3/arch/riscv_gcc/esp32c5/chip_kernel_impl.c`：`esp32c5_intmtx_route` のMAP書込み値を
  `intno` → **`CLIC_LINE(intno)`**（=INTNO+16）に。
- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：コンソール線17の配線を
  `#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG` で **USB_SERIAL_JTAG / UART0** を切替（C6同型）。
- `asp3/arch/riscv_gcc/esp32c5/clic_kernel_impl.h`：MAP値の意味に関する旧コメント
  （「INTNOそのままでよい」）を実機確定内容へ訂正。

### 実機検証（pyserialのDTR非アサート読取り＝リセット誘発回避／openocd非介在）
```
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 10 2026, 11:16:30)
… Copyright …
System logging task is started on port 1.
Sample program starts (exinf = 0).
task1 is running (001).   |
…（連続・単調増加）… task1 is running (027).   |
```
- 8秒キャプチャで **task行30件・ROMバナー0＝自己リブート無し**（安定）。
  task2/3が出ないのは同一優先度task1が譲らないsample1本来の挙動（ブロック要求時のみ切替）。
- `EP1_CONF`(0x6000F004) の IN_DATA_FREE(bit1)：修正前=0固定（アプリ文脈で掃けず）→
  修正後はログタスク割込みでFIFOが掃けるようになり出力成立（上記ログが実証）。

### CPUロックアップ（reset cause 26）の位置づけ
free-run（openocd非介在・pyserialのみ）では**自己リブートは観測されず**（ROMバナー0）。
実施中に見えた複数回のROMバナー再出現は全て **openocdのhalt/idle起因のリセット
（ROM表示 rst:0x18=JTAG_CPU）**。＝実施01の「lockupはJTAG halt誘発の可能性が高い」を支持。
恒常的なリブート要因は無い。

### 変更ファイル
- 上記3ソース＋本doc（実施02）。`build/c5_sample1_usbjtag/` を再ビルド・再書込みして検証。

---

## 実施03：B-1完了 — タイマ較正（TICKS_PER_US=16確定・CPU=192MHz確定・SIL_DLY較正）＋ test_porting 実機 `# 6/6 passed`

実施02で割込み配送の根本バグ（INTMTX MAP値の意味がC5≠C6）を修正済み。本ラウンドは
B-1の完了判定＝**test_porting 6/6 PASS**を実機で達成し，あわせて暫定値のまま残っていた
較正定数（`esp32c5.h`）をすべて実機実測で確定させた。調査手段は全てC5 #1
（JTAG MAC `d0:cf:13:f0:a7:44`／ttyACM2）へのOpenOCD JTAG（`adapter serial`ピン留め）と，
DTR/RTS非アサートのpyserial読取り（リセット誘発回避）。

### 1. SYSTIMERカウント率 → TICKS_PER_US=16 を実機確定（暫定→確定）
OpenOCDでUNIT0カウンタ（`0x6000A000`：`+0x04`にスナップショット更新bit30を書き，
bit29 VALUE_VALIDを待って`+0x40/+0x44`のHI/LOを実読）を**壁時計bracket**で2s／4s間隔で採取：
- 0→1: dt=2.0059s dcount=32,101,997 → **16.0039 MHz**
- 1→2: dt=2.0059s dcount=32,091,786 → **15.9989 MHz**
- 0→2: dt=4.0118s dcount=64,193,783 → **16.0014 MHz**

→ **カウント率＝16.00MHz＝ticks/us=16で正しい**（暫定値がたまたま正解）。C5のSYSTIMERは
XTAL48MHz÷3=16MHz駆動（C6は40MHz÷2.5=16MHzと分周は違うが結果の16MHzは一致）。
CPUクロックとは独立の固定16MHz。SYSTIMERはCPU halt中も歩進する（本計測が成立した事実＝
CPU停止中もカウンタが進む＝上記dcountが得られた）。設計書 §8.1.5（ticks/us）を確定。

### 2. CPUクロック → CORE_CLK_MHZ=192 を実機確定（160→192に訂正）
mcycle CSR（CPU実行サイクル数．halt中は凍結）とSYSTIMER（連続16MHz）を基準にした
**二点法**（halt→mcycle/systimer読取→resume→sleep T→halt→再読取，T=1s/4sの差分で
halt処理オーバヘッドを相殺）：
- 各raw点：191.99〜192.02MHz（1s点・4s点とも）
- 二点法：**f_cpu = 191.9993 MHz**

→ **CPU=192MHz（=XTAL48MHz×4）を確定**。C6の160MHz（SPLL÷3）とは異なり，C5の
ROMブートローダは起動時点で既に192MHzに設定済み（レジスタ書換え不要）。設計書 §8.1.3・
§8.1（CPUクロック）を確定。CORE_CLK_MHZを160→192へ訂正。

### 3. SIL_DLY_TIM1/TIM2 → 20/20 を実機較正（30/12→20/20）
`sil_dly_nse`（`core_support.S`：`addi a0,-TIM1; bgtz→loop{addi a0,-TIM2; bgtz}`）の
1反復あたり実時間を，OpenOCDで**pc=sil_dly_nse・a0=N・retにhw-bp**を仕掛けて注入し，
SYSTIMER（連続16MHz）で壁時計計測（N=400M/800Mの二点法で固定debugオーバヘッドを相殺・
mstatus.MIE=0でISR混入排除）：
- **1反復 = 20.84 ns = 4 CPUサイクル@192MHz**（分岐成立ペナルティ由来）。
- C6のTIM2=12から大きく外れる＝設計書 §8.1.5「単純な比例外挿では合わない」の好例。

→ 未達（delay<要求）を避けるため **TIM2=20**（20.84の切下げ．実測でsil_dly_nse(N)は
要求の約1.04倍＝約4%の安全余裕・アンダーシュート無し）。エントリ（addi＋分岐成立≒1反復
≒20ns）を **TIM1=20** とした。較正後バイナリで再実測し全Nで delay≥要求（no undershoot）を確認
（絶対値のns/req＝1.07〜1.17はOpenOCD resume/halt handshakeの固定オーバヘッド≒26ms分の上振れ．
二点法の傾き＝真値1.042）。

### 4. WDT解錠キー（LP_WDT/SWD）：据置きで問題なし
実施02でsample1実行中に全WDT無効（LP_WDT/MWDT0/1・SWD_DISABLE=1）を実測済み＝解錠キーは
実際に効いている（効かなければWDT有効のままリブートする）。本ラウンドのtest_porting実行でも
8秒キャプチャで自己リブート（ROMバナー再出）ゼロ＝リセット要因なし。よって
`LP_WDT_WDT_WKEY`/`SWD_WKEY`（C3/C6転用）は据置きで妥当。

### test_porting 実機結果（usbjtagコンソール・ボーレート非依存・DTR非アサート読取り）
```
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 (Jul 10 2026, 12:33:43)
… Copyright …
# test_porting: kernel porting verification
1..6
ok 1 - syslog_output
ok 2 - tick_timer_basic
ok 3 - task_create_activate
ok 4 - semaphore_signal_wait
ok 5 - eventflag_set_wait
ok 6 - alarm_handler
# 6/6 passed
```
- **`# 6/6 passed`**（較正前・較正後の両バイナリで再現＝計3回一貫）。
- `ok 2 tick_timer_basic`＝TICKS_PER_US=16のend-to-end検証，`ok 6 alarm_handler`＝
  CLIC経由のタイマ割込み配送（実施02のa746e2c修正）のend-to-end検証。
- → **B-1完了判定（設計書 §7.2）を達成**。

### 変更ファイル
- `asp3/arch/riscv_gcc/esp32c5/esp32c5.h`：`CORE_CLK_MHZ` 160→192，`SIL_DLY_TIM1` 30→20，
  `SIL_DLY_TIM2` 12→20（いずれも実測根拠のコメントへ更新）。`TICKS_PER_US`は16のまま（実測で
  16.00MHz確定＝コメントを【暫定】→【実機確定】へ）。
- `docs/c5-bringup.md`：本節（実施03）。
- ビルド：`build/c5_test_porting_usbjtag/`（test_porting・usbjtag）を較正値で再ビルド・再書込み。

### 次の一手（B-2a）
- Wi-Fi scan（`esp_wifi_init`〜`scan`）＝deaf-RX切り分けの本丸（設計書 §7.2 B-2a）。

---

## 実施04：B-2a TRIAGE — Wi-Fi scan到達せず。`esp_wifi_init`がblob `pp_create_task`のpoll-delayループでハング（例外なし・C5固有・C6は`init`完走していた）。deaf-RX切り分けは現時点では判定保留（scan未到達）。APM/TEE初回JTAG読取り＝POR未初期化を確認（C5は`HP_APM_FUNC_CTRL=0x1f`でM4含む＝C6の`0xF`より広い）

### ビルド・書込み（ソース無変更・実施01-03の較正値バイナリ）
- `-DESP32C5_WIFI=ON -DESP32C5_CONSOLE=usbjtag`／`wifi_scan`アプリでビルド成功
  （RAM 75.96%＝約76%，リンク0エラー．gcc14+の`-Wno-error`群＝commit `d7f6c21`が効いている）。
- `build/c5_wifi_scan_usbjtag/asp_flash.bin` を esptool v5.3.1（`--chip esp32c5`）で
  `/dev/ttyACM2`（MAC照合済み `D0:CF:13:F0:A7:44`）へ 0x0 書込み・`Hash of data verified`。
- **ソースは一切変更していない**（本ラウンドは切り分け＝観測のみ）。`wifi_scan.c`のC6専用診断は
  すべて`#ifdef TOPPERS_ESP32C6_WIFI`ガード内でC5では非実行（クリーン）。

### TRIAGE結果（deaf-RXの手前でハング＝scan未到達）
DTR/RTS非アサートのpyserial読取り（リセット誘発回避）でコンソールを取得。**3回再現**
（初回=書込み後のhard reset／2回目=45秒観測／3回目=esptool `--after hard-reset run`での
クリーン再起動）。**毎回まったく同一の地点で停止**：
```
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 …
System logging task is started on port 1.
wifi_scan: initializing shim
pp rom version:
wifi_scan: esp_wifi_init
I (26) pp: pp rom version: 78a72e
      ← ここで以後一切の出力なし（"wifi_scan: esp_wifi_init -> N" が出ない）
```
- **`esp_wifi_init()`がリターンしない**＝`esp_wifi_start`／`esp_wifi_scan_start`／
  scan-doneコールバックには一切到達していない。よって**「AP検出 vs 0AP(deaf-RX)」の
  判定は現時点では下せない**（scanの手前で詰まっている）。
- 45秒観測しても進展なし＝単なる遅延ではない。`esp_shim: task '…' -> tskid N` の
  syslog（`esp_shim.c:626`）も一度も出ていない＝shim経由のタスク生成完了ログ未達。
- 自己リブート無し（ROMバナー再出ゼロ）。system aliveでタイマ割込みは配送されている
  （下記int_count[16]＝1417）。

### ハング地点の特定（JTAGバックトレース・確度高）
OpenOCD（`adapter serial D0:CF:13:F0:A7:44`ピン留め）で10回連続halt＝**PC=`0x42020C0C`
（`dispatcher_1`＝カーネルアイドルループ `csrrsi mstatus,8; j .`）に固定**＝`p_runtsk=NULL`＝
**readyなタスクが無く，`main_task`は待ち状態**（例外・トラップではない．`debug_reason=00000000`
＝DBGREQ）。C6で警戒していた`Breakpoint.`／illegal-instruction／`periph_module_reset()`の
enum値域最適化トラップ（設計書§7.3・§8.1.12）は**発生していない**——本件は別種の「ハング」。

`main_task`（=TCB8．saved SP=`0x40837bfc`が`_kernel_stack_MAIN_TASK` 0x40835e20+0x2000内で確定）
のスタックを全走査し，flash領域(0x42……)のリターンアドレスを`addr2line`で解決した呼出し連鎖：
```
main_task (wifi_scan.c:167 = esp_wifi_init(&cfg))
 → esp_wifi_init (hal .../esp_wifi/src/wifi_init.c:455)
  → esp_wifi_init_internal (blob libpp/libnet80211)
   → wifi_init_in_caller_task (blob)
    → pp_create_task (blob)              ← ここでblobがpoll待ち
     → task_create_pinned_to_core_wrapper (esp_wifi_adapter.c:488)/_kernel_wait_tmout_ok
      → dly_tsk (task_sync.c:473)        ← 現在ここでブロック
```
- **`dly_tsk`＝「遅延」であって永久待ちのプリミティブではない**＝`pp_create_task`が
  vTaskDelay相当（`task_delay_wrapper`→`dly_tsk`）でpollしながら，決して成立しない条件を
  待ち続けている（delay→条件チェック→delayの無限ループ．だからアイドルが回る）。
- 別のwifi-adapterタスク（TCB0．SP=`0x4084512c`）は`esp_shim_queue_recv`
  （`esp_wifi_adapter.c:386` `queue_recv_wrapper`→`esp_shim.c:533`）でRXキュー受信待ち＝
  こちらは起動して待機状態にある。`esp_shim_timer_task`（TCB6）は`twai_sem`待ち＝正常アイドル。

### 割込み計測（`esp_shim_int_count[]` @ `0x40803260`，JTAG実読）
- **line 1〜15（アプリがWi-Fi線として集計する範囲）＝すべて0**＝Wi-Fi MAC/BB割込みが
  一つも配送されていない。
- **index 16 = `0x589`=1417**（タイマ系＝カーネルtick．system aliveの証拠）。
- ＝「タイマ割込みは届くが，Wi-Fi割込みソースは一度もCPUへ配送されていない」。これは
  C6のdeaf-RX最終所見（"WiFi MAC割込みソース自体が実機で一切アサートされない"）と**現象が符合**するが，
  C5では**それがscan完走前（init段階）で顕在化**している点が異なる。

### APM/TEE 初回JTAG読取り（次フェーズのseed．アドレスはC5 halヘッダで実確認）
本来この読取りは「deaf-RX（scan完走・0AP）再現時」の枝だが，scan手前でハングしたため
本ラウンドでは判定不能。ただし停止状態のまま安価に採取できるので次フェーズseedとして記録する。
**C6のオフセットを転記せず，C5の`hal/components/soc/esp32c5/register/soc/{reg_base,tee_reg,hp_apm_reg}.h`で
実確認**（`DR_REG_TEE_BASE=0x60098000`・`DR_REG_HP_APM_BASE=0x60099000`＝設計書§9と一致）：

| レジスタ | C5アドレス | ASP3実測（本ハング状態） | POR/期待の解釈 |
|---|---|---|---|
| `TEE_M0_MODE_CTRL_REG` | `0x60098000` | `0x00000000` | M0(HPCORE)=TEEモード（POR既定） |
| `TEE_M4_MODE_CTRL_REG` | `0x60098010` | `0x00000003` | M4(WiFi/BTモデムDMA)=ree_mode2（POR既定．C6の実施86仮説値`0x3`と一致） |
| `HP_APM_REGION_FILTER_EN_REG` | `0x60099000` | `0x00000001` | region0フィルタ有効（POR既定） |
| `HP_APM_REGION0_ATTR_REG` | `0x6009900c` | `0x00000000` | region0にR/W/X一切許可なし（POR既定．C5は名称"ATTR"，C6は"PMS_ATTR"） |
| `HP_APM_FUNC_CTRL_REG` | `0x600990c4` | `0x0000001F` | **bit0-4＝M0-M4の5マスタ全てPMSフィルタ有効**（C6は`0xF`＝M0-M3の4本のみ） |

- **C5固有の重要差分**：`HP_APM_FUNC_CTRL`のPOR値が**C5=`0x1f`（M4を含む5本）／C6=`0xF`（M0-M3の4本）**。
  C6の実施86では「`FUNC_CTRL`が名指すのはM0-M3のみでモデム(M4)がregionベースAPMで
  直接ゲートされる確証は無い」ことが確度を中に留める留保だった。**C5では`FUNC_CTRL`に
  bit4(=`0x10`)が立っておりM4(モデムDMA)がPMSフィルタ対象に含まれる**＝
  `REGION_FILTER_EN=1`かつ`REGION0_ATTR=0`（許可ゼロ）の下でモデムDMAがメモリアクセスを
  弾かれうる状況＝**APM未初期化候補がC5ではC6より因果として強く成立しうる**。
  （ASP3のDirect Bootは`bootloader_init_mem()`を呼ばず，これらがPOR未初期化のまま＝実施86の前提と一致）。

### 判定と厳密性の留保（`memory/feedback_hardware_investigation_rigor.md`）
- **確定事実**：(1) C5は`esp_wifi_init`（blob `pp_create_task`のpoll-delayループ）でハングし
  scanに到達しない（3回再現・バックトレース確定），(2) Wi-Fi割込みは一切配送されず（line1-15=0），
  タイマは配送される（index16=1417），(3) APM/TEEはPOR未初期化で，C5の`HP_APM_FUNC_CTRL=0x1f`は
  M4を含む（C6=0xF）。
- **未確認（相関を因果と早合点しない）**：この初期化ハングの真因がAPM/TEEゲートなのか，
  Wi-Fi割込みソースのCLIC/INTMTXルーティング欠落（実施02でC5固有のMAP値意味差＝`CLIC_LINE`＋16
  を修正した系統の，Wi-Fi線に対する追加漏れ）なのか，別要因なのかは**未特定**。
  決定実験（`FUNC_CTRL←0`＋`REGION0_ATTR←全許可`のpoke，あるいはWi-Fi割込み線のMAP実値確認）は
  **本ラウンドでは実施しない**（scope＝初回読取りで停止）。

### TRIAGEの結論（申し送り）
- **C5移植のねらい（設計書§0）に対する現時点の答え＝保留**：C5がAPを受信するか(→C6固有確定)／
  0AP(→新世代モデム共通)かは，**scan到達前の別ハングにより未判定**。ただしこのハング自体が
  「Wi-Fi割込みソースが一切アサート/配送されない」というC6 deaf-RXと同根の現象を**より早い段階で**
  露呈しており，新世代モデム共通のゲーティング/ルーティング問題の可能性を示唆する（断定はしない）。

### 次フェーズの推奨（次ラウンド以降・本ラウンドでは未着手）
1. **APM/TEE決定実験**：ハング中にJTAGで`HP_APM_FUNC_CTRL(0x600990c4)←0`，
   `HP_APM_REGION0_ATTR(0x6009900c)←全許可(X/W/R)`，必要なら`REGION_FILTER_EN(0x60099000)←0`を
   poke→`esp_wifi_init`がリターン/前進しWi-Fi割込みが立つかを観測（因果の可否を切り分ける）。
   前進すれば恒久策＝エディタブル層（`asp3/target/esp32c5_espidf`の`hardware_init_hook()`）で
   `bootloader_init_mem()`相当のAPM初期化を最小移植。
2. **Wi-Fi割込みルーティングの実値確認**：blobが`set_isr`で要求するWi-Fi MAC/BBソースの
   INTMTX MAP実値（実施02の`CLIC_LINE(intno)`=+16規約が適用されているか）と，該当CLIC線の
   IE/IP/pendingをJTAGで確認（1のAPMと独立に潰せる対抗仮説）。
3. 参考機（stock-IDF/NuttXのC5）を2台目C5(#2)で立て，同一初期化段階のAPM/TEE・
   Wi-Fi割込み線をレジスタdiffするのが最も確実（C6作業でNuttX陽性対照が効いた実績）。

### 変更ファイル
- `docs/c5-bringup.md`：本節（実施04）のみ。**ソースコード変更なし**（観測ラウンド）。
- ビルド：`build/c5_wifi_scan_usbjtag/`（wifi_scan・usbjtag）を書込み・観測。

---

## 実施05：INTMTXルーティング修正（`set_intr_wrapper`）を実機投入も**ハング不変**＝この修正はハング上流のため無効／APM決定実験を実施＝**APM仮説を反証（poke無効）**。真因は`pp_create_task`のblob内条件待ち（APM/割込みルーティングいずれでもない）

### 検証した修正（実施04申し送り#2の系統）
`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c` `set_intr_wrapper()`：INTMTXのMAP書込み値を
生の`intr_num`→**`line`（=`CLIC_LINE(intr_num)`=intr_num+16）**へ（実施02のカーネル側修正a746e2c＝
実機確定と同型）。仮説＝「Wi-Fi MAC割込みが未許可のCLIC線へ配線され，blobの`pp_create_task`が
永久に来ないIRQを待つ→`esp_wifi_init`ハング」。同ファイルの`ints_on_wrapper`（`CLIC_IE_OFF(CLIC_LINE(n))`）・
CTL/ATTR書込み（いずれも`line`基準）と内部整合する修正であることも確認。

### ビルド・書込み（実施02-04と同一手順・C5 #1のみ）
- `-DESP32C5_WIFI=ON -DESP32C5_CONSOLE=usbjtag`／wifi_scanでビルド成功（RAM 75.96%・リンク0エラー）。
  banner時刻が旧いのは`esp_wifi_adapter.c`のみ再コンパイル＝banner定義ファイル非再ビルドのため（＝新バイナリで正しい）。
- `build/c5_wifi_scan_usbjtag/asp_flash.bin`を esptool v5.3.1（`--chip esp32c5`）で `/dev/ttyACM2`
  （MAC照合 `D0:CF:13:F0:A7:44`）へ 0x0書込み・`Hash of data verified`。

### 結果①：ルーティング修正はハングを一切変えない（3回再現・実施04と完全同一）
DTR/RTS非アサートのpyserial読取り（12s/30s/reset後観測）で毎回まったく同一地点で停止：
```
… System logging task is started on port 1.
wifi_scan: initializing shim
pp rom version: 
wifi_scan: esp_wifi_init
I (26) pp: pp rom version: 78a72e9   ← 以後一切出力なし（"esp_wifi_init -> N"未達）
```
JTAG（`adapter serial D0:CF:13:F0:A7:44`）でhalt：**PC=`0x42020C0C`（dispatcher_1アイドル）・
`_kernel_p_runtsk`(0x40801df0)=NULL・`esp_shim_int_count[]`(0x40803260) line0-15＝全0／index16＝
`0x589`(=1417)**＝実施04と数値まで一致。**Wi-Fi割込みは相変わらず一つも配送されていない**。
- **決め手**：`set_intr_wrapper`は入口で`syslog(LOG_NOTICE, "wifi_adapter: set_intr …")`を出すが，
  全キャプチャで**一度も出ていない**＝blobは**set_intrを呼ぶ前に**（＝Wi-Fi MAC割込みを登録する前に）
  `pp_create_task`でハングしている。よって本修正のコードパスはハング到達前に踏まれず，
  **このハングには効きようがない**（修正自体は下記の通り正当だが，効果はハング解消の下流）。

### 結果②：APM決定実験（実施04推奨#1）＝**APM仮説を反証**
ハング到達後にJTAG単一セッション内で poke→resume→4s待ち→再haltし，デタッチ由来のリセット
（実施02既知：openocd detach＝rst 0x18でpokeが消える）を排除して観測：

| レジスタ | poke前（POR＝実施04と一致） | poke後（書込み確認） |
|---|---|---|
| `HP_APM_REGION_FILTER_EN`(0x60099000) | `0x00000001` | `0x00000000`（フィルタ無効化） |
| `HP_APM_REGION0_ATTR`(0x6009900c) | `0x00000000` | `0x00000777`（R/W/X全許可・3モード全部） |
| `HP_APM_FUNC_CTRL`(0x600990c4) | `0x0000001F` | `0x00000000`（PMSフィルタ全解除） |

**APMを完全開放して4秒走らせても前進ゼロ**：PC=`0x42020C0C`（アイドルのまま）・p_runtsk=NULL・
int_count line0-15＝全0（Wi-Fi割込み一つも立たず）。console進展もなし。
→ **`esp_wifi_init`ハングはAPM/PMSによるモデムDMAゲーティングが原因ではない（反証）**。
実施04で「C5は`FUNC_CTRL=0x1f`でM4含む＝C6より因果が強く成立しうる」とした候補は，実機決定実験で**否定**された。
恒久策（`hardware_init_hook()`へのAPM初期化移植）は**不要**＝実装しない。

### 確定した真因の所在（断定はここまで）
- 真因は`pp_create_task`（blob）内の**条件待ち（`dly_tsk`ポーリングループ）**であり，
  **APMでも割込みルーティングでもない**（両方を実機で潰した）。blobが作った下位タスクが
  ready条件に達しない類（実施04：別wifi-adapterタスクTCB0が`esp_shim_queue_recv`で受信待ち）が残る候補。
- これは新たな多段調査の入口＝**本ラウンドではここで停止**（scope遵守）。

### ルーティング修正の扱い＝commitする（潜在バグ修正・ただしハング解消ではない）
本修正はハングを解消しないが，(1) 実機確定のカーネル側実施02修正a746e2cと同型，(2) 同ファイルの
`ints_on_wrapper`/CTL/ATTRが全て`line`基準で，MAPだけ生`intr_num`なのは明白な不整合，
(3) blobが`pp_create_task`を越えて実際にset_intrを呼ぶようになった時点で確実に刺さる地雷，
の3点から**正当な潜在バグ修正としてcommitする**（commitメッセージ／本節に「ハングは解消しない」旨を明記）。

### 次フェーズの推奨
- APM・割込みルーティングの2大仮説を実機で潰した以上，**2台目C5（stock-IDF/NuttX）参照機が
  ここで明確に必要**：同一init段階（`esp_wifi_init`内`pp_create_task`）でのタスク状態・
  blobが待つ条件・関連レジスタを正常系とdiffするのが最短（C6でNuttX陽性対照が効いた実績）。
- 単体でのソフト側追撃は`pp_create_task`が待つ条件の特定（blob内・シンボル限定的）で確度が低い。

### 変更ファイル
- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：`set_intr_wrapper`のINTMTX MAP値を
  `intr_num`→`line`（=`CLIC_LINE(intr_num)`）へ（潜在バグ修正）。
- `docs/c5-bringup.md`：本節（実施05）。
- ビルド：`build/c5_wifi_scan_usbjtag/`（wifi_scan・usbjtag）を再ビルド・再書込みして検証。
- **注**：`asp3/asp3_core`（submodule）に残るC6調査由来の一時diff（`kernel/time_event.c`の
  "no time event"syslog抑止）は本ラウンドの対象外・非commit（submoduleポインタは触らない）。

---

## 実施06：真因を訂正——「`dly_tsk`ポーリングの良性ハング」ではなく**PHY/早期wifi-init中のストアアクセス例外（mcause=7・書込先=読取専用ROM 0x40038640）→ ASP3のCLICモード例外ディスパッチが多重フォルト（mcause上位ビット未マスク）→ CPU_LOCKUP（rst 0x1a）→リブートループ**。stock参照機（2台目C5）は同一チップで`esp_wifi_init`完走・AP多数検出（5GHz ch52含む）＝HW・電波環境ともに正常＝C5はdeaf-RXではない公算大（C6 deaf-RXはC6固有の可能性）

### 方法：陽性対照diff（stock ESP-IDF v6.1を2台目C5=#2で実行）＋ASP3側JTAG（例外捕捉）
- 実機は2台：**C5 #1＝ASP3 DUT**（JTAG MAC `d0:cf:13:f0:a7:44`／今回はttyACM4，UART bridge=ttyUSB2）と
  **C5 #2＝stock参照**（JTAG MAC `d0:cf:13:f0:c8:94`／ttyACM2，UART bridge=ttyUSB1）。毎操作前に
  `udevadm`でMAC照合。書込み・OpenOCDともMACピン留め。
- **重要な観測条件の発見**：usbjtagコンソール（native USB CDC＝ttyACM4）を**ホストがドレインしていないと，
  アプリはカーネルバナー出力の`esp32c5_usbjtag_putready`ポーリングでFIFO満杯待ちに入り，`esp_wifi_init`の
  クラッシュ地点まで到達しない**（＝アイドルに見える）。ドレインすると本来の挙動（クラッシュ）が出る。
  → **実施04/05の「PC=0x42020C0C アイドル・リブート無し・`dly_tsk`待ち」は，コンソール未ドレイン時の
  “バナー出力ブロック”状態を見ていた**（＝ハングの誤診）。JTAG観測時は別プロセスでttyACM4を常時読取り
  ドレインするのが必須。

### stock参照機（C5 #2）＝正常系の確定（陽性対照）
stock `examples/wifi/scan`（`idf.py set-target esp32c5`，VERBOSEログ）をC5 #2へ書込み・UART bridgeから
リセット起動。UART(ttyUSB1)で全ログ取得：
```
I (1035) pp: pp rom version: 78a72e9d5
I (1035) net80211: net80211 rom version: 78a72e9d5
I (1055) wifi:wifi driver task: 4085f2b8, prio:23, stack:6656, core=0
… efuse/nvs 多数 …
I (1385) wifi:wifi firmware version: 33352cb
I (1505) phy_init: phy_version 109,edb400d6,Mar 10 2026,10:22:11
… scan …
I (13108) scan: SSID  <SSID-EDU>   RSSI -47   Channel 1
I (13188) scan: SSID  <SSID-INST-1X> (5GHz)  RSSI -53
I (13208) scan: SSID  <SSID-EDU>   Channel 52   (=5GHz)
I (13298) main_task: Returned from app_main()
```
- **stockは`esp_wifi_init`を完走しscanでAP多数を検出（2.4GHz ch1＋5GHz ch52）**。
  → (1) このC5チップのWi-Fi HWは正常，(2) 電波環境にAPが存在（5GHz含む），(3) blob/PHYの正規経路は動く，
  を確定。**＝C5がAPを受信できることの直接証拠＝C5はdeaf-RXではない公算が高い**（C6のdeaf-RXはC6固有の
  可能性が強まる。ただしASP3側がinitを完走していないため“ASP3構成でのdeaf-RX”は依然未判定）。
- 決定的な差：blobは同一libpp（rom version 78a72e9d5＝ASP3と一致）。**違いはRTOSグルーではなく，
  Direct Bootが省く起動時初期化**（下記）。

### ASP3側（C5 #1）＝真因の実機確定（JTAG・例外捕捉）
コンソールをドレインした状態での真の挙動（無介入・OpenOCD非介在でも再現）：
```
… wifi_scan: esp_wifi_init
I (32) pp: pp rom version: 78a72e9d5
ESP-ROM:esp32c5-eco2-20250121
rst:0x1a (CPU_LOCKUP),boot:0x18 (SPI_FAST_FLASH_BOOT)
Core0 Saved PC:0x42020e48
…（以後リブートを繰返す＝リブートループ）
```
`core_exc_entry_1`（`0x42020e20`）にhw-bpを置いて**最初の例外**を捕捉（`monitor reset halt`→
`riscv set_mem_access sysbus`）：
```
mcause = 0x38000007   ← 例外コード=7＝Store/AMO access fault（OpenOCD表示 "PMP Store access fault"）
mepc   = 0x4000165e   ← フォルトした命令＝ROM領域（0x40000000–0x40050000＝内蔵ROM/SOC_IROM_MASK）．
                        近傍はrom.ldのphy_*群．レジスタ s0=phy_param（0x4080023c）
mtval  = 0x40038640   ← ストア先アドレス．**物理的に読取専用の内蔵ROM**（実読で命令列 0x00000297…が入っており
                        書込不可）．+bit23した 0x40838640 はHP-SRAM（可書込・現在ゼロ）だが，これはASP3の
                        _kernel_stack_SHIM_TIMER_TSK領域＝PHYの正規ターゲットでもない
```
- **CPU_LOCKUPの機序（多重フォルト）**：Saved PC `0x42020e48` は `core_exc_entry_1` 内の
  `jalr a2`（＝`_kernel_exc_table[mcause]` のハンドラ呼出し）．ディスパッチは
  `csrr a1,mcause; slli t2,a1,2; add t2,t2,_kernel_exc_table; lw a2,0(t2); jalr a2`．
  **CLICモードのmcause=0x38000007は上位に MPP/MPIE 等のビット（0x38000000）を含む**ため，
  `slli mcause,2`のインデックスが桁外れ（≒0xE000001C）となり，`_kernel_exc_table`の遥か範囲外を
  読んでゴミのハンドラアドレスへ`jalr`→即フォルト→**例外処理中の例外＝CPU_LOCKUP（rst 0x1a）→リセット**。
  ＝単一の回復可能なアクセス例外を，例外ディスパッチのバグが致命的ロックアップに増幅している。
- **保護機構（PMP/PMA）は無関係と確定**：`pmpcfg0-3=0`・`pmpaddr0-15=0`（PMP空＝Mモードは全許可），
  `pma_cfg0-15=0x08000000`（EN=0＝全無効）・`pma_addr=0`．**フォルトはPMP/PMA由来ではなく，
  0x40038640が物理的に読取専用ROMだから**（実施05で反証したAPMとも別レイヤ）。
- **フォルト地点は`phy_enable`到達前**：`wifi_clock_enable_wrapper`/`phy_enable_wrapper`/
  `esp_phy_enable`/`register_chipv7_phy` の4関数にbpを置いて連続continueしても，**いずれも1つも踏まずに
  先に`core_exc_entry_1`へ到達**．＝クラッシュは「pp rom version」直後・osi経由のPHYイネーブルより**手前**の
  blob早期wifi-init（stockでいう “wifi driver task” 生成〜efuse/nvs〜firmware version の帯）で，
  blobが直接呼ぶROM（PHY系）コードが不正アドレス0x40038640へストアして落ちる．ASP3の
  `esp_shim_task_create`のログ「esp_shim: task … tskid」も出ない（＝“wifi driver task”生成完了より前で落ちる）。

### 確定事実（厳密性の留保つき）
1. C5 ASP3の`esp_wifi_init`は**良性ハングではなくクラッシュ**：ROMコード（mepc=0x4000165e）が
   読取専用ROMアドレス0x40038640へストア→Store access fault（mcause=7）→ASP3のCLIC例外ディスパッチが
   多重フォルト→CPU_LOCKUP→リブートループ（複数回再現・OpenOCD非介在の純コンソール観測でも再現）。
2. stock ESP-IDFは**同一チップで**`esp_wifi_init`完走・scanでAP多数検出（5GHz含む）＝HW・電波環境は正常。
   blobは同一（rom version一致）＝差はDirect Bootが省く起動時初期化。
3. PMP/PMAは空＝保護機構ゲートではない．0x40038640は物理ROM＝**blob/ROMが不正アドレスを計算している**
   （何らかの初期化欠落による）。
4. **副次バグ（別個・重要）**：C5はCLICモードでmcauseに上位ビットを含むため，asp3_core共通の
   RISC-V例外ディスパッチ（`core_support.S`の`slli mcause,2`で`_kernel_exc_table`をインデックス）が
   **mcauseを例外コードにマスクしておらず**，任意の例外で範囲外参照→CPU_LOCKUPになる（＝今回の増幅器．
   通常運用でも「例外が起きた瞬間にpanicではなくロックアップ」という潜在的地雷）。

### 未確定（相関を因果と早合点しない）
- **0x40038640という不正ストア先の“素性”は未特定**：計算元のベース（レジスタ/変数/blobグローバル or
  破損した関数ポインタ）がどの初期化欠落で不正になるかは未RE．stockが省かない起動時初期化のうち
  どれが効くかは未同定（PMP/PMAは物理ROMゆえ無関係と確定済み＝候補から除外）。
- したがって**本ラウンドはソース修正を行わない**（推測パッチ禁止＝`memory/feedback_hardware_investigation_rigor.md`）。
  実施02のカーネル側修正のように「実機で書換え→前進」を確認できる決定実験に乗ってから修正する。

### 次フェーズの推奨（次ラウンド）
1. **不正ストア先の計算元を特定**：`core_exc_entry_1`到達時のistack上の例外退避フレーム（`_kernel_core_exc_entry`
   が`sp-76`にra/mepc/mstatus/汎用レジスタを保存）から，フォルト直前のra＝ROM関数の呼出し元blobアドレスを
   復元し，blob側の該当関数を`addr2line`/nmで同定→そのベース計算の入力（phy_param・efuse・グローバル）を追う。
   （今回はhw-bpリソース枯渇とリブートループの再入でフレーム走査を完了できなかった＝daemon再起動でトリガ
   クリア後に再挑戦）。
2. **副次バグ（CLIC mcauseマスク）を先に潰すと調査が進む**：例外がロックアップ→即リセットではなく
   正規のpanic（例外ハンドラ）へ落ちれば，mepc/mtval/コールスタックがコンソールに出て素性特定が容易になる。
   ただし`core_exc_entry_1`は`asp3/asp3_core`（submodule・改変禁止）＝恒久修正はasp3_core側の
   「CLICターゲットでmcauseを例外コードへマスクする」対応＝submodule bumpが要る（本リポジトリでは不可）。
   暫定策としてC5 arch（editable）で mtvec を「mcause上位を落として共通ハンドラへ渡す薄いC5専用トランポリン」に
   向ける案を要検討。
3. **stockの起動時初期化のうち省いている項目の棚卸し**：`bootloader_init`／`esp_cpu_configure_region_protection`
   （PMP/PMA＝今回無関係と判明）以外に，キャッシュ/MMU・PHY init data・efuse較正ロードなど，Direct Bootが
   通らない初期化で 0x40038640 のベースに効くものを stock 起動ログの初期化関数列（`cpu_start: calling init
   function …`）と対照して洗い出す。

### 変更ファイル
- `docs/c5-bringup.md`：本節（実施06）のみ。**ソースコード変更なし**（観測・真因訂正ラウンド）。
- ビルド：`build/c5_wifi_scan_usbjtag/asp_flash.bin`（実施04-05と同一・再書込みのみ）でC5 #1を観測．
  C5 #2はstock `examples/wifi/scan`（esp32c5）を書込み・正常系として保全（再flashで元に戻せる）。

---

## 実施07：増幅バグ（CLIC例外ディスパッチのmcause未マスク）を修正＝サイレントCPU_LOCKUP消滅（実機確認）．根本store faultは残存

### 背景
実施06で真因を «PHY早期initの不正store（ROM `0x40038640`書込み）＋CLIC mcause未マスクの例外ディスパッチが多重フォルト→CPU_LOCKUP» と特定．本節では«増幅バグ»側を修正した（根本は別途）．設計はユーザー承認のうえ **FMP3（esp32p4/coreboard_esp32s31でCLIC実績）と同一のチップ依存マクロ方式** に統一．

### 修正（asp3_core submodule ＋ C5 arch）
- **共通 `asp3_core/arch/riscv_gcc/common/core_support.S`**：例外ディスパッチ2箇所
  （`core_exc_entry_1` 870行・`nk_exc_entry_2` 1172行）の `csrr a1, mcause` を
  `core_get_exccode_asm a1` に置換．exccode抽出をチップ依存マクロへ委譲し共通部は
  チップ非依存を保つ．
- **`core_get_exccode_asm` マクロ**（`save_additional_regs_int`等と同じ機構）：
  - CLIC版＝`arch/riscv_gcc/esp32c5/chip_asm.inc`（本リポジトリ）：`csrr;slli 20;srli 20`
    ＝mcause下位12bit（exccode）のみ残す（一時レジスタ不要）．
  - 非CLIC版＝`esp32c3/esp32c6/polarfire_soc/rp2350/chip_asm.inc`（submodule）：
    `csrr \reg, mcause`（そのまま）．共通部が呼ぶため全RISC-Vチップに定義が必須．
- submoduleコミット **ef1f1c8**（`fix(arch): CLIC対応 …`，`kernel/time_event.c`の既存差分は非ステージ）
  ＝`origin/feat/esp32c6` 先端`8dab767`の上へrebase（当初`e006aeb`は作業ツリーが記録より2コミット
  古い`66988d2`基点だったため`8dab767`へ載せ直し）→`feat/esp32c6`へpush（`8dab767..ef1f1c8` FF）．
  外側でsubmoduleポインタを`8dab767→ef1f1c8`へbump．

### 検証
- **ビルド**：C5 wifi_scanフルビルド成功．C6/C3で `core_support.S` アセンブル成功
  （両者のリンク失敗は `esp_rom_set_cpu_ticks_per_us` の既存問題＝本変更と無関係）．
  非CLIC 4ポートは生成アセンブリが従来と同一＝機能変化なし．
- **逆アセンブル**：`asp.elf` の `core_exc_entry_1` に `csrr a1,mcause; slli a1,0x14;
  srli a1,0x14` を確認．`exc_table[7]=_kernel_default_exc_handler(0x42023220)`．
- **実機（C5 #1 = d0:cf:13:f0:a7:44，クリーン再書込み11/11再現）**：

  | | 修正前（実施06） | 修正後（本節） |
  |---|---|---|
  | リセット要因 | `rst:0x1a (CPU_LOCKUP)` | **`rst:0x7 (TG0_WDT_HPSYS)`** |
  | Saved PC | `0x42020e48`（誤ディスパッチjalr＝アプリ内ゴミ） | **`0x40038598`（PHY-init ROM）** |

  即時ハードロックアップが消滅し，CPUは生存したままPHY-init ROM（元store faultと同近傍）
  へ到達＝例外が正しく `exc_table[7]` へディスパッチ．**増幅バグ解消を確認**．

### 残課題（次段）
- 根本のstore fault（`0x40038640`＝読取専用ROMへの不正書込み）は健在＝依然リブート
  ループだが，«CPU_LOCKUP»でなく«TG0 WDTタイムアウト»駆動に変化（デフォルト例外
  ハンドラがhaltせずfault再発→WDTリセット）．次段は根本＝«飛ばされたinitで不正な
  ベースポインタが残る»点の特定（fault時 `ra` 回収／stockの `cpu_start: calling init
  function …` 列と対照）．
- 実機JTAG制約：usbjtag CDCのドレイナーとOpenOCDが同時実行不可（OpenOCD接続時は
  ROM段で停止しアプリfaultに到達せず）．post-fix mcause直読・console panicログ・
  fault時raは本ラウンドで未取得．

### 変更ファイル
- submodule（ef1f1c8＝feat/esp32c6）：`arch/riscv_gcc/common/core_support.S`＋
  `esp32c3/esp32c6/polarfire_soc/rp2350/chip_asm.inc`．
- 本リポジトリ：`arch/riscv_gcc/esp32c5/chip_asm.inc`（CLIC版マクロ）＋submodule bump＋本doc．

### 訂正・追検証（電源断後）— «回帰»はボード状態の交絡で，CLIC修正/TLS基点は健全
反映直後，正基点`ef1f1c8`でtest_portingが早期ブートで失敗（WDTループ・バナー無）し
«回帰»に見えたが，実機bisectで**否定**した：
- test_portingを`8dab767`(TLSのみ)・`e006aeb`(66988d2+CLIC修正)・そして**`66988d2`
  （実施03で6/6通った基点そのもの）**でビルドしても，**全ビルドが同一WDTループで失敗**．
  ＝コード変更に起因しない．原因は**C5 #1に latch されたボード/環境状態**（実施04-06の
  Wi-Fi CPU_LOCKUP/WDTリブートループ実験由来と推定．ソフト/ハードリセットで消えない）．
- **物理電源断（両USBケーブル抜挿）で解消**．電源断後は`ef1f1c8`・`66988d2`とも
  **`# 6/6 passed`（各2回再現）**＝**push済み`ef1f1c8`は健全・revert不要**，B-1も正基点
  （TLS作動）で6/6再確認．
- 上記«残課題»節の`0x40038598`は**red herring**（download-modeブートでも出るROM
  Saved-PCで fault番地ではない）．実施06/07でこの番地を「PHY-init到達」の根拠とした
  記述は割り引くこと．wifi_scanの「store fault→exc_table[7]ディスパッチ」挙動は
  クリーンなボードで取り直す（次段・タスク根本原因）．
- 教訓：ESP32-C5で既知良ファームが突然WDTループしたら，まず物理電源断（memory
  `c5-latched-board-state`）．JTAG-during-crashは不可（ドレイナーとOpenOCD排他・WDTが
  速すぎてasync-halt不可）＝fault捕捉はRTC-RAM記録が堅い．

---

## 実施08：B-2a根本原因を確定・修正——eco3.ldがblobのPHY関数をROM版に上書き（rev不整合）．store fault解消を実機確認．次の壁＝PHY RX較正ハング

### 根本原因（実機で確定）
`esp_wifi_init`のstore fault（実施05-07：mepc `0x4000165e`・mtval `0x40038640`=読取専用ROM）の連鎖を確定：
```
esp_wifi_init → phy_rf_init(flash/libphy) → phy_band_i2c_set(ROM 0x40001654) → sw t1,0(t0), t0=0x40038640 → fault
```
- **t0はテンポラリ**で，ROM `phy_band_i2c_set`は«呼出し直前の状態»として使う設計．それを整えるflash側`phy_rf_init`（hal libphy＝os_adapter v8世代）と，eco2 ROMの`phy_band_i2c_set`の契約が食い違う．
- **真因**：ASP3が`esp32c5.rom.eco3.ld`を無条件リンクしており，同ldが`phy_band_i2c_set = 0x40001654;`（生ROMアドレス）を含む383シンボルを供給し，**blob自身のRAM版PHY関数を上書き**．実チップはeco2で，eco2 ROMのその関数がblobと非互換→fault．
- **stock IDF v6.1（同一eco2チップ／C5#2）はblobのRAM版`phy_band_i2c_set`（`0x40803704`）を使うため無事**（stock ELFで`phy_rf_init`→0x40803704，ASP3は→0x40001654 を確認）．設計§8.1 9番「eco3.ldがrev固有では？」の懸念が的中．
- 補足：hal libphy.a ≠ IDF v6.1 libphy.a（md5相違），os_adapter版 hal=0x08 / IDF=0x09（blob-swapはABI非互換で不成立＝この差分から判明）．

### 修正（エディタブル層・2ファイル）
- `asp3/target/esp32c5_espidf/esp_wifi.cmake`：ROM ldリストから **`esp32c5.rom.eco3.ld`を除外**（blob自前のRAM版PHY関数を使わせる）．
- `asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`：eco3.ldの唯一の本来目的だった **`phy_get_max_pwr`固定値スタブ（`int8_t`,return 20）を復活**（C6版と同型）．
- 検証：wifi_scanフルビルド成功（eco3.ld除外の副作用＝未定義参照なし）．`nm`で`phy_band_i2c_set`→`0x4205a292`（blob版・flash），`phy_get_max_pwr`→スタブ，を確認．

### 実機検証（C5 #1）
- **store fault消滅を確定**：修正版で`esp_wifi_init`が旧fault点を越える．旧RTC frameは«reflash跨ぎの残骸»と立証（クリア→reset run→再走→再生成せず＝今回faultなし）．ボードlatch無し．
- **新たな壁＝PHY RX較正ハング**（crashでなくハング）：`register_chipv7_phy`内のRXフロント較正
  （`phy_iq_est_enable_new`・`phy_get_pkdet_data`・`phy_rx_pkdet_dc_cal`・`phy_gen_rx_gain_table`・
  `phy_abs_temp`近傍，PC=0x4202xxxx=blob）で非収束ループ．scan/AP未到達＝deaf-RX判定は保留．
  停止点がRXフロントエンド較正＝deaf-RXの隣接領域（位置相関・因果は未主張）．

### 申し送り
- 次段＝このPHY RX較正ハングの根本（RXアナログ系のクロック/電源/較正の未初期化，または別のblob/ROM取り違え）．
- **C6 deaf-RXへの示唆**：C6は`eco*.ld`が存在せず本問題は持たない（＝手前を越えてscan到達）が，hal C6 blobもIDFと版が異なる．C6の84ラウンドはレジスタ比較中心で«ROM-vs-blob関数の取り違え»は盲点＝C6再開時に同手法（stock参照で関数解決先を差分）で見直す価値あり．
- 一時計装：`apps/wifi_scan/wifi_scan.c`のDEF_EXC fault捕捉ハンドラは未コミットのまま残置（RX較正ハング調査で再crashに備え．診断用・本コミットには含めない）．

### 変更ファイル
- `asp3/target/esp32c5_espidf/esp_wifi.cmake`・`wifi/esp_shim_blobglue.c`＋本doc（実施08）．

---

## 実施09：PHY RX較正ハングの根本＝libphy blob世代とeco2シリコンの不整合（＝実施08と同一根）．C5シリコン自体はdeaf-RXでないと確定

### 診断（実機・厳密）
実施08の修正後にハングするPHY RX較正ループを解析：
- ループ本体＝`phy_iq_est_enable_new`（blob-flash `0x420280c2`）．退出条件は**`0x600a047c` bit16（IQ推定«完了»フラグ）**のみ．ASP3では0のまま立たず（`phy_get_pkdet_data`/`phy_abs_temp`を巡回して無限ループ），stockでは0x00010000に立つ．
- **決定的差分実験**：stock IDF v6.1をC5#2で同一較正点（`phy_iq_est_enable_new`入口）にbreakpointし，RXアナログ有効化レジスタをASP3(ハング)とバイト比較→**全て一致**：
  PMU_RF_PWC `0x600b0158`=0xff800000／MODEM_SYSCON `0x600a9c14`=0x003be7ff・`0x600a9c18`=0x10003802／
  MODEM_LPCON `0x600af018`=0x7（wifipwr+coex+i2c_mst）・`0x600af010`=0．pkdet `0x600a0c50`=0（stockの動作時も0＝red herring）．
- ＝**「RXアナログのクロック/電源/regi2c/WIFIPWR初期化欠落」仮説クラスは棄却**（C6由来のregi2c/WIFIPWR修正は正しく効いている）．eco3.ld類の誤解決も棄却（RX較正関数は全てblob-flashに解決）．PHY init dataも正しい（c5版）．

### 真因（最有力・確定的）
残る差＝**libphy.aの版そのもの**．ASP3＝hal `esp32c5/libphy.a`（md5 `51166fb6…`），stock＝IDF v6.1（`4ccdbdbe…`）．逆アセンブルで両者の`phy_iq_est_enable_new`が**別コード/別アルゴリズム**（ASP3=`li s2,50`+単純`bge`／stock=`li s2,1;s4,80;s5,150`＋追加status `0x600a08d0`読取り＋多条件）と実証．
＝**hal(v8世代)のlibphyの`register_chipv7_phy` RX IQ推定シーケンスがeco2シリコンで収束しない．IDF v6.1(v9)のlibphyは収束する**．
**実施08(store fault)と同一根＝hal blob世代とeco2シリコンの不整合が，1較正ステップ後に再顕在化**したもの．

### ミッション上の確定事項
- **C5シリコン自体はdeaf-RXでない**：stock IDF v6.1（v9 blob）が同一eco2チップ（C5#2）で同室AP多数（5GHz ch52含む）を受信．
  → 「新世代モデムは本質的にdeaf」説はシリコンレベルで**棄却**＝**C6のdeaf-RXはC6固有**（新モデム普遍の性質ではない）．
- 「ASP3統合下でC5がdeafか」は**blob世代を揃えるまで測定不能**（現blobではWi-Fi initが完走しない）．

### 恒久修正＝blob世代整合（ユーザー決断待ち・本ラウンドは未実装）
C5 Wi-Fi統合をIDF v6.1(v9) blob世代へ移行する必要．os_adapter/wifi_initのCグルーをv9 ABIへ再移植
（`wifi_osi_funcs_t`に+4フィールド，drop/rename API `esp_wifi_skip_supp_pmkcaching`，coexist `printf`等）＝
実質C5 Phase Bのやり直し規模（実施08でhal v8↔IDF v9のABI非互換を確認済＝部分swap不可）．

### 検証・変更
- 実機JTAG読取りのみ（C5#1ハング機・C5#2 stock参照）．ソース変更・コミット無し（本doc実施09の追記のみ）．ボードlatch無し・電源断不要．

---

## 実施10：C5 Wi-Fi統合をIDF v6.1(v9)へ移行——PHY非互換を根本解消．残るは esp_wifi_init_internal のソフト失敗

### 移行（ユーザー決定：IDF v6.1 v9）
実施08/09で確定した「hal v8 blobがeco2 C5非対応」の恒久修正として，C5 Wi-Fi/PHYを
**ESP-IDF v6.1（v9・os_adapter 0x09．`/home/honda/tools/esp-idf-v6.1`）へ移行**．
- **minimal-IDF方式**：IDFはblob直結の`esp_wifi`/`esp_phy`/`esp_coex`（headers＋Cソース＋blob＋
  ROM ld＋phy_init_data＋v9 os_adapter）のみ．register/support層（`esp_hw_support`/`soc`/`hal`/
  `esp_rom`/`mbedtls`/`wpa_supplicant`）はhalのまま（同一eco2 C5でABI互換）．IDF全体をhal基盤へ
  被せるとmbedtls 4.0→4.1再編・`esp_attr.h`・ROM/soc header衝突が連鎖するため，この分割が最適．
- 参考：`~/TOPPERS/esp32_s3`のblobは実はv8（hal同一）でv9テンプレートではなかったが，
  「hal Cグルー＋整合blob」の切り分けは確認できた．
- **v9 ABI差分の解消**：`wifi_osi_funcs_t`にC5の5フィールド追加（`_wifi_bb/mac_sleep_retention_
  attach/detach`＋HEゲートの`_wifi_disable_ac_ax`）＋旧C5ゲートの`_regdma_link_set_write_wait_
  content`/`_sleep_retention_find_link_by_id`をno-opスタブ．drop API `esp_wifi_skip_supp_pmkcaching`＝
  weakスタブ(0)．libcoexistの裸`printf`＝weakスタブ．IDFの`esp_event.h`/`phy_init.c`が要求する
  `freertos/*.h`＝コンパイル専用スタブ新設（`portmacro`はcritical sectionを`esp_shim_int_disable/
  restore`へ）．

### 実機検証（C5 #1）＝«PHYの壁»突破を確定
- **ビルド・リンク・起動・v9 blob実行まで到達**．`g_wifi_osi_funcs._version=0x09`，blobはIDF v6.1由来．
- **実施08のstore fault・実施09のRX較正ハングとも消滅**＝**eco2シリコンのPHY非互換（hal v8 blob）が根本解消**．
- **新たな壁（ソフト・シリコンではない）**：`esp_wifi_init_internal`（v9 blob内部）が**エラー0x3001を返し
  失敗→rollback（wifi_deinit_internal）→RTC_SWDTリセット**．到達点は「pp/net80211 rom versionの後，
  `esp_supplicant_init`/`esp_phy_enable`より前」＝**os_adapter/リソース系のどれかをblobが拒否**．
  エラーテキストはASP3 syslogのFIFO詰まりで文字化け中（JTAG bp/UART捕捉で挙動確定）．

### 申し送り（次段）
- `esp_wifi_init_internal`失敗の切り分け：UART経由でエラーテキストを可読化，またはosiラッパ
  （task/queue/semaphore/timer生成・v9 sleep-retention hook）を既知良v9と対比してどの呼出しが
  失敗か特定．＝«v9シムの詰め»（扱いやすいソフト段階）．
- deaf-RX本丸判定は`esp_wifi_init`完走後（scan到達後）＝この失敗を越えれば到達．

### 変更ファイル（エディタブル層のみ・WIPコミット）
- `esp_wifi.cmake`（移行本体）・`wifi/esp_wifi_adapter.c`（v9構造体）・`wifi/esp_shim_blobglue.c`
  （pmkcaching/printfスタブ）・`wifi/freertos_stub/freertos/{FreeRTOS,task,queue,portmacro}.h`（新設）＋本doc．

---

## 別PC再開メモ（2026-07-10 区切り時点）

**再開点＝commit `45f7532`（本branch `claude/c6-wifi-c5-dev-5vc6x9`，push済み）**。次にやること＝
実施10「申し送り」＝`esp_wifi_init_internal`が返すエラー **0x3001** の切り分け（どのosi呼出しをblobが
拒否するか）→修正→`esp_wifi_init`完走→scan→AP＝deaf-RX本丸判定．

### 別PCで揃える前提
- **ESP-IDF v6.1 を `~/tools/esp-idf-v6.1` に配置**（移行必須）。`esp_wifi.cmake:85` が
  `set(IDF /home/honda/tools/esp-idf-v6.1)` と**ローカルパス直書き**．別パスなら同行を書換えるか
  `-DIDF=<path>` 化する（TODO：将来環境変数/キャッシュ変数へ）。IDF v6.1 の
  esp_wifi/esp_phy/esp_coex/lib/esp32c5 blob（v9・os_adapter 0x09）が必要．
- ツールチェーン：xpack `riscv-none-elf-gcc` 15.2（`~/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin`），
  CMakeは `-DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf-`（memory `env-esp32c3-toolchain`）．
- submodule：`asp3/asp3_core` は **ef1f1c8**（`origin/feat/esp32c6` にpush済＝fetch可．CLIC例外修正
  実施07含む）．`hal`（esp-hal-3rdparty b90b183）は公開repo．`git submodule update --init --recursive`．
- 実機：C5 #1（DUT）=JTAG MAC `d0:cf:13:f0:a7:44`・UART `b04e3bcf…`／C5 #2（stock v9参照）=
  `d0:cf:13:f0:c8:94`・`3e7bd19f…`．**番号でなくMACで照合**（`~/usb_status.md`）．触れない機＝
  S3-A `f4:12:fa:5b:40:2c`／P4-A `30:ed:a0:ea:98:0e`．esptool＝idf6.1 venv v5.3.1，OpenOCDは
  `-c "adapter serial <MAC>" -f board/esp32c5-builtin.cfg`．

### 未コミットのローカル診断（別PCには転送されない＝必要なら再作成）
- `apps/wifi_scan/wifi_scan.{c,cfg,h}` のDEF_EXC fault捕捉ハンドラ（RTC-RAM `0x50000000`，magic
  `0xFA017C05`．実施06/08で実装．CPU例外を1発捕捉して凍結）．今回の0x3001は«ソフト失敗»で
  CPU例外ではないため本ハンドラは発火しない＝次段はosiラッパ計装/UART可読化で追う．
- submodule `asp3_core` の `kernel/time_event.c`（C6由来の"no time event"抑制．C5に無関係・非コミット）．

### ビルド確認コマンド（wifi_scan＋v9移行）
```
export PATH="$HOME/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"
cmake -S asp3/asp3_core -B build/c5_idf61 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake -DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf- \
  -DASP3_TARGET=esp32c5_espidf -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c5_espidf \
  -DESP32C5_WIFI=ON -DESP32C5_CONSOLE=usbjtag \
  -DASP3_APPLDIR=$PWD/apps/wifi_scan -DASP3_APPLNAME=wifi_scan
cmake --build build/c5_idf61
```

## 実施11：`docs/c5-wifi-v9-0x3001-plan.md`候補1を実機で確認・修正——0x3001は解消，しかしesp_wifi_init_internal完了前に新規ハングを発見（未解決）

### 背景

実施10の申し送り＝`esp_wifi_init_internal`が返すエラー0x3001（`ESP_ERR_WIFI_NOT_INIT`）の
切り分け。別セッションの静的レビューによる解決計画（`docs/c5-wifi-v9-0x3001-plan.md`）に従い，
副次修正（printf weakスタブ）→候補1の確認手順（機構の成立を反証先行で確認）→恒久修正→実機
検証，の順で実施した。

### 環境準備（別PCとの差分）

本セッションのマシンには計画書が前提とする環境（IDF v6.1本体・xpack riscv-none-elf-gcc）が
無かったため，以下を新規に整えた：
- **IDF v6.1**：`git@github.com:espressif/esp-idf.git`のタグ`v6.1-beta1`を
  `~/tools/esp-idf-v6.1`へshallow clone（`esp_wifi.cmake:85`のハードコードパスと完全一致）。
  blob submodule（`components/{esp_wifi,esp_phy,esp_coex}/lib`）を個別に`git submodule update
  --init --depth 1`で取得（本体cloneには含まれない）。
- **ツールチェーン**：xpack `riscv-none-elf-gcc`は用意できなかったため，既存の
  `riscv32-esp-elf-gcc`(14.2.0，Espressif版)で代替——`-DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`。
  問題なくビルド・リンクできた（ツールチェーンの選択自体は候補1/2の判定に影響しない）。
- **esptool**：`idf6.1 venv v5.3.1`は本マシンに無かったため，既存のIDF v5.5用venv
  （`/home/honda/tools/python_env/idf5.5_py3.12_env`）のesptool（v4.12.dev3）で代替。
  引数形式がハイフンでなくアンダースコア区切り（`--before usb_reset`・`write_flash`）である点に
  注意（バージョン差）。
- **実機**：接続時に**C5#1（DUT）ではなくC5#2（stock参照）が挿さっていた**ことを検出
  （MACアドレス`D0:CF:13:F0:C8:94`で判別）。C5#1（`D0:CF:13:F0:A7:44`）が追加接続されるまで
  DUTへの操作は待機した。

### 副次修正：printf weakスタブの実装バグ

`asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`の`printf()`weakスタブが，コメント
（「diagnostic可視化のためsyslogへ折り返す」）に反して**`format`引数を無視し`return 0`する
だけのno-op**だった。`vsnprintf`＋`syslog(LOG_NOTICE, "%s", buf)`（C3/C6の
`log_writev_wrapper`と同じ確立済みパターン）へ実装し直した。

### 候補1の確認（修正より先に，反証先行）

1. **機構確認（grep）**：IDF v6.1の`components/esp_wifi/include/`を`grep -rn 'CONFIG_SOC_'`
   した結果，`esp_wifi/esp_phy/esp_coex`が参照する`CONFIG_SOC_*`は
   `WIFI_HE_SUPPORT`・`WIFI_SUPPORT_5G`・`WIFI_MAC_VERSION_NUM`・`WIFI_SUPPORTED`・
   `WIFI_ENABLED`・`WIFI_NAN_SUPPORT`・`IEEE802154_SUPPORTED`の7件。
   `components/esp_wifi/include/esp_private/wifi_os_adapter.h:162`で
   `_wifi_disable_ac_ax`フィールドが実際に`#if CONFIG_SOC_WIFI_HE_SUPPORT`でガードされている
   ことを確認——**計画書の前提「v9ヘッダの構造体定義自体がガードされている」は成立**。
2. **サイズの直接確認**：`CONFIG_SOC_WIFI_HE_SUPPORT`未定義（現行sdkconfig.h，0件）でビルドした
   `g_wifi_osi_funcs`は`nm -S`で**0x1f8（504バイト）**。`-DCONFIG_SOC_WIFI_HE_SUPPORT=1`を
   追加した再ビルドでは**0x1fc（508バイト）**——**4バイト増加を実測**し，計画書の判定基準
   （4バイト以上差＝機構成立）を満たした。**候補1は実機・実測で確定**。

### 副産物：既存コードのバージョン不整合を発見・修正（v8/v9混在）

候補1修正のビルド過程で，`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`が
`_wifi_apb80m_request`/`_wifi_apb80m_release`という**wifi_osi_funcs_tに存在しないフィールド**
（コンパイルエラー）を初期化子に持っていることが判明。IDF v6.1の別タグ`v6.1-dev`
（`ESP_WIFI_OS_ADAPTER_VERSION=0x08`）にはこの2フィールドが存在するが，`v6.1-beta1`
（`=0x09`，実機で使うblob世代）では削除されていることを確認——**実施10のv9移行時，v8由来の
フィールドが誤って残存していた見落とし**と判明。該当2行を削除（wrapper関数自体は残置，
未使用関数警告のみで実害なし）。既存のv9固有フィールド（`_wifi_bb/mac_sleep_retention_*`・
`_wifi_disable_ac_ax`）は既に正しく設定されていることも確認した。

### 修正の実装

`sdkconfig_stub/sdkconfig.h`に，esp_wifi/esp_phy/esp_coexが参照する`CONFIG_SOC_*`全7件を
`hal/components/soc/esp32c5/include/soc/soc_caps.h`の実値どおりミラー追加した（計画書の
指示「1件だけ足して終わりにしない」に従い恒久対策として一括追加）：
`WIFI_SUPPORTED=1`・`WIFI_ENABLED=1`（対応するSOC_*capsが無く,Wi-Fi有効ビルドのため1と判断）・
`WIFI_HE_SUPPORT=1`・`WIFI_SUPPORT_5G=1`・`WIFI_MAC_VERSION_NUM=3`・`WIFI_NAN_SUPPORT=1`・
`IEEE802154_SUPPORTED=1`。本番ビルドで`g_wifi_osi_funcs`が期待通り0x1fcバイトになることを
再確認した。

### 実機検証（C5#1，DUT）

修正済みイメージを書込み・起動。**候補1修正の効果を確認**：修正前に即座に返っていた
0x3001失敗パターンは再現せず，`esp_wifi_init`が実際に呼ばれてPHY/blob初期化が進行した
（printf修正のおかげで`I (36) pp: pp rom version: ...`・`net80211 rom version: ...`が
syslog経由で出力されることを確認——修正前は完全に無音だった診断出力が可読化された）。

**しかし新たな症状＝esp_wifi_init_internal完了前の永久ハングを発見**：
- コンソール出力はrom versionメッセージの後で完全に停止し，計画書が記載する
  「0x3001失敗→rollback→RTC_SWDTリセット」というリブートループは**発生しない**
  （45秒間の継続監視でリセットもリブートも起きず，同じ状態のまま静止）。
- JTAGでPC確認：`resume`を挟んで複数回サンプルした結果，PCは`dispatcher_1`
  （カーネルの正常アイドルループ，`0x4202134c`/`0x42021350`の2命令間）に完全に収束——
  クラッシュではなく**「実行可能なタスクが1つも無い」正常アイドル状態**と判明
  （`_kernel_p_schedtsk`/`_kernel_p_runtsk`とも0）。
- タスク状態を直接確認：TCBテーブルの動的生成タスク2件（両方とも`esp_shim_task_entry`
  トランポリン経由，`nm`で確認）のうち，1件は`tstat=0x20`＝**TS_WAITING_RDTQ（データキュー
  受信待ち）**，もう1件は`tstat=0x00`＝**TS_DORMANT（休止状態）**。実行可能状態
  （TS_RUNNABLE）のタスクは存在しない。
- **副次的に確立した手法**：計画書が提案していた「syslog_buffer[]をJTAGで直読みしログ原文を
  復元する」手法を実装・検証した。`SYSLOG`構造体（`t_syslog.h`，32バイト＝logtype 4B +
  logtim 4B + logpar[6] 4B×6）のレイアウトに基づき，`mdb`で取得した生バイナリを
  Pythonでパースし，`logpar[0]`（フォーマット文字列ポインタ，`vasyslog.c`の
  `LOG_TYPE_COMMENT`規約）をELFから逆引きしてメッセージを人間可読化することに成功
  （"System logging task is started on port %d."等，8件のリングバッファエントリを復元）。
  ただし`%s`引数（`printf`スタブが積むローカルバッファのアドレス）は後続の呼出しで
  上書きされ得るため，タイミングによっては内容が失われる（今回はentry[5]/[6]の
  "net80211 rom version: 78a72e9d5"は復元できたが，entry[2]/[3]は既に上書き後で読めなかった）。

### 判定：候補1は確認・修正済み（0x3001は解消）。ただし0x3001の解消により露見した，別の新規ハングが未解決で残る

**候補1（`CONFIG_SOC_WIFI_HE_SUPPORT`等の欠落によるwifi_osi_funcs_tサイズ/オフセットずれ）は
実機・実測で機構を確認し，修正により実際に0x3001即時失敗は解消した——候補1は確定・解決とする。**

しかし0x3001が解消されたことで露見した，**新しい種類の詰まり**（`esp_wifi_init_internal`完了
前の，リブートを伴わない永久ハング。1つのタスクがデータキュー受信待ち，もう1つが休止状態）は
未解決のまま残る。これは計画書の候補2（coexアダプタのv9追随漏れ）・候補3
（sleep-retentionスタブ戻り値）とは異なる新しい症状であり，別途の切り分けが必要。

### 申し送り（次段）

1. **タスクIDの正確な同定**：TCBテーブルの2件（tinib@`0x4206cefc`,exinf=0／
   tinib@`0x4206cf14`,exinf=1）が具体的にどのタスク（wifiドライバタスク／
   ログタスク／その他）に対応するか，`esp_shim_task_create`の呼出し履歴
   （syslogの"esp_shim: task '%s' -> tskid %d"ログ，tskid=1が記録済み）と
   突合せて特定する。
2. **データキュー受信待ちタスクが待っている相手の特定**：`TS_WAITING_RDTQ`状態のタスクが
   どのデータキューID（`p_winfo`経由）を待っているか，そのデータキューへ本来送信するはずの
   タスク（休止状態のタスク？）が何故送信せず休止したのかを追う。
3. **JTAGブレークポイントでの前進的な追跡**：`esp_wifi_init_internal`内部の呼出し列
   （coex_pre_init／esp_supplicant_init／esp_phy_load_cal_and_init等）に順次ブレークポイントを
   置き，rom versionメッセージの後，具体的にどの関数呼出しの後でタスクがブロックされたのかを
   特定する。
4. 候補2（coexアダプタのv9追随）・候補3（sleep-retentionスタブ戻り値）は，新しいハングの
   原因究明の過程で該当しそうであれば併せて確認する。

### 変更ファイル

- `asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`：printf weakスタブの実装
  （vsnprintf+syslog折り返し）。
- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：v8由来の
  `_wifi_apb80m_request`/`_wifi_apb80m_release`初期化子を削除（コメント化，wrapper関数は残置）。
- `asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h`：`CONFIG_SOC_*`全7件を
  soc_caps.h実値どおり追加。
- 本doc。

### 検証

- ビルド：`cmake --build build/c5_idf61`エラーなし成功（警告のみ，既知の暗黙宣言警告等で
  候補1/2判定とは無関係）。
- サイズ実測：`nm -S`で`g_wifi_osi_funcs`が修正前0x1f8→修正後0x1fc（+4バイト，期待通り）。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：書込み・起動・JTAG確認。0x3001即時失敗は再現せず。
  新規ハングをJTAG（PC・TCB tstat直読み）で確認。

## 実施12：実施11の新規ハングの原因特定・修正——第2の欠落フィールド（`_wifi_pm_sleep_lock_acquire/release`）／さらにその先でPHY校正ループの未解決ハングを新規発見

「続けて」の指示を受け，実施11で発見された「reboot無しの永久ハング」の根本原因調査を継続。
`docs/wifi-shim-c6.md`のJTAG手法（PC複数回サンプリング・ブレークポイントでの構造的確認）を
そのまま流用した。

### 原因究明の経路（JTAG，C5#1）

1. 実施11の「`dispatcher_1`アイドルループへの収束」は事実だが，**そこへ至った経路**を
   `ext_ker`（カーネル終了）にブレークポイントを張って確認したところヒットし，
   `ra=0x420215de`＝`core_exc_entry_1`内部——すなわち**未処理CPU例外がカーネルを
   正規のシャットダウン手続きで終了させていた**と判明（データキュー待ちのデッドロックでは
   なかった，実施11の解釈を訂正）。
2. `core_exc_entry_1`のディスアセンブルで，`mcause`を12bitマスクして`_kernel_exc_table`
   （関数テーブル，全エントリが`_kernel_default_exc_handler`の同一アドレス）へ分岐する
   構造を確認。`_kernel_default_exc_handler`のエントリ（`0x42023a22`）へブレークポイントを
   置き直し，ヒット時の`a1`（例外コード）＝**1＝Instruction access fault**を確認
   （OpenOCD自身の`Halt cause (1) - (PMP Instruction access fault)`表示とも一致）。
3. `asp3/asp3_core/arch/riscv_gcc/common/core_kernel.h`の`T_EXCINF`構造体定義から
   `pc`（保存済みmepc）のオフセット＝52バイトと算出し，例外時の`a0`（`p_excinf`ポインタ，
   `0x4084514c`）経由でJTAGから直接読み出した結果，**保存されたmepc＝`0x00000000`**——
   すなわち**NULL関数ポインタ呼び出し**がフォールト原因と確定。
4. 同じ`T_EXCINF`から`ra`（呼出し元の戻りアドレス）＝`0x4202eaaa`を取得し，`addr2line`で
   `wifi_hw_start`内と判明。逆アセンブルで直前の`jalr a5`（`0x4202eaa8`）を確認：
   `a5 = g_osi_funcs_p[0]->(offset 0xC8)` を経由した間接呼出しだった。
5. `wifi_os_adapter.h`（IDF v6.1-beta1，`ESP_WIFI_OS_ADAPTER_VERSION=0x09`）の
   `wifi_osi_funcs_t`をフィールド順にオフセット計算し，オフセット0xC8（200バイト，
   4バイトポインタとして50番目のフィールド）＝**`_wifi_pm_sleep_lock_acquire`**と同定。

### 根本原因

`_wifi_pm_sleep_lock_acquire`/`_wifi_pm_sleep_lock_release`（`void(*)(void)`ペア）は，
v9の`wifi_os_adapter.h`では実施11で扱った`_wifi_apb80m_request`/`_wifi_apb80m_release`
（v8のみに存在，v9で削除）と**同じ構造体スロット**（`_dport_access_stall_other_cpu_end_wrap`の
直後，`_phy_disable`の直前）に位置する**別フィールドへの置換**であって単純削除ではなかった。
実施10（v9移行）でapb80m側の削除は正しく行われたが，この置換後継フィールドの追加が漏れて
おり，`g_wifi_osi_funcs`の当該フィールドがNULLのまま残っていた。blobの`wifi_hw_start()`が
無条件でこの関数ポインタを呼び出すため，NULL呼出し→PMP Instruction access fault→
`_kernel_default_exc_handler`→`ext_ker()`→カーネル停止（`dispatcher_1`アイドル収束）
という経路でハングしていた。

### 修正

`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：他のPM/sleep-retention系フィールド
（`_wifi_bb_sleep_retention_attach`等）と同じ方針で，本リポジトリはESP-IDFの動的クロック/
スリープPMサブシステムを実装しないため，no-opスタブ
（`wifi_pm_sleep_lock_acquire_wrapper`/`_release_wrapper`，共に空実装）を追加し，
正しい構造体スロットの初期化子として登録した。

### 実機検証：この修正でNULL呼出しは解消——しかしさらに先で新たな未解決ハングを発見

- `nm -S`で`g_wifi_osi_funcs`のサイズが0x1fcのまま不変であることを確認（NULLフィールドを
  埋めただけで構造体サイズには影響しない，想定通り）。
- ビルド成功（既存の未使用関数警告のみ，実害無し）。
- 実機（C5#1）に書込み・起動。**`_kernel_default_exc_handler`へのブレークポイントは
  もうヒットしない**——実施11のNULL呼出しハングは解消したことをJTAGで確認。
- PCを複数回サンプリング（`resume`→`sleep`→`halt`を反復）した結果，PCは
  `phy_get_pkdet_data`・`phy_iq_est_enable_new`・`phy_abs_temp`の間を行き来しており，
  **PHY/RF校正シーケンスが実際に進行中**であることを確認（実施11時点では到達していな
  かった段階）。
- しかし**90秒以上・複数回のサンプリングで同じ狭い範囲（`phy_iq_est_enable_new`とその
  被呼出し関数群）から一切抜け出さない**ことを確認。`phy_iq_est_enable_new`を逆アセンブル
  したところ，MMIOレジスタ`0x600a047c`のbit16をポーリングし，そのビットがクリアされない
  限り**関数から一切脱出できない構造**（カウンタ比較分岐があるように見えるが，カウンタ
  超過時も無条件で同じループ先頭へ`j`しており，実質的な打ち切り経路が存在しない）と判明。
- **反証実験（`feedback_hardware_investigation_rigor.md`の鉄則に従い実施）**：
  同関数の`ret`命令アドレス（`0x4202884e`）にハードウェアブレークポイントを置き，
  `reset halt`から100秒間`resume`——**一度もヒットしなかった**。手動`halt`で確認したPCも
  依然として同じループ内（`phy_get_pkdet_data`内，呼出し元は`phy_iq_est_enable_new`）。
  これにより「たまたま遅い正規の一回限り校正」ではなく，**100秒以上継続する真の無限
  リトライループ**であることをPCサンプリングだけでなくブレークポイント不着火という
  構造的手法でも確認した。

### 判定：実施11のハング原因（NULLポインタ呼出し）は確定・修正済み。ただしそれにより到達した，さらに新しい種類の未解決ハング（PHY/RF校正の無限リトライループ）が残る

このPHY校正ループの停止条件（MMIO `0x600a047c` bit16）が何故クリアされないかは**未調査**。
現時点でわかっているのは症状（無限ループ・停止条件のレジスタアドレスとビット位置）のみで，
原因（校正に先立つ別の初期化ステップ欠落／クロック・アナログフロントエンドの状態異常／
真のハードウェア不具合，等）は特定していない。C6 deaf-RX調査とは**別チップ・別ブロブの別
症状**であり，安易に同一原因と結びつけないこと（`feedback_hardware_investigation_rigor.md`）。

### 変更ファイル（実施12追加分）

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：
  `_wifi_pm_sleep_lock_acquire`/`_wifi_pm_sleep_lock_release`のno-opスタブを追加し，
  正しい構造体スロットに登録。
- 本doc。

### 申し送り（次段）

1. **PHY校正ループの停止条件調査**：`0x600a047c`のbit16が何を意味するレジスタか
   （PHYレジスタマップ・ROMシンボル・他ターゲット(C6/C3)の同等コードとの比較）を特定し，
   校正が完了しない理由を追う。C6のAGC/RF調査（`docs/wifi-shim-c6.md`）で確立した
   JTAG計装手法（`wifi_regsnap`等）が転用できないか検討。
2. `esp_phy_load_cal_and_init`等，このループの手前で呼ばれるべき初期化（PHY較正データの
   ロード等）が正しく行われているか確認する（sdkconfig_stubの较正関連CONFIG_*漏れの
   可能性も含む）。
3. 候補2（coexアダプタのv9追随漏れ）・候補3（sleep-retentionスタブ戻り値）は，本ハングとは
   別症状であるため優先度を下げる（今回のハングが解決してから改めて確認）。
4. C5#2（stock参照，`D0:CF:13:F0:C8:94`）は本ラウンドも一切接続・操作せず（未接触）。

### 検証（実施12）

- ビルド：`cmake --build build/c5_idf61`エラーなし成功。
- サイズ実測：`nm -S`で`g_wifi_osi_funcs`＝0x1fcのまま不変（NULLフィールドを埋めただけ，
  期待通り）。
- 実機（C5#1）：書込み・起動。実施11のNULL呼出しハング（`_kernel_default_exc_handler`
  ブレークポイント）は再現せず＝解消を確認。新規のPHY校正無限ループを，PCサンプリング＋
  ブレークポイント不着火（100秒）の両方で構造的に確認。
- C5#2（stock参照）：本ラウンドも一切接続・操作せず（未接触を確認）。
- C5#2（stock参照，`D0:CF:13:F0:C8:94`）：本ラウンドは一切接続・操作せず（未接触を確認）。

## 実施13：PHY校正ループの原因＝WIFIBBクロックのICGゲート（PMU icg_modem.code=0）——反証実験で因果確認し修正．BBは可制御になったが完了ビットは依然立たず（別要因が残存）

「続けて」「進めて」の指示で実施12の残課題（`phy_iq_est_enable_new`の無限ループ）を継続調査。

### ★実施12の記述の訂正（極性の誤り）

実施12は「MMIOレジスタ`0x600a047c`のbit16をポーリングし，**そのビットがクリアされない限り**
脱出できない」と記載したが，**これは逆で誤り**。逆アセンブルを読み直した結果：

```
lw   a5,1148(s1)      ; s1=0x600a0000 → 0x600a047c
slli a4,a5,0xf        ; a4の符号ビット = a5のbit16
bgez a4, <loop body>  ; bit16==0 のときループ本体へ  ← 0で回り続ける
<fallthrough>         ; bit16==1 で ret             ← 1で脱出
```

正しくは「**bit16が“立つ”のを待っている**（done/valid系ビット）」。追記であり実施12の
結論（無限ループであること・ブレークポイント不着火100秒）自体は変わらないが，
以降の推論はこの正しい極性に基づく。

### レジスタ空間の同定

`0x600A0000`＝`DR_REG_MODEM0_BASE`（`soc/esp32c5/register/soc/reg_base.h`）。OpenOCDが
最近傍シンボルとして表示する`PCR+0xa47c`は**無関係な誤誘導**（PCRのシンボルが手前に
あるだけ）。後述の反証実験により，この`MODEM0`ブロックが**Wi-Fi BB（ベースバンド）**の
レジスタ空間であることを実測で確定した。

- `MODEM0+0x450` bit1 … IQ推定の**起動（start）**ビット（blobがclear→delay→setとパルス）
- `MODEM0+0x47C` bit16 … 対応する**完了（done）**ビット（ループはこれを待つ）

### 決定的な観測：BBレジスタへの書込みが一切効かない

halt状態で`MODEM0`の各レジスタへ書込み→読み戻しを試すと**値が変わらない**（`0x450`,
`0x448`, `0x470`いずれも）。一方，読み出しは非ゼロ値を返す（例：`0x448`=`0x16100000`）。

ここで**2段階の対照実験**を行い，安易な「ブロックが死んでいる」という結論を避けた：

1. **JTAG書込み自体は生きているか**（対照）：`MODEM_LPCON`(`0x600af008`)・
   `MODEM_SYSCON`(`0x600a9c08`)への書込みは**成立**（書換え→読み戻し一致→復元）。
   よってJTAGのペリフェラル書込み経路は正常。`MODEM0`だけが書込みを受け付けない。
2. **CPUからの書込みでも同じか**（JTAG/APM由来のアーティファクト排除）：JTAG書込みが
   効かないことは，APM/TEE等が**JTAG起点の書込みだけ**を遮断している可能性を排除できない
   （C6調査でCodexが挙げた未検証仮説）。そこでレジスタ`fp`/`a5`/`pc`を操作して**CPU自身に
   既存の`sw a5,1104(fp)`命令を1命令だけ実行させる**手法で検証した。
   まず陽性対照として`MODEM_LPCON+0x8`へCPU storeを実行→`0x310`→`0x314`に**変化**（手法は有効）。
   同じ手法で`MODEM0+0x448`へCPU storeを実行→**変化せず**。
   ⇒ **CPU起点でも`MODEM0`書込みは無視される**＝JTAG起点のアーティファクトではない。

### 原因の同定：WIFIBBクロックがICGでゲートされている

`MODEM_SYSCON_CLK_CONF_FORCE_ON`(`0x600a9c08`)にビットを立てると`MODEM0`が書込み可能に
なることを発見。ビットを個別に試して**`bit0`＝`MODEM_SYSCON_CLK_WIFIBB_FO`単独で十分**と
判明（＝`MODEM0`はWi-Fi BBであり，止まっていたのはWIFIBBクロック）。

しかし`MODEM_SYSCON_CLK_CONF1`(`0x600a9c14`)＝`0x003be7ff`で，`CLK_WIFIBB_*_EN`（bit0-8）・
`CLK_WIFIMAC_EN`(bit9)・`CLK_WIFI_APB_EN`(bit10)は**すべて1**。つまり
「enableビットは立っているのにクロックが動いていない」状態であり，その上位にある
**ICG（internal clock gating）ゲート**が閉じていた：

- `MODEM_SYSCON_CLK_CONF_POWER_ST`(`0x600a9c0c`)＝`0x64646400`
  → `CLK_WIFI_ST_MAP`（bit23:20）＝`0x6`＝`BIT(1)|BIT(2)`
  （＝ICGコードが1か2のときだけWi-Fi系クロックをungate）
- `PMU_HP_ACTIVE_ICG_MODEM_REG`(`0x600b000c`)のcode（bit31:30）＝**0**
  → マップ`{1,2}`に含まれない ⇒ **WIFIBBクロックはゲートされたまま**

Direct Bootでは`pmu_init()`等が動かないためこのcodeが0のまま残る。まさに
`esp_wifi_adapter.c`に存在しながら**無効化されていた**`esp_shim_modem_icg_init()`
（`pmu_ll_hp_set_icg_modem(pmu, HP_ACTIVE, 2)`＋PMUの即時反映パルス）が行う処理である。
この無効化はC6での実測（C6ではICG初期化は冗長）を**C5に未検証のまま踏襲**したもので，
コード上も`【実機確認待ち】`と明記されていた。その仮定が誤りだったことになる。

### 反証実験（A/B/A/B）による因果の確認

`CLK_CONF_FORCE_ON=0`（強制ONは使わない）・`CLK_CONF_POWER_ST`は元値`0x64646400`のまま
固定し，**`icg_modem.code`だけを0↔2でトグル**して，都度BBレジスタ`0x600a0448`への書込み
成否を確認した（毎回PMUの即時反映パルス2本を打つ）：

| 手順 | icg code | 書いた値 | 読み戻し | 判定 |
|---|---|---|---|---|
| A1 | 0 | `0x11110000` | `0x04440000`（不変） | 書込み無視 |
| B1 | 2 | `0x22220000` | `0x02220000` | **書込み成立** |
| A2 | 0 | `0x33330000` | `0x02220000`（不変） | 書込み無視 |
| B2 | 2 | `0x44440000` | `0x04440000` | **書込み成立** |

（上位ニブルは一部RO。A/B/A/Bで再現性を確認＝相関ではなく因果）

**適用にはPMUの即時反映パルスが2本とも必要**：`PMU_IMM_MODEM_ICG_REG`(`0x600b00dc`)bit31
（`update_dig_icg_modem_en`）と`PMU_IMM_SLEEP_SYSCLK_REG`(`0x600b00d0`)bit28
（`update_dig_icg_switch`）。**codeを書くだけ／片方だけパルスした場合は反映されない**
ことも実測した（最初この片方漏れのため一度「ICG仮説は棄却」と誤判定しかけた）。
`esp_shim_modem_icg_init()`はこの2本を正しく打っている。

### 修正と実機検証

`wifi_clock_enable_wrapper()`内の`(void) esp_shim_modem_icg_init;`（無効化）を
**`esp_shim_modem_icg_init();`（有効化）に変更**（本ラウンドの変更はこの1件のみ＝
切り分け可能性を保つというユーザ指示に従う）。ビルド成功・C5#1へ書込み・JTAG確認：

- `PMU_HP_ACTIVE_ICG_MODEM_REG`＝`0x80000000`（code=2）＝**ファームウェアが正しく適用**。
  `CLK_CONF_FORCE_ON`＝`0`（強制ONに頼っていない）。
- **`MODEM0+0x450`が`0x00000000`→`0x00102003`に変化**＝修正前は永久に0だったBB制御
  レジスタに，**blobの書込みが実際に届くようになった**（起動ビットbit1も立っている）。
  実行中のJTAG書込みテストでもBBは書込み可能。
- ⇒ **ICG修正は本物であり必要**（BBが可制御になった）。

### しかし完了ビットは依然立たない＝必要条件だが十分条件ではない

`MODEM0+0x47C` bit16は修正後も`0`のままで，`phy_iq_est_enable_new`のループは継続
（PCサンプリングで同ループ内を確認．リビルドでアドレスは`+0x80`ずれるが，逆アセンブルで
**同一構造の同一ループ**であることを確認済み＝アドレス変化に騙されていない）。

**本ラウンドで棄却した候補**：
- **FE 20M/40M クロック欠落**：`CLK_CONF1`のbit11(`CLK_FE_20M_EN`)/bit12(`CLK_FE_40M_EN`)が
  0であることに着目し，実行中にJTAGで`0x003be7ff`→`0x003bffff`（両bitをON）にして
  resume→**ループから脱出せず，done bitも0のまま**。⇒ FEの20M/40Mクロック単独では
  原因ではない（棄却）。

### 判定

- **実施12の残課題の原因の一部を確定・修正**：WIFIBBクロックのICGゲート
  （PMU `icg_modem.code`=0）。反証実験で因果を確認し，`esp_shim_modem_icg_init()`有効化で
  BBが可制御化した。**これはC5移植の実バグであり，「C6で冗長だった」という未検証の
  踏襲が誤りだった**ことの実証でもある（`【実機確認待ち】`と明記していた箇所が的中）。
- **ただしPHY校正の完了ビットは依然立たず，ハングは未解決**。ICG以外にもう1つ以上，
  BB/FE/RFの初期化に欠落がある。

### 申し送り（次段）

1. `phy_iq_est_enable_new`が起動ビットを立てた後，**BBがIQ推定を完了できない理由**を追う。
   FE 20M/40Mは棄却済み。
   - ~~(a) ICG初期化のタイミングが遅すぎる~~ → **本ラウンドの実測で自己棄却**。ハング中の
     BBは書込み可能（実行中のJTAG書込みが成立・`0x450`＝`0x00102003`）＝IQ推定ループが
     回っている**最中にBBクロックは生きている**。呼出し位置の前倒しでは何も変わらない。
   - **(b) RF/PLL（regi2c経由）の較正未完了＝最有力**。IQ不均衡推定はアナログRF鎖
     （LO/ミキサ/ADC）が実際に動いてテストトーンを注入・測定できて初めて完了ビットが立つ。
     RF PLLがロックしていない／regi2c越しのRF較正が完了していないなら，起動ビットを
     立てても有効な測定結果が得られず`done`は永久に立たない。これはC6の根本原因修正
     （regi2cのI2C_MSTクロック）とも，本ラウンドの`FE_CFG`=0という観測とも整合する。
     **次段の判別実験＝ハング時点でregi2cが応答して正気な値を返すか／RF PLLがロックを
     報告しているかを確認する**。
   - (c) `MODEM_SYSCON_WIFI_BB_CFG`(`0x600a9c18`＝`0x10003802`)や
     `FE_CFG`(`0x600a9c1c`＝`0x00000000`)の設定欠落。

   **調査の打ち切り基準（先に決めておく）**：C5はそもそも「C6のdeaf-RXがC6固有か
   同世代モデム共通かを判別する」ために選んだ（`memory/project_c6_agc_investigation.md`）。
   したがって**(b)の確認でregi2c/RF較正が正常に動いていると分かった上でIQ推定が完了しない
   なら，それはC6と同種のアナログ壁を較正時点で再現したということであり，それ自体が答え**
   ＝「C6-generic」と結論して記録し停止する（85ラウンド級のレジスタ総当りに入らない）。
   逆にregi2c/RF較正が動いていないなら，それは修正可能な移植漏れなので直す。
2. ~~`modem_syscon_ll_set_modem_apb_icg_bitmap`が効いていない疑い~~ → **本ラウンド内で解決**。
   `CLK_CONF_POWER_ST`が`0x64646400`のまま（APBニブルが6→4にならない）理由は
   オフセット誤りではない：`modem_syscon_struct.h`の並び（`test_conf`/`clk_conf`/
   `clk_conf_force_on`/`clk_conf_power_st`）から`clk_conf_power_st`は確かに`+0xC`で，
   ビットフィールド定義（`clk_modem_apb_st_map`＝bit31:28）もレジスタマップと一致。
   C5のsyscon基底`0x600A9C00`（C6から+0x400）も正しい。
   実際には`esp_shim_modem_icg_init()`の**後**に走る`modem_clock_select_lp_clock_source()`／
   `wifi_module_enable()`（modem_clock側）が`POWER_ST`を再設定して上書きしているだけ。
   A/B実験でこのマップ書換えは不要と確認済みであり，**実害なし・対応不要**。
3. `0x600a9c1c`（`FE_CFG`）が0であることの妥当性を，IDF側の初期化コードと突合せる。

### 変更ファイル（実施13）

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：`wifi_clock_enable_wrapper()`で
  `esp_shim_modem_icg_init()`を有効化（無効化コメントを実測結果に基づき置換）。**変更はこの1件のみ**。
- 本doc（実施12の極性誤りの訂正を含む）。

### 検証（実施13）

- ビルド：`cmake --build build/c5_idf61` エラーなし成功。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：書込み・起動・JTAG確認。
  - `icg_modem.code`=2 適用済み・`FORCE_ON`=0 を実測確認。
  - `MODEM0+0x450`：修正前`0x00000000`（永久） → 修正後`0x00102003`（blobの書込みが到達）。
  - A/B/A/B反証実験でICG codeとBB書込み可否の因果を確認。
  - FE 20M/40Mクロック仮説は実機介入で棄却。
  - 完了ビット`MODEM0+0x47C` bit16は依然0＝ハング未解決（次段へ申し送り）。
- C5#2（stock参照，`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触）。

---

## 実施14：判別実験——regi2c/クロック基盤はハング中も生存・応答している（regi2c破損ではない）が，RF PLLロックそのものは未公開レジスタのため確認不能＝「C6-genericか否か」は本ラウンドでは判定できず保留／★重大な方法論的発見：JTAG「reset halt」とnative USB-JTAGのRTSリセットは`MODEM_SYSCON`/`MODEM0`/PMUドメインをクリアしない（UARTブリッジ経由のRTSリセットのみ真にクリーンな再現に必要）

実施13の申し送り(b)「regi2c/RF較正（PLLロック）がハング時点で機能しているか」を実機で判別する回。

### 机上準備（`hal/`読み取り専用）

- regi2c前提クロック：`MODEM_LPCON_CLK_CONF`(`0x600af018`) bit2 `clk_i2c_mst_en`
  （`hal/components/hal/esp32c5/include/hal/regi2c_ctrl_ll.h`）と，
  `MODEM_SYSCON_CLK_CONF`(`0x600a9c04`，構造体`clk_conf`はsyscon先頭+0x4) bit12
  `clk_i2c_mst_sel_160m`（`regi2c_ctrl_ll_master_configure_clock()`が設定）が前提条件。
  ASP3側`esp_wifi_adapter.c`の`wifi_clock_enable_wrapper()`（650・793・813行台）は
  両方をすでに呼んでいる（`_regi2c_ctrl_ll_master_enable_clock(true)` +
  `regi2c_ctrl_ll_master_configure_clock()` + `0x600af018=0x7`明示書込み）＝机上では
  移植漏れなし。
- regi2cトランザクション実体：`hal/components/esp_rom/patches/esp_rom_hp_regi2c_esp32c5.c`
  （`regi2c_read_impl`等）で確定。`I2C_ANA_MST`base=`0x600AF800`
  （`I2C0/1_CTRL_REG`=+0x0/+0x4，bit25=busy，bits15:8=reg_addr，bits7:0=block/slave_id，
  bits23:16=読み戻しdata），`ANA_CONF0_REG`=+0x18（bit24=`I2C_MST_BBPLL_CAL_DONE`，
  bit2/3=BBPLL較正force high/low），`ANA_CONF1/2_REG`=+0x1C/+0x20（block選択マスク・
  MST_SEL）。公開ヘッダで名前が付いている regi2c block は **BBPLL(0x66)・BIAS(0x6a)・
  DIG_REG(0x6d)・ULP_CAL/BOD(0x61)・SAR_ADC(0x69) の5つのみ**——C6調査で確定した
  「約60KBの未公開regi2cアドレス空間」と同型の欠落がC5にも存在し，**WiFi RF専用の
  regi2c block（シンセサイザ/PA/LNA較正）を指す公開レジスタ名は存在しない**。
  つまりC6同様，「RF PLLがロックしているか」を直接名指しで読める公開レジスタは無い。
  代替指標として`BBPLL`(block 0x66，reg8: bit7=`OR_LOCK`・bit6=`OR_CAL_END`)を使う
  （C6実施7の手法を踏襲）。ただしBBPLLはCPU/AHBクロック生成用のディジタルPLLであり，
  WiFi RFシンセサイザそのものではない点に注意（`hal/components/esp_hw_support/port/
  esp32c5/rtc_clk.c`の`rtc_clk_bbpll_configure()`がCPU周波数設定時にこの較正を行う）。
- `FE_CFG`(`0x600a9c1c`)・`WIFI_BB_CFG`(`0x600a9c18`)：`hal/`内で明示的に書き込むのは
  スリープ復帰用regdmaリンク（`esp_hw_support/lowpower/port/esp32c5/sleep_modem_state.c`）
  のみで，非スリープの通常初期化パスではIDFドライバ自体が一切書かない
  （`FE_CFG`のPORデフォルトはヘッダ上も`32'h0`）。よって**申し送り(c)「FE_CFG=0の
  設定欠落」はASP3ポート側の移植漏れではない**——書くとすればWi-Fi blob内部の仕事で，
  IDF/HALレベルでは対応するコードが無いため机上では判定できない（棄却ではなく
  「本ポートの範囲では確認できる欠落なし」に格下げ）。

### ★重大な方法論的発見：この基板の「リセット」は種類によって効くドメインが違う

実機実験の前半で，同一ビルド（`ninja: no work to do`＝実施13から無変更）にも関わらず
「ループが即座に完了する（bit16=1）」という実施13と矛盾する結果に遭遇した。原因を
追った結果，**この観測結果は測定アーティファクトであり，リセット手段の選択ミスに
起因すると判明した**：

`hal/components/soc/esp32c5/include/soc/reset_reasons.h`の用語定義：
> CPU Reset: CPUコアのみ／Core Reset: RTCサブシステムを除く全ディジタル系／
> System Reset: RTCサブシステムを含む全ディジタル系／Chip Reset: アナログ含む全チップ

実機で確認した対応：

| リセット手段 | 観測されたreset cause | ドメイン | `MODEM_SYSCON_CLK_CONF`/`PMU_ICG_MODEM`/`MODEM0+0x47C`は消える？ |
|---|---|---|---|
| OpenOCD `reset halt`（JTAG） | `(24) JTAG CPU reset` | CPUのみ | **消えない**（前回boot の値が残存） |
| native USB-JTAG（ttyACM，esptoolデフォルト）のRTSリセット | `(0x15) USB UART resets…core (hp system)` | Core（RTC除く） | **消えない**（`MODEM_SYSCON_CLK_CONF`=`0x00201002`・`PMU_ICG_MODEM`=`0x80000000`が新規boot開始直後＝ROMベクタ到達時点で既に残存を実測） |
| **UARTブリッジ（CP2102N，ttyUSB1）のRTSリセット** | `(0x12=18) Super Watchdog resets core **and rtc**` | System（RTC含む） | **消える**（同時点で全て`0x00000000`を実測） |

実測の決め手：native USB-JTAGのRTSリセット直後，**まだ1命令もASP3コードが走っていない
ROMリセットベクタ（PC=`0x40001610`）の時点**で`MODEM_SYSCON_CLK_CONF`=`0x00201002`・
`PMU_HP_ACTIVE_ICG_MODEM_REG`=`0x80000000`(code=2)・`MODEM0+0x47C`=`0x00010000`
（doneビット**既に1**）を読んだ——これはASP3コードが今回のブートで設定した値では
あり得ず，**前回ブート（既にphy_iq_est完了・タスクidle化していたセッション）からの
残存値**であることが確定した。一方，UARTブリッジのRTSリセット直後は同じ3レジスタが
すべて`0x00000000`（真のPORデフォルト）で，このリセットのみがドメインを正しくクリアする。

**教訓（今後のC5実機ラウンドすべてに適用）**：`MODEM_SYSCON`/`MODEM0`/`PMU`まわりの
レジスタをJTAGで検証する実験では，**native USB-JTAGの`reset halt`／RTSリセットに
依存してはならない**。UARTブリッジ（ttyUSB1）側からのRTSハードリセットを使うか，
真のクリーン状態を都度ROMベクタ到達時点で確認すること。

### 実機実験（クリーンリセット確立後，C5#1のみ）

ビルドは実施13から無変更（`cmake --build build/c5_idf61`：`ninja: no work to do`）。
`riscv32-esp-elf-objdump`でループ構造を再確認——アドレスは実施13と同一
（`phy_iq_est_enable_new`のループ先頭`0x420288b2`／`ret`は`0x420288ce`，リビルドなしのため
シフトなし）。UARTブリッジ経由のRTSリセットでクリーンブートを確立後，OpenOCDで
loop-top(`0x420288b2`)とret(`0x420288ce`)にhw breakpointを張って捕捉する手法で，
**独立な2回のクリーンブート**それぞれで以下を確認：

1. **ハングは再現する**：doneビット(`MODEM0+0x47C`)=`0`のままloop-topで捕捉，以後
   resumeして12〜15秒待っても`ret`ブレークポイントは発火せず（PCは
   `phy_get_pkdet_data`/`phy_get_tone_sar_dout`呼出しを含むloop本体を巡回し続ける）＝
   実施13の恒久ハングが，真にクリーンな状態でも忠実に再現することを確認。
2. **regi2cクロック前提は生きている**：ハング中（loop-top停止時）に
   `MODEM_LPCON_CLK_CONF`=`0x00000007`（WIFIPWR/COEX/I2C_MST全bit1）・
   `MODEM_SYSCON_CLK_CONF`=`0x00201002`（bit12=`clk_i2c_mst_sel_160m`=1）を確認
   （2回のクリーンブートで再現一致）。
3. **regi2cバス自体は応答する**（クリーンブート1回目のみで実施。2回目は再現確認の
   対象を「ハング再現＋クロック前提値」に絞り，本トランザクションは再実施していない）：
   ハング中に`regi2c_read_impl`と同じプロトコルをJTAG手動re-play（`ANA_CONF1`に
   BBPLL用read maskを書込み→`I2C_ANA_MST_I2C1_CTRL_REG`に`(block=0x66,reg=8)`を
   書込み→busy(bit25)クリアを確認→data(bits23:16)読出し）で実行し，**busyが速やかに
   クリアし，非stuckな値`0x0C`が返った**（トランザクションが成立＝regi2cバスは
   ハング中も応答可能）。
4. **CPUクロック用BBPLLは較正済み・強制停止状態**：`I2C_ANA_MST_ANA_CONF0`
   (`0x600af818`)=`0x2100e444`——bit24(`I2C_MST_BBPLL_CAL_DONE`)=1（較正完了）・
   bit2(`STOP_FORCE_HIGH`)=1・bit3(`STOP_FORCE_LOW`)=0（強制停止状態）。これは
   `rtc_clk_bbpll_configure()`（起動早期のCPU=192MHz設定，実施03）が正常完了して
   いることを示す。ただし手順3で読んだreg8の値`0x0C`＝`OR_CAL_CAP`のみで
   `OR_LOCK`(bit7)=0・`OR_CAL_END`(bit6)=0——**これはCPUクロックPLLが「較正後に
   意図的にforce-stopされた」状態の読みであり，「ロックしていない」ことの証拠では
   ない**（force-stop中はロック検出コンパレータの出力が意味を持たない可能性が高い）。
   このBBPLLはWiFi RFシンセサイザそのものではないため，この結果はWiFi RF PLLの
   ロック状況について確定的なことは何も言わない。
5. **`FE_CFG`/`WIFI_BB_CFG`再確認**：ハング中も`FE_CFG`=`0x00000000`（不変）・
   `WIFI_BB_CFG`=`0x10003802`（実施13と同値，blob内部が設定）——机上調査の
   結論（(c)はASP3ポート側の欠落ではない）と整合。

### 判定：branch (a)「regi2c破損」は明確に棄却できるが，branch (b)「C6-generic」は立証できていない＝branch (c)「未確定のまま保留」が正しい着地

- **branch (a)「regi2c/RF較正経路が壊れている」（修正可能な移植漏れ）は棄却**：
  I2C_MSTクロックはハング中も生きており(`0x600af018`=`0x7`)，MODEM_SYSCONの
  クロック源選択も正しく(`bit12`=1)，**手動で発行したregi2cトランザクションは
  busyが速やかにクリアし非stuckな値を返した**（バス自体がstuck/deafではない）。
  ASP3側のregi2c関連初期化（`esp_wifi_adapter.c`のクロック有効化群）に机上・実機
  とも欠落は見つからなかった。
- **branch (b)「regi2cが正常応答しRF PLLもロックしているのにdoneビットが立たない」＝
  C6-genericは，立証できていない（早合点しないこと）**：branch (b)を名乗るには
  「RF PLLもロックしている」ことを積極的に確認する必要があるが，WiFi RF専用の
  regi2c blockを指す公開レジスタがC5にも存在しない（C6と同型の約60KB未公開
  アドレス空間）ため，これは**確認不能**だった。手動で読めたのはCPU/AHBクロック
  生成用のBBPLL（block 0x66）のみで，これはWiFi RFシンセサイザではない
  ——CPUが192MHzで走っている事実自体がBBPLLのロックを裏付けてはいるが，
  WiFi RF PLLについては何の情報にもならない。
  さらに重要な点として，**C6実施7の「regi2cバス生存・BBPLL正常ロック・しかし
  AP0個」という比較対象は，PHY較正が完了した後・実行時RXの段階での観測**
  （`docs/wifi-shim-c6.md`実施7：「esp_wifi_start()直後，PHY較正完了後の時点で」）
  であり，**C5は較正そのもの（`phy_iq_est_enable_new`）から抜け出せていない**。
  両者は失敗する「段階」が異なる（C5＝較正中に完了信号が来ない，C6＝較正は完了
  したのに実行時受信が成立しない）ため，「構造的に同型」という主張は失敗点の
  違いを覆い隠す。regi2cの**観測不能性（未公開レジスタの壁）**という点でのみ
  C6と同じ限界にぶつかっている，という以上のことは言えない。
- **総合判定＝branch (c)**：regi2c/クロック**基盤**はハング中も生存・応答しており，
  ASP3ポート側に特定できる欠落は見当たらない（これは確定）。しかし，
  「C5のIQ推定未完了がC6のdeaf-RXと同一の根本原因か」は，**RF PLLロックの
  直接確認手段が無いため本ラウンドでは判定できない**。C6-genericと決めつけず，
  「regi2c基盤は正常に見えるが，RF PLLロックそのものは未公開レジスタのため
  直接確認できず，根本原因は依然未確定」として保留する。
  実施13が定めた打ち切り基準（「regi2c/RF較正が正常に動いていると分かった上で
  IQ推定が完了しないなら C6-generic」）は，**RF PLLロックの確認**という前提条件を
  満たしていないため，まだ適用できない。一方，未公開regi2c blockを逆アセンブル・
  トレースで特定する作業（C6実施23の`wifi_regi2c_patch_install`相当）はレジスタ
  総当りの領域に踏み込むため，本ラウンドでは着手しない——**打ち切り基準を
  適用できる状態にまだ至っていないことを申し送り，判断はコーディネータに委ねる**。

### 申し送り

1. 本ラウンドで**修正コードは実装していない**（regi2c/クロック側に欠落が
   見つからなかったため）。実施13までの修正（ICG有効化）はそのまま維持。
2. 今後C5実機でJTAGレジスタ検証を行う場合は，**UARTブリッジ（ttyUSB1）経由の
   RTSリセットを使う**こと。native USB-JTAG（ttyACM）のリセットや
   OpenOCD `reset halt`はCPU/Coreレベルまでしか届かず，`MODEM_SYSCON`/`MODEM0`/
   PMUドメインの残存状態を誤って「今回のブートの結果」と読み違える罠がある
   （本ラウンド前半で実際に踏んだ——「ループが一瞬で完了する」という誤観測の
   原因はこれだった）。
3. WiFi RF専用regi2c blockの特定（C6同様，未公開のアドレス空間）は，このリポジトリの
   公開ヘッダ調査の範囲では実施不能。これ以上追うならESP-IDF blobの逆アセンブル・
   トレース（C6実施23の`wifi_regi2c_patch_install`手法の移植）が必要だが，実施13の
   打ち切り基準に従い本ラウンドでは着手しない。
4. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新は本ラウンド報告を
   受けたコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施14）

- 本doc（`docs/c5-bringup.md`）：実施14セクション追記のみ。ソースコード変更なし
  （`asp3/target/esp32c5_espidf/`・`asp3/arch/`とも無変更）。

### 検証（実施14）

- ビルド：`cmake --build build/c5_idf61` → `ninja: no work to do`（実施13から
  ソース無変更のため再ビルド不要，既存`asp_flash.bin`をそのまま使用）。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：UARTブリッジ（ttyUSB1）RTSリセットによる
  クリーンブート**2回**で，ハング再現（doneビット0のまま12〜15秒以上ループ）・
  regi2cクロック前提値・手動regi2cトランザクション成立を再現一致で確認。
- C5#2（stock参照，`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触）。

---

## 実施15：stock ESP-IDF v6.1 `examples/wifi/scan`との同一個体A/B比較——陽性対照（stockはC5#1で完走・27AP検出）を確認．MMIO可視状態は2件の再現差分を検出したが両方とも因果検証（JTAG注入）で棄却．全ての説明可能なクロック/ICG/BB-config系レジスタはstock/ASP3間でビット同一——残る説明領域はregi2c内部の不可視RF状態か，静的値に落ちないコードパス差のみ

コーディネータ指示＝実施14の申し送り(b)「同一個体・同一libphy blob上でのstock ESP-IDFとの
A/B比較」を実施。**本ラウンドはC5#1のみ使用，C5#2には一切接続・操作していない**。

### 机上準備・環境整備

- ESP-IDF v6.1（`~/tools/esp-idf-v6.1`，実施10/11で`v6.1-beta1`をclone済み）は
  本セッションのマシンでは`idf.py`用Python venvが未構築だったため，
  `./install.sh esp32c5`を実行して新規構築（`~/.espressif/python_env/
  idf6.1_py3.12_env`，esptool **v5.3.1**——doc冒頭の固定情報と一致，実施11で
  使った代替品v4.12.dev3ではない）。ツールチェーンは実施11同様
  `~/.espressif/tools/riscv32-esp-elf/esp-14.2.0_20241119`（xpackではなく
  Espressif配布版，`-DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`相当）を使用。
  `idf.py set-target esp32c5`実行時，cmakeがIDFツリー全体の`.gitmodules`
  （bt-lib各種・lwip・mbedtls・openthread等）を自動初期化した（`git submodule
  update --init`相当，pinned upstream commitのfetchのみでIDF自体のソース内容は
  改変していない——`examples/wifi/scan`はスクラッチ側にコピーして作業したため
  IDFツリー本体への書込みはこの標準的な依存関係解決のみ）。
- **Gate 1（blob同一性）**：ASP3の`esp_wifi.cmake:85`は
  `set(IDF /home/honda/tools/esp-idf-v6.1)`で**同一ツリーを直接参照**
  （`-L${IDF}/components/esp_phy/lib/esp32c5`等）しているため，stockビルドと
  ASP3ビルドは**構造的に同一のlibphy.a**を使う。実測でも確認：
  `md5sum ~/tools/esp-idf-v6.1/components/esp_phy/lib/esp32c5/libphy.a`
  = `4ccdbdbe1faf04a84b4059c882febe0f`（比較の前提が成立）。
- stockの`examples/wifi/scan`をスクラッチへコピーし，`idf.py set-target esp32c5`。
  JTAG長時間halt中のWDTリセットを避けるため`sdkconfig.defaults`で
  `CONFIG_ESP_INT_WDT=n`・`CONFIG_ESP_TASK_WDT_EN=n`・
  `CONFIG_BOOTLOADER_WDT_ENABLE=n`・`CONFIG_BOOTLOADER_WDT_DISABLE_IN_USER_CODE=y`
  を指定（stockのWiFi/PHY初期化コード自体は無変更）。`idf.py build`成功。
- stock ELFで`phy_iq_est_enable_new`の構造を確認：loop-top`0x4203296c`／
  ret`0x42032988`——`lw a5,1148(s1)`／`slli a4,a5,0xf`／`bgez a4,<loop_body>`
  という**ASP3実施13/14と同一の命令列**であることをobjdumpで確認。ASP3側は
  `build/c5_idf61/asp.elf`が実施13から無変更のため再ビルドせず，既知アドレス
  （loop-top`0x420288b2`／ret`0x420288ce`）をobjdumpで再確認のみ（アドレス
  シフト無し，実施13/14と同一結果）。

### ★新たに直面した技術的な壁と手法確立：UARTブリッジRTSリセットはJTAG接続を切断する

実施14の絶対条件「クリーンブートはUARTブリッジのRTSリセットのみ」に従い，
事前にhwブレークポイントを張ってから同リセットを打つ手順を試したところ，
**リセット直後にOpenOCDが`LIBUSB_ERROR_NO_DEVICE`を出して切断**した。
実施14がROMベクタ時点の値を読めていたのは，同じ表内の**別の**リセット手段
（native USB-JTAGのRTSリセット，Core止まり）によるものであり，そちらは
USB-Serial-JTAGペリフェラル自体を再列挙させないためJTAG接続が生存する。
一方，UARTブリッジのRTSリセット（System，RTC含む）はUSB-Serial-JTAG
ペリフェラルごと再初期化させるため，**USBデバイスが一旦消えて再列挙し，
既存のOpenOCDセッション（ブレークポイント設定含む）は失われる**——実施14では
無限ループ（ASP3のハング）が相手だったため後から接続しても実害が無く
気づかれなかった問題である。stockは校正が有限時間で完了するため，
このタイミング喪失は致命的（後から接続した時点で既にret通過済みの恐れ）。

対策として**再接続競争**の手法を確立・実測した：UARTブリッジRTSパルス
→USBデバイス再列挙をポーリング検出（実測**約0.29〜0.46秒**）→新規OpenOCD
プロセスをただちに起動・attach・即halt（実測**約0.05〜0.06秒**，ROM領域
`0x4xx3xxxx`〜`0x42000xxx`早期でのhalt成功）→ここでbp設置→resume。
合計リセット後**約0.6〜0.8秒**でhaltできる。この値はコンソールログの
phy_init開始時刻（**+0.492秒**，`I (492) phy_init: phy_version...`）と
直接比較できる数値ではない（JTAG接続を伴うブートは接続のオーバーヘッド分
コンソールのみのブートより遅くなりうる）が，**実際にloop-top用bpが
毎回正しく発火した**（＝取りこぼしなく`phy_iq_est_enable_new`呼出し前に
間に合っていた）という経験的事実そのものが，先行時間が十分だったことの
直接証拠である（`openocd_capture2.py`・`rts_reset.py`，スクラッチに保存）。
この手法により，UARTブリッジRTSリセット
（実施14が要求するドメインクリア）を維持したままstock/ASP3両方で
loop-top/retにbpを張れることを実証した——実施14では未解決だった
「クリーンリセット×JTAG精密捕捉の両立」を本ラウンドで解決。

### ★JTAG-haltタイミングアーティファクトの検出・修正（1回目の失敗から学んだ教訓）

最初のstock捕捉試行で，loop-top停止直後に8個のMMIOブロックを順に読む
スクリプトを使ったところ，**done bit(`0x600a047c`)が既に`1`**という結果が
出た（読み出し順で6番目，停止から約1.5〜2秒経過後の読み取り）。これは
`memory/feedback_hardware_investigation_rigor.md`の「JTAG haltは周辺回路の
実時間進行を止めない」という既知の罠（C6実施50と同型）である——CPUは
loop-topでフリーズしていても，較正ハードウェア自体は動き続けており，
我々がゆっくり他のレジスタを読んでいる間に完了してしまっていた。
**修正**：done bitを含むブロック（`0x600a0400`）を読み出し順の先頭に移動し，
さらに単発の高速読み（halt検出から**+0.10秒**）を追加。再測定の結果，
**この高速読みでも既にbit16=1**——つまりstockでは較正完了が極めて速く
（CPUの最初のポーリングチェックの時点，あるいはそれに近いタイミングで
既にハードウェアが完了している），"loop"が実質的にほぼ回らない（1パス
以内で完了する）ことが判明した。これ自体が新知見：stockのIQ推定は
ワンショットに近い即完了で，ASP3のように何十秒もポーリングし続ける
状況にはそもそも至らない。

### 陽性対照（Gate 3）：UART console実測

JTAGを介さず，UARTブリッジRTSリセット→コンソール読み取りのみでも確認：
stockはC5#1上で**完全に成功**した。

```
[  0.041] I (23) boot: ESP-IDF v6.1-beta1 2nd stage bootloader
[  0.424] I (382) main_task: Calling app_main()
[  0.492] I (482) phy_init: phy_version 109,edb400d6,Mar 10 2026,10:22:11
[  1.151] I (1112) wifi:mode : sta (d0:cf:13:f0:a7:44)
[  1.151] I (1112) wifi:enable tsf
[ 10.834] I (10822) scan: Total APs scanned = 27, actual AP number ap_info holds = 10
```

phy_init開始から「enable tsf」（＝esp_wifi_start完走，PHY校正完了）まで
**約0.66秒**。以降スキャン自体（全チャネル走査のため時間がかかる仕様）で
2.4GHz/5GHz合計27APを検出（ch1/6/36/44/48/104など）。**同一個体（C5#1）・
同一libphy.aブロブで，stockは明確に完走する**——これ自体がASP3固有の
ポート不備であることの最有力傍証であり，本ラウンドの絶対的な陽性対照となった。

### MMIOスナップショット比較（4ブート：stock×2, ASP3×2）

`docs`記載の固定リストの通り，loop-top初回ヒット時点で以下を`mdw`一括取得
（計166ワード）：`MODEM_SYSCON`(`0x600a9c00`〜`+0x2c`)・`MODEM_LPCON`
(`0x600af000`〜`+0x2c`)・PMU ICG系3レジスタ（`0x600b000c`/`0x600b00d0`/
`0x600b00dc`）・`I2C_ANA_MST`(`0x600af800`〜`+0x28`)・`MODEM0`
(`0x600a0000`〜`+0xfc`と`0x600a0400`〜`+0xfc`，計128ワード)。

- **再現性**：ASP3のboot1とboot2は**完全にバイト一致**（diff無し）。
  stockのboot1とboot2は**測定値行（`0x600a0460`〜`0x47c`の較正結果・
  ノイズ性の値）を除き完全一致**（同ブロック内で既知パターンの
  ブート毎解析ノイズ，`memory/feedback_hardware_investigation_rigor.md`
  記載のパターンと整合）。なお本ラウンドでは実施14必須項目の「ROMベクタ
  時点でMODEM_SYSCON/MODEM0/PMUが0であることの確認」を個別には行って
  いないが，**2ブートがバイト単位で完全一致した**という事実自体が
  それより強い証拠になっている——もし前回ブートの残存値が混入していれば，
  ブート毎に異なる残存パターンとなり2ブート一致は起こりにくい
  （残存汚染があるなら「たまたま2回とも同じ残存値」という偶然が必要）。
- **プラットフォーム間で安定して異なるレジスタ＝2件**（4ブート全てで
  上記の意味で再現・かつ両platform間で系統的に差がある）：
  1. **`MODEM_LPCON_COEX_LP_CLK_CONF_REG`(`0x600af008`)**：
     ASP3=`0x00000310`（DIV_NUM=49） vs stock=`0x00000000`。
     このレジスタは`esp_wifi_adapter.c`のコメント（実施6〜10で記載済み）で
     「C6版はこのレジスタに書込みを行っていたが，C5移植では『検証目的の
     未確定措置であり根本原因ではない』と判断し意図的に移植しなかった」
     と明記されている箇所——にも関わらずASP3側は`0x310`という非ゼロ値に
     なっており（明示的な移植漏れの書込みではなく，`modem_clock_select_
     lp_clock_source()`等，別の《移植済み》経路が生成しているとみられる），
     stockとは異なる値に落ち着いている。
  2. **`MODEM0+0x41c`**（公開ヘッダに対応する名前が存在しない，
     WiFi BB blob内部専用アドレス空間）：ASP3=`0x00027008` vs
     stock=`0x0003d008`。
  - それ以外の全レジスタ（`MODEM_SYSCON`の`CLK_CONF`/`CLK_CONF_FORCE_ON`/
    `CLK_CONF_POWER_ST`/`WIFI_BB_CFG`/`FE_CFG`，`MODEM_LPCON_CLK_CONF`
    (`0x600af018`=`0x7`)，`PMU_HP_ACTIVE_ICG_MODEM`(`0x80000000`，code=2)，
    `I2C_ANA_MST_ANA_CONF0`(`0x2100e444`)含む`I2C_ANA_MST`全11ワード，
    `MODEM0`の`0x450`(`0x00102003`)含む残り126ワード)は**stock/ASP3間で
    完全にビット一致**（`0x47c`のdoneビットと較正結果行を除く）。

### 因果検証（JTAG注入）：両候補とも棄却（★advisorレビューを受け0x600af008は前トリガ注入で再検証・結論変わらず）

まず1回目はASP3をloop-topで捕捉（bit16=0確認）→当該レジスタにstockの値を
JTAG書込み→読み戻しで書込み成立を確認→**loop-top用bpを解除**（毎パス
再発火して「ヒット」を誤検出しないため）→resume→**25秒間**，ret用bpのみで
監視，という手順で両候補を検証した。

| 対象 | ASP3値→注入値 | 25秒後 | doneビット | 書込み値の残存 |
|---|---|---|---|---|
| `0x600af008` (COEX_LP_CLK_CONF) | `0x310`→`0x000` | **ret未到達**（PC依然ループ内） | `0`のまま | 25秒後も`0x000`のまま（後続コードに上書きされず） |
| `0x600a041c` (MODEM0内部) | `0x27008`→`0x3d008` | **ret未到達**（PC依然ループ内） | `0`のまま | 25秒後も`0x3d008`のまま（同上） |

**★この時点でadvisorレビューにより方法論上の弱点を指摘された**：上記の
注入はいずれも`phy_iq_est_enable_new`の**loop-top（start-pulse発行後）**で
行っており，「較正エンジンがトリガ発行時点でクロック/リファレンスを
ラッチしている場合，トリガ後にレジスタを直しても手遅れ」という可能性を
排除できていない。特に`0x600af008`はクロック分周設定（DIV_NUM）であり，
較正の基準クロックに影響しうる値のため，この弱点は看過できない
（`memory/feedback_hardware_investigation_rigor.md`が戒める「安価な反証
実験が既に用意されているのに走らせずに結論を書く」パターンに該当しうる）。
そこで`0x600af008`についてのみ，**関数エントリ（`phy_iq_est_enable_new`
本体，`nm`で確定：`0x4202880a`，start-pulse発行より前）**にbpを張り直し，
同じ25秒間監視の手順で再実施した：

- エントリ捕捉時点で`0x600af008`が既に`0x310`であることを確認
  （エントリ〜loop-topの区間をobjdumpで確認済み——同区間内に`0x600af`
  番地への`lui`/`sw`が存在しないため，この値は関数呼出し前に既に確定して
  おり，エントリでの書込みは以降のstart-pulse発行にも確実に間に合う）。
- `0x000`へ書込み→読み戻しで成立確認→エントリ用bpを解除→resume→
  **25秒間**ret用bpのみで監視 → **ret未到達，doneビット0のまま，
  レジスタ値も25秒後まで`0x000`を保持**（上書きなし）。

**トリガ前injectionでも結果は変わらず＝`0x600af008`も因果関係なしと
確定した**（post-triggerの弱い棄却ではなく，pre-triggerを含む二重の
反証による確定的な棄却）。`0x600a041c`は公開名の無いblob内部アドレスで
候補としての説明力が弱く（下位バイトは両platformで一致し上位のみ異なる
＝カウンタ的な値の疑いが強い），pre-trigger再検証は行っていない
（post-triggerの棄却のみ。もし将来これを深追いするなら同様の
pre-trigger確認を推奨）。

### 判定

- **陽性対照＝成立**：stockはC5#1（ASP3がハングする個体そのもの）で
  完走し，27AP検出。同一blob・同一個体でstockが成功する以上，ハングは
  ハードウェア故障ではなくASP3ポート側（またはC6→C5移植時の未解決差分）
  に起因することが再確認された。
- **blob MD5一致＝確認済み**（構造的に同一ファイル，実測でも一致）。
- **MMIO差分＝2件検出・両方とも因果検証で棄却**。実施14は「差分が
  見つからなかった」という消極的な結果だったのに対し，本ラウンドは
  「差分を見つけた上でJTAG注入により因果関係を積極的に否定した」——
  より強い形の同じ結論に到達した。
- **総合結論＝実施14のbranch (c)の強化版**：クロック/ICG/BB設定/I2C_ANA_MST
  較正状態など，JTAGで観測可能な**MMIOレジスタ空間はstock/ASP3間で
  実質的に等価**（2件の差分もどちらも非因果と確定）。それでもなお結果が
  分岐する以上，残る説明領域は実施14が特定した壁と同じ——**WiFi RF専用
  regi2cブロック（未公開アドレス空間）内部のアナログ状態**，または
  MMIOの静的スナップショットには落ちないコードパス／シーケンス／
  タイミングの違い（blob内部の分岐条件等）のいずれかに限定される。
  **「C6-genericである」と断定はしない**——実施14の判断枠組み通り，
  RF PLLロックの直接確認手段が無い以上，この結論は変わらない。ただし
  「観測可能な範囲に修正可能な移植漏れがある」という望みは，本ラウンドで
  見つかった2つの具体的候補を両方とも実測で潰したことにより，実施14の
  時点よりも小さくなった。
- **余力による追加（regi2c watchpoint比較）は実施せず**：時間予算の都合上，
  上記の主要な結果（陽性対照＋MMIO全面比較＋因果棄却2件）を確実に完了
  させることを優先し，任意項目は次段へ持ち越した。
- **副次観測**：ASP3のハングを一切JTAGで介入せず放置すると，約3.5秒周期で
  `rst:0x12 (RTC_SWDT_SYS)`のリブートループに入ることを確認（最終復帰
  確認時，UART consoleのみで観測）。ループ内でウォッチドッグが一切
  feedされないための自然な帰結であり，新しい根本原因ではないが，
  「無限に静止するハング」ではなく「約3.5秒毎に自己リブートするハング」
  という，より正確な症状の記述として記録する。

### 申し送り（次段）

1. **修正コードは実装していない**（因果検証で棄却された2件のみで，
   他に確認可能な移植漏れが見当たらなかったため）。実施13までの修正
   （ICG有効化）はそのまま維持。
2. 次に追うなら，実施14/本ラウンドが繰り返し行き着く壁＝**WiFi RF専用
   regi2cブロックの特定**（ESP-IDF blobの逆アセンブル・トレース，C6実施23の
   `wifi_regi2c_patch_install`手法の移植）に踏み込むしかない。ただし
   実施13の打ち切り基準（レジスタ総当りへは入らない）を踏まえ，
   着手はコーディネータの判断を仰ぐ。
3. 任意項目として残した「IQ推定開始前のI2C_ANA_MST watchpointによる
   regi2cトランザクション列比較」（stock/ASP3で発行されるblock/reg/data
   系列を比較）は，未着手のまま次段へ持ち越す。
4. 今後もUARTブリッジRTSリセットでのJTAG精密捕捉が必要な場面では，
   本ラウンドで確立した「再接続競争」手法（`openocd_capture2.py`・
   `rts_reset.py`，スクラッチ保存）が再利用可能。ただし対象アドレスが
   より早期（起動後0.6秒未満）にある場合は先行時間が不足する可能性が
   あるため，個別に再測定すること。
5. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新は
   本ラウンド報告を受けたコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施15）

- 本doc（`docs/c5-bringup.md`）：実施15セクション追記のみ。
- ソースコード変更なし（`asp3/target/esp32c5_espidf/`・`asp3/arch/`とも無変更，
  実施13の修正のまま）。
- スクラッチ（`/tmp/claude-1000/.../c75ad9fa-.../scratchpad/`，本セッション限り）：
  `rts_reset.py`・`uart_capture.py`・`openocd_capture2.py`・`openocd_causal.py`
  （再利用可能な手法として作成）。生データ：`stock_scan/`（stockビルド一式），
  `stock_console_boot1.log`（陽性対照コンソールログ），
  `stock_boot1v2.*`・`stock_boot2.*`・`asp3_boot1.*`・`asp3_boot2.*`
  （MMIOスナップショット），`asp3_causal_coexlp2.*`・`asp3_causal_041c.*`
  （post-trigger因果検証結果），`asp3_causal_coexlp_pretrigger.*`
  （advisorレビューを受けた0x600af008のpre-trigger再検証結果），
  `asp3_final_restore_console.log`（最終復帰確認）。
- 環境：`~/tools/esp-idf-v6.1`で`install.sh esp32c5`を実行し
  `idf6.1_py3.12_env`を新規構築（IDFツリー自体のソース改変なし，
  pinned submoduleのfetchとpython venv構築のみ）。

### 検証（実施15）

- ビルド：stock`idf.py build`成功（esptool v5.3.1でイメージ生成確認）。
  ASP3側は`build/c5_idf61`無変更（実施13のビルド済み成果物をそのまま使用，
  アドレス再確認のみ）。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - stock：UARTブリッジRTSリセットからのクリーンブート**2回**で
    loop-top捕捉・MMIOスナップショット取得・完走確認（コンソールでも
    独立に1回，27AP検出まで完全確認）。
  - ASP3：同条件でクリーンブート**2回**，loop-top捕捉・スナップショット
    取得・boot間バイト一致確認。
  - 因果検証：クリーンブート**3回**（post-trigger×2＝候補ごとに1回ずつ，
    pre-trigger×1＝`0x600af008`のadvisorレビュー後再検証），各25秒間の
    監視でret未到達を確認。
  - 最終復帰：ASP3イメージのまま（実施13から無変更），UARTブリッジRTS
    リセットで約3.5秒周期のWDTリブートループ（既知のハング症状）を
    確認——復帰完了。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，
  MACで都度照合）。

---

## 実施16：regi2cトランザクション列トレース（stock/ASP3，4-way比較）——実施13の打ち切り基準「(c)/(d)＝C6-generic」は**不成立**。局所化された再現性のある差分2件（書込み値の恒久的分岐＋ASP3限定の追加read）を発見，具体的な原因候補（`phy_get_pkdet_data`のMMIO読出し値）まで絞り込んだ。C6-generic判定は見送り，continue推奨

コーディネータ指示＝実施13/14/15の残課題(b)（regi2cトランザクション列の実測比較）に着手。
`docs/wifi-shim-c6.md`実施23・実施39〜42の手法をC5へ移植する。

### 1. 机上調査：C5では`--wrap`が直接効く（C6のROM関数ポインタテーブルパッチは不要）

IDF v6.1のC5用`libphy.a`を`nm`・`objdump -dr`で調査した結果，C6と決定的に異なる
構造であることが判明した：

- C6は`rom_i2c_writeReg`等がROM常駐かつ`g_phyFuns`という実行時関数ポインタ
  テーブル経由の間接呼出しでしか到達できず，`--wrap`が原理的に無力だった
  （テーブル自体を直接パッチする必要があった）。
- **C5は`phy_i2c_writeReg`/`phy_i2c_writeReg_Mask`/`phy_i2c_readReg`/
  `phy_i2c_readReg_Mask`が`libphy.a`内の`phy_i2c.o`が提供する通常の大域
  リンケージ関数**であり，`phy_rx_cal.o`・`phy_analog_cal.o`・`phy_rfpll.o`
  等，他の多数の`.o`から`U`（未解決参照）として呼ばれている（`objdump -dr`の
  `R_RISCV_CALL`relocationで実引数パターンも確認：
  `writeReg(block,host_id,reg_add,data)`＝a0-a3，
  `writeReg_Mask(block,host_id,reg_add,msb,lsb,data)`＝a0-a5，
  `readReg(block,host_id,reg_add)`＝a0-a2，
  `readReg_Mask(block,host_id,reg_add,msb,lsb)`＝a0-a4，C6と同じ規約）。
- よって`-Wl,--wrap=phy_i2c_writeReg`等の標準的な`--wrap`が**そのまま直接
  効く**——C6のようなROM常駐テーブルパッチ手法は不要と判定した。

### 2. 計装実装（ASP3・stock同一フォーマット）

- リングバッファ構造体`wifi_regi2c_t`（`t_us_low`,`block`,`host_id`,
  `reg_add`,`data`,`msb`,`lsb`,`op`〔0=write/1=write_mask/2=read/
  3=read_mask〕，12バイト/エントリ，C6の`wifi_regi2c_t`とバイトレイアウト
  完全一致）を2048エントリ確保。
- **ASP3側**：`asp3/target/esp32c5_espidf/wifi/wifi_trace.c`・`.h`を新規
  追加（診断コード，`#if`ガード相当のCMakeオプション
  `ESP32C5_WIFI_REGI2C_TRACE`〔既定OFF〕で有効化）。`target.cmake`に
  オプション追加・`esp_wifi.cmake`に`--wrap`4個追加。`apps/wifi_scan/
  wifi_scan.c`に`TOPPERS_ESP32C5_WIFI_REGI2C_TRACE`ガード付きで
  `esp_wifi_init()`直前の`wifi_regi2c_reset()`/`wifi_regi2c_dump_addr()`
  呼出しを追加。ビルドは`build/c5_idf61_trace`（既存の`build/c5_idf61`
  ＝実施13の非計装版とは別ディレクトリ，RAM使用率82.5%）。リンク後
  `nm`／`objdump`で`__wrap_phy_i2c_*`への実際のジャンプ先解決を確認
  （blob内の呼出し元が`__wrap_*`へ飛ぶ実アドレスをobjdumpで直接確認）。
- **stock側**：実施15と同じくスクラッチへコピーした`examples/wifi/scan`
  （`stock_scan/`）に同一構造の`wifi_trace.c`・`.h`を追加し，
  `main/CMakeLists.txt`で`idf_build_set_property(LINK_OPTIONS ...
  APPEND)`により最終実行ファイルのリンクへ`--wrap`4個を確実に到達させた
  （`target_link_options(main ...)`では最終exeへ伝播しない懸念をadvisor
  指摘により回避）。IDFツリー本体は無改変。同様に`nm`／`objdump`で
  `__wrap_phy_i2c_*`への実ジャンプを確認。
- 両ビルドで`phy_iq_est_enable_new`のloop-top/ret命令アドレスを計装追加後に
  再確認（objdump，実施13と同一の`lw/slli/bgez`パターン）：
  - ASP3：loop-top`0x42028a72`／ret`0x42028a8e`
  - stock：loop-top`0x42032a60`／ret`0x42032a7c`
  - リングバッファ本体アドレス：ASP3`0x40833400`（pos`0x40801d94`）／
    stock`0x40819e10`（pos`0x40825224`）。

### 3. 実機測定（C5#1のみ，4ブート）

実施15の「再接続競争」手法（`rts_reset.py`＋`openocd_capture_regi2c.py`
〔`openocd_capture2.py`を土台に，MMIOスナップショットの代わりにregi2c
リングバッファのpos読取り→エントリ数分だけ`mdw`でダンプする処理に置換〕）
で，UARTブリッジRTSリセット→loop-top初回ヒットでリングバッファを
JTAGダンプ→resume→ret用bp監視，を4ブート実施：

| ブート | pos（エントリ数） | done bit（+0.10s読み） | resume後 |
|---|---|---|---|
| stock boot1 | 1242 | `1`（`0x00010000`） | ret到達（0.10秒後） |
| stock boot2 | 1242 | `1` | ret到達（0.10秒後） |
| ASP3 boot1 | 1244 | `0`（`0x00000000`） | loop-top再ヒット（0.10秒後，ret未到達＝ハング継続を確認） |
| ASP3 boot2 | 1244 | `0` | loop-top再ヒット（0.10秒後） |

ASP3側は1回目の再取得試行（`boot2b`）でloop-topのヒット待ちが6秒
タイムアウトした（再接続競争のブレで初期haltが遅く着地した1回。
OpenOCDログの`Could not read register 'pc'`は「CPUが走行中で読めない」
という正常な過渡状態で異常ではない——boot1の成功ログにも同数出現して
いることを確認済み。クラッシュ/JTAG切断ではなく単なる待ち時間不足）。
再試行（`boot2c`）で成功し，以降はこれをASP3 boot2として採用。

### 4. 解析：4-way比較（difflibによるアラインメント考慮，C6実施41/42の手法を移植）

当初，`t_us_low`（起動後経過時間）を比較対象に含めたまま単純位置比較を
行い「ほぼ全エントリが違う」という誤った結果を得た（実施41が戒める
「単発比較の誤判定」と同型のバグ——`t_us_low`は当然ブート毎に異なる
ため除外要）。修正後，`(op,block,host_id,reg_add,msb,lsb,data)`のみで
比較し，かつ`difflib.SequenceMatcher`でアラインメント（挿入/削除を
考慮した最小編集列）を取ることで，位置ズレによる見かけ上の差分の
連鎖（1件の挿入が下流全てを「差分」に見せかける）を排除した。

4種類の比較（A＝ASP3内2ブート，B＝stock内2ブート，C＝ASP3boot1×stock
boot1，D＝ASP3boot2×stock boot2）を行い，**CとDの両方に現れ，AにもBにも
現れない**差分のみを「プラットフォーム決定的」と認定する実施41/42の
基準を適用した。

#### 4a. 大半（idx 0〜234）は完全一致，以降も大部分は既知のSAR/DCオフセット比較器ノイズ

- idx 0〜234（235エントリ）は4ブート全て完全一致。
- `block=0x63,host=1,reg=0x0b`（逐次比較型のSAR/DC-offset比較器読出しと
  推定，C6実施41が同種のノイズを確認済みの現象）は，A（ASP3内2ブート）に
  14件，B（stock内2ブート）に9件の**±1カウント程度**の差分を生む——
  典型的な起動毎アナログノイズであり，プラットフォーム差ではない。
  C・D双方にも同位置で現れるが，A・Bにも現れるため4-way基準で除外される。

#### 4b. ★新規・再現性あり・プラットフォーム決定的な差分（1）：`block=0x6b,host=1,reg=0x02`書込み値がidx=235を境に恒久分岐

`block=0x6b,host_id=1,reg_add=0x02`への単純write（`msb=lsb=0xFF`，
フルバイト書込み）は同一トレース内に複数回出現する。idx=101,145,189では
**4ブート全てが`0x74`で完全一致**。ところが**idx=235以降，ASP3は
`0x87`に切り替わり，それ以降トレース終端（idx=1143含む，捕捉できた
全域）まで一度も変化しない**一方，**stockは`0x74`のまま**（1箇所だけ
`0x54`への逸脱があるが，これはstock自身の2ブートでも起きない外れ値で
むしろノイズ側の性質——本件の主張には使わない）：

| idx | asp3_1 | asp3_2 | stock_1 | stock_2 |
|---|---|---|---|---|
| 101/145/189 | 74 | 74 | 74 | 74 |
| 235 | **87** | **87** | 74 | 74 |
| 260 | **87** | **87** | 74 | 74 |
| 285 | **87** | **87** | 54 | 53 |
| 336 | **87** | **87** | 74 | 74 |
| 361 | **87** | **87** | 74 | 74 |
| 362 | **87** | **87** | 74 | 74 |
| 1143（asp3側番号，末尾付近） | 87 | 87 | 74（stock側1141） | 74 |

4-way基準（C・Dに現れ，A・Bに現れない）を満たす：ASP3は2ブートとも
一貫して`0x87`（恒久的に固定），stockは2ブートとも一貫して`0x74`
近辺——**ノイズでは説明できない，恒久的な書込み値の分岐**である。

#### 4c. ★新規・再現性あり・プラットフォーム決定的な差分（2）：ASP3限定の追加read `block=0x69,host=0,reg=0x06`

idx=364（asp3側番号）で，**ASP3は`block=0x69,host_id=0,reg_add=0x06`への
read（戻り値`0x2f`）を実行するが，stockは同位置でこの読出しを一切行わ
ない**（`difflib`のアラインメントで純粋な1エントリ挿入と確定，以降は
1エントリのシフトで再同期する）。この同一の追加readは**ASP3の捕捉
できた範囲内でもう一度，idx=1118で再度出現**（値も同じ`0x2f`）——
単発の偶然ではなく，ASP3側の処理に構造的に組み込まれた追加ステップで
あることを示す。両出現ともASP3の2ブートで再現し，stockの2ブートには
一度も現れない。

#### 4d. 参考：idx=333の`block=0x68,host=1,reg=0x03`（read_mask）差分は低信頼度

`asp3=0x1e` vs `stock=0x1f`（±1）——4-way基準は満たすが，差分の絶対値が
4aのノイズ級（±1）と同程度であり，n=2ブートでは偶然A/Bノイズが該当
位置を踏まなかっただけの可能性を排除できない。4bと違い**恒久的な分岐
ではなく単発**のため，本ラウンドでは参考情報に留め主張の柱には使わない。

### 5. 原因候補の絞り込み（読み取り専用のIDFソース調査，レジスタ総当りには入らない）

4bの分岐点（idx=235）に近い`phy_iq_est_enable_new`自身のループ本体
（実施13が特定済み，loop-top`0x42028a72`直後）をobjdumpで再確認した：

```
42028a90: jal phy_get_pkdet_data
42028a94: jal phy_abs_temp
```

- `phy_get_pkdet_data`（ROM常駐でない，blob内`0x42027082`）を逆アセンブル
  すると，**MMIO `0x600a0c50`を読み，符号拡張した値をそのまま返すだけ**
  （regi2c非経由）。
- `phy_abs_temp`（`0x42026b12`）は引数の`abs()`を返すだけの汎用ユーティリ
  ティ（名前に反し温度センサ読出し自体は行わない）。

この2関数が`phy_iq_est_enable_new`のループ本体から**毎回**呼ばれている
ことと，4bの分岐が**一度起きたら最後まで固定値`0x87`に張り付く**という
挙動（典型的な「入力が凍結した」パターン）を重ね合わせると，**MMIO
`0x600a0c50`（パワーディテクタ/ADCラッチ値と推定される公開名の無い
`MODEM0`内部アドレス）がASP3側で凍結値を返している可能性**が，本ラウンド
で追跡可能な範囲での最有力仮説として浮上した。ただしこれは**本ラウンドの
regi2cトレースだけでは検証できない**（MMIO直読みの生きている/凍結して
いるかのA/B比較が必要で，実施13が確立した`0x600a7xxx`域のAGC生存確認
手法と同種の，別の新しいJTAG実験になる）。よって本ラウンドではこの
仮説の提示に留め，実験は次段へ申し送る（レジスタ総当りには入らない）。

### 6. 判定：実施13の打ち切り基準「(c)/(d)＝C6-generic」は不成立——continue推奨

★advisorレビューを経て，当初案（「regi2cが正気な値を返している＝(c)/(d)，
C6-genericと結論して停止」）を**撤回**した。理由：

- 実施13の打ち切り基準(b)は「regi2c/RF較正が**正常に動いている**と
  分かった上でIQ推定が完了しないなら」C6-genericと結論する，という
  条件だった。本ラウンドが確認したのは「regi2cは動いている（正気な値を
  返す）」ことだけでなく，**stockという既知良品の対照と比較して
  再現性のある形で異なる**（4b・4c）ことである。「正気な値を返す」は
  「stockと一致する」より弱い基準であり，後者が満たされない以上，
  (c)/(d)（regi2c可視範囲は同一に振る舞っている）は成立しない。
- 4bは特に「入力へ依存して変動するはずの値が，ある時点から先ASP3だけ
  固定される」という，較正が**機能していない**ことを示唆する具体的な
  シグネチャであり，「アナログ壁だから仕方ない」と片付けるにはstockが
  同じチップ・同じblobで安定して変動＝収束できているという反証が強すぎる。
- 一方で，どちらの分岐（4b／4c）についても**まだ「規i2c逆アセンブルの
  総当り」に入らずに追える具体的な次の一手（5節のMMIO `0x600a0c50`
  A/B比較）がある**——実施13の打ち切り基準が要求する「打ち切る前に
  安価な反証実験が尽きていること」を満たしていない。

**したがって本ラウンドでは「C6-genericである」と結論しない**。
判定はコーディネータへ申し送り，継続（5節の候補を追う）を推奨する。

### 7. 終了処理

- C5#1をASP3計装ビルド（`build/c5_idf61_trace/asp_flash.bin`，
  `ESP32C5_WIFI_REGI2C_TRACE=ON`）のまま書き戻し済み（実験の最後の
  ブートがこのビルド）。UARTブリッジRTSリセットで最終確認した結果，
  約3.5秒周期の`rst:0x12 (RTC_SWDT_SYS)`リブートループを再現——
  既知のハング症状は計装ビルドでも完全に不変であることを確認した
  （計装オーバーヘッドがハングの原因/解消に無関係であることの追加
  傍証でもある）。
- ASP3側の計装コード（`wifi_trace.c`・`.h`，`target.cmake`・
  `esp_wifi.cmake`・`wifi_scan.c`の変更）はツリーに残す（CMakeオプション
  `ESP32C5_WIFI_REGI2C_TRACE`既定OFFのため，通常の`ESP32C5_WIFI=ON`
  ビルドには一切影響しない）。

### 申し送り（次段）

1. **最優先**：5節の仮説（`phy_get_pkdet_data`が読むMMIO`0x600a0c50`が
   ASP3側で凍結している可能性）をJTAG A/B比較で検証する。手法は実施13の
   `0x600a7xxx`AGC領域生存確認と同型（stock/ASP3双方でloop-top付近の
   複数時点で当該アドレスを読み，値が変動するかを見る）。凍結が確認
   できれば，その上流（PMU/ICG/クロック等，実施13と同じ種類の移植漏れ）
   を追う具体的な次のステップができる。
2. 4bの`block=0x6b,reg=0x02`と5節のMMIO仮説の**因果関係は未検証**
   （時間的近接と分岐タイミングの一致は状況証拠に過ぎない）。MMIO仮説が
   反証された場合は，4b・4cの由来を別の角度（例えば`host_id`が0/1で
   何を区別するか＝C5はデュアルバンドのため2.4GHz/5GHz双方のRFチェーンを
   意味する可能性——4cの追加readが2回出現している事実と符合する）から
   再検討する必要がある。
3. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新は
   本ラウンド報告を受けたコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施16）

- `asp3/target/esp32c5_espidf/wifi/wifi_trace.c`・`wifi_trace.h`：新規
  追加（regi2c read/write/read_mask/write_maskトレース，`--wrap`ベース，
  診断専用）。
- `asp3/target/esp32c5_espidf/target.cmake`：`ESP32C5_WIFI_REGI2C_TRACE`
  オプション追加（既定OFF），ONの場合のみ`wifi_trace.c`をソースへ追加。
- `asp3/target/esp32c5_espidf/esp_wifi.cmake`：同オプションON時のみ
  `phy_i2c_{read,write}Reg[_Mask]`への`-Wl,--wrap`4個を追加。
- `apps/wifi_scan/wifi_scan.c`：`TOPPERS_ESP32C5_WIFI_REGI2C_TRACE`
  ガード付きで`wifi_trace.h`インクルードと`esp_wifi_init()`直前の
  リセット/アドレス出力呼出しを追加。
- 本doc（`docs/c5-bringup.md`）：実施16セクション追記のみ。
- スクラッチ（`c75ad9fa-5310-4781-9013-97e0c0ec7812/scratchpad/`）：
  `openocd_capture_regi2c.py`（実施15の`openocd_capture2.py`を土台に
  regi2cダンプ処理へ置換，再利用可能），`decode_regi2c.py`（4-way
  アラインメント比較スクリプト，デコード時のNUL先頭バイト除去・
  タイムスタンプ除外など2件のバグを本ラウンド内で発見・修正済み），
  `stock_scan/`（stock側計装込みビルド一式，`main/wifi_trace.{c,h}`
  ・`main/CMakeLists.txt`のidf_build_set_property追加を含む），
  生データ`r16_asp3_boot1.*`・`r16_asp3_boot2.*`（タイムアウト）・
  `r16_asp3_boot2c.*`（成功，boot2として採用）・`r16_stock_boot1.*`・
  `r16_stock_boot2.*`・`r16_asp3_final_restore_console.log`。

### 検証（実施16）

- ビルド：ASP3側`build/c5_idf61_trace`（`ESP32C5_WIFI_REGI2C_TRACE=ON`）
  ・stock側`stock_scan/build`（`idf.py build`）とも成功。両ビルドとも
  `nm`／`objdump`で`--wrap`の実リンク（`__wrap_phy_i2c_*`への実ジャンプ）
  を確認。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - stock：UARTブリッジRTSクリーンブート**2回**，loop-top捕捉・
    regi2cリングバッファダンプ（各1242エントリ，ラップアラウンド無し）・
    ret到達（陽性対照，各0.10秒後）を確認。
  - ASP3：同条件で**2回**（1回はタイムアウトで再試行，成功分を採用），
    loop-top捕捉・regi2cリングバッファダンプ（各1244エントリ，
    ラップアラウンド無し）・resume後はloop-top再ヒット（ret未到達＝
    ハング継続）を確認。
  - 4-way比較：`difflib.SequenceMatcher`によるアラインメント考慮比較で
    実施41/42の基準（C・Dに現れ，A・Bに現れない）を適用し，2件の
    プラットフォーム決定的差分（4b・4c）と1件の低信頼度差分（4d）を
    検出。ノイズ（block=0x63,reg=0x0b系，A14件・B9件）は正しく除外。
  - 最終復帰：ASP3計装ビルドのまま，UARTブリッジRTSリセットで約3.5秒
    周期のWDTリブートループ（既知のハング症状）を確認——復帰完了。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，
  MACで都度照合）。

---

## 実施17：pkdet凍結仮説（実施16申し送り）をstock/ASP3 A/Bで検証——**棄却／不確定**（両platformとも恒久的に`0x00000000`，stockも変動せず判別力なし）。副次発見の`0x600a0c00`域4-way差分2件は因果検証で棄却。C5#1は計装ビルドのまま復帰・症状不変

コーディネータ指示＝実施16申し送り「`phy_get_pkdet_data`が読むMMIO`0x600a0c50`が
ASP3側で凍結している可能性」をJTAG A/B比較で検証。**本ラウンドもC5#1のみ使用，
C5#2には一切接続・操作していない**。

### 判定基準の事前固定

測定開始前に以下を固定（スクラッチ`r17_criteria_precommit.txt`）：ASP3で全サンプル
（MMIO直読み・`a0`返り値とも）が完全同一値（±0）かつstockでヒット毎に変動（実施15/16の
±1カウント級ノイズを明確に超える）なら「凍結」確定。ASP3側も変動していれば仮説棄却。
advisorレビューで2点を追加：(1) `0x600a0c50`の値は読み出し時点のRF条件（`phy_rx_pkdet_dc_cal`
文脈 vs `phy_iq_est_enable_new`文脈）に依存するため，異なる呼出し元(`ra`)をまたいだ絶対値
比較は無意味——`ra`でタグ付けし同一呼出し元内で比較する。(2) 両platformとも近い値で
ほぼ一定なら「不確定」とし，無理に凍結/非凍結を結論しない（実験室環境でRF入力が無ければ
正当に起こりうるため）。

### 手法：dual-breakpoint常駐観測（新規スクリプト）

`phy_iq_est_enable_new`のloop-top（`MODEM0+0x47C`のdoneビット確認位置）と
`phy_get_pkdet_data`唯一の`ret`命令（**3箇所の呼出し元すべて**——`phy_rx_pkdet_dc_cal`の
内部ループ・`phy_iq_est_enable_new`のループ本体・その直後の単発呼出し——から到達する
共通アドレス）の2つにhwブレークポイントを同時常駐させ，ヒット毎に`resume`を挟んで
交互ヒットを継続観測する`pkdet_watch.py`をスクラッチに新規作成（実施15/16の
UARTブリッジRTSリセット＋再接続競争手法を踏襲）。LOOP_TOPヒットでは直接MMIO読み
（`mdw 0x600a0c50`），PKDET_RETヒットでは`a0`（返り値，符号拡張済み）と`ra`
（呼出し元識別用）を記録。同じヒットのタイミングで`0x600a0c00`〜`0x600a0cfc`
（64ワード，実施15が未比較のまま残していた領域）も初回ヒット時点で1回スナップショット
（advisorレビュー指摘により実施2をボート消費なしで同一ブートに統合）。

ASP3はハングしたまま放置すると約3.5秒周期でSUPER_WDT（`rst 0x12`）が全チップリセットを
かけJTAG接続を道連れにする（UARTブリッジRTSリセットと同じSystem+RTCドメインの
リセット種別）ため，初回halt直後にJTAG経由で`LP_WDT_SWD_CONFIG_REG`
（`0x600b1c1c`，unlock keyは`LP_WDT_WPROTECT_REG`＝`0x600b1c20`に`0x50d83aa1`）の
`SWD_DISABLE`ビット(30)を立てて本キャプチャの間だけSWDを無効化した。LP_WDTはRTC
ドメインに属するため次回のUARTブリッジRTSリセットで自動的にPORデフォルトへ戻り，
後続のクリーンブートを汚染しない（実測でも確認——本ラウンド末尾の最終復帰確認で
ハング症状が実施16と完全に同一のまま再現した）。

### ハマった点1：JTAG再接続レースが`sil_dly_nse`（ASP3カーネルのビジーウェイト遅延）の
### 呼出し内に着地すると，以後breakpointが一切発火しなくなる

初回試行でASP3側が0ヒットのまま繰り返しタイムアウトした。原因調査の結果，
初回halt PCが`0x42021958`前後（`sil_dly_nse1`, `asp3/asp3_core`カーネルの
マイクロ秒busy-waitルーチン内）に着地したブートでは，resume後どれだけ待っても
（実測20秒以上）loop-top/pkdet-retのいずれにも到達しなかった。JTAG haltは
ハードウェア/周辺回路の実時間進行を止めない（`memory/feedback_hardware_investigation_rigor.md`
recurrence #5と同型）という既知の罠のソフトウェア版と考えられる——この遅延ループが
何らかの実時間参照（タイマ等）と突き合わせて校正的な動作をしている場合，haltを挟むと
校正の算術が狂って極端に長い（または終わらない）追加待ちを生む可能性がある。
初回halt PCが`0x42020000`〜`0x42022000`の範囲に入ったブートは自動的に破棄し
UARTブリッジRTSリセットからやり直す再試行ロジックを`pkdet_watch.py`に実装した。
この範囲を避けても**なお毎回ではなく確率的に**loop-top到達に時間がかかる（またはこの
ラウンドの計測時間内に到達しない）ケースが残ったため，patience（`per_hit_timeout`・
再試行）を実測しながら調整した。原理は未解明のまま，実務的な回避（着地判定＋再試行）
で対処——深追いは本ラウンドの主目的ではないため打ち切り，申し送りとする。

### ハマった点2：`mdw`直読みパーサのNUL先頭バイトバグ（実施16の既知バグと同型，本スクリプトでは未修正のまま流用していた）

初回の本番キャプチャ後，記録された`mmio_c50`が全ヒットで`0x00000000`という綺麗な
結果が出たが，念のためコードを再点検した結果，`pkdet_watch.py`の`parse_mdw_value()`が
OpenOCDの応答先頭に付く生の`\x00`バイトを`str.strip()`で除去できず（`\x00`は空白文字
ではない），`line.startswith("0x")`判定が**常に**失敗して`None`を返していたことが判明した
（実施16の`decode_regi2c.py`が独立に踏んだのと同じ「NUL先頭バイト」バグ，本スクリプトでは
見落として流用していた）。さらに悪いことに，この`None`を表示する際のフォールバック値が
print文（`0`）とファイル書込み（`0xFFFFFFFF`）で**不一致**だったため，ライブ標準出力は
偽の「`mmio_c50=0x00000000`」を表示し，パース失敗を隠蔽していた——`memory/
feedback_hardware_investigation_rigor.md`が戒める「静かに嘘の値を出す」典型例を
本ラウンド自身が作り込んでいたことになる。

一方，`a0`/`ra`は別関数`parse_reg_value()`（`"(/32):"`という部分文字列一致で判定，NUL
プレフィックスに影響されない設計）で取得しており，これは**汚染されていない**。
`a0`は`phy_get_pkdet_data`が`0x600a0c50`の値をそのまま符号拡張して返す値そのもの
（逆アセンブル済み，実施16）であり，実質的に同じ情報を独立した頑健な経路で得ていた
ことになる。

このバグを`pkdet_watch.py`内で修正した上で，**壊れやすい常駐dual-bp長時間キャプチャを
再度回す代わりに**，実施15/16で実績のある単一breakpoint方式（`openocd_capture2.py`と
同型）で`0x600a0c50`を直接複数回読む軽量な確認専用スクリプト`mmio_pkdet_verify.py`を
別途作成し，ASP3・stock双方で独立に実施した。生のOpenOCD応答テキストを`print`で
そのまま可視化し，パース結果と付き合わせて二重確認した：

```
ASP3 (loop-top 0x42028a72, 5回, resumeを挟み1周期あたり約0.25s間隔):
  read #0..#4: 0x600a0c50 = 0x00000000（全て一致，生テキストも0x00000000で明示）
stock (loop-top 0x42032a60, 5回, 同様):
  read #0..#4: 0x600a0c50 = 0x00000000（全て一致，生テキストも0x00000000で明示）
```

以上により，dual-bp長時間キャプチャの`a0`結果（後述）と単一bp軽量確認の直接MMIO
読みが独立に一致し，「両読み方（関数返り値／直接MMIO）で同じ結論」という当初の
プロトコル意図（halt-artifact対策とは別に，パース経路の独立性としても）は結果的に
満たされた。なお`c00_snapshot`（後述の`0x600a0c00`域64ワード）と因果検証の`mdw`結果は
**このバグの影響を受けていない**——いずれも値をパースせず生テキストのまま保存
（`c00_snapshot`はオフライン解析時に別途NUL除去パーサで再パース，因果検証は
生テキストを目視でそのまま採用）しており，本バグはpkdet_watch.pyのライブ
`mmio_c50`表示にのみ影響していた。

### 結果1：pkdet凍結A/B（主実験）——両platformとも恒久的に`0x00000000`，凍結仮説は不確定（stockが判別力を持たない）

UARTブリッジRTSクリーンブート，ASP3×2・stock×2の計4ブートで，dual-bp常駐観測を
実施（各ブート40ヒットまで，数秒〜11秒スパン）：

| ブート | LOOP_TOP直読み(件) | PKDET_RET `a0`(件) | 値 | `ra`内訳 |
|---|---|---|---|---|
| ASP3 boot1 | 20 | 20 | **全件`0x00000000`** | dc_cal文脈(`0x42027102`)×1，iq_est-loop文脈(`0x42028a94`)×19 |
| ASP3 boot2 | 20 | 20 | **全件`0x00000000`**（boot1と秒単位で同一タイミング） | 同上（dc_cal×1，iq_est-loop×19） |
| stock boot1 | 34 | 6 | **全件`0x00000000`** | dc_cal文脈(`0x42030e34`)×6のみ（iq_est-loop文脈`0x42032a82`は0件） |
| stock boot2 | 33 | 7 | **全件`0x00000000`** | dc_cal文脈×7のみ（同上） |

軽量確認（`mmio_pkdet_verify.py`，修正済みパーサ・生テキスト確認込み）でも
ASP3・stockとも`0x600a0c50`＝`0x00000000`を再確認（前節）。

**LOOP_TOP⇔PKDET_RETの交互ヒットが全ブートで一貫して観測された**（例：ASP3
boot1のidx=0(PKDET_RET)→idx=1(LOOP_TOP)→idx=2(PKDET_RET)→…）——これは
両halt間でCPUが実際に1ループ分進行した証拠であり，halt-artifact対策（実施16申し送りが
要求した「resumeを挟む」観測）が機能していたことを示す。

**stockのiq_est-loop文脈(`ra=0x42032a82`)は2ブートとも0件**——実施15/16が確立した
「stockのIQ推定はほぼワンショットで完了し，doneビットが初回チェックで既に1になっている
ことが多い」という知見と整合する（ループ本体からのpkdet呼出しが一度も発生しない）。
一方stockのLOOP_TOP自体は30回超ヒットしている——`phy_iq_est_enable_new`が
**チャンネル毎に複数回呼び出されている**ため（stockのスキャンは全チャンネル走査で
約10秒かかる，実施15のコンソールログと整合），呼出しの都度1回だけloop-topを通過して
即終了（ループ本体には入らない）を繰り返していると解釈できる。ASP3のdc_cal文脈も
逆アセンブルが示す最大4回ループ想定に対し1回しか出現しなかった（早期exit分岐条件が
初回で満たされたと推定，未確認）。

### 判定1：pkdet凍結仮説は**棄却／不確定**——事前固定した基準を満たさない

事前基準「ASP3全サンプル同一値**かつ**stockでヒット毎に変動（ノイズ超）」の後半
（stockの変動）が成立しなかった。stockは健全に動作しAP検出まで完走する個体・ビルド
でありながら，`0x600a0c50`は`a0`・直接MMIO読みともに**ASP3と同じく恒久的に
`0x00000000`**だった。これは事前に定めた「不確定」分岐（advisorレビュー由来）に
該当する：両platformとも近い値でほぼ一定という結果は，実験室環境で外部RF入力が
無い状況では正当に起こりうる（電力検出器が物理的に読むべき信号強度が実際に
存在しない）。したがって**「凍結している」というASP3固有の病理は本実験では
確認できない**——同時に，「凍結していない」と断定するのも早計ではなく（この
レジスタが`0x00000000`固定になる理由自体は未解明），単純に**この特定のMMIOが
stock/ASP3の判別力を持たないと分かった**という消極的な結果として記録する。
`phy_get_pkdet_data`自体は両platformで完全に同一のコード（実施16確認済み）であり，
呼ばれた時点で入力（`0x600a0c50`の生の値）がゼロなら，健全なstockでも当然ゼロを
返す——「関数が正常に動いている」ことと「入力がゼロ」は独立である，という
当たり前の事実を実測で確認した形になる。

### 結果2：`0x600a0c00`域4-wayスナップショット比較（実施15が未比較だった領域を充足）

上記4ブートそれぞれの初回ヒット時点（ASP3は`ra=dc_cal`，stockも同じくdc_cal文脈——
両者とも同一の「文脈」で採取できており比較に適する）で`0x600a0c00`〜`0x600a0cfc`
（64ワード）を取得し4-way比較した（NUL先頭バイトを除去する修正版パーサでオフライン
解析——`c00_snapshot`自体は生テキスト保存のため前節のバグの影響を受けていない）。

**プラットフォーム決定的な差分＝2ワード**（ASP3 boot1/boot2で一致，stock boot1/boot2で
一致，かつASP3≠stock）：

| アドレス | ASP3(boot1=boot2) | stock(boot1=boot2) |
|---|---|---|
| `0x600a0c0c` | `0x00006000` | `0x00007dc2` |
| `0x600a0c8c` | `0x00006000` | `0x00007dc2` |

両アドレスは`0x80`バイト差（`0x600a0c00`ブロックと`0x600a0c80`ブロックの同一オフセット
`+0xc`）で，スナップショット全体を見るとこの2つの`0x80`バイトブロックは**各platform内で
互いに完全一致**（同一データが2回繰り返されている——実施16が示唆した
「`host_id`＝2.4G/5Gデュアルバンドの2チェーン分」という解釈と整合する構造）。
残り62ワードは4ブートすべてでビット一致（公開名の無いこの領域にも関わらず，
大部分はplatform非依存の静的値であることが分かる）。この2ワードは実施14/15/16の
公開ヘッダ調査では該当する名前が見つからず，`FE_CFG`/`WIFI_BB_CFG`と同様
blob内部専用のアドレスと判断する。

### 結果3：因果検証（JTAG注入）——**棄却**（doneビット不変・ハング不変・注入値は25秒間保持され上書きされず）

実施15と同じ手法（loop-top捕捉→書換え→loop-top bp解除→resume→25秒監視）で，
`0x600a0c0c`と`0x600a0c8c`に**同時に**stock値`0x00007dc2`を注入した：

- 書込み前：両アドレスとも`0x00006000`（ASP3値，doneビット`0x600a047c`＝`0`）。
- 書込み・読み戻し：両アドレスとも`0x00007dc2`へ変化を確認（書込み自体は成立）。
- 25秒監視後：**`ret`未到達**（PCは`phy_iq_est_enable_new`内に留まる，
  `0x42028a7a`＝ループ本体内）。doneビット依然`0`。**注入値は25秒後も
  `0x00007dc2`のまま**（後続のblobコードによる上書きなし）。

書込みが持続したまま何の変化も起きなかったことから，「後で上書きされて無効化された」
という反証（実施15のadvisorレビューが指摘した弱点と同型）は排除でき，この2ワードは
**ハングの原因ではないと確定的に棄却**できる。

### 判定・まとめ

- **pkdet凍結仮説（実施16申し送り最優先項目）＝棄却／不確定**：ASP3の
  `0x600a0c50`は恒久的に`0x00000000`だが，健全なstockも同一値・同一挙動を示す
  ため「ASP3固有の凍結」という主張は支持されない。この特定のMMIOは
  stock/ASP3判別に使えないと判明した。
- **`0x600a0c00`域の副次スナップショット比較＝実施15の欠落領域を充足，
  2ワードのプラットフォーム決定的差分を発見したが因果検証で棄却**。
- **修正コードは実装していない**（task指示通り，根本原因の特定に至らなかった
  ため）。ASP3計装ビルド（実施13〜16の修正込み，`build/c5_idf61_trace`）は無変更。
- **方法論的な教訓2件を記録**：(1) JTAG再接続レースが`sil_dly_nse`ビジーウェイト
  ルーチン内に着地すると以後breakpointが極端に発火しづらくなる現象（原理未解明，
  着地判定＋再試行で回避）。(2) `mdw`直読みパーサのNUL先頭バイトバグを本ラウンド
  自身が新規スクリプトに作り込み，かつ表示用フォールバック値の不一致がそれを
  一時的に隠蔽した——実施16の同型バグから教訓が汎化されていなかった。今後
  OpenOCDテキスト応答をパースするスクリプトは`decode_regi2c.py`のNUL除去処理を
  必ず流用するか，パース結果の生テキストを常に併記して検証可能にすること。

### 申し送り（次段）

1. `0x600a0c50`（pkdet）は判別力なしと判明したため，実施16の最有力候補は解消。
   実施16の4b（`block=0x6b,reg=0x02`書込み値の恒久分岐`0x87` vs `0x74`）と4c
   （ASP3限定の追加read`block=0x69,host=0,reg=0x06`）は依然未解明・未棄却のまま
   残っている——次段はこの2件（特に4b，恒久分岐という強いシグネチャ）を
   別のアングルから追うのが筋が良い。
2. `0x600a0c00`域の2ワード差分（`+0xc`/`+0x8c`）は棄却済みだが，この64ワード
   ブロック全体が「2.4G/5Gデュアルバンドの2チェーン分の繰り返し構造」という
   構造的知見は今後の同種調査（`host_id`の意味の特定等）に流用できる。
3. `sil_dly_nse`ビジーウェイト内での着地問題は，今後C5実機でJTAG再接続競争を
   使う全ラウンドに影響しうる（原理未解明のまま回避策のみ導入）。深追いするなら
   「ASP3のtiming calibrationが何を実時間参照にしているか」の机上調査が先。
4. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新は本ラウンド報告を
   受けたコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施17）

- 本doc（`docs/c5-bringup.md`）：実施17セクション追記のみ。
- ソースコード変更なし（`asp3/target/esp32c5_espidf/`・`asp3/arch/`とも無変更，
  実施13〜16の状態のまま。task指示通り根本原因を特定できなかったため修正は
  実装していない）。
- スクラッチ（`c75ad9fa-5310-4781-9013-97e0c0ec7812/scratchpad/`，本セッション）：
  `pkdet_watch.py`（dual-bp常駐観測，新規），`mmio_pkdet_verify.py`（軽量単一bp
  確認，新規），`openocd_causal_dual.py`（2アドレス同時注入の因果検証，実施15の
  `openocd_causal.py`を拡張），`r17_criteria_precommit.txt`（判定基準の事前固定）。
  生データ：`r17_asp3_boot1/2.*`・`r17_stock_boot1/2.*`（dual-bp観測結果・
  `c00_snapshot`），`r17c_asp3_verify.*`・`r17c_stock_verify.*`（軽量確認），
  `r17_causal_c0c_c8c.*`（因果検証），`r17_final_restore_console.log`（最終復帰）。

### 検証（実施17）

- ビルド：本ラウンドはソース変更なしのためビルドは実施せず。ASP3側は既存の
  `build/c5_idf61_trace/asp_flash.bin`（実施16の計装ビルド，無変更）をそのまま
  使用。stock側も既存の`stock_scan/build`（実施16でビルド済み，無変更）を再利用。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - ASP3：UARTブリッジRTSクリーンブート**2回**（dual-bp観測，各40ヒット）＋
    軽量MMIO確認**1回**（5回読み）＋因果検証**1回**（25秒監視）で計4回のクリーンブート。
  - stock：UARTブリッジRTSクリーンブート**2回**（dual-bp観測，各40ヒット）＋
    軽量MMIO確認**1回**（5回読み）で計3回のクリーンブート。
  - 因果検証：書込み成立・25秒後も値保持・doneビット不変・`ret`未到達を確認。
  - 最終復帰：ASP3計装ビルドのまま，UARTブリッジRTSリセットで約3.5秒周期の
    `rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを確認——実施16と症状完全一致，
    復帰完了。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，MACで
  都度照合）。

---

## 実施18：regi2cトレースに呼出し元PC(`ra`)を追加し，4b/4cの発行元関数を確定——両方とも**ASP3/stock間で同一コード**（4b=`phy_set_txcap_reg`，4c=`phy_tsens_temp_read_local`）と判明。4bは`phy_param[176..183]`という静的キャリブレーションテーブルの**内容**の分岐（コードではなくデータの分岐）で，チャンネル非依存の`0x87`固定を数値レベルで説明できた。4cは`phy_param[35]`という制御フラグがASP3=0／stock=1で分岐しており，これが4cの直接原因とJTAG A/B読みで確定。tsens計算が読むeFuse入力(`EFUSE_RD_MAC_SYS5_REG`)はplatform間で同一値と確認（tsens関連の当該eFuse仮説は棄却）。修正は未実装（真の分岐源=`phy_param[35]`/`phy_param[176..183]`を書き込む上流コードは未特定のため）

コーディネータ指示＝実施16/17の申し送り「4b（`block=0x6b,reg=0x02`書込み値の恒久分岐`0x87`対`0x74`）と4c（ASP3限定の追加read`block=0x69,host=0,reg=0x06`）の発行元をregi2cトレースへの呼出し元PC追加で直接特定する」に着手。

### 1. 計装拡張：`wifi_regi2c_t`に`ra`欄を追加（12→16バイト／エントリ）

`asp3/target/esp32c5_espidf/wifi/wifi_trace.h`・`.c`（ASP3側）とスクラッチ
`stock_scan/main/wifi_trace.{c,h}`（stock側）の両方で，各`__wrap_phy_i2c_*`に
`__builtin_return_address(0)`を追加記録した（構造体末尾に`uint32_t ra`を追加，
先頭3ワードは実施16/17と同一レイアウトのため`decode_regi2c.py`は読み進め
ワード数の変更のみで流用できた）。2段目（呼出し元の呼出し元）の記録は
**見送った**——GCCの`__builtin_return_address(1)`はフレームポインタ連鎖に
依存し，`-O2`最適化済みblobコード（フレームポインタ省略が一般的）に対して
使うと未定義動作／クラッシュのリスクが高いと判断（実施の鉄則「未検証の
判別指標に乗らない」に基づく事前判断）。ASP3側RAM使用率は82.5%→84.55%
（+8KB，2048エントリ×4バイト増）で予算内。両ビルドとも成功を確認。

### 2. 実機採取（C5#1のみ，各1ブート）

実施16/17の「再接続競争」手法をそのまま流用し，ASP3×1・stock×1ブートで
loop-top初回ヒット時にリングバッファをJTAGダンプした。4bの分岐位置
（idx≈235）・4cの初出位置（idx≈364）は実施16で既知のため，本ラウンドは
raの同定が目的であり各1ブートで開始する計画どおり実施——**ra値はブート間
で揺れず結論を左右しなかったため，2ブート目は追加しなかった**（後述の
txcap引数トレース追加後の再採取でも同一のraが再現し，間接的にこの判断を
補強）。

- ASP3：pos=1244エントリ（実施16と同数），loop-top再ヒット・doneビット`0`
  でハング継続を確認（症状不変）。
- stock：pos=1242エントリ（実施16と同数），ret到達（doneビット`1`）で
  陽性対照を確認。

### 3. 発行元関数の同定：4b＝`phy_set_txcap_reg`，4c＝`phy_tsens_temp_read_local`——両方ともASP3/stock間で**同一コード**

`nm`/`objdump -dr`でraをシンボル解決した（`resolve_syms.py`を新規作成，
`nm -n`のソート済みシンボル表に対する二分探索）。

**4b**（`block=0x6b,host=1,reg=02`のplain write，idx=101/145/189=`0x74`
〔両platform一致〕，idx=235以降=ASP3`0x87`固定 vs stock`0x74`近辺で変動）：

| idx群 | 値 | ra（ASP3） | ra（stock） | 解決先 |
|---|---|---|---|---|
| 101/145/189 | 74（両platform） | `phy_tx_cap_init+140` | `phy_tx_cap_init+144` | 同一関数（オフセット差は命令列長のわずかな差） |
| 235/260/285/336/362/1143 | ASP3=87固定 | `phy_set_txcap_reg+26` | （同位置）`phy_set_txcap_reg+30`(RAM/`.iram0.text`) | **同一コード**，配置のみ相違（ASP3=flash実行，stock=IRAM実行） |
| 361 | ASP3=87 | `phy_rx_loop_cap_set+100` | `phy_rx_loop_cap_set+112` | 同一関数（別系統の書込み経路） |

`phy_set_txcap_reg`をobjdumpで完全比較した結果，**命令列は完全に同一**
（`phy_get_chan_cap`→`phy_freq_to_mbgain`→`phy_txcap_setting`を呼ぶだけ，
差異はstockが`.iram0.text`（RAM実行）に配置されているためlibphy.aの遠隔
関数呼出しが`jal`ではなく`auipc+jalr`になっている点のみ——リンク配置の
違いであって計算ロジックの違いではない）。よって**4bは「発行元コードが
違う」のではなく「同一コードが計算する値そのものが違う」**——実施16の
時点で想定していた「別コードパス」仮説はここで反証された。

**4c**（ASP3限定の追加read`block=0x69,host=0,reg=06`→`0x2f`，idx=364/1118）：

raは両出現とも`phy_tsens_temp_read_local+20`——`phy_i2c_readReg(block=0x69
=105,host=0,reg=6)`を直接呼んでいる箇所（tsens/SAR_ADCの生DACコード読出し）。
関数名からもコード内容からも，これは**温度センサ読出し**そのもの
（task事前仮説の「tsens/SAR_ADC経路説」が的中）。

### 4. `phy_set_txcap_reg`の入力トレースを追加し，4bの原因を「同一コードだが入力(チャンネル)ではなくテーブル内容が違う」まで特定

`phy_set_txcap_reg`もlibphy.aの大域リンケージ関数（`nm`で`T`型）と確認できた
ため，`--wrap`で第2の小さなリングバッファ（`wifi_txcap_call_t{t_us_low,arg0}`，
32エントリ）へその唯一の引数（channel/freq，MHz単位の整数）を記録する計装を
追加した（`esp_wifi.cmake`に`-Wl,--wrap=phy_set_txcap_reg`を追加，ASP3/stock
双方同型）。

同一ブート内でregi2cトレースとtxcap引数トレースを`t_us_low`（同じクロック
源）で突き合わせた結果，ASP3の6回の`0x87`書込みは実際には**6つの異なる
チャンネル**（ch1=2412MHz／ch6=2437MHz／ch11=2462MHz／ch5=2432MHz／ch5再測定
／ch6最終確認）に対応していた（`phy_set_txcap_reg`呼出しのタイムスタンプと
regi2c書込みのタイムスタンプが数〜数十µs差で1対1対応）。stockも**全く同じ
チャンネル列**（同一の校正シーケンス，同一関数）を辿っており，値は
ch1=74/ch6=74/ch11=53/ch5=74/ch5=74/ch6=74——ch11だけ異なる値になる。

**ASP3は6チャンネル全てで恒久的に同一の`0x87`**（チャンネル非依存）——
「入力（チャンネル）は動いているが出力が変わらない」という，実施16が
懸念した「凍結」パターンの確定的な再現である。

### 5. 数値レベルでの根本原因確定：`phy_param[176..183]`テーブルの内容分岐

`phy_set_txcap_reg`→`phy_get_chan_cap`→（3バケットの粗い周波数しきい値判定）
→`phy_param+176/178/180`（各2バイト，計3組の(cap0,cap1)ペア）を読み，
`phy_txcap_setting`が`(cap1<<4)|cap0`を計算してレジスタへ書く，という
処理列をobjdumpで完全に追った（`phy_get_chan_cap`もASP3/stock間で命令列
完全一致，配置のみ相違——4bと同型）。

同一ブート・同一loop-top halt時点（全チャンネル校正完了後）でこの6バイト
テーブルをJTAGで直接読み，A/B比較した：

| オフセット | ASP3 | stock |
|---|---|---|
| phy_param+176/177 | `07 08` | `04 07` |
| phy_param+178/179 | `07 08` | `04 07` |
| phy_param+180/181 | `07 08` | `03 05` |

**ASP3は3バケット全てが同一の`(07,08)`**——`(08<<4)|07=0x87`は3バケット
どこを引いても同じ値になる（チャンネル非依存の理由が数値的に確定）。
stockは`(07<<4)|04=0x74`（2バケット）と`(05<<4)|03=0x53`（1バケット）の
2値が混在——`0x53`はch11（実施16表の`0x54`と同値域，起動毎の境界ノイズ）
に対応し，実施16の4d（低信頼度差分として保留していた`idx=333`近辺の±1
ノイズ）とは無関係な，正規のバケット分岐であることも判明した。

この6バイトテーブルは`phy_tx_cap_init`（4bの`0x74`側writeの発行元でも
ある）が2.4GHz帯で3回ループしながら`phy_chip_set_chan_ana`（RF PLLを
参照チャンネルへ実際にチューニング）→`phy_rfcal_txcap`（実測に基づく
キャリブレーション，結果をこのテーブルへ書く）という手順で埋めている
ことをobjdumpで確認した。`phy_rfcal_txcap`自体の内部（実際の測定に何を
使っているか）までは本ラウンドの深追い上限（2〜3関数）を超えるため
立ち入っていない——**次段の最有力候補**として申し送る。

### 6. tsens/SAR_ADC比較：eFuse入力は同一と確認（該当仮説は棄却），真の分岐点は`phy_param[35]`フラグ

`phy_tsens_temp_read_local`をobjdumpで解析した結果：
`phy_i2c_readReg(0x69,0,6)`→4bitマスク→`phy_tsens_dac_to_index`→
`EFUSE_RD_MAC_SYS5_REG`（`0x6000e058`，eFuse生システムデータワード）の
下位バイトを追加のテーブル添字として使用→`phy_tsens_attribute`
（ROM/flash常駐の定数テーブル，`0x4206d4d0`）から符号付きバイトを引き→
`phy_code_to_temp`→`phy_tsens_dac_cal`という，正規の温度センサ較正
パイプラインだった。

`EFUSE_RD_MAC_SYS5_REG`（`hal/.../efuse_reg.h`で確認，ESP32-C5のeFuse
ベース`0x6000e000`+`0x58`）をJTAGでA/B直読みした：

- ASP3：`0x0000c002`
- stock：`0x0000c002`（**完全一致**）

eFuseは物理的に一度書き込まれた読み出し専用データであり，ソフトウェア
初期化の有無に関わらずハードウェアが起動時に自動でシャドウレジスタへ
展開する設計のため，値が一致すること自体は驚きではないが，「ASP3が
eFuseの初期化ステップを欠いていて生値/デフォルト値を読んでいる」という
サブ仮説は**本ラウンドで反証**できた（実施13のICG型パターンとは異なる）。

一方，`phy_tsens_temp_read_local`（すなわち4cの追加read自体）が
**そもそも呼ばれるかどうか**は，`register_chipv7_phy`内の
`phy_param[35]`（1バイトフラグ）の値で分岐することをobjdumpで確認した
（`lbu a5,35(s0); bnez a5,skip_phy_get_temp_init`）。同一loop-top halt
時点でこのバイトをJTAGでA/B直読みした：

- ASP3：`phy_param[35] = 0x00`（→分岐せず`phy_get_temp_init`
  →`phy_tsens_temp_read`→`phy_tsens_temp_read_local`を実行）
- stock：`phy_param[35] = 0x01`（→スキップ）

**これが4c（「stockはこの読み出しを一度も行わない」）の直接かつ確定的な
原因**——コードの欠落ではなく，`phy_param[35]`という状態フラグの値が
platform間で異なることによる，正規の条件分岐の結果である。

### 7. 因果関係の整理：4bと4cは**独立した症状**（時系列上，直接の因果連鎖ではない）

regi2cトレースのタイムスタンプで確認した限り，4bの分岐（idx=235，
`phy_tx_cap_init`の2.4GHzループ内，t_us≈0x0020b51e付近）は，4cの初出
（idx=364，t_us≈0x0022d168）よりも**時間的に前**に発生している。すなわち，
4bの原因（`phy_param[176..183]`テーブルの内容）が確定した時点では，まだ
4cのtsens読み出しは一度も実行されていない——**「4cの凍結した温度入力が
4bの計算に使われている」という直感的な統合仮説は，この時系列だけで
反証される**（時間的に後の事象が前の事象の原因にはなり得ない）。
`phy_freq_to_mbgain`が書く`phy_param+0x502`（温度依存ゲイン補正値）も
`phy_txcap_setting`の`(cap1<<4)|cap0`計算には使われていない（コード上
無関係な出力先）ことも確認済み。

したがって，現時点の最も正直な結論は：**4bと4cはいずれもASP3固有の
再現性ある挙動であり，`phy_param[35]`のような「較正モード」を表す
フラグ群の値がplatform間で違うことに起因する可能性が高い（示唆的だが
未確定）が，4b自体の直接原因は`phy_param[176..183]`テーブルの内容
そのものであり，4cのtsens読み出し有無とは別系統の分岐である**——両者を
1本の因果に単純化しない。

### 8. 因果検証・修正：未実施（真の書込み元が未特定のため）

`phy_param[176..183]`および`phy_param[35]`それぞれの**書込み元**
（どのコードが，何を入力にこれらの値を計算・格納しているか）は本
ラウンドでは特定に至っていない。`phy_rfcal_txcap`（4bのテーブル埋め）と，
`phy_param[35]`をどこかで設定しているはずの初期化コード（PHY init-data
ロード／NVS較正データ有無判定など，ESP-IDFの一般的な「full cal / partial
cal」モード切替の文脈が濃厚）のいずれも，本ラウンドの深追い上限
（2〜3関数）を超えるため立ち入っていない。**入力の差を「特定できた」と
言えるのは4c（`phy_param[35]`というフラグの値そのもの）までであり，
その値がなぜASP3で0・stockで1になるのかという，さらに1段上流の原因は
未特定**。よって，JTAG注入によるハング解消の因果検証や，ASP3移植層の
修正は本ラウンドでは実施しなかった（task指示「特定に至らなければ修正
せず記録」に従う）。

### 9. C5#1の最終状態

C5#1はASP3計装ビルド（`build/c5_idf61_trace/asp_flash.bin`，
`ESP32C5_WIFI_REGI2C_TRACE=ON`，本ラウンドの`ra`/txcap引数トレース拡張
込み）のまま書き戻し済み。UARTブリッジRTSリセットでの最終確認（コンソール
ログ）：

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
ESP-ROM:esp32c5-eco2-20250121
rst:0x12 (RTC_SWDT_SYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
```

約3.5秒周期の`rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを再現——実施13〜17
と症状完全一致。本ラウンドの計装追加（regi2cの`ra`欄・txcap引数トレース）
はハングの有無に影響しない（既知のこと，実施16/17でも確認済みの計装
オーバーヘッド非関与パターンと整合）。

### 申し送り（次段）

1. **最優先**：`phy_param[35]`を書き込んでいる上流コード（`register_chipv7_phy`
   より前，PHY init-data／NVS較正データロード周辺が濃厚）を特定し，
   ASP3がなぜ`0`（stockは`1`）になるかを追う。ESP-IDFの一般的な
   full-calibration/partial-calibration切替の文脈と合致するかを
   `hal/`（読取専用）側のPHY init APIドキュメント／構造体定義から
   机上調査すると当たりが付けやすい可能性がある。
2. `phy_rfcal_txcap`（`phy_param[176..183]`テーブルの実測ベース書込み元）
   の内部を追い，そこが読む生入力（regi2c／MMIO）を特定する。4bの
   直接の書込み元はここであり，`phy_param[35]`と共通の上流原因を持つのか，
   独立した別要因なのかもここで切り分けられる可能性がある。
3. 4bと4cを1本の因果に単純化しない（本ラウンドで時系列反証済み）。
   統合仮説を再提案する場合は，まず新しい時系列証拠を示すこと。
4. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新は
   本ラウンド報告を受けたコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施18）

- `asp3/target/esp32c5_espidf/wifi/wifi_trace.h`・`wifi_trace.c`：
  `wifi_regi2c_t`に`ra`欄追加（呼出し元PC記録）／`phy_set_txcap_reg`の
  引数トレース用の第2リングバッファ（`wifi_txcap_call_t`，32エントリ）
  ・`wifi_txcap_reset()`・`wifi_txcap_dump_addr()`・
  `__wrap_phy_set_txcap_reg`を新規追加。
- `asp3/target/esp32c5_espidf/esp_wifi.cmake`：`ESP32C5_WIFI_REGI2C_TRACE`
  ON時の`--wrap`一覧に`phy_set_txcap_reg`を追加。
- `apps/wifi_scan/wifi_scan.c`：`esp_wifi_init()`直前で
  `wifi_txcap_reset()`/`wifi_txcap_dump_addr()`も呼ぶよう追加。
- 本doc（`docs/c5-bringup.md`）：実施18セクション追記のみ。
- スクラッチ（`c75ad9fa-5310-4781-9013-97e0c0ec7812/scratchpad/`）：
  `openocd_capture_regi2c.py`（`ENTRY_WORDS`を3→4に更新，txcapバッファの
  オプションダンプ機能を追加），`decode_regi2c.py`（4ワード/エントリの
  パース・`ra`のfmt/semantic対応に更新），`resolve_syms.py`（新規，
  nmシンボル表への二分探索アドレス解決），`read_phyparam_table.py`
  （新規，phy_param任意オフセットのA/B読み，bad-landing retry付き），
  `read_efuse.py`・`read_phyparam35.py`相当（`/tmp/`に作成した使い捨て
  スクリプトの内容を統合済み，恒久化する場合はスクラッチへ移設要），
  `txcap_watch.py`（新規作成したが結局不採用——ライブ単一breakpoint方式は
  RTS再接続競争の待ち時間分散が大きく非効率と判明したため，計装追加
  方式（4節）に切替えた。再利用時は要改修），`stock_scan/main/
  wifi_trace.{c,h}`・`stock_scan/main/CMakeLists.txt`：ASP3側と同型の
  `ra`欄・txcap引数トレース追加，`scan.c`：同トレースのreset/dump呼出し
  追加。

### 検証（実施18）

- ビルド：ASP3側`build/c5_idf61_trace`（RAM 84.62%，FLASH 11.76%）・
  stock側`stock_scan/build`とも成功。`nm`で`__wrap_phy_set_txcap_reg`の
  実リンクを確認。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - `ra`欄追加後の初回A/B比較（各1ブート）：ASP3 pos=1244／stock
    pos=1242（実施16と同数），期待通りの結果（ハング継続／ret到達）を
    再確認。
  - txcap引数トレード追加後のA/B比較（各1ブート，`ra`欄も同時取得）：
    ASP3 txcap pos=27／stock txcap pos=27（同数，同一チャンネル列）を確認。
  - `phy_param[176..183]`（6バイトテーブル）A/B直読み：上記5節の通り
    プラットフォーム決定的な差分を確認。
  - `EFUSE_RD_MAC_SYS5_REG`（`0x6000e058`）A/B直読み：完全一致を確認
    （2回試行，各platformとも安定して`0x0000c002`，reset非依存の静的値
    のため再現性の懸念は元々小さい）。
  - `phy_param[35]`A/B直読み：ASP3=`0x00`・stock=`0x01`を確認（各1回，
    静的値のため十分）。
  - ライブ単一breakpoint方式（`txcap_watch.py`）の試行：`phy_set_txcap_reg`
    エントリへのbreakpointが12〜25秒の観測窓で一度も発火せず（既知の
    ROM-vector正常着地でも同様），RTS再接続競争の待ち時間分散
    （実施16のboot2bタイムアウト事例と同型）が原因と判断し，計装追加
    方式へ切替えて解決——不採用の経緯として記録。
  - 最終復帰：ASP3計装ビルドのまま，UARTブリッジRTSリセットで約3.5秒
    周期の`rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを確認——実施13〜17
    と症状完全一致，本ラウンドの計装追加もハングの有無に無関係と確認。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，
  MACで都度照合）。

---

## 実施19：cal_mode交絡を実測で解消（stock full-cal実測=degenerateにならず）——`phy_param[176..183]`の真の書込み元を`phy_rfcal_txcap`の2つの書込みサイトまで特定・**新知見**：ASP3はサーチループの第2書込みサイトに一度も到達しない＝探索的キャリブレーション自体が「候補0番目のシード値のまま」で止まっている。`phy_param[35]`は cal_mode フラグではなく「`register_chipv7_phy`完了マーカー」だったと訂正（4cは実質ハングの同義反復）。修正は未実装（真因はtxcap探索が依存する測定入力＝`phy_get_tone_sar_dout`/tone自己ループバック側にあると判明したため）

コーディネータ指示＝実施18の最優先申し送り「`phy_param[35]`／`phy_param[176..183]`を書いている上流を特定し，
ASP3/stockでその入力が分岐する箇所を突き止める」＋最重要の交絡「stockはNVS較正キャッシュのfull/partial cal
交絡が疑われるため，flash全消去→初回ブート（full cal）で基準比較を取り直す」に着手。**本ラウンドもC5#1の
みを使用，C5#2には一切接続・操作していない（MACで都度照合，最終確認済み）**。

### 0. 静的調査：`register_chipv7_phy`呼出しとinit_data/cal_modeの構築（ASP3＝stockと同一ソース）

- `asp3/target/esp32c5_espidf/esp_wifi.cmake:294-296`が
  `${IDF}/components/esp_phy/src/phy_init.c`・`phy_common.c`・
  `${IDF}/components/esp_phy/esp32c5/phy_init_data.c`を**IDFツリーのソースファイルをそのまま**
  ソースリストへ追加している（ASP3側にローカルコピーは無い）——つまり`esp_phy_load_cal_and_init()`
  ・`register_chipv7_phy()`呼出しの組み立てコードそのものはASP3/stockで**バイト単位ではなく
  ソースレベルで完全同一**。
- `esp_phy_load_cal_and_init()`（`phy_init.c:895-`）は`#ifdef CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`
  の有無でNVS較正キャッシュの有無が分岐する：
  - **stock**（`stock_scan/sdkconfig:1634`）＝`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=y`（Kconfig既定値
    どおり）。`esp_phy_load_cal_data_from_nvs()`が失敗（NVSに較正データ無し＝初回ブート）すれば
    `calibration_mode = PHY_RF_CAL_FULL`にフォールバックし，成功すれば`CONFIG_ESP_PHY_CALIBRATION_MODE=0`
    （`PHY_RF_CAL_PARTIAL`）を使う。
  - **ASP3**（`asp3/target/esp32c5_espidf/sdkconfig_stub/sdkconfig.h`，NuttXポート由来）は当該マクロを
    定義しない＝`#else`分岐で**常に`register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL)`を無条件に
    呼ぶ**（`esp_wifi.cmake:224-236`のコメントに明記済み，実施10〜13から変更なし）。
  - enum値は`esp_phy_init.h:58-60`で確認：`PHY_RF_CAL_PARTIAL=0`／`PHY_RF_CAL_NONE=1`／`PHY_RF_CAL_FULL=2`。
- `esp_phy_init_data_t`（`esp_phy_init.h:26-32`）はC5では`uint8_t params[256]`固定長。
  `phy_init_data.c`の埋め込み配列は`CONFIG_ESP_PHY_MAX_TX_POWER`のみに依存し，ASP3側sdkconfig_stub
  （`=20`）とstock側sdkconfig（`=20`，Kconfig既定`ESP_PHY_MAX_TX_POWER=ESP_PHY_MAX_WIFI_TX_POWER`と一致）
  で**値が一致**——机上の時点で「init_dataは恐らくバイト同一」との仮説が立った（4節で実測確認）。

### 1. 実機：`register_chipv7_phy`エントリでa0/a1/a2を直接読む新スクリプト（`r19_capture.py`）

実施15〜18の「再接続競争」（UARTブリッジRTSリセット→ttyACM1再列挙をポーリング→OpenOCD即attach→
`sil_dly_nse`系bad-zone着地は自動リトライ）を土台に，2段階のブレークポイント捕捉を1ブートで行う
新スクリプト`r19_capture.py`を作成した：
1. `register_chipv7_phy`のエントリ（`nm`実測：ASP3=`0x4202476e`，stock=`0x4202e3ac`）にhw bp。
   ヒット時に`reg a0/a1/a2`を読む（RISC-V呼出し規約，第1〜3引数）と`a0`（init_data）から
   256バイト（実際は121ワードの安全マージンで読み，構造体長で切って比較）を`mdw`ダンプ。
2. `phy_iq_est_enable_new`のloop-top/ret（objdump実測：ASP3 loop-top=`0x42028b32`／ret=`0x42028b4e`，
   stock loop-top=`0x42032b3c`／ret=`0x42032b58`——実施18からアドレスがシフトしているのは
   前ラウンドまでの計装追加の影響，`asp.elf`/`scan.elf`を都度`nm`/`objdump`で再確認したうえで使用）
   にhw bpを張り直し，`phy_param+35`（1バイト）と`phy_param+176`（4ワード＝16バイト，実施18の
   6バイトより広めに読んで境界確認）を読む。

### 2. ★最重要の交絡潰し：stock flash全消去→真のfull-cal初回ブートを実測

- `esptool erase-flash`でC5#1を**完全消去**（NVSパーティション含む全域）した後，`stock_scan/build`を
  書込み。1回目はUARTのみでの素通し確認（プレーンなconsole capture）——`Total APs scanned = 34`で
  正常完走を確認したが，この時点ではJTAG計装なしのため`register_chipv7_phy`の`a2`引数は未取得。
- ★ハマった点（新規）：`esptool write-flash`は末尾で自動的に`Hard resetting via RTS pin`する。
  この自動リセットで発生する1回目のブートが，コマンド呼出しの往復レイテンシの間に**裏で勝手に
  完走してNVSへ較正データを書き込んでしまう**ため，直後に`r19_capture.py`が発行する独自の
  UARTブリッジRTSリセットは既に**2回目（partial cal）**になってしまう——実際に1回この
  トラップを踏み，`a2(cal_mode)`実測値が`0`（`PHY_RF_CAL_PARTIAL`）になった
  （`r19_stock_postflash.summary.txt`）。
- 対策：`esptool --after no-reset write-flash --erase-all ...`で消去＋書込みを1コマンド化し，
  **自動リセットを起こさせない**（"Staying in bootloader."で確認）。ところがこの状態から
  `rts_reset.py`単体でリセットすると，今度は**ROM/ダウンロードモード関連の別経路に着地して
  アプリが一切起動しない**（`register_chipv7_phy`エントリbpが40秒待っても一度もヒットしない）
  現象を2回連続で踏んだ——原因は未確定のまま棚上げし（`--after no-reset`とCP2102のDTR/RTSラッチの
  相互作用が疑わしいが本ラウンドでは深追いせず），**`erase-flash`単体→`write-flash`（既定の
  自動hard-reset）→同一bashコマンド内で即座に`r19_capture.py`を実行**（LLM往復無しでチェイン
  し，自動リセット後の空白時間を最小化）という運用に切替えて再挑戦した。3回目でようやく
  **`a2(cal_mode)=2`（`PHY_RF_CAL_FULL`）を実測で確認**（`r19_stock_fullcal4.summary.txt`）——
  これが本ラウンドの基準となる「真のfull-cal」捕捉である。
- 副産物（新規bad-zone）：full-cal狙いの再試行中，2回連続で`0x42030c84`前後（stockの
  `phy_pwdet_tone_start`関数内）に初期halt着地し，以後`register_chipv7_phy`エントリに
  到達しなくなる現象を確認した——実施17の`sil_dly_nse`（ASP3固有）と同型の「JTAG haltが
  リアルタイムRFトーン測定ループの内部状態を壊す」現象の**stock版**と推定される（原理は
  未解明のまま，`r19_capture.py`/`r19_bp_writer.py`にbad-zone自動リトライとして実装・記録のみ）。

### 3. 判定：cal_mode交絡は解消——full-cal同士でも`phy_param[176..183]`の分岐は残る

| 捕捉 | `a2`(cal_mode) | `phy_param[35]` | `phy_param[176..183]`（6バイト） |
|---|---|---|---|
| stock, uncontrolled（実質partial） | `0`(PARTIAL) | `0x00` | `04 07 04 07 04 05`（channel依存） |
| **stock, 実測full-cal確定** | **`2`(FULL)** | `0x00` | **`05 07 04 07 03 05`（channel依存）** |
| ASP3（常にFULL，静的に既知＋実測でも`2`確認） | `2`(FULL) | `0x00` | `07 08 07 08 07 08`（channel非依存・実施16-18と再現一致） |
| 実施18（過去ラウンド，cal_mode未確認） | 不明 | ASP3=`0x00`/stock=`0x01` | stock=`04 07 04 07 03 05` |

**判定：cal_mode交絡は反証された（不成立）**。stockのfull-cal実測値（`05 07 04 07 03 05`）は
partial-cal実測値（`04 07 04 07 04 05`）およびpast-round実施18の値（`04 07 04 07 03 05`）と
±1カウントの範囲でしか違わない（実施15/16が確立済みの起動毎アナログノイズ帯）——**full/partial
どちらのcal_modeでもstockはchannel依存の健全な値を出す**。一方ASP3はcal_mode=FULLで
stockと揃えても`07 08`固定のまま——**cal_modeの違いは4b（実施16-18のtxcap分岐発見）の
原因ではないと確定**。

`phy_param[35]`については全4条件で`0x00`（loop-top時点）——**stockもfull-cal/partial-cal
問わずloop-top時点では`0x00`**。これは実施18の「stock=`0x01`」（＝別の，`register_chipv7_phy`
呼出しが完全に完了した後のwifi_scanの per-channel `phy_iq_est_enable_new`再呼出し時点）とは
**異なる文脈での比較**だったことを意味する（4節で訂正）。

### 4. `register_chipv7_phy`引数の実測比較：init_dataはバイト同一，cal_modeも揃えて分岐せず

同一boot内（`r19_asp3_cap1`）でASP3の`register_chipv7_phy`エントリも捕捉：
`a0(init_data)=0x42067510` `a1(cal_data)=0x4080ae78` `a2(cal_mode)=2`。

`a0`が指す256バイト構造体をASP3・stock（`r19_stock_fullcal4`）で`mdw`ダンプし先頭40バイト
（`CONFIG_ESP_PHY_MAX_TX_POWER`依存のTX電力テーブル部分）を比較：

```
ASP3 : 4c505000 4c444c4c 4c444448 40444448 4044483c 48484840 40444844 44483c40 383c4040 3c404044
stock: 4c505000 4c444c4c 4c444448 40444448 4044483c 48484840 40444844 44483c40 383c4040 3c404044
```

**完全一致**。さらに最終advisorチェックで構造体全256バイト＝64ワードを機械照合した
結果，**不一致ゼロ**——idx40〜のゼロ埋め領域も，唯一の非ゼロ末尾ワード`+0xfc`＝
`0xf5000000`（＝最終バイト255=`0xf5`，`PHY_INIT_DATA_TYPE_OFFSET`=254近傍のタイプ/
チェック域）も両platformで同一。構造体境界以降＝隣接rodataは無関係な内容で当然相違
（ASP3側は`PHYINIT\0`マジック文字列の別配置，stock側はドライバ名文字列群——構造体外
なので意味なし）。

**結論：init_data（バイト同一）・cal_mode（2=FULLで揃えた比較でも同一）のいずれも
`register_chipv7_phy`への入力としては分岐していない**。それでも`phy_param[176..183]`の
出力は分岐する——申し送り2「引数同一なのにphy_paramが分岐する場合」に該当，発行元の
内部処理（探索アルゴリズムが依存する測定入力）に踏み込む必要がある。

### 5. hw watchpointは本ターゲットで機能せず（実測で確認・反証）——実施18指定の代替手法（書込み命令アドレスへのbp）へ切替え

task指示の「hw watchpoint（OpenOCD `wp`）」をまず試した。`phy_param+176`（ASP3，
`0x40800300`）へ`wp 0x40800300 1 w`を設定し`resume`——20秒間，一度も発火せず
（`r19_asp3_wp176b`）。念のため**必ず数百ms以内に書き込まれることが分かっている**
`phy_param+18`（`register_chipv7_phy`冒頭で`sb`即書込み）でも同様に試したが20秒間
一度も発火せず（`r19_asp3_wp18_sanity`）——これはwatchpoint機構そのものが本
OpenOCD/esp32c5組合せで機能していないことの決定的な反証（3回連続，既知の近接書込み
アドレスですら不発）。`Warn : [esp32c5] Failed to read memory via program buffer.`
という接続時ログと符合する制限と推測される。**task記載の代替手段
「実施18のra手法（当該書き手関数の入口/出口breakpoint）」へ計画通り切替えた**。

### 6. `phy_param[176..183]`の真の書込み元を特定：`phy_rfcal_txcap`の2つの`sb ...,0(s1)`命令

`phy_tx_cap_init`→`phy_rfcal_txcap`（ASP3=`0x4202bb42`，stock=`0x42035c74`，objdump比較で
命令列完全一致，実施18の「同一コード」判定と整合）を全命令逆アセンブルした。この関数は
呼出し元から渡されたポインタ`s1`（第4引数`a3`）へ**間接**に書込む——`phy_param+176`という
symbolic名がobjdumpに出ないのはこのため（symbolテーブルには解決できない実行時ポインタ経由
の書込み）。書込みサイトは2箇所：
- サイト1（`sb a5,0(s1)`，ASP3=`0x4202bb92`／stock=`0x42035cc0`）：探索ループ突入直前，
  シード値（`li a5,7`／`li a5,8`のリテラル——各(cap0,cap1)ペアの第1バイトなら7・
  第2バイトなら8，ペア内インデックス`s0`で切替）をそのまま`s1[0]`へ書く
  （※最終advisorチェックでの訂正：当初「2.4GHz帯=7／5GHz帯=10」と誤読していたが，
  `li a0,10`は`phy_pbus_force_test`へ渡すコマンド引数であってシードではない。実測
  ヒットのa5=07/08交互パターンとも7/8解釈が整合）。
- サイト2（`sb s11,0(s1)`，ASP3=`0x4202bbe8`／stock=`0x42035d1a`）：`phy_pbus_force_test`で
  トーンを発生させ`phy_get_tone_sar_dout`でSAR ADC読み（`a5`）を取った後，
  `bge s7,a5,skip`（＝直前までの最良値`s7`以下なら書かない）というmax探索の更新条件が
  真の時だけ`s1[0]`を**候補値`s11`で上書き**する（`s11`はサイト1のシード値から1ずつ
  減じながら候補を振る，`s7`は初期値0）。

このサイト1/2の両方に`r19_bp_writer.py`（`sil_dly_nse`系bad-zone自動リトライ＋実施17の
SWD watchdog無効化を流用，長時間観測のため）でhw bpを張り，`s1`（書込み先アドレス）・
`a5`／`s11`（書込み値）・`ra`（呼出し元）を実測した：

**ASP3**（8ヒット，全て+2.7秒〜+5.6秒）：
```
hit0 s1=0x40800300 a5=0x07  (=phy_param+176, サイト1)
hit1 s1=0x40800301 a5=0x08  (=phy_param+177, サイト1)
hit2 s1=0x40800302 a5=0x07  (=phy_param+178, サイト1)
hit3 s1=0x40800303 a5=0x08  (=phy_param+179, サイト1)
hit4 s1=0x40800304 a5=0x07  (=phy_param+180, サイト1)
hit5 s1=0x40800305 a5=0x08  (=phy_param+181, サイト1)
hit6 s1=0x40800522 a5=0x07  (別テーブル領域, サイト1)
hit7 s1=0x40800523 a5=0x08  (同上, サイト1)
```
**8ヒット全てがサイト1（`0x4202bb92`）——サイト2（`0x4202bbe8`）は一度も発火しなかった**。

**stock**（8ヒット，+1.6秒〜+4.4秒）：
```
hit0 s1=0x40815478 a5=0x07                 (サイト1，シード)
hit1 s1=0x40815478 a5=0xbb s11=0x07 (サイト2，候補s11=7，測定値0xbb)
hit2 s1=0x40815478 a5=0xd9 s11=0x06 (サイト2，候補s11=6，測定値0xd9)
hit3 s1=0x40815478 a5=0xe1 s11=0x05 (サイト2，候補s11=5，測定値0xe1)
hit4 s1=0x40815478 a5=0xe2 s11=0x04 (サイト2，候補s11=4，測定値0xe2)
hit5 s1=0x40815479 a5=0x08                 (サイト1，次バイトのシード)
hit6 s1=0x40815479 a5=0xce s11=0x08 (サイト2)
hit7 s1=0x40815479 a5=0xe1 s11=0x07 (サイト2)
```
**stockはサイト1（シード書込み）の直後に必ずサイト2（探索更新）が複数回発火**し，
候補`s11`を振るたびに**実測値`a5`が変化**している（`0xbb→0xd9→0xe1→0xe2`）——
探索アルゴリズムが機能し，最終的にシードとは異なる値へ収束しうる状態。

### 7. ★新知見：ASP3はtxcap探索ループの「候補評価」自体に一度も入っていない

サイト2の発火条件は`bge s7,a5,skip`（`s7`＝直前までの最良測定値，初期値0，`a5`＝今回の
SAR ADC測定値）——**`a5 > s7`（初回は`a5 > 0`）が一度も成立しなければサイト2は永遠に
発火しない**。ASP3が8ヒット全てサイト1（シードのみ）だったという事実は，**ASP3では
`phy_get_tone_sar_dout`が返す測定値が初回から`0`以下（探索の起点を更新できない値）で
あり続けている**ことを直接示す。つまり最終的に観測される`phy_param[176..183]`の
固定値`07 08 07 08 07 08`は，「探索した結果たまたま同じ値に収束した」のではなく，
**「シード値のまま一切更新されずに終わっている」**——探索処理そのものが空振りしている。

これは実施16-18が到達した「4bは同一コードだがテーブル内容が違う」という結論を，
**「同一コードの内部で，キャリブレーション探索ループの評価入力（トーン自己ループバック
のSAR ADC読み，`phy_pbus_force_test`→`phy_get_tone_sar_dout`）がASP3側で機能していない」**
という，1段深い具体的な機構レベルまで特定した新知見である。この先（`phy_get_tone_sar_dout`
・`phy_pbus_force_test`が実際に読む生のregi2c/MMIO値がASP3でどう振る舞うか）は，
本ラウンドの深追い上限（書き手関数とその直接の入力読取り，数百命令以内）を超えるため
立ち入っていない——次段の最有力候補として申し送る。

### 8. `phy_param[35]`の再解釈：cal_modeフラグではなく「`register_chipv7_phy`完了マーカー」——4cは実質ハングの同義反復と訂正

`register_chipv7_phy`本体を全命令逆アセンブルした結果，`phy_param[35]`への**唯一の書込み**は
関数末尾近く（ASP3=`0x42024926`，`li a5,1; sb a5,35(s0)`）に1箇所だけ存在し，**`cal_mode`
（`s4`）の値に一切依存しない無条件書込み**であることを確認した（このため6節冒頭で
watchpointが機能しないと分かった際も，このアドレスへの直接bpという代替検証手段が
用意できた——ただし本ラウンドでは3節のcal_mode実測結果で目的を達したため，この
bp自体は未実行）。この書込みは関数の`ret`直前，`phy_rf_init`／`phy_bb_init`
（`phy_iq_est_enable_new`のloop-top/ret捕捉ポイントを含む，txcapループもここより前）
より**後**に位置する。

すなわち`phy_param[35]`は「`register_chipv7_phy`の当該呼出しが**最後まで完了したか**」
を示すマーカーであり，cal_modeそのものをエンコードしたフラグではない。3節の実測
（stock full-cal／partial-cal問わずloop-top時点で`0x00`）はこの解釈と整合する。
実施18が観測した「stock=`0x01`」は，**`register_chipv7_phy`の当該1回の呼出しが
既に完全に完了した後**（＝boot直後のPHY初期化ではなく，実施17が特定済みの
「wifi_scanのチャネル切替毎に`phy_iq_est_enable_new`が再呼出しされる」文脈，
`ra=0x42032a82`系統）でのサンプルだったために`0x01`だったと考えられる——時系列が
異なる2つの文脈を同じ「loop-top」というラベルで比較していたことになる。

この解釈のもとでは，**4c（「stockはこの読み出しを一度も行わない」）はASP3の
`register_chipv7_phy`呼出しが完了しない＝ハングそのものの自明な帰結**であり，
ハングの原因を説明する独立した新情報ではない（実施18時点の「フラグの上流原因を
追う」という申し送りは，本ラウンドの実測で**目標を失った＝追う必要がなくなった**
と判定する）。実施18の「4bと4cは独立した症状」という時系列判定自体は覆らないが，
4cの解釈（「platform間で分岐する制御フラグ」）は本ラウンドで訂正する。

### 9. 修正：未実装（真因は探索アルゴリズムの測定入力側にあり，本ラウンドの深追い上限を超えるため）

`register_chipv7_phy`への入力（init_data・cal_mode）はバイト同一・値同一と確定した以上，
ASP3移植層（`asp3/target/esp32c5_espidf/`）側に明確な移植漏れ（欠落コード・誤った定数）は
見当たらない。真因の手がかりは`phy_get_tone_sar_dout`／`phy_pbus_force_test`という，
libphy.a内部のトーン自己ループバック測定チェーン（regi2c／MMIO経由でRFを自己ループバック
させ，その結果をSAR ADCで読む）に移っており，これは実施14/15が到達した「WiFi RF専用
regi2cブロックの不可視アナログ状態」という同じ壁に近い領域である可能性がある
（advisorレビュー時の指摘のとおり，両方の分岐がRF較正未収束という同一の根に帰着し，
「ハードウェア/アナログの壁」であって移植バグではない可能性を排除できない）。
task指示「特定に至らなければ修正せず記録」に従い，本ラウンドでは修正を実装していない。

### 10. C5#1の最終状態

C5#1はASP3計装ビルド（`build/c5_idf61_trace/asp_flash.bin`，実施16-18の計装込み，
本ラウンドはソース変更なし）へ書き戻し済み。UARTブリッジRTSリセットでの最終確認：

```
ESP-ROM:esp32c5-eco2-20250121
rst:0x1 (POWERON),boot:0x18 (SPI_FAST_FLASH_BOOT)
ESP-ROM:esp32c5-eco2-20250121
rst:0x12 (RTC_SWDT_SYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
...(以下 約3.5秒周期で継続)
```

実施13〜18と症状完全一致（WDTリブートループ）——本ラウンドの多数回のflash消去／
stock再書込み／JTAG計装は，最終的にASP3ビルドへ書き戻した後のハング症状に影響しない
ことを確認した。

### 申し送り（次段）

1. **最優先**：`phy_get_tone_sar_dout`（ASP3=`0x42027018`）と，それが呼ぶ
   `phy_pbus_force_test`（`0x4205c502`，トーン発生＋pbus経由のRF自己ループバック
   設定と推定）を逆アセンブル・JTAG A/B比較し，ASP3で測定値が初回から0以下に
   張り付く理由（regi2cブロック未応答／pbus MMIO設定漏れ／トーン発生自体が
   出ていない等）を特定する。7節の新知見（サイト2に一度も入らない＝探索の
   起点自体が機能していない）が出発点。
2. `phy_pbus_force_test`は`phy_rfcal_pwrctrl`等，txcap以外のRF較正関数からも
   広く呼ばれている共通ルーチンの可能性が高い（`nm`で確認要）——もしここが
   真因なら，phy_param[176..183]だけでなく実施14/15が「regi2c内部の不可視
   アナログ状態」と表現していた領域全体の説明にもなりうる。
3. cal_mode交絡は本ラウンドで確定的に解消済み——今後このライン（NVS較正
   モード差）を再提案する場合は，本ラウンドのfull-cal実測結果
   （`r19_stock_fullcal4.*`）を反証根拠として提示すること。
4. hw watchpointは本OpenOCD/esp32c5組合せで機能しない（3回連続不発，
   既知の近接書込みアドレスでも不発）——今後の同種調査は最初から
   命令アドレスへのbp方式（6節の手法）を使うこと。
5. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新はコーディネータ側で
   行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施19）

- 本doc（`docs/c5-bringup.md`）：実施19セクション追記のみ。
- ソースコード変更なし（`asp3/target/esp32c5_espidf/`・`asp3/arch/`とも無変更，
  task指示「特定に至らなければ修正せず記録」に従い9節の理由で未実装）。
- スクラッチ（`c75ad9fa-5310-4781-9013-97e0c0ec7812/scratchpad/`）：
  `r19_capture.py`（新規，register_chipv7_phyエントリ+loop-top/retの2段階捕捉，
  platform別bad-zone対応），`r19_watchpoint.py`（新規，hw watchpoint試行——
  本ラウンドでは機能しないと判明したが手法として記録），`r19_bp_writer.py`
  （新規，txcap書込みサイトへの直接bp，SWD watchdog無効化込み）。生データ：
  `r19_stock_boot1_freshcal_console.log`（消去後初回UART確認），
  `r19_stock_postflash.*`（uncontrolled=partial cal実測），
  `r19_stock_fullcal.*`/`r19_stock_fullcal2.*`（download-mode着地で失敗，
  記録として残す），`r19_stock_fullcal3.*`（bad-zone着地でリトライ），
  `r19_stock_fullcal4.*`（**成功，本ラウンドの基準full-cal捕捉**），
  `r19_asp3_cap1.*`（ASP3のregister_chipv7_phyエントリ+phy_param捕捉），
  `r19_asp3_wp176b.*`/`r19_asp3_wp18_sanity.*`（watchpoint不発の記録），
  `r19_asp3_txcapwriter.*`/`r19_stock_txcapwriter.*`（6-7節のtxcap書込み
  サイトbp実測，本ラウンドの中心的結果），`r19_asp3_final_restore_console.log`
  （最終復帰確認）。

### 検証（実施19）

- ビルド：本ラウンドはソース変更なし，既存ビルド成果物（ASP3=`build/c5_idf61_trace`，
  stock=`stock_scan/build`）をそのまま使用。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - stock flash全消去：**2回**（それぞれ`esptool erase-flash`で全域消去を確認）。
  - stock書込み：**4回**（uncontrolled 1回＋`--after no-reset`失敗2回＋
    hard-reset chaining成功1回，postflashの追加書込み1回を含め計5回の
    `write-flash`実施）。
  - stock UARTブリッジRTSクリーンブート：uncontrolled確認1回（34AP検出，
    完走）＋`r19_capture.py`によるJTAG捕捉4回（内2回はdownload-mode着地で
    STAGE1未到達，1回はbad-zone着地でリトライ後成功，最終1回が
    `a2=2`のfull-cal基準捕捉）＋`r19_bp_writer.py`によるtxcap書込みサイト
    捕捉1回（bad-zone着地1回のリトライ含む）＝計6回のクリーンブート。
  - ASP3書込み・UARTブリッジRTSクリーンブート：`register_chipv7_phy`エントリ
    +phy_param捕捉1回＋watchpoint試行2回（不発，機構自体の反証）＋
    txcap書込みサイト捕捉1回（bad-zone着地5回のリトライ含む）＋最終復帰
    確認1回＝計5回のクリーンブート。
  - cal_mode実測：ASP3=`2`（静的既知と一致）／stock=`0`（partial，1回）・
    `2`（full，1回，本ラウンドの主要成果）を`a2`レジスタ直読みで確定。
  - init_dataバイト比較：ASP3/stock先頭40バイト完全一致を確認（構造体全体は
    256バイト，残りはゼロ埋めで両platform一致，構造体外は無関係のため未比較）。
  - watchpoint機構の反証：3回（`phy_param+176`・`phy_param+18`sanity×2）
    全て20秒間不発を確認——機能していないと判定する十分な根拠。
  - txcap書込みサイトbp：ASP3 8ヒット（全てサイト1のみ）／stock 8ヒット
    （サイト1→サイト2複数回の交互パターン）を確認，`s1`/`a5`/`s11`/`ra`
    レジスタで書込み先・値・呼出し元を直接記録。
  - 最終復帰：ASP3計装ビルドのまま，UARTブリッジRTSリセットで約3.5秒周期の
    `rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを確認——実施13〜18と症状
    完全一致，復帰完了。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，udevadm
  での読み取り専用MAC照合のみ，最終確認含め複数回実施）。

---

## 実施20：トーン測定チェーンの読み先/書き先を有界逆アセンブルで確定——全て MODEM0（0x600A0000）内部MMIO＋APB_SARADC 1ヶ所，regi2c不使用と判明。候補(b)のクロック/ICG/リセット系レジスタは実機A/Bで**全ビット一致**（regi2cは候補から除外，PCR/MODEM_SYSCONも反証）。一方，生ADCサンプルレジスタ自体はASP3=恒久ゼロ／stock=生きた変動値と確定——制御系は完全に健全なのに測定データだけが出ない，という新しい切り分け

コーディネータ指示＝実施19最優先申し送り「`phy_get_tone_sar_dout`の読み先と`phy_pbus_force_test`の書き先を
有界逆アセンブルで確定し，その読み先/書き先ブロックの状態をstock/ASP3で実測A/B比較する」に着手。
**本ラウンドもC5#1（`D0:CF:13:F0:A7:44`）のみを使用，C5#2には一切接続・操作していない**（udevadm
でのMAC照合を複数回実施，最終確認済み）。

### 1. 有界逆アセンブル：読み先/書き先を全て確定（4関数，regi2c呼出しゼロと判明）

`objdump -d`で`phy_get_tone_sar_dout`（ASP3=`0x42027018`／stock=`0x42030d66`）と，
それが呼ぶ`phy_pwdet_tone_start`・`phy_read_sar_dout`・`phy_en_pwdet`，および
`phy_pbus_force_test`（ASP3=`0x4205c502`／stock=`0x40803940`，stockは`.iram0.text`
配置）を全命令逆アセンブルした（実施18/19と同型の「同一コード，配置のみ相違」を
objdumpで再確認——4bと同じパターン）。

- **`phy_get_tone_sar_dout`**：`MODEM0+0x41C`のbit18をセット（測定有効化）
  →`esp_rom_delay_us(10)`→**4回ループ**して`phy_pwdet_tone_start()`→
  `phy_read_sar_dout(sp)`を呼び，各回の戻り値（`sp+2`のhalfword）を`s0`へ
  加算→ループ後`MODEM0+0x41C`のbit18をクリア→`delay_us(10)`→
  **`s0>>2`（4回の平均）を16bit値として`return`**。
- **`phy_read_sar_dout`**：`MODEM0+0x81C`から`0x828`まで**4ワード**を読み，
  各ワードの`bit[13:1]`と`bit[29:17]`をそれぞれ13bit値として取り出す
  （＝**1ワードに2サンプル，計8個の生ADCサンプル**）。8個の最大値を求め，
  出力バッファの`+2`（halfword）へ格納する（`phy_get_tone_sar_dout`が
  読む値はこの「8サンプル中の最大値」）。
- **`phy_pwdet_tone_start`**（ループ内で毎回呼ばれる）：
  **`APB_SARADC_CTRL_REG`（`0x6000E000`）のbit29を立てる**——
  `hal/components/soc/esp32c5/register/soc/apb_saradc_reg.h`で
  `APB_SARADC_SARADC2_PWDET_DRV`（bit29，"enable saradc2 power detect
  driven func"）と確認。続いて`MODEM0+0x41C`bit18を再セット（冗長），
  `MODEM0+0x808`のbit0をクリア→セット（トリガパルス），
  `esp_rom_delay_us(10)`後，**`MODEM0+0x80C`の`bit[16:14]`が`7`になるまで
  ポーリング**（完了ステートマシン待ち——実施13の`MODEM0+0x47C`bit16待ちと
  同型構造）。
- **`phy_pbus_force_test(a0,a1,a2)`**：`MODEM0+0x884`をread-modify-writeし
  （`bit[16:2]`へ引数を詰め，`bit1`を強制的に1にする=トリガパルス），
  **`MODEM0+0x890`のbit31（符号ビット，`bltz`判定）が立つまでポーリング**
  （完了待ち），その後`0x884`のbit1をクリア。
- **`phy_en_pwdet`**：`MODEM0+0x808`のbit1/2/3をクリアするのみ。

**結論：この測定チェーン全体（4関数）は`esp_rom_delay_us`以外の外部呼出しが
一切なく，`phy_i2c_writeReg`/`readReg`/`_Mask`（regi2c）の呼出しはゼロ**——
候補(a)「regi2c block 0x69」は本ラウンドの起点となる4関数の範囲では
**disassemblyそのもので直接除外**できる（実施14の手動regi2cリプレイ手法は
今回不要と判断）。読み書き先は全て**MODEM0（`0x600A0000`）内部MMIO**＋
**`APB_SARADC_CTRL_REG`（`0x6000E000`）1ヶ所**のみ——候補(c)寄りだが，
`hal/components/soc/esp32c5/include/modem/`にはMODEM0本体の詳細レジスタ
定義が無く（`modem_syscon`/`modem_lpcon`のみ），これらのオフセット
（`0x41C`/`0x808`/`0x80C`/`0x81C`/`0x884`/`0x890`）はblob内部の
未公開レジスタと確認した。

### 2. 検討したが早々に反証したリード：`esp_phy/src/phy_override.c`の未リンク

`hal/components/soc/esp32c5/register/soc/pcr_reg.h`と
`hal/components/hal/esp32c5/include/modem/modem_syscon_reg.h`
（およびESP-IDF v6.1のC言語ソース，`esp_hw_support/port/esp32c5/
sar_periph_ctrl.c`・`esp_phy/src/phy_override.c`）を読み，
`set_xpd_sar()`/`phy_set_pwdet_power()`（`sar_periph_ctrl_pwdet_power_acquire()`
経由で`modem_clock_module_enable(PERIPH_MODEM_ADC_COMMON_FE_MODULE)`＋
`regi2c_saradc_enable()`＋`sar_ctrl_ll_set_power_mode_from_pwdet()`を呼ぶ）
という，実施13のICG型移植漏れと同型に見えるリードを発見した。

`nm`で確認したところ，ASP3の`asp.elf`には`set_xpd_sar`・`phy_set_pwdet_power`・
`sar_periph_ctrl_pwdet_power_acquire`・`regi2c_saradc_enable`が**一切存在せず**
（stockには全て強シンボルとして存在），`phy_i2c_enter_critical`/
`phy_i2c_exit_critical`もASP3では弱シンボル（stockは強シンボル）——
`asp3/target/esp32c5_espidf/esp_wifi.cmake`のソースリストに
`esp_phy/src/phy_override.c`が含まれていないことに起因すると確認した。

しかし，`libphy.a`を`ar x`で展開し全20個の`.o`を`nm -u`で走査した結果，
**`set_xpd_sar`／`phy_set_pwdet_power`／`regi2c_saradc_enable`／
`sar_periph_ctrl_pwdet_power_acquire`のいずれもblob側から一度も参照
されていない**（1節の4関数の完全逆アセンブルとも整合——外部呼出しは
`esp_rom_delay_us`のみ）。**このリードは反証した**——`phy_override.c`の
未リンクは実在する差分だが，`phy_rfcal_txcap`が使うトーン自己ループバック
測定チェーンとは無関係（別のADC経路，例えば`esp_adc`ドライバや
`temperature_sensor`ドライバ用の一般的な電源シーケンスと推測される）。
次ラウンドが同じリードを再発見して再度潰す手戻りを避けるため明記する。

### 3. 実機A/B：候補(b)のクロック/ICG/リセット系レジスタは全ビット一致

新規スクリプト`r20_capture.py`（実施15-19の再接続競争手法を流用）で，
2箇所のブレークポイントヒット時点（STAGE1=`register_chipv7_phy`エントリ
＝静的/初期化時点，STAGE2=`phy_get_tone_sar_dout`エントリ＝実測定時点）
で以下を一括ダンプする計装を実装した：

| レジスタ | アドレス | 内容 |
|---|---|---|
| PCR_SARADC_CONF | `0x60096088` | CLK_EN(b0)/RST_EN(b1)/REG_CLK_EN(b2)/REG_RST_EN(b3) |
| MODEM_SYSCON_CLK_CONF | `0x600A9C04` | PWDET_SAR_CLOCK_ENA(b0) |
| MODEM_SYSCON_CLK_CONF_POWER_ST | `0x600A9C0C` | ICGビットマップ群（WIFI[23:20]/BT[19:16]/FE[15:12]/ZB[11:8]等） |
| MODEM_SYSCON_MODEM_RST_CONF | `0x600A9C10` | RST_FE_ADC(b12)/RST_FE_PWDET_ADC(b10) |
| MODEM_SYSCON_CLK_CONF1 | `0x600A9C14` | CLK_FE_ADC_EN(b20)/CLK_FE_PWDET_ADC_EN(b19) |
| PMU HP_ACTIVE icg_modem | `0x600B000C` | 実施13で確定済みのICGコード（bit31:30） |
| MODEM0+0x41C/0x808/0x80C/0x884/0x890 | — | 1節で確定した測定チェーンの制御レジスタ |

**結果（ASP3 3ブート・stock 3ブート，全て再現一致）**：

STAGE1（静的）・STAGE2（実測定時点）とも，**上記レジスタは1ビットの
例外を除き全て完全一致**した。唯一の差：`PCR_SARADC_CONF`の
`REG_CLK_EN`(bit2)がASP3ではSTAGE1で`0x1`（未セット）だが**STAGE2までに
`0x5`へ変化**（stockはSTAGE1から既に`0x5`）——実際の測定が起きる
STAGE2時点では両platformとも一致するため，症状（値が出ない）への
因果はない（起動シーケンスの前後関係の違いに過ぎない）。

`MODEM_SYSCON_CLK_CONF_POWER_ST`＝`0x64646400`（両platform・両stage共通）
を実測でフィールド分解すると，`CLK_FE_ST_MAP`（bit[15:12]）＝`0x6`
（`BIT(1)|BIT(2)`）——実施13で確定した`CLK_WIFI_ST_MAP`と同じ`0x6`で，
`icg_modem.code=2`（`PMU_HP_ACTIVE`実測`0x80000000`が示す値）はこの
ビットマップに含まれる＝**FEドメインもWIFIBBと同様，実施13の修正で
既にICGゲート通過状態**であることを確認した。`CLK_FE_ADC_EN`(b20)・
`CLK_FE_PWDET_ADC_EN`(b19)も両platformとも`1`（`CLK_CONF1=0x003be7ff`）。

追加で1ブートずつの探索的確認（`r20_pwdet_conf_check.py`）として
`PWDET_CONF_REG`（`0x600A0810`，`hal/.../sar_ctrl_ll.h`の
`SAR_POWER_FORCE`(b24)/`SAR_POWER_CNTL`(b23)）と`APB_SARADC_CTRL_REG`の
ベースライン値も比較し，いずれも**完全一致**（ASP3=stock=`0x0f0f0fff`・
`0x60038240`）を確認した。

**判定：候補(a)regi2cは1節のdisassemblyで最初から除外，候補(b)PCR/
MODEM_SYSCON系クロック・ICG・リセットは実測で反証**——実施13型の
「ICG移植漏れ」パターンはこのチェーンには存在しない。

### 4. 生ADCサンプルレジスタ自体はASP3=恒久ゼロ，stock=生きた変動値（新規確定）

同一ブート内でSTAGE2直後に`MODEM0+0x81C`から4ワード（1節で確定した
生ADCサンプル本体）を追加ダンプし，さらに`phy_get_tone_sar_dout`の
`ret`アドレス（ASP3=`0x42027086`／stock=`0x42030dd4`，実施18/19と同じ
「同一コード」）にもbpを張って`a0`（そのコールの計算結果＝4回平均）を
直接読む，というSTAGE3を追加した。

```
ASP3  (boot3): MODEM0+0x81C(4w) = 00000000 00000000 00000000 00000000
               a0(このコールの平均) = 0x00000000 (0)
stock (boot3): MODEM0+0x81C(4w) = 04ec0514 04ec04f0 07d407d0 07d207d2
               a0(このコールの平均) = 0x00000435 (1077)
```

制御レジスタ（3節）が完全一致するにもかかわらず，**ASP3では生ADC
サンプル自体が恒久的にゼロ**（3ブート中STAGE2の直前サンプル領域も
含め毎回`00000000`×4を確認），**stockは非ゼロで変動する実測値**——
実施19の「`phy_get_tone_sar_dout`の戻り値が初回から0以下」という
間接的な結論を，**`a0`レジスタ直読みという独立した手法で追試確認**した
うえで，一段深い「そもそも生サンプルレジスタが1個も有効な値を
返していない」という機構レベルまで特定した。

### 5. タイミング所見：ASP3は`register_chipv7_phy`到達が stock より**遅い**（速いのではない）

`r20_capture.py`のSTAGE1ヒット時刻（UARTブリッジRTSリセットからの
実時間）を4ブートずつ比較：ASP3＝`+2.335s`/`+2.272s`/`+2.277s`/`+2.230s`，
stock＝`+1.047s`/`+1.184s`/`+1.289s`/`+1.041s`/`+1.110s`。**ASP3の方が
約1秒遅い**（速くない）——「Direct Bootは経路が短くアナログ回路の
安定待ち時間が足りない」という直感的仮説には**反証方向の材料**となる
（起動シーケンス全体の実時間は短縮されていない）。ただし，これは
「PHY初期化に至るまでの総経過時間」であり，「SAR ADC電源投入から
測定開始までの区間内の特定ステップに明示的なdelayが欠けている」という，
より局所的な仮説までは反証しない。

### 6. 因果検証：実施せず（差分レジスタが見つからずJTAG注入対象がない）

3節・4節・5節の結果，制御レジスタは1ビットも有意差がなく，注入すべき
具体的な「直したら直るはずのビット」が本ラウンドでは見つからなかった。
そのため実施13型の「JTAGで書き換えて症状が変わるか確認する」という
因果検証は**対象が無く実施していない**（task指示のとおり，反証実験は
可能な範囲で先行させたが，本ラウンドの発見は一貫して「候補を反証する」
方向であり，「これを直せば治る」という正の候補は得られなかった）。

### 7. 修正：未実装（正の候補が無いため）

3〜6節のとおり，ASP3移植層（`asp3/target/esp32c5_espidf/`・
`asp3/arch/riscv_gcc/esp32c5/`）に対する具体的な移植漏れ・誤設定は
本ラウンドでは特定できなかった（2節の`phy_override.c`未リンクは実在
するが無関係と確認済み）。task指示「特定に至らなければ修正せず記録」に
従い，修正は実装していない。

### 8. C6 deaf-RXとの構造比較

C6のdeaf-RX（83ラウンドで「software/JTAG+物理RF刺激では解決不能」と
結論）と，C5の本症状は表層的に類似する（＝「制御系はビット単位で
完全に生きているのに，最終的な信号/データだけが得られない」）が，
構造は異なる：

- **C6**：外部からの実RF信号（既知の強いWi-Fiビーコン）を**受信**する
  経路が，MAC割込みは正常に発火するのに`lmacRxDone`より先で恒久的に
  ゼロ——「受信」の壁。
- **C5（本症状）**：外部信号は一切関与しない**自己ループバック**
  （blobがトーンを内部発生させ，同じチップ内でSAR ADCへ折り返して
  測る）チェーンが，全てのデジタル制御レジスタが健全なまま，恒久的に
  ゼロを返す——「自己完結した測定」の壁。

C5の方が変数が少ない（外部アンテナ・距離・環境ノイズが原理的に無関係）
分だけ切り分けやすいはずだが，**制御レジスタが全て一致するのに結果だけ
異なる**という点は，C6が83ラウンドかけて到達した「レジスタ的には健全，
それでも機能しない」という同型の壁に，C5はわずか2ラウンド（実施19-20）
で到達したとも言える。ただしC5にはC6ほどのラウンド数の反証実験が
まだ積まれていない——次段でPLLロック状態や，本ラウンドが検査していない
`ANA_I2C_MST`（`0x600AF800`）経由の他のregi2cブロックを確認する余地が
残っている（8節時点でC5を「C6と同種の壁で打ち切り」と判定するのは
時期尚早）。

### 9. C5#1の最終状態

C5#1はASP3計装ビルド（`build/c5_idf61_trace/asp_flash.bin`，実施16-19の
計装込み，本ラウンドはソース変更なし）へ書き戻し済み。UARTブリッジ
RTSリセットでの最終確認：

```
ESP-ROM:esp32c5-eco2-20250121
Build:Jan 21 2025
rst:0x12 (RTC_SWDT_SYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
ESP-ROM:esp32c5-eco2-20250121
Build:Jan 21 2025
rst:0x12 (RTC_SWDT_SYS),boot:0x18 (SPI_FAST_FLASH_BOOT)
```

約3.5秒周期の`rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを再現——
実施13〜19と症状完全一致。本ラウンドの多数回のflash消去／stock再書込み
／JTAG計装は，最終的にASP3ビルドへ書き戻した後のハング症状に影響しない
ことを確認した。

### 申し送り（次段）

1. **最優先**：`phy_rfpll`（RF PLLロック）関連のレジスタ/regi2c応答を
   実測A/B比較する——3節で制御レジスタが全一致と確定したため，次の
   有力な差分候補は「PLLが実際にロックしているか」という，本ラウンドの
   範囲外だったRF状態にある。
2. `ANA_I2C_MST`（`0x600AF800`）経由で，txcap探索より前の初期化段階
   （`phy_bb_init`・`register_chipv7_phy`本体の前半）に，本ラウンドが
   トレースしていない別のregi2cブロックへの書込みが無いか，実施16-18
   で構築済みの`wifi_regi2c`トレース計装（既に`build/c5_idf61_trace`に
   組み込み済み）の生ログを`register_chipv7_phy`エントリより前の区間で
   再確認する価値がある（追加ビルド不要，既存計装の時間窓を変えるだけ）。
3. 2節の`phy_override.c`未リンクは**反証済み**——再調査不要（次段が
   同じリードを再発見して再度潰す手戻りを避けるためここに明記する）。
4. 5節のタイミング所見（ASP3が約1秒遅い）から「delay不足」の単純な
   仮説は後退した。単純な`delay_us`追加のような場当たり的な修正は
   推奨しない。
5. `memory/project_c6_agc_investigation.md`・`MEMORY.md`更新はコーディネータ側
   で行う運用（CLAUDE.md記載の通り）。

### 変更ファイル（実施20）

- 本doc（`docs/c5-bringup.md`）：実施20セクション追記のみ。
- ソースコード変更なし（`asp3/target/esp32c5_espidf/`・`asp3/arch/`とも
  無変更，7節の理由で未実装）。
- スクラッチ（`c75ad9fa-5310-4781-9013-97e0c0ec7812/scratchpad/`）：
  `r20_capture.py`（新規，STAGE1/2/3の3段階捕捉，3節・4節のレジスタ
  一括ダンプ），`r20_pwdet_conf_check.py`（新規，PWDET_CONF_REG／
  APB_SARADC_CTRL_REGの単発A/B確認，2回流用）。生データ：
  `r20_asp3_boot1/boot2/boot3_stage3.*`・`r20_stock_boot1/boot2/
  boot3_stage3.*`（3節・4節の実測値，各`.summary.txt`に集約），
  `r20_asp3_final_restore_console.log`（9節の最終確認）。

### 検証（実施20）

- ビルド：本ラウンドはソース変更なし，既存ビルド成果物（ASP3=
  `build/c5_idf61_trace`，stock=`stock_scan/build`）をそのまま使用。
- 逆アセンブル：`riscv32-esp-elf-objdump`で4関数（`phy_get_tone_sar_dout`・
  `phy_read_sar_dout`・`phy_pwdet_tone_start`・`phy_pbus_force_test`・
  `phy_en_pwdet`）を全命令確認，`nm -u`で`libphy.a`全20`.o`の未解決
  シンボルを走査（2節の反証に使用）。
- 実機（C5#1，`D0:CF:13:F0:A7:44`）：
  - stock flash消去→正しいオフセット（`0x2000`/`0x8000`/`0x10000`，
    `flasher_args.json`実測）での再書込み：初回に誤ったオフセット
    （`0x0`/`0x20000`）で書込んでしまい`invalid header`ブートループを
    誘発したが，`erase-flash`→正しいオフセットでの再書込みで復旧
    ・確認した（本ラウンドの作業ミスとして記録，C5#2には影響なし・
    MAC照合済み）。
  - 3節のレジスタ一括比較：ASP3 3ブート（`r20_asp3_boot1/boot2/
    boot3_stage3`）・stock 3ブート（`r20_stock_boot1/boot2/
    boot3_stage3`）で再現一致を確認。
  - 4節の生ADCサンプル・`a0`直読み：ASP3・stockとも1ブート
    （boot3_stage3）で確認（探索的追加測定のため単発，3節の
    レジスタ一致自体は3ブートで確認済み）。
  - 2節のリード反証：`nm`によるシンボル存在確認と`libphy.a`の
    `nm -u`走査で完結（実機測定不要）。
  - 最終復帰：ASP3計装ビルドのまま，UARTブリッジRTSリセットで
    約3.5秒周期の`rst:0x12 (RTC_SWDT_SYS)`WDTリブートループを確認
    ——実施13〜19と症状完全一致，復帰完了。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも一切接続・操作せず（未接触，
  udevadmでの読み取り専用MAC照合のみ，最終確認含め複数回実施）。

## 別PC再開メモ（第2版，2026-07-11 実施20完了・実施21中断時点）

**再開点＝本branch `claude/c6-wifi-c5-dev-5vc6x9` のHEAD（実施14〜20コミット済み・push済み）**。
環境構築（IDF v6.1 `~/tools/esp-idf-v6.1`・xpackツールチェーン・submodule・ビルドコマンド）は
第1版の「別PC再開メモ」（実施10直後のセクション）を参照。以下は差分のみ。

### 現在地（実施14〜20の到達点）

統一像＝**blob内トーン自己ループバック測定（トーン注入→RF鎖→BB内ADCキャプチャ）が
ASP3でのみ不動作**。生ADCサンプル（`MODEM0+0x81C..0x828`）がASP3だけハードゼロ
（stockは同一個体・同一blob・同一init_data・同一cal_mode(full)で非ゼロ変動・完走）。
一方，デジタル可視領域は徹底比較の末**全て一致**：
- MMIOスナップショット（MODEM_SYSCON/MODEM_LPCON/PMU ICG/I2C_ANA_MST/MODEM0
  0x000・0x400・0xc00域）＝実質等価，差分は全て因果棄却（実施15/17）
- regi2c発行列＝最初の測定失敗まで完全一致（実施16/18/19）
- トーン測定経路の制御レジスタ（PCR_SARADC/MODEM_SYSCON/ICG/RST/PWDET_CONF/
  APB_SARADC_CTRL）＝ビット同一（実施20）
- 引数（init_data 256バイト・cal_mode）＝同一（実施19）

### 実施21（中断・未実施扱い）：次にやること

PMU/LPアナログ電源ドメインの全域スイープ比較。**起動→着手直後にユーザ指示で
中断したため，docs追記・ソース変更・結論は一切ない**。再開時は実施21として
最初からやり直すこと。計画の要点：
1. `hal/components/soc/esp32c5/`ヘッダで`PMU`・`LP_ANA`・`LP_AON`・`LPPERI`・
   `PCR`全域のベース/サイズを確定（読取り副作用レジスタは除外し記録）
2. 採取点を揃えて（`phy_rfcal_txcap`エントリ＋`register_chipv7_phy`エントリの2点）
   stock×2・ASP3×2の4-wayスイープ
3. 差分→`hal/`ヘッダで意味解読（xpd/LDO/バイアス系最優先）→JTAG注入で因果検証
   （PMU即時反映パルス2本を忘れない，実施13参照）→移植層修正1件
4. 差分ゼロなら「Direct Boot起因の共通アナログ壁（C6-generic）」への構造比較を
   整理して停止・ユーザ判断へ
根拠：stockは`pmu_init()`/`esp_rtc_init()`実行・ASP3 Direct Bootは非実行，かつ
実施13のICGバグ（PMU域・C6では冗長だがC5では必須）という前例。C6実施30相当の
全PMU比較はC5では未実施。

### ★ボード状態の注意（中断の副作用）

- **C5#1（`D0:CF:13:F0:A7:44`）のflash内容は不確定**（実施21エージェントを
  実験途中で停止したため，stock scanイメージ／ASP3計装ビルドのどちらが
  入っているか未確認）。**再開時は必ずASP3をビルドして書き直してから始める**
  （`build/c5_idf61`または計装付き`build/c5_idf61_trace`構成，
  `-DESP32C5_WIFI_REGI2C_TRACE=ON`）。
- C5#2（`D0:CF:13:F0:C8:94`，stock v9参照機）は実施14〜21を通じて一切未接触。

### 失われる資産（スクラッチ＝セッション限り，別PCには無い）

- stockビルド一式（`stock_scan/`）→ 実施15の手順で再ビルド
  （`~/tools/esp-idf-v6.1`の`examples/wifi/scan`をコピーして
  `idf.py set-target esp32c5 && idf.py build`。flashオフセットは
  `flasher_args.json`準拠＝0x2000/0x8000/0x10000，実施20の事故記録参照）。
  regi2cトレース計装（`--wrap`）はASP3側`wifi_trace.c`と同型をstockコピーに
  再適用（実施16/18参照）
- JTAGスクリプト群（`rts_reset.py`・再接続競争・キャプチャ・シンボル解決）
  → docs実施15/17/19の記述から再作成

### 実機手順の罠 早見表（詳細は各実施）

| 罠 | 対処 | 出典 |
|---|---|---|
| JTAG `reset halt`/native USB-JTAGリセットはMODEM/PMUドメインを消さない | クリーンブート＝UARTブリッジ(ttyUSB1) RTSリセットのみ | 実施14 |
| UARTブリッジRTSリセットはJTAG接続を切断 | 再接続競争（USB再列挙~0.3s+OpenOCD再attach） | 実施15 |
| 再接続で`sil_dly_nse`内に着地するとbpが発火しない | bad-landing検出→リトライ | 実施17 |
| hw watchpoint（OpenOCD `wp`）が本環境で不発 | 書込み命令アドレスへのbpで代替 | 実施19 |
| `esptool write-flash`末尾の自動リセットが初回full-calブートを裏で消費 | erase→write→即捕捉を同一チェインで | 実施19 |
| stockのflashオフセット誤り（0x0/0x20000）でROMブートループ | `flasher_args.json`の0x2000/0x8000/0x10000に従う | 実施20 |
| WDTリブートループ中はUSB-JTAGが約3.5秒毎に再列挙＝OpenOCD切断 | 接続直後にSWD無効化（`0x600B1C20`/`0x600B1C1C` bit18） | 実施21 |
| 起動早期（+0.4〜+2.6s）のJTAG haltがブートを確率的に恒久アイドル停滞させる | 遅延attach（+2.6s以降）＋ハングループ内PC確認ゲート＋リトライ | 実施21 |
| USB-JTAGのCDCポート（ttyACM）を開くと`rst:0x15`コアリセット＝非クリーンブート | ASP3コンソールのドレインは測定中は行わない | 実施21 |

---

## 実施21：計画書`docs/c5-tone-adc-plan.md`の候補A/Bを実機で判別——**候補A（`PCR_FPGA_DEBUG` bit31）＝棄却**（予測不成立：stockもbit31=1のまま完走。真因＝計画の一次情報がhal/サブモジュール（別世代）由来で，実ビルドのIDF v6.1にはerratum対策コード自体が存在しない）／**候補B（PVT自動dbias）＝差分実在・因果棄却**（JTAG注入＋加算移植の両方で「PVT実動作」を読み戻し確認した上で症状不変）。★別PC環境固有のJTAG新罠3件（early-halt起動停滞・CDCオープン0x15リセット・WDTループ毎のUSB再列挙）を発見・回避手法確立

コーディネータ指示＝実施21改訂計画（`docs/c5-tone-adc-plan.md`）の候補A→（棄却時）候補Bの順で実機判別。
**本ラウンドもC5#1（`D0:CF:13:F0:A7:44`）のみ使用。C5#2（`D0:CF:13:F0:C8:94`）は物理切断済みで
一切未接触**（全デバイス操作はby-idパスでD0:CF:13:F0:A7:44にピン留め，udevadm照合済み）。

### 0. 環境（新PC，2026-07-12。docs記載値からの差分）

- C5#1 native USB-JTAG＝**ttyACM0**・UARTブリッジ（CP2102N `b04e3bcf…`）＝**ttyUSB0**
  （旧記録のttyUSB1から変化。スクリプトは全てby-idパス使用）。同居：S3-B（ttyACM2）・
  CH340（ttyACM1）＝別調査用，未接触。
- OpenOCD `v0.12.0-esp32-20260703`・esptool v5.3.1（`~/.espressif/python_env/idf6.1_py3.12_env`）・
  ツールチェーン`riscv32-esp-elf esp-14.2.0_20241119`（`~/tools/espressif/tools/`）。
- 前セッションのスクラッチ資産は消失（旧パス自体が存在せず）→stockビルド・JTAGスクリプト群を
  本ラウンドで再作成（新スクラッチ`260d98fa…/scratchpad/`，`r21_*.py`系）。
- 前提整備：`build/c5_idf61_trace`（`ESP32C5_WIFI_REGI2C_TRACE=ON`）は`ninja: no work to do`＝
  実施19/20とバイナリ同一（全シンボルアドレス一致をnm/objdumpで確認）。C5#1へ書込み後，
  UARTブリッジRTSクリーンブートで既知症状（約3.5秒周期`rst:0x12 (RTC_SWDT_SYS)`リブート
  ループ）の再現を確認してから着手。

### 1. ★方法論的発見（この個体/PC環境でのJTAG新罠3件。以後のラウンド全てに適用）

1. **WDTリブートループ中はUSB-JTAGが約3.5秒毎に再列挙**される（by-id symlinkのctimeが毎サイクル
   更新されるのを実測）＝ハング中のC5#1にOpenOCDを「つなぎっぱなし」にはできない。SWD無効化
   （`LP_WDT_SWD_WPROTECT`=`0x600B1C20` key `0x50D83AA1`→`SWD_CONFIG`=`0x600B1C1C` bit18
   `AUTO_FEED_EN`セット）を接続直後に行えば以後は切断されない。
2. **起動早期（+0.4〜+2.6秒）のJTAG haltがブートを確率的（体感1/2〜2/3）に恒久停滞させる**：
   halt→resume後もカーネルが`dispatcher_1`（アイドルループ）から二度と復帰せず，コンソール
   出力も止まる（PCサンプリングで+2.9s以降20秒超アイドル固定を実測）。halt時間を1行バースト
   （halt;書込み;resume＝数十ms）に短縮しても発生。着地先はコンソール出力コード
   （`sio_fput`/`sil_dly_nse`/`esp32c5_usbjtag_snd_chr`）や早期wifi-init。機構は未解明のまま
   （systimerカウンタ自体は halt間で正常進行を実測済み＝タイマ停止ではない）。
   **回避策＝遅延attach**：自然ブートは本PCでも正常にPHYハングループへ到達する
   （JTAG無介入で+2.75sにattach→PC=`phy_get_pkdet_data`内を1発読みで確認）ため，
   attachを+2.6s以降に遅らせ，+7sの「ハングループ内PC確認」をゲートにし，
   不成立ならブートごとリトライする方式で全測定を成立させた。
   ハングループは`phy_get_pkdet_data`を永久に呼び続けるため捕捉の取り逃しが無い
   （なお恒久ループが呼ぶのはpkdet系のみで，`phy_get_tone_sar_dout`はtxcap探索フェーズ
   限定と判明——tone系エントリへのbpはハング成立後には発火しない）。
3. **USB-JTAGのCDCポート（ttyACM0）を開くと`rst:0x15 (USB_UART_HPSYS)`コアリセットが発火**
   （C3の既知CDCハザードのC5版。DTR/RTS=Falseで開いても発生）。0x15はUSB再列挙を伴わないが
   MODEM/PMU/RTCドメインを消さない＝**非クリーンブート**になり測定を汚すため，
   「コンソールをドレインしながら測る」方式は不成立。ASP3のUSBコンソールはホスト未読だと
   1文字あたり約1.5ms（`sio_fput`のリトライ上限）でブートが伸びるが，実測では
   PHY到達自体は+2〜3s程度で完了するため実害なし。
4. 実施17のtelnet CR-NULパースバグを再踏（`0x600b0028:`行の先頭に`\x00`）→スクリプト側で
   `\x00`一括除去を実装。実施19の「hw watchpoint不発」も本環境で同前提とし最初からbp方式。

### 2. 候補A：`PCR_FPGA_DEBUG_REG`（`0x60096FF4`）bit31——**棄却**（判定基準は測定前に固定済み）

**事前固定の予測**：ASP3＝bit31=1（POR既定`0xFFFFFFFF`のまま）／stock＝bit31=0
（`esp_perip_clk_init()`がクリア）。**予測が外れたら注入に進まず棄却**（計画書の明文ルール）。

**ASP3実測（独立クリーンブート×2）**：
- boot1（再接続競争＋早期halt方式，3回目の試行で成立）：`register_chipv7_phy`エントリ
  （`0x4202476e`，+1.876s）と`phy_get_tone_sar_dout`エントリ（`0x42027018`，+2.905s）の
  両採取点で`0x60096FF4`=`0xFFFFFFFF`。同時に生ADC（`0x600A081C..828`）全ゼロ・
  done（`0x600A047C`）=0＝既知症状同時確認。
- boot2（遅延attach方式，ハング中）：baseline/final両方で`0xFFFFFFFF`。
- **ASP3側は予測どおりbit31=1**（かつ`asp3/target/esp32c5_espidf/`にこのレジスタへの書込みは
  grep 0件＝静的にも整合）。

**stock実測（独立クリーンブート×2，同一ブート内で陽性対照確認済み）**：
- stockビルドは`~/tools/esp-idf-v6.1` `examples/wifi/scan`から実施15と同手順で再作成
  （WDT無効sdkconfig.defaults付き，`idf.py set-target esp32c5 && build`成功，
  libphy.a MD5=`4ccdbdbe1faf04a84b4059c882febe0f`＝実施15と同一＝blob同一性Gate通過，
  flashオフセット0x2000/0x8000/0x10000）。
- boot1：UARTコンソールで**27AP検出・完走**（本PCでも陽性対照成立，実施15と同数）→
  同一ブートにattach・halt→`0x60096FF4`=`0xFFFFFFFF`（done bit16=1も確認）。
- boot2：24AP完走→同読み＝`0xFFFFFFFF`。
- **stockは予測に反しbit31=1のまま完走**——**予測不成立＝候補A棄却**（ルールどおり注入せず）。

**棄却の真因（ソースレベルで確定）**：計画書の一次情報
（`hal/components/esp_system/port/soc/esp32c5/clk.c:239`の無条件
`clk_ll_soc_root_clk_auto_gating_bypass(true)`）は**esp-hal-3rdpartyサブモジュール（別世代）の
コードであり，stock/ASP3が実際にビルドしているIDF v6.1-beta1には存在しない**
（`~/tools/esp-idf-v6.1`のclk.cにFPGA_DEBUG/auto_gating/IDF-11064への言及ゼロをgrep確認）。
v6.1で当該ビットに触れる唯一の経路は`esp_pm`のDFS（`pm_impl.c:691/695`→
`rtc_clk_root_clk_switch_protect()`，`SOC_CLK_ROOT_CLK_SWITCH_PROTECT`）で，
PLL 160M↔240M切替の間だけ一時的にbypassし直後に戻す——scanサンプルはCONFIG_PM無効で不使用。
つまり**このerratum対策は実ビルドでは両プラットフォームとも「無い」＝差分になり得ない**。
stockがbit31=1のまま較正完走・スキャン成功する実測はこれと完全整合。
（教訓：hal/サブモジュールとIDF実ツリーは世代が異なる——計画立案時の一次情報は
**実際にリンクされるツリー**で裏取りすること。）

### 3. 候補B：PVT自動dbias初期化の欠落——**差分実在（各×2再現）・因果棄却**（2独立手法で注入成立を確認済みの上で症状不変）

**前提確認（実測）**：
- stock `sdkconfig`＝`CONFIG_ESP_ENABLE_PVT=y`（Kconfig既定どおり）。
- eFuse `RD_MAC_SYS2`（`0x600B484C`）=`0x21500310`→`blk_version`=major(bit12:11)=0×100+
  minor(bit10:8)=3＝**3≧2で成立**（PVT経路はこのDUTで実際に走る）。
- v6.1ソース：`pmu_init()`（`pmu_init.c:228-238`）に加え，**PLL 160M/240MへのCPUクロック切替
  関数自体（`rtc_clk.c` `rtc_clk_cpu_freq_to_pll_240/160_mhz`）が毎回PVT初期化4関数を呼ぶ**＝
  stockは必ずPHYより前にPVT有効。ASP3はgrep 0件，かつそもそもCPUクロック切替を行わない
  （ROM設定のまま，`target_kernel_impl.c`コメント参照）＝この経路を通らない。
- `pmu_pvt.c`の4関数（`pvt_auto_dbias_init`/`charge_pump_init`/`pvt_func_enable`/
  `charge_pump_enable`）は**全てMMIO書込みのみ（regi2c不使用）**＝JTAGで忠実に再現可能。

**A/B実測（stock×2・ASP3×2，ASP3側1回はハングループ内PC＝`phy_abs_temp`←
`phy_iq_est_enable_new`を確認した上で採取）**：

| 項目 | stock（2ブート一致） | ASP3（2ブート一致） |
|---|---|---|
| `PCR_PVT_MONITOR_CONF`（`0x600960B8`） | `0x1D`（CLK_EN=1） | `0x1C`（**CLK_ENゲート**） |
| `PCR_PVT_MONITOR_FUNC_CLK_CONF`（`0x600960BC`） | `0x00500001` | （ブロック未クロックで0） |
| PVTブロック（`0x60019000`〜`0x1EF`，124w） | 設定済み＋既定値＋生きた計測値（設定値は2ブート完全一致，計測フィールドのみ変動） | **全ゼロ** |
| `PMU_HP_ACTIVE_HP_REGULATOR0`（`0x600B0028`） | `0xC004AFD0`（bit14=0＝dbias制御PVT移譲） | `0xC667F180`（bit14=1＝PMU制御のまま） |

**因果検証1＝JTAG注入（ハング成立ブートの+2.7s，txcap探索窓+2.7〜5.6sとiq_est窓を被覆）**：
stock実測ダンプ準拠の全レジスタ（PCRリセットパルス→CLK/FUNC_CLK→PVT設定群→チャージポンプ→
`PMU_REGULATOR0`のDIG_DBIAS_INITダンス→TIMER_EN）を1 haltウィンドウで書込み。
**注入成立の直接証拠**＝PMUが`0xC667F180`→`0xC667BFF0`（bit14クリア＋dbias実値がPVT駆動値に
変化）・PVT生カウンタ（`0x60019198`）が全サンプルで変動＝PVT/チャージポンプ実動作。
**30秒観測：done=0・生ADC全ゼロ・PCはiq_estループ滞留のまま**——症状不変。

**因果検証2＝加算移植（stockと同じ「PHY較正開始前」タイミングでのbuild-level A/B）**：
`esp_wifi_adapter.c`に`esp_shim_pvt_init()`を新設（`pmu_pvt.c`の実行順を忠実に再現，
値は同一個体stock実測ダンプ＝eFuse由来のチップ固有gap込み，eFuse blk_versionガード付き），
`wifi_clock_enable_wrapper()`の一度きりブロック先頭（regi2c有効化・phy_enableより前）から呼出し。
ビルド成功→書込み→JTAG検証：**移植コードの完全実行を確認**（`PCR_B8=0x1D`・
`PCR_BC=0x500001`＝stock完全一致・PMU bit14=0・PVT生カウンタ変動）。
**それでも20秒超の観測でdone=0・生ADC全ゼロ・ハングループ滞留・UART上のWDTループ症状も
移植前と完全同一**——**候補Bも因果棄却**。

**判定**：PVT初期化の欠落は実在する再現差分だったが，**トーン自己ループバック測定の
生ADCゼロ問題の原因ではない**（測定窓を被覆する2独立手法＝mid-boot JTAG注入と
較正前build移植の両方で，PVT実動作を機械確認した上で症状不変）。

### 4. 変更ファイル（実施21）

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：`esp_shim_pvt_init()`新設＋
  `wifi_clock_enable_wrapper()`からの呼出し1ヶ所＋`esp_rom_sys.h` include追加。
  **本変更は「因果確定した修正」ではない**（3節のとおり症状に影響しない）。stockとの実在差分を
  1件解消し将来のA/B比較から交絡を除く目的で作業ツリーに残置するが，採否（keep/revert）は
  コーディネータのレビューに委ねる。革命的でない証拠として：**移植前後でUART症状は同一**
  （同日A/B，3.5秒周期`rst:0x12`ループ）＝この変更を将来の症状変化の原因と誤帰属しないこと
  （`memory/feedback_hardware_investigation_rigor.md`第6再発事例の予防記録）。
- 本doc（`docs/c5-bringup.md`）：実施21セクション追記。
- `docs/c5-tone-adc-plan.md`：冒頭に結果追記（候補A棄却・候補B因果棄却）。
- スクラッチ（`260d98fa…/scratchpad/`，本セッション限り）：`r21_capture.py`（再接続競争＋
  telnet NUL除去＋ドレイン対応版），`r21_late.py`／`r21_pvtread_late*.py`（遅延attach方式），
  `r21_natural.py`（自然ブート1発読み），`r21_pcsample.py`／`r21_stalldiag.py`（起動停滞診断），
  `r21_pvt_inject2.py`（PVT注入），`r21_pvtport_verify.py`（移植検証），`rts_reset.py`／
  `uart_capture.py`，stockビルド一式（`stock_scan/`），生ログ`r21_*.log`一式。

### 5. 申し送り（次段＝実施22）

1. **計画書の候補A/Bは両方消えた**。次はスイープ表（`tmp/c5_review_jisshi21_plan.md`）の
   本体＝PMU HP_ACTIVE群（`DIG_POWER`/`BIAS`/`HP_REGULATOR0/1`/`PD_HPWIFI_CNTL`）と
   未踏査領域`MODEM1`（`0x600AC000`）・`MODEM_PWR0`（`0x600AD000`）のA/B。ただし
   「ASP3はPMU電源初期化をゼロ行も実行していない」（レビュー机上確定）ため，差分探しよりも
   **決定実験C（`pmu_hp_system_init`がHP_ACTIVEに書く4レジスタ群のstock値注入）**を先に
   実施する方が速い可能性が高い（本ラウンドで確立した遅延attach＋1 haltウィンドウ注入手法が
   そのまま使える）。
2. **本PCでのJTAG手順は1節の罠3件を前提に組むこと**。特にSTAGE1型
   （`register_chipv7_phy`エントリbp＝1ブート1回きり）の捕捉は早期halt必須のため
   停滞リトライ前提で高コスト——採取点を「ハング成立後」（遅延attach）へ寄せられる測定から
   優先的に消化するのが効率的。
3. 候補A棄却の副産物として得た教訓「**hal/サブモジュールとIDF実ツリー（v6.1）は世代が
   異なる——一次情報は実際にリンクされるツリーで裏取りする**」は，今後の計画立案の
   チェックリストに含めること（計画書の他の項目にも同種の混入が無いか，スイープ前に
   v6.1側での再確認を推奨）。
4. `esp_wifi_adapter.c`のPVT移植のkeep/revert判断（4節）。
5. `memory/`更新はコーディネータ側で行う運用（CLAUDE.md記載の通り）。

### 6. 検証（実施21）

- ビルド：着手時`ninja: no work to do`（実施19/20とバイナリ同一，nm/objdumpでアドレス一致確認）。
  PVT移植後のリビルド成功（FLASH 11.77%/RAM 84.62%，リンクOK）。
- 実機（C5#1，`D0:CF:13:F0:A7:44`，全操作by-idピン留め）：
  - 症状再現（前提整備）：クリーンブートで`rst:0x12`ループ確認。
  - 候補A読み：ASP3独立2ブート・stock独立2ブート（stockは同一ブート内で27AP/24AP完走の
    陽性対照確認済み）。全て`0xFFFFFFFF`。
  - 候補B A/B読み：stock×2・ASP3×2（うち1回はハングループ内PC確認済み）。
  - 候補B注入：1回成立（+2.7s，30秒観測）＋対照はハング持続の既存実測
    （実施14の12-15秒・本ラウンドlate1の8秒・pvt2ブート）。
  - 候補B移植検証：1回成立（+7sハングループ確認→レジスタ読み→20秒観測）。
  - 最終状態：C5#1＝PVT移植込みASP3計装ビルド書込み済み，クリーンブートで
    約3.5秒周期`rst:0x12 (RTC_SWDT_SYS)`ループを最終確認（実施13〜20と症状同一）。
  - JTAG介入で停滞したブート（1節の罠2）は測定から全て除外（in-loopゲート不成立＝リトライ）。
- C5#2（`D0:CF:13:F0:C8:94`）：物理切断済み・一切未接触。

---

## 実施22：決定実験C（PMU HP_ACTIVEバンク4語）＋PMU_POWER_PD_HPWIFI_CNTL＝**新規差分2件を発見・mid-hang注入とbefore-PHY移植の両方で因果棄却（実施21候補Bと同じ完全性水準）**／PMU/LP/MODEM1/MODEM_PWR0スイープ＝残る差分（LP_ANA・LPPERI）はいずれもBOD/電圧グリッチ検出器・LPドメイン周辺クロックで，WiFi RF/PHYバイアス経路と機序上無関係と判明・棄却

コーディネータ指示＝`tmp/c5_review_jisshi21_plan.md`の決定実験C（`pmu_hp_system_init`の
HP_ACTIVE 4レジスタ注入）を最優先で実施し，解決しなければPMU/LP全域スイープへ。
本ラウンドもC5#1（`D0:CF:13:F0:A7:44`，ttyACM0／UARTブリッジttyUSB0）のみ使用。

### 0. 方法論的発見（本ラウンド固有）

- **実施21の「+2.6s遅延attach＋SWD無効化バースト」を踏襲した最初の試行は，本ラウンドでは
  ほぼ常に失敗した**（早期停滞trap＝PCが`dispatcher_1`(0x420217cc/d0)に恒久固定，
  systimerは進むがカーネルが二度と復帰しない）。30回近く試行してほぼ全滅という，
  実施21の「体感1/2〜2/3」よりはるかに悪い成功率だった。
- 原因を切り分けるため，**JTAG介入を一切行わずに単発の遅延halt**（RTS reset後，
  +9.9s＝WDTループの3サイクル目まで完全に手放しで自然実行させてから初めて1回だけ
  halt）を試したところ，**初回で確実に`phy_iq_est_enable_new`/`phy_get_pkdet_data`
  （既知のハングループ）内で捕捉できた**。以後この方式（早期SWD無効化バーストを
  行わず，単発の遅い一撃読みのみ→ハングループ内と確認できてから初めてSWD無効化）
  に切替えたところ，ASP3側成功率が大幅に改善した（複数回連続で初回成功）。
  実施21の「+2.6sバーストは安全」という結論は，**この個体/この時点のこの環境では
  もはや成立しない**（PC/ビルド内容やUSB/ホスト側の何らかの要因でタイミング窓が
  シフトした可能性——未解明。次段は+2.6s付近の早期介入を避け，本ラウンドで確立した
  「単発遅延一撃」方式をデフォルトにすることを推奨）。
- stock側は`~/tools/esp-idf-v6.1` `examples/wifi/scan`（実施21のスクラッチ資産，
  libphy.a MD5=`4ccdbdbe1faf04a84b4059c882febe0f`＝実施15/21と同一を確認）を毎回
  RTSクリーンリセット→UARTでscan完走確認（陽性対照）→JTAG attachのみ（halt不要，
  常に安定）で読んだ。stock側の停滞は一度も観測されなかった＝停滞trapはASP3
  （Direct Boot固有の何らかのブート経路）に限定される。

### 1. 決定実験C：PMU HP_ACTIVEバンク5レジスタのstock/ASP3比較＋因果検証

対象：`PMU_HP_ACTIVE_DIG_POWER`(`0x600B0000`)・`BIAS`(`0x600B0018`)・
`HP_REGULATOR0`(`0x600B0028`)・`HP_REGULATOR1`(`0x600B002C`)・
`PMU_POWER_PD_HPWIFI_CNTL`(`0x600B0108`)。stock×2・ASP3×2（各独立クリーンブート，
ASP3は上記「単発遅延一撃」方式でハングループ内到達を確認してから読取り）。

| レジスタ | stock（2ブート一致） | ASP3（2ブート一致） | 判定 |
|---|---|---|---|
| DIG_POWER | `0x00000000` | `0x00000000` | 差分なし |
| BIAS | `0x02000000`（**XPD_BIAS=1**） | `0x00000000`（POR既定＝XPD_BIAS=0） | **新規差分** |
| HP_REGULATOR0 | `0xC004AFD0` | `0xC667BFF0` | 差分あり＝実施21のPVT/dbias軸（**既に因果棄却済み**，再掲不要） |
| HP_REGULATOR1 | `0x00000000` | `0x00000000` | 差分なし |
| PD_HPWIFI_CNTL | `0x00000000`（force全解除＝FSM委譲） | `0x0000001C`（POR既定＝FORCE_PU/NO_RESET/NO_ISO=1のまま） | **新規差分** |

`hal/components/esp_hw_support/port/esp32c5/pmu_param.c:210-212`
（`PMU_HP_ACTIVE_ANALOG_CONFIG_DEFAULT()`）で`.bias.xpd_bias = 1`と確定——stockの
`pmu_hp_system_init(PMU_MODE_HP_ACTIVE, ...)`がHP_ACTIVEモードのアナログバイアス
生成器を明示的に起動している。ASP3はこの呼出しがゼロ行（grep 0件，実施21レビュー
機上確認どおり）のためPOR既定（バイアス生成器が明示起動されない状態）のまま。
`PMU_POWER_PD_HPWIFI_CNTL`は`pmu_power_domain_force_default()`が「force固定→HW FSM
委譲」へ遷移させる4ドメインの1つ（`hal/.../pmu_init.c:145-152`，plain R/Wビットフィールド，
トリガ不要）——ASP3は一度もこの遷移を経験していない。両方とも**PMU/LP系ブロックの
中で実施11〜21のどのラウンドでも一度も実測されていなかった具体的レジスタ**であり，
「制御は健全・アナログ測定だけ死ぬ」症状と機序面で強く整合する候補。

### 2. 因果検証（mid-hang注入）＝**症状不変（棄却）——ただしタイミング上の限界あり（3節）**

判定基準（実施前に固定）：30秒観測でIQ_DONE(`0x600a047c` bit16)が立つ，または
生ADC(`0x600a081c`)が非ゼロになれば成功。stock値どおりに書込み，読み戻しで注入成立を
機械確認した上で判定する。

ASP3のハングループ内（`phy_get_pkdet_data`/`phy_iq_est_enable_new`到達を確認した
1撃halt）で，SWD無効化と同一burst内で注入：

- **組合せ注入**（`BIAS=0x02000000`＋`PD_HPWIFI_CNTL=0x0`）：2回実施，いずれも
  読み戻しでstock値と完全一致を確認した上で30秒観測（PCはハングループ内を巡回，
  IQ_DONE=0，生ADC=全ゼロのまま）——**症状不変**。
- **BIAS単独注入**：1回実施，同条件で症状不変。
- **PD_HPWIFI_CNTL単独注入**：1回実施，同条件で症状不変。

### 3. ★重要な留保（advisorレビューで指摘）→ 4節のbefore-PHY移植で解消

mid-hang注入（PHY較正が既にハングループに入った後に値を書く）は，**もし該当レジスタが
PHY初期化の早期（`phy_enable`より前）に一度だけ読まれてそれ以降の測定チェーンの
内部状態（バイアス基準点・ラッチされた測定系オフセット等）を決めてしまうタイプ
であれば，注入のタイミングとして原理的に無力**——実施21の候補Aで既に指摘された
のと同型の限界であり，実施21の候補Bはこの限界を踏まえて**mid-hang注入と
較正前タイミングでの加算移植の両方**を行って初めて棄却を確定させている。
2節はXPD_BIAS/PD_HPWIFI_CNTLについて**mid-hang注入のみ**であり，特にXPD_BIAS
（アナログバイアス生成器の起動ビットそのもの）はこの限界が最も懸念される
レジスタだった。**したがって2節時点の「症状不変」は限定的な棄却であり，
「候補として完全に棄却された」とは言えなかった**——そのため4節でbefore-PHY
移植による再検証を実施し，この留保を解消した（実施21の候補Bと同じ完全性
水準に揃えた）。

### 4. PMU/LP/MODEM1/MODEM_PWR0スイープ（決定実験Cのmid-hang結果を受けて並行実施）

`tmp/c5_review_jisshi21_plan.md`のブロック表のうち未踏査だった
`LP_CLKRST`(`0x600B0400`)・`LP_AON`(`0x600B1000`)・`LP_I2C_ANA_MST`(`0x600B2400`)・
`LPPERI`(`0x600B2800`)・`LP_ANA`(`0x600B2C00`)・`MODEM1`(`0x600AC000`)・
`MODEM_PWR0`(`0x600AD000`)を一括ダンプ（stock×2・ASP3×2，各ブロック内
reproducibility確認込み）。

**ノイズ除外**（platform内2ブートで既に変動＝ライブ計測/カウンタ系と確定，
比較対象から除外）：
- `LP_AON+0x4`（`0x600B1004`）：stock内でも変動——起動後経過に依存するカウンタ。
- `LPPERI+0x10/+0x24`（`0x600B2810`/`0x600B2824`）：stock/ASP3いずれも自己内で変動。
- `MODEM_PWR0`内`+0x800`/`+0xC00`（`0x600AD800`/`0x600ADC00`）と`+0x86C`/`+0xC6C`：
  自己内で変動。

**reset-cause由来の非本質的差分**（ASP3はJTAG捕捉時点で直近の実リセットが
内部RTC_SWDTリセット，stockはRTSクリーンリセット直後のまま——**リセット種別が
異なる**という交絡であり，症状の原因側候補にはならない。除外）：
- `LP_CLKRST_RESET_CAUSE_REG`(`0x600B0410`)：ASP3`0x32`／stock`0x21`。
- `LP_CLKRST`の周辺（`+0x0`/`+0x4`/RC32K/FOSC校正関連と目される`+0x10`直近）も
  同種の起動来歴依存の疑いがあり，本ラウンドでは候補として扱わない
  （RESET_CAUSEと同一ブロック内・同一交絡下での比較のため）。
- `MODEM_PWR0+0x868`/`+0xC68`の`001c0000`(ASP3)対`001c6a28`(stock)，および
  `+0x8AC`/`+0xCAC`の1ビット：ロック/完了ステータス系と推定（IQ_DONEと同種の
  **症状の結果**であり原因候補ではない——値0はASP3の較正が一度も完了していない
  ことの反映）。
- `0x600AD000`〜`0x600AD1FC`（MODEM_PWR0先頭64語）：ASP3側ダンプで該当チャンクが
  欠落（telnetパース起因の取りこぼしと推定，全ゼロ域でstock側も全ゼロ＝実害なしと
  判断し再取得は省略）。

**残った genuine 差分＝LP_ANA（`0x600B2C00`，アナログ周辺——計画が名指しした
`LP_ANA`そのもの）**：

| offset | stock | ASP3 |
|---|---|---|
| `+0x0` | `0x6ffc02c0` | `0x0ffc0100` |
| `+0xC` | `0xffffffc1` | `0xffffffc3` |
| `+0x18` | `0x80000000` | `0x00000000` |

`lp_analog_peri_reg.h`のBOD/POWER_GLITCH/FIB_ENABLE系ビットフィールド。
reset-cause系のような起動来歴依存の説明が付かない（2ブートとも安定・
reset種別に依存する理由が無い）ため，**未検証の新規候補として次段へ持ち越す**
（本ラウンドでは時間の都合でフィールド解読・因果検証は未実施）。

**追加でスイープした`LPPERI+0x0`（`0x600B2800`）はstock`0x41000000`／ASP3`0x7f000000`
で差分あり**。

### 5. LP_ANA／LPPERIのフィールド解読——**いずれもWiFi RF/PHYバイアス経路と
機序上無関係と判明・棄却（因果検証は不要と判断）**

`lp_analog_peri_reg.h`でLP_ANAの3差分を解読した結果，全てBOD（brown-out detector，
低電圧検出）／電圧グリッチ検出器の設定であることが確定した：
- `+0x0`＝`LP_ANA_BOD_MODE0_CNTL_REG`（BOD mode0の割込み待ち時間・リセット待ち時間・
  割込み/リセット有効化ビット）。bit7の`PD_RF_ENA`（「BOD検出時にRFモジュールを
  強制power downする」有効化ビット）はRFに言及するが，**実際にBODイベント
  （電源電圧降下）が発生した場合にのみ発火する保護機構**であり，定常動作中
  （ベンチ電源で安定給電）のPHY較正には無関係。
- `+0xC`＝`LP_ANA_FIB_ENABLE_REG`（電圧グリッチ検出器の強制有効化ビット）。
- `+0x18`＝`LP_ANA_INT_ENA_REG`（BOD割込みのCPU配送有効化）——ADC/RF回路そのものとは
  無関係な純粋な割込みルーティング設定。

`lpperi_reg.h`でLPPERI+0x0を解読した結果，`LPPERI_CLK_EN_REG`＝**LPドメイン周辺機器
（RNG／OTPデバッグ／LP_UART／LP_IO等）のクロックイネーブル**であり，HP系WiFi/BB
（MODEM0＝`0x600A0000`）のクロックとは完全に別系統。stock/ASP3の差はどのLP周辺機器を
使うか（例：ASP3はUSB-Serial-JTAGコンソール，stockはLP_UARTを使わない構成の可能性）の
違いを反映しているだけで，PHYトーン測定チェーンとは無関係。

**両ブロックとも「計画が名指しした`LP_ANA`＝xpd/LDO/バイアス系」に該当する
フィールドは実際には含まれておらず**（ブロック名レベルでは一致するが，中身は
BOD/電圧監視と周辺クロックゲートであり，RFバイアス生成器や電源レギュレータ
そのものではない），mid-hang注入・before-PHY移植のいずれによる因果検証も
妥当性が無い（そもそも症状に影響する機序が存在しない）と判断し，**JTAG注入は
実施せず，机上棄却とする**。

### 6. XPD_BIAS/PD_HPWIFI_CNTLのbefore-PHY移植A/B——**決定実験C，完全棄却確定**

3節の留保（mid-hang注入はタイミング的に不十分な可能性）を解消するため，実施21の
`esp_shim_pvt_init()`と同じ呼出し点（`wifi_clock_enable_wrapper()`，regi2c有効化・
`phy_enable`より前，一度きり）に新関数`esp_shim_hpactive_bias_init()`を追加し，
`PMU_HP_ACTIVE_BIAS`(`0x600B0018`)=`0x02000000`（XPD_BIAS=1）・
`PMU_POWER_PD_HPWIFI_CNTL`(`0x600B0108`)=`0x00000000`（force全解除）を書込む形で
移植した（`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`）。

実機検証（PATHに`riscv32-esp-elf`ツールチェーンを追加して再ビルド→書込み→
「単発遅延一撃」方式で読取り）：
- **移植コードの実行確認**：ハングループ内（`phy_get_pkdet_data`/
  `phy_iq_est_enable_new`）到達後に`BIAS`/`PD_HPWIFI_CNTL`を読み取り，
  `0x02000000`/`0x00000000`＝**stock実測値と完全一致**を独立2ブートで確認
  （較正開始よりずっと前に反映済みであることの直接証拠）。
- **観測（30秒・20秒，独立2ブート）**：PCはハングループ内（`phy_get_pkdet_data`/
  `phy_iq_est_enable_new`）を巡回，IQ_DONE(`0x600a047c` bit16)=0，
  生ADC(`0x600a081c`)=全ゼロのまま——**症状不変**。
- UARTコンソールでも移植後の症状を再確認：約3.5秒周期`rst:0x12 (RTC_SWDT_SYS)`
  ループ（実施13〜21と同一）。

**結論：XPD_BIAS／PD_HPWIFI_CNTLはmid-hang注入（2節）とbefore-PHY移植（本節）の
両方で症状不変を確認——実施21の候補Bと同じ完全性水準で因果棄却が確定した**。
3節の留保は解消済み。

### 7. 変更ファイル・スクラッチ資産

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：`esp_shim_hpactive_bias_init()`
  新設＋`wifi_clock_enable_wrapper()`からの呼出し1ヶ所（`esp_shim_pvt_init()`の直後）。
  **本変更も実施21の`esp_shim_pvt_init()`同様「因果確定した修正」ではない**（6節の
  とおり症状に影響しない）。stockとの実在差分を解消し将来のA/B比較から交絡を除く
  目的で作業ツリーに残置するが，採否（keep/revert）はコーディネータのレビューに
  委ねる。
- 本doc（実施22追記）。
- スクラッチ（`260d98fa…/scratchpad/`）：`r22_pmuread.py`／`r22_pmuread2.py`
  （決定実験C読取り，v2が単発遅延一撃方式），`r22_inject_biaspd.py`（mid-hang因果
  検証注入，組合せ/単独），`r22_dump.py`（PMU/LP/MODEM1/MODEM_PWR0一括ダンプ），
  `r22_probe_delay.py`／`r22_probe_nohalt.py`（早期停滞trapの切り分け診断），
  `r22_uart_ts.py`（WDTループ周期実測），`r22_port_observe.py`（before-PHY移植の
  観測），生ログ・ダンプ一式（`r22_*.log`／`r22_*.dump.txt`）。stockビルドは実施21
  資産（`stock_scan/`）を再利用（再ビルドなし）。

### 8. C5#1の最終状態

- `build/c5_idf61_trace`をPATHにツールチェーンを追加して再ビルド
  （FLASH 11.77%/RAM 84.62%，実施21のPVT移植ビルドと同じ使用量＝新規追加分は
  誤差程度）→C5#1へ書込み。
- クリーンブート（UARTブリッジRTSリセット）で症状を最終確認：
  `ESP-ROM:esp32c5-eco2-20250121`→`rst:0x12 (RTC_SWDT_SYS)`ループが約3.5秒周期で
  継続（実施13〜21と同一，独立2回のキャプチャで確認）。
- 移植コード（PVT＋XPD_BIAS/PD_HPWIFI_CNTL）はいずれも実行確認済み・stock値との
  完全一致を確認済みだが，**症状（トーン自己ループバック測定の生ADCサンプルが
  ASP3のみハードゼロ）は不変**。C5#1は本状態（実施21のPVT移植＋実施22の
  HP_ACTIVEバイアス移植込みASP3計装ビルド）のまま次ラウンドへ引き継ぐ。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも物理切断済み・一切未接触。

### 9. 到達点の整理と申し送り（次段）

**本ラウンドで潰した候補**（実施21の候補A/Bに続き，計6件が因果棄却済み）：
`PCR_FPGA_DEBUG` bit31（実施21）／PVT自動dbias（実施21）／PMU HP_ACTIVE
`DIG_POWER`・`HP_REGULATOR1`（差分なしで棄却）／PMU HP_ACTIVE `XPD_BIAS`
（mid-hang＋before-PHYの両方で棄却）／`PMU_POWER_PD_HPWIFI_CNTL`（同）／
LP_ANA・LPPERIの表面上の差分（機序上無関係と机上棄却）。

**未踏査のまま残っている領域**（`docs/c5-tone-adc-plan.md`「分岐計画」ケース2の
チェックリストとの対比）：
1. `tmp/c5_review_jisshi21_plan.md`が挙げた`LP_I2C_ANA_MST`(`0x600B2400`)は本ラウンドで
   ダンプ済みだが，stock/ASP3間の差分抽出（4-way比較のフィールド解読）は未実施
   （ダンプの生データは`r22_dump_*.dump.txt`に保存済み，次段で再利用可）。
2. `MODEM1`(`0x600AC000`)・`MODEM_PWR0`(`0x600AD000`)は本ラウンドで初めて全域ダンプし，
   全ゼロ（一部のライブ計測/ロックステータス系を除く）でstock/ASP3間に config
   レベルの差分は見つからなかった——「未踏査」だった領域は埋まったが，**有意味な
   差分は出なかった**（plan doc「分岐計画」ケース2の(1)は本ラウンドで完了）。
3. **「電源系初期化列の加算移植A/B」（`pmu_init()`+`esp_rtc_init()`相当をより広く
   関数単位で段階適用）は未着手**（plan doc「分岐計画」ケース2の(2)）。本ラウンドで
   個別レジスタ注入・移植を行った`esp_shim_modem_icg_init()`(実施13)・
   `esp_shim_pvt_init()`(実施21)・`esp_shim_hpactive_bias_init()`(実施22)は
   `pmu_hp_system_init()`/PVTの一部の値のみを狙い撃ちした部分移植であり，
   `pmu_hp_system_init()`全体（今回明示的に比較しなかったdigital/clock/retentionの
   各パラメータ群）・`pmu_lp_system_init()`（LPシステム側）・
   `esp_rtc_init()`のうち今回扱っていない部分は未移植・未検証のまま。
4. **早期停滞trap（0節）が本ラウンドで大幅に悪化した**（体感1/2〜2/3→ほぼ全滅，
   「単発遅延一撃」方式で回復）原因は未解明。次ラウンドはこの方式をデフォルトにし，
   もし再び悪化したら別途切り分けること。

**「C6-genericな共通アナログ壁」への言明について**：plan doc「分岐計画」ケース2は
「これらを尽くしてから停止・ユーザー判断へ」としている。本ラウンドは(1)を完了し
(2)は未着手のため，**現時点でC6-genericと結論するのは時期尚早**——(2)の段階
適用（特に`pmu_hp_system_init()`のdigital/retentionパラメータと`pmu_lp_system_init()`）
を次段で実施してから改めて判断すべき。ただし，計6件の個別候補（PMU HP_ACTIVE
バンク5レジスタ全数＋PCR_FPGA_DEBUG＋PVT）が軒並み「差分はある/ないが，あっても
因果関係なし」という結果になっている事実は，「デジタル制御系は健全・アナログ
測定だけ死ぬ」という統一像を追加で6ラウンド分補強しており，残された説明領域が
着実に狭まっていることは記録しておく。

---

## 実施23：関数単位の加算移植A/B（`pmu_hp_system_init()`残余・`pmu_lp_system_init()`）——**新規差分4件を発見・すべて実行確認込みで因果棄却**（BBPLL/BB-I2Cアナログ電源`HP_ACTIVE.CK_POWER`／SYSCLK ICG＋retention／LP_ACTIVEレギュレータ＋LP_SLEEPバンク）。**`PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL`のforce解除は，このタイミング（WiFi init時点の後付け移植）で書くとJTAG捕捉のブート到達性が悪化する現象を発見・「悪化したら即revert」方針に従い反転・未検証のまま保留**。`esp_ocode_calib_init()`（bandgap o-code較正，`esp_rtc_init()`の内容の一部）は差分自体を確定した（stockはeFuse値でforce／ASP3は未forceのままHW自動較正）が，ASP3のOCODE実測値が妥当で基準電圧回路自体は生きているため機序的には弱い候補と評価・因果検証は実装コスト（ROM regi2cパッチのリンクが必要）を理由に未着手。**分岐計画ケース2の(2)は「主要項目は尽くしたが完全ではない」状態で終了——C6-generic結論はユーザー判断に委ねる**

コーディネータ指示＝`docs/c5-tone-adc-plan.md`「分岐計画」ケース2(2)：`pmu_hp_system_init()`の
digital/clock/retentionパラメータ・`pmu_lp_system_init()`・`esp_rtc_init()`のうち実施21/22で
扱っていない部分を関数単位で段階的に加算移植しA/B判定する回。本ラウンドもC5#1
（`D0:CF:13:F0:A7:44`，ttyACM0／UARTブリッジttyUSB0）のみ使用。C5#2は物理切断済みで
一切未接触。

### 0. 事前準備：レジスタレベルでの静的分析

`pmu_hp_system_init()`（`hal/components/esp_hw_support/port/esp32c5/pmu_init.c`）は
`hp_sys[mode]`（HP_ACTIVE/HP_MODEM/HP_SLEEPの3バンク，各`0x600B0000`+`0x34`*mode）へ
dig_power/icg_func/icg_apb/icg_modem/sys_cntl/clk_power/bias/backup/backup_clk/
sysclk/regulator0/regulator1/xtalの13レジスタを書く。`pmu_lp_system_init()`は
`lp_sys[mode]`（LP_ACTIVE=`0x600B009C`域・LP_SLEEP=`0x600B00B4`域）へ書く。
`pmu_power_domain_force_default()`（`pmu_init()`内，実施22で1/4ドメインのみ移植済み）は
`PMU_POWER_PD_{TOP,HPAON,HPCPU,WIFI}_CNTL`＋LP側forceを解除する。`esp_rtc_init()`
（`hal/components/esp_system/port/soc/esp32c5/clk.c:52`）は**`pmu_init()`を呼ぶだけ**
（`#if !CONFIG_IDF_ENV_FPGA`）であり，独自の内容を持たない——つまり実施23の対象3関数の
うち`esp_rtc_init()`は実質`pmu_hp_system_init()`＋`pmu_lp_system_init()`＋
`pmu_power_domain_force_default()`＋`esp_ocode_calib_init()`＋PVT（実施21で既済）に
帰着することを確認した（C6実施34で棄却された「esp_rtc_init独自の処理」に相当するものは
C5には存在しない）。

Direct Bootは起動以来一貫してHP_ACTIVE（かつLP_ACTIVE）モードのままで，light-sleep等の
電源モード遷移を一度も経験しない。したがって**HP_MODEM/HP_SLEEP/LP_SLEEPバンクの値は
「現在は参照されない休止中の設定」**であり，PHYトーン測定チェーンには機序的に無関係と
判断した——これは本ラウンドの明示的な**意識的スコープ判断**であり（コーディネータ指示に
「HP_SLEEP/HP_MODEM状態バンクがあればそれも」と明記されていたことは認識している），
LP_SLEEPバンクを実際に移植・実行確認した上で症状不変だったこと（3節）を同種バンクの
経験的裏付けとして使う。ユーザーが必要と判断すればHP_MODEM/HP_SLEEPバンクも同様の
手順で移植・検証できる（未実施）。

### 1. JTAG全域ダンプ：HP_ACTIVE/HP_MODEM/HP_SLEEP/LP_ACTIVE/LP_SLEEP全72ワード

`r23_pmu_full_dump.py`（実施22の`r22_dump.py`と同じ「単発遅延一撃」方式，`0x600B0000`
から72ワード＝`0x120`バイトを一括ダンプ）で，stock×2（`--stock-attach`，安定）・
ASP3×2（`--asp3-late 9.9`，独立クリーンブート）を採取。ASP3は2ブートとも1回目の
試行で成功し内部一致（37レジスタ同一・35レジスタ差分），stockも2ブート内部一致。

差分35件のうち，HP_MODEM/HP_SLEEPバンク（意識的に対象外，0節）を除いた**現在
「生きている」HP_ACTIVE/LP_ACTIVEバンクの新規差分**：

| offset | レジスタ | ASP3 | stock | 判定 |
|---|---|---|---|---|
| `+0x14` | `HP_ACTIVE.HP_CK_POWER` | `0x00000000` | `0x70000000` | **新規**（xpd_bb_i2c/xpd_bbpll_i2c/xpd_bbpll全OFF vs 全ON） |
| `+0x1c` | `HP_ACTIVE.BACKUP`（retention） | `0x00000000` | `0x010200a0` | **新規**（HP_MODEM/SLEEPと同種＝未遷移のため不活性と推定） |
| `+0x20` | `HP_ACTIVE.BACKUP_CLK` | `0x00000000` | `0xffffffff` | **新規**（同上） |
| `+0x24` | `HP_ACTIVE.SYSCLK`（icg_sysclk_en） | `0x00000000` | `0x08000000` | **新規** |
| `+0x28` | `HP_ACTIVE.HP_REGULATOR0` | `0xc667bff0` | `0xc004afd0` | 既知（実施21 PVT軸，因果棄却済み） |
| `+0x9c` | `LP_ACTIVE.REGULATOR0` | `0xc6600000` | `0xe8400000` | **新規**（LP側dbias，HP側PVT軸のLP版だが未検証） |
| `+0xb4/bc/c4/c8` | `LP_SLEEP`バンク4語 | 相違 | 相違 | **新規**（意識的スコープ判断＝0節，休止中設定と推定） |
| `+0xf8/fc/100` | `PD_TOP/HPAON/HPCPU_CNTL` | `0x1c`（POR既定） | `0x00000000` | **新規**（実施22のPD_HPWIFIと同型，未移植3ドメイン） |
| `+0x10c` | `PD_LPPERI_CNTL` | `0x1c` | `0x00000000` | **新規**（同関数のLP側force解除） |

（HP_ACTIVE.DIG_POWER/ICG_FUNC/ICG_APB/ICG_MODEM/SYS_CNTL/REGULATOR1/XTALは
両platform一致——実施13/21/22の既存移植・POR既定で説明済み。生データは
`r23_asp3_full_try1.dump.txt`／`r23_stock_full1.dump.txt`ほか。）

`HP_ACTIVE.HP_CK_POWER`が最優先候補と判断した：BBPLL（ベースバンドPLL）とBB I2C
（ベースバンド内蔵I2Cバス）はトーン測定が使うBB内部ADCと同じアナログ/BB領域に属し，
実施11〜22で見つかったどの差分よりもRF/BB機序への近さが高い。

### 2. 単体因果検証(1)：`HP_ACTIVE.HP_CK_POWER`（BBPLL/BB-I2Cアナログ電源）——**因果棄却**

`esp_shim_hpactive_ckpower_init()`を新設し，`esp_shim_pvt_init()`/
`esp_shim_hpactive_bias_init()`と同じ呼出し点（`wifi_clock_enable_wrapper()`，
PHY較正より前・一度きり）で`PMU_HP_ACTIVE_HP_CK_POWER`(`0x600B0014`)=`0x70000000`
（stock実測値）を書込む形で移植した。

実機検証（独立2ブート，「単発遅延一撃」方式`+9.9s`）：
- **実行確認**：ハングループ内到達後の読み戻しで`0x70000000`＝stock実測値と完全一致
  （2ブートとも）。
- **観測**（30秒・20秒）：PCはハングループ内（`phy_get_pkdet_data`/
  `phy_iq_est_enable_new`域）を巡回，IQ_DONE(`0x600a047c`)=0，
  生ADC(`0x600a081c`)=全ゼロのまま——**症状不変**。

advisorレビューでの指摘どおり，ASP3は既にトーン測定フェーズに到達している＝
BBPLLは何らかの別経路（`rtc_clk`/CPUクロック設定側）で既に起動済みであり，
本レジスタの値はマスクされている可能性が高い（この解釈は事後の推測であり
追加検証はしていない）。**候補として因果棄却**。

### 3. まとめ移植(2)：SYSCLK/BACKUP/BACKUP_CLK＋PD_TOP/HPAON/HPCPU/LPPERI force解除——**前半は因果棄却，後半はブート到達性の悪化を検出し即revert（未検証のまま保留）**

残り6レジスタは個別に単体因果検証するほどの機序的優先度が無い（BACKUP系は
未遷移モード専用，SYSCLKは実施13のicg_modemと同系統，PD_*は実施22で因果棄却済みの
PD_HPWIFIと同型）と判断し，`esp_shim_hpactive_residual2_init()`として1関数分＝
1回のA/Bにまとめた（`docs/c5-tone-adc-plan.md`の「関数単位」の粒度に対応）。

初回ビルド（SYSCLK+BACKUP+BACKUP_CLK+PD_TOP+PD_HPAON+PD_HPCPU+PD_LPPERIを全て
含む）をUARTで確認したところ，リブート周期（約3.6秒）に変化はなかった
（`rst:0x12 (RTC_SWDT_SYS)`ループ継続，自然ブートレベルでの明白な悪化は無し）。
しかし**「単発遅延一撃」JTAG捕捉法（+9.9s/+12s/+13s）が11回連続でPHYハングループ
（`0x42026000`-`0x4202a000`）に到達できず，毎回`dispatcher_1`近傍
（`0x420217xx`-`0x420218xx`）に着地する**という，それ以前のCK_POWER単体ビルドでは
再現しなかった新しいパターンを検出した。

**ビセクション**：`PD_TOP/HPAON/HPCPU/LPPERI`の4行を`#if 0`で無効化し，
SYSCLK/BACKUP/BACKUP_CLKのみ残したビルドで再検証したところ，JTAG捕捉の成功率は
元の「数回に1回成功」という基準的な頻度に復帰した（2回とも2〜3回目の試行で成功）。
これにより**PD_TOP/HPAON/HPCPU/LPPERIの4行がJTAG捕捉の到達性悪化の原因**と特定した。

- **SYSCLK/BACKUP/BACKUP_CLKのみ（実行確認込み，独立2ブート）**：読み戻しで
  `SYSCLK=0x08000000`・`BACKUP=0x010200a0`・`BACKUP_CLK=0xffffffff`＝stock実測値と
  完全一致。20秒観測でIQ_DONE=0・生ADC=0のまま——**症状不変（因果棄却）**。
- **PD_TOP/HPAON/HPCPU/LPPERI（4行）**：「悪化したら即revertして記録」の方針に従い
  `#if 0`のまま保留した。**因果検証は未実施**（この状態でPHYハングループに到達
  できないため）。

★重要な留保（rigor doc 6番目の教訓を踏まえた記述）：確立できたのは
「PD_*が有効な状態→単発halt捕捉法で11/11失敗，無効化→通常の成功率に復帰」という
**相関**のみである。UARTのWDTリブート周期（約3.6秒）はPD_*の有無で変化しなかった
ため，**「PD_*がブートを真にハングさせる」とは断定していない**——確立したのは
「このタイミング（WiFi init時点の後付け移植）でPD_*を書くと，単発halt捕捉法が
PHYハングループを捕まえられなくなる」という事実のみで，真のハング／単なる
捕捉タイミングのシフトのいずれかは切り分けていない。CPU/TOP電源ドメインという
影響範囲の広さ（実施22で既に因果棄却済みのWIFIドメインより遥かにセンシティブ）から，
安全側に倒してrevertした。

### 4. `pmu_lp_system_init()`：LP_ACTIVE.REGULATOR0＋LP_SLEEPバンク——**因果棄却**

`esp_shim_lpsystem_init()`を新設し，同じ呼出し点でLP_ACTIVE.REGULATOR0
(`0x600B009C`)=`0xe8400000`・LP_SLEEPバンク4語(`0x600B00B4/BC/C4/C8`)=stock実測値を
書込む形で移植した（CPU/TOP等の広域ドメイン制御を含まないプレーンR/Wのみである
ことを確認した上で適用——3節の教訓を反映）。

実機検証（独立2ブート）：
- UART確認：リブート周期約3.6秒，変化なし。
- **実行確認**：ハングループ内到達後の読み戻しで5レジスタ全てstock実測値と完全一致
  （2ブートとも）。
- **観測**（20秒・15秒）：IQ_DONE=0・生ADC=0のまま——**症状不変（因果棄却）**。

### 5. `esp_ocode_calib_init()`（bandgap o-code較正）：差分は確定・機序的には弱い候補・因果検証は未着手

0節のとおり`esp_rtc_init()`＝`pmu_init()`の残る未検証部分は
`esp_ocode_calib_init()`（`hal/components/esp_hw_support/port/esp32c5/ocode_init.c`）
のみである。`RESET_REASON_CHIP_POWER_ON`時にのみ実行され，本チップ（eFuse
`chip_version==1`・`blk_version=3`，実施21で確認済み）は`set_ocode_by_efuse(1)`分岐
（regi2cブロック`I2C_ULP`(`0x61`)の`EXT_CODE`(reg 6)にeFuse由来のocode値を書き，
`IR_FORCE_CODE`(reg 5, bit6)を1にしてforce）を通る。bandgap基準電圧の較正値であり，
「デジタル制御系は健全・アナログ測定精度だけ不正確／死ぬ」という統一像と機序的に
高い整合性を持つ新規候補である。

**未実装の理由**：`esp_rom_regi2c_write_mask`はC5では真のROM関数ではなく，
`hal/components/esp_rom/patches/esp_rom_hp_regi2c_esp32c5.c`という一個のC実装
（`regi2c_write_mask_impl`へのalias）であり，esp32c5用の`*.rom.api.ld`（他チップには
存在するリンカスクリプトのROMシンボル別名定義）がesp32c5には存在しない＝この
patchesファイル自体をASP3側のビルドにコンパイル対象として追加しない限り呼び出せない
（`asp3/target/esp32c5_espidf/`のCMake変更が必要，本ラウンドの残り時間では
構造変更として大きすぎると判断し見送った）。

**簡易diff確認の試行**：正式実装の前段階として，I2C_ANA_MSTバス
(`0x600AF800`域)を`esp_rom_hp_regi2c_esp32c5.c`の`regi2c_read_impl`と同じ手順で
JTAGから直接`mww`/`mdw`でビットバンギングし，ASP3/stock双方の`I2C_ULP`
reg_add=4/5/6（OCODE/IR_FORCE_CODE/EXT_CODE）を読む簡易スクリプト
（`r23_ocode_regi2c_read.py`）を書いて試行した：
- **ASP3側**（ハングループ内，独立2ブート）：`OCODE=0x65`/`0x68`（ブート毎に微差＝
  HW自動較正のアナログノイズとして自然），`IR_FORCE_CODE=0x00`（force無効），
  `EXT_CODE=0x80`（両ブートで再現）——3レジスタが**互いに異なる値**を返しており，
  読み出しインデックスが正しく機能している内的整合性がある。
- **stock側**（安定attach，独立2ブート）：reg_add=4/5/6の3レジスタが3回とも
  全く同一のバイト値（`0xc2`）を返した。異なるサブレジスタが恒等に一致するのは
  物理的に不自然で，このアドホックな読み出しプロトコル（CPUをhaltした状態での
  手動バスビットバンギング）が，stockの他の同時進行中のregi2c状態と何らかの形で
  干渉した結果のアーティファクトである可能性が高いと判断し，**stock側の実測値
  そのもの（`0xc2`という数値）は信頼できないデータとして破棄する**。
- **ただし，diffの有無自体は別経路で確定できる**：ASP3側の読み（信頼できる）で
  `IR_FORCE_CODE=0x00`（force無効）が2ブートとも再現しており，一方stock側の
  ソース（0節）は`set_ocode_by_efuse()`が`REGI2C_WRITE_MASK(I2C_ULP,
  I2C_ULP_IR_FORCE_CODE, 1)`を無条件に実行する（本チップのeFuse条件
  `chip_version==1 && blk_version>=1`は実施21で確認済みで成立）。かつASP3は
  `pmu_init()`（＝`esp_ocode_calib_init()`を含む）自体を一切呼ばない構造的事実
  （0節）と合わせれば，**「stockはIR_FORCE_CODE=1（eFuse値で強制），
  ASP3はIR_FORCE_CODE=0（POR既定のまま，HW自動較正まかせ）」という差分は
  ソースコードの論理から確定できる**——stock側の生JTAG読み値（`0xc2`）を
  経由しなくても divergence 自体は確認済みとしてよい。**未確認なのは
  この差分がADCハードゼロの症状に因果関係を持つかどうかのみ**（因果検証は
  未実施）。
- **機序的な重み付け**：ASP3の`OCODE=0x65`/`0x68`は妥当な中間値であり，
  `IR_FORCE_CODE=0`（force無効）はHW自動較正（`calibrate_ocode()`相当の
  回路によるo-code自己較正ループ）が動いていることと整合する——**bandgap
  基準電圧回路自体は生きており，較正済み**である（eFuse強制値を使うか
  自己較正値を使うかの違いに過ぎない）。トーン測定の生ADCが**完全な固定
  ゼロ**という症状は，「生きていて較正済みだが基準点が微妙に異なる」という
  この差分の性質としては強すぎる可能性が高く，過大評価すべきでない
  （この解釈は5節末の推奨に反映する）。

### 6. 到達点の整理と判定

**本ラウンドで因果棄却が確定した候補**（実行確認込み，独立2ブート以上）：
- `HP_ACTIVE.HP_CK_POWER`（BBPLL/BB-I2Cアナログ電源，2節）
- `HP_ACTIVE.SYSCLK`（icg_sysclk_en）・`BACKUP`・`BACKUP_CLK`（3節）
- `LP_ACTIVE.REGULATOR0`＋`LP_SLEEP`バンク4語（4節）

**未検証のまま保留**：
- `PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL`のforce解除（3節，ブート到達性悪化を
  検出しrevert——真のハングか捕捉アーティファクトかは未切り分け）
- `esp_ocode_calib_init()`（5節）：**差分自体は確定済み**（stock=IR_FORCE_CODE=1
  でeFuse値強制／ASP3=IR_FORCE_CODE=0でPOR既定のままHW自動較正まかせ——ソース
  コードの構造とASP3側の信頼できるJTAG読みから確定，stock側生JTAG読み値のみ
  アーティファクトとして破棄）だが，**因果検証は未実施**（実装コストを理由に
  移植を見送った）。ASP3のOCODE実測値（0x65/0x68）が妥当な中間値でHW自動較正が
  機能していることを示すため，機序的には「基準電圧回路は生きていて較正済み，
  基準点が違うだけ」であり，ADCの完全固定ゼロという症状に対しては相対的に
  重みが低い（過大評価しない）。
- `HP_MODEM`/`HP_SLEEP`バンク（0節，意識的スコープ判断で対象外——ユーザーが
  必要と判断すれば追加検証可能）

`docs/c5-tone-adc-plan.md`「分岐計画」ケース2(2)＝「電源系初期化列の関数単位・
段階的加算移植A/B」は，**主要な候補（RF/BBアナログに機序的に近いもの，および
安全に移植できたもの）は尽くしたが，3件の残余（うち1件はリスクを理由に意図的に
未検証）が残る状態で終了する**。plan docの「これらを尽くしてから停止・
ユーザー判断へ」という文言に厳密に従うなら，まだ完全な「尽くした」状態ではない。

**C6-genericという言明について，本ラウンドでは書かない**（意図的な選択）。
その代わり，以下をユーザーへの申し送りとする：

1. 実施21〜23の合計で，デジタル可視領域の個別候補は**11件**（PCR_FPGA_DEBUG・
   PVT・PMU HP_ACTIVE 5レジスタ・HP_ACTIVE CK_POWER/SYSCLK/BACKUP/BACKUP_CLK・
   LP_ACTIVE REGULATOR0+LP_SLEEPバンク）が「差分の有無に関わらず，実行確認込みで
   因果棄却」という結果になっている。「デジタル制御系は健全・アナログ測定だけ
   死ぬ」という統一像は，実施11開始からの累計で極めて強く補強されている。
2. 一方で，**後付け移植（WiFi init時点でのタイミング）という手法自体が
   PD_TOP/HPAON/HPCPU/LPPERIで初めて限界を露呈した**（ブート到達性の悪化）。
   これは「静的な電源ドメインforce解除でさえ，実際のブートシーケンスの
   タイミングでしか安全に行えない可能性がある」ことを示唆しており，
   もし真因がここにあったとしても，本ポートの現在のアーキテクチャ
   （Direct Boot＋WiFi init時点での後付けshim）ではそもそも安全に検証できない
   （実施22までの11件の反証と同じ土俵に立てない）ことを意味する。
3. `esp_ocode_calib_init()`（bandgap基準電圧較正）の**差分自体はソースコードと
   ASP3側の信頼できるJTAG読みから確定した**（stock=eFuse値でforce／
   ASP3=POR既定のままHW自動較正）が，**因果検証は未実施**。かつASP3の実測
   OCODE値（0x65/0x68，妥当な中間値）はHW自動較正が正常に機能していることを
   示しており，「基準電圧回路は生きていて較正済み，基準点が違うだけ」という
   機序である——「トーン測定の生ADCが完全な固定ゼロ」という症状に対しては
   相対的に弱い候補と評価する（過大評価しない）。
4. **推奨（2案，優先順位はユーザー判断）**：
   - (a) `esp_ocode_calib_init()`を正式実装（`esp_rom_hp_regi2c_esp32c5.c`を
     ASP3ターゲットのCMakeへ追加リンクし，ROM実装と同一のcritical-section手順で
     移植・A/B検証）してから最終判断する一段階限定の追加ラウンド。ただし3.の
     機序的評価（弱い候補）を踏まえると，CMake構造変更のコストに見合う情報量は
     限定的である可能性が高い。
   - (b) ここまでの11件の反証結果と「後付け移植の限界」，および今回確定した
     ocode差分の機序的な弱さを材料に，C5の壁をC6-genericとして凍結し，
     実施21改訂計画の申し送りどおりEspressif問い合わせへ進む（ocode差分は
     「確定しているが機序的に弱く，因果未検証」として問い合わせに含める）。
   コーディネータの判断では，ocode差分の機序評価（3.）を踏まえると(b)に
   やや傾くが，最終的な凍結／継続の判断はユーザーに委ねる。

### 7. 変更ファイル・スクラッチ資産

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：
  `esp_shim_hpactive_ckpower_init()`（2節，keep），
  `esp_shim_hpactive_residual2_init()`（3節，SYSCLK/BACKUP/BACKUP_CLKはkeep，
  PD_TOP/HPAON/HPCPU/LPPERIの4行は`#if 0`で無効化のまま残置——コード自体は
  ツリーに残す方針，実施21/22と同じパリティ方針），
  `esp_shim_lpsystem_init()`（4節，keep）を新設し，
  `wifi_clock_enable_wrapper()`から3関数とも呼出し。いずれも「因果確定した修正」
  ではない（症状に影響しないか，または未検証）が，stockとの実在差分を解消し
  将来のA/B比較から交絡を除く目的で作業ツリーに残置する。採否（keep/revert）は
  コーディネータ／ユーザーのレビューに委ねる。
- 本doc（実施23追記）。`docs/c5-tone-adc-plan.md`の分岐計画消化状況も追記予定。
- スクラッチ（`260d98fa…/scratchpad/`）：`r23_pmu_full_dump.py`（0節の全域ダンプ），
  `r23_verify_ckpower.py`／`r23_verify_residual2.py`／`r23_verify_lpsystem.py`
  （各段の実行確認＋観測），`r23_ocode_regi2c_read.py`（5節の簡易diff確認，
  結果は破棄），生ログ・ダンプ一式（`r23_*.log`／`r23_*.dump.txt`）。stockビルドは
  実施21資産（`stock_scan/`）を再利用。

### 8. C5#1の最終状態

- `build/c5_idf61_trace`を再ビルド（FLASH 11.77%/RAM 84.62%，実施21/22ビルドと
  同じ使用量＝新規追加分は誤差程度）→C5#1へ書込み。**PD_TOP/HPAON/HPCPU/LPPERIの
  4行は`#if 0`で無効化した状態**（3節のブート到達性悪化を受けた措置）でビルド。
- クリーンブート（UARTブリッジRTSリセット）で症状を最終確認：
  `ESP-ROM:esp32c5-eco2-20250121`→`rst:0x12 (RTC_SWDT_SYS)`ループが約3.6秒周期で
  継続（実施13〜22と同一，独立2回のキャプチャで確認，リブート周期の悪化なし）。
- 移植コード（PVT＋XPD_BIAS/PD_HPWIFI＋CK_POWER＋SYSCLK/BACKUP/BACKUP_CLK＋
  LP_ACTIVE/LP_SLEEP）はいずれも実行確認済み・stock値との完全一致を確認済みだが，
  **症状（トーン自己ループバック測定の生ADCサンプルがASP3のみハードゼロ）は不変**。
  C5#1は本状態（実施21〜23の全移植込みASP3計装ビルド，PD_*系4行のみ無効化）の
  まま次ラウンドへ引き継ぐ。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも物理切断済み・一切未接触。

### 9. 検証（実施23）

- ビルド：`cmake --build build/c5_idf61_trace`成功（警告は実施21/22から既知の
  2件のみ，新規warning/errorなし）。
- 実機：CK_POWER単体（独立2ブート，実行確認＋30秒/20秒観測）・
  SYSCLK/BACKUP/BACKUP_CLKビセクション後（独立2ブート，実行確認＋20秒/15秒観測）・
  LP system（独立2ブート，実行確認＋20秒/15秒観測）の3段とも，読み戻しでstock値と
  完全一致を確認した上で症状不変を確認。PD_TOP等はブート到達性悪化を11回の
  独立試行で確認しビセクションで原因を特定した上でrevert（機械確認込み）。
  最終状態はUART独立2ブートで確認。厳密性基準（1関数ずつ・実行確認・
  独立複数ブート・悪化時は即revert）を遵守した。

---

## 実施24：計画残り2項目（ocode強制・PD_TOP系force解除）の因果検証を**JTAG不使用**で完了——**両方とも因果棄却（負）**。ocode=eFuse強制値がASP3の自己較正値とほぼ同値のため弱い試験である点に注意。PD_TOP系はUARTでは無劣化・症状不変を確認したが，実施23のJTAG特有の症状は本ラウンドでは再検証不能につき**安全側で`#if 0`に維持**。分岐計画ケース2は消化完了——C6-generic総括を本節に記載，最終判断はユーザーに委ねる

### 0. ★環境の相違（本ラウンド最大の制約）——JTAG使用不能，UARTのみで遂行

着手時の環境確認で，計画が前提としていた「C5#1のnative USB-JTAG（ttyACM2相当）」が
**本セッションのホストには接続されていない**ことが判明した。実際に接続されていたのは：
- `ttyUSB0`：C5#1のUARTブリッジ（`esptool chip-id`で`d0:cf:13:f0:a7:44`と実機確認——
  docs記載のDUTと一致）。
- `ttyACM1`/`ttyUSB1`：**C6 AGC調査（`memory/project_c6_agc_investigation.md`，
  ★FROZEN at 実施85）の「board C」（JTAG MAC`14:C1:9F:E0:5A:9C`，`esptool chip-id`で
  ESP32-C6と実機確認）**。CLAUDE.mdの禁則・別調査の凍結ボードのため一切未接触。

つまり本ラウンドはC5#1のJTAG（OpenOCD）に一切アクセスできず，**UARTブリッジのみ**で
遂行した。計画書が指示する「JTAG読み戻しでの機械確認」「JTAG halt注入」はすべて
UART代替手段に置き換えた（詳細は1節）。この制約は最終判断（6節）にも影響するため，
以後のJTAG系ラウンドを開始する前に必ず本節を読むこと。

### 1. 方法論：UARTのみでの機械確認手法の確立

- **現flashのコンソールがusbjtag設定だったため，UARTブリッジには何も出力されない**
  ことをまず確認した（`build/c5_idf61_trace`のCMakeCache＝`ESP32C5_CONSOLE=usbjtag`。
  target.cmakeの既定は`uart0`だが，このtraceビルドは過去ラウンドで明示的に
  usbjtagへ切替済みだった）。ROMバナーのみUART0直接出力のためttyUSB0に届くが，
  ASP3アプリのsyslog/バナーはUSB-Serial-JTAG側へ出るためttyACM無しでは一切見えない。
- **対策**：同一ソースツリーから`-DESP32C5_CONSOLE=uart0`で新規ビルド
  `build/c5_idf61_uart`を作成（`cmake -S asp3/asp3_core -B build/c5_idf61_uart -G Ninja
  -DCMAKE_TOOLCHAIN_FILE=.../toolchain-riscv64.cmake -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-
  -DASP3_TARGET=esp32c5_espidf -DASP3_TARGET_DIR=.../asp3/target/esp32c5_espidf
  -DASP3_APPLDIR=.../apps/wifi_scan -DASP3_APPLNAME=wifi_scan -DESP32C5_WIFI=ON
  -DESP32C5_WIFI_REGI2C_TRACE=ON -DESP32C5_CONSOLE=uart0`）。ビルド成功・書込み後，
  UARTブリッジでASP3自身のバナー・syslog出力が読めることを確認した
  （ROM側UART0と同一物理ペリフェラルのため，コンソール切替だけで両立）。
- **★新規発見（重要）**：syslog経由の周期出力は，**PHY較正の無限リトライループに
  入った時点で完全に停止する**ことを実測で確認した。`wifi_scan.cfg`に1秒周期の
  `CRE_CYC`（`TNFY_HANDLER`）を追加し`wifi_diag_cyclic_handler`（`wifi_scan.c`，
  `TOPPERS_ESP32C5_WIFI_REGI2C_TRACE`ガード）でMODEM0生ADC/IQ_DONE/PD_*を
  `syslog()`で出そうとしたところ，起動直後の1回分（`LOGTASK_PRIORITY=3` >
  `MAIN_PRIORITY=10`のはずが，起動ごく初期の1発のみ）が届いた後は，ハングループへ
  入って以降（タイミング計測で確認：ROMバナーからハングループ到達相当点まで
  0.2秒以内・以後WDTリセット`rst:0x12`までの約3.4秒間は完全に無音）， 一切追加の
  syslog出力が届かなくなる（`r24_uart_timing.log`で秒単位のタイミングを記録）。
  優先度上はlogtaskがmain_taskを即座にpreemptできるはずであり，原因は
  「PHY較正ループが割込みマスクないし長時間のCPUロックを伴う」ことの示唆だが，
  **本ラウンドでは未確定のまま**（advisorレビュー指摘どおり，これ自体を過大解釈しない）。
- **対策2（本命）**：`syslog`/logtaskを経由しない直接ポーリング出力
  `target_fput_log`（`syssvc/logtask.c`の下請け＝カーネルバナー同様，タスク
  スケジューリングに非依存で常に届く低レベル文字出力）を，PHY較正の無限
  リトライループが確実に呼び続けると実施21で確認済みの`phy_get_pkdet_data`
  （引数無し・`0x600a0c50`を読み符号拡張して返すだけの関数．libphy.a内の通常の
  大域リンケージ関数と`nm`で確認済み＝`-Wl,--wrap`が直接効く．実施16と同じ手法）
  へ`--wrap`で設置し，1秒に1回だけMODEM0生ADC(`0x600a081c`)・IQ_DONE
  (`0x600a047c` bit16)・`PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL`
  (`0x600b00f8/fc/100/10c`)を直接文字出力する`__wrap_phy_get_pkdet_data`
  （`wifi_trace.c`）を追加した。この方式は**PHY較正ループ中も確実に1秒おきに
  出力される**ことを実測で確認した（`wifi_diag_live: ...`行，1ブートあたり
  約3回，WDTリセットまで安定して出続ける）。これにより**JTAGなしで
  「症状の生きた観測」＋「shim書込みの機械確認」の両方が可能になった**。

### 2. 実験1：`esp_ocode_calib_init()`のbefore-PHY移植——**因果棄却（負）**。ただし弱い試験である点に注意

`esp_shim_ocode_force_init()`（`esp_wifi_adapter.c`）を新設した。実施23が
「ROM regi2cパッチ（`hal/esp_rom_hp_regi2c_esp32c5.c`）をASP3ビルドへ追加リンクする
CMake構造変更が必要」と評価していたコストを回避するため，実施14で確立した
「手動regi2cリプレイ」（`I2C_ANA_MST`(`0x600AF800`)のプロトコルをMMIOで直接
再現する手法）をshim関数化した（`esp_shim_regi2c_ctrl_addr`/`esp_shim_regi2c_read`/
`esp_shim_regi2c_write_mask`。hal/の`esp_rom_hp_regi2c_esp32c5.c`は読取り専用で
参照しアルゴリズムを確認したのみ——**hal/自体は編集していない**）。

stockの`set_ocode_by_efuse(1)`（`hal/esp_hw_support/port/esp32c5/ocode_init.c`）を
忠実に再現：eFuse `RD_SYS_PART1_DATA4`(`0x600B486C`) bit[16:9]からocode値を読み，
`I2C_ULP`(0x61) reg6(EXT_CODE)へ書込み，reg5(IR_FORCE_CODE) bit6を1にする。
適用条件（`chip_revision==1&&blk_ver>=1`または`chip_revision>=100&&blk_ver>=2`）は
実機のeFuse値（`RD_MAC_SYS2`＝`0x600B484C`）から動的に判定し，本DUTでは
`chip_revision=100・blk_version=3`で成立することを確認した（実施21/23と一致）。
呼出し位置は既存shim群（PVT/HP_ACTIVE系）と同じ`wifi_clock_enable_wrapper()`の
一度きりブロックだが，**regi2cマスタクロックが実際に有効化された直後
（`_regi2c_ctrl_ll_master_enable_clock(true)`+`regi2c_ctrl_ll_master_configure_clock()`の
直後）**に置いた（regi2cトランザクションが物理的に成立するために必須）。

**実機検証（独立5ブート＝RTSクリーンリセット×2＋同一セッション内WDTループ再現×3，
全て同一結果）**：

```
ocode_force: chip_rev=100 blk_ver=3 ocode=0x65 readback ext_code_reg=0x65 force_reg=0x40 force_bit=1
```

- **書込み成立の機械確認**：`readback ext_code_reg=0x65`（書いたeFuse由来値と一致）・
  `force_reg=0x40`（bit6=1＝IR_FORCE_CODE成立）——regi2cトランザクションが物理的に
  成立していることをJTAG無しで確認した。
- **観測（`wifi_diag_live`，各ブート3サンプル・約1秒間隔）**：`raw_adc=0x00000000`・
  `done16=0`のまま——**症状不変**。

**★弱い試験であることの明記（advisorレビュー指摘）**：実施23で確認済みのASP3側
自己較正値は`OCODE=0x65`/`0x68`（HW自動較正の結果）であり，本ラウンドで強制した
eFuse値も`0x65`——**ほぼ同一の値を強制したに過ぎない**。したがって本試験が示すのは
「eFuse強制 vs 自己較正（ほぼ同値）という**書込み経路の違い自体**は症状に無関係」
ということであり，「bandgap基準電圧が大きくズレていても症状に無関係」を意味しない
（そのような大きなズレをこのDUTで作る手段が無いため，強い意味での反証はできていない）。
実施23の評価（「基準電圧回路は生きていて較正済み，基準点が違うだけ」で症状に対しては
弱い候補）と整合する結果だが，**「ocodeは棄却された」と単純化しないこと**。

### 3. 実験2：`PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL` force解除——**因果棄却（負）・ただし安全側で`#if 0`のまま維持**

実施23で`#if 0`のまま保留されていた4行（`esp_shim_hpactive_residual2_init()`内）を
`#if 1`へ切替え，1節で確立したUART直接出力手法でA/B検証した。

**実機検証（独立5ブート，前項と同一セッション）**：

```
wifi_diag_live: raw_adc=0x00000000 done16=0 pd_top=0x00000000 pd_hpaon=0x00000000 pd_hpcpu=0x00000000 pd_lpperi=0x00000000
```
（3サンプル/ブート，5ブートとも完全一致）

- **書込み成立の機械確認**：`pd_top`/`pd_hpaon`/`pd_hpcpu`/`pd_lpperi`が全て
  `0x00000000`（POR既定の`0x1c`から変化＝stock実測値と一致）——force解除が
  物理的に成立している。
- **観測**：`raw_adc=0x00000000`・`done16=0`のまま——**症状不変**。
- **UARTでの劣化なし**：ROMバナー→`ocode_force`行→`wifi_diag_live`×3→
  `rst:0x12`という起動シーケンスの文字列パターン・タイミング（約3.5秒周期）は
  本ブロック無効時（baseline，`r24_baseline_pkdet_live.log`）と完全に同一。
  実施23が観測した「WDTリブート周期はPD_*の有無で変化しなかった」という所見と
  本ラウンドのUART計測は整合する。

**★重要な留保（advisorレビュー指摘・安全側の判断）**：実施23が実際に検出した問題は
「**JTAG単発halt捕捉法がPHYハングループへ到達できず，毎回dispatcher_1近傍へ着地する**」
という，**JTAG介入時にのみ現れる現象**だった。本ラウンドはJTAGが使用不能な環境
（0節）だったため，**この現象そのものを再検証することはできていない**。UARTでの
無劣化・自由継続実行時のpkdet呼出し頻度（1ブートあたり3サンプル，1秒間隔で
安定＝ハングループに正常に留まり続けている）は，実施23の停滞が「真のハング」
ではなく「JTAG介入自体のアーティファクト」だった可能性を示唆する**傍証**には
なるが，確定ではない。次回JTAG環境（ユーザーが別PCで再開予定）での実施23の
主要な調査ツール（単発halt捕捉法）を壊さないことを優先し，**因果棄却の結論
（症状には無関係）は確定させつつ，コードは`#if 0`のまま安全側で維持する**
（実施23の判断をそのまま踏襲）。

### 4. 変更ファイル

- `asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`：
  - `esp_shim_regi2c_ctrl_addr`/`esp_shim_regi2c_read`/`esp_shim_regi2c_write_mask`
    （実施14の手動regi2cリプレイのshim化，I2C_ULPブロック専用）。
  - `esp_shim_diag_fput_str`/`esp_shim_diag_fput_hex8`/`esp_shim_diag_fput_dec`
    （`target_fput_log`ベースの軽量フォーマッタ）。
  - `esp_shim_ocode_force_init()`（実験1，**keep**＝`wifi_clock_enable_wrapper()`から
    無条件呼出し。stockとの実在差分を解消するパリティ目的，実施21/22/23の既存shimと
    同じ位置付け）。
  - `esp_shim_hpactive_residual2_init()`内のPD_TOP/HPAON/HPCPU/LPPERI 4行は
    **`#if 0`のまま維持**（3節の判断，実施23から変更なし）。
- `asp3/target/esp32c5_espidf/wifi/wifi_trace.c`：`__wrap_phy_get_pkdet_data`
  （1節，`target_fput_log`直接出力，レート制限1秒）を新設。
- `asp3/target/esp32c5_espidf/esp_wifi.cmake`：`ESP32C5_WIFI_REGI2C_TRACE=ON`時に
  `-Wl,--wrap=phy_get_pkdet_data`を追加。
- `apps/wifi_scan/wifi_scan.c`・`wifi_scan.h`・`wifi_scan.cfg`：
  `wifi_diag_cyclic_handler`（1秒周期syslog版，`TOPPERS_ESP32C5_WIFI_REGI2C_TRACE`
  ガード）。1節のとおりPHY較正ループ中は出力が止まるため主たる観測手段ではないが，
  起動ごく初期のPD_*/ADC状態のsnapshot（1回分）は確実に取得できるため残置する。
- 本doc（実施24追記）。`docs/c5-tone-adc-plan.md`は分岐計画を「消化完了」へ更新
  （5節）。
- スクラッチ（`260d98fa…/scratchpad/`）：`r24_baseline_boot1.log`（現状把握，
  usbjtagコンソール無音の確認）・`r24_uart_console_boot1.log`／`r24_uart_timing.log`
  （syslog経由出力がハングループで停止する実測）・`r24_baseline_pkdet_live.log`
  （`__wrap_phy_get_pkdet_data`によるbaseline，PD_TOP無効時）・
  `r24_ocode_check_boot1.log`（実験1の機械確認ログ）・
  `r24_pdtop_enabled_boot1.log`／`r24_pdtop_enabled_boot2_independent.log`
  （実験2のA/B・独立2ブート分）・`r24_final_state_console.log`（最終状態確認）。

### 5. 分岐計画（`docs/c5-tone-adc-plan.md`）の消化状況——**ケース2(2)消化完了**

実施23が残していた2件（`PMU_POWER_PD_TOP/HPAON/HPCPU/LPPERI_CNTL`のforce解除・
`esp_ocode_calib_init()`）を本ラウンドで両方とも実機検証した。結果は両方とも
「因果棄却（負）」——ただし2節・3節に記載した2つの重要な留保（ocodeは弱い試験・
PD_TOP系はJTAG特有の現象を再検証できていない）付きである。これにより，計画書
「分岐計画」ケース2(2)「電源系初期化列の関数単位・段階的加算移植A/B」は
**消化完了**とする。

累計：実施21〜24で個別に因果棄却された候補は**13件**
（`PCR_FPGA_DEBUG`・PVT・PMU HP_ACTIVE 5レジスタ・HP_ACTIVE
CK_POWER/SYSCLK/BACKUP/BACKUP_CLK・LP_ACTIVE REGULATOR0+LP_SLEEPバンク・
PD_TOP/HPAON/HPCPU/LPPERI force解除・bandgap ocode強制）。

### 6. C6-genericという言明について——総括と推奨（判断はユーザーに委ねる）

**確定した事実の要約（実施14〜24）**：
1. デジタル可視領域は実施15/20/21/22/23で反復比較され，説明可能なクロック/ICG/
   BB-config系レジスタは全てstock/ASP3間でビット同一（唯一の例外＝実施20で
   確定したMODEM0生ADCサンプル自体，これが症状そのもの）。
2. 起動時電源初期化列（`pmu_init()`＝`pmu_hp_system_init()`+`pmu_lp_system_init()`+
   `pmu_power_domain_force_default()`+PVT+ocode）を関数単位で段階的に移植し，
   stock値との完全一致を機械確認した上で13件全てが症状不変（実施21〜24）。
3. stockは同一個体（C5#1）・同一libphy.aブロブ上で完走する（実施15/21，陽性対照）
   ——個体差・環境交絡は排除済み。
4. regi2c/クロック基盤自体はハング中も生存・応答している（実施14）。
5. トーン自己ループバック測定チェーンの読み書き先はMODEM0内部MMIO
   （regi2c非経由）と逆アセンブルで確定済み（実施20）。

**C6との構造比較**：
- 共通点：Direct Boot構成であること，同世代（Wi-Fi 6デュアルバンド）モデムである
  こと，「デジタル制御系は全一致なのにアナログ/RF層の応答だけ欠落する」という
  症状の型が同じであること，双方で十数〜数十項目の個別候補を反証済みという
  調査の厚みが同水準であること。
- 相違点：C5は**較正段階そのもの**（`phy_iq_est_enable_new`の自己ループバック
  測定，実行時通信より前）で無応答，C6は**実行時の受信鎖**（`lmacRxDone`）と
  TX無放射で無応答——症状が現れる段階が異なる。またC5はstock（同一個体・同一
  ブロブ）陽性対照があり，個体・環境交絡が完全に排除されている点でC6より
  条件が良い（C6は既知の交絡除去に82ラウンドを要した）。

**原理的に未確認の残余**：
- regi2c越しに見えないRF専用アナログ状態（シンセサイザPLLロック・LNA/PA
  バイアス・インピーダンス整合等）——公開レジスタでは観測不能（実施14で確認）。
- blob内部のアナログ較正シーケンス自体（regi2c不使用のMODEM0直接アクセス，
  実施20）——ソースが無く逆アセンブル以上の追跡は困難。
- 2節・3節の留保（ocodeは弱い試験・PD_TOP系はJTAG特有現象を未検証）。

**推奨（2案，優先順位はユーザー判断）**：
- **(a) C5/C6の証拠パッケージを揃えてEspressif問い合わせへ進む**。理由：
  デジタル可視領域を尽くした（13+82=95件の個別反証）という調査の厚みは
  すでに十分に強く，残る説明領域は公開情報のみでは原理的に確認不能な
  アナログ内部状態に絞り込まれている。C5はC6より個体・環境交絡が少ない
  ぶん証拠として強く，「Direct Boot構成でのWi-Fi PHY較正/受信が構造的に
  失敗する」という共通パターンを補強する良い追加事例になる。
- **(b) さらに続ける場合の残り手段**：
  - JTAG環境復帰後に3節の留保（PD_TOP系のJTAG halt捕捉再検証）を解消する
    （ただし因果棄却自体は本ラウンドで確定済みのため優先度は低い）。
  - ocode正式実装（`esp_rom_hp_regi2c_esp32c5.c`のASP3ターゲットへの追加
    リンク）による，より強い試験（eFuse値と自己較正値が意図的に異なる
    個体があれば理想的だが，本DUTでは両者が近いため得られる情報は限定的）。
  - 実施14で保留した「未公開regi2c blockの逆アセンブル・トレース」
    （C6実施23の`wifi_regi2c_patch_install`手法の移植）——ただし打ち切り
    基準に照らして本命度は低いと評価されている。

**最終判断はユーザーに委ねる**。途中で症状が動いた場合はもちろんそちらを優先し，
本節の総括は撤回する。

### 7. C5#1の最終状態

- 最終ソース状態：ocode force（keep，実験1）・PD_TOP系4行は`#if 0`維持（実験2，
  3節の判断）・実施21〜23の既存shim（PVT・HP_ACTIVEバイアス・CK_POWER・
  SYSCLK/BACKUP/BACKUP_CLK・LP系）は全てkeepのまま変更なし。
- `build/c5_idf61_trace`（**usbjtag console，実施14〜23と同じ既定設定**）を
  上記ソース状態で再ビルド（FLASH 11.82%/RAM 84.63%，新規追加分は誤差程度）→
  C5#1へ書込み。
- 最終UART確認（RTSクリーンリセット，独立1回・約12秒キャプチャ）：
  usbjtagコンソール設定のため，ASP3アプリ側の出力はUARTに出ない
  （実施14〜23と同じ既知の制約）。ROMバナー→`rst:0x12 (RTC_SWDT_SYS)`ループが
  約3.5秒周期で継続することを確認——実施13〜23と症状同一，退行なし。
- **新規の恒久資産**：`build/c5_idf61_uart`（同一ソース，`-DESP32C5_CONSOLE=uart0`
  のみ相違）を今回新設した。次回以降，JTAGが使えない環境でもUARTだけで
  症状のライブ観測（`wifi_diag_live`）が可能になる恒久的な代替手段として
  スクラッチではなくビルド設定として残す（ソース側の変更＝
  `esp_wifi.cmake`の`--wrap=phy_get_pkdet_data`と`wifi_trace.c`の対応する
  ラッパは常設，コンソール種別のみビルド時選択）。
- C5#2（`D0:CF:13:F0:C8:94`）：本ラウンドも物理切断済み・一切未接触。
- C6 board C（`14:C1:9F:E0:5A:9C`，別調査★FROZEN）：本ラウンドは`esptool chip-id`
  （読取り専用）でのみ識別のため触れ，その後は一切未接触。フラッシュ内容
  変更なし。

### 8. 検証（実施24）

- ビルド：`build/c5_idf61_uart`（新規構成）・`build/c5_idf61_trace`（既存構成，
  最終状態）とも成功。警告は実施21〜23から既知の2件のみ，新規warning/errorなし。
- 実機（C5#1，UARTブリッジ`ttyUSB0`のみ，全操作esptool/pyserial経由）：
  - baseline確認（PD_TOP無効・ocode force有効）：独立1ブート，`wifi_diag_live`
    3サンプルで`raw_adc=0・done16=0・pd_top等=0x1c`を確認。
  - 実験1（ocode force）：独立5ブート（RTSリセット×2＋WDTループ内再現×3），
    全て`ocode_force: ...readback...`行で書込み成立を機械確認，症状不変。
  - 実験2（PD_TOP系）：独立5ブート（同上），全て`wifi_diag_live`で
    `pd_top=0x00000000`等の機械確認，症状不変，WDTリブート周期不変。
  - 最終状態：`build/c5_idf61_trace`書込み後，独立1ブートでUART症状
    （`rst:0x12`ループ約3.5秒周期）を再確認。
- C5#2：物理切断済み・一切未接触。C6 board C：`chip-id`読取りのみ，機能に
  影響する操作は一切実施せず。

---

## 実施25：未公開regi2c block（0x63/0x68/0x6b）を含む全8blockの0x00〜0x1Fフルスイープを4-way実施——新規プロトコル検証つき，genuinely新規かつ未説明のプラットフォーム決定的差分は**ゼロ**（既知3件の再確認＋2件のCPU周波数差／自作shim起因の確認込み）

計画書`docs/c5-tone-adc-plan.md`総括§6(b)「未公開regi2c blockの逆アセンブル・
トレース」の実施回。本ラウンドはC5#1のnative USB-JTAG（`D0:CF:13:F0:A7:44`）が
使用可能な環境だった（実施24とは異なる）。

### 0. advisorレビューで発見された致命的な手法の穴と，その解消

着手前にadvisorへ相談したところ，計画段階の設計（「host_idでI2C0/I2C1を
直接選択し，未公開blockのANA_CONF1は前回書込みのまま放置する」）には
重大な欠陥があると指摘された：`hal/components/esp_rom/patches/
esp_rom_hp_regi2c_esp32c5.c`の`regi2c_enable_block()`は名前付き5block
（BBPLL/BIAS/DIG_REG/ULP_CAL/SAR_I2C）にしかcaseが無く，**未公開block
（0x63/0x68/0x6b）に対してはi2c_sel=0固定・ANA_CONF1不変のまま**——
host_id引数も全実装で`(void)host_id`と無視される。この状態で読んだ値が
「たまたま2boot一致・cross platform不一致」に見えても，それは正しく
アドレッシングされたレジスタ内容ではなく単なるバス残留状態（ゴミ）の
可能性が高く，「regi2c可視範囲は一致」という偽陰性を記録するリスクが
あった（`memory/feedback_hardware_investigation_rigor.md`の第3反復と
同型の罠）。advisorの指示に従い，**既知の正解値に対する検証ゲート**を
実装の前に必ず通すことにした。

### 1. Step A：未公開blockの正しい読取りプロトコルを実測で確立

- **手法**：既存`wifi_trace.c`のregi2c `--wrap`計装に，block=0x63/0x68/0x6b
  へのアクセス時のみ`ANA_CONF1`/`ANA_CONF2`を記録する専用リングバッファ
  （`wifi_regi2c_cfgsnap`，64エントリ）を追加（読み取り専用，ANA_CONF1/2
  へは一切書き込まない）。ASP3計装ビルド（`build/c5_idf61_trace`）にのみ
  追加——libphy.aブロブはstock/ASP3で同一バイナリのため，プロトコル自体の
  学習はASP3（既にハングしていて失うものが無い側）だけで完結する。
- **1回目の観測**：block=0x63/0x68/0x6bを無差別にフィルタしたところ，
  リング（64エントリ）がblock=0x63（SAR/DC-offset比較器系，実施16 4aの
  既知ノイズ源）の高頻度アクセスに埋め尽くされ，0x6bの情報が上書きされて
  消えていた——単純な受動観測では収集バイアスが起きることを確認し，
  フィルタを0x6b単独に絞って再ビルド・再測定した。
- **既知答え合わせ（known-answer gate）**：実施16 4bで確定した「ASP3の
  block=0x6b,host=1,reg=0x02は恒久的に`0x87`に凍結」という事実を正解として
  使い，loop-top halt中にJTAG手動regi2cリプレイで複数の(ctrl_reg,
  ANA_CONF1候補)の組合せを試した。結果：**`host=1→I2C1_CTRL`
  ＋`ANA_CONF1=0x00FFFFF7`（~BIT(3)）の組合せのみ`0x87`を返し，他の全組合せ
  （誤host・誤mask・無変更）は`0xFF`（無効）を返した**——独立2ブートで再現。
  これにより「host_idはブロブの`phy_i2c_readReg`内部で本当にI2C0/I2C1を
  直接選択する」「ANA_CONF1のRD_MASKビットはblock固有で，正しく設定しないと
  データが読めない」の両方を実測で確定した。
- **block=0x63（reg 0x0b）・0x68（reg 0x00/0x03/0x07）への同様の検証**：
  regi2cリングバッファ（2048エントリ，本ラウンドの捕捉時点で未ラップ）が
  記録しているブロブ自身の最終読み書き値と，JTAG手動リプレイの読み値を
  直接クロスチェックした。結果は3レジスタとも完全一致（0x63/reg0x0b＝
  ブロブ最終読み0x49，リプレイ読みも0x49／0x68/reg0x03＝ブロブの
  masked read 0x1f，リプレイのフルバイト読み0x5fをmaskすると0x1fで一致／
  0x68/reg0x07＝ブロブ最終書込みのbit[3:2]=2，リプレイ読み0x08の
  bit[3:2]=2で一致）。**副産物**：block=0x69(SAR_ADC)reg=0x06のブロブ自身の
  読み（host=0，値0x2f）が実施16 4cの記録と完全一致することも確認——
  デコードスクリプトの正しさの追加証拠。
- **結論（検証済みプロトコル）**：ctrl_reg = host_id==1?I2C1_CTRL:I2C0_CTRL
  （ブロブは名前付きblockも含め全blockでhost_idを直接ルーティングに使う，
  ROMパッチのANA_CONF2 MST_SELベース選択とは別系統）。RD_MASK
  （ANA_CONF1のクリアすべきビット）はblock固有：0x63→~BIT(4)，0x68→~BIT(5)，
  0x6b→~BIT(3)（新規実測）。既知5blockはhal記載のビット（BIAS→~BIT(6)，
  BBPLL→~BIT(7)，ULP_CAL→~BIT(8)，SAR_I2C→~BIT(9)，DIG_REG→~BIT(10)）を
  使用。

### 2. Step B：8block×0x00〜0x1F×host(0/1)のフルスイープを2採取点で実施

- **採取点**：(i)`register_chipv7_phy`エントリ（precal，実機アドレスは
  ASP3=`0x42025136`／stock=`0x4202e12c`）・(ii)`phy_iq_est_enable_new`
  loop-top（ASP3=`0x420294fa`／stockはリビルドにより実施16から再確認し
  `0x4203296c`——実施16時点の値から4バイトずれていたため今回objdumpで
  再確定）。JTAG手動regi2cリプレイをOpenOCD telnet経由で自動化
  （スクラッチ`r25_sweep.py`，`r21_capture.py`のOOCD/RTSリセット基盤を再利用）。
- **★ハマった点（新規）**：1blockぶんの64回（host×reg）のread/writeを
  `;`区切りで1行のOpenOCD telnetコマンドに束ねたところ，**OpenOCDの
  telnetラインバッファが長い連結コマンドを黙って切り詰め，`mdw`の
  usageエラーだけが返る**（データは一切返らない）現象を発見した。
  4件程度の束ねでも症状が残ったため，安全のため**mdw/mww 1回ずつを
  個別の往復（バッチ化なし）に戻して解消**した（1boot・1採取点あたり
  512回のレジスタ読出しで約20〜25秒，許容範囲）。
- **read-only性の確認**：本スイープが発行するのは全て素のread
  （ctrl_regのbit24=WR_CNTLを立てない）——書込みは発生しない。
  ANA_CONF1のみ一時的に変更するため，各blockのスイープ前後で元の値を
  保存・復元した（block単位で1回ずつ）。

### 3. 実機測定

- **ASP3**：クリーンブート（UARTブリッジRTSリセット）×2，各ブートで
  2採取点×8block×2host×32regの全読出しを実施。ASP3は元々ハング中
  （失うものが無い）ため，まずASP3側で完走することを確認してから
  stockへ進んだ（計画の指示どおり）。
- **stockのperturbation検証**：スイープ挿入前のbaseline確認
  （独立1ブート，`Total APs scanned = 24`・`Returned from app_main()`到達）
  を先に取得。その後，**スイープ実行と同一ブートを裏でUARTコンソール
  キャプチャしながら**実行し，スイープ後に`resume`された同一ブートが
  `Total APs scanned = 23`・`Returned from app_main()`まで正常完走する
  ことを確認した——**スイープの介入（2箇所のhalt＋ANA_CONF1の一時変更×
  合計16block-visitぶん）はstockの完走を一切妨げない**（挿入後も陽性対照が
  陽性対照のまま，というperturbation検証の要件を満たす）。
- stockも独立2ブート実施（`register_chipv7_phy`/loop-top）。

### 4. 4-way解析と解釈

`(op,block,host,reg)`キーで「ASP3内2ブート一致・stock内2ブート一致・かつ
ASP3≠stock」を機械的に抽出した（実施16/41/42と同じ基準）。まず
host=0/1のどちらが各blockの「生きている」（0xFF固定でない）ルーティングか
をブロック・採取点ごとに実測確認した上で対応する差分だけを解釈した
（無効host側は両platformとも0xFFで一致するため自動的に除外される）。

検出された「プラットフォーム決定的」候補と，その解釈：

| block | reg | ASP3 | stock | 解釈 |
|---|---|---|---|---|
| BBPLL(0x66) | 0x03,0x05,0x06,0x09 | 12,0,88,2 | 10,17,120,3 | **CPU動作周波数差で説明可能・新規知見ではない**：ASP3の`CORE_CLK_MHZ=192`（実施03実測）に対しstockの`sdkconfig`は`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`——BBPLLはCPU/AHBクロック生成用の較正PLL（WiFi RFシンセサイザではない，実施14で確認済み）であり，目標周波数が違えば較正値が違うのは当然。WiFi RF較正とは無関係。 |
| DIG_REG(0x6d) | 0x05,0x07,0x0d | 24,24,78 | 152,152,66 | **BBPLLと同カテゴリの疑い（未確証）**：DIG_REGはデジタルレギュレータのトリム値で，CPU周波数差に伴う電源電圧要求差でも説明しうる。BBPLLほど確証は無いため「推定・未確証」と明記する。 |
| ULP_CAL(0x61) | 0x05 | 64(0x40) | 71(0x47) | **自作shimによる既知の差・新規知見ではない**：実施24で`esp_shim_ocode_force_init()`をkeepしたままの計装ビルドを使用しており，このレジスタ（`I2C_ULP_IR_FORCE_CODE`，bit6）はASP3側が意図的にforce書込みしている値。因果検証は実施24で既に完了（症状無関係）。 |
| UNK_68(0x68) | 0x03 | 95 | 96 | **実施16 4dの再確認（±1，低信頼度ノイズ）**：実施16が「低信頼度・単発」と評価した同一レジスタで，今回も±1差。ノイズ域を出ない。 |
| UNK_6b(0x6b) | 0x02 | 135(0x87) | 117(0x75) | **実施16 4bの再確認**：txcap探索停止（実施18〜20で`phy_rfcal_txcap`まで追跡済み・因果関係も検討済み）による既知の恒久分岐。stock値が実施16の`0x74`から`0x75`へ変動しているのは実施16でも観測された「stock自体の±1変動」の範囲内。 |
| SAR_ADC(0x69) host=1 | 全レジスタ | 0xFF固定 | 変動 | **無効ルーティングのアーティファクト**：SAR_ADCの正しいhost_idは0（実施16 4c・本ラウンドの答え合わせで確認）。host=1は本来無効な選択で，ASP3側はバス無応答(0xFF)，stock側はその瞬間にI2C1を使っていた別の正当な処理のライブ値を偶然拾っただけ——**block=0x69自体の内容差ではない**。有効なhost=0では両platform完全一致。 |

**新規かつ未説明のプラットフォーム決定的差分＝ゼロ**。検出された5件は
全て「(a)確立済みの非WiFi要因（CPU周波数）で完全または高確度に説明可能」
「(b)自分自身が実施24でkeepした既知の意図的差分」「(c)実施16以来
追跡済み・既に因果棄却済みの2件の再現」「(d)無効ルーティングの
アーティファクト」のいずれかに分類できた。

### 5. 因果検証について

上表のとおり，本ラウンドで検出した差分はいずれも「新規のRF状態系候補」
ではないと判定したため，4節の追加注入実験（計画の手順4）は実施しなかった
（ULP_CAL/txcap/68reg3はいずれも既に過去ラウンドで注入検証済みか，ノイズ
と評価済み）。BBPLLの192対240MHz説明は`CORE_CLK_MHZ`定義位置と
sdkconfigの値を直接読み合わせて確定できる（実測注入なしで十分に強い）。
DIG_REGの「同カテゴリだが未確証」の扱いは，本質的にCPU周波数の話であり
WiFi RF較正との関連性が薄いため，今回は追加の注入実験を見送った
（必要なら次段でASP3を240MHzへ合わせてDIG_REGの差が消えるか確認できるが，
これはWiFi/PHY調査ではなくクロック移植の話になる）。

### 6. 判定：regi2cで届く範囲（8block×0x00〜0x1F×host 0/1）は，既知の
非RF要因を除いて全一致——総括§「原理的に未確認の残余」がさらに絞られた

実施14が指摘した「未公開regi2c blockは公開レジスタ名が無いため観測
不能」という限界を，本ラウンドで**観測可能な形に変えた**（block/host/
ANA_CONF1の正しい組合せをblob自身のトレースから実測で確立）。その上で
0x00〜0x1Fの全アドレス空間を4-way比較した結果，**WiFi RF較正に関連しうる
新規の未説明差分は見つからなかった**。実施24総括の「原理的に未確認の
残余」（regi2c越しに見えないRF専用アナログ状態）は，「regi2cで**アドレス
指定可能な**範囲は全て確認し尽くした上でなお一致」という，より強い形に
更新できる——残る説明領域は，regi2cのアドレス空間そのものでは届かない
真のアナログ物理量（PLLロック検出そのものが意味する物理現象・LNA/PA
バイアスの実効値・インピーダンス整合など，レジスタとして読める「設定値」
ではなく回路の実際の振る舞い）にさらに絞られた。

### 7. 申し送り

- **DIG_REGの「CPU周波数差で説明できるか」は未確証のまま**。もし今後
  WiFi/PHYと無関係な理由でASP3のCPU周波数移植（192→240MHz対応）を
  行う機会があれば，DIG_REGの差が消えるかどうかは良い追加確認になる
  （優先度は低い——WiFi RF較正とは別カテゴリの課題のため）。
- 実施24総括の推奨2案（(a)Espressif問い合わせ／(b)継続する場合の残り
  手段）のうち，本ラウンドは(b)の「未公開regi2c blockの逆アセンブル・
  トレース」を実施し，**新規知見なし（既知差分の確認と説明のみ）**という
  結果だった。残る(b)手段は「JTAG環境でのPD_TOP系再検証」（優先度低・
  実施24で因果棄却済み）のみで，実質的に(b)の主要な手段は出尽くした
  と評価する。ユーザーの最終判断（(a)Espressif問い合わせへ進むか，
  さらに続けるか）は引き続き保留。

### 8. 変更ファイル（実施25）

- `asp3/target/esp32c5_espidf/wifi/wifi_trace.h`・`wifi_trace.c`：
  `wifi_regi2c_cfgsnap`（block=0x6b専用，Step Aのプロトコル学習用，
  読み取り専用）を追加。`ESP32C5_WIFI_REGI2C_TRACE`ガード配下のため
  既定ビルドに影響なし。
- `apps/wifi_scan/wifi_scan.c`：`wifi_regi2c_cfgsnap_reset()`/
  `wifi_regi2c_cfgsnap_dump_addr()`の呼出しを既存のregi2c/txcapリセット
  呼出しの隣に追加（同ガード配下）。
- 本doc（`docs/c5-bringup.md`）：実施25セクション追記のみ。
- ソース側の恒常的な機能変更は無し（診断専用の追加のみ，実験1/実験2に
  相当する「常時ON」の恒久修正は今回は無い——差分が全て既知/非WiFi要因と
  判明したため）。
- スクラッチ（`260d98fa…/scratchpad/`）：`r25_validate.py`／
  `r25_knownanswer.py`／`r25_ka2.py`（Step Aのプロトコル検証）・
  `r25_sweep.py`（Step Bのフルスイープ自動化，`r21_capture.py`の
  OOCD/RTSリセット基盤を再利用）・生データ
  `r25_sweep_{asp3,stock}_boot{1,2}.sweep.json`・
  `r25_stock_baseline_console1.log`／`r25_stock_boot2_console.log`
  （stockのperturbation検証ログ）・`r25_final_state_console.log`
  （最終状態確認）。

### 9. 検証（実施25）

- ビルド：`build/c5_idf61_trace`（cfgsnap追加後）を再ビルドし成功
  （`riscv32-esp-elf-nm`でシンボルアドレス確認）。stock側は既存
  `stock_scan`（実施15/16由来，無改造の素の`examples/wifi/scan`相当）を
  そのまま使用——本ラウンドの計測はJTAG手動リプレイのみで完結するため
  stock側のソース改造は不要だった。
- 実機（C5#1，UARTブリッジ`b04e3bcfa270f0118f4894301045c30f`＋native
  USB-JTAG`D0:CF:13:F0:A7:44`を都度MAC照合の上で使用）：
  - Step A：known-answer gate，独立2ブートで`0x87`一致を再現確認。
  - Step B：ASP3×2ブート・stock×2ブート，各ブート2採取点×512レジスタ
    読出し（計2048読出し/プラットフォーム）を実施，パースエラーなし。
  - stock perturbation検証：スイープ挿入後も同一ブートが
    `Total APs scanned=23`・`Returned from app_main()`まで完走（baseline
    の`24`と同水準，スイープによる劣化なし）。
  - 最終状態：`build/c5_idf61_trace`（実施24と同一ソース状態＋cfgsnap
    追加）を書込み，UARTブリッジRTSリセットで独立1ブート確認——
    `rst:0x12 (RTC_SWDT_SYS)`ループ約3.5秒周期を再確認，退行なし。
- C5#2：本ラウンドも物理切断済み・一切未接触。C6 board C：本ラウンドは
  接続確認すら行わず（`/dev/serial/by-id`一覧で識別のみ），完全に未接触。

---

## 実施26：C6実施33のクロスカーネル・ハンドオフ機構をC5へ移植 — stockの較正完了状態からリセット無しでASP3へジャンプしたところ，生ADC・IQ-est doneビットとも非ゼロで再現（2/2）——「ブート時に確立される状態がソフト到達可能」の一次証拠。ASP3自身の`esp_wifi_init`はosi_funcs未実装（`_recursive_mutex_create`=NULL）でクラッシュし較正完走までは未達

### 背景・目的

実施14〜25の累計で，MMIO/regi2cアドレス空間の到達範囲は全域一致，
起動時電源初期化もほぼパリティ化した一方，生ADC（`MODEM0+0x81C`）は
ASP3常時ゼロ・stockは生変動という切り分けが実施20で確定した。残る
論点は「ブート時に確立される未発見のソフト状態」か「ASP3ランタイム
との干渉」かが未判別な点。C6の実施33（NuttXが確立したAGC生存状態を
リセット無しでASP3へジャンプし，ASP3のスケジューラ下でも生存を確認
した決定実験）と同じ機構をC5へ移植し，同じ判別を行うのが本ラウンドの
目的。

### 1. 機構設計

C6実施33を精読した結果，機構の核は「(a) 2つのイメージをflashの
異なるオフセットに共存させ，(b) リセットを一切挟まずに，動いている
ホストの中からMMU再マッピング＋キャッシュ無効化＋直接ジャンプでゲスト
へ制御を渡す」という点であり，C6実施21/22（本物のブートローダ経由で
ASP3を起動しようとしてPMP/PMAロックの壁にぶつかった別アプローチ）とは
別物である。実施33はホストにNuttXを使うことでこの壁を回避していた
（NuttX自身の起動処理がPMPを緩く設定するため）。

C5にはNuttX移植が無いため，ホストは実機で実績のある stock ESP-IDF
v6.1 `examples/wifi/scan`（実施15/24/25で使用してきた`stock_scan`）を
採用するほかなく，これは実機の本物の2nd-stageブートローダを経由する
——実施21/22の壁が再現するリスクを最初から抱えている，という位置づけを
明確にした上で着手した（＝機構検証を先に，を徹底する理由）。

設計：
- **flash配置**：stock（bootloader 0x2000／partition-table 0x8000／
  app 0x10000，実測アプリサイズ約0.9MB）をオフセット0系列に，ASP3
  ゲスト（`asp_flash.bin`を実データ域のみ切詰め）をオフセット
  `0x200000`（2MB）に配置。C5#1の実flashは`esptool flash-id`で
  **8MB**と確認（4MBのC6より余裕あり，切詰めは安全マージンのみが目的）。
- **ジャンプ実装**：`mmu_hal_unmap_all()` → `mmu_hal_map_region(0,
  MMU_TARGET_FLASH0, vaddr=0x42000000, paddr=0x200000, len=0x100000,
  &out_len)` → `cache_hal_invalidate_addr(0x42000000, 0x100000)` →
  関数ポインタ経由で`0x42000008`（ASP3の実エントリ，`readelf -h`で
  確認済み。C6と同一）へジャンプ。実行前に`csrw mie,zero`+
  `csrci mstatus,0x8`で割込みを全マスク（C6実施33と同一手順）。
  `mmu_hal.c`/`cache_hal.h`はチップ非依存の共通HAL実装であり，C5でも
  シグネチャ・API とも無改造で使えることを確認済み。
- **ホスト側統合**：`stock_scan/main/asp3_jump.c`（新規，IRAM常駐）＋
  `scan.c`の`app_main()`末尾（`wifi_scan()`完了後3秒スリープしてから
  `asp3_jump_now()`呼出し）。stockプロジェクトはリポジトリ外の
  スクラッチ（`/tmp/.../scratchpad/stock_scan/`）にあり，本リポジトリ側
  ソース変更は無し。

### 2. 機構検証（実装前のフィージビリティ確認：PMPロック）

C6実施21/22の教訓に従い，実装に入る前にstockホスト単体を実機で
ブート・完走させ（`Total APs scanned`まで正常，陽性対照），JTAGで
`pmpcfg0-3`/`pmpaddr0-15`とEspressif独自PMA CSR（`0xbc0-0xbcf`）を
read-onlyで採取した（書込み一切無し）。

**結果：本物のブートローダ経由でも，C6実施22と同型の"ロック済み・
アプリ固有サイズにフィットしたW^X PMP"が実測された**（`pmpcfg0=
0x808d809f`等，`pmpaddr0=0x09ffffff`はC6実施22の実測値と**完全一致**）。
デコードすると，8本の有効エントリ全てが`L=1`（ロック）で，範囲は
stock自身の`.iram0.text`（`[0x40800000,0x40814b80) RX`）・
`.dram0`（`[0x40814b80,0x40860000) RW`）・flash `.text`
（`[0x42000000,0x420b0000) RX`）**にぴったり合わせて**フィットして
いた。MMIO側は`[0x60000000,0x60100000) RW`の1entryのみで，これは
`DR_REG_MODEM0/MODEM1/PMU/LP_*/PCR/EFUSE/APM/TEE`まで広くカバーする
（`hal/components/soc/esp32c5/register/soc/reg_base.h`で確認）ため
MMIO自体は問題無いと分かったが，**stockの`.iram0.text`が82KBしか
無いため，ASP3の`.data`/`.bss`（0x40800000起点）の先頭82KBがRX-only
ロック領域に落ちてstore-faultする**ことが，実際にジャンプする前の
段階でレジスタ解析だけで判明した。

原因を`~/tools/esp-idf-v6.1/components/esp_system/Kconfig`まで遡って
特定：これは`CONFIG_ESP_SYSTEM_MEMPROT`（既定`y`。「IRAM0/DRAM0バスの
全アクセスに対しROM/RAM/flashをコード部（RX）とデータ部（RW）に自動分割
してW^X保護する」機能，実装は`esp_hw_support/port/esp32c5/
cpu_region_protect.c`）という**Kconfigでトグル可能なセキュリティ機能**
だった。`stock_scan/sdkconfig.defaults`に`CONFIG_ESP_SYSTEM_MEMPROT=n`
＋`CONFIG_BOOTLOADER_REGION_PROTECTION_ENABLE=n`を追加して再ビルド・
再実機確認したところ，**scanは変わらず正常完走**し（機能的退行無し），
JTAG再測定で**`pmpcfg0-3`/`pmpaddr0-15`が全て`0x00000000`**（通常の
Direct Boot時と同じ無制限状態）に変化したことを確認した。

**この発見はC6実施21/22への申し送りにもなる**：C6の「本物のブート
ローダ経由だとPMP/PMAがロックされて解除不能」という結論も，同じ
`CONFIG_ESP_SYSTEM_MEMPROT`（C6も対象チップのはず）を無効化した
ビルドで再検証すれば覆る可能性がある——ただしC6側の検証は本ラウンドの
スコープ外につき未実施，申し送りに留める。

### 3. 機構本体の実装・検証（ジャンプ成立の確認）

まずHANDOFF_SKIP_WIFI_INIT付きの最小プローブ（`build/c5_handoff_probe`，
`apps/wifi_scan`を`-DASP3_EXTRA_COMPILE_DEFS='HANDOFF_SKIP_WIFI_INIT=1'`
でビルド，flashは256KB切詰め）でジャンプを試験。

**結果：ジャンプは成立した**——stockの`scan`完走ログの直後に
`asp3_jump_now: masking interrupts`，続けて**`TOPPERS/ASP3 Kernel
Release 3.7.2 for ESP32-C5`のバナー**（＝UART再初期化・WDT無効化・
`chip_initialize`・カーネルスケジューラが正常動作した証拠，C6実施33と
同種の副次証拠），`wifi_scan: initializing shim`，`coexist rom
version`，`HANDOFF_SKIP_WIFI_INIT`スキップメッセージまで到達。この後
`wifi_api_lock`内で`g_osi_funcs_p`テーブルのオフセット+76（後述）が
NULLで`Instruction access fault (pc=0)`。機構自体（MMU再マッピング＋
キャッシュ無効化＋ノーリセットジャンプ）は**成立**——C6と同型の
クラッシュ位置（`g_osi_funcs_p`経由の関数ポインタ呼出し）だが，原因は
別（後述）。

### 4. 判別実験本体：結果は**PASS**（生ADC非ゼロ・doneビット成立，2/2再現）

本番ゲストとして，実施16〜25で使ってきた計装ビルド
`build/c5_idf61_uart`（regi2cトレース・txcapトレース・
`wifi_diag_cyclic_handler`診断タスク入り，`HANDOFF_SKIP_WIFI_INIT`
無し＝ASP3が通常通り自前で`esp_wifi_init`を呼ぶ）を640KB切詰めで
`0x200000`へ配置し，同じ手順でジャンプを実行（独立2試行）。

両試行とも同一の結果：

```
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 ...
wifi_diag: raw_adc=0x0a4a0a54 done16=1 pd_top=0x00 pd_hpaon=0x00   ← 試行1
wifi_diag: raw_adc=0x0a740a78 done16=1 pd_top=0x00 pd_hpaon=0x00   ← 試行2（独立値，生きた変動）
wifi_diag: pd_hpcpu=0x00 pd_lpperi=0x00
Ss Instruction access fault. ... pc = 0x00000000 ...
-- buffered messages --
wifi_scan: initializing shim
coexist rom version 78e5c6e42
wifi_scan: esp_wifi_init
Instruction access fault. ... pc = 0x00000000 ...  (同一クラッシュ，esp_wifi_init内部で再度到達)
```

**`wifi_diag_cyclic_handler`はASP3カーネル起動直後から独立に動く周期
タスクで，`wifi_scan`の`main_task`が`esp_wifi_init`を呼ぶより前に
発火する**（`apps/wifi_scan/wifi_scan.c:78` `raw_adc = *(volatile
uint32_t *)0x600A081CU`，`done16`はMODEM0+0x47C系bit16）。つまり
**ASP3のWi-Fi blob・shimコードが一切実行される前**の，カーネル起動
直後の時点で，タスクからの単純なMMIO読出しとして`raw_adc`が非ゼロ・
`done16`が1（成立）だった。実施20以来25ラウンド・全ての通常Direct
Boot起動で`raw_adc=0x00000000`・`done16=0`だった記録（本ラウンド末尾の
最終確認でも再確認）と対照的。

判定基準（本ラウンド開始前に固定した基準どおり）：

> ASP3の較正が**通る**（生ADC非ゼロ・doneビット成立）→ 原因は
> 「ブート時に確立される何か（未発見だがソフト到達可能）」＝探索継続の
> 価値が確定

→ **この基準を満たした**。ASP3コード自身は較正を一切実行していない
時点で既にこの状態が観測されているため，「ASP3ランタイムがPHYを
積極的に抑制している」という解釈は追加で否定される（C6実施33と同じ
構図）。

### 5. 切替前後の状態比較（advisor指摘に基づく必須確認）

- **切替前**（stock完走直後，ジャンプ直前）：`Total APs scanned`が
  正常に出力される＝stock自身のPHY/AGC/tone測定チェーンは生きて
  完走している状態（実施15と同型の陽性対照）。
- **切替直後・ASP3コード実行前**：`wifi_diag_cyclic_handler`の初回
  発火が非ゼロ`raw_adc`・`done16=1`を観測——**ASP3側の`hardware_init_
  hook`やカーネル起動処理がSRAM（`0x40800000`起点の`.data`/`.bss`）を
  上書きしても，MMIO側のトーン測定チェーン（`MODEM0`内部レジスタ）は
  無傷で持ち越されている**ことが直接示された。C6実施33の「AGCはNuttXの
  SRAM状態と無関係にMMIOレジスタとして生存する」という構図とも整合的。
- **`esp_wifi_init`内部に入った直後**：`wifi_api_lock`で
  クラッシュしたため，較正が実際に走った後の状態までは未観測
  （次項）。

### 6. クラッシュの特定：`_recursive_mutex_create`未実装（C5固有のshim gap，今回の壁とは別種）

`ra=0x42031738`（本番ゲスト）／`ra=0x42008b18`（プローブ）を
`riscv32-esp-elf-objdump`で逆アセンブルしたところ，両方とも同一の
`wifi_api_lock`関数内，`g_osi_funcs_p`（`wifi_osi_funcs_t*`）の
**オフセット+76**にある関数ポインタへの`jalr`だった。ESP-IDFの
`wifi_os_adapter.h`のフィールド順から，オフセット+76は
**`_recursive_mutex_create`**と特定した。`asp3/target/esp32c5_espidf/
wifi/esp_wifi_adapter.c`の`g_wifi_osi_funcs`テーブル（1671行目）を
確認する必要があるが，このフィールドが未実装（NULL）のまま残って
いる可能性が高い——**通常のDirect Boot経由の起動では`esp_wifi_init`が
別の理由（トーン測定ハング等）で先に止まるため，このコードパス
（`wifi_api_lock`の初回ロック生成）に到達したことが実施1〜25を通じて
一度も無く，本ラウンドで初めて露呈したC5固有のshim実装漏れ**と
推定される。これはPMPロック（§2）とは完全に独立な，別種の障害物。

**重要な限定**：この関数はHANDOFF_SKIP_WIFI_INIT付きプローブでも
（＝`esp_wifi_init`を呼ばなくても）`esp_shim_coex_adapter_register()`
経由で呼ばれてクラッシュしており，**ハンドオフ機構固有の産物ではなく，
このシムのgapは通常のDirect Boot環境でもいずれ露呈しうるコード上の
欠落**である（今回はハンドオフによって初めて到達しただけ）。

### 7. 判定

**判定：PASS（原因は「ブート時に確立される未発見のソフト状態」であり，
ソフトウェアから到達可能）。探索継続の価値が確定した。**

- ASP3自身のコードが較正処理を一切実行する前の時点で，stockが確立した
  状態のおかげで生ADC・doneビットとも成立を示した——2/2再現。
- ただし`_recursive_mutex_create`未実装によるクラッシュのため，
  ASP3自身の`esp_wifi_init`が最後まで完走した状態での「txcapサイト2到達・
  scan結果」までは**未観測**（次ラウンドへの持ち越し）。
- C6実施33との対比：C6は「読取り専用プローブでAGC生存を確認」だった
  のに対し，本ラウンドは「ASP3コード実行前の生ハードウェア状態」で
  既に成立を確認できた点でより直接的（ソフトウェア側の解釈の余地が
  さらに小さい）。一方，C6はゲストのランタイム下での**持続**
  （6秒間の観測窓）まで確認できたのに対し，本ラウンドはクラッシュに
  より短時間の一点観測に留まる——「ASP3ランタイム下での持続」の確認は
  次ラウンドの`_recursive_mutex_create`修正後に持ち越し。

### 次ラウンドへの申し送り

1. **最優先**：`asp3/target/esp32c5_espidf/wifi/esp_wifi_adapter.c`の
   `g_wifi_osi_funcs`に`_recursive_mutex_create`（および対になる
   `_recursive_mutex_delete`等，未実装なら）を実装する（他chipの
   esp_shimまたはESP-IDF本家のFreeRTOS実装を参考に，ASP3のmutexプリミ
   ティブで実装）。これが直れば，本ラウンドと同じハンドオフで
   `esp_wifi_init`完走・txcapサイト2到達・IQ-est doneビット・実際の
   scan結果まで観測できる可能性が高い。
2. C6実施21/22への申し送り：`CONFIG_ESP_SYSTEM_MEMPROT=n`での
   PMPロック解除が有効か，C6でも再検証する価値がある（本ラウンドでは
   未実施）。
3. 「ブート時に確立される状態」の正体はまだ未特定（本ラウンドは
   「存在する・ソフト到達可能」を証明したのみ）。次段は，stock側の
   MODEM0/PMU/PCR等のレジスタを（a)stock完走直後,(b)ジャンプ直後
   の2点でJTAGスナップショットして差分を取り，どのレジスタ/状態が
   鍵かを絞り込む地道な作業になる（本ラウンドはUARTのみで完結させ
   時間の都合上JTAGスナップショット比較は見送った）。

### 8. C5#1最終状態（終了処理）

- `build/c5_idf61_trace`（実施24/25と同一の計装ビルド，本ラウンド開始時
  点の「現flash」）を`0x0`に4MB全域書込みし直し，`0x200000`のゲスト
  イメージ残置も含めて完全復元。
- 復元後の実機確認（native USB-JTAGコンソール，1回）：
  `raw_adc=0x00000000 done16=0 pd_top=0x1c pd_hpaon=0x1c`——通常
  Direct Boot時の症状（生ADCゼロ）が変わらず再現することを確認。
  リセット要因は`rst:0x15 (USB_UART_HPSYS)`（native JTAG接続に伴う
  コアのみリセット，実施14の分類上MODEM/PMUドメインは保存される類）
  だったが，それでも`raw_adc=0`——「stockの起動を経由しない限り
  この状態は生じない」という本ラウンドの主張と矛盾しない。
- C5#2：本ラウンドも物理切断済み・未接触。C6 board C
  （`14:C1:9F:E0:5A:9C`）・そのUARTブリッジ（`125a266b...`）：
  本ラウンドは`/dev/serial/by-id`一覧での識別以外一切触れていない。

### 変更ファイル

- `docs/c5-bringup.md`：本節（実施26）追加。
- リポジトリ内ソース変更：**無し**（`asp3/target/esp32c5_espidf/`・
  `apps/`とも無改造。既存の`HANDOFF_SKIP_WIFI_INIT`ガードと計装ビルド
  ［実施16〜25資産］をそのまま利用した）。
- スクラッチ（リポジトリ外，`/tmp/.../scratchpad/`）：
  `stock_scan/main/asp3_jump.c`（新規，ジャンプ実装）・
  `stock_scan/main/scan.c`（末尾に`asp3_jump_now()`呼出しを追加）・
  `stock_scan/main/CMakeLists.txt`（`hal`コンポーネント追加）・
  `stock_scan/sdkconfig.defaults`（`CONFIG_ESP_SYSTEM_MEMPROT=n`等追加）。
  ビルド：`build/c5_handoff_probe`（本リポジトリ内，gitignore対象，
  `HANDOFF_SKIP_WIFI_INIT=1`のプローブ）。
- git commitは行っていない（指示どおり）。

---

## 実施27：実施26クラッシュの真因は「シム実装漏れ」ではなくROMインターフェース領域のstaleポインタと確定（_recursive_mutex_create未実装説を静的検証で反証）→ハンドオフ上で**ASP3自身のesp_wifi_init＋フル較正が完走（2/2再現・"esp_wifi_init -> 0"明示・txcap両帯探索・done16成立）**——実施26判別実験の完結。scanは新たな壁＝CLIC mintstatus.mil固着による全割込み凍結で未達。★最重要新発見：**同じmil固着凍結が冷間ブートでも起動後約72msで発生している**＝25ラウンド追跡してきた「較正ハング（トーンADCゼロ）」の真因がCLICポートの割込み出口経路である可能性が浮上

### 背景・目的

実施26の申し送り：(1) `g_wifi_osi_funcs._recursive_mutex_create`（オフセット
+76）がNULLでクラッシュ→シムに実装する（恒久修正）、(2) その上でハンドオフ
（stock@0x0完走→無リセットジャンプ→ASP3@0x200000）でASP3自身の較正が
完走するかを確認し実施26の判別実験を完結させる。

### 1. 【前提を覆す発見】シム実装漏れは存在しない——実施26クラッシュの真因はstale登録ポインタ

実装着手前の独立確認（実施26自身が「1671行目を確認する必要がある」と
留保していた点）で、**実施26の結論が反証された**：

- **ASP3のosiテーブルは完全**：`build/c5_idf61_uart/asp.elf`の
  `g_wifi_osi_funcs`（0x40800004、サイズ0x1FC、magic=0xdeadbeaf@+0x1F8）を
  全ワード走査した結果、**NULLフィールドは0個**。オフセット+76は
  `recursive_mutex_create_wrapper`（0x42002256）が正しく入っている。
  `_mutex_create/_delete/_lock/_unlock`（+72/+80/+84/+88）も全て実装済み。
- **真のメカニズム（静的解析＋実機で確定）**：
  1. stock/ASP3両ELFとも、WiFi/PHY/pp/net80211/coexの登録ポインタ群
     （`phy_rom_phyFuns`〜`coex_env_ptr`、約130シンボル）を**同一の絶対
     番地**（SRAM最上部0x4085fb80〜0x4085ffc4、esp32c5.rom.*.ldがPROVIDE
     するROMインターフェース領域）に持つ。`g_osi_funcs_p`=0x4085ff60。
  2. ASP3の`__bss_end`=0x408513d8＜0x4085fb80のため、**ASP3起動時にこの
     領域はクリアされず、stockが登録したポインタが生き残る**。
  3. blobの`wifi_osi_funcs_register`は逆アセンブルで確認した通り
     「`g_osi_funcs_p`が非NULLなら登録をスキップして成功を返す」
     （`esp_coex_adapter_register`の`g_coa_funcs_p`も同型）。
  4. よってASP3は自分のテーブルを登録できず、`wifi_api_lock`は
     **stockのテーブルがあった番地**（0x408151cc等）のオフセット+76を
     読む。この番地はASP3では.bss内＝ゼロクリア済みのため関数ポインタ=0
     →`pc=0`クラッシュ。実施26の観測（ra=wifi_api_lock内のjalr直後・
     +76）と完全に整合し、**冷間ブートで発生し得ない**（POR時は
     g_osi_funcs_p=0→正規登録が走る）ことも説明する＝「通常ビルドでも
     いずれ露呈するshim gap」という実施26の推定も否定。
- **実機での直接証拠**：ホストのジャンプコードに証拠プリントを追加し、
  ジャンプ直前に`*0x4085ff60 == &stock側g_wifi_osi_funcs`（0x4081524c）
  を2/2試行で実測。

**→リポジトリ側のシム実装・修正は一切不要**（本ラウンドの当初想定タスク
(1)は対象消滅）。対処はハンドオフ機構側（ホストのジャンプコード）が正しい：
割込みマスク後・ジャンプ前に、WiFi系ROMインターフェースポインタ領域
`[0x4085fb80, 0x4085ffc8)`をゼロクリアしてPOR相当へ戻す
（0x4085ffc8以降＝cache/spiflash/ets_ops/syscall等のROM OSポインタは
ホスト自身とジャンプコードが使用中のため保持）。スクラッチの
`stock_scan/main/asp3_jump.c`に実装（リポジトリ外）。

### 2. 本命実験：ハンドオフ上でASP3自身の較正が**完走（2/2）**——実施26判別実験の完結

修正済みホスト＋計装ゲスト（`build/c5_idf61_uart`、640KB切詰め@0x200000）で
独立2試行。**両試行ともpc=0クラッシュは解消**し、以下を観測：

- **再登録成功**（JTAG実測）：ハンドオフ後の`g_osi_funcs_p`=0x40800004＝
  **ASP3自身のテーブル**。
- **`esp_wifi_init -> 0`**（試行2のsyslogリングに明示、後述の手法で回収。
  試行1も`esp_wifi_start -> 0`まで進行＝init成功の含意）。冷間ブートでは
  実施13以来一度も返らなかった関数が、**約0.5秒で正常完了**。
- **done16（IQ-est doneビット、MODEM0+0x47C bit16）の時系列**が「stock残存
  →ASP3自身の較正」を区別するログ設計どおりに推移：ジャンプ直後=1
  （stock残存）→ASP3較正中=0（較正がre-arm）→較正後=1（**ASP3自身の較正で
  成立**）。生ADC（+0x81C）は全期間non-zeroで生きた変動。
- **txcapリング**（JTAG回収・36呼記録）：ASP3自身の`phy_set_txcap_reg`が
  2.4GHz帯（2432/2437/2443/2462MHz）と5GHz帯（5240〜5885MHz、3周回）を
  **実際に探索**。冷間ブートの「シード書込みのみで探索空振り」（実施19）と
  決定的に対照。
- `esp_wifi_start -> 0`、`WIFI_EVENT id=2(STA_START)`、promiscuous設定成功
  まで到達（2/2）。

**判定（事前固定基準）**：「stockが確立した状態の上でASP3の較正チェーンは
正常完走する」——実施26の判別実験は完結。原因が「ブート時に確立される
ソフト到達可能な状態」であることが、ASP3自身の較正完走という最も強い形で
再確認された（ただし§4の新発見により「状態」の正体の解釈が大きく動く）。

### 3. 新たな壁：scan未達——promiscuous有効化直後の全割込み凍結（CLIC mintstatus.mil固着）

両試行とも、promiscuous有効化の直後（ゲスト時刻14.211s／14.189s、
**2/2で同一箇所**）から出力が途絶。JTAGフォレンジクス（read-only）で
状態を完全に特定した：

- CPUはASP3のidleループ（`dispatcher_1`＝旧名`dispatcher_2`相当、PC変動
  あり＝自由走行中）。`_kernel_p_schedtsk`=NULL、ready_primap=0＝全タスク
  待ち状態。カーネル時刻`_kernel_current_hrtcnt`は上記時刻で**凍結**
  （SYSTIMERの生カウンタ自体は進行＝クロック生存）。
- **全割込みが「ペンディングのまま配送されない」**：CLIC線17（WiFi
  MAC/PWR、INTMTX src0/src2→17）・線32（タイマ、src61→32）とも
  IP=1・IE=1・ATTR=0xC0・CTL=0x5f。SYSTIMER INT_RAW=INT_ST=1。
  mstatus.MIE=1。mintthresh=0x1f（全開）。INTMTXマップも全て正しい。
- 残る唯一のゲート＝**mintstatus.mil（bit31:24）が0x5fに固着**。CLICの
  配送条件はlevel>max(mintthresh,mil)のため、レベル0x5f（本ポートの
  全割込み）は永久にブロックされる。milの降格手段はmret（mcause.MPILの
  復元）のみ。
- **WiFi ISRは一度も実行されていない**（`esp_shim_int_count[]`全0、
  promisc_rx_count=0）＝WiFi ISRの暴走・ブロックではない。最後に受け
  付けられたtrapはタイマ（mcause=0xb8000020、mepc=idleループ）で、
  **その出口がmretを経由せずmilが昇格されたまま残った**と解釈される
  （ASP3のRISC-Vポートには「割込みからのディスパッチでトラップフレームを
  破棄する（mret非経由の）出口経路」が存在する）。凍結タイミングが
  「最初のWiFi RX割込みアサート直後」に2/2で一致する詳細トポロジは未特定。

補助データ：syslog出力が途絶する既知現象（実施24）に対し、本ラウンドで
**syslogリングバッファ（`syslog_buffer`、TCNT=128・32B/entry）をJTAGで
ダンプし、ELFのフォーマット文字列を引いてオフライン復元する手法**を確立
（`r27_syslogdump*.py`）。ログタスク死後の進行（`esp_wifi_init -> 0`等）は
全てこれで回収した。

### 4. ★最重要新発見：**同じ凍結が冷間ブートでも起動後約72msで起きている**——「較正ハング」の真因候補

ハンドオフの凍結解析の対照実験として、冷間ブート（従来症状＝較正ハング中）
の同じレジスタをJTAGで読んだところ：

- **`_kernel_current_hrtcnt`=0x11a95（≒72ms）で凍結**（約10秒間隔の
  3サンプルで不変。PCは較正ビジーループ内を変動＝CPU自体は走行中）。
- **mintstatus.mil=0x5f固着**（3/3）。mintthresh=0x1f、MIE=1＝ハンドオフ
  凍結と同一シグネチャ。
- 較正ハング中に周期的に出る`wifi_diag_live`はビジーループ内のpkdet
  ラッパ直接printでありタイマ不要＝割込み死と矛盾しない。ハング中に
  約数秒周期で来るRTC SWDTリセット（rst:0x12、実施24ログにも記録あり）も
  「割込み凍結でSWDのフィードが止まる」ことで説明がつく。

**含意（次ラウンドの最優先仮説）**：ASP3の冷間ブートでは、起動後72msの
時点（wifi初期化の入口付近）で全割込みが死んでおり、**その後の
`register_chipv7_phy`のフル較正は「タイマ・タスクスケジューリングが一切
動かない世界」で走っていた**。blobの較正チェーン（トーン自己ループバック
測定）が割込み/タスク機構に依存する部分を持つなら、実施14〜25で追跡した
「制御レジスタは全一致なのに生ADCサンプルだけ出ない」「txcap探索が
空振りする」という症状群は**RF/アナログの未発見状態ではなくCLICポートの
割込み凍結の下流症状**として説明できる可能性が高い。ハンドオフで較正が
通った理由も「stockが確立したRF状態」ではなく「ハンドオフ経路では凍結の
発生が較正完了より遅かった（14.2s）」だけかもしれない——**実施26/27の
判別実験の結論（ソフト到達可能）は不変のまま、「状態」の正体候補が
RF系からカーネルポートのCLIC割込み出口へ大きく移動した**。

### 5. CLIC_INT_CONFIG正規化の試行と撤回・計測上の罠2件（正直な記録）

- 凍結の対策として`clic_initialize()`にCLIC_INT_CONFIG_REG=0（ヘッダ上の
  default値）を書く修正を一時実装した。ハンドオフ試行2で**0の反映まで
  確認した上で、凍結は解消しなかった**（milが0x5f→0xffに変わるだけで
  同型凍結）→効果なしと実測されたため撤回した。**冷間ブートでも
  CLIC_INT_CONFIGは実測0x6（mnlbits=3、ROM/HWが設定）**であり、ヘッダの
  「default:0」は当てにならない。この知見はarch層コメント
  （chip_support.S冒頭・chip_kernel_impl.c）に反映済み（機能変更なし）。
- **罠1（危うく誤った因果結論）**：上記修正入りの`c5_idf61_trace`ビルドを
  冷間ブートさせたところUARTにバナーが出ずSWDTループに見え、一時
  「修正がブートを壊した」と判定しかけた。実際は**`c5_idf61_trace`は
  `ESP32C5_CONSOLE=usbjtag`ビルド**でUARTブリッジには何も出ないのが正常
  （r26の最終確認がnative USB-JTAGコンソールで行われていたのはこのため）。
  counterfactual比較の2変数交絡（修正の有無＋コンソール先）による誤り
  で、`memory/feedback_hardware_investigation_rigor.md`第6再発則の変形。
  单一変数に揃えた再検証（コメントのみ復帰後のuart0ビルド640KB）で
  正常ブート・症状不変を確認して解消した。
- **罠2**：SWDTループ（rst:0x12の繰り返し）は「壊れたビルド」の証拠では
  なく、**較正ハング中の正常な既知サイクル**（実施24ログ冒頭にも同じ
  rst:0x12あり）。

### 6. 判定と申し送り

**判定**：
1. 実施26申し送り(1)は**対象消滅**（シム実装漏れは存在しない。静的検証で
   反証、真因はハンドオフ固有のstaleポインタ→ホスト側ゼロクリアで解決）。
2. 実施26申し送り(2)は**PASS 2/2**——ASP3自身の`esp_wifi_init`＋フル較正が
   stock確立状態の上で完走（"esp_wifi_init -> 0"・done16のre-arm→成立・
   txcap両帯探索）。
3. scanは**未達**——新たな壁＝CLIC mil固着による全割込み凍結（2/2、
   promiscuous有効化直後）。かつ同じ凍結が**冷間ブートでも72msで発生**して
   いることを発見。

**次ラウンド最優先（凍結の根治＝較正ハング真因仮説の検証）**：
1. mret非経由の割込み出口経路を特定する（`asp3_core`のRISC-V共通
   `core_support.S`のret_int→ディスパッチ経路と、C5 arch層
   `irc_begin_int/irc_end_int`の突き合わせ。arch層＝`asp3/arch/riscv_gcc/
   esp32c5`は**本リポジトリ内で編集可能**）。対策候補＝割込み出口での
   mil降格の保証（irc_end_int内synthetic mret等。mcauseのMPIL/EXCCODEを
   壊さないこと）。修正後は`test_porting`（実機`# 6/6 passed`）の回帰
   確認が必須。
2. 修正が動いたら**冷間ブートで較正が完走するか**が決定実験（成功なら
   C5較正ハング＝トーンADCゼロ問題の真因確定でクローズ、scanまで一直線。
   失敗なら「ブート時確立状態」のRF説が復活し、ハンドオフ差分の絞り込みへ
   戻る）。判定指標：hrtcnt凍結の消滅→`esp_wifi_init -> 0`→AP数。
3. なぜ冷間は72ms・ハンドオフは14.2s（WiFi RX割込みアサート直後）で
   凍結するのかのトポロジ特定は、1.の経路特定と同時に解けるはず。
4. C6への波及調査（C6はPLIC_MX＝mil機構なしのため直接は該当しないが、
   「割込み出口でのマスク状態復元」の同型監査は価値あり）。

**再利用資産（スクラッチ）**：`r27_syslogdump*.py`（syslogリングJTAG回収
＋ELF文字列でのオフライン復元）、`r27_pcsample.py`、修正済み
`stock_scan/main/asp3_jump.c`（証拠プリント＋ROM-ifポインタ領域ゼロ化）、
`r27_frozen_t2.dump.txt`等の凍結状態レジスタ一式、
mintthresh/mintstatusのOpenOCD読み出し法（`reg mintthresh`／
`reg csr_mintstatus`が素で使える。`csr839`等の番号指定は不可）。

### 7. C5#1最終状態（終了処理）

- `build/c5_idf61_uart`（コメントのみの差分＝機能不変を再ビルド、
  **uart0コンソール**）のフル4MBイメージを0x0へ書込み——0x200000の
  ゲスト残置も同時に消去され、フラッシュはクリーン。
- 復元後の冷間ブート確認（UARTブリッジ、RTSリセット）：バナー正常・
  `raw_adc=0x00000000 done16=0 pd_top=0x1c`・`wifi_diag_live`反復
  （25回/30s）＝**従来症状が退行なく再現**。
- 本ラウンドのリポジトリ差分はarch 3ファイルの**コメント＋レジスタ定義
  2行のみ**（機能変更なし。ビルド確認済み）。
- C5#2：物理切断のまま未接触。C6 board C（`14:C1:9F:E0:5A:9C`）・その
  UARTブリッジ（`125a266b...`）：`/dev/serial/by-id`一覧での識別以外
  一切未接触（本ラウンド中、DUTブリッジ`b04e3bcf...`=ttyUSB0、
  禁止対象`125a266b...`=ttyUSB1に番号が入れ替わっていた——by-id運用の
  重要性を再確認）。

### 変更ファイル

- `docs/c5-bringup.md`：本節（実施27）追加。
- `asp3/arch/riscv_gcc/esp32c5/chip_support.S`：冒頭コメントの
  「実機確認待ち」を実施27の実測結果（mil自動昇格の実在・mnlbits冷間
  実測0x6・mil固着凍結）で更新（コメントのみ）。
- `asp3/arch/riscv_gcc/esp32c5/chip_kernel_impl.c`：`clic_initialize()`に
  「CLICグローバル設定を書き替えない」旨の知見コメントを追加
  （コメントのみ。一時実装したCONFIG=0書込みは実測非効果につき撤回済み）。
- `asp3/arch/riscv_gcc/esp32c5/esp32c5.h`：`ESP32C5_CLIC_INT_CONFIG`／
  `ESP32C5_CLIC_INT_THRESH`のアドレス定義2行を追加（コメント参照用）。
- スクラッチ（リポジトリ外）：`stock_scan/main/asp3_jump.c`（証拠
  プリント＋ROM-ifポインタ領域ゼロ化を追加・再ビルド）、`r27_*`一式
  （ログ・ダンプ・スクリプト）。
- git commitは行っていない（指示どおり）。

---

## 実施28：mret非経由の割込み出口経路を割込みリング計装で実測特定（2/2）——凍結＝「最初のwake-from-idleでのdispatch_r復帰」と確定し，irc_begin_intのsynthetic mretによるmil即時降格を恒久実装。**凍結は完全解消（hrtcnt進行・全割込み配送継続・test_porting 6/6維持）**。しかし**較正ハング（トーンADCゼロ）は凍結と独立に残存**＝実施27の「凍結が較正ハングの真因」仮説は反証——scanは未達のまま，探索はRF/ブート時確立状態の線へ回帰

### 背景・目的

実施27の申し送り：(1) mret非経由の割込み出口経路をコード読解＋実測で特定，
(2) 全出口でのmil降格を保証する修正をarch層（`asp3/arch/riscv_gcc/esp32c5/`，
本リポジトリ側＝編集可）に実装，(3) 決定実験＝修正後の冷間ブートで
(i)72ms凍結の消滅・(ii)PHY較正の完走・(iii)scan到達，(4) `test_porting`
6/6回帰維持，(5) C6への波及所見。

### 1. コード読解：mret非経由出口の列挙（asp3_core共通部，読み取りのみ）

`arch/riscv_gcc/common/core_support.S`の割込み出口は`irc_end_int`通過後に
3分岐し，うち2つがmret非経由：

1. **idle復帰**：nest→0かつ`p_runtsk==NULL`→割込みフレーム破棄→
   `j dispatcher_0`（L556）。dispatcher_0で`p_schedtsk`があれば
   `jr TCB_pc`で切替先タスクへ。
2. **遅延ディスパッチ**：`core_int_entry_3`（`p_runtsk≠p_schedtsk`）→
   コンテキスト保存→`j dispatcher`→切替先の`TCB_pc`へ`jr`。切替先の
   再開番地が`ret_int_r`（割込みでプリエンプトされたタスク）なら最終的に
   `core_int_entry_4`→mretへ合流するが，**`dispatch_r`（自発的に待ちに
   入ったタスク）と`start_r`（新規起動タスク）はmretを一度も経由しない**。
3. `core_int_entry_4/5`→mret（これだけがmilを降格できる経路）。

CPU例外側（`core_exc_entry`）にも同型の分岐があるが，CLICは例外では
milを昇格しない（水平トラップ）ため対象外。

### 2. 実測特定：割込みリング計装（arch層のみ，`ESP32C5_CLIC_DEBUG_RING`）

`irc_begin_int`/`irc_end_int`に32エントリ×8ワードのリング記録を追加
（既定無効の恒久診断としてリポジトリに残置）。begin側=
{mcause（exccode=CLIC線番号，**MPIL[23:16]=受付直前のmil**＝直前の割込みの
出口後のmil値），mepc，mintstatus，hrtcnt}，end側={exit分岐を決定づける
入力＝excpt_nest_count・p_runtsk・p_schedtsk・p_schedtsk->TCB_pc}。
例外経路はカウンタのみ（c5dbg_exc_count）。回収は
`r28_ringdump.py`（スクラッチ，r21ハーネス利用：RTSリセット→attach→
SWD無効化burst→自由走行→halt→mdw回収）。

**結果（wifi_scan冷間ブート，独立2ブートで全記録が同一＝2/2）**：

- seq189〜219：INTNO17（**UARTコンソール割込み**，`INTNO_SIO`）が実行中
  タスクに命中（prun==psch）→mret出口。**全エントリでMPIL=0x00＝mretは
  毎回milを正しく降格していた**。
- **seq220（最後の割込み・以後永久に配送なし）**：INTNO16（SYSTIMER，
  CLIC線32）が**idleループに命中**（mepc=dispatcher idle，p_runtsk=NULL，
  hrt=0x11a23≒72ms）。ハンドラがタスクを起床（p_schedtsk=0x4083e348）→
  出口=`j dispatcher_0`→`jr TCB_pc=dispatch_r`＝**mret非経由**→
  mil=0x5f固着。以後idx・hrtcnt完全凍結（3秒後も不変），
  csr_mintstatus=0x5f000000をJTAG実測。
- c5dbg_exc_count=0＝例外経路は一切通っていない（見えない経路なし）。

**★「72ms」の正体**＝wifi_scanブートで**最初に発生するwake-from-idle**。
それ以前の割込み（UART TX等）は全て実行中タスクへのmret復帰のため無害
だった。

**★test_porting 6/6が凍結を検出できなかった理由も同時に確定**：
`test/porting/test_porting.c`は項目2/6がビジーポーリング（割込みは実行中
タスクに命中→prun==psch→mret出口），項目3〜5のディスパッチはタスク
コンテキストからの`dispatch()`（割込み出口ではない＝mil昇格なし）。
**「割込みからの遅延ディスパッチ／wake-from-idle」を構造的に一度も
通らない**ため，このバグと共存して全項目PASSする。

### 3. 恒久修正：irc_begin_intでのsynthetic mretによるmil即時降格

`asp3/arch/riscv_gcc/esp32c5/chip_support.S`の`irc_begin_int`で，
mintthresh昇格の直後に：

```
la   t1, irc_begin_int_demote
csrw mepc, t1
li   t1, MSTATUS_MPIE
csrc mstatus, t1       /* MPIE=0：mret後もMIE=0（CPUロック相当）を維持 */
mret                   /* mil ← mcause.MPIL（受付前レベル）へ降格 */
irc_begin_int_demote:
```

- **設計**：milを受付前レベル（HWが受付時にmcause.MPILへ保存済み，
  無変更で使用）へ即時降格。以後どの出口を通ってもmilは残らない。
  同一優先度以下のブロックは直前に昇格したmintthreshが担う（C6の
  PLIC_MX threshと同一意味論）＝milとmintthreshの二重マスクの解消。
- **整合性**（詳細はコード内コメント）：mepcは共通部が入口で保存済みの
  ため破壊可。MIE=0維持のため事前にMPIEクリア（MPIEは次トラップ受付時に
  HWがMIEから再設定するため副作用なし）。この区間はMIE=0でプリエンプト
  不可＝mcause/mepcは書き換わらない。多重割込みは各入口で同様に降格され
  整合。出口の正規mret（core_int_entry_5）が読むmcause.MPILも本方式では
  常にベースレベル＝一貫。
- 例外経路（irc_begin_exc）は変更なし（例外はmilを昇格しないため）。

### 4. 回帰確認：test_porting 実機 `# 6/6 passed`（修正込み）

`build/c5_r28_tp`（usbjtagコンソール＝実施03と同構成，
`-DASP3_EXTRA_APP_C_FILES=test/porting/tap.c`）。独立2試行（キャプチャ内の
再ブート分を含め計7ブート）**全て `# 6/6 passed`**＝回帰なし。

### 5. 決定実験（wifi_scan冷間ブート，`build/c5_idf61_uart`＝従来症状の標準ビルドを修正込みで再ビルド）

- **(i) 72ms凍結の消滅：PASS（2/2）**。JTAGで`_kernel_current_hrtcnt`が
  3.0秒の壁時計に対し**ちょうど+3,000,000μs進行**（2試行とも），
  mintstatus=0x00000000（mil=0）。リング計装ビルドでの長時間観測でも
  85秒以上hrtcnt進行・割込み1800回超を継続配送（凍結前は221回で停止）。
  UART上も従来未達だった`esp_shim: task 'wifi' -> tskid 1`・
  `wifi driver task: ...`（wifiドライバタスクの起動）まで**全ブートで
  到達**（修正前は実施27最終確認9ブート＋本ラウンド3ブートで出現0回）。
  タスクプリエンプションが効き始めたことによるログの並行交錯も出現。
- **(ii) PHY較正の完走：FAIL**。生ADC（MODEM0+0x81C）=0x00000000・
  done16=0のまま（SWD無効化下で100秒観測しても不変）。ハング位置は
  `phy_iq_est_enable_new`のdoneビット待ち（`esp_shim_time_us`ポーリング）
  ＝既知の壁と同一。SWDT（rst:0x12）サイクルも継続。
- **(iii) scan到達：未達**（(ii)がブロック）。

**判定**：mil固着凍結はASP3/C5ポートの実在の致命バグであり修正は必要
条件だったが，**トーンADCゼロ（較正ハング）の原因ではなかった**——
実施27の最優先仮説「凍結が較正ハングの真因」は**反証**。実施26/27の
判別実験の結論（「stockブートが確立するソフト到達可能な状態」がASP3冷間
ブートに欠けている）が探索対象として復活する。ただし凍結解消により，
(a)較正チェーンはwifiドライバタスク上で正常なタスク/割込み環境の下で
走るようになった，(b)今後のあらゆる実験から「割込み死」という巨大な交絡
が除去された，という点で探索基盤は大きく改善した。

### 6. C6への所見（申し送り）

- **C6は本バグ非該当**。C6の優先度マスクはPLIC_MXのTHRESHメモリマップト
  レジスタへのlw/swによる完全ソフトウェア方式（irc_begin_int/irc_end_int
  で保存・昇格・復元）であり，**CLICのmilに相当する「mret以外で降格
  できないハードウェア状態」が存在しない**。mret非経由出口はC6にも同様に
  あるが，マスク復元はirc_end_int（全出口共通）で完了し，MIE再許可も
  dispatcher_2/start_r/dispatch()呼出し元のunlockが行うため取り残しは
  ない。C6の割込みが〜140/s発火し続けていた観測とも整合。deaf-RXとは
  無関係。
- 逆に**CLIC搭載チップ（C5・C61・H4・P4等）へ今後ポートする場合は本修正
  （または全出口mret化）が必須**。TOPPERSのRISC-V共通部を使う限り
  同型バグが必ず発生する。

### 7. C5#1最終状態（終了処理）

- **flash＝`build/c5_idf61_uart`（修正込み・リング計装なし・uart0
  コンソール）フル4MB@0x0**。最終確認ブート（RTSリセット，30秒）：
  バナー→wifi task起動→較正ハング（raw_adc=0）→SWDTサイクル，の
  修正後標準症状を確認。0x200000域はフル4MBイメージにより消去状態。
- C5#2：物理切断のまま未接触。C6 board C（`14:C1:9F:E0:5A:9C`＝ttyACM0）
  ・UARTブリッジ`125a266b...`（=ttyUSB1）：`/dev/serial/by-id`一覧と
  udevadmでの識別以外未接触（DUTブリッジ`b04e3bcf...`=ttyUSB0を毎回
  by-id照合して使用）。

### 8. 次ラウンドへの申し送り

1. 探索は実施26/27の「stockブートが確立するソフト到達可能な状態」の
   絞り込みへ回帰する（実施26申し送り3：stock完走直後 vs ジャンプ直後の
   2点JTAGスナップショット差分が本命）。ただし今後は凍結交絡なしで，
   ASP3上でdly_tsk等も正常動作する環境で実験できる。
2. `ESP32C5_CLIC_DEBUG_RING`（`ASP3_EXTRA_COMPILE_DEFS`で有効化）と
   `r28_ringdump.py`/`r28_hrtcheck.py`（スクラッチ）は割込み配送問題の
   汎用診断として再利用可能。
3. SWDT（rst:0x12，約8秒周期）が較正ハング中に発火し続ける件は未解決の
   別問題（ASP3はWDT無効化しているはずだが，wifi経路で再有効化されて
   いる可能性）。JTAG実験時はr21ハーネスのSWD無効化burstで回避できる。
4. test_portingは「割込みからの遅延ディスパッチ／wake-from-idle」を
   検出できない（§2）。移植検証テストとしてこの経路を踏む項目（例：
   dly_tsk待ちからのタイマ起床）の追加はasp3_core側の改善候補として
   申し送り（本リポジトリからは編集不可）。

### 変更ファイル

- `asp3/arch/riscv_gcc/esp32c5/chip_support.S`：irc_begin_intへの
  synthetic mret（恒久修正）＋リング診断（`ESP32C5_CLIC_DEBUG_RING`，
  既定無効）＋冒頭コメントを実施28の確定事実へ更新。
- `asp3/arch/riscv_gcc/esp32c5/chip_kernel_impl.c`：リング診断バッファ
  定義（既定無効）＋clic_initialize()の知見コメント更新。
- `docs/c5-bringup.md`：本節（実施28）追加。
- ビルド：`build/c5_r28_ring`（wifi_scan+リング計装），`build/c5_r28_tp`
  （test_porting回帰），`build/c5_idf61_uart`（標準ビルド再ビルド＝最終
  flash内容）。
- スクラッチ（リポジトリ外）：`r28_ringdump.py`・`r28_hrtcheck.py`・
  `r28_acm_capture.py`・`r28_*.log`/`.ring.txt`一式。
- git commitは行っていない（指示どおり）。

---

## 実施29：Codex round2最重要判別実験——stock `app_main` 先頭（NVS/WiFi/PHY一切未実行）で即ハンドオフしたところ、**ASP3自身のesp_wifi_init＋較正が完走し、scanまで実際に完走（20〜25AP検出、2/2再現）**。判定：鍵の状態は「stockのPHY較正/WiFi実行」ではなく「ESP-IDF標準ブート列（2nd-stageブートローダ〜call_start_cpu0〜esp_system_init）」にあると確定（Codexの候補6が的中、候補1「PHY較正の残留」は明確に反証）

### 背景・目的

実施26/27のハンドオフは「stock scanが完走した**後**」にジャンプしていたため、
鍵の状態を作ったのが (A) bootloader/startup（ブート列そのもの）か、(B) stockの
PHY較正/WiFi実行そのものか、まだ区別できていなかった（`tmp/codex_c5_round2_
output.txt`の批判：「stock scan/PHY較正が作った保持状態」を排除できていない）。
Codexが最重要と位置づけた判別実験——**stockの`app_main`先頭（NVS・WiFi・
`esp_wifi_init`・`esp_phy_enable`を一切呼ばない状態）で即ハンドオフし、ASP3の
冷間相当フロー（実施28のCLIC修正込みwifi_scan）を走らせて較正が完走するかを見る」
を実施する。

判定基準（事前固定）：
- **完走する**（done16の0→1遷移をASP3自身が起こす。stock残存値との区別は
  実施27のdone16時系列手法を流用）→ 鍵はbootloader/startup側 → 次段は
  `ESP_SYSTEM_INIT_FN`レベルでの二分探索へ
- **完走しない**（raw_adc=0のまま較正ハング）→ 実施26/27の成功はstockのPHY/WiFi
  実行が作った状態だった → 次段はstock側PHY実行の段階分割＋残留試験へ

advisor事前レビューで4点の解釈上のガードを確認した上で着手した：
(1) 実施26型の「pre-esp_wifi_init時点でraw_adc非ゼロ」というシグナルは本実験
では原理的に出ない（stockが較正を一度も走らせていないため`+0x81C`はstock側
でも未確定）——判定はASP3自身の`esp_wifi_init`によるdone16の0→1遷移で行う，
実施26ではなく実施27と比較すること。
(2) バナー・`wifi_diag`周期タスク・`esp_wifi_init`到達が確認できて初めて
「機構は動いた」と言える——ハンドオフ失敗（機構）と較正失敗（本題）を必ず
区別する。
(3) FAILの場合は「ブートが状態を確立しなかった」のか「確立したがASP3の
`hardware_init_hook`が壊した」のかが未分離であることを明記する。
(4) stock（uart0既定コンソール）とゲスト`c5_idf61_uart`（`ESP32C5_CONSOLE=
uart0`，CMakeCache.txtで確認）が同一ブリッジ（`b04e3bcf...`=ttyUSB0）に出る
ことを確認済み——実施27のusbjtagコンソール罠を回避。

### 1. 実装：ハンドオフ呼出し点をstockの`app_main`先頭へ移動

実施26/27のスクラッチプロジェクト（`stock_scan/`、`asp3_jump.c`は無改造で
再利用——実施27で確立したROM-ifポインタ領域`[0x4085fb80,0x4085ffc8)`の
ゼロクリアと証拠プリントをそのまま流用）を丸ごと複製し，新規スクラッチ
プロジェクト`stock_headjump/`を作成。`main/scan.c`を以下に置き換えた
（NVS初期化・`wifi_scan()`呼出しを完全に削除，`app_main`の最初の一文が
ジャンプ）：

```c
void app_main(void)
{
    esp_rom_printf("r29_headjump: app_main entered (no NVS/WiFi/PHY executed) -- jumping now\n");
    asp3_jump_now();
}
```

`main/CMakeLists.txt`の`PRIV_REQUIRES`は`esp_wifi hal`のまま維持した
（`asp3_jump.c`の証拠プリントが参照する`g_wifi_osi_funcs`シンボルの解決に
必要——コンポーネントのリンクのみで自動初期化フックは無いことを実機の
`g_osi_funcs_p`readingで後述のとおり確認済み）。`sdkconfig.defaults`の
`CONFIG_ESP_SYSTEM_MEMPROT=n`（実施26で発見したW^X PMPロック解除）は
そのまま継承。ゲストは実施28の標準ビルド`build/c5_idf61_uart`（CLIC
synthetic mret修正込み，計装入り）を640KB切詰めでオフセット`0x200000`へ
配置——実施26/27と同一の手法・同一のゲストソース。

ビルド確認：`idf.py set-target esp32c5 && idf.py build`（`rc=0`）。
`riscv32-esp-elf-readelf -s`で`asp3_jump_now`のリンク後アドレス
`0x408027d2`（IRAM常駐）を確認。

### 2. 判別実験本体：結果は**PASS**（生ADC非ゼロ・done16 0→1・**scan自体が完走**、2/2再現）

`esptool write-flash`でstock_headjump一式（bootloader@0x2000・
partition-table@0x8000・app@0x10000）＋ゲスト@0x200000を書込み，
UARTブリッジRTSリセット後の出力を独立2試行キャプチャした
（`r29_headjump_trial1.log`／`r29_headjump_trial2.log`）。

両試行とも同一の結果（値は試行間で変動＝生きた信号）：

```
I (252) main_task: Started on CPU0
I (262) main_task: Calling app_main()
r29_headjump: app_main entered (no NVS/WiFi/PHY executed) -- jumping now
asp3_jump_now: masking interrupts
asp3_jump_now: g_osi_funcs_p=0x00000000 g_coa_funcs_p=0x00000000 stock_osi_tbl=0x4080eaac

TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 ...

wifi_diag: raw_adc=0x00000000 done16=0 pd_top=0x00 pd_hpaon=0x00      ← esp_wifi_init前（advisor予測どおりゼロ）
...
wifi_scan: initializing shim
wifi_scan: esp_wifi_init
esp_shim: task 'wifi' -> tskid 1 (prio 23)
wifi_scan: esp_wifi_start -> 0
wifi_scan: DIAG set_promiscuous_rx_cb -> 0
wifi_diag: raw_adc=0x0a520a4c done16=1 pd_top=0x00 pd_hpaon=0x00      ← esp_wifi_init後：ASP3自身の較正で成立（試行2は0x0a580a5a）
...
wifi_scan: esp_wifi_scan_start -> 0
esp_event: WIFI_EVENT id=1
wifi_scan: 20 APs found (err=0)                                       ← 試行1（試行2も20 APs found）
  [0] <SSID-2G> (rssi=-57 ch=1)
...
wifi_scan: RESCAN 24 APs (err=0)                                       ← 試行1（試行2はRESCAN 25 APs）
```

観測点（判定基準・advisorガードへの対応）：

- **`g_osi_funcs_p=0x00000000`**（証拠プリント，2/2）——実施26/27のような
  「stockのテーブルへのstaleポインタ」が**存在しない**。stockが
  `wifi_osi_funcs_register`を一度も呼んでいない（＝WiFi/PHYコードパスに
  一切入っていない）ことの直接証拠。ROM-ifポインタ領域ゼロクリアは
  この状況では実質no-opだった（既にゼロ）。
- **`esp_wifi_init`前のdone16=0・raw_adc=0**（2/2）——advisorが予測した
  とおり，実施26のような「pre-init時点で既に非ゼロ」というシグナルは
  出ない（stockが較正を一度も走らせていないため当然）。
- **`esp_wifi_init`後にdone16が1へ遷移し，raw_adcが非ゼロの生きた値へ**
  （2/2，試行間で`0x0a520a4c`→`0x0a580a5a`と変動）——これは**ASP3自身の
  `esp_wifi_init`が呼んだ較正チェーンが成立した**ことを示す，実施27と
  同型のシグネチャ。
- **scanが実際に完走**（`20 APs found`→`RESCAN 24/25 APs`，SSID/RSSI/
  chまで正常出力，2/2）——実施27はここでCLIC mil凍結によりscan未達
  だったが，実施28の修正が入った今回のゲストではその壁も無い。
  実施26/27を上回る「最も強い形」の完走。

### 3. 判定：**A（ブート列側）が確定**——B（PHY較正/WiFi実行の残留）はこの実験で明確に反証

事前固定基準に照らし，本実験は「完走する」側にきれいに倒れた。しかも
stockは`nvs_flash_init`すら呼んでおらず，`esp_wifi_init`／`esp_phy_enable`／
scanのいずれのコードパスにも一度も入っていない
（`g_osi_funcs_p=0x00000000`が直接証拠）。**したがって実施26/27で観測された
「鍵の状態」は，stockのPHY較正やWiFi runtime実行が作ったものではあり得ない**
——鍵は，stockの**ESP-IDF標準ブート列**（2nd-stageブートローダ→
`call_start_cpu0`→`esp_system_init`等，`app_main`に到達するまでの経路）に
確立されている，という結論になる。Codex round2の候補リストでいえば
「候補1：stockのPHY較正そのものが作った保持状態」は明確に反証され，
「候補6：PMP/cache/MMU/CPU startup状態」または「候補2〜4のクロック/PMU/
電源系のブート時一回性シーケンス」のいずれか（＋実施26で機構的に必要
だった`CONFIG_ESP_SYSTEM_MEMPROT=n`によるPMP解除自体も含む）が生きた
候補として残る。

advisorガード(2)（機構失敗と較正失敗の区別）：本実験ではバナー・
`wifi_diag`周期タスク・`esp_wifi_init`到達に加えてscan完走まで確認できた
ため，「機構は完全に動作した」ことに疑問の余地はない。advisorガード(3)
（B1/B2分離）は，Aが確定した以上そもそも該当しない（B自体が起きて
いないので「stockが確立してASP3が壊した」という分岐は発生しない）。

**厳密には**，本実験が比較しているのは「ESP-IDF標準ブート列＋ハンドオフ
操作（割込みマスク／ROM-ifポインタ域ゼロクリア／MMU unmap-remap／cache
invalidate）」対「ASP3 Direct Boot」であり，ハンドオフ操作自体も一種の
共変量である。ただしこれらの操作はいずれもMODEM/PMU/PHYアナログ域に一切
触れない（レジスタ書込み先はSRAM上のROM-ifポインタ領域とMMU/cache HAL
のみ）ため，「鍵となる状態」の発生源としては妥当性が低い。かつ，この
ハンドオフ機構自体は実施26/27/29を通じて完全に同一であり，次段の
`ESP_SYSTEM_INIT_FN`二分探索でも変更しない予定——共変量として固定され
続けるため，二分探索の結果はブート列側の中で局所化できる。

### 4. ハンドオフ直前スナップショット（JTAG、次段の差分解析用）

当初`asp3_jump_now`エントリ（`0x408027d2`）にHW breakpointを張る方式で
試みたが，**RTSリセット→USB-JTAG再列挙検出→OpenOCD attachのレース**が
実際の起動時間（app_main到達〜ジャンプまで約260ms）より遅く，1回目の
試行はbreakpoint未着火（`NO-HIT`，機構問題として記録・破棄）。原因は
実測で確認：早期halt時点で既にPC`0x42027946`——ASP3ゲストのflash
マッピング域に入っており、素のheadjumpビルドでは間に合わないと判明。

対策として，**スナップショット採取専用**の別ビルド`stock_headjump_snap`
（`scan.c`にジャンプ直前3秒のbusy-wait`esp_rom_delay_us(3000000)`を追加，
判定結果には無関係）を作成し，同じbreakpointで2回採取に成功
（`r29_snapshot_boot1/2.dump.txt`，MODEM0(SYSCON/LPCON域含む)/MODEM1/
MODEM_PWR0/PCR/PMU_HP_LP/LP_CLKRST/LP_AON/LP_I2C_ANA_MST/LPPERI/LP_ANA/
PVT_MONITORの11ブロック）。2試行間の差分は`LP_AON`のRTCカウンタ・
`LPPERI`のノイズ源・`PVT_MONITOR`の温度/電圧系動的読み値のみ（想定内の
生きた値のばらつき，構造的な差ではない）——安定した採取手法として
確立した。この2試行分が「stock ESP-IDF標準ブート列完了時点」の
スナップショットであり，**scan完走後ハンドオフ時点の同リスト
（実施26/27では時間の都合で見送られ，まだ存在しない）と将来比較する
ための最初の材料**となる。

**次段への注意（一貫性）**：本スナップショットは「`asp3_jump_now`
エントリでbreakpoint」かつ「ジャンプ前3秒delay付き」という条件で採取した。
将来，scan完走後ハンドオフ時点のスナップショットと比較する際は，
**同じ採取点（asp3_jump_now直前）・同等のタイミング条件**で採る必要がある
——そうしないと，RTC/PVT/ノイズのドリフト（今回既に確認した唯一の
ブート間差分）と3秒delayのオフセットが，構造的な差と誤認される恐れが
ある。

### 5. 追加の安価な対照

Aが明確に確定したため，Codexの「B側の残留試験」（stock scan完走→
`esp_wifi_stop`/`esp_wifi_deinit`/`esp_phy_disable`後にハンドオフ）は
本ラウンドでは実施していない——これはB（PHY残留）を疑う場合の対照
であり，本ラウンドの結果はB自体を明確に反証しているため優先度が
下がったと判断した。

Codexの「bootloader完了直後（appの`call_start_cpu0`前）でのハンドオフ」
（二分探索の設計2）は，実装が重い（2nd-stage bootloaderのソース改造・
別のジャンプ機構が必要）ため，本ラウンドでは**設計メモのみ**に留める：
- 次段の二分探索は「bootloaderそのもの」と「app側`call_start_cpu0`/
  `esp_system_init`」を切り分けることが情報量最大——Codexの二分探索案
  どおり，`ESP_SYSTEM_INIT_FN`のinit levelでの挿入（アセンブリを触るより
  安全）を優先する。
- 具体的には，`components/esp_system/startup.c`の`esp_system_init_fn`群
  （`clk.c:esp_clk_init`／`pmu_init.c:pmu_hp_system_init`・
  `pmu_lp_system_init`／`esp_rtc_init`等，Codex回答の候補2〜4と対応）の
  前後にハンドオフ呼出し点を段階的に増やしていく設計が有力。
- **切る順序**：まず**app側で最も早い時点**（`esp_clk_init`／`pmu`系／
  `esp_rtc_init`のいずれよりも前，`esp_system_init_fn`群の先頭）で
  ハンドオフして本ラウンドと同じくPASSするか確認する。もしそこでも
  PASSするなら，鍵は2nd-stageブートローダまたはROM側にあると判明し，
  その時点で初めて（コストの高い）bootloader完了直後ハンドオフが必要に
  なる。もしFAILするなら，鍵はapp側startup（`esp_clk_init`〜
  `esp_system_init`完了の間）に存在すると判明し，そのまま
  `esp_system_init_fn`群の間で二分探索を続行する——安価な切り分けを
  先に行うことで，重い実験（bootloader改造）が必要になるケースを
  最小化する。
- 本ラウンドの結果により「PHY較正済み状態」という誤った先入観を排除
  できたため，次段はブート列側だけに絞って安全に二分探索を進められる。

### 6. C5#1最終状態（終了処理）

- ベースライン再確認（1回）：`build/c5_idf61_uart`（実施28修正込み，
  本ラウンド開始時点の現flashと同一ビルド）をフル4MB@0x0へ書込み，
  RTSリセット後UARTキャプチャで`raw_adc=0x00000000 done16=0`・
  `rst:0x12 (RTC_SWDT_SYS)`によるSWDT周期リセットを確認——実施28終了時
  点の症状と完全一致，退行なし（本ラウンドは時間の都合上メイン実験の
  後に実施したが，同一ビルドでの再確認のため有効性に影響しない）。
- 本ラウンド終了処理として同じ`build/c5_idf61_uart/asp_flash.bin`を
  フル4MB@0x0へ再書込み（0x200000のゲスト残置・stock_headjump系は
  全て消去），最終確認1回：バナー正常・`raw_adc=0x00000000 done16=0`・
  較正ハングの標準症状を再確認。
- C5#2：物理切断のまま未接触。C6 board C（`14:C1:9F:E0:5A:9C`）・
  UARTブリッジ`125a266b...`：`/dev/serial/by-id`一覧での識別以外
  未接触（DUTブリッジ`b04e3bcf...`=ttyUSB0を毎回by-id照合して使用）。

### 変更ファイル

- `docs/c5-bringup.md`：本節（実施29）追加。
- リポジトリ内ソース変更：**無し**（arch/target/appとも無改造。実施26/27/28
  の資産をそのまま流用）。
- スクラッチ（リポジトリ外）：`stock_headjump/`（新規，`stock_scan/`の
  複製＋`main/scan.c`をapp_main先頭ジャンプへ書換え）・
  `stock_headjump_snap/`（新規，スナップショット採取専用，3秒delay追加）・
  `r29_guest_640k.bin`（`build/c5_idf61_uart/asp_flash.bin`から640KB切詰め）・
  `r29_snapshot.py`（新規，JTAGスナップショット採取ハーネス，r21_capture.py
  流用）・`r29_headjump_trial1/2.log`・`r29_snapshot_boot1/2.dump.txt`・
  `r29_baseline_recheck.log`・`r29_final_state_console.log`ほか
  `r29_*.log`一式。
- git commitは行っていない（指示どおり）。

---

## 実施30：ハンドオフ点の二分探索——鍵の状態は単一ではなく**2つに分離**していると判明。(A)較正キー＝P1〜P4の消去法で`call_start_cpu0`内の全候補（`init_cpu`/`get_reset_reason`/`init_bss`/`ext_mem_init`/`sys_rtc_init`=`esp_rtc_init`/`mspi_init`）を除外でき，**2nd-stage bootloader自体にほぼ確定**（次段=2A）。(B)scan/RX（deaf-RX）キー＝`esp_clk_tree_initialize()`＋`esp_clk_init()`（`esp_perip_clk_init()`は既にASP3側で遅延移植済みのため除外，UART文字化け消失点の傍証から`esp_clk_init()`＝CPU周波数切替えが本命）。P1（`system_early_init`直前）でハンドオフすると較正は完走するがscanは恒久0件・promiscuous受信も0件という「一見C6のdeaf-RXに似た」症状が出たが，advisorの介入で「ジャンプが早すぎるゆえのクロック未確定アーティファクト」と正しく切り分け，P2/P3で反証（C6線への安易な統合は回避）。ASP3ソース変更・冷間ブート決定実験は未実施（較正キー未確定のため時期尚早と判断，手順4に従い区切りを固めて申し送り）

### 背景・目的

実施29で「鍵はESP-IDF標準ブート列（2nd-stage bootloader〜`call_start_
cpu0`〜`esp_system_init`）のどこか」まで絞り込んだ。本ラウンドは
Codex round2の二分探索案（`tmp/codex_c5_round2_output.txt`）に従い，
ハンドオフ点を`call_start_cpu0`内のできるだけ早い点へ動かして境界を
特定する。

### 1. 手法：スクラッチ内`esp_system`コンポーネントoverride

`~/tools/esp-idf-v6.1`は禁則により直接編集できない。標準的な
`-Wl,--wrap`は`system_early_init()`が`static`関数のため機能しない
可能性が高く（コンパイラがローカル呼出しを直接解決し得るため），
確実な方法として**ESP-IDFのコンポーネント検索パス機構**（プロジェクト
直下の`components/<同名>/`が組込みコンポーネントを完全に上書きする
仕様）を利用した：

- スクラッチ`stock_earlyjump/`（実施29の`stock_headjump/`を複製）に
  `components/esp_system/`として`~/tools/esp-idf-v6.1/components/
  esp_system/`を丸ごとコピーし，**このスクラッチ内コピーだけ**を
  `cpu_start.c`で編集した（SDK本体は無改造）。`idf.py set-target`の
  ログで`Component paths`が`.../stock_earlyjump/components/esp_system`
  を指すことを確認済み。
- `main/scan.c`は実施29のまま（未到達になるため内容は無関係）。
- ハンドオフ本体`asp3_jump.c`（実施26/27/29から無改造で流用）は
  IRAM常駐・`mmu_hal`/`cache_hal`/ROM `esp_rom_printf`のみに依存する
  ため，どの挿入点でも動作要件は同じ。ただし**`mmu_hal_unmap_all`／
  `mmu_hal_map_region`は`cache_hal_init`/`mmu_hal_ctx_init`
  （`ext_mem_init()`内）が完了済みであることが前提**——これが本ラウンド
  で試せる最速点の下限になる（後述）。

### 2. P1：`system_early_init()`呼出し直前（`ext_mem_init`/`sys_rtc_init`/
`mspi_init`完了後の`call_start_cpu0`内最速点）

`cpu_start.c`の`call_start_cpu0`，`mspi_init();`直後・
`system_early_init(rst_reas);`直前（ソース上の`Separator`コメント
直後）に`asp3_jump_now()`を追加。ここは`ext_mem_init()`が既に
cache/mmu HALを初期化済み（ジャンプ機構の前提を満たす）かつ，
`esp_clk_init`／`esp_perip_clk_init`／コンソールUART baud再設定／
`core_intr_matrix_clear`を含む`system_early_init()`本体は**一切
実行していない**，という点で「ジャンプ機構が成立する範囲でできる
だけ早い点」。

結果（`build/c5_idf61_uart`ゲスト，640KB切詰め@0x200000，2/2独立
RTSリセット試行，`r30_p1_trial1.log`/`r30_p1_trial2.log`）：

- **較正は完走**：`raw_adc`＝試行1 `0x0a6e0a68`／試行2 `0x0a4c0a56`
  （非ゼロ・試行間variation＝生信号），`done16`は`esp_wifi_init`前0→
  後1（実施27/29と同型のシグネチャ）。
- **しかしscan/RXは恒久ゼロ**：`promisc_rx_count=0`（2/2），
  `wifi_scan: 0 APs found`（2/2），40秒延長キャプチャでも
  `RESCAN 0 APs`を4回連続確認（`r30_p1_trial2_long.log`）——
  一過性ではなく安定した「較正は通るがRXが死んでいる」状態。
- ハンドオフ直後のUARTコンソールが断続的に文字化けした
  （`asp3_jump_now`の証拠プリント以降，ASP3自身のprintf出力の一部が
  乱れる）。

### 3. advisor介入：「C6のdeaf-RXが再現した」と早合点しないためのガード

P1の結果を報告した時点でadvisorへ相談した。要点：

- **UART文字化けは副次情報ではなく直接証拠**：`esp_clk_init`／
  `esp_perip_clk_init`が未実行＝クロックツリーが確定する前にジャンプ
  している証拠であり，`promisc_rx_count=0`はその「クロック未確定な
  ジャンプ」の期待される帰結（深刻な新事実ではなく単なるアーティ
  ファクトの可能性が高い）と評価すべき。
- **「C5でdeaf-RXが再現した，C6と同根」という統合仮説は書くべきでは
  ない**——MEMORY.mdで「C6/C3のRF-cal根源共有仮説はDROPPED」と既に
  一度整理されており，同じ性急な統合の再演になりかねない。
- **決定実験**：ハンドオフ点を`esp_clk_init`実行直後（CORE
  init-fn群の後・scheduler開始前）まで進め，2試行で「UARTが正常化し
  RXも回復する」か「UARTは正常化してもRXは死んだまま」かを見る。
  前者ならP1のRX死はジャンプ時期尚早のアーティファクトで**C6線への
  独立反証（クリーンな負の結果）**，後者なら本物のfully-clocked
  deaf-RXとしてC6と初めて比較可能になる。

### 4. P2：`esp_perip_clk_init()`完了直後（`system_early_init()`の
クロック／コンソール処理が全て完了した時点）

`system_early_init()`内，コンソールUART baud再設定
（`_uart_ll_set_baudrate`）＋`ESP_EARLY_LOGI`直後（＝
`esp_clk_tree_initialize`/`esp_clk_init`/`esp_perip_clk_init`/
`g_startup_time`/`core_intr_matrix_clear`が全て完了済み）へ
`asp3_jump_now()`を移設。

結果（2/2，`r30_p2_trial1.log`/`r30_p2_trial2.log`）：**較正・
scan・RXとも完全PASS**——`promisc_rx_count=210`／`232`，
`wifi_scan: 20 APs found`／`20 APs found`，`RESCAN 24 APs`／
`23 APs`（実施29のstock_headjump実測とほぼ同等の規模）。advisor
予測どおり「UART正常化とともにRXも回復」——**P1のRX死はジャンプ
時期尚早のクロック未確定アーティファクトであり，C5独自の
fully-clocked deaf-RXではない**と判定（C6線への性急な統合を回避
できた，という意味でのクリーンな負の結果）。

### 5. P3：`esp_clk_init()`直後・`esp_perip_clk_init()`呼出し直前
（P1とP2の間をさらに分割）

ASP3の`esp_wifi_adapter.c`（実施6/13相当の移植）は，`esp_wifi_init`
経路の中で`modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,
MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0U)`という**`esp_perip_clk_init()`の
代替呼出しを既に持っている**（`wifi/esp_wifi_adapter.c:1299-1301`，
コメントで明記）。したがって「P1→P2間の鍵は本当に`esp_perip_
clk_init()`か，それとも`esp_clk_init()`（CPU周波数切替）か」を
切り分ける価値が高いと判断し，`esp_clk_init();`の直後・
`esp_perip_clk_init();`の直前へ`asp3_jump_now()`を追加した
（同じスクラッチ内overrideをさらに編集）。

結果（2/2，`r30_p3_trial1.log`/`r30_p3_trial2.log`）：**P2と同じく
完全PASS**——`promisc_rx_count=270`／`281`，`20 APs found`（2/2），
`RESCAN 23 APs`／`24 APs`。**`esp_perip_clk_init()`はscan/RX-keyでは
ない**（ASP3側の遅延移植で既に代替されているため当然の帰結）。
scan/RX-keyは`esp_clk_tree_initialize()`＋`esp_clk_init()`の区間に
narrowingされた。

### 6. scan/RX-keyの実体候補：`esp_clk_init()`の`rtc_clk_cpu_freq_
set_config()`（CPU周波数切替＝共有PLLの起動）

`~/tools/esp-idf-v6.1/components/esp_system/port/soc/esp32c5/clk.c`
の`esp_clk_init()`（159行目付近，`__attribute__((weak))`）の中身を
確認した。ブート時点のCPU周波数設定（`old_config`，通常bootloaderの
低め初期値）から`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ`（フルスピード）へ
**能動的に切り替える**`rtc_clk_cpu_freq_set_config(&new_config)`を
呼んでいる。これはXTAL直結ではなくPLLベースの周波数へ切り替える
操作であり，CPU/APB用のPLLと，WiFiモデムのRFセクションが参照する
PLL資源が共有されているチップでは，このタイミングでモデム側の
アナログ参照クロックが実質的に初めて安定供給される可能性がある
（Codex round2の候補4「clock tree / PLL lock / glitchless muxの
過渡状態」が的中した形）。もう一つの候補として`esp_clk_tree_
initialize()`（`esp_hw_support/port/esp32c5/esp_clk_tree.c:84`）も
`_clk_gate_ll_ref_240m_clk_en(false)`等，現在使用していないPLL
参照クロックのゲート操作を行っており，副作用皆無ではない——両関数を
個別に切り分けるさらなる二分探索は本ラウンドでは実施していない
（時間の都合，次段へ申し送り）。

**追加の状況証拠（UARTの文字化け消失点）**：P3のハンドオフ点
（`esp_clk_init()`直後）は，コンソールUART baudの明示的再設定
（`_uart_ll_set_baudrate`，842〜848行目）や`core_intr_matrix_clear()`
（838行目）よりも**前**にある。それにもかかわらずP3のUARTコンソール
出力はP1と比べて明確にクリーンだった（コアなdiag行の文字化けは
解消）。これは「UART文字化けの解消はbaud再設定やintr matrix clear
が原因ではなく，`esp_clk_init()`自身がAPB周波数をASP3の`CORE_CLK_
MHZ`前提と一致する値へ引き上げたこと」を示唆する一次証拠であり，
`esp_clk_tree_initialize()`より`esp_clk_init()`（特に`rtc_clk_
cpu_freq_set_config()`）をscan/RX-keyの本命とする根拠を補強する。

**★重要な既存コードとの整合**：ASP3の`hardware_init_hook()`
（`asp3/target/esp32c5_espidf/target_kernel_impl.c`92〜121行）の
「CPUクロックの切替え」節は，**C6の実績（ROMブートローダが起動時点
で既にPCRをSPLL÷3÷1＝160MHzへ設定済み）をC5にそのまま仮定して
クロック切替えレジスタを一切書き換えない**設計になっており，コード
コメント自身が「C5のPCR相当レジスタの実際の初期値は未確認
（【実機確認待ち】）」と明記している。本ラウンドの実測（`esp_clk_
init()`が実際にCPU周波数を能動的に切り替えており，かつその前後で
RXの生死が切り替わる）は，この「ROMが既に適切に設定している」という
仮定を再検証すべき具体的根拠になる——次段の最有力ポート候補。

### 7. 較正キー（(A)）：P4でさらに縮小——`sys_rtc_init`(`esp_rtc_init`)・
`mspi_init`とも除外でき，2nd-stage bootloader自体が最有力候補に確定

P1（`system_early_init()`一切未実行）で較正が既に完走することが
2/2で確認されたため，**Codex round2候補2〜4（PCR/MODEM clock pulse
history・PMU FSM handoff・clock tree/PLL lock）は較正キーの説明には
ならない**（これらは全て`system_early_init()`内部の処理であり，
P1はそれより前にジャンプしているため）。

advisorの指摘で訂正：「`ext_mem_init()`より前でのハンドオフには
自前cache/mmu bootstrapが必要でハング риスクを伴う」という記述は
不正確だった。**`ext_mem_init()`が完了した"直後"であれば，追加の
bootstrap無しで`asp3_jump_now()`の前提（`cache_hal_init`/
`mmu_hal_ctx_init`済み）を満たす**——実際に危険なのは
`ext_mem_init()`**より前**にジャンプする場合のみ。この訂正に従い，
同一の安全な機構のまま`ext_mem_init()`直後・`sys_rtc_init()`
呼出し直前へ`asp3_jump_now()`を移設した（P4，スクラッチの
`components/esp_system/port/cpu_start.c`をさらに編集）。

**P4結果（2/2，`r30_p4_trial1.log`/`r30_p4_trial2.log`）：較正は
完走**（`raw_adc`＝`0x0a3a0a46`／`0x0a4e0a4e`，`done16`は0→1），
**scan/RXはP1と同型で恒久ゼロ**（`promisc_rx_count=0`，
`0 APs found`，2/2——`sys_rtc_init`/`mspi_init`とも未実行なので
scan/RX-keyがまだ満たされないのは予想どおり）。

**この結果の意味**：`sys_rtc_init()`が呼ぶ`esp_rtc_init()`
（PMU/RTC電源設定，C6実施34「フル移植済み・NuttXで反証済み」の
残された「C5では未検証」枠として最有力視していた）も，`mspi_init()`
（flash timing tuning）も，**較正キーとしては除外できた**——
どちらも実行せずに較正が2/2で完走している。P4の手前に残る
`init_cpu()`（`gp`レジスタ設定・例外ベクタ再配置・分岐予測有効化・
WFEモード無効化のみ）・`get_reset_reason()`（読み取り専用）・
`init_bss()`（RAMのmemsetのみ）・`ext_mem_init()`（cache/mmu HALの
XIP設定のみ）は，いずれもMODEM/PMU/アナログ系レジスタへの書込みを
一切含まない（ソース確認済み，394〜476行目）。

**結論：較正キー（A）は2nd-stage bootloader自体（`bootloader_init()`
等）にほぼ確定した**——`call_start_cpu0`内の候補（`esp_rtc_init`含む）
は本ラウンドで実質的に消去法で除外できたため，次段は2A
（bootloaderの初期化列挙＋選択的無効化，またはASP3側への加算移植）
に絞って進めるのが妥当。「さらに早い点＝bootloaderそのものの内側
でのハンドオフ／計装」は，bootloaderは実行ファイル自体が別（app
ではなくbootloader.elf）なので，同じコンポーネントoverride手法は
使えず，bootloaderの改造（またはbootloaderからの独自ジャンプ機構）
が必要になる——次段の実装コストとして明記する。

### 8. なぜ本ラウンドはASP3移植・冷間ブート決定実験を実施しなかったか

較正キー（(A)）が依然未特定である以上，scan/RX-key（(B)）だけを
ASP3の`hardware_init_hook()`へ移植しても，冷間Direct Bootは
（(A)が満たされないため）従来どおり較正ハングで停止し，(B)の効果を
確認できない。そのため「決定実験（冷間ブートで較正完走→scan成功）」
を今回試みても，既知の失敗（`raw_adc=0`ハング）を再現するだけで
新規情報が得られないと判断し，ASP3側のソース変更は本ラウンドでは
行わなかった。これは手順4「1ラウンドで手順3まで届かない場合は，
境界の特定まで確実に固めて申し送る（無理に完走しない）」に沿った
判断である。

### 9. C5#1最終状態（終了処理）

- スクラッチ実験終了後，`build/c5_idf61_uart/asp_flash.bin`
  （フル4MB@0x0，本ラウンド開始時点と同一ビルド）を再書込みし，
  RTSリセット後20秒キャプチャで確認（`r30_final_state_console.log`）：
  `raw_adc=0x00000000 done16=0`・`pd_top/pd_hpaon/pd_hpcpu/pd_lpperi
  =0x1c`（較正未完走の標準値）・`rst:0x12 (RTC_SWDT_SYS)`による
  約数秒周期のSWDTリセットサイクルを確認——実施28/29終了時点と同型の
  既知症状，退行なし。
- C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触
  （前ラウンドから変更なし）。

### 変更ファイル

- `docs/c5-bringup.md`：本節（実施30）追加。
- リポジトリ内ソース変更：**無し**（arch/target/appとも無改造）。
- スクラッチ（リポジトリ外）：`stock_earlyjump/`（新規，
  `stock_headjump/`複製＋`components/esp_system/port/cpu_start.c`
  にP1→P2→P3→P4の4段階でハンドオフ点を移設編集，SDK本体は無改造）・
  `r30_p1_trial1/2.log`・`r30_p1_trial2_long.log`・
  `r30_p2_trial1/2.log`・`r30_p3_trial1/2.log`・
  `r30_p4_trial1/2.log`・
  `r30_p1_flash1.log`・`r30_p2_flash1.log`・`r30_p3_flash1.log`・
  `r30_p4_flash1.log`・
  `r30_earlyjump_settarget.log`・`r30_earlyjump_build.log`・
  `r30_p2_build.log`・`r30_p3_build.log`・`r30_p4_build.log`・
  `r30_final_flash.log`・
  `r30_final_state_console.log`ほか`r30_*.log`一式。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

1. **較正キー（A）の特定**：P4により`call_start_cpu0`内の候補
   （`init_cpu`/`get_reset_reason`/`init_bss`/`ext_mem_init`/
   `sys_rtc_init`=`esp_rtc_init`/`mspi_init`）は全て消去法で除外
   でき，**2nd-stage bootloader自体（`bootloader_init()`等）に
   ほぼ確定**した。次段は2A（bootloaderの初期化列挙＋選択的無効化，
   またはASP3の`hardware_init_hook()`への加算移植）を最優先で
   進める。bootloaderは別実行ファイル（`bootloader.elf`）のため，
   本ラウンドで使ったコンポーネントoverride手法はそのままでは使え
   ず，bootloader側の改造または独自計装が必要になる点に注意。
2. **scan/RX-key（B）のASP3移植**：`hardware_init_hook()`の
   「CPUクロックの切替え」節（現在は無改造でROM任せ）へ，
   `esp_clk_init()`相当（特に`rtc_clk_cpu_freq_set_config()`による
   CPU周波数切替え）を移植する。P3のUART文字化け消失（§6追加証拠）
   は baud再設定や`core_intr_matrix_clear`ではなく`esp_clk_init()`
   自身が原因である可能性を支持しており，`esp_clk_init()`が本命。
   `esp_clk_tree_initialize()`の ref-clockゲート操作も候補として
   残る——両者を個別に切り分ける追加の二分探索（829/830行目の間へ
   1点，1回のビルド編集で十分安価）を先にやってから移植すると
   精度が上がる。
3. **(A)と(B)は無関係な2つの謎ではなく，「クロックレベル」という
   1つの物語の可能性がある**（advisor指摘）：stockの2nd-stage
   bootloaderも`bootloader_clock_configure()`等で独自にクロック設定
   を行っており，P1〜P4の較正はいずれも**その一段階昇格したクロック
   の下で**走っている（reset直後のデフォルトクロックではない）。
   つまり「較正にはbootloaderが与えるクロックレベルXで足り，RXには
   `esp_clk_init()`が与えるさらに上のレベルYが要る」という一本の
   ストーリーである可能性が高く，(A)の探索でもクロック関連の
   bootloaderステップ（`bootloader_clock_configure`等）を優先的に
   疑うべき。
4. (A)(B)の両方が揃って初めて，冷間Direct Bootでの較正完走→scan
   成功という最終決定実験が意味を持つ。(B)だけを先に移植しても
   冷間ブートは(A)未解決のため従来どおりハングする点に注意
   （手順3の決定実験は(A)特定後に実施すること）。
5. P1のdeaf-RX的症状（`promisc_rx_count=0`・`0 APs found`）は
   **C6のdeaf-RXと安易に統合しない**——P2/P3で「クロック確定後は
   RXが正常に働く」ことを2/2×2点で確認済みであり，これはC5独自の
   構造的deaf-RXではなく単なる時期尚早ジャンプのアーティファクト
   だったと判定済み。この判定はadvisor介入によって救われた
   （最初の思考のまま書いていたら「C5でdeaf-RX再現」という誤った
   統合仮説を記録するところだった）——今後も同種の早合点に注意。

---

## 実施31：較正キー(A)候補「bootloader_random_enable/disable()のSAR ADC駆動サイクル」を加算移植＋減算法の両方で検証——**refute確定**（2手法×2/2）。新規JTAG発見（PCR_SARADC_CLKM_CONF_REGのCLKM_ENが実施14-25で見落とされていた恒久差）を伴うが，因果には無関係と判明。次候補は`bootloader_clock_configure()`

### 背景・目的

実施30で較正キー(A)は「2nd-stageブートローダ自体」にほぼ確定した
（`call_start_cpu0`内の候補は全て消去法で除外済み）。本ラウンドは
Codexの候補5「ROM/eFuse由来の一回性アナログ転写」・ユーザプロンプトが
最有力候補として挙げた「`bootloader_random_enable()`系（SAR ADCを
エントロピー源として駆動→`bootloader_random_disable()`）」を，
`bootloader_init()`（`~/tools/esp-idf-v6.1/components/bootloader_support/
src/esp32c5/bootloader_esp32c5.c`）の実行順に机上で列挙した上で検証する。

### 1. (A)候補の机上列挙（`bootloader_init()`実行順）

| 順 | 関数 | アナログ/クロック接触 | 優先度 |
|---|---|---|---|
| 1 | `bootloader_hardware_init()` | `axi_icm_ll_reset_with_core_reset`／regi2cマスタクロック有効化・160MHzソース選択 | 中（後述：有効化分は既にASP3に存在） |
| 2 | `bootloader_ana_reset_config()` | BOD reset enable(mode1)＋`bootloader_power_glitch_reset_config`（PMU_RF_PWC＋I2C_SAR_ADCのglitch検出無効化＋LP_ANAリセット許可設定） | 中〜低（reset**挙動**設定が主・aktive analog touchは無効化のみ） |
| 3 | `bootloader_super_wdt_auto_feed()` | 非関連 | 低 |
| 4 | `bootloader_init_mem`/`clear_bss_section` | RAM操作のみ | 対象外 |
| 5 | `bootloader_clock_configure()` | CPU/APBクロック（PLL）切替 | **高**（実施30 申し送り#3「一つのクロック物語」） |
| 6 | `bootloader_console_init`/`print_banner` | 非関連 | 対象外 |
| 7 | `bootloader_init_ext_mem`（cache/mmu） | 非関連（ASP3のDirect Bootは独自にcache/mmu初期化） | 対象外 |
| 8 | flash ID/XMC/header/validity/spi flash init | flashのみ | 対象外 |
| 9 | `bootloader_check_reset`/`bootloader_config_wdt` | リセット要因読取り・WDT設定 | 低 |
| 10 | `bootloader_enable_random()`=`bootloader_random_enable()` | **SAR ADC(ADC_UNIT_1/2)をreset→enable→sampling駆動．`bootloader_utility_load_boot_image()`直前の`bootloader_random_disable()`まで持続** | **最優先**（ユーザプロンプト指定．較正のトーン測定はSAR ADC読出し＝実施20） |

本ラウンドは10番（最優先指定）を検証した。5番（`bootloader_clock_
configure`）は机上分析のみに留め，次段の最有力候補として申し送る。

### 2. 新規JTAG発見：PCR_SARADC_CLKM_CONF_REGのCLKM_ENが実施14-25で未検査だった恒久差

`bootloader_random_enable()`（`bootloader_random_esp32c5.c`）の内容を
精査した結果，`adc_ll_enable_func_clock(true)`（PCR_SARADC_CLKM_CONF_REG
bit22=CLKM_EN）を立てるが，対応する`bootloader_random_disable()`は
**`BOOTLOADER_BUILD`分岐では`regi2c_saradc_disable()`を呼ばない**設計
（`ANALOG_CLOCK_ENABLE/DISABLE`もBOOTLOADER_BUILDではno-op——「bootloader
では常時有効のまま」というコメントが明記）であるため，CLKM_ENは
disable後も**恒久的に1のまま**app側へ引き継がれる，と判明した。

実機JTAG実測（`r31_regcheck.py`，新規・`halt`→`mdw`→`resume`の単純手法，
2/2再現）：

- **ASP3較正ハング状態**（現flash `build/c5_idf61_uart`，本ラウンド
  開始時点）：`PCR_SARADC_CLKM_CONF_REG(0x6009608c)=0x00004000`
  （CLKM_EN=0，DIV_NUM=4のみ），`PMU_RF_PWC_REG(0x600b0158)=0xff800000`，
  `PCR_SARADC_CONF_REG(0x60096088)=0x00000005`（2/2一致）。
- **stock**（実施29 `r29_snapshot_boot1/2.dump.txt`，同一v9 blob era，
  `app_main`先頭ハンドオフ直前＝2nd-stageブートローダ完了直後）：
  同レジスタ=`0x00404000`（CLKM_EN=1，DIV_NUM=4，2/2一致）。

**CLKM_EN(bit22)がASP3=0・stock=1で恒久的に異なる**——実施14-25/実施20
の網羅比較（PCR_SARADC_CONF等）はこの`PCR_SARADC_CLKM_CONF_REG`を対象に
含んでいなかった（見落とし）。ただし後述のとおり，これは**bootloader_
random系の因果とは無関係**と判明した（3節）。

### 3. 加算移植：ASP3側へbootloader_randomサイクルを移植——**FAIL**（2/2）

`asp3/target/esp32c5_espidf/target_kernel_impl.c`の`hardware_init_hook()`
（`esp_rom_set_cpu_ticks_per_us()`呼出し直後）に，
`esp32c5_r31_bootloader_random_cycle()`を新設・呼出しを追加した
（`#ifdef TOPPERS_ESP32C5_WIFI`でガード）。移植内容：

- `bootloader_hardware_init()`のうち`regi2c_ctrl_ll_master_configure_clock()`
  （MODEM_SYSCON.clk_conf.clk_i2c_mst_sel_160m=1）。`_regi2c_ctrl_ll_
  master_enable_clock(true)`相当は`esp_wifi_adapter.c`の
  `phy_enable_wrapper()`が`0x600af018=0x7`で既に等価に設定済み（実施09で
  実機確認済み）のため対象外とした。
- `bootloader_random_enable()`/`disable()`の**MMIO直叩き部分のみ**を，
  レジスタ順序・自己クリア型パルスを保持して移植：`adc_ll_reset_
  register()`（PCR_SARADC_CONF RST_EN/REG_RST_ENパルス×2），
  `temperature_sensor_ll_reset_module()`パルス，`adc_ll_enable_bus_
  clock`/`enable_func_clock`（PCR_SARADC_CONF/CLKM_CONF），`adc_ll_digi_
  clk_sel(XTAL)`+`controller_clk_div(0,0,0)`，`regi2c_ctrl_ll_i2c_sar_
  periph_enable()`（PMU_RF_PWC RSTB/XPDパルス），パターンテーブル設定
  （`adc_ll_digi_set_pattern_table`のビット詰め計算を転写）＋
  `digi_set_clk_div(15)`+`trigger_interval(200)`+`trigger_enable()`，
  `esp_rom_delay_us(5000)`（実ADC変換を複数回走らせる近似待機），
  disable側の`trigger_disable`+パターンテーブルクリア+`controller_clk_
  div(4,0,0)`（stockの最終値`CLKM_CONF=0x00404000`に一致するようCLKM_EN
  はクリアしない——disable()自体が元々クリアしないため）。
- **意図的に対象外**：`adc_ll_regi2c_init()`/`adc_ll_set_calibration_
  param()`等，I2C_SAR_ADCブロック（regi2c host 0x69）経由のbias/較正
  レジスタ書込み。実施16の8block×reg 0x00-0x1Fフルスイープ・実施25の
  同スイープ再確認で，**block=0x69（有効host=0）は両platform完全一致**
  と既に確定しているため（4節でも再確認），最終値としては差が無いと
  分かっている部分への移植労力を割かなかった。`bootloader_ana_reset_
  config()`（BOD/glitch reset設定）も本ラウンドでは対象外。

ビルド確認：`cmake --build build/c5_idf61_uart`（rc=0，`target_kernel_
impl.c`のみ再コンパイル・リンク成功）。

**実機検証**：`esptool write-flash 0x0`でフル4MB書込み，UARTブリッジ
RTSリセット後キャプチャ（`r31_port1_trial1.log`/`r31_port1_trial2.log`，
独立2試行）：**両試行とも`raw_adc=0x00000000 done16=0`のまま**——較正
ハングは解消しなかった（判定基準：raw_adc非ゼロ・done16の0→1遷移を
PASS，恒久ゼロ持続をFAILと事前固定）。

**移植が実際に効いたことの確認**（`r31_port1_regcheck.log`）：この
ハング状態のままJTAG実測すると`PCR_SARADC_CLKM_CONF_REG=0x00404000`
——stockの値と完全一致（CLKM_ENが意図どおり1になった）。**「移植した
つもりが実は効いていなかった」という機構的疑いを排除した上での，
確定的なFAIL**。

### 4. 減算法（advisor推奨の決定実験）：stockからSAR ADC状態を明示的に剥ぎ取っても較正は**PASSのまま**——bootloader_random系はload-bearingでないと確定

3節のFAILだけでは「(a)較正キーはbootloader_random系ではない」と
「(b)ASP3側への移植が何かを取りこぼしている（regi2c媒介部分等）」を
区別できない（advisor指摘）。これを混同なく判定するため，実施30で
確立した`stock_earlyjump`のP4ハンドオフ点（`sys_rtc_init`直前，較正は
PASSする既知の到達点）の直前に，2nd-stageブートローダの
`bootloader_random_enable()`が残した状態を**明示的に解除する**
`r31_teardown_sar_adc_state()`を追加した。P4はapp componentとして
リンクされる（`BOOTLOADER_BUILD`ではない）ため，regi2c媒介のLL関数も
そのまま使え，`bootloader_random`の全体（regi2c部分含む）を漏れなく
解除できる：

- `adc_ll_set_dtest_param(0)`／`adc_ll_set_ent_param(0)`／
  `adc_ll_enable_calibration_ref(ADC_UNIT_1/2, false)`／
  `adc_ll_set_calibration_param(ADC_UNIT_1/2, 0x0)`
  （`adc_ll_regi2c_adc_deinit()`相当，I2C_SAR_ADCブロックのbias/較正
  コードを0へ）
- `regi2c_ctrl_ll_i2c_sar_periph_disable()`（PMU_RF_PWC の
  XPD_PERIF_I2Cを解除——bootloader-buildパスが意図的に呼ばないため
  ハング状態でも`0xff800000`のまま立っていた電源）
- `REG_CLR_BIT`でPCR_SARADC_CONF/CLKM_CONFのbus/func clockを停止
- `adc_ll_digi_trigger_disable()`

（`stock_earlyjump/components/esp_system/port/cpu_start.c`，`#include
"hal/adc_ll.h"`等を追加，`asp3_jump_now()`直前に`r31_teardown_sar_
adc_state()`を呼ぶ1行を追加。SDK本体は無改造，スクラッチ内override
のみ。）

ビルド：`idf.py build`（rc=0，`cpu_start.c`のみ再コンパイル）。
フラッシュ：`bootloader.bin`@0x2000・`partition-table.bin`@0x8000・
`scan.bin`@0x10000・実施29の`r29_guest_640k.bin`（ASP3ゲスト，本ラウンド
のASP3側変更を含まない実施29時点のクリーンな資産）@0x200000。

**結果（2/2独立RTSリセット試行，`r31_p4teardown_trial1.log`/
`r31_p4teardown_trial2.log`）：較正は完走**——`raw_adc`は試行1
`0x0a660a68`・試行2`0x0a4c0a48`（`esp_wifi_init`前は`0x00000000`，
実施27/29/30と同型のシグネチャ），`done16`は0→1。（`0 APs found`は
実施30既知のP4アーティファクト——`esp_clk_init`未実行によるscan/RX-key
(B)未充足であり，較正=(A)の判定には無関係。）

**SAR ADC/regi2c状態を明示的に全て剥ぎ取っても較正はPASSしたまま**——
`bootloader_random_enable/disable()`が残す状態（regi2c媒介部分を含む
全体）は較正キー(A)のload-bearingな構成要素ではないと，3節の加算
移植（FAIL）とは独立の手法で確定した。

### 5. 事後の安価な追加確認：実施25の記録を再読——regi2cのI2C_SAR_ADCブロックは元々「両platform完全一致」

3節で移植を意図的に見送ったregi2c媒介部分について，実施25の記録
（`docs/c5-bringup.md`4254行目近辺の表）を再確認したところ，
`SAR_ADC(0x69) host=1`の行は「無効ルーティングのアーティファクト」
「**有効なhost=0では両platform完全一致**」と明記されていた——4節の
減算法の結果は，この既存記録（regi2cレベルでは元々差が無い）とも
整合する。

### 6. 判定：bootloader_random_enable/disable()系は較正キー(A)としてrefute確定

加算移植（3節，FAIL・機構的に効いたことを確認済み）と減算法（4節，
PASSのまま）という**独立した2手法**が同じ結論（非load-bearing）を
指しており，かつ実施25の既存regi2cスイープ結果とも整合するため，
本ラウンドの候補（ユーザプロンプト最優先指定）は確定的にrefuteできた
と判断する。2節のCLKM_EN差分自体は実在する新規発見（記録として残す
価値あり）だが，**較正の成否とは無関係な副次的差分**だったと結論する。

### 7. (A)の次候補：`bootloader_clock_configure()`（未着手・申し送り）

1節の机上列挙どおり，次に高優先度な(A)候補は`bootloader_clock_
configure()`（`~/tools/esp-idf-v6.1/components/bootloader_support/src/
bootloader_clock_init.c`）——2nd-stageブートローダが最初にCPU/APBクロック
（PLL経由）を切替える処理。実施30 申し送り#3「(A)と(B)は無関係な2つの
謎ではなく，クロックレベルという1つの物語の可能性がある」（advisor
指摘）とも整合する本命候補。

**未着手の理由・注意点**：
- CPU周波数変更はASP3の`CORE_CLK_MHZ`固定前提（`s_ticks_per_us`・
  `SIL_DLY`較正・`hardware_init_hook()`の既存コメントで「ROMが適切に
  設定している」と仮定して未変更にしている箇所）と直接競合しうる
  ハザードがあり（advisor指摘），ASP3側への加算移植は
  `test_porting`回帰・ハング耐性を慎重に確認しながら進める必要がある。
- 減算法で先に試す方が安全：`stock_earlyjump`のP4はさらに手前
  （`ext_mem_init()`直後）でハンドオフしており，この時点で既に
  2nd-stageブートローダの`bootloader_clock_configure()`は実行済み。
  P4より手前でのハンドオフはcache/mmu HAL未初期化のため使えない
  （実施30で確認済みの制約）——bootloader_clock_configure単体を
  P4で切り分けるには，ブートローダのクロック設定を「P4到達後に
  部分的に巻き戻す」形の減算法が必要になり，4節の手法をそのまま
  横展開できない可能性がある。次段でこの実装可否を先に検討すること。

### 8. (B)（scan/RX-key）：本ラウンドは未着手

本ラウンドの時間はすべて(A)候補の検証（3〜6節）に充てたため，実施30で
特定済みの(B)（`esp_clk_init()`の`rtc_clk_cpu_freq_set_config()`）の
ASP3移植は着手していない。次段でも(A)特定後にまとめて扱う方針は
実施30の申し送りのまま変更なし。

### 9. 決定実験（task手順4）：未実施

(A)が依然未特定のため，冷間Direct Bootでの較正完売→scan成功という
最終決定実験は本ラウンドでも見送った（実施30と同じ判断根拠：(A)が
満たされない状態でASP3側の(B)だけ移植しても，既知の較正ハングを
再現するだけで新規情報が得られない）。

### 10. 回帰確認：`test_porting` 実機 `# 6/6 passed`

`build/c5_r31_test_porting`（新規構成：`cmake -S asp3/asp3_core -B
build/c5_r31_test_porting -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/
toolchain-riscv64.cmake -DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-
-DASP3_TARGET=esp32c5_espidf -DASP3_TARGET_DIR=.../esp32c5_espidf
-DASP3_APPLDIR=asp3/asp3_core/test/porting -DASP3_APPLNAME=test_porting
-DESP32C5_CONSOLE=uart0 -DASP3_EXTRA_APP_C_FILES=.../test/porting/tap.c`，
`ESP32C5_WIFI`は指定せずOFF＝本ラウンドの新規コード
`esp32c5_r31_bootloader_random_cycle()`は`#ifdef TOPPERS_ESP32C5_WIFI`
によりこのビルドには一切コンパイルされない——構造的に無関係かつ
実機でも再確認）。実機フラッシュ後，同一8秒キャプチャ内で**3回連続
起動を観測しすべて`# 6/6 passed`**（`r31_test_porting_trial1.log`）。
退行なし。

### 11. C5#1最終状態（終了処理）

- `build/c5_idf61_uart/asp_flash.bin`（本ラウンドの加算移植込み，FAIL
  確定後もコードは残置——`#ifdef TOPPERS_ESP32C5_WIFI`ガード内・害
  なしと3節で確認済み）をフル4MB@0x0へ再書込み。RTSリセット後15秒
  キャプチャ（`r31_final_state_console.log`）：`raw_adc=0x00000000
  done16=0`・`rst:0x12 (RTC_SWDT_SYS)`による数秒周期リセットを確認——
  実施28/29/30終了時点と同型の既知症状，退行なし。
- C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触
  （前ラウンドから変更なし）。DUTは常に`b04e3bcf...`（ttyUSB0）を
  by-id照合のうえ使用。

### 変更ファイル

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r31_bootloader_random_cycle()`（新設）＋`hardware_init_hook()`
  からの呼出し追加（`#ifdef TOPPERS_ESP32C5_WIFI`ガード）。FAIL確定
  済みだが，2節の新規JTAG発見の再現手順・3節の「移植が機構的に効いた
  ことの確認」の記録として残置（git commitはしない）。
- `docs/c5-bringup.md`：本節（実施31）追加。
- スクラッチ（リポジトリ外，`/tmp/.../scratchpad/`）：`r31_regcheck.py`
  （新規，PMU_RF_PWC/PCR_SARADC_CONF/CLKM_CONFの単純JTAG読取り）・
  `r31_asp3_boot1/2.log`（新規JTAG発見の初回確認）・
  `r31_port1_trial1/2.log`（3節加算移植の実機結果）・
  `r31_port1_regcheck.log`（移植が効いたことの確認）・
  `stock_earlyjump/components/esp_system/port/cpu_start.c`
  （`r31_teardown_sar_adc_state()`追加・`asp3_jump_now()`直前で呼出し，
  4節の減算法）・`r31_p4teardown_trial1/2.log`（減算法の実機結果）・
  `build/c5_r31_test_porting/`（新規，test_porting C5ビルド）・
  `r31_test_porting_trial1.log`（回帰確認）・
  `r31_final_state_console.log`（終了処理の最終確認）。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

1. **(A)の次候補＝`bootloader_clock_configure()`**（7節）。CPU周波数
   変更のハザードに注意し，まず減算法での切り分け実装可否を検討する
   こと（P4の「クロック設定済み状態からの部分巻き戻し」が必要になる
   可能性がある，4節参照）。
2. `bootloader_ana_reset_config()`（BOD/glitch reset設定）は本ラウンド
   で優先度を下げた（advisor評価：アナログ接触は「既に存在するビット」
   か「glitch検出の無効化」のみで，較正のload-bearing候補としては
   弱い）——`bootloader_clock_configure()`がFAILに終わった場合の次点
   候補として残す。
3. (B)（`esp_clk_init()`のASP3移植）は未着手のまま。(A)特定後に
   まとめて着手する実施30の方針を継続する。
4. 2節のCLKM_EN差分自体は「実施14-25の網羅比較から漏れていた新規の
   静的差分」という事実として記録に残すが，**較正には無関係と確定
   済み**——今後このレジスタを再度「有力候補」として持ち出さないこと
   （厳密性基準：一度refuteした指標の再浮上を防ぐ）。

---

## 実施32：`bootloader_clock_configure()`解剖から，ASP3のDirect Bootが
一度もCPUクロックをXTAL(48MHz)からPLLへ切替えていないという構造的事実を
JTAG実測で確定——較正キー(A)は**確定・解消**（冷間ブートで較正PASS，
raw_adc非ゼロ・done16=1を独立2回のRTSリセット試行＋1キャプチャ内5回の
自然WDTリブートすべてで再現）。副産物として実施03の「CPU=192MHz実機
確定」を反証（実際はXTAL48MHz直結だった）。(B)scan/RXは新しい壁
（`set_promiscuous(true)`成功後・`esp_wifi_scan_start`の戻り印字前で
無条件WDTリブートに至るハング）で未達——次段へ申し送り

### 背景・目的

実施31で(A)候補の最有力として`bootloader_clock_configure()`
（2nd-stageブートローダのCPU/APBクロック切替え）が申し送られた。本
ラウンドはこれを解剖し，減算法（部分巻き戻し）または加算法（ASP3への
移植）で判定した上で，可能なら(B)（`esp_clk_init()`相当）も併せて
移植し，冷間Direct Bootでの較正完走→scan成功という最終決定実験を狙う
（task手順どおり）。

### 1. 机上解剖：`bootloader_clock_configure()`の実体

`~/tools/esp-idf-v6.1/components/bootloader_support/src/bootloader_clock_
init.c:bootloader_clock_configure()`は，リセット要因がSW再起動でない
（＝POR相当）限り`rtc_clk_init(clk_cfg)`
（`hal/components/esp_hw_support/port/esp32c5/rtc_clk_init.c`，本
リポジトリのhal submoduleに同名のC5専用実装がある——esp-idf-v6.1側と
ほぼ同一だがシンボル名が一部異なる）を呼ぶ。内容を4項目に分解した：

1. **`rtc_clk_modem_clock_domain_active_state_icg_map_preinit()`**：
   `PMU_HP_ICG_MODEM_CODE_ACTIVE`のICGマップ設定＋MODEM_SYSCON/
   MODEM_LPCONのICGビットマップ設定＋`PMU_IMM_MODEM_ICG_REG`の
   force-updateトリガ。→**実施13で既に同等の内容がASP3へ移植済み**
   （`esp_wifi_adapter.c`の`wifi_clock_enable_wrapper`内，
   `pmu_ll_hp_set_icg_modem`/`modem_syscon_ll_set_modem_apb_icg_bitmap`
   等）。「BBは可制御になったが完了ビットは依然立たず」という実施13
   当時の結論と整合——この項目は較正キー(A)としては既に消化済みと判断。
2. **RC_FAST/RC_SLOW較正パラメタ＋dbias handover**：
   `LP_CLKRST_FOSC_DFREQ`・`I2C_DIG_REG`（`SCK_DCAP`/`ENIF_RTC_DREG`/
   `ENIF_DIG_DREG`/`XPD_RTC_REG`/`XPD_DIG_REG`）と，
   `PMU_HP_ACTIVE_HP_REGULATOR0`のDBIAS_SEL＋DBIAS値（
   `get_act_hp_dbias()`＝eFuse由来のhp_cali_dbias）。→regi2c
   `I2C_DIG_REG`（block=0x6a相当）は実施16/25の8ブロック全域スイープで
   「新規差分ゼロ」と確認済み，`HP_ACTIVE.HP_REGULATOR0`は実施21の
   PVT候補として実機注入・因果棄却済み（`get_act_hp_dbias()`の値自体は
   `pmu_hp_system_init()`の`regulator0.dbias`と同一の値を書く関数
   であることをソースで確認——実施23が扱った経路と実質同一）。→この
   項目も較正キー(A)としては既に消化済みと判断。
3. **CPU周波数切替え**：`rtc_clk_cpu_freq_get_config()`→
   `rtc_clk_cpu_freq_mhz_to_config(cfg.cpu_freq_mhz)`→
   `rtc_clk_cpu_freq_set_config()`。stockのsdkconfig実測
   （`stock_scan/sdkconfig`）で`CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ=80`・
   `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`を確認——チップrev v1.0
   （`!ESP_CHIP_REV_ABOVE(chiprev,101)`分岐）では，80MHzでも
   `SOC_CPU_CLK_SRC_PLL_F240M`・divider=3を使う（IDF-11064のroot clock
   ICG erratum対策）。`rtc_clk_cpu_freq_to_pll_240_mhz()`内で
   `rtc_clk_bbpll_enable()`（`PMU_IMM_HP_CK_POWER_REG`のtie-highパルス）
   ＋（`s_cur_pll_freq`が既に480MHz相当でなければ）
   `rtc_clk_bbpll_configure()`（regi2c I2C_BBPLLブロックへの480MHz
   目標書込み＋`regi2c_ctrl_ll_bbpll_calibration_start/is_done/stop`）
   を経由する——**未検証**。
4. **RC_FAST/slow clock source set**：`rtc_clk_8m_enable`・
   `rtc_clk_fast_src_set`・`rtc_clk_slow_src_set`。ASP3のDirect Bootは
   RTC_FAST/SLOWクロック源を一切使わない（WDT無効化のみ・sleepモード
   遷移を経験しない）ため機序的関連が薄いと判断し優先度を下げた
   （未検証のまま次点扱い）。

→ 4項目のうち3.（CPU周波数切替え）だけが，過去の網羅比較（実施14-31）
で一度もカバーされていない**真に新規の候補**と判定した。

### 2. 判別実験：CPUクロックソースの生JTAG読み（減算/加算いずれにも
先立つ，安価な事前確認）

advisorの指摘（「switching to PLLかどうかは実測すべき，192MHzという
既存値を鵜呑みにしない」）に従い，較正ハング状態のASP3（実施31終了
時点のflashそのまま）を対象に，PCRのSOC根クロックmux
（`PCR_SYSCLK_CONF_REG.soc_clk_sel`，`0x60096110`ビット[17:16]）と
ディバイダ（`PCR_CPU_FREQ_CONF_REG`/`PCR_AHB_FREQ_CONF_REG`）を
JTAG直読みした（`r32_clkcheck.py`，「単発halt→mdw→resume」，2/2独立
RTSリセット試行）：

- **ASP3（較正ハング状態）**：`PCR_SYSCLK_CONF_REG=0xb0000200`→
  `soc_clk_sel=0`（**SOC_CPU_CLK_SRC_XTAL**）・`PCR_CPU_FREQ_CONF_REG=
  0x00000000`（div=1）・`PCR_AHB_FREQ_CONF_REG=0x00000000`（div=1）
  （2/2一致）。
- **stock**（実施29 `r29_snapshot_boot1.dump.txt`，ESP-IDF標準ブート列
  完走後の状態）：同オフセットで`PCR_SYSCLK_CONF_REG=0xb0030200`→
  `soc_clk_sel=3`（**SOC_CPU_CLK_SRC_PLL_F240M**）。

**ASP3のPCR根クロックmuxはXTAL直結のまま一度も切り替わっていない**
（対してstockはPLL_F240Mへ切替え済み）——静的レジスタレベルで明確な
プラットフォーム差分。

副次的に，同じ読み取りで`I2C_ANA_MST_ANA_CONF0_REG`（`0x600AF818`，
実施14-31のどのMMIOスイープにも含まれていなかった新規ブロック）の
bit24（`I2C_MST_BBPLL_CAL_DONE`）を確認したところ，ASP3較正ハング
状態で**既に1**（2/2）——BBPLL自体はROMの時点で較正済みと判明した
（「BBPLLが一度も較正されていない」という別候補は本チェックだけで
反証できた）。

### 3. mcycle実測でのCPU周波数二点法再計測——実施03の「192MHz実機確定」
を反証，実際はXTAL48MHz直結と確定

2節の静的読みだけでは「レジスタの意味付けがヘッダと食い違っている
可能性」を排除できないため，実施03と同じ手法（mcycle CSR対SYSTIMER
16MHz基準の二点法：halt→読取→resume→sleep T→halt→再読取）を
`r32_cpufreq_recheck.py`として再実装し，現在の実機で再計測した：

- **ASP3較正ハング状態（WiFi有効ビルド，現flash）**：T=2.0s→
  47.9747MHz，T=4.0s→47.9958MHz（独立2回のRTSリセット試行）。
- **ASP3 WiFi無効ビルド（`build/c5_r31_test_porting`，実施31資産の
  再フラッシュ）**：T=2.0s→47.9606MHz——**WiFi有無に関わらずXTAL直結**
  （実施03の当時の測定条件＝WiFi無し，に対応する構成でも同じ結果）。
- **stock（`stock_scan`，陽性対照として`examples/wifi/scan`をscan完走
  前の時間窓で計測）**：T=0.5/1.0/1.0/2.0/3.0sの5試行で
  322.1/283.0/283.2/261.1/255.0MHzと，窓を広げるほど期待値240MHzへ
  収束する傾向を確認——これは測定法自体のバイアスではなく，**stockの
  ブート列自体が段階的に加速する**（bootloader段=80MHz→app段=240MHz）
  実際の二段階遷移を短い窓が拾ってしまうためと判明した（4節で追加
  確認）。この陽性対照により，計測手法自体（mcycle対SYSTIMER二点法）
  は高速なCPUクロックも正しく検出できることを確認した——ASP3側の
  一貫した約48.0MHz（窓を2〜8sへ広げても収束せず安定，5節参照）は
  測定バイアスではなく真の値と判断した。

**結論：実施03の「CPU=192MHz実機確定（=XTAL48MHz×4）」は反証された**
——実際のCPUクロックはXTAL48MHz直結（PLLの関与なし）であり，`192MHz
=XTAL×4`という積算は，`rtc_clk_cpu_freq_to_xtal()`（除算のみでXTALを
逓倍できない実装）とも整合しない机上の誤りだったか，あるいは実施03
以降に何らかの理由で後退したかのいずれかである。advisorの指摘に従い，
この経緯自体の追跡（29ラウンド分の回帰ハント）は本ラウンドでは行わず，
「較正キー(A)の実体＝CPUクロックがXTALのままPLLへ切り替わっていない
こと」という機序の確定と，実測値に基づく較正定数の訂正に注力した。

### 4. 加算移植：CPUクロックソースをXTALからPLL_F240M÷3へ明示的に切替え

上記2〜3節で「較正キー(A)候補＝CPUクロック切替えの欠落」が静的・動的
の両面で確定したため，task手順どおり**加算**（減算より安全側）で実装
した。`asp3/target/esp32c5_espidf/target_kernel_impl.c`に
`esp32c5_r32_cpu_clock_switch()`を新設し，`hardware_init_hook()`の
最初期（WDT無効化の直後・`esp_rom_set_cpu_ticks_per_us()`より前）で
**無条件に**（`TOPPERS_ESP32C5_WIFI`の有無に関わらず）呼び出す：

1. `PMU_IMM_HP_CK_POWER_REG`（`0x600B00CC`，WT型自己クリアレジスタ）へ
   `clk_ll_bbpll_enable()`相当のtie-highパルス
   （`GLOBAL_BBPLL_ICG`/`XPD_BB_I2C`/`XPD_BBPLL_I2C`/`XPD_BBPLL`）を
   無条件発行（安全：自己クリア・副作用なし）。
2. `I2C_ANA_MST_ANA_CONF0_REG`のCAL_DONEビットを確認し，**1（既に較正
   済み，2節で確認済み）ならスキップ**，0の場合のみタイムアウト付き
   （200万回上限）で較正パルス（`regi2c_ctrl_ll_bbpll_calibration_
   start/stop`相当）を発行するフォールバックを用意（rtc_clk.cの
   無条件`while(!done)`はASP3では採用しない安全設計）。
3. `PCR_CPU_FREQ_CONF_REG.cpu_div_num=2`（÷3）・
   `PCR_AHB_FREQ_CONF_REG.ahb_div_num=5`（÷6，`rtc_clk_cpu_freq_to_
   pll_240_mhz()`の固定値）を先に設定し，
   `PCR_SYSCLK_CONF_REG.soc_clk_sel=3`（PLL_F240M）へ切替え，
   `PCR_BUS_CLK_UPDATE_REG`（R/W/WTC型自己クリアトリガ）で確定させる
   （タイムアウト付き）——`rtc_clk_cpu_freq_set_config()`と同じ順序を
   保持。

レジスタ設定はstockの2nd-stageブートローダ
（`CONFIG_BOOTLOADER_CPU_CLK_FREQ_MHZ=80`）と完全に同一
（`soc_clk_sel=3`・`cpu_div=3`・`ahb_div=6`）である。

### 5. 想定外の結果：レジスタ設定は意図通りだが実測CPU周波数は96MHz
（期待値80MHzと不一致）——BBPLL実周波数の推定誤りと判断し実測値で校正

`build/c5_r31_test_porting`（WiFi無効，安全側で先に検証）へ実装・
ビルド・書込みし，`test_porting`が引き続き`# 6/6 passed`
（8秒キャプチャ内3回連続起動，全て成功——退行なし）であることを
確認した上で，JTAG実読で切替え後のレジスタを確認した
（`r32_afterport_clkcheck_try1`）：

- `PCR_SYSCLK_CONF_REG=0xb0030200`（`soc_clk_sel=3`＝stockの完走後の値
  と完全一致）・`PCR_CPU_FREQ_CONF_REG=0x00000002`（div=3）・
  `PCR_AHB_FREQ_CONF_REG=0x00000005`（div=6）——**レジスタ設定は意図
  通り**。

しかし`r32_cpufreq_recheck.py`でmcycle二点法により実測したところ：

- T=2.0s×2試行→96.01/96.00MHz，T=6.0s→95.98MHz，T=8.0s→95.97MHz——
  **窓を2秒から8秒まで広げても収束せず一貫して約96.0MHz**（3節で
  確認したstockの「短窓バイアス→収束」パターンとは異なり，ASP3側は
  単一の安定したクロック体制へ即座に切り替わっているため，測定
  バイアスではなく真の値と判断できる）。理論値80MHz（=240MHz÷3）とは
  一致しない。

**原因の推定**：本移植は`rtc_clk_bbpll_configure()`相当のBBPLL周波数
設定（regi2c I2C_BBPLLブロックへの目標480MHz書込み）を実行せず，
ROMが既に較正済みのBBPLL（2節でCAL_DONE=1を確認）をそのまま流用した
——ROMが実際にロックした周波数がESP-IDFの前提する480MHzとは異なる
可能性が高い（実測96MHzから逆算すると"PLL_F240M"ネット＝288MHz相当，
BBPLL本体は576MHz相当と推定される）。BBPLLの実際の周波数設定を
regi2c経由で修正する（リスク増）よりも，**実測値をそのまま正として
較正定数を校正する**方針を採った（advisorの「実測を信頼する・回帰
ハントにしない」指針を踏襲）。

`asp3/arch/riscv_gcc/esp32c5/esp32c5.h`を実測96MHzへ更新した：
`CORE_CLK_MHZ` 192→**96**（実施32実測確定），`SIL_DLY_TIM1`/
`SIL_DLY_TIM2` 20→**42**（4サイクル/96MHz=41.67nsを切上げ，実施03と
同じ「1反復=4サイクル」という周波数非依存のマイクロアーキテクチャ
定数からの机上比例外挿——**JTAG hw-bp注入による実機再較正（実施03
§3と同じ手法）は本ラウンドでは実施していない**，次段への申し送り）。
更新後，再ビルド・再書込みし，`test_porting`が引き続き`# 6/6 passed`
（3回連続起動）であることを再確認した。

### 6. 決定実験：WiFiビルドで冷間Direct Boot較正——**PASS確定**（独立2回
のRTSリセット試行＋1キャプチャ内5回の自然WDTリブートすべてで再現）

`build/c5_idf61_uart`（標準WiFi有効ビルド，実施31までと同一構成＋本
ラウンドのクロック切替え・較正定数更新のみ）を再ビルド・実機書込みし，
UARTブリッジRTSリセット後の出力を観測した（判定基準は実施26以降と
同一：`raw_adc`非ゼロかつブート毎に変動＝生信号，`done16`の0→1遷移を
PASS，恒久ゼロをFAIL）。

**結果（`r32_wifi_trial1.log`：20秒キャプチャ，自然WDTリブート5回・
`r32_wifi_trial2.log`：独立RTSリセット，10秒キャプチャで2回）**：

```
wifi_diag: raw_adc=0x00000000 done16=0 ...                 ← esp_wifi_init前（想定どおりゼロ）
...
wifi_scan: esp_wifi_init
esp_shim: task 'wifi' -> tskid 1 (prio 23)
wifi_diag_live: raw_adc=0x095a0968 done16=1 ...             ← esp_wifi_init後：較正完走
wifi_scan: esp_wifi_init -> 0
wifi_scan: esp_wifi_start -> 0
wifi_scan: DIAG set_promiscuous_rx_cb -> 0
wifi_scan: DIAG set_promiscuous(true) -> 0
wifi_diag: raw_adc=0x095a0968 done16=1 ...                  ← 較正値は安定して保持
```

7回の独立起動全て（`r32_wifi_trial1.log`の5回＋`r32_wifi_trial2.log`
の2回）で`raw_adc`が非ゼロかつ起動毎に変動（`0x095a0968`/`0x09640968`/
`0x09620966`/`0x095e095c`/`0x095a0964`/`0x09680972`/`0x096a0972`/
`0x096e096e`——実施26/27/29と同型の生信号シグネチャ）・`done16`は
`esp_wifi_init`前0→後1で再現した。**較正キー(A)＝CPUクロック切替えの
欠落は確定的に解消した**——25ラウンド以上（実施11〜31）追跡してきた
「トーン自己ループバック測定の生ADCサンプルがASP3のみ恒久ハードゼロ」
という壁は，本ラウンドで解消した。

### 7. (B)scan/RXの現状：新しい壁——`set_promiscuous(true)`成功後・
`esp_wifi_scan_start`の戻り印字前でハングし，WDTリブートに至る

較正PASS後もscanには到達しなかった。UARTログを精査すると，`wifi_scan:
DIAG set_promiscuous(true) -> 0`まで印字された後，`wifi_scan: esp_wifi_
scan_start`（呼出し直後に印字される既知のログ行）が一度も現れないまま
`rst:0x12 (RTC_SWDT_SYS)`によるリブートに至っている（7回の独立起動
全てで同一パターン，`r32_wifi_trial1.log`/`r32_wifi_trial2.log`で
`grep -a "APs found|esp_wifi_scan_start|RESCAN"`が空であることを確認
済み）——`esp_wifi_scan_start()`の内部（戻る前）でハングしていると
判定できる。

これは実施27で見つかった「promiscuous直後の全割込み凍結（CLIC
`mintstatus.mil`固着）」と表面的に似た症状域だが，**実施28でその凍結
自体は解消済み**（`irc_begin_int`のsynthetic mret化，`docs/c5-bringup.
md`実施28）であり，本ラウンドで新たに導入したCPUクロック切替え
（XTAL→PLL_F240M÷3，実測96MHz）が，実施28の修正が前提としていた
タイミング関係（CLICの割込み出口処理とmretのタイミング）を変化させ，
別の形で類似の症状を再発させている可能性がある——**未確認・未診断**
（読取り専用のJTAG PCサンプル診断を試みたが，本ラウンドの残り時間内に
OpenOCD接続の問題で完了できなかった）。

(B)（`esp_clk_init()`相当のさらなる240MHzへの昇格＋`esp_perip_clk_
init`相当）は，本ラウンドでは着手していない——`esp32c5_r32_cpu_clock_
switch()`はbootloader相当の設定（PLL_F240M÷3）に留めており，app側の
さらなる昇格（÷1＝240MHz相当，実測なら約288MHz相当になると予想され
安全域を超える懸念がある，5節参照）は意図的に見送った。

### 8. test_porting回帰：`# 6/6 passed`（3回連続起動，退行なし）

`build/c5_r31_test_porting`（WiFi無効，実施31資産の再利用）で，
esp32c5.hの較正定数更新前後それぞれでビルド・実機書込みし，8秒
キャプチャ内でいずれも3回連続起動を観測しすべて`# 6/6 passed`
（`r32_port_testporting_uart.log`／`r32_finalconst_testporting_uart.
log`）。CPUクロックが48MHz→96MHzへ変化したにも関わらずカーネル基本
機能（syslog・tick timer・task生成/起動・semaphore・eventflag・
alarm/CLIC割込み配送）は全て正常——退行なし。

### 9. C5#1最終状態（終了処理）

本ラウンドの成果物である`build/c5_idf61_uart/asp_flash.bin`
（実施32のクロック切替え＋較正定数更新込み，実施31までの全機能を
包含）を最終状態としてそのまま維持した（手順5「最新ASP3ビルドで
書き戻し」に該当——本ラウンドの新規ビルド自体が最新版のため追加の
書き戻しは不要）。RTSリセット後12秒キャプチャで最終確認
（`r32_final_state_console.log`）：`raw_adc=0x0974097a done16=1`
（較正PASS，6節と同型），`set_promiscuous(true) -> 0`まで到達し
その後`rst:0x12 (RTC_SWDT_SYS)`——7節の新しい壁のまま，退行・改善
双方なく安定。

C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触（前ラウンド
から変更なし）。DUTは常に`b04e3bcf...`（ttyUSB0）をby-id照合の上使用。

### 10. 交絡の分離実験（advisor指摘への対応）：クロック切替え自体が
較正の必要条件であり，delay較正の訂正だけでは不十分と確定

6節までの加算実験は，(i) CPUクロックをXTAL(48MHz)からPLL(実測96MHz)へ
切替えることと，(ii) `esp_rom_set_cpu_ticks_per_us()`をCORE_CLK_MHZの
新しい値（96）で正しく再較正することを**同時に**行っていた。advisorの
指摘（rigor doc「recurrence 6」と同型：「変更Xをしたら症状が動いた」を
Xの内訳を分離せずに単一原因へ帰属させていないか）に従い，2つの効果を
切り分ける対照実験を実施した。

**対照実験の設計**：`esp32c5_r32_cpu_clock_switch()`の呼出しを一時的に
無効化し（CPUはXTAL48MHzのまま），`esp_rom_set_cpu_ticks_per_us()`の
引数だけを実速度に合わせた正しい値（`48`，ハードコード）へ変更。
`SIL_DLY_TIM1`/`SIL_DLY_TIM2`も一時的に48MHz相当（4cyc/48MHz=83.33ns
→切上げ84）へ変更し，ASP3自身のsil_dly_nse系delayもXTAL速度に対して
正しくなるようにした——**クロックはXTALのまま，delay較正だけを完全に
正す**という条件。

**結果（独立2回のRTSリセット試行，各キャプチャ内の自然WDTリブート
計5回を含む，`r32_control_trial1.log`/`r32_control_trial2.log`）**：
**すべてFAIL**（`raw_adc=0x00000000 done16=0`が恒久的に継続，較正が
一度も成立しなかった）。

**判定**：delay較正（`esp_rom_set_cpu_ticks_per_us`とSIL_DLY）を実速度
に対して完全に正しく設定しても，CPUがXTALのままでは較正はPASSしない
——**PLLへのクロック切替え自体が較正キー(A)の必要条件**であることが
確定した。「変更前は`esp_rom_set_cpu_ticks_per_us(192)`という誤った
較正値のまま4倍遅く走っていたので，PHY blob内部のdelayが実質4倍長く
なっていたことが真因で，クロック切替えは付随的だったのではないか」
という代替仮説（advisor提起）は，本実験により**反証**された。

この結果を受け，対照実験用の一時的な変更（クロック切替え無効化・
ticks_per_us=48固定・SIL_DLY=84）は**revert**し，6節の本採用構成
（クロック切替え有効・`CORE_CLK_MHZ=96`・`SIL_DLY_TIM1/TIM2=42`・
`esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ)`）へ戻して再ビルド・
再書込みし，較正PASSが再現することを最終確認した
（`r32_finalfix_confirm.log`：`raw_adc=0x0964095a done16=1`）。
`test_porting`も同構成で再度`# 6/6 passed`（3回連続起動，
`r32_final_testporting_uart.log`）を確認済み。

### 変更ファイル

- `asp3/arch/riscv_gcc/esp32c5/esp32c5.h`：`CORE_CLK_MHZ` 192→96・
  `SIL_DLY_TIM1`/`SIL_DLY_TIM2` 20→42（実施32実測確定＋机上比例外挿，
  コメントに実施03からの訂正経緯を明記）。
- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r32_cpu_clock_switch()`（新設，CPUクロックXTAL→PLL_F240M÷3
  切替え，`PMU_IMM_HP_CK_POWER_REG`/`I2C_ANA_MST_ANA_CONF0_REG`/
  `PCR_SYSCLK_CONF_REG`/`PCR_CPU_FREQ_CONF_REG`/`PCR_AHB_FREQ_CONF_REG`/
  `PCR_BUS_CLK_UPDATE_REG`を使用）を新設し，`hardware_init_hook()`の
  最初期（WiFi有無に関わらず無条件）から呼出し。
- `docs/c5-bringup.md`：本節（実施32）追加。
- スクラッチ（リポジトリ外，`/tmp/.../scratchpad/`）：`r32_clkcheck.py`
  （新規，PCR/I2C_ANA_MST/PMU_IMM_HP_CK_POWERのJTAG読取り）・
  `r32_cpufreq_recheck.py`（新規，mcycle対SYSTIMER二点法によるCPU
  周波数再計測）・`r32_pcsample.py`（新規，7節の診断用，本ラウンドは
  未完走）・`r32_asp3_clk_try1/2.log`（2節）・
  `r32_cpufreq_try1/2.log`／`r32_cpufreq_noWiFi_try1.log`／
  `r32_cpufreq_stockscan_try1〜5.log`（3節）・
  `r32_afterport_clkcheck_try1.log`（5節）・
  `r32_cpufreq_afterport_noWiFi_try1/2.log`／
  `r32_cpufreq_afterport_long_try1/2.log`（5節）・
  `r32_port_testporting_uart.log`／`r32_finalconst_testporting_uart.
  log`（8節）・`r32_wifi_trial1.log`／`r32_wifi_trial2.log`（6節）・
  `r32_final_state_console.log`（9節）・`r32_precheck_uart.log`
  （ラウンド開始時のベースライン再確認）・`r32_control_trial1/2.log`
  （10節，交絡分離実験）・`r32_finalfix_confirm.log`／
  `r32_final_testporting_uart.log`（10節，revert後の最終再確認）。
  stockの陽性対照は実施15/29資産（`stock_scan/`）を再利用。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

0. **（確認済み・再検証不要）較正キー(A)の因果帰属**：10節の対照実験に
   より，「クロック切替え」と「delay較正の訂正」を分離した上で，
   クロック切替え自体が較正PASSの必要条件であることを確定した
   （delay較正だけを正しくしてもXTALのままではFAIL，独立2回）。
   「実はdelay較正の誤りが真因で，クロック切替えは付随的だった」と
   いう代替解釈は反証済み——今後この点を再度切り分け直す必要はない。
1. **最優先＝7節の新しい壁（`esp_wifi_scan_start`内ハング）の診断**。
   実施27/28型（CLIC mil固着）の再発かどうかをJTAG PCサンプルで確認
   すること——本ラウンドで用意した`r32_pcsample.py`（未完走，OpenOCD
   接続問題）をまず動かすか，実施27の手法（割込みリング計装）を再利用
   するのが早い。クロック切替えのタイミング変化が実施28の修正の前提
   （synthetic mretのタイミング）を崩している可能性がある。
2. **BBPLL実周波数の解明**：実測96MHzがなぜ理論値80MHzと一致しないか
   （5節）は未解明のまま。`rtc_clk_bbpll_configure()`相当の完全な
   regi2c設定（`clk_ll_bbpll_set_config()`，480MHz目標値をXTAL周波数
   から計算しI2C_BBPLLブロックへ書込み）を実装すれば理論値へ近づく
   可能性がある——ただしregi2c書込みのリスク増を伴うため，7節の壁が
   解消してから着手する方が優先度として妥当。
3. **SIL_DLY_TIM1/TIM2=42の実機再較正**：現在は机上比例外挿（未実測）
   のまま。実施03 §3と同じhw-bp注入による二点法（`sil_dly_nse`への
   pc=エントリ・a0=N・retにbp）で改めて実機較正することを推奨する
   （task手順2・5節の申し送りどおり）。
4. **(B)（`esp_clk_init()`相当のさらなる240MHzへの昇格＋
   `esp_perip_clk_init`相当）は未着手のまま**。7節の壁が解消してから，
   実施30で特定済みの設計（`rtc_clk_cpu_freq_set_config()`によるさらに
   上のCPU周波数への切替え）を検討する。ただし5節の実測結果を踏まえ，
   「÷1＝240MHz相当」を単純に指定すると実際には約288MHz相当になる
   懸念があるため，BBPLL実周波数（2の申し送り）を先に解明してから
   昇格幅を決める方が安全。
5. **実施03の「CPU=192MHz実機確定」反証の扱い**：本ラウンドでは
   advisor指摘に従い経緯の追跡（regression hunt）を行わなかった。
   ユーザーが必要と判断すれば，実施04〜30の各ビルドを遡ってCPU周波数を
   再測定し，いつ・どの変更で48MHzへ後退した（または実施03の測定自体
   が誤りだった）かを特定できる——ただし優先度は7節の壁より低いと
   判断する。

---

## 実施33：precheckで(i)CLIC mil固着の再燃を確定的に反証（mintstatus=0終始・hrtcnt実時間進行・PC分布はidle支配）——約8秒周期のSUPER_WDT_RESET（rst:0x12）というhang構造そのものを発見・**真因＝LP_WDT_SWD_WPROTECTの解錠キー誤り**（`0x8F1D312A`は誤記，hal `lp_wdt_reg.h`確認済み正値`0x50D83AA1`。実機確認待ちのまま25ラウンド放置されていた既知の罠が実際に発火していた）と特定・修正——**scanハングは解消**（冷間Direct Bootで`esp_wifi_scan_start -> 0`到達・約11秒の実スキャン完走を2/2再現，WDTリセット0件）。ただし**新しい壁＝0 APs found**（近隣に強信号AP多数を確認済みの環境で0件——deaf-RX類縁の新規課題，未着手）。test_porting 6/6維持

### 背景・目的

実施32の申し送り：(B)scan/RXの壁＝`set_promiscuous(true)`成功後・
`esp_wifi_scan_start`の戻り印字前で無条件WDTリブートに至るハング。
疑い筋は(i)CLIC mil固着の再燃／(ii)(iii)クロック起因のタイマ・
ペリフェラル不整合／(iv)純粋な別バグ，の4択。本ラウンドはprecheckで
大分類した上で，可能なら修正し，冷間Direct Bootでのscan成功（AP検出）
を狙う。

### 1. 準備：診断リング計装の記録位置修正（精査レポート§5-#8の反映）

`docs/c5-clic-exit-fix-review.md`§5-#8の指摘（`ESP32C5_CLIC_DEBUG_RING`の
begin側記録がsynthetic mretの**後**にあるため，記録されるmepc/mintstatus
が常に降格後の値になり，実施28型の凍結特定に使えない）を先に修正した。
`chip_support.S`の`irc_begin_int`で，begin側リング記録ブロックを
synthetic mret（`la t1, irc_begin_int_demote`以降）の**前**（mintthresh
昇格直後の`fence`の後）へ移動。加えて精査レポート§5-#6/#4/#10で指摘
された3つの保守制約（窓内フォールト命令追加禁止・mcause無保存との
隠れ結合・mnxti非互換）をコメントとして明文化した。**non-ring（既定）
ビルドへの影響はゼロ**であることをobjdump比較で確認済み（後述2節）
——`#ifdef ESP32C5_CLIC_DEBUG_RING`外のコードは1バイトも変更していない。

### 2. precheck：(i)CLIC mil固着の再燃を確定的に反証

`build/c5_r33_ring`（`ASP3_EXTRA_COMPILE_DEFS=ESP32C5_CLIC_DEBUG_RING`）
と`build/c5_r33_std`（無計装，1節のchip_support.S修正のみ）の2種類を
ビルドし，`build/c5_idf61_uart`（実施32の実際のflashバイナリ，未変更）
とobjdump比較したところ，`_kernel_irc_begin_int`/`_kernel_irc_end_int`の
機械語列は**完全に同一**（1節の変更は診断コード内に閉じている）ことを
確認した。

冷間Direct Boot後，`set_promiscuous(true)`成功直後からハング中に
`r33_pcsample2.py`（JTAG：halt→`reg pc/ra/mcause/csr_mintstatus`＋
`_kernel_current_hrtcnt`読出し→resume→0.4s→繰返し）で複数点サンプルを
採取した（`r33_pc_try3_a1〜a3.log`，独立3試行×各8サンプル）：

- **`csr_mintstatus`は全サンプルで`0x00000000`**（mil=0）——固着なし。
- **`_kernel_current_hrtcnt`は壁時計とほぼ1:1で進行**（サンプル間隔
  ~0.6sに対しhrtcnt増分もほぼ同量）——凍結していない。
- PCの分布：大半は`dispatcher_2`（idleループ）,一部
  `core_int_entry_2+4`（ra=`dly_tsk+0x82`＝dly_tsk実行中に割込み受付）,
  一度だけ`phy_pwdet_tone_start`内の busy-wait ループ（後述4節）。

**判定：(i)CLIC mil固着の再燃は反証**。全割込みが正常に配送され続けて
おり，カーネル時刻も実時間で進行している——「凍結」ではなく，別の
機構によるハングである。

### 3. hangの真の構造：ソフトハングではなく約8秒（96MHz化後は約3.5〜4秒）
周期のハードウェアWDTリセットループだった

`r33_precheck_uart.log`（ringビルド，冷間ブート25秒キャプチャ）で，
`set_promiscuous(true) -> 0`直後から`no time event is processed in
hrt interrupt.`（asp3_core `kernel/time_event.c:625`のLOG_NOTICE．
HRT割込みハンドラが呼ばれたが処理すべきタイムイベントが無かった場合に
出る，**正常系のメッセージ**——`asp3_core/docs/dev/esp32c6-target.md`
でC6実機検証時に「SYSTIMER割込みが正しく・継続的に配送されている
証拠」と明記されている）が多数出力され続け，その後`rst:0x12
(RTC_SWDT_SYS)`（ROMのreset_reason=18=`SUPER_WDT_RESET`．hal
`esp32c5/rom/rtc.h`で確認）でリブートし，同じ症状で再ブートし続ける
（周期ループ）ことを確認した。

★このメッセージは実施32のログ（`r32_wifi_trial1/2.log`）には一切
現れていなかった（grep 0件）。しかし2節のobjdump比較で機械語が
同一と確認済みであり，かつ`build/c5_idf61_uart`（実施32のバイナリ
そのもの，一切未変更）を**そのまま再flash**して再確認したところ
（`r33_orig32_recheck.log`），同じ`rst:0x12`周期ループが再現した
（メッセージの有無だけが異なる）。これは`memory/feedback_hardware_
investigation_rigor.md`が警告する「ASP3のsyslogにはburst-loss既知
バグがある」ことと整合する——**実際の症状（周期的WDTリセット）は
実施32時点から一貫して同一だったと判断**し，メッセージ出現の有無を
別の症状として扱わない（advisor指摘に従い，見かけの差分を追わず
実体の症状に絞った）。

PC分布が主にidle（2節）であることと合わせ，**「ソフトウェアの無限
ハング」ではなく，何らかのウォッチドッグが周期的に発火してリセット
している**という構造が本ラウンドの一次発見である。

### 4. 途中で発見した副次事項（因果棄却）：`phy_pwdet_tone_start`の
busy-waitは無限ループではなかった

2節の1回のサンプルでPCが`phy_pwdet_tone_start`（APB_SARADCおよび
MODEM0レジスタを触るblob関数．PCR bit18セット→MODEM0+0x808の
リセットパルス→2μs待ち→MODEM0+0x80cのbit[16:14]が0x7になるまで
busy-wait，**タイムアウト無し**）内の busy-wait ループ命令アドレス
（`phy_pwdet_tone_start+0x54`）で一致した。「タイムアウト無し
busy-wait」は過去25ラウンドの`phy_iq_est_enable_new`等と同型の危険
パターンに見えたため追跡したが，`r33_reg808c.py`で同一タイミング帯
（RTSリセット後+4〜+10秒）を再サンプルしたところ，**MODEM0+0x80cの
bits[16:14]は既に0x7に到達しPCはidleへ復帰していた**（10サンプル
全て）——この関数のbusy-waitは正常に完了していると判明し，本ラウンド
の主要因ではないと**因果棄却**した（scan中のper-channel PHYキャリブ
レーションの一部と推定，正常動作）。

### 5. advisorとの合流：「無限ハング」ではなく「WDTが正常に動いている
scanを殺している」可能性への転換

2〜4節の材料（mil=0，hrtcnt進行，PC=idle支配，WDTリセットが周期的）を
advisorに提示したところ，「システムは生きて待っている状態であり，
scanが実際には進行しているが8秒のSuper-WDTより長くかかりリブートに
巻き込まれている可能性が高い」という再解釈が示された。決定実験として
提案されたのは「scan_start直前で到達可能な全WDT（TIMG0/1・RWDT・
SWD）を無効化し直し，60秒自由走行させて進行/完了を見る」。

### 6. 決定実験1回目：disable再アサート（診断）——効果なし

`target_kernel_impl.c`に`esp32c5_reassert_wdt_disable()`を新設し
（既存の無効化列を関数化），`apps/wifi_scan/wifi_scan.c`の
`set_promiscuous(true)`直後・`esp_wifi_scan_start()`直前・毎秒の
待ちループ内の計3箇所から呼び出す診断ビルドを作成した。60秒キャプチャ
（`r33_wdtfix_boot1.log`）でも**周期的な`rst:0x12`は変わらず継続**
（16回/60秒）——disable再アサートだけでは症状不変だった。

### 7. 決定実験2回目：disable＋FEED（能動フィード）——それでも効果なし

「disableビットの書込みが実際には効いていない可能性」（JTAG読出しでは
DISABLE=1に見えても，実行時挙動と食い違う——5節のadvisor指摘）を
踏まえ，TIMG0/1・RWDT・SWDそれぞれの明示的FEED（WT型カウンタリセット
ビット）も追加で叩く版を実装・実機投入したが，**やはり`rst:0x12`は
変わらず継続**（`r33_wdtfeed_boot1.log`，16回/60秒，6節と同一パターン）。
この時点で「頻度・タイミングの問題ではなく，そもそも無効化/フィード
自体が届いていない」という仮説へ転換した。

### 8. 根本原因の特定：`ESP32C5_LP_WDT_SWD_WKEY`の解錠キー誤記

hal `hal/components/soc/esp32c5/register/soc/lp_wdt_reg.h`の
`LP_WDT_SWD_WPROTECT_REG`フィールドコメントを直接確認したところ：

```
/** LP_WDT_SWD_WKEY : R/W; bitpos: [31:0]; default: 0;
 *  Configure this field to lock or unlock SWD`s configuration registers.
 *  0x50D83AA1: unlock the RWDT configuration registers.
 *  ...
 */
```

（コメント文自体はRWDT用の説明文がコピペされたもの——Espressif純正
ヘッダ側の軽微な記述バグ——だが，**キー値`0x50D83AA1`はTIMG/RWDTと
共通**であることが読み取れる）。一方，`asp3/arch/riscv_gcc/esp32c5/
esp32c5.h`の`ESP32C5_LP_WDT_SWD_WKEY`は**`0x8F1D312A`**という別の値
を使っていた——コード内コメントが最初から「【未確認・暫定値】C3/C6
実績の転用であり，ヘッダに正しく記載されていない可能性がある（C6の
esp32c6.hに実際に誤記があった前例あり）。実機で解錠成功を必ず確認
すること」と警告していた，まさにその罠が現実に発火していたと確定
した。

誤ったキーでは`LP_WDT_SWD_WPROTECT_REG`の解錠に失敗し，後続の
`LP_WDT_SWD_CONFIG_REG`へのDISABLEビット書込みが書込み保護によって
**無視される**（実行時は一度もSWDが真に無効化されていなかった）。
2節でJTAG読出しにDISABLE=1が見えていた点は，「JTAG haltがWDTの
カウント進行を一時停止させる副作用（r21ハーネスの『SWD無効化burst』
と同型）により，無効化されていない実行時の実際の挙動とhalt中の読出し
値が一致しなかった」という説明を試みたが，**この点はadvisor査読で
未解決と指摘された**——書込み保護でブロックされたなら，CONFIGビット
自体はPOR既定値0のまま読めるはずで，「カウンタが止まる」ことと
「設定ビットの読出し値」は別の話である。この細部の機構は**未確定
のまま**とし，本節で確定しているのは「SWDの解錠キーを正しい値へ
訂正した」という**変更点**と，9節のA/B実験（キー訂正**のみ**を単一
変数として切替え，disable/feed呼出し箇所は同一のまま症状が消えた
ことを確認）による**因果の実証**であり，「保護がどう見え方に影響
したか」という正確な内部機構の解明は今回の修正の正しさには影響しない
（advisor指摘のとおり，追加調査の優先度は低い）。

### 9. 修正と決定実験：単一変数A/B（キー訂正のみ）で因果確認，さらに
最小修正版（reassert呼出し全廃）でも解消を確認（2/2再現）

**単一変数A/B**：7節の構成（誤キーのまま，`wifi_scan.c`側3箇所＋
`hardware_init_hook()`のdisable+feed拡張）から，**`ESP32C5_LP_WDT_
SWD_WKEY`の値だけ**を`0x8F1D312A`→`0x50D83AA1`へ変更し，他は一切
変更せずに再ビルド・実機投入した（`r33_flash_keyfix.log`）。結果
（`r33_keyfix_boot1.log`/`r33_keyfix_boot2.log`，独立2回）：**`rst:
0x12`が0回**（POR以外のリセットなし）・`esp_wifi_scan_start -> 0`
到達・約11秒のscan完走。7節（誤キー，`rst:0x12`が16回/60秒）との
唯一の差分がキー値であることから，**キー訂正が原因であることを
単一変数の実験で確認した**。

続いて，`apps/wifi_scan/wifi_scan.c`側の診断的reassert呼出し3箇所
（6〜7節で追加したもの）が最小修正でも不要か検証するため，これらを
**revert**し，`hardware_init_hook()`一箇所（起動最初期の1回きりの
無効化）だけを正しいキーで実行する最小構成でビルド・実機投入した。

**結果（`r33_minimal_boot1.log`/`r33_minimal_boot2.log`，独立2回の
RTSリセット試行，各45秒キャプチャ）**：

```
wifi_scan: DIAG set_promiscuous(true) -> 0
（no time event spamなし，またはあっても正常範囲）
wifi_scan: esp_wifi_scan_start -> 0
wifi_scan: 0 APs found (err=0)
```

**`rst:0x12`は0回**（POR以外のリセットなし，2/2）。`esp_wifi_scan_
start()`の戻り値印字に**本調査で初めて到達**し，約11秒の実スキャン
（チャンネル走査の実時間相当）を完走した。**scanハングは解消した**
——起動時1回のWDT無効化（正しいキー）だけで十分であり，6〜7節の
scan経路への追加呼出しは不要と判明した（最小修正の原則どおり，
target_kernel_impl.cには関数として残置——保険的なFEED拡張は無害な
まま，将来キーが再び誤っていた場合の保険として残す）。

### 10. 新しい壁：0 APs found（deaf-RX類縁，未着手）

scanは完走したが，**検出AP数は0件**（2/2とも）。host機（同一室内）の
`nmcli device wifi list`で確認したところ，信号強度84・62・59等の強い
近隣APが多数存在しており（同室内・至近距離），**環境要因（周辺に
APが無い）ではない**——真のRX/検出失敗と判定できる。promiscuous
モードでの3秒間観測でも`promisc_rx_count=0`（1パケットも受信して
いない）。scanの所要時間（約11秒）は瞬時ではなく，チャンネル走査
自体は実時間で進行していたと見られる（OSIRATE計装のsemTake/qRecv/
qSend/qSendISR/timerArmは全て0のままだったが，これらは特定の5箇所の
OSI呼出しのみを数える計装であり，scanが使う別経路を捕捉していない
可能性がある——未確認）。**C6のdeaf-RX（85ラウンドの調査）と表面的に
類似する新規課題**だが，本ラウンドでは未着手（時間配分の都合，
手順3の「区切りを固めて申し送る」に従う）。C6線への安易な統合は
時期尚早（rigor docの教訓どおり）。

★advisor査読で指摘された**有力な代替仮説（次段の最優先候補）**：
「0 APs」はC6型の未知deaf-RXではなく，実施32から持ち越しの**BBPLL
未較正周波数（実測96MHz，理論80MHz）の続き**である可能性が高い。
根拠——(a)実施29：クロックが正しく確立された時点（stockの
`app_main`先頭でのハンドオフ）でジャンプすると，ASP3自身のscanが
20〜25AP検出まで完走した実績がある。(b)実施30：scan/RXの鍵（B）は
`esp_clk_init()`（CPU周波数切替え）と特定されていた。(c)実施32：
ASP3 Direct BootのCPU周波数切替えは実装したが，BBPLL自体は
ROMの未較正値（理論480MHzに対し実測相当576MHz程度と推定）のまま
流用しており，理論80MHzに対し実測96MHzという不一致が残ったまま
だった。——**「較正キー(A)＝クロック切替えの欠落」は解消したが
「BBPLL実周波数の不一致」は解消していない**という実施32の申し送り
がそのまま，0 APsという形で顕在化した可能性がある。次段では，
実施26/27/29のクロスカーネル・ハンドオフ（stockの正しいクロック
確立状態からASP3へジャンプ）上でscanのAP検出を確認することを
**クロック仮説の判別実験として最優先**で行うべき（AP検出できれば
BBPLL不一致が0 APsの主因と確定，できなければ別要因を探る）。

### 11. test_porting回帰：`# 6/6 passed`（独立2ブート）

`build/c5_r33_tp`（WiFi無効，usbjtag→uart0コンソール，9節の最小修正
構成を含む現HEAD）で，独立2回のRTSリセット試行それぞれ12秒キャプチャ
にて`# 6/6 passed`（`r33_tp_boot1.log`/`r33_tp_boot2.log`）——退行なし。

### 12. C5#1最終状態（終了処理）

`build/c5_r33_std`（wifi_scanアプリ，9節の最小修正構成，フル4MB@0x0）
を最終flashとして書き戻した（`r33_flash_final.log`）。RTSリセット後
45秒キャプチャで最終確認（`r33_final_state_console.log`）：POR以外の
`rst:`は0回，`set_promiscuous(true) -> 0`→`esp_wifi_scan_start -> 0`
→`0 APs found (err=0)`——9〜10節と同型（scan完走・0 AP検出・退行
なし）。

C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触（前ラウンド
から変更なし）。DUTは常に`b04e3bcf...`（ttyUSB0，MAC照合済み）／JTAG
は`D0:CF:13:F0:A7:44`（ttyACM1，MAC照合済み）を使用。

### 変更ファイル

- `asp3/arch/riscv_gcc/esp32c5/esp32c5.h`：`ESP32C5_LP_WDT_SWD_WKEY`
  `0x8F1D312A`→**`0x50D83AA1`**（実施33で根本原因と確定・修正）。
  `ESP32C5_TIMG_WDTFEED`/`ESP32C5_LP_WDT_FEED`/
  `ESP32C5_LP_WDT_SWD_FEED_BIT`マクロを新設（保険用FEED拡張のため）。
  `ESP32C5_LP_WDT_WDT_WKEY`のコメントを「実機確認待ち」から「実施33
  確認済み」へ更新。
- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `hardware_init_hook()`内のWDT無効化列を`esp32c5_reassert_wdt_
  disable()`として関数化・export（起動時に1回呼ぶのみ．正しいキーで
  disableのみで十分と確認済み）。各WDTの明示的FEEDも追加済み（保険，
  無害）。
- `asp3/arch/riscv_gcc/esp32c5/chip_support.S`：`ESP32C5_CLIC_DEBUG_
  RING`のbegin側リング記録をsynthetic mretの前へ移動（`docs/c5-clic-
  exit-fix-review.md`§5-#8の反映）＋保守制約3点（§5-#6/#4/#10）を
  コメントとして明文化。non-ringビルドへの影響ゼロ（objdump確認済み）。
- `apps/wifi_scan/wifi_scan.c`：診断的に追加した`esp32c5_reassert_
  wdt_disable()`呼出し3箇所は，9節の検証後**revert**（最小修正の
  ため不要と判明）——最終的な差分なし。
- `docs/c5-bringup.md`：本節（実施33）追加。
- スクラッチ（リポジトリ外）：`r33_pcsample2.py`／`r33_reg808c.py`／
  `r33_swdcheck.py`（新規，JTAG診断）。`r33_pc_try*.log`（2節）・
  `r33_precheck_uart.log`／`r33_orig32_recheck.log`（3節）・
  `r33_reg808c_try1.log`（4節）・`r33_wdtfix_boot1.log`（6節）・
  `r33_wdtfeed_boot1.log`（7節）・`r33_keyfix_boot1/2.log`（9節の
  reassert残置版）・`r33_minimal_boot1/2.log`（9節の最終最小構成）・
  `r33_tp_boot1/2.log`（11節）・`r33_final_state_console.log`（12節）。
  ビルド：`build/c5_r33_ring`（診断用リング計装）・`build/c5_r33_std`
  （最終wifi_scan構成）・`build/c5_r33_tp`（test_porting回帰）。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

1. **最優先＝10節の新しい壁「0 APs found」の診断——BBPLL/クロック
   仮説（実施32からの持ち越し）を筆頭候補として判別実験から着手
   すること**（advisor査読で指摘・10節に根拠を記載）。「deaf-RXの
   入口」に見えるが，C6型の未知の新規原因と決めつけず，まず実施
   26/27/29のクロスカーネル・ハンドオフ（stockが確立した正しい
   クロック状態からASP3へジャンプ）上でscanのAP検出を確認する
   ことを**最初の一手**とする——実施29はこの経路で20〜25AP検出を
   達成した実績があり，「クロックが正しければ検出できる」という
   一次仮説の判別が安価に行える。AP検出できればBBPLL実周波数の
   解明（旧項目2，`rtc_clk_bbpll_configure()`相当のregi2c設定実装）
   がそのまま0 APs解消の本命ルートとなり優先度が上がる。検出でき
   なければ別要因（真のRX/MAC経路の問題）を疑う——C6の85ラウンド
   調査の知見（`memory/project_c6_agc_investigation.md`）はその際の
   参照先だが，C6線への性急な統合は避けること（rigor doc）。
3. **`ESP32C5_LP_WDT_WDT_WKEY`（RWDT用，TIMG用）は今回`0x50D83AA1`で
   正しく動作することを実質的に確認した**（本ラウンドのdisable単独
   では効果がなかったが，これはSWDキーの問題であり，RWDT/TIMGの
   キー自体は当初から正しかったと判断できる——disableの効果が出な
   かったのはSWDの解錠失敗が支配的だったため）。
4. 本ラウンドで確定した教訓：**JTAG haltでのレジスタ読出しは，読出し
   対象がカウントダウン系（WDT等）の場合，halt自体がカウントを止める
   副作用を持つため「効いているように見える」が実際には効いていない
   ケースを覆い隠しうる**——今後同種の「レジスタは正しい値なのに実機
   挙動が伴わない」場面では，firmwareでの動作確認（本ラウンドの
   `esp32c5_reassert_wdt_disable`のような直接介入）を優先すべき。
   `memory/feedback_hardware_investigation_rigor.md`への追加候補。
