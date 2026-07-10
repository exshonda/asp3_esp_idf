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
