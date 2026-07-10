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
