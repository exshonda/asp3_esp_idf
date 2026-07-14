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
| 手順6「フル4MB@0x0書き戻し」はハンドオフ用ゲストバイナリ（`0x200000`）も消去する＝次回ハンドオフでCPU_LOCKUP無限ループ | ハンドオフ検証時は`stock_scan`3オフセットに加え`0x200000`へゲストバイナリを必ず別途書き込む | 実施37 |
| CP2102Nブリッジをpyserialでopenするとタイミング次第でDTRがassertされ，RTSトグルがダウンロードモード落ち＝UART完全無音になる（`r38_dualcap.py`が本ラウンドで再現） | open直後に`setDTR(False)`を明示してからRTSトグル（`r39_dualcap.py`で修正済み） | 実施39 |
| Direct BootではROM残置のPMAエントリ14（NAPOT 0x40800000/512KB＝HP SRAM全域，cfg=0xC0000019＝R+W+EN・**Xなし**）によりSRAMからの命令フェッチがInstruction access faultになる（IRAM_ATTR相当のRAM実行コードは全滅） | L=0（非ロック）なので実行前に`csrw 0xBCE, 0xC000001D`（X付与）。stockブート列はstartupでPMA全体を自前設定に上書きするため露呈しない | 実施39 |
| JTAG用USB-JTAG-serialのttyACM番号（`/dev/ttyACM1`/`ACM2`等）はUSB再列挙順に依存し，docs記載の固定値と実際の対応が入れ替わることがある（実施41で`D0:CF:13:F0:A7:44`がACM1ではなくACM2で列挙された実例を確認） | ttyACM/ttyUSB番号を信用せず，`/dev/serial/by-id/`のMACアドレス付きパスのみで照合してから操作する（UARTブリッジのby-idパスは本セッション内では安定していた） | 実施41 |

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

---

## 実施34：BBPLL正規較正（regi2c I2C_BBPLLブロック，48MHz XTALプロファイル）を実装——ROMは誤って**40MHz XTALプロファイル**で較正しており実効480×(48/40)=576MHz相当にロックしていたと事前JTAG読みで確定（advisor予測どおり），修正後は理論値=実測（80.00MHz/240.00MHz，各2/2）。CPU動作周波数をユーザ指示によりESP-IDF標準の**240MHz**へ昇格（CORE_CLK_MHZ/SIL_DLY/test_porting全て追随・6/6維持）。**しかし0 APs found（deaf-RX類縁）は解消せず**——WiFi-BBクロック設定（MODEM_SYSCON_CLK_CONF/CLK_CONF_POWER_ST/CLK_CONF1）をstockとbit完全一致まで確認した上での結果のため，実施33が最有力視した「BBPLL誤ロック仮説」は**反証**。次段の本命は実施14が既に示唆していた「BBPLL(block 0x66)はCPU/AHB用デジタルPLLでありWiFi RFシンセサイザそのものではない」線＝未公開のRF-synth用regi2c/analogブロック，または非クロック系のRXパス

### 背景・目的

実施33の申し送り：scanハングは解消したが`0 APs found`という新しい壁が残った。advisor査読による最有力候補は実施32が申し送っていたBBPLL周波数の不一致（実測96MHz vs理論80MHz）——WiFi MAC/BB/PHYのクロックがBBPLL系から分周されるため，全RF/BBタイミングが約20%ずれた規格外動作になっている疑い。本ラウンドはBBPLLを正規較正して規格内へ収め，冷間Direct BootでAP検出（本調査の主目標）を狙う。作業途中でコーディネータから追加指示があり，最終構成はCPU=80MHzで止めず，ESP-IDF標準（stock既定`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`）の240MHzまで昇格することとした。

### 1. 机上解剖：`rtc_clk_bbpll_configure()`のregi2c書込み列

`~/tools/esp-idf-v6.1/components/esp_hw_support/port/esp32c5/rtc_clk.c`の
`rtc_clk_update_pll_state_on_cpu_src_switching_start()`が，CPUをXTALから
PLL_F160M/PLL_F240Mへ切替える際，`s_cur_pll_freq != 480`（起動直後は常に
真）なら`rtc_clk_bbpll_enable()`＋`rtc_clk_bbpll_configure(xtal_freq,
480)`を呼ぶことをソースで確認した——**stockの2nd-stageブートローダは
CPUをPLLへ切替える都度，BBPLLを規正480MHzへ較正し直している**。
`rtc_clk_bbpll_configure()`（`hal/esp_hal_clock/esp32c5/include/hal/
clk_tree_ll.h`の`clk_ll_bbpll_set_config()`）の中身：

1. `clk_ll_bbpll_calibration_start()`：`I2C_ANA_MST_ANA_CONF0_REG`
   （実施14で既知＝`0x600AF818`）のbit2(`STOP_FORCE_HIGH`)をクリア・
   bit3(`STOP_FORCE_LOW`)をセット（較正回路を「実行中」へ）。
2. `clk_ll_bbpll_set_config(480, xtal_freq)`：regi2c block `0x66`
   （`I2C_BBPLL`）へXTAL周波数別のプロファイルを書込む。
   **48MHzプロファイル**＝`div_ref=1・div7_0=10・dr1=1・dr3=1・
   dchgp=5・href=3・lref=1`（**40MHzプロファイル**＝`div7_0=12・
   dr1=0・dr3=0`，他は共通）。書込み先はreg2(`OC_REF_DIV|OC_DCHGP`，
   全byte)・reg3(`OC_DIV_7_0`，全byte)・reg5(`OC_DR1[2:0]`/
   `OC_DR3[6:4]`，各masked)・reg6(`OC_DLREF_SEL[7:6]`/
   `OC_DHREF_SEL[5:4]`，各masked)。
3. `while(!clk_ll_bbpll_calibration_is_done())`：`ANA_CONF0`
   bit24(`CAL_DONE`)を待つ（stockは無条件while，本移植はタイムアウト
   付きに変更）。
4. `esp_rom_delay_us(10)`→`clk_ll_bbpll_calibration_stop()`：
   `STOP_FORCE_LOW`をクリア・`STOP_FORCE_HIGH`をセット（較正回路を
   確定的に停止）。

regi2cトランザクションの実体（`I2C_ANA_MST_I2C0/1_CTRL_REG`＝
`0x600AF800`/`+0x4`経由の疑似バス転送）は実施14が既に手動リプレイ
プロトコルを確立済みであり，本ラウンドもそれを踏襲した
（`_regi2c_impl_write`/`_regi2c_impl_write_mask`，
`~/tools/esp-idf-v6.1/components/esp_hal_regi2c/esp32c5/regi2c_impl.c`
で実体確認）。

### 2. advisor指示のcheap disambiguator：書込み前にBBPLL現在値を読む

advisorから「書換える前に現在のblock 0x66のreg3/reg5を読め——おそらく
`div7_0=12`（40MHzプロファイル）になっており，それだけで576/480=1.2倍
という実測96/理論80の比とぴったり一致する」との指摘を受け，
`r34_bbpll_read.py`（実施14の手動regi2cリプレイをJTAG化，read-only）で
実施33終了時点のflash（CPU=XTAL→PLL_F240M÷3切替済み，実測96MHz）を
そのまま読んだ（`r34_bbpll_pre_try1.log`）：

```
ANA_CONF0=0x2100e448 CAL_DONE=1 STOP_FORCE_HIGH=0 STOP_FORCE_LOW=1
OC_REF_DIV_byte(reg2)=0x51   (dchgp=5,div_ref=1 — 両プロファイル共通)
OC_DIV_7_0(reg3)=0x0c        (=12 → 40MHzプロファイル！48MHzなら0x0a=10)
OC_DR_byte(reg5)=0x00        (dr1=0,dr3=0 → 40MHzプロファイル．48MHzなら0x11)
OC_DHLREF_byte(reg6)=0x58
```

**予測的中**：ROMは実際のXTAL（48MHz）ではなく**40MHzプロファイル**で
較正しており，実効PLL周波数は480×(48/40)=**576MHz**相当——実測96MHz
÷理論80MHz=1.2倍と寸分違わず一致する。これにより実施32以来の
「BBPLL実周波数の推定違い」の**具体的な機序**が確定した（regi2c書込み
自体は事前に確認できたため，本ラウンドの実装＝この誤ったプロファイルを
正しい48MHzプロファイルへ上書きし再較正すること，と方針が固まった）。

### 3. 実装：`esp32c5_r34_bbpll_configure_480mhz()`（regi2c手動MMIO実装）

`asp3/target/esp32c5_espidf/target_kernel_impl.c`に新設。ROM regi2c
関数へのリンクは検討したが，B-0/B-1（WiFi無効）ビルドがROM ldを
含まないため使えず，実施14の手動プロトコル（`I2C_ANA_MST_I2C0/1_
CTRL_REG`への直接書込み，ビルド形態非依存）を採用した。

- `esp32c5_r34_regi2c_select_ctrl()`/`esp32c5_r34_regi2c_write()`/
  `esp32c5_r34_regi2c_write_mask()`：`_regi2c_impl_write()`/
  `_regi2c_impl_write_mask()`のビットレイアウトを忠実に再現
  （`[7:0]=block・[15:8]=reg_addr・[24]=WR_CNTL・[23:16]=data`，
  busy=bit25，全てタイムアウト付きbusy-wait）。
- `esp32c5_r34_bbpll_configure_480mhz()`：`clk_ll_bbpll_calibration_
  start()`→`clk_ll_bbpll_set_config(480,48)`相当のregi2c書込み列
  （2節のプロファイル）→タイムアウト付き`CAL_DONE`待ち→
  `calibration_stop()`，という順序を保持（regi2cのdiv/dr/lref/href
  書込みは較正実行中に行う，stockと同じ順序）。呼出し前提：CPUは
  まだXTAL直結（このPLLの出力はCPUクロックとして未参照）のため，
  較正中の一時的不定状態がCPU動作へ影響しない。
- `esp32c5_r32_cpu_clock_switch()`を修正：旧来のCAL_DONEビットのみを
  見る「較正済みならスキップ」分岐（実施32，ROMの較正が正しい前提の
  誤り）を廃止し，CAL_DONEの値に関わらず無条件で
  `esp32c5_r34_bbpll_configure_480mhz()`を呼ぶよう変更した。

### 4. 第1回実装のハマり：ICGドメインゲートでregi2cが無反応（新規発見）

非WiFi`test_porting`ビルド（`build/c5_r34_tp`）で実装・実機投入した
ところ，`r34_bbpll_read.py`で読み直すと**`ANA_CONF0`が恒久的に
`0x00000000`のまま**（`CAL_DONE=0`・`STOP_FORCE_HIGH/LOW`とも0＝
較正の痕跡なし）で，CPU周波数実測も旧96MHzのままだった
（`r34_tp80_cpufreq_try1.log`）。

直接アドレス読みで切り分けたところ，`MODEM_LPCON_CLK_CONF`
（`0x600af018`）は`0x00000004`（`clk_i2c_mst_en`=1，自分の書込みが
確かに反映）・`MODEM_SYSCON_CLK_CONF`（`0x600a9c04`）も`0x00201002`
（`clk_i2c_mst_sel_160m`=1）と，**前提クロックの設定自体は正しく
効いている**のに，`I2C_ANA_MST`ブロック自体（`ANA_CONF0/1/2`含む）が
無反応と判明した（`r34_diag_lpcon.log`）。

原因を`wifi/esp_wifi_adapter.c`の`esp_shim_modem_icg_init()`
（実施13の根本原因修正）と突き合わせて特定：**PMU HP_ACTIVEの
`icg_modem.code`（`0x600B000C` bits[31:30]）がDirect Boot起動直後は
0のままで，`MODEM_SYSCON`/`MODEM_LPCON`のICGビットマップ
（`CLK_CONF_POWER_ST`のST_MAP系フィールド）にコード0が含まれない
ため，`I2C_MASTER`ICGドメイン（regi2cマスタ自体の機能クロック）が
ゲートされたまま**——実施13が発見した「WIFIBBクロックのICGゲート」と
**同じ機構**が，regi2cマスタ自体にも掛かっていた（新規発見）。
`esp_shim_modem_icg_init()`はWiFiビルドの`esp_wifi_init()`経路で
のみ・本関数よりずっと後に実行されるため，`hardware_init_hook()`
最初期で動くBBPLL較正には間に合わない——**WiFi有無を問わず**発生する
構造的な順序問題である。

`esp32c5_r34_modem_icg_enable_min()`を新設し，
`esp_shim_modem_icg_init()`の最小部分集合（`I2C_MASTER`/`MODEM_APB`/
`LP_APB`の3 ICGドメインのみ，`WIFI`/`BT`/`FE`/`ZB`等は対象外）を
BBPLL較正の直前に独立して適用するよう修正した：`icg_modem.code=2`
（`0x600B000C`）→`MODEM_SYSCON_CLK_CONF_POWER_ST`
（`0x600A9C0C`）の`CLK_MODEM_APB_ST_MAP`＋`MODEM_LPCON_CLK_CONF_
POWER_ST`（`0x600AF020`）の`CLK_LP_APB_ST_MAP`/`CLK_I2C_MST_ST_MAP`
へビットマップ`BIT(2)=0x4`を設定→即時反映パルス2本
（`PMU_IMM_MODEM_ICG_REG` bit31＋`PMU_IMM_SLEEP_SYSCLK_REG` bit28，
実施13で両方必要と確認済みの組合せ）。

修正後の再実機投入で`ANA_CONF0=0x2100e404`（`CAL_DONE=1`・
`STOP_FORCE_HIGH=1`・`STOP_FORCE_LOW=0`＝正しく較正完了・停止），
`reg2=0x51・reg3=0x0a・reg5=0x11・reg6=0x78`（48MHzプロファイル
どおり）を確認した（`r34_tp80v3_bbplregs_try1.log`）。

### 5. 決定実験（第1段・安全側）：CPU=80MHz(div=3)でmcycle実測——理論値と完全一致

advisor指摘（「div=1で一気に240MHzへ行くと，もしBBPLL修正が失敗していた
場合576MHz÷1=576MHz相当の実オーバークロックになる恐れがあるので，
まずdiv=3(80MHz)で安全に検証してから昇格すべき」）に従い，
`ESP32C5_R34_CPU_DIVIDER=3`（bootloader相当）でまず検証した。

`build/c5_r34_tp`（非WiFi，`CORE_CLK_MHZ=80`/`SIL_DLY_TIM1/2=50`へ
一時的に変更）で`r32_cpufreq_recheck.py`（mcycle対SYSTIMER 16MHz
二点法）を実施：**79.9934MHz（試行1）／80.0095MHz（試行2）**——
理論値80MHzと実測が事実上完全に一致（実施32の96MHzという不一致は
解消）。`test_porting`は2/2独立RTSリセット試行いずれも
`# 6/6 passed`（`r34_tp80v3_console_try1.log`）。

続いて`build/c5_r34_wifi80`（WiFi有効`wifi_scan`アプリ，同じ80MHz
構成）を実機投入したところ，較正PASS・`esp_wifi_scan_start -> 0`
到達・約11秒のscan完走まで実施33同様に到達したが，**`0 APs found
(err=0)`は変わらず**（`promisc_rx_count=0`，WDTリセット0件，
`r34_wifi80_console_try1.log`）——真の80MHz環境下でも0 APsのまま。

### 6. 決定実験（第2段・最終構成）：CPU=240MHz（ESP-IDF標準）へ昇格

コーディネータからの追加指示（「CPUの動作周波数はESP-IDFの標準と同じ
動作周波数とする」）を受け，`stock_scan/sdkconfig`実測で
`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240`（`CONFIG_ESP_DEFAULT_CPU_FREQ_
MHZ_240=y`）を確認した上で，`ESP32C5_R34_CPU_DIVIDER=1`（
`rtc_clk_cpu_freq_to_pll_240_mhz(240)`相当，cpu_divider=1・
ahb_divider=6固定，制約`cpu_div<=ahb_div`かつ`ahb_div%cpu_div==0`は
1/6で充足）へ変更し，`esp32c5.h`の`CORE_CLK_MHZ`を240，
`SIL_DLY_TIM1/TIM2`を17（4cyc/240MHz=16.67ns→切上げ，実施32と同じ
「1反復=4サイクル」机上比例外挿——**hw-bp実機再較正は未実施**，
実施32以来の申し送り項目のまま）へ更新した。

`build/c5_r34_tp`（非WiFi，240MHz再構成）で`r32_cpufreq_recheck.py`：
**239.9708MHz（試行1）／239.9632MHz（試行2）**——理論値240MHzと
完全一致（2/2）。`test_porting`は2/2独立RTSリセット試行いずれも
`# 6/6 passed`（`r34_tp240_console_try1/2.log`）——CPUクロックが
96MHz→240MHzへ変化してもカーネル基本機能に退行なし。

`build/c5_r34_wifi80`（240MHz再構成，WiFi有効）を実機投入：較正PASS・
`esp_wifi_scan_start -> 0`到達・scan完走まで到達したが，**`0 APs
found (err=0)`は2/2とも変わらず**（`promisc_rx_count=0`，WDT
リセット0件，`r34_wifi240_console_try1/2.log`）——理論値と実測が
完全一致するESP-IDF標準クロック環境下でも0 APsのまま。

### 7. 環境要因の再確認：近隣に強信号APが引き続き存在

`nmcli device wifi list`で確認したところ，信号強度77・60・57等の
強い近隣APが多数存在（実施33と同様の環境）——環境要因（周辺にAPが
無い）ではないことを改めて確認した。

### 8. advisor指示の決定的判別実験：WiFi-BBクロック設定のstock比較（bit完全一致）——BBPLL誤ロック仮説を反証

CPU周波数の理論値=実測一致だけでは，「RXが実際に使うWiFi-BB
クロックチェーン自体が正しく動いているか」は未確認である（RXは
CPUクロックではなくWiFi-BBクロックで動く）。advisor指摘に従い，
以下3レジスタ（実施13が特定したWiFi-BBクロックのICG/enable設定，
実施14/15当時は「値は一致するがBBPLLが誤っていたため一致の意味が
弱かった」レジスタ群）を，stock（`stock_scan/`，scan実行中の生き値）
とASP3（240MHz構成，`wifi_scan`実行中の生き値）で比較した：

| レジスタ | stock（scan実行中） | ASP3（240MHz，scan実行中，2/2） |
|---|---|---|
| `MODEM_SYSCON_CLK_CONF`(`0x600a9c04`) | `0x00201002` | `0x00201002`（一致） |
| `MODEM_SYSCON_CLK_CONF_POWER_ST`(`0x600a9c0c`) | `0x64646400` | `0x64646400`（一致） |
| `MODEM_SYSCON_CLK_CONF1`(`0x600a9c14`，実施13の`CLK_WIFIBB_*_EN`群) | `0x0038e7ff` | `0x0038e7ff`（一致） |

**3レジスタとも完全一致**（`r34_stock_modemclk_diag.log`／
`r34_asp3_240_modemclk_diag.log`／`r34_asp3_240_modemclk_diag_try2.
log`）。実施14/15でも同様の一致は既に確認されていたが，当時は
「BBPLLが誤ロックしたままの一致」だったため判別力が弱いと保留されて
いた（実施14「全ての説明可能なクロック/ICG/BB-config系レジスタは
stock/ASP3間でビット同一——残る説明領域はregi2c内部の不可視RF状態か，
静的値に落ちないコードパス差のみ」）。**本ラウンドはBBPLLが真に
480MHzへ較正され直した状態での一致であるため，「WiFi-BBデジタル
クロックチェーンは今や真に正しく動作している」という強い意味を持つ**。

### 9. 総合判定：BBPLL誤ロック仮説は反証——0 APsの真因は依然未解明

- 実施33が最有力視した「BBPLL誤ロック（実測96MHz vs理論80MHz）が
  WiFi MAC/BB/PHY系クロックを規格外にしている」という仮説は，
  本ラウンドで**反証**された：BBPLLを正規較正し理論値=実測（80MHz・
  240MHzの両方で確認）にしても，WiFi-BBデジタルクロック設定も
  stockとbit完全一致にしても，`0 APs found`は一切変化しなかった
  （4通りの構成——旧96MHz/WiFi，真80MHz/WiFi，真240MHz/WiFi×2回—— 
  すべてで再現）。
- 実施14が既に示唆していた重要な限定が，本ラウンドで再浮上する：
  **block 0x66のBBPLLはCPU/AHBクロック生成用のデジタルPLLであり，
  WiFi RFシンセサイザそのものではない**。本ラウンドで確認できたのは
  「デジタル側（CPU/AHB/WiFi-BBクロックチェーン）が理論値どおり・
  stock一致で動いている」ことまでであり，**WiFi RF専用の regi2c/
  アナログブロック（PA/LNA/シンセサイザ較正等，実施14で「約60KBの
  未公開regi2cアドレス空間」と位置付けられた領域）が正しくロック・
  較正されているかは，本ラウンドでも依然確認できていない**。
  次段の最有力候補はこちらへ移る。
- 別の可能性として，クロック系ではなく非クロック系のRXパス
  （MAC/BB受信データパスのレジスタ設定・PHYの受信感度較正値等）に
  真因がある可能性も残る——本ラウンドの判別実験はクロック系のみを
  対象としており，これを排除するものではない。

### 10. C6 deaf-RXへの示唆（断定はしない）

本ラウンドは「ROMが起動時に確立するはずの状態が，Direct Bootでは
検証されないまま前提とされていた」という**同じバグ形式**（実施32の
CPUルートクロック未切替，実施33のWDTキー誤記，本ラウンドのBBPLL
プロファイル誤り）を3ラウンド連続で発見・修正しており，Direct Boot
特有の「bootloader相当処理の欠落」ファミリーとして一貫している。
C6の85ラウンドにわたるdeaf-RX調査（`memory/project_c6_agc_
investigation.md`）でも，同種の「ROM初期状態の未検証仮定」を
洗い直す価値はあるかもしれない——ただし，本ラウンドの主目標だった
「BBPLL修正でAPが検出できる」という予測は**外れた**ため，C6との
機序共有を积極的に示唆する材料にはならない（C5もC6と同様，デジタル
クロック系を全て正した上でなお0 APs/deaf-RXが残る，という点で
「表面的な類似」はむしろ強まったが，rigor docの教訓どおり安易な
統合はしない）。

### 11. test_porting回帰：`# 6/6 passed`（80MHz・240MHzそれぞれ2/2）

8節までの各段階（80MHz・240MHz）で`build/c5_r34_tp`（WiFi無効）を
2回ずつ独立RTSリセットで起動し，全て`# 6/6 passed`を確認済み
（5・6節に記載）。CPUクロックが96MHz→80MHz→240MHzと2段階変化しても
カーネル基本機能（syslog・tick timer・task生成/起動・semaphore・
eventflag・alarm/CLIC割込み配送）は退行なし。

### 12. C5#1最終状態（終了処理）

`build/c5_r34_wifi80`（BBPLL正規較正＋CPU=240MHz＝ESP-IDF標準構成，
`wifi_scan`アプリ，フル4MB@0x0）を最終flashとして書き戻した。RTS
リセット後45秒キャプチャで最終確認（`r34_final_state_console.log`）：
`rst:`はPOWERON以外0件（WDTリセットなし，45秒安定）・
`esp_wifi_scan_start -> 0`→`0 APs found (err=0)`→`RESCAN 0 APs`
×3回——8・9節と同型（scan完走・0 AP検出・クロックは正規化済み）。

C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触（前ラウンド
から変更なし）。DUTは常にUARTブリッジ`b04e3bcf...`（本セッションでは
`/dev/ttyUSB0`，MAC/シリアル照合済み，番号は環境で変動）／JTAGは
`D0:CF:13:F0:A7:44`（本セッションでは`/dev/ttyACM1`）を使用。

### 変更ファイル

- `asp3/arch/riscv_gcc/esp32c5/esp32c5.h`：`CORE_CLK_MHZ` 96→**240**・
  `SIL_DLY_TIM1`/`SIL_DLY_TIM2` 42→**17**（実施34確定，ESP-IDF標準
  240MHzへの昇格＋BBPLL正規較正後の机上比例外挿．hw-bp実機再較正は
  未実施のまま，実施32からの申し送り継続）。
- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r34_modem_icg_enable_min()`（新設，I2C_MASTER/MODEM_APB/
  LP_APB ICGドメインの最小有効化）・
  `esp32c5_r34_regi2c_select_ctrl()`/`esp32c5_r34_regi2c_write()`/
  `esp32c5_r34_regi2c_write_mask()`（新設，手動regi2c MMIOプロトコル）・
  `esp32c5_r34_bbpll_configure_480mhz()`（新設，BBPLL正規較正・
  48MHzプロファイル書込み＋再較正）を追加し，`esp32c5_r32_cpu_clock_
  switch()`から無条件で呼び出すよう変更（旧CAL_DONEのみ判定する分岐は
  廃止）。`ESP32C5_R34_CPU_DIVIDER`（1＝240MHz，段階検証中は3＝
  80MHzだった）・`ESP32C5_R34_AHB_DIVIDER`（6，固定）を新設。
- `docs/c5-bringup.md`：本節（実施34）追加。
- スクラッチ（リポジトリ外，`/tmp/.../scratchpad/`）：
  `r34_bbpll_read.py`（新規，BBPLL block 0x66 reg2/3/5/6/8のJTAG手動
  regi2c読出し）・`r34_bbpll_pre_try1.log`（2節）・
  `r34_tp80_console_try1.log`／`r34_tp80_bbplregs_try1.log`／
  `r34_tp80_cpufreq_try1.log`（4節・修正前の失敗ログ）・
  `r34_diag_lpcon.log`（4節，ICGゲート特定）・
  `r34_tp80v2_*`（settle delay単独では不十分だったことの確認ログ）・
  `r34_tp80v3_bbplregs_try1.log`／`r34_tp80v3_cpufreq_try1/2.log`／
  `r34_tp80v3_console_try1.log`（5節，ICG修正後の成功確認）・
  `r34_wifi80_console_try1.log`（5節，80MHz WiFi scan結果）・
  `r34_tp240_cpufreq_try1/2.log`／`r34_tp240_console_try1/2.log`
  （6節）・`r34_wifi240_console_try1/2.log`（6節，240MHz WiFi scan
  結果）・`r34_stock_modemclk_diag.log`／`r34_asp3_240_modemclk_
  diag.log`／`r34_asp3_240_modemclk_diag_try2.log`（8節，決定的
  判別実験）・`r34_final_state_console.log`（12節）。
  ビルド：`build/c5_r34_tp`（test_porting，非WiFi，段階的に80MHz→
  240MHzへ再構成）・`build/c5_r34_wifi80`（wifi_scanアプリ，同じく
  段階的に再構成，名称は80MHz検証開始時のまま）。
  stock参照：`stock_scan/build/`（実施15/29資産，本ラウンドで
  再フラッシュして8節の一次データ取得に使用．DUTへ再フラッシュ後は
  ASP3へ戻し済み）。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

1. **最優先＝WiFi RF専用regi2c/アナログブロックの探索**：本ラウンドで
   デジタル側クロック（CPU/AHB/WiFi-BB）は理論値・stock一致の両方で
   正しいと確定できたため，残る候補はRFシンセサイザ・PA/LNA較正等，
   実施14が「約60KBの未公開regi2cアドレス空間」と位置付けた領域に
   絞られた。C6の85ラウンド調査でも同種の壁（未公開regi2c block）に
   突き当たっており，参照先として有用だが，C6線への性急な統合は
   避けること（rigor doc）。
2. **非クロック系RXパスの確認**：本ラウンドの判別実験はクロック系の
   みを対象としており，MAC/BB受信データパスのレジスタ設定やPHY受信
   感度較正値そのものが誤っている可能性は排除できていない。
3. **SIL_DLY_TIM1/TIM2=17の実機再較正**：実施32以来，机上比例外挿の
   ままhw-bp実機再較正（実施03 §3と同じ手法）が未実施——優先度は
   1・2より低いが，累積している申し送り項目。
4. **BBPLLプロファイル誤りの再発防止**：ROMが「なぜ40MHzプロファイル
   で較正したのか」（40MHz XTAL品番との共通ブートコード・eFuse
   XTAL周波数フィールドの読み違い等）自体は未解明のまま。今回は
   ASP3側で正しいプロファイルへ**上書き**する対症療法で解決したが，
   根本（ROM側の挙動）を理解しておくと，他のレジスタでも同種の
   「ROM既定値が微妙に違うプロファイルを選んでいる」パターンを
   予見しやすくなる可能性がある——優先度は1・2より低い。

---

## 実施35：`esp_clk_init()`/`esp_clk_tree_initialize()`の残余効果4件（RTC_FAST_CLK源・RTC_SLOW_CLK較正値・PMU HP_MODEMバンク・PMU HP_SLEEPバンク）を実機JTAG差分で特定・ポート——**全て個別に因果棄却**（4件とも2/2でAP=0継続，JTAG読み戻しで正しく適用されたことは確認済み）。副産物として実施26/27/29のクロスカーネル・ハンドオフ機構を本セッションで再構築・実機で成功再現（20〜25AP検出，2/2）——「同一ソフトウェアで壁時計正しい状態から起動すれば動く」ことを再確認した上で，**動く状態と動かない状態の広域レジスタ差分を候補4件のポート後も含めて再取得したところ，未説明の差分は19語まで縮小し，その全てが既知の非因果カテゴリ（実施23/24で棄却済みのPD_TOP系force・実施22で棄却済みのBOD/グリッチ検出器ノイズ・PVTドリブンの動的dbias/oscillatorトリム・読取専用ステータス）に帰属できた**——静的レジスタ値としての「RXキー」はこの時点で実質的に尽きたことを示す強い間接証拠

### 背景・目的

実施30が特定した scan/RX キー（B）の境界は，`esp_clk_tree_initialize()`＋
`esp_clk_init()`区間（ハンドオフ点P2/P3で成立・P1で不成立）にあると
判定されていたが，実施32/34はこの区間のうち「CPU周波数切替え」
（`rtc_clk_cpu_freq_set_config()`）と「BBPLL正規較正」のみを移植して
おり，`esp_clk_init()`のそれ以外の効果（RTC_FAST/SLOW_CLK関連）は
未移植のまま残っていた。本ラウンドはこれを列挙・優先度付けした上で
1候補ずつ加算移植し，冷間Direct BootでのAP検出（本調査の主目標）を
狙う。

### 1. 机上：`esp_clk_init()`/`esp_clk_tree_initialize()`の全効果列挙

`~/tools/esp-idf-v6.1/components/esp_system/port/soc/esp32c5/clk.c`の
`esp_clk_init()`を全行精読し，実施32/34で移植済みの項目を除いた残余
リストを作成した：

| # | 効果 | 移植状況（本ラウンド開始時点） |
|---|---|---|
| 1 | `rtc_clk_8m_enable(true)`（RC_FAST発振器enable） | 未移植 |
| 2 | `rtc_clk_fast_src_set()`（RTC_FAST_CLK源選択，既定RC_FAST） | 未移植 |
| 3 | RTC WDTタイムアウト再設定（`CONFIG_BOOTLOADER_WDT_ENABLE`依存） | ASP3独自のWDT無効化方式のため対象外と判断 |
| 4 | `modem_clock_deselect_all_module_lp_clock_source()` | WiFi分は`modem_clock_select_lp_clock_source()`内部で実質的に再デセレクトされるため実施30時点で既に消化済みと判断（COEX/BT分はWiFi専用ビルドのため無関係） |
| 5 | `select_rtc_slow_clk()`：`rtc_clk_slow_src_set()`（RTC_SLOW_CLK源選択） | 未移植（ただし既定値がリセット後既定と同じRC_SLOWのため実質no-opの可能性） |
| 6 | `select_rtc_slow_clk()`：`pmu_ll_lp_set_clk_power()`（PMU LP_ACTIVE.clk_power） | 未移植 |
| 7 | `select_rtc_slow_clk()`：`rtc_clk_cal()`＋`esp_clk_slowclk_cal_set()`（RTC_SLOW_CLK較正値保存） | 未移植 |
| 8 | CPU周波数切替え（`rtc_clk_cpu_freq_set_config()`） | **実施32/34で移植済み** |
| 9 | `esp_cpu_set_cycle_count()`（mcycle再計算） | WiFi/RXと無関係，対象外 |

`esp_clk_tree_initialize()`（`esp_hw_support/port/esp32c5/esp_clk_tree.c`）
は，現在のCPUクロック源以外の未使用PLL参照クロック（12M/20M/40M/48M/
60M/80M/120M/160M/240M refクロック）を個別にゲートオフする処理のみで，
「不要な参照クロックを止める」という**節電目的の減算的操作**であり，
ASP3がこれを呼ばないことは（それらの参照クロックを止めずに残す方向
なので）RXを塞ぐ機序としては考えにくいと判断し，優先度を下げた
（advisorとの合流でも同じ判断）。

### 2. JTAG差分確認：候補5〜7に対応する2件の静的差分を実機で確認

advisor指摘に従い，机上の優先順位だけで実装に入らず，まずJTAG読取り
で「ASP3冷間 vs stock動作中」の実測差分を確認してから対象を絞った。
ESP-IDF v6.1 `examples/wifi/scan`を本ラウンドでC5#1へ再ビルド・再
フラッシュし（`stock_scan/`），scan実行中（RTSリセット後T=0.3s/6.0s，
独立2回試行）にJTAGで下記レジスタを読み取り，同時点のASP3冷間
（`build/c5_r34_wifi80`，実施34終了時点のまま）と比較した：

| レジスタ | ASP3冷間 | stock（scan中，2/2・2点とも同一） | 判定 |
|---|---|---|---|
| `RTC_SLOW_CLK_CAL_REG`=LP_AON_STORE1（`0x600B1004`） | `0x00000000` | `0x00358b20` | **差分あり**（候補7） |
| `LP_CLKRST_LP_CLK_CONF`（`0x600B0400`，`fast_clk_sel`＝bits[3:2]） | `0x00000004`＝1(XTAL_D2，リセット既定のまま) | `0x00000000`＝0(RC_FAST) | **差分あり**（候補2） |
| PMU LP_ACTIVE.clk_power（`0x600B00AC`） | `0x40000000` | `0x40000000` | **一致**（候補6は移植不要と判明，POR既定値が既にesp_clk_init()の目標値と同じだった） |

`LP_CLKRST_LP_CLK_CONF`の`slow_clk_sel`（bits[1:0]）はASP3・stockとも
`0`（RC_SLOW）で一致——esp_clk_init()の既定選択（RC_SLOW）はリセット
既定値と同じため候補5は実質no-opと確認できた（移植不要）。

### 3. 候補1（＝机上番号2，RTC_FAST_CLK源）：実装・実機テスト・**refute**

`asp3/target/esp32c5_espidf/target_kernel_impl.c`に
`esp32c5_r35_rtc_fast_clk_select()`を新設（`LP_CLKRST_LP_CLK_CONF`の
`fast_clk_sel`をmasked read-modify-writeでRC_FAST(0)へ変更），
`hardware_init_hook()`の`esp32c5_r32_cpu_clock_switch()`直前（stockの
実行順序に合わせた位置）から呼び出す。

`build/c5_r34_wifi80`へビルド・実機投入し，独立2回のRTSリセット試行
で確認：JTAG読み戻しで`fast_clk_sel=0`（stock一致）へ切り替わった
ことを確認した上で，**`0 APs found`は不変**（2/2）。**候補1は単独では
refute**（レジスタ適用は正しいことをJTAGで確認済み，AP検出への因果は
無し）。

### 4. 候補2（＝机上番号7，RTC_SLOW_CLK較正値）：因果確認段階（固定値注入）・**refute**

task手順の2段構え（「固定値書込みでまず因果を確認→効けば正式な較正
実装へ」）に従い，`rtc_clk_cal()`の完全な実測ルーチン（主XTAL基準の
サイクルカウント測定）ではなく，2節でstockから実測した値
（`0x00358b20`）を`esp32c5_r35_rtc_slowclk_cal_set_fixed()`として
定数注入する形でまず実装した。候補1に追加する形で
`hardware_init_hook()`へ組み込み，ビルド・実機投入。

独立2回のRTSリセット試行：JTAG読み戻しで注入値が正しく保持されている
ことを確認した上で，**`0 APs found`は不変**（2/2）。**候補2も
refute**——advisorが事前に指摘していたとおり（`promisc_rx_count=0`
という「タイミング定数の誤りというより受信経路そのものが無反応」と
いう症状の性質から，タイミング較正値の候補は本命ではないと予想され
ていた），この予想が実測でも裏付けられた。

### 5. advisor指摘：診断が「stock（scan-example）とASP3（wifi_scan）という別ソフトウェア」の比較で確認されている点への懸念，広域差分の必要性

候補1・2が個別にrefuteされた時点でadvisorに相談したところ，
「実施29の11ブロックJTAGスナップショット相当の広域差分比較を行い，
`esp_clk_init()`の外側も含めて未探索の差分がないか確認すべき」との
指摘を受けた。実施29の`stock_headjump/`・`stock_earlyjump/`等の
スクラッチ資産は本セッションでは失われていた（別セッション/別PCで
作成されたもの）ため，まず**stock（`examples/wifi/scan`）実行中と
ASP3冷間（wifi_scanアプリ，候補1・2込み）**という，ソフトウェアが
異なる2条件でのJTAG広域差分を先に取得した（`blockdump.py`新設，
MODEM_SYSCON/MODEM_LPCON/MODEM1/MODEM_PWR0/PMU/LP_CLKRST/LP_AON/
LP_I2C_ANA_MST/LPPERI/LP_ANA/PVT_MONITORの11ブロック，実施29の
スナップショット対象と同一構成）。

この最初の広域差分で，**PMU HP_MODEMバンク（`pmu_hp_system_init()`が
`PMU_MODE_HP_MODEM`で設定する7レジスタ，`0x600B0034`〜`0x600B005C`）
がASP3ではほぼ全ゼロ（一度も設定されていない）のに対しstockは
`pmu_init()`実行後の実測値が全て非ゼロ，という大きな差分**を発見した
（実施21〜24の較正キー(A)調査はHP_ACTIVEバンクの一部フィールドのみを
対象としており，HP_MODEMバンクは調査範囲外だった）。ただしこの時点
での差分（MODEM_SYSCON/MODEM_LPCONの一部・PMU多数・LP_CLKRST/LPPERI/
LP_ANA等，計40件超）は，advisorが後に指摘したとおり，**stockと
ASP3で異なるアプリケーション（`examples/wifi/scan` vs 自作
`wifi_scan`）を比較しているという交絡**を含んでいた
（MODEM_SYSCONの8箇所の1bit差分・MODEM_LPCONでASP3の方が多くビットを
立てている2箇所は，後に「同一ソフトウェア」比較で消失することが
判明——アプリ層のWiFi/COEX設定パスの違いによるノイズだったと確定，
7節参照）。

### 6. 候補3（PMU HP_MODEMバンク＋HP_ACTIVE.HP_REGULATOR0）：固定値注入・**refute**

5節の発見を受け，`esp32c5_r35_pmu_hp_modem_bank_fixed()`を新設し，
HP_MODEMバンク10レジスタ（`DIG_POWER`/`ICG_HP_FUNC`/`ICG_HP_APB`/
`ICG_MODEM`/`HP_SYS_CNTL`/`HP_CK_POWER`/`BACKUP`/`BACKUP_CLK`/
`SYSCLK`/`HP_REGULATOR0`）＋`HP_ACTIVE.HP_REGULATOR0`（軽微な差分
あり）へstock実測値を固定注入，`pmu_ll_imm_update_dig_icg_modem_code`/
`update_dig_icg_switch`相当のラッチパルス（実施34と同一レジスタ，
`PMU_IMM_MODEM_ICG_REG`/`PMU_IMM_SLEEP_SYSCLK_REG`）も発行する形で
実装した。`hardware_init_hook()`の候補1・2より前（stockの実行順序，
`pmu_init()`は`esp_clk_init()`より前）に追加。

独立2回のRTSリセット試行：**`0 APs found`は不変**（2/2）。JTAG読み
戻しでHP_MODEMバンクは正しく注入値を保持していることを確認したが，
`HP_ACTIVE.HP_REGULATOR0`は注入直後は正しい値だったものの，再読取り
すると別の値（`0xc0048060`）へ収束していた——**PVT自動dbias補正
ループ（実施21で確認済みの機構）がライブに上書きしている**ことが
判明した（stockの実測値`0xc004afd0`とも一致しない）。**候補3も
refute**——ただしHP_MODEMバンク自体の注入は正しく機能したことは
確認済み（8節で再確認）。

### 7. クロスカーネル・ハンドオフ機構の再構築と実機成功再現（2/2，20〜25AP検出）

候補1〜3が尽きた時点でadvisorに再度相談し，「実施29がASP3自身の
コードで20〜25AP検出に成功した実績があるので，`esp_clk_init()`だけに
絞らず，同一ソフトウェア条件下での広域差分（＝実施26/27/29のハンド
オフ機構を使った差分）を取るべき」との指摘を受けた。実施26/27/29の
`asp3_jump.c`はスクラッチ（本セッションのscratchpad）に偶然残置
されていたため（別セッションでの作業の残骸と判明，`mmu_hal_unmap_all`
→`mmu_hal_map_region`→`cache_hal_invalidate_addr`→エントリ0x42000008
への直接ジャンプ，実施27のROM-ifポインタ域ゼロクリア込み），これを
再利用し，`stock_scan/main/scan.c`を実施29の「headjump」設計
（`app_main()`先頭でNVS/WiFi一切未実行のまま即ジャンプ）で書き換えた。

**新規の安全対策**：本ラウンドの候補1〜4（後述）はハンドオフ状況では
危険となりうる——ホストが既に確立したCPU/BBPLLクロックを，生きた
CPU実行中に再切替え・再較正することになるため，実行中クロックの
グリッチ/ハングリスクがある。`hardware_init_hook()`に
`HANDOFF_SKIP_CLOCK_INIT`コンパイルガードを新設し，このガード付き
ビルドでは候補1〜4＋実施32/34のCPUクロック切替え/BBPLL較正を全て
スキップしてホストの状態をそのまま引き継ぐようにした（実施26の
`HANDOFF_SKIP_WIFI_INIT`と同種の設計）。

`build/c5_r35_handoff_guest`（`ASP3_EXTRA_COMPILE_DEFS=
HANDOFF_SKIP_CLOCK_INIT=1`，wifi_scanアプリ，候補1〜4込みソース）を
640KB切詰めでオフセット`0x200000`へ配置，`stock_scan`（bootloader/
partition-table/app）をオフセット0系列へ配置し，独立2回のRTSリセット
試行を実施した：

```
r35_headjump: app_main entered (no NVS/WiFi/PHY executed) -- jumping now
asp3_jump_now: masking interrupts
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5 ...
...
wifi_scan: 20 APs found (err=0)      ← 試行1
wifi_scan: RESCAN 24 APs (err=0)
...
wifi_scan: RESCAN 21 APs (err=0)     ← 試行2
wifi_scan: RESCAN 23 APs (err=0)
```

**実施29のheadjump結果を完全に再現**（2/2，20〜25APのオーダー）。
これにより，本ラウンドで候補1〜4を追加したコード自体が「壁時計正しい
起動状態」の下では機能を阻害しないこと，かつ機構自体が本セッションで
正しく再構築できたことが同時に確認できた。

### 8. 「同一ソフトウェア」差分：候補3（HP_MODEMバンク）は正しく機能していたことを確認・新規候補4（HP_SLEEPバンク）を発見

7節のハンドオフ成功状態（候補1〜3込み，`HANDOFF_SKIP_CLOCK_INIT`で
候補1〜3自体は未実行）と，候補1〜3込みのASP3冷間（`HANDOFF_SKIP_
CLOCK_INIT`無し，候補1〜3が実行される）を，**同一のwifi_scanソース
ツリー**で広域11ブロック差分を取り直した（`asp3cold_full_boot1.
json`，5節の交絡を解消した比較）。

結果：MODEM_SYSCON/MODEM_LPCON/MODEM1/MODEM_PWR0/LP_I2C_ANA_MSTは
**全て完全一致**（0diff，5節で見えていた差分は全てアプリ層ソフト
ウェア差の交絡だったと確定），HP_MODEMバンクも**完全一致**（候補3の
注入が正しく機能していたことをここで確認），一方
**HP_SLEEPバンク（`0x600B0068`〜`0x600B0098`，9レジスタ）が未移植の
まま大きく異なる**ことを新規発見した——`pmu_hp_system_init()`は
`PMU_MODE_HP_ACTIVE`/`PMU_MODE_HP_MODEM`/`PMU_MODE_HP_SLEEP`の3
モードを同一ループで設定するが，候補3はHP_MODEMとHP_ACTIVE.
HP_REGULATOR0のみを対象としており，HP_SLEEPバンクを見落としていた。

### 9. 候補4（PMU HP_SLEEPバンク）：固定値注入・**refute**

`esp32c5_r35_pmu_hp_sleep_bank_fixed()`を新設し，HP_SLEEPバンク10
レジスタ（`DIG_POWER`/`ICG_HP_FUNC`/`ICG_HP_APB`/`HP_SYS_CNTL`/
`HP_CK_POWER`/`BACKUP`/`BACKUP_CLK`/`SYSCLK`/`HP_REGULATOR0`/
`XTAL`）へ実施35のハンドオフ実験の実測値を固定注入する形で実装，
候補3の直後に追加した。

独立2回のRTSリセット試行：**`0 APs found`は不変**（2/2）。**候補4も
refute**。

### 10. 候補1〜4込みの最終差分：19語まで縮小・全て既知の非因果カテゴリに帰属

候補1〜4を全て適用したASP3冷間状態で再度11ブロック広域差分を取り
直し（`asp3cold_full_cand4_boot1.json`），7節のハンドオフ成功状態
（同一wifi_scanソースツリー）と比較した。**未説明の差分は19語**まで
縮小し，内訳は以下のとおり全て説明可能なカテゴリに帰属できた：

- **PD_TOP/HPAON/HPCPU/LPPERI force系**（`0x600B00F8`/`FC`/`100`/
  `10C`，ASP3=`0x1c`・ハンドオフ=`0`）：実施23/24で個別に因果検証・
  **既に棄却済み**の項目群と同一クラスタ。
- **PD_MEM_CNTL**（`0x600B0110`）：上記forceクラスタの一部（軽微な
  ビット差）。
- **SLP_WAKEUP_CNTL3**（`0x600B0130`）：sleep-wakeup関連，Direct Boot
  は実sleepへ遷移しないため機序上無関係。
- **CLK_STATE0**（`0x600B0198`）：現在のクロックmux状態を示す
  **読取専用ステータスレジスタ**（設定対象ではない）。
- **HP_ACTIVE.HP_REGULATOR0**（`0x600B0028`）：6節で確認済みの
  **PVT自動dbias補正ループによる動的値**（固定注入不可能）。
- **`LP_CLKRST_FOSC_CNTL`**（`0x600B0418`）：RC_SLOW発振器の
  トリム値レジスタと判明（`LP_CLKRST_FOSC_CNTL_REG`）——dbiasと同様
  PVT/温度依存の自動較正回路が駆動する動的値の可能性が高い（未確認，
  優先度低）。
- **`LP_CLKRST_LP_CLK_PO_EN`**（`0x600B0404`）：ハンドオフ(WORKS)側
  が`0`（無効）・ASP3冷間(FAILS)側が`0x7ff`（有効）——**「動く方が
  無効」という逆方向**であり，「欠けている設定」という仮説とは整合
  しない（追加のenableが不要である証拠）。
- **LPPERI 4件・LP_ANA 3件**：実施22で「BOD/電圧グリッチ検出器・LP
  ドメイン周辺クロックで，WiFi RF/PHYバイアス経路と機序上無関係」と
  既に判定済みのレジスタ群と同一。
- **`RTC_SLOW_CLK_CAL_REG`**（`0x600B1004`）：ハンドオフ実測
  `0x00359755`・候補2の固定注入値`0x00358b20`とわずかに異なる（実測
  オシレータのブート間variation，4節で候補2自体は既にrefute済み）。
- **PVT_MONITOR**（`0x600190d8`）：温度/電圧の生きた読み値（4節と
  同型の動的データ，実施29のスナップショットでも既知の変動源）。

**19語のうち，静的な設定漏れの可能性が残るのはゼロ**——全て
(a)既に個別に因果棄却済み，(b)読取専用ステータス，(c)PVT/温度駆動の
動的補正値，のいずれかに分類できた。これは，task手順3が想定していた
「P4ハーネスでの差分消失/残存判定」を，より直接的な広域レジスタ差分
という形で先取りして行った結果に相当し，**「静的レジスタ値としての
RXキー」は本ラウンドの探索範囲内で実質的に尽きたことを示す強い間接
証拠**である。

### 11. 総合判定：候補1〜4は個別に因果棄却，AP検出（本調査の主目標）は未達

- 4候補（RTC_FAST_CLK源・RTC_SLOW_CLK較正値・PMU HP_MODEMバンク・
  PMU HP_SLEEPバンク）は全て実機JTAG差分で発見され，全て正しく適用
  されたことをJTAG読み戻しで確認した上で，全て単独／累積いずれの
  構成でも`0 APs found`が不変（各2/2，累積構成でも2/2）——**全て
  causal refute**。
- 一方，実施26/27/29のクロスカーネル・ハンドオフ機構を本セッションで
  再構築し，実機で成功を再現できた（2/2，20〜25AP検出）——調査の
  中核ツールが本セッションでも機能することを再確認できた**重要な
  副産物**。
- 10節の広域差分縮小により，「動く状態」と「動かない状態」の**静的
  レジスタレベルでの説明可能な差はほぼ尽きた**——次段の本命は，
  実施34が既に示唆していた「未公開のRF-synth用regi2c/analogブロック」
  か，あるいはC6の85ラウンド調査が到達した「シーケンス/タイミング/
  レジスタに現れないアナログ特性」という軸（advisorが繰り返し
  指摘していた方向）に，より強く絞られた。

### 12. test_porting回帰：`# 6/6 passed`（2/2，一時的なUART出力消失は実施35のコード変更と無関係と確定）

`build/c5_r34_tp`（WiFi無効，候補1〜4込みの現ソースツリー）で回帰
確認を試みたところ，最初の2回の試行でUARTコンソール出力が0バイト
（`_kernel_target_exit`の無限ループには正常到達——JTAG読取りでPCが
2秒間隔でも同一番地に固定されていることを確認，カーネル自体は正常
完走していた）という現象に遭遇した。**単一変数A/Bで原因を切り分け**：
`target_kernel_impl.c`を`git stash`でHEAD（実施35の変更皆無）へ完全
に戻し同じ`build/c5_r34_tp`で再ビルド・再フラッシュしても**同じ0
バイト現象が再現**したため，**実施35のコード変更が原因ではないと
確定**した。変更を`git stash pop`で復元後，改めてビルド・フラッシュ
した際には問題なく`# 6/6 passed`（TAP出力を含め正常）が2回連続で
得られており，繰り返しのJTAG/USB接続操作に伴う一時的な環境要因
（USBシリアルブリッジ側の過渡状態等）だった可能性が高いと判断した
（原因の完全な特定は本ラウンドのスコープ外，次段への申し送り）。

最終確認（`r35_tp_final_boot2.log`）：
```
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
CPUクロックが候補1〜4のPMU/LP_CLKRST変更を含んでもカーネル基本機能
（syslog・tick timer・task生成/起動・semaphore・eventflag・alarm/CLIC
割込み配送）に退行なし。

### 13. 出荷時ガード：refuteされた候補2〜4を既定で無効化（advisor指摘への対応）

12節時点の最終flashは候補1〜4を**全て常時実行**する構成だったが，
advisorの査読で「候補3・4はstock実測値（HP_MODEM/HP_SLEEPバンクの
`regulator0`含む）を固定注入しており，`pmu_init.c:175`の実コードでは
これらの`dbias`はeFuse由来の基板固有電圧トリム（`get_act_hp_dbias()`）
であるべき値——HP_ACTIVE側でのPVT自動dbias上書き（6節）は確認済みだが
HP_MODEM/HP_SLEEP側で同種の上書きが起きるかは未確認であり，refuteされた
実験コードのまま『出荷』すると他基板でC5#1固有のトリム値を焼き付ける
副作用リスクがある」との指摘を受けた。

対応として，`hardware_init_hook()`に`ESP32C5_R35_ENABLE_REFUTED_
CANDIDATES`コンパイルガードを新設し，**候補2（RTC_SLOW_CLK較正値固定
注入）・候補3（PMU HP_MODEMバンク）・候補4（PMU HP_SLEEPバンク）を
既定で無効化**した（関数自体は次段の参考・再現実験用に残置，定義時のみ
有効化）。**候補1（RTC_FAST_CLK源をRC_FASTへ）はstockの実際の選択と
一致する正しい修正でありrefuteの対象外のため，常時有効のまま残す**。

ガード適用後の再ビルド・実機確認（`build/c5_r34_wifi80`，
`build/c5_r34_tp`）：
- `wifi_scan`：`0 APs found`→`RESCAN 0 APs`（複数回）——ガード前
  （候補1〜4全有効）と同一の症状，退行なし（`r35_shipped_final_
  console.log`）。
- `test_porting`：`# 6/6 passed`（`r35_tp_shipped_boot1.log`）——退行
  なし。

### 14. C5#1最終状態（終了処理）

`build/c5_r34_wifi80`（**240MHz構成**，候補1のみ常時有効・候補2〜4は
`ESP32C5_R35_ENABLE_REFUTED_CANDIDATES`ガードで既定無効という13節の
出荷時構成で再ビルド，フル4MB@0x0）を最終flashとして書き戻した。RTS
リセット後30秒キャプチャで最終確認（`r35_shipped_final_console.log`）：
`rst:`はPOWERON以外0件（WDTリセットなし，30秒安定）・`wifi_scan: 0 APs
found`→`RESCAN 0 APs (err=0)`を複数回確認——13節と同型（0 AP検出は
変わらず・退行なし）。

C5#2・C6 board C・UARTブリッジ`125a266b...`：完全未接触（前ラウンド
から変更なし）。DUTは常にUARTブリッジ`b04e3bcfa270f0118f4894301045c30f`
（本セッションでは`/dev/ttyUSB0`）／JTAGは`D0:CF:13:F0:A7:44`（本
セッションでは`/dev/ttyACM1`）をMAC/シリアル照合の上使用。

### 変更ファイル

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r35_rtc_fast_clk_select()`（候補1，新設・**常時有効**）・
  `esp32c5_r35_rtc_slowclk_cal_set_fixed()`（候補2，新設，固定値注入
  ——診断専用，正式なrtc_clk_cal()実測ではない旨をコメントに明記）・
  `esp32c5_r35_pmu_hp_modem_bank_fixed()`（候補3，新設）・
  `esp32c5_r35_pmu_hp_sleep_bank_fixed()`（候補4，新設）を追加し，
  `hardware_init_hook()`から呼出し。`HANDOFF_SKIP_CLOCK_INIT`
  コンパイルガードを新設し，候補1＋実施32/34のCPUクロック切替え/
  BBPLL較正一式をこのガードで囲み，ハンドオフ実験時に無効化できる
  ようにした（通常のcold bootでは未定義のため効果に変化なし）。
  4候補とも**refute**のため機序としては未解明のまま実装は残置するが，
  13節（advisor指摘）のとおり**候補2〜4は`ESP32C5_R35_ENABLE_
  REFUTED_CANDIDATES`ガードで既定無効**とした（board固有dbias値の
  焼付けリスクを避けるため）——候補1のみが出荷パス（既定ビルド）で
  常時実行される。
- `docs/c5-bringup.md`：本節（実施35）追加。
- スクラッチ（リポジトリ外，`/tmp/.../scratchpad/`）：
  `stock_scan/`（ESP-IDF v6.1 `examples/wifi/scan`本ラウンドで再
  ビルド，`main/asp3_jump.c`は別セッションの残置資産を再利用・
  `main/scan.c`を実施29のheadjump設計へ書換え・`main/CMakeLists.txt`
  に`hal`コンポーネント追加）・`r35/blockdump.py`（新規，11ブロック
  JTAG広域差分採取）・`r35/reset_and_read.py`（新規，タイムド
  RTSリセット＋JTAG読取り）・`r35/offcheck.c`/`offcheck2.c`（新規，
  PMU構造体オフセット計算，ホストネイティブビルド）・
  `r35/*_blocks_boot*.json`（各段階の11ブロックダンプ）・
  `r35/r35_guest_640k.bin`（`build/c5_r35_handoff_guest/asp_flash.bin`
  切詰め）・`r35/r35_*.log`一式。
  ビルド（本リポジトリ内，gitignore対象）：`build/c5_r35_handoff_guest`
  （`HANDOFF_SKIP_CLOCK_INIT=1`のゲスト，実施26/27/29機構の再検証用）。
- git commitは行っていない（指示どおり）。

### 次段への申し送り

1. **最優先＝10節で示唆された「静的レジスタ値としてのRXキーは尽きた」
   という結論の妥当性を，実施34の本命候補（未公開RF-synth用regi2c/
   analogブロック）で検証すること**。実施14が「約60KBの未公開regi2c
   アドレス空間」と位置付けた領域の系統的探索（block番号総当たり，
   実施25が試みた0x00〜0x1Fスイープをさらに広い範囲・より深いレジスタ
   まで広げる）が次の具体的な一手になる。
2. C6の85ラウンド調査が最終的に「シーケンス/タイミング/レジスタに
   現れないアナログ特性」に絞り込んだのと同型の壁にC5も到達しつつ
   ある——ただしrigor docの教訓どおり，C6線への性急な統合はまだ
   避けること（C5固有の探索余地，特に1の未公開regi2c領域はC6でも
   未踏の可能性があり，先にC5で潰す価値がある）。
3. 本ラウンドで再構築したクロスカーネル・ハンドオフ機構
   （`stock_scan/main/asp3_jump.c`＋`HANDOFF_SKIP_CLOCK_INIT`ガード）
   は次段以降でも再利用可能な資産——スクラッチが失われた場合も
   本ラウンドの記述（7節）から再構築できる詳細度で記録した。
4. `LP_CLKRST_FOSC_CNTL`（RC_SLOW発振器トリム値と推定）・
   `LP_CLKRST_LP_CLK_PO_EN`（用途未確認，ハンドオフ側で無効なのに
   ASP3冷間側で有効という逆方向の差分）は，本ラウンドでは優先度低
   として深掘りしなかった——1の探索が不調に終わった場合の予備候補
   として記録しておく。
5. `test_porting`のUART出力消失現象（12節）は原因未特定のまま
   再発しなくなった——次段で再発した場合は，USBシリアルブリッジの
   物理層状態（JTAG/UART接続の頻繁な切替えの影響）を疑うこと。

---

## 実施36：同一ソフトウェア差分ハーネスをregi2c（アナログ）ドメイン全域へ拡張——系譜決定的差分9語を検出し，scan前注入(a)・較正前注入(b)の両タイミングで**全て因果棄却（各2/2）**。副産物として0x63/0x6b系の差分は「系譜キー」ではなく**較正の生きた出力（ブート毎/ビルド毎に変動）**と再分類。stockブート列のリセット/イネーブルパルス列のソース監査も**欠落ゼロ**（唯一の真のパルス=rst_wifimacはblob経由で両者同一）——regi2cでアドレス可能な観測・介入空間はこれで消尽

### 背景・目的

実施35で静的MMIOレジスタとしてのRXキーは消尽（未説明差分19語→全て既知
非因果カテゴリに帰属）。残る観測空間＝regi2c（アナログ）ドメイン。実施25
の8blockフルスイープは「stock vs 冷間ASP3（較正ハング中・クロック未切替
時代）」の比較で差分ゼロだったが，較正が壊れていた当時の条件であり，
現在の「同一ASP3バイナリ・較正成功・scan完走で0AP vs 20-25AP」という
比較条件は初めて。実施25の検証済み読取りプロトコル（known-answer gate
込み）を，実施35のハンドオフ差分ハーネスへ移植して4-way比較を行い，
差分が出れば冷間側へ注入して因果検証（主目標=AP検出）する。

### 1. ハーネス設計（`r36/r36_sweep.py`）

- **採取点＝壁時計固定遅延（RTSリセット後t=15.0s）でのJTAG halt**。
  両条件とも文字通り同一のwifi_scanバイナリロジック（初回scan完了
  t≈13.7-14.0s→RESCANループ）が走っており，t=15sは両条件で「RESCAN
  1回目の実行中」という同一ソフトウェア地点に当たる（事前にUART
  タイムスタンプ計測で両条件のタイムラインがほぼ一致することを確認：
  esp_wifi_init完了=冷間0.80s/ハンドオフ1.0s，scan_start=両者3.9-4.0s，
  初回scan完了=13.7-14.0s）。ブレークポイント方式（実施25）でなく
  タイマ方式にしたのは，同一バイナリなら同一アドレスに揃うため
  bp が不要になり，かつ後述の「JTAG haltがタイムラインを歪める」問題
  を避けられるため。
- **UARTキャプチャはhalt を跨いで継続**し，スイープ後のresumeで同一
  ブートのRESCANが正常なAP数で完走することを毎回確認する（採取が
  scan成否を乱さないことのブート内確認）。
- **known-answer gate（毎ブート）**：block=0x6b/host=1/reg=0x02を
  「正しいRD_MASK（~BIT(3)）」と「マスク無し（0xFFFFFF）」の両方で
  読み，正マスク側が非0xFFかつ両者が異なることを要求（RD_MASKが
  生きている＝I2C_ANA_MSTのICGが開いている＝読める，の実証。実施25
  Step A / rigor doc第3反復の偽陰性対策）。**4ブート全てPASS**
  （冷間=0x74固定，ハンドオフ=0x54/0x53——この値自体が後述の重要な
  手がかりになった）。
- 対象：実施25の8block（0x61/0x63/0x66/0x68/0x69/0x6a/0x6b/0x6d）×
  host 0/1×reg 0x00-0x1F（512読出し/ブート，素のread専用・ANA_CONF1
  はblock毎に保存/復元・復元成功を毎回読み戻し確認）＋`I2C_ANA_MST`
  MMIO生値11語（0x600AF800-0x828）＋探索的拡張=未検証block ID
  8個（0x60/62/64/65/67/6c/6e/6f，9種のRD_MASK候補×host両側×reg0）。

### 2. 4-way採取結果（冷間×2・ハンドオフ×2，各ブートで採取後のscan成否確認込み）

- **採取の非摂動性**：4ブート全てで，24〜33秒のJTAG halt（スイープ
  実行時間）後のresume後に同一ブートのRESCANが正常完走——冷間=0 APs
  （ベースライン一致），ハンドオフ=RESCAN 24-26 APs（20-25APの
  ベースライン範囲）。**採取はscan成否を乱さない**（要件充足）。
- **系譜決定的差分（冷間2ブート一致∧ハンドオフ2ブート一致∧冷間≠
  ハンドオフ）＝9語**：

  | block | host,reg | 冷間 | ハンドオフ | 備考 |
  |---|---|---|---|---|
  | BBPLL(0x66) | 1,4 | 0x27 | 0x23 | |
  | BBPLL(0x66) | 1,7 | 0x0b | 0x0d | 後に書込み不能（RO）と判明 |
  | BBPLL(0x66) | 1,9 | 0x02 | 0x03 | |
  | DIG_REG(0x6d) | 0,5 | 0x18 | 0x98 | bit7差 |
  | DIG_REG(0x6d) | 0,7 | 0x18 | 0x98 | bit7差 |
  | DIG_REG(0x6d) | 0,13 | 0x4e | 0x42 | |
  | ULP_CAL(0x61) | 0,5 | 0x40 | 0x47 | 実施24既知の自己shim差・除外 |
  | UNK_63(0x63) | 1,3 | 0x65 | 0x68 | 後に再分類（5節） |
  | UNK_63(0x63) | 1,4 | 0x00 | 0xc0 | 後に再分類（5節） |

  ＋近接差分1件：UNK_6b(0x6b) host1 reg2＝冷間0x74/0x74 vs ハンドオフ
  0x54/0x53（実施16以来追跡のtxcapレジスタ，ハンドオフ側±1変動）。
- **I2C_ANA_MST MMIO生値**：4ブート全て11語完全一致（差分ゼロ）。
- **探索的拡張8block**：全組合せで両条件とも0xFF固定＝どのマスク候補
  でも応答するルーティングを発見できず。**手がかり無し**（当該block
  IDがシリコン上に存在しないのか，マスク候補が悪いのかは判別不能。
  低確度の探索として記録のみ）。

### 3. 因果検証(a)＝scan前注入（JTAG，t=2.5s＝esp_wifi_init完了後・scan_start前）——refute（2/2）

`r36/r36_inject.py`で，ULP_CAL（実施24既知）を除く9候補へハンドオフ値
を書込み（実施25プロトコルのWR_CNTL付き書込み＋読み戻し確認）：

- **8/9が書込み成立**（読み戻し一致）。BBPLL reg7のみ書込み不能
  （0x0bのまま，2試行とも）——読取り専用のステータス様レジスタと判明。
- 結果：**2/2で0 APs found・後続RESCANも0**——scan前注入は**因果棄却**。

### 4. 因果検証(b)＝較正前注入——JTAG方式は実測不能，ソースシード方式で実施——refute（2/2）

- **JTAGブレークポイント方式の断念（実測記録）**：`register_chipv7_phy`
  エントリ（nm実測0x42024ce0）へのbp設置は成立するがヒットせず。診断の
  PCサンプリング（0.3-0.4s間隔のhalt/resume）で，**JTAG haltを繰り返す
  だけでブートのタイムラインが大きく歪む**ことを確認——JTAG無しなら
  t=0.80sで較正完了するのに，halt介入下ではt=6.2s時点でもまだ初期の
  busy-waitループ（0x4202171c/20）内だった。サブ秒の較正窓を「乱さずに
  較正前で止まる」ことはこのハーネスでは原理的に不可能と判断。
- **代替＝ソースレベルシード**：`esp32c5_r36_regi2c_seed()`（
  `target_kernel_impl.c`，`ESP32C5_R36_REGI2C_SEED`ガード付き・既定
  無効）を新設し，appの`esp_wifi_init`直前（＝タスク定義の点(b)
  「較正前（hardware_init_hook後）」そのもの）から呼ぶ。書込み成立は
  即時読み戻しを`esp32c5_r36_seed_readback[]`に記録しsyslogで出力。
  - ★ハマった点（記録）：初回は`hardware_init_hook()`末尾から呼んだが
    readback配列（.bss）がカーネル起動時のbssクリアで消えて確認不能
    ——app側へ移動して解決（タスクの点(b)定義とも一致する位置）。
  - 結果（`build/c5_r36_seed`，独立2ブート）：**BBPLL reg4/reg9は
    較正前の時点で書込み成立を読み戻し確認**（0x23/0x03）。BBPLL reg7
    は(a)と同じくRO。**DIG_REGはこの時点では3レジスタとも0xFF応答**
    （I2C0/host0系のslave電源がblobのphy_enable内でしか上がらない
    ためと推定——hardware_init_hookのPMU_RF_PWC PERIF_I2Cパルス
    ［実施31移植分］だけでは不足）＝書込み不成立。0x63/0x6bは読み
    戻し0x00＝成立不確定。
  - **2/2で0 APs found**——着弾確認済みのBBPLL reg4/reg9について
    較正前注入も**因果棄却**。DIG_REG/0x63/0x6bの較正前注入は
    「着弾させられない」ため未検証（正直な限界として記録。ただし
    5節の再分類によりこれらの優先度自体が下がった）。

### 5. ★重要な再分類：0x63/0x6b/BBPLL系の「系譜決定的差分」は較正の生きた出力

シードビルドの定常状態読み（JTAG，`r36_seedcheck.py`）で決定的な
追加データが得られた：

| レジスタ | 冷間r34(2ブート) | seedビルド(3ブート) | ハンドオフ(2ブート) |
|---|---|---|---|
| UNK_63 reg3 | 0x65, 0x65 | 0x74, 0x4c, 0x4f | 0x68, 0x68 |
| UNK_63 reg4 | 0x00, 0x00 | 0xe3, 0xe3, 0x1c | 0xc0, 0xc0 |
| UNK_6b reg2 | 0x74, 0x74 | 0x53, 0x53, 0x53 | 0x54, 0x53 |
| BBPLL reg7 | 0x0b, 0x0b | 0x0d, 0x0d, 0x0d | 0x0d, 0x0d |
| BBPLL reg4 | 0x27, 0x27 | 0x27（注入0x23が較正で復帰） | 0x23 |

- **UNK_63 reg3/reg4は同一ビルド内のブート間でも変動**（0x4c vs 0x4f・
  0xe3 vs 0x1c）＝比較器ベースの探索出力（rigor doc第1反復と同型）。
  4-wayで「決定的」に見えたのは，同一バイナリ＋同一タイムラインの
  擬似決定性だったと判断——**系譜キーではなく較正の出力**。
- **UNK_6b reg2（txcap）＝0x53（ハンドオフと同値域）で0 APs**が
  seedビルドで3回再現——txcap値そのものがRXキーである可能性を
  さらに直接的に棄却（実施18-20の系譜の補強）。
- **BBPLL reg7＝0x0d（ハンドオフ値）で0 APs**も同様に棄却。
- 総合：**9語の差分のうち「動く/動かない」を規定する静的キーは
  1つも無い**——差分は全て（i）較正探索の生きた出力（成功較正と
  失敗較正で分布が違うのは当然＝下流の症状），（ii）RO/動的レジスタ，
  （iii）既知の自己shim差，に帰着した。

### 6. 手順3（パルス列）＝ソース監査で欠落ゼロ（加算実験の対象なし）

stockブート列〜esp_wifi_init()のWiFi関連リセット/イネーブルパルスを
esp-idf-v6.1全域＋ASP3が実際にリンクするhal submodule側の両方で監査
（サブエージェント，60ツール呼出し）：

- **C5のWiFi initパスに存在する真の「assert→deassertパルス」は
  `MODEM_SYSCON_MODEM_RST_CONF.rst_wifimac`の1つだけ**で，これは
  `wifi_reset_mac_wrapper()`→`modem_clock_module_mac_reset()`として
  **ASP3側に既に同一実装あり**（`esp_wifi_adapter.c:695-699`，
  osi_funcs._wifi_reset_mac登録済み・呼出しタイミングはblob内部＝
  両プラットフォームで構造的に同一）。実リンクされるhal submodule版
  とesp-idf-v6.1版がバイト同一であることも確認。
- レガシー`periph_module_reset()`はC5では`__PERIPH_CTRL_ALLOW_LEGACY_
  API`対象外＝no-op。`esp_wifi_bt_power_domain_on()`のRTC_CNTLパルスは
  `SOC_PM_MODEM_PD_BY_SW`未定義でC5ではコンパイルされない（C5はPMU
  ステートマシン方式＝実施22/23/35で調査済みの領域）。
  `modem_syscon_ll_reset_wifibb`はS31専用・`reset_fe`は呼出し元ゼロ。
  `esp_system_reset_modules_on_exit()`の大規模パルス列はsoft-restart/
  panic専用でPOR/RTSブートでは実行されない。
- **判定：移植すべき欠落パルスは存在しない**——加算実験は対象なしで
  完了（vacuous）。コードパス側からも「パルス列不足」仮説は棄却。

### 7. 総合判定と申し送り

- **regi2cでアドレス可能な範囲の観測（8block全レジスタ・4-way・検証
  ゲート付き）と介入（scan前/較正前の両タイミング・読み戻し確認付き）
  は本ラウンドで消尽した**。検出された差分は全て非因果（棄却）または
  較正出力（症状）と分類でき，AP検出（主目標）は未達。
- 残る説明領域は実施35総括から変わらず，さらに絞られた形：
  (i) regi2cでも読めない/書けないアナログ状態（blobのphy_enable内で
  しか電源が上がらないI2C0系slaveドメインに書込み専用トリムがある
  可能性は残る——DIG_REGの較正前シードが着弾しなかった事実はこの
  ドメイン構造の傍証），(ii) blob内部の非ソース可視なシーケンス。
  **「ソフト到達可能だが観測不能な状態」の最終局面に到達——ハンド
  オフ運用を暫定解としつつ，Espressif問い合わせへ進む判断材料は
  揃った**と評価する（最終判断はユーザー）。
- 残余の具体的な次の一手（もし継続する場合）：
  1. DIG_REG（I2C0/host0系）のslave電源をblobのphy_enable相当の
     PMU_RF_PWC全ビット（XPD_TXRF_I2C等）まで手動で上げてから
     較正前シードを再試行する（本ラウンドは PERIF_I2Cのみで不足）。
  2. ハンドオフ側で較正**中**のregi2c書込み列を実施16のトレースで
     再採取し，冷間と較正中の**書込みシーケンス**（値でなく順序）を
     比較する——「静的値」比較はもう尽きたが「較正中の軌跡」比較は
     成功較正vs失敗較正として未実施。

### 8. C5#1最終状態（終了処理）

- `build/c5_r34_wifi80`（240MHz構成・実施35出荷時ガード構成）を
  ソース変更込みで再ビルド（objdump -d全文比較で**旧バイナリと
  disassembly完全一致**＝r36ガードの不活性を証明，r36シンボル無し
  も確認）し，フル4MB@0x0へ書き戻し。RTSリセット後30秒キャプチャ
  （`r36/final_state_console.log`）：POWERON以外のrst無し・
  `0 APs found`→`RESCAN 0 APs`——既知症状のまま退行なし。
- C5#2・C6 board C（`14:C1:9F:E0:5A:9C`）・UARTブリッジ`125a266b...`：
  完全未接触。DUTは常にUARTブリッジ`b04e3bcf...`（本セッション
  /dev/ttyUSB0）／JTAG `D0:CF:13:F0:A7:44`（/dev/ttyACM1）をMAC/
  シリアル照合の上使用。
- 環境メモ：esptoolのpython_envパスが本セッションから
  `~/.espressif/python_env/idf6.1_py3.12_env`に変わっていた
  （旧`~/tools/espressif/python_env/...`は消滅）。ninjaは
  `~/.mcuxpressotools/ninja-1.12.1/ninja`（要PATH追加）。

### 変更ファイル（実施36）

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r36_regi2c_seed()`＋`esp32c5_r36_seed_readback[]`を
  `ESP32C5_R36_REGI2C_SEED`ガード付きで追加（既定無効・診断専用，
  C5#1個体の実測値焼付けのため恒久有効化禁止の旨コメント明記）。
  既定ビルドへの影響ゼロはobjdump全文比較で証明済み。
- `apps/wifi_scan/wifi_scan.c`：同ガード配下で`esp_wifi_init`直前の
  シード呼出し＋readback syslogを追加。
- `docs/c5-bringup.md`：本節追記。
- ビルド（gitignore対象）：`build/c5_r36_seed`。
- スクラッチ（`260d98fa…/scratchpad/r36/`）：`r36_sweep.py`（4-way
  スイープ＋gate＋非摂動確認の一体ハーネス）・`r36_diff.py`・
  `r36_inject.py`（点(a)）・`r36_inject_early.py`（点(b) JTAG版，
  不成立の記録）・`r36_seedcheck.py`・`uart_capture_ts.py`・
  `*.result.json`／`*.uart.log`／`*.inject_result.json`一式・
  `writeup_draft.md`。ハンドオフ資産は実施35の`stock_scan/`
  （headjump版scan.c）＋`r36_guest_640k.bin`（`build/c5_r35_handoff_
  guest`再ビルド後の640KB切詰め）を再利用。
- git commitは行っていない（指示どおり）。

### 検証（実施36）

- known-answer gate：4/4ブートPASS（差分マスク検証込み）。
- 非摂動確認：4/4ブートで採取後の同一ブートRESCANが期待AP数で完走。
- 注入の読み戻し確認：点(a)=8/9成立（BBPLL reg7はROと判明），
  点(b)=BBPLL reg4/reg9成立・DIG_REG不達（0xFF応答）を記録。
- 再現性：点(a) 2/2・点(b) 2/2・ハンドオフ陽性対照2/2（20 APs）・
  冷間ベースライン2/2（0 APs）。
- 最終状態：objdump一致証明＋実機UART 30秒確認（退行なし）。

---

## 実施37：Codex round3計画のH1（PCR XTAL周波数信念フィールド）・H2（SRAM近傍16KB diff）を実機判別——**両方棄却/消尽**（H1＝冷間/ハンドオフとも48MHz一致，機序は生きているが値が分岐しない。H2＝系譜決定的差分184語中145語はスタック残渣ノイズ，vtableルートは完全一致，安全に注入検証できた17語は2/2で因果棄却）。AP検出は未達のまま手順4（申し送り）で終了

コーディネータ指示＝`tmp/codex_c5_round3_output.txt`のH1/H2判別計画（行523〜640）
を優先順位どおり実行。C5#1（`D0:CF:13:F0:A7:44`，UARTブリッジ`b04e3bcf…`）
のみ使用，禁止個体には未接触。

### 1. H1判別：`PCR_SYSCLK_CONF_REG`(0x60096110)の`PCR_CLK_XTAL_FREQ[30:24]`

**事前デスクチェック（advisor指摘，実機起動前に実施）**：

1. 実施32の既存JTAG実測（本ラウンド以前に別目的で採取済み）を再解釈。
   `PCR_SYSCLK_CONF_REG`は実施32で既に読まれていた（ただしbits[17:16]の
   `soc_clk_sel`のみに着目・bits[30:24]は未解釈のまま放置されていた）：
   - ASP3較正ハング状態（実施32時点，較正キー未修正の冷間Direct Boot）
     ＝`0xb0000200`→`(v>>24)&0x7F`＝**0x30＝48**
   - stock完走後＝`0xb0030200`→同様に**0x30＝48**
   - つまりH1が予測する「冷間=40（0x28）」は，調査史上一度も観測されて
     いない——遡って再解釈しただけでH1の中核予測が崩れている。
2. `nm -u`によるblob消費経路の確認（機序ゲート，Codexのコマンドどおり）：
   ```
   riscv32-esp-elf-nm -u -A libphy.a libpp.a | egrep -i 'xtal|clk|freq|...'
   ```
   → `libphy.a:phy_init.o: U rtc_clk_xtal_freq_get` を検出——**機序は
   生きている**（blobは実際にこの関数を呼ぶ）。さらにASP3側のソース監査
   （`asp3/target/esp32c5_espidf/esp_wifi.cmake:730`のコメント）で，
   ASP3が実際に本物のesp-idf `rtc_clk.c`（`rtc_clk_xtal_freq_get()`の
   実装そのもの）をリンクしていることを確認——これは`clk_ll_xtal_get_
   freq_mhz()`経由でまさに`PCR.sysclk_conf.clk_xtal_freq`（＝当該
   レジスタ・当該ビット）を読む。**機序ゲートはPASS**（H2ではなくH1側の
   経路が実在する）。

**実機確認（新規ハーネス`r37/r37_h1_read.py`，実施36の`OOCD`/UART採取
パターンを流用，delay=15.0sの steady-state RESCANループ内で単発
mdw×2語読み——実施36と同じ「同一停止点」規約）**：

| 系譜 | ブート | `PCR_SYSCLK_CONF` | `XTAL_FREQ[30:24]` | `soc_clk_sel[17:16]` | `EFUSE_XTAL_48M_SEL[11:9]` | popcount奇数(48MHz) | scan AP数 |
|---|---|---|---|---|---|---|---|
| 冷間ASP3 | boot1 | 0xb0030200 | **48** | 3 | 1 | True | 0 |
| 冷間ASP3 | boot2 | 0xb0030200 | **48** | 3 | 1 | True | 0 |
| ハンドオフ | boot1b | 0xb0030200 | **48** | 3 | 1 | True | 18 |
| ハンドオフ | boot2 | 0xb0030200 | **48** | 3 | 1 | True | 20 |

（`soc_clk_sel=3`は実施34修正済みの240MHz構成が両系譜で健在なことの
副次確認。eFuse`EFUSE_XTAL_48M_SEL`は両系譜・全ブートで完全一致＝
不変のはずの値が実際に不変——読み取りプロトコル自体の健全性の傍証。）

**判定：H1棄却**——冷間/ハンドオフとも`PCR_CLK_XTAL_FREQ`は48（0x30）で
完全一致。タスク定義の判定基準（「両方48なら H1棄却」）に該当。

**H1棄却の意味の精密化（advisor指摘）**：これは「このPCRフィールドが
RXキーの担い手ではない」ことを示すのみで，「blobのどこにも40MHz前提が
存在しない」ことまでは意味しない。実施34はROMがBBPLLを40MHzプロファイル
で誤って較正していた事実を確定させており，それとは矛盾しない——単に
本ラウンドが判別対象にした特定のPCRフィールドが担体ではない，という
狭い主張に限定する。

### 2. ハマった点（記録）：ハンドオフflashの罠——ゲストバイナリが
`0x200000`から消えていた

`stock_scan`（bootloader/partition-table/scan.bin，標準3オフセット）を
再flashしてハンドオフ1回目を読んだところ，**CPU_LOCKUP（`rst:0x1a`）
が約300ms周期で139回連続**する未知の症状に遭遇——scanへ到達せず
AP検出ゼロ（UARTログに`APs found`一切なし）。

原因を`asp3_jump.c`のソースで確認：ASP3ゲストイメージは`esptool
write-flash`の対象に含まれない**別オフセット`0x00200000`に生バイナリ
として置かれ**，`asp3_jump_now()`がそこへ直接MMUマップしてジャンプする
設計（実施26由来）。実施36の終了処理（手順6，本ラウンドの手順6と同じ
「フル4MB@0x0への書き戻し」）で`build/c5_r34_wifi80`をオフセット0x0
から書き込んだ際，この4MB範囲が`0x200000`のゲストバイナリ領域を含んで
おり**上書き/消去していた**（cold ASP3バイナリ自体は0x200000領域まで
達しないため中身は不定＝実質ジャンプ先が破壊されたデータになっていた）。

対処：スクラッチ資産`r36_guest_640k.bin`（655360バイト＝0xA0000，
実施36が`build/c5_r35_handoff_guest`から再作成した固定サイズ切詰め版）
を`0x200000`へ単独書き込みし直した——以後ハンドオフは正常再現
（18AP・20AP，実施35/36の20-25APベースライン範囲内）。

**教訓（今後のラウンドへの申し送り）**：手順6「フル4MB@0x0書き戻し」を
実行すると，次回セッション開始時にハンドオフ資産（`0x200000`のゲスト
バイナリ）が毎回消える。ハンドオフ検証を行う回では，`stock_scan`の
3オフセットに加えて**必ず`r3N_guest_640k.bin`（または同等の最新
ゲストバイナリ）を`0x200000`へ別途書き込むこと**をチェックリスト化
すべき（`docs/c5-bringup.md`の「実機手順の罠早見表」へ追記予定）。

### 3. H2：SRAM `0x4085c000..0x40860000`（16KB）の冷間/ハンドオフdiff

H1棄却を受けてCodex計画の手順3へ進行。同一停止点（delay=15.0s，実施36と
同じsteady-state RESCANループ内，H1判別で使ったのと同じ規約）で，
`r37/r37_sram_dump.py`（実施22の64語チャンクmdwパターンを流用）により
16KB＝4096語を4ブート（冷間×2・ハンドオフ×2，スワップ間で上記の
ゲストバイナリ罠を踏んで復旧）ダンプした。

**系譜決定的差分（両条件とも2ブート一致，かつ冷間≠ハンドオフ）＝184語**。
advisor指摘の2段フィルタを適用：

1. **ROM起動スタック領域（`<0x4085e5a0`，Codex/soc.hが定めるスタック
   トップ）を除外**——145語がこの範囲に該当し，かつ**同一プラット
   フォーム内の2ブート間でも安定しない**（冷間内boot-stable率
   2058/4096・ハンドオフ内2344/4096——半分近くがそもそも同一系譜内で
   再現しない）。これは実機手順の罠早見表と同型の「スタック残渣ノイズ」
   で，advisorが事前に指摘した除外対象そのもの。
2. **残る39語（`>=0x4085e5a0`，named ROM SRAM APIゾーン）**を
   `esp32c5.rom.{ld,pp.ld,phy.ld,net80211.ld,coexist.ld,spiflash.ld,
   libc.ld}`の全PROVIDEシンボルと突合：
   - **最重要のvtableルート（`g_osi_funcs_p`/`g_coa_funcs_p`/
     `coex_env_ptr`/`net80211_funcs`/`phy_rom_phyFuns`/`g_ic_ptr`/
     `g_scan`/`g_chm`等）は全て冷間/ハンドオフで完全一致**——実施26/27
     で修正済みの「stock残置ポインタの読み違え」バグがぶり返して
     いないことの確認にもなった。
   - 39語中17語は`asp3_jump.c`の`ROMIF_WIFI_PTRS_START..END`
     （`0x4085fb80..0x4085ffc8`）ジャンプ前ゼロクリア範囲内で，
     **ハンドオフ側は0のまま**（cold=不定の残置値）。ハンドオフは
     この状態のままAP検出に成功しているため，**この17語がゼロで
     あることはRX成立の必要条件ではない**という構造的論拠がある
     （後述の因果検証で直接確認）。
   - 4語（`g_timer_func`/`g_config_func`/`s_encap_amsdu_func`/
     `s_michael_mic_failure_cb`）は両条件とも非ゼロで，同種の
     flashマップ済みコードアドレスを指す関数ポインタ——**advisor
     指摘で判明した重要な訂正**：cold−handoffの差分は4組全てで
     厳密に同一の`0x2AE`（686バイト）：
     `0x42031056-0x42030da8=0x2ae`／`0x4202e634-0x4202e386=0x2ae`／
     `0x420407a4-0x420404f6=0x2ae`／`0x420369e0-0x42036732=0x2ae`。
     これは「MMUマッピングの違い」ではなく，**冷間とハンドオフ
     ゲストが別ビルド**（冷間=`build/c5_r34_wifi80`，ハンドオフ
     ゲスト=`build/c5_r35_handoff_guest`，実施36の記録どおり
     headjump版scan.c向けの別ターゲット）であるために生じる
     **一定のコードレイアウトシフト**——同一コールバック関数が
     単純にビルド間でオフセットしているだけであり，**注入検証を
     要さずに非因果と結論できる**（同一機能への単なる別アドレス，
     値の一致/不一致に意味的な因果関係はない）。当初「同一
     ソフトウェア/same ASP3 binary」と記述したのは不正確——冷間/
     ハンドオフ双方とも**ASP3自身の起動処理・osi_funcs登録ロジック
     は同一ソースだが，リンクされたビルド成果物（アドレス配置）は
     別**という点を正確に記録する。★この点はSRAM diffの解釈にのみ
     影響し，ハンドオフ実験そのもの（stockが確立した状態を保持
     したままASP3を走らせるにはHANDOFF_SKIP_CLOCK_INIT等の再初期化
     省略が本質的に必要）の妥当性は損なわない。
   - 残り18語（`syscall_table_ptr`/`g_flash_guard_ops`/未命名の
     `0x4085f4f0`等）はいずれも**ハンドオフ側のみ非ゼロでstockの
     flashマップ済みアドレスを指す**——`asp3_jump.c`のコメントが
     警告する「ASP3が触れないアドレスに残るstock残渣」そのものと
     判断（ASP3自身の`.bss`（`__bss_start=0x40801d20`〜
     `__bss_end=0x408492b8`）はこの範囲の外）。

**機序ゲート（objdump参照カウント，`s_running_phy_type`のみ精査）**：
appのELF全体を逆アセンブルし`0x4085fc88`への参照を検索したところ
**唯一の参照は書込み（`sb`）1件のみ，読出しは0件**——リンク済み
コード全体でこの値を読む箇所が存在しない（少なくともELFにリンクされた
オブジェクトの範囲では）。他の未命名アドレスは`lui`+即値オフセット
分割アドレッシングのため単純な文字列grepでは網羅的に確認できず，
機序ゲートは`s_running_phy_type`一点の確認に留めた（正直な限界）。

**因果検証：17語ゼロクリア注入（安全なサブセットのみ，delay=15.0の
JTAG halt→注入→読み戻し確認→resume→以後のRESCAN観測）**——
`r37/r37_inject_zero.py`，冷間ASP3で2回独立試行：

- trial1：17/17語の書込み・読み戻し確認成功→以後`RESCAN 0 APs`
- trial2：17/17語の書込み・読み戻し確認成功→以後`RESCAN 0 APs`

**2/2で因果棄却**。関数ポインタ4語は上記の`0x2AE`定数シフトにより
ビルド差の産物と確定＝**注入不要で非因果**。flashマップ済みポインタ
18語（`syscall_table_ptr`等）は，stockのflashマッピングに依存する
アドレス値をそのまま冷間へ注入すると意味不明のコードへジャンプする
危険があるため，安全な注入手段が無いと判断し**注入は見送り**（正直な
限界として記録——ただし機序ゲートと構造的論拠［ハンドオフ自身が
これらをstock残渣のまま動作している事実］により，これらが単純な
「値の一致/不一致」型のRXキーである可能性は低いと判断した）。

**★正直な限界（advisor指摘）**：本節の「系譜決定的差分184語」という
カウント自体，上記の`0x2AE`ビルドシフトのような**ビルドレイアウト
起因の差分で水増しされている**（冷間とハンドオフゲストは同一
ソースだが別ビルド成果物）。今回の結論は「非因果」という**否定
結果**であり，この水増しがあっても結論の正しさは揺るがない
（vtableルート一致・17語の直接因果棄却は水増しと無関係に成立する
一次証拠）。ただし将来このデータを**肯定結果**（「この差分が原因」）
の根拠に使う場合は，ビルドレイアウトシフトの候補（同一ソースの
コールバック関数群が定数オフセットで並ぶパターン）を必ず除外
してから判断すること。

**参考チェック（plan手順5，IDF staticsの個別比較は省略可と確認）**：
`esp_clk_tree_initialized`（`0x40801df8`）・`s_pll_src_cg_ref_cnt`
（`0x40833978`）をnmで実アドレス特定したところ，いずれもASP3自身の
`.bss`範囲内（`0x40801d20`〜`0x408492b8`）に収まることを確認——
`asp3_jump_now()`のジャンプ先であるASP3自身の起動処理が毎回無条件に
この範囲を再初期化するため，**冷間/ハンドオフで構造的に同一になる
ことが保証されており，個別比較は不要**（advisor指摘の再ゼロ化論拠を
実アドレスで裏取り）。

### 4. H2の総合判定：**16KB窓では棄却/消尽**——手順4（申し送り）へ

- 系譜決定的差分184語のうち145語（79%）はスタック残渣ノイズとして
  除外，残り39語のうち安全に注入検証できた17語は2/2で因果棄却，
  vtableルート（最重要候補）は完全一致で差分なし，残る22語は
  「注入不可能なほど危険」または「非因果と構造的に論証済み」に
  帰着し，**新規のRXキー候補は発見できなかった**。
- Codexの推奨範囲（`0x4085c000..0x40860000`，16KB）に限れば，
  H2はMMIO（実施35）・regi2c（実施36）と同じ「探索したが空振り」の
  結論に達した。広め範囲（`0x4084e000..0x40860000`）やSRAM全域は
  時間予算の制約で本ラウンドでは未実施（申し送り）。

### 5. 総括（申し送り，手順4）

本ラウンドでCodex round3計画のH1/H2を両方実施し，**AP検出（主目標）は
未達**のまま終了する。MMIO（実施35）・regi2c（実施36）・PCR XTAL信念
フィールド（本ラウンドH1）・SRAM近傍16KB窓（本ラウンドH2）と，
「同一ソフトウェア差分」手法で読み取り可能な状態空間を実施34以降
4ラウンド連続で系統的に走査し，**いずれも「差分はあるが全て非因果，
または冷間/ハンドオフの構造的な違い（スタック履歴・zeroクリア設計・
MMU依存ポインタ）で説明可能」という同型の結論**に達している。

実施12以来の壁の最終内訳（本ラウンド時点の整理）：

1. CLICベクタ出口（実施02）
2. コンソール割込みソース誤配線（実施02）
3. mcause多重フォルト（実施06/07）
4. eco revブロブ不整合（実施08/09/10）
5. osi_funcs欠落フィールド（実施11/12）
6. WIFIBBクロックICGゲート（実施13）
7. LP_WDT_SWD解錠キー誤り（実施33，「借用値・実機未確認」というコメント
   付き既知の罠が実際に発火）
8. CPUルートクロックXTAL直結のまま（実施32，較正キー(A)の本体）
9. BBPLL 40MHzプロファイル誤較正（実施34，480×1.2=576MHz相当）
10. regi2cマスタ自体のICGゲート（実施34，副次発見）
11. **PCR XTAL信念フィールド＝本ラウンドH1で明示的に判別・棄却**
    （冷間/ハンドオフとも48で一致——ROM/Direct Bootが「40MHz時代の
    既定値」を実際に借用していたのは9番（BBPLL）のみで，PCRの
    このフィールド単体は関与していないと確定）

共通テーマは「ROM/Direct Bootの40MHz時代の既定値と借用値」で概ね
説明できるが，**9番（BBPLL）を修正した後も0AP症状は解消しておらず**
（実施34で確定済み），本ラウンドのH1棄却はこのテーマの残りの候補を
一つ削っただけで，「40MHz前提がblobのどこかに他にもまだ残っている」
可能性そのものを閉じるものではない（advisor指摘どおり，狭い主張に
限定する）。

C6への示唆：C6はXTAL固定40MHz（PCR相当フィールドの分岐が原理的に
存在しない）ためH1相当の判別は非該当。ただし「ROM/Direct Bootの
既定値を無検証で信頼しない」という総点検の視点自体はC6の85ラウンド
調査（`memory/project_c6_agc_investigation.md`）とも整合する教訓であり，
C5がPCR/eFuse/BBPLL/regi2c ICGの4件で「借用値」を発見できたのに対し
C6側でこの視点からの再監査が行われたかは未確認——将来のC6再開時の
チェック項目として記録する。

残作業：
- BBPLL 40MHzプロファイル問題（実施34）とH1棄却の関係は，「BBPLLの
  修正はH1の判定と独立に必要（すでに実施34で適用済み・本ラウンドは
  それを前提に実施した）」という整理で確定——統合上の矛盾はない。
- H2はCodexの推奨16KB窓に限定した探索であり，広め範囲・SRAM全域は
  未実施。次段の候補として残す（優先度は中——vtableルート一致という
  強い反証的シグナルがあるため，期待値は実施35/36と同程度に低い
  と評価する）。
- 4語の危険関数ポインタ（`g_timer_func`等）・18語のflashマップ済み
  stock残渣ポインタは「注入不可能」という限界を残したまま——安全な
  注入形式（例：ASP3自身の対応するflashマップ済みアドレスへ変換して
  から注入する等）を考案できれば因果検証を完結できる可能性はあるが，
  費用対効果は不明（同種のvtableルートは既に一致しているため）。
- **Espressif問い合わせ判断の材料**：MMIO・regi2c・PCR信念フィールド・
  SRAM近傍16KB窓の4系統の「読み取り可能な状態空間」がいずれも空振り
  に終わったことで，「blobを外部から読み書きできる範囲には冷間/
  ハンドオフを分ける静的キーが存在しない」という主張の裏付けが
  さらに1系統分厚くなった。残る説明領域は実施36総括から変わらず
  ①regi2cでも読めない/書けないアナログ状態（phy_enable内でのみ
  電源が上がるI2C0系slaveの書込み専用トリム）②blob内部の非ソース
  可視な実行時シーケンス——のいずれかであり，これは静的な状態比較
  では原理的に到達できない領域である。最終判断（Espressif問い合わせ
  へ進むか，継続探索か）はユーザーに委ねる。

### 6. C5#1最終状態（終了処理）

- `build/c5_r34_wifi80/asp_flash.bin`（240MHz構成，実施36と同一
  バイナリ・本ラウンドで一切のソース変更なし——`git status`で
  `docs/c5-bringup.md`のみ変更済みと確認）をフル4MB@0x0へ書き戻した
  （`r37/flash_final.log`）。
- RTSリセット後35秒キャプチャ（`r37/final_state_console.log`）：
  `rst:0x1 (POWERON)`のみ・クラッシュ/リブート無し・`wifi_scan:
  esp_wifi_init -> 0`・`esp_wifi_scan_start -> 0`・`0 APs found`→
  `RESCAN 0 APs`——既知症状のまま退行なし。
- **`test_porting`はN/A（意図的省略，オミッション扱いしないための
  明記）**：本ラウンドはJTAG/UART計装のみで`asp3/target/esp32c5_espidf/`
  等のソースを一切変更していない（`git diff`で確認済み）ため，最終
  flashバイナリは実施36終了時点（`test_porting # 6/6 passed`確認済み）
  とバイト同一——回帰の可能性が原理的に無く，再実行は情報を持たない。
- ハンドオフ用ゲストバイナリ（`0x200000`）は本ラウンドの最終フル
  4MB書き戻しにより再び消去された——次回ハンドオフ検証時は
  2節の罠に従い`r36_guest_640k.bin`（または最新版）を`0x200000`へ
  別途書き込むこと。
- C5#2・C6 board C・禁止UARTブリッジ：完全未接触。

### 変更ファイル（実施37）

- `docs/c5-bringup.md`：本節追記（H1/H2判別・因果検証・総括）。
- スクラッチ（`260d98fa…/scratchpad/r37/`）：`r37_h1_read.py`
  （H1判別ハーネス）・`r37_sram_dump.py`（H2 SRAM 16KBダンプ）・
  `r37_inject_zero.py`（H2安全サブセット因果検証）・
  各種`.result.json`/`.sram.txt`/`.sram.meta.json`/`.uart.log`
  一式・`flash_*.log`。
- git commitは行っていない（指示どおり）。

### 検証（実施37）

- H1：冷間2/2・ハンドオフ2/2，全て`PCR_CLK_XTAL_FREQ=48`で一致
  （事前デスクチェック［実施32既存データ再解釈］とも整合）。
- H2 SRAM diff：冷間2/2・ハンドオフ2/2のダンプで系譜決定的差分を
  確定，安全な17語サブセットの因果検証2/2で棄却。
- ハンドオフ陽性対照：本ラウンドのべ4回（18/20/18/20 AP，20-25AP
  ベースライン近傍）——ゲストバイナリ罠の発見・復旧を含む。
- 冷間ベースライン：本ラウンドのべ4回（全て0 APs，既知症状の再現）。
- 最終状態：`git diff`でソース無変更を確認＋実機UART 35秒確認
  （退行なし）。

---

## 実施38：候補0（外部AI回答・2×2判定）——C5#2をスニファへ転用し，冷間0APの真因を初めてTX/RX軸で直接分離。**結果＝TX無・RX無（C6 deaf-RXと同一バケット）**——37ラウンド追ってきた「RX側較正チェーン」の外側，TX/RX共通のフロントエンド/シンセ段が疑わしいと判明

コーディネータ指示＝`tmp/external_ai_c5_answer_fable.md`候補0の実行。
C5#2（`D0:CF:13:F0:C8:94`）を**ユーザーが2026-07-13に直接承認**した役割変更で
スニファへ転用し，C5#1（`D0:CF:13:F0:A7:44`）のscan中probe requestが空中に
出ているか（TX）・promiscuousコールバックが発火するか（RX）を初めて直接測る。

### 0. 環境・機材

- C5#1（DUT）＝`D0:CF:13:F0:A7:44`／ttyACM1／UARTブリッジ`b04e3bcf…`＝ttyUSB0。
  着手時flash＝`build/c5_r34_wifi80`（240MHz出荷構成，実施34以降の標準）。
- C5#2（スニファ，**役割変更をユーザー承認済み・2026-07-13**）＝
  `D0:CF:13:F0:C8:94`／ttyACM2／UARTブリッジ`3e7bd19f…`＝ttyUSB2。
  着手時flash＝旧stock scanサンプル（上書き）。
- 接触禁止（C6 board C `14:C1:9F:E0:5A:9C`／ttyACM0，UARTブリッジ`125a266b…`／
  ttyUSB1）は全操作で無接触（by-idピン留め徹底，操作前後にudevadm照合）。
- 全UART通信はCP2102Nブリッジ（ttyUSB0/ttyUSB2）のみ使用——ttyACM（native
  USB-JTAG CDC）はopenで`rst:0x15`を誘発する既知罠（実施21）のため未接触。
  JTAG/OpenOCDも本ラウンドは不使用（advisor助言どおり，ファームウェア内
  UART計装のみで完結）。

### 1. スニファアプリ（C5#2，IDF v6.1 native，スクラッチ`sniffer/`・`sniffer5g/`）

`esp_wifi_set_promiscuous`+`WIFI_PROMIS_FILTER_MASK_ALL`で全種別を受信し，
802.11フレームコントロール（type/subtype）を自前パースして
全フレーム数／mgmt数／probe request数／beacon数（環境陽性対照用）と，
送信元MAC＝DUT（`d0:cf:13:f0:a7:44`）のprobe request数・全フレーム数を
1秒周期でUARTへ出力する。帯域・チャンネルはビルド時定数固定
（2.4GHz版＝ch6，5GHz版＝`esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY)`＋ch36）。
5GHzチャンネルは事前サーベイ（36〜165の主要チャンネルを1.5秒ずつhop）で
ch36/ch48が安定して強い環境beacon活性を示すことを確認して選定
（ch36＝UNII-1，非DFS＝active scan常時許可）。

**環境陽性対照（両帯域とも成立を確認してからDUT測定に進む，鉄則どおり）**：
- 2.4GHz ch6：起動後13秒で total=1460 mgmt=1232 probereq=19 beacon=956
  （継続的に増加，ambient probe/beaconの安定検出を確認）
- 5GHz ch36：起動後13秒で total=1332 mgmt=717 probereq=22 beacon=454
  （同様に安定検出）

### 2. DUT計装（`apps/wifi_scan/wifi_scan.c`，新規`ESP32C5_R38_RXINSTR`ビルドフラグ）

既存の3秒promiscuous対照窓（実施12由来）をscan実行中も維持するよう変更
（`esp_wifi_set_promiscuous_filter(ALL)`を明示設定した上でscan完了後もdisableしない）。
新規`r38_rxinstr_cyclic_handler`（実施24の`wifi_diag_cyclic_handler`と同型，
`TNFY_HANDLER`＝タスク非依存の1秒周期CRE_CYC）で以下をUARTへ出力：
promiscuousコールバック累積数・delta，Wi-Fi MAC割込み総数
（`esp_shim_int_count[1..15]`合計，実施78のC6手法のC5移植）・delta，
トーン自己ループバック測定チェーンの生ADC（`0x600A081C`）とIQ_DONE
（`0x600A047C` bit16，実施20由来）。scan設定はNULL既定（active scan，
show_hidden無効，全チャンネル走査——変更なし，advisor助言により「既知の
20-25APベースラインに新変数を入れない」方針を優先）。

冷間ビルド（`build/c5_r38_cold`）とハンドオフゲスト（`build/c5_r38_handoff`，
`HANDOFF_SKIP_CLOCK_INIT=1`併用，実施35のstock app_main先頭ジャンプ機構を
`stock_scan/`スクラッチ資産で再利用）は**同一ソース**から
`ESP32C5_R38_RXINSTR=1`のみ差分でビルド（実施37が確定した「冷間/ハンドオフ
ゲストは別リンク＝0x2AEオフセット差」は残るが，計装コード自体は同一ソース）。

### 3. 測定マトリクス（各セル2回再現，DUT・スニファ同時二重キャプチャ，
   各70秒，`r38_dualcap.py`でUARTブリッジ2本を同時読み・DUT側のみRTSリセット）

| セル | promisc(DUT) | DUT送信元フレーム(スニファ検出) | MAC割込み | scan結果 |
|---|---|---|---|---|
| 冷間×2.4GHz boot1 | 0（70秒間終始） | 0（probereq含め終始0，同時に環境probereq最大116件検出） | 生きている（2688→7831，バースト状） | RESCAN 0 APs |
| 冷間×2.4GHz boot2 | 0（130秒相当，終始） | 0 | 生きている（→16096） | RESCAN 0 APs |
| 冷間×5GHz boot1 | 0 | 0（環境probereq同時44件検出） | 生きている（→24768） | RESCAN 0 APs |
| 冷間×5GHz boot2 | 0 | 0 | 生きている | RESCAN 0 APs |
| ハンドオフ×2.4GHz boot1 | 生きている（0→1786） | **17**（probereq） | 生きている（896→2068） | RESCAN 22/27 APs |
| ハンドオフ×2.4GHz boot2 | 生きている（→3431） | **39** | 生きている（→4023） | RESCAN 25-29 APs |
| ハンドオフ×5GHz boot1 | 生きている（→4838） | **7〜8** | 生きている（→5738） | RESCAN 23-28 APs |
| ハンドオフ×5GHz boot2 | 生きている（→6754） | **16** | 生きている（→7902） | RESCAN 26-28 APs |

**DUT側陽性対照（advisorが要求した「反対条件で本当に検出できるか」）が
本ラウンドの核**：同一2枚・同一設置・同一アプリコード経路・同一チャンネルで，
ハンドオフ系譜は4セット全てでスニファがDUT送信元probe requestを実検出
（7〜39件）し，promiscuousコールバックも生きている。冷間系譜は同じ2枚・
同じ設置で4セット全てゼロ——**リンクや設置の問題ではなく，DUT（冷間ブート
固有の状態）に原因が局在する**ことを，実施のたびに書き換える必要のない
一つの実験系列で示せた。

トーンADC/IQ_DONE（生存代理指標）：**iq_done16=1が冷間・ハンドオフ両方，
全ブートで終始1**（実施15-20時代の「ASP3のみ生ADCハードゼロ」からの変化——
実施33/34のクロック/WDT修正でこの較正ビットは完走するようになっていた）。
生ADC値自体は各ブート内で単一の静的値に固定され動かない（冷間
`0x0a3a0a44`，ハンドオフ`0x0a3a0a3a`）——両系譜とも「一発較正値が固まって
動かない」点で同型であり，下位バイトの差はブート毎の較正ノイズの域を出ない
（`memory/feedback_hardware_investigation_rigor.md`のdegenerate-comparator
ノイズと同型）。**プラットフォーム差の指標としては不採用**（iq_done16=1と
いう「両系譜とも較正チェーン自体は完走する」事実だけを採用する）。

### 4. 判定

事前固定の判定表（外部AI回答候補0）：

> TX有+promisc≈0＝RX前段（RF/AGC/BB）で死亡／TX有+promisc>0＝MACフィルタ〜
> ソフト集計で死亡／**TX無＝TX・RX共通の前段（synth/FE/BB共通）で死亡**

観測＝**TX無・RX無**（4セル×2回，計8回全てTX/RXともゼロ，DUT側陽性対照が
同一機材・同一手法で4/4成立）。判定は**「TX・RX共通の前段で死亡」**——
これは37ラウンド（実施15〜37）が集中して追ってきた「RX側較正チェーン
（トーン自己ループバック測定）」の**外側**にある可能性を強く示唆する新知見。

ただし観測と機序は区別する（advisor指摘）。本ラウンドが直接示したのは
「冷間ブートはTX電波を一切放射せず，RXも一切フレームを受理しない」という
**観測**であり，「共有フロントエンド／シンセ段が起動しない」は最有力
**仮説**であって直接測定した機序ではない。「FEは上がるがTXパスだけ個別に
死んでいる」という代替説とスニファ無検出という結果だけでは区別できないが，
**両方とも同じ場所（共有FE/シンセ）に収束する**ため，判定の分岐先は変わらない。
最も強く言える一文：冷間scanは`err=0`で「完走」を報告し（`RESCAN 0 APs
(err=0)`），かつ同一チャンネルでハンドオフ系譜は完走scanが確実に電波を
放射する——**「完走を報告するのに何も放射しない」冷間scanは異常であり，
フロントエンドが一度も起動していない可能性を指し示す**。

なお`promiscuous(true)`自体の戻り値ログは，起動直後のsyslogバースト
（既知のsyslog取りこぼしバグ，`memory/feedback_hardware_investigation_rigor.md`
参照）で本ラウンドの捕捉から漏れ，冷間ブートでpromiscuousが実際にarmされた
ことの直接証明は取れていない——ただしRX無の判定はスニファ側で独立に
（`esp_wifi_scan_get_ap_records`が0件を返す＝RESCAN 0 APsという，
promiscuous設定に依存しない別経路の観測）成立しているため，この欠落は
判定を揺るがさない。

### 5. C6 deaf-RXとの定量比較

| 項目 | C5冷間（本ラウンド） | C6 deaf-RX（実施80/81/83） |
|---|---|---|
| TX電波放射 | 無（4/4セル×2回，DUT陽性対照で検出感度は確認済み） | 無（実施81，271件の環境probe対照下でゼロ） |
| RX（promiscuous/lmacRxDone等） | 無（promisc=0終始） | 無（`lmacRxDone` 0/120s） |
| MAC割込み | 生きている（バースト状，1秒窓で0〜256，scanサイクルに同期） | 生きている（定常〜140/s） |
| 較正ビット | iq_done16=1（両系譜とも完走） | （AGC/CCA動的に生存，実施78） |

**同じバケット（TX無・RX無・下位MAC割込みは生存）に入るが，時間パターンは
異なる**——C6は定常的な割込みレート，C5冷間はscanサイクルに同期したバースト
（サイクル中0〜256，サイクル間0）。「同一バケット・同程度の桁」であって
「同一現象」と断定はしない。

### 6. 変更ファイル（実施38）

- `apps/wifi_scan/wifi_scan.c`／`wifi_scan.h`／`wifi_scan.cfg`：新規
  `ESP32C5_R38_RXINSTR`ビルドフラグ（未定義時は実施12以来の挙動と完全互換）。
  promiscuousをscan中も維持するよう変更（フラグ有効時のみ）・
  `r38_rxinstr_cyclic_handler`新設（1秒周期UART計装）。
- 本doc：実施38節追記。
- スクラッチ（`260d98fa…/scratchpad/`）：`sniffer/`・`sniffer5g/`・
  `sniffer5survey/`（IDF v6.1 nativeプロジェクト一式）・`r38_dualcap.py`
  （DUT/スニファ同時二重キャプチャ）・`r38_guest_640k.bin`（ハンドオフ
  ゲスト，実施37罠対応で0x200000へ別途書込み済みだったが，手順6の全域
  書き戻しで意図的に消去——次回ハンドオフ検証時は再作成要）・
  `r38_cold_*`/`r38_handoff_*`の生ログ一式（DUT/スニファ双方）・
  `survey5_out.log`（5GHzチャンネルサーベイ）。
- git commitは行っていない（コーディネータのレビュー後のcommit&push運用）。

### 7. 検証（実施38）

- ビルド：`build/c5_r38_cold`（FLASH 11.79%/RAM 76.23%）・
  `build/c5_r38_handoff`（同型）とも成功。`sniffer`/`sniffer5g`/
  `sniffer5survey`（IDF v6.1 native，esp32c5）とも成功。
- スニファ環境陽性対照：2.4GHz ch6・5GHz ch36とも成立（1節）。
- DUT側陽性対照（ハンドオフ系譜）：4セット全てでpromiscuous生存・
  MAC割込み生存・AP数20-29（既知ベースライン内）・スニファでDUT送信元
  probe request実検出（7〜39件）——本ラウンドの核となる反対条件対照。
- 冷間ベースライン再現：4セット全てTX無・RX無・RESCAN 0 APs（2/2×2帯域）。
- 最終状態：C5#1＝`build/c5_r34_wifi80`（240MHz出荷構成）を`write-flash 0x0`
  でフル4MB書き戻し（実施37罠どおり，これにより0x200000のハンドオフゲストは
  意図的に消去），UART確認で`wifi_scan: 0 APs found (err=0)`・
  `wifi_scan: done`・`RESCAN 0 APs (err=0)`を実測——冷間症状の回帰を確認。
  C5#2＝スニファ（2.4GHz ch6版）のまま維持（次ラウンド以降も転用継続の
  想定，役割変更はユーザー承認済み2026-07-13）。
  接触禁止2台（C6 board C／UARTブリッジ`125a266b…`）は全工程で無接触。

---

## 実施39：外部AI回答Q3判別——ASP3→ASP3セルフハンドオフ（stockコード完全不経由の「POR後2回目のソフト起動」）を実装・実行。**結果＝2周目も0AP・TX無・RX無（V1/V2両変種×各2回，計4/4）**＝「2回目のソフト起動（遷移の履歴）」説を明確に反証，**鍵は「stock ESP-IDFブート列の内容そのもの」と確定**——候補1（過渡ラッチ）の優先度低下，候補3/4（MACブロック実行時diff・regi2c 0x20以降）へ。★新規知見2件：Direct BootはROM残置PMA（エントリ14=HP SRAM全域Xなし）によりRAM実行不可（X付与で解除可，stockはstartupでPMA上書きするため露呈しない）／CP2102N openのDTR assertがRTSリセットをダウンロードモード落ちにする罠

コーディネータ指示＝`tmp/external_ai_c5_answer_fable.md`「Q3：別解釈と判別」1項
（＋候補1前哨）の実行。stockコードを一切経由せず，冷間Direct BootしたASP3から
自分自身（同等のASP3イメージ）へ実施26/29と同一のハンドオフ機構で再ジャンプし，
2周目ASP3のscanでAP検出が復活するかを判別する。

### 0. 環境・機材

- C5#1（DUT）＝`D0:CF:13:F0:A7:44`／UARTブリッジ`b04e3bcf…`＝ttyUSB0。
  着手時flash＝`build/c5_r34_wifi80`（240MHz出荷構成）@0x0，0x200000消去済み
  （実施38終了状態のまま）。
- C5#2（スニファ）＝`D0:CF:13:F0:C8:94`／UARTブリッジ`3e7bd19f…`＝ttyUSB2。
  実施38の2.4GHz ch6スニファfirmwareのまま流用（本ラウンドは書換え一切なし，
  RTSリセットのみ）。
- 接触禁止2台（C6 board C `14:C1:9F:E0:5A:9C`／ttyACM0，UARTブリッジ
  `125a266b…`／ttyUSB1）は全工程で無接触（by-idピン留め徹底）。
- JTAG/OpenOCD不使用（全観測はUART計装＋スニファで完結）。
- git HEAD=149f72e（実施38コミット済み状態）から作業。

### 1. 事前固定の判定表

> - **2周目AP>0（TX/RX復活）**→「stock列は無関係，『遷移の履歴』（クロック/
>   電源が確立された状態からの再初期化，またはPOR後2回目であること自体）が
>   鍵」＝候補1（過渡ラッチ仮説）を強く支持→実施40（ドメインリセット判別
>   3段階）へ
> - **0APのまま**→「stockブート列固有の何か」と確定＝候補1の優先度低下，
>   候補3/4（MACブロック実行時diff・regi2c 0x20以降）へ

補助観測：スニファ（C5#2）で2周目scanのprobe request有無（TX判定），
2周目のpromiscカウンタ・MAC割込み（RX判定）——実施38と同一の観測系。

### 2. 実装：セルフハンドオフ（`apps/wifi_scan/wifi_scan.c`，`ESP32C5_R39_SELFHANDOFF`ガード・既定不活性）

機構は実施26/27/29のstockホスト版（スクラッチ`stock_scan/main/asp3_jump.c`）
のASP3移植：割込み全マスク（`csrw mie,zero`＋`csrci mstatus,0x8`）→証拠
プリント（`g_osi_funcs_p`=0x4085ff60／`g_coa_funcs_p`=0x4085ffb4の生値）→
ROMインターフェースWiFiポインタ域`[0x4085fb80,0x4085ffc8)`のゼロクリア
（実施27，2周目のblob再登録を保証）→MMU全512エントリunmap→
vaddr 0x42000000〜へpaddr 0x200000〜を16ページ（1MB）マップ→
`Cache_Invalidate_Addr`（ROM 0x40000650）→`0x42000008`へ直接ジャンプ。

stockホスト版との唯一の機構差分＝**ASP3はXIP（全コードflash実行）のため，
MMU unmap後に踏む最終段（MMU操作＋cache invalidate＋ジャンプ）をRAM常駐の
自己完結スタブ`r39_jump_stub`に置く**。`.data.r39_jump_stub`セクション
（start.Sの`__idata`コピーでRAMへ展開済み，実測0x40800004）に配置し，
参照はMMIO（SPIMEM0 `ITEM_INDEX`=0x60002380／`ITEM_CONTENT`=0x6000237C，
`mmu_ll.h`のmmu_hal実体と同一のレジスタ・値）とROM常駐関数のみ
（flash .rodata参照なし——逆アセンブルで確認済み）。

呼出し点は2変種（ビルド時に択一）：
- **V1（`=1`）**：起動直後（wifi完全未実行，起動3秒後）にジャンプ＝
  実施29のstock app_main先頭ジャンプと対照が揃う。1周目＝ASP3の
  ブート列相当（hardware_init_hook＋カーネル起動）のみ。
- **V2（`=2`）**：1周目でフルwifi初期化＋初回scan完走後（promiscuous
  disable＋2秒後）にジャンプ＝実施26のstock scan完走後ジャンプと対照が
  揃う。

ホストは両変種とも`ESP32C5_R38_RXINSTR=1`併用（1周目にも実施38計装＝
ベースライン内蔵）。**ゲスト＝実施38ハンドオフゲストと同一バイナリ**
（`build/c5_r38_handoff`＝`HANDOFF_SKIP_CLOCK_INIT=1`+RXINSTR，スクラッチ
`r38_guest_640k.bin`，md5でビルド成果物と一致確認済み）を0x200000へ配置——
stockハンドオフで22〜29APを実測した個体そのもの，という最強の対照。

実装上の落とし穴（正直な記録）：`r39_jump_stub`末尾の無限ループから
GCCがnoreturnを推論し，V1のジャンプ呼出し以降（esp_wifi_init〜scan一式）を
デッドコード除去→wifi blob全体がGCされFLASH 0.43%まで縮小した。volatile
フラグ`r39_arm`ガードで抑止し，ホストを「r38冷間ビルド＋ジャンプパス」の
最小差分に保った（FLASH 11.81%≒r38の11.79%）。

### 3. ★新規知見（機構検証で発見）：Direct BootのROM残置PMAがSRAM実行を禁止

機構検証（判定より先）の初回実行で，スタブ先頭（pc=0x40800004）で
**Instruction access fault**（2/2再現）。Espressif PMA CSR（0xBC0〜/0xBD0〜）
の全16エントリをUART計装でダンプして原因確定：

```
R39PMA[0..11]: cfg=08000000 addr=00000000   （未使用）
R39PMA[12]: cfg=c0000019 addr=140007ff  → NAPOT 0x50000000/16KB  (LP RAM)   R+W+EN, Xなし
R39PMA[13]: cfg=c0000015 addr=1000ffff  → NAPOT 0x40000000/512KB (ROM)      R+X+EN
R39PMA[14]: cfg=c0000019 addr=1020ffff  → NAPOT 0x40800000/512KB (HP SRAM)  R+W+EN, ★Xなし
R39PMA[15]: cfg=c0000015 addr=10bfffff  → NAPOT 0x42000000/32MB  (flash窓)  R+X+EN
```

**エントリ14がHP SRAM全域の命令フェッチを禁じている**（ROMブートローダが
設定して残置。L=0＝非ロック）。ASP3は全コードXIPのため36ラウンド露呈
しなかったが，IRAM_ATTR相当のRAM実行を試みるコードは全滅する。stock
ESP-IDFブート列は`call_start_cpu0`のregion protection設定でPMA全体を自前
構成に上書きしてIRAM実行を可能にしている——だからstockホスト版ジャンプ
（IRAM_ATTR）は動いた。対処＝ジャンプ直前に`csrw 0xBCE, 0xC000001D`
（エントリ14へX付与のみ，範囲・他属性不変，読み戻し確認付き）。
C6のDirect Bootにも同型の残置PMAがある可能性が高い（申し送り）。

修正後の機構検証：1周目バナー→R39証拠プリント→ジャンプ→**2周目バナー
（ゲストビルド16:20:47＝ホスト16:57:13と別バイナリであることをバナー時刻で
確認）→ゲストの`esp_wifi_init -> 0`→scan完走**まで成立——機構PASS。

### 4. 判別実験本体（V1×2・V2×2，DUT/スニファ同時二重キャプチャ`r39_dualcap.py`）

| 試行 | 1周目（ホスト） | 2周目（ゲスト）scan | 2周目promisc | 2周目MAC割込み | スニファDUT検出（全期間） | スニファ環境対照 |
|---|---|---|---|---|---|---|
| V1 boot1 | wifi未実行・3秒でジャンプ（g_osi=0確認） | **0 APs＋RESCAN 0 APs×8** | 0（終始） | 生存（〜6656，バースト状） | **0** | probereq 121・beacon 2806 |
| V1 boot2 | 同上 | **0 APs＋RESCAN 0 APs×8** | 0 | 生存（〜6656） | **0** | probereq 126・beacon 2848 |
| V2 boot1 | フルwifi＋scan＝**0 APs**（ベースライン内蔵）→ジャンプ（g_osi=0x40800068＝ホスト自身の登録値を確認→ゼロクリア） | **0 APs＋RESCAN 0 APs×13** | 0 | 生存（〜10784） | **0** | probereq 160・beacon 5143 |
| V2 boot2 | 同上（1周目0 APs） | **0 APs＋RESCAN 0 APs×13** | 0 | 生存（〜10784） | **0** | probereq 171・beacon 5814 |

- 2周目のiq_done16=1・生ADC非ゼロ（例0x0a6c0a6a）・`esp_wifi_init -> 0`＝
  較正チェーン自体は2周目も完走（実施38冷間と同型）。
- 2周目の`g_osi_funcs_p`再登録も成功（`esp_wifi_init -> 0`が含意，
  実施27と同じ機構パリティ）。
- スニファ環境陽性対照は4試行全てで成立（ambient probereq/beacon安定検出）
  ——「検出できない設置・リンク」ではない。DUT送信元フレームは1周目・
  2周目を通じて**完全にゼロ**。

### 5. 判定：**0APのまま＝「stockブート列固有の内容」が鍵と確定**（事前固定表どおり）

**「POR後2回目のソフト起動（遷移の履歴）」説は明確に反証された。**
同一ゲストバイナリ・同一ハンドオフ機構・同一機材で：

- ホスト＝stock ESP-IDFブート列（実施38）→ 22〜29AP・TX放射あり・promisc生存
- ホスト＝冷間ASP3（本ラウンド，V1/V2とも）→ 0AP・TX無・RX無

つまり「リセット無しの2度目のソフト起動」「確立済みクロックの上での
wifi/PHY再初期化」（V2では1周目にフル較正＋scanまで実行済み）だけでは
何も直らない——**ハンドオフが効くのはstockブート列が確立する具体的状態の
おかげ**であり，その状態は実施15〜37の静的比較（MMIO/regi2c/PCR/SRAM窓）を
全てすり抜けている何か，という絞り込みが完成した。候補1（過渡ラッチ＝
「遷移それ自体」説）はこの結果と両立しにくい（ASP3も自前のPLL切替・
BBPLL較正遷移を実行しており，V2では2周分の遷移履歴がある）——優先度低下。

限定事項（正直な記録）：
- 本機構はPMAエントリ14へのX付与という共変量を導入する（stockハンドオフ
  経路もstock startupがPMA全体を上書きするため，どちらのハンドオフも
  「ROM素のPMA」ではない）。PMAはCPUアクセス属性のみでRF経路に機序なし，
  かつ2周目の挙動は冷間（ROM素のPMA）と完全同型のため，判定を揺るがさない。
- ゲストは`HANDOFF_SKIP_CLOCK_INIT=1`（ホスト確立のクロックを引継ぎ）。
  「2周目が自前でクロック初期化をやり直す」変種は未実施——ただしそれは
  冷間ブートのクロック初期化と同一コードであり，新情報は薄い（V1の1周目が
  まさにそれを実行済み）。

### 6. 次ラウンドへの推奨

外部AI回答の残候補のうち，本結果で優先度が上がった順：

1. **候補3（MACブロックの「scan実行中」系譜diff＋LP RAM全域）**：blobが
   実行時に環境値で分岐する可能性は静的比較では原理的に見えない。scan中の
   同一チャネルdwellで両系譜（stockハンドオフ vs 冷間/セルフハンドオフ）を
   停止し，WiFi MAC制御ブロック全域＋LP RAM 16KB＋LP_AON STOREをdiff。
   セルフハンドオフ機構（本ラウンド資産）は「同一ゲストバイナリでWORKS/
   FAILSを切替えられる」ため，このdiffの理想的なハーネスになる。
2. **候補4（regi2c 0x20以降＝RF-synth残存穴）**：blob逆アセンブルでreg引数
   最大値を確認→0x20以上が実在すれば追加スイープ。
3. 候補2（残差19語の同時注入）：安価だが期待値は下がった（19語は実施35で
   全て既知非因果カテゴリに帰属済み）。

### 7. 両ボード最終状態（終了処理）

- C5#1＝`build/c5_r34_wifi80`（240MHz出荷構成，セルフハンドオフパス無し）を
  `write-flash 0x0`フル4MB書き戻し。**0x200000のゲストは意図的に消去済み**
  （実施37罠どおり。次回ハンドオフ検証時は`r38_guest_640k.bin`を再書込み
  すること）。RTSリセット後45秒キャプチャ（`r39_final_state_console.log`）：
  バナー正常（12:00:27ビルド）・`0 APs found`→`RESCAN 0 APs`×3・R39
  マーカー無し——冷間症状の回帰を確認。
- C5#2＝スニファ（2.4GHz ch6版）のまま未書換え・維持。
- 接触禁止2台は全工程で無接触。

### 変更ファイル（実施39）

- `apps/wifi_scan/wifi_scan.c`：`ESP32C5_R39_SELFHANDOFF`（=1/=2）ガードの
  セルフハンドオフ一式（`r39_jump_stub`／`r39_selfhandoff_now`／
  `r39_pma_dump`／PMAエントリ14 X付与）。既定（未定義）ビルドは
  バイナリ実質不変（md5差はバナーのビルド時刻5バイトのみ，nmシンボル表
  完全一致を確認済み）。
- 本doc：実施39節追記・罠早見表2行追加（DTR罠・ROM残置PMA罠）。
- スクラッチ（`260d98fa…/scratchpad/`）：`r39_dualcap.py`（DTR修正版
  二重キャプチャ）・`r39_hostboot_640k.bin`／`r39_hostscan_640k.bin`・
  `r39_v1_dut_boot1/2.log`・`r39_v1_sniff_boot1/2.log`・
  `r39_v2_dut_boot1/2.log`・`r39_v2_sniff_boot1/2.log`・
  `r39_final_state_console.log`。
- git commitは行っていない（コーディネータのレビュー後のcommit&push運用）。

### 検証（実施39）

- ビルド：`build/c5_r39_host_boot`（V1）・`build/c5_r39_host_scan`（V2）
  とも成功（FLASH 11.81〜11.82%）。既定フラグ無しビルド
  （`build/c5_r39_regress`）＝実施38冷間ビルドとnmシンボル表完全一致・
  バイナリ差はビルド時刻文字列5バイトのみ（無影響確認）。
- 機構検証を判定より先に実施：PMA障害の発見→修正→2周目バナー・
  `esp_wifi_init -> 0`・scan完走の成立を確認してから判別試行に進んだ。
- 再現性：V1×2・V2×2の全4試行で同一結果（2周目0AP・promisc=0・
  スニファDUT検出0）。スニファ環境陽性対照は4/4成立。
- 事前固定の判定表に照らし「0APのまま」側に確定——考察・次段推奨まで
  本節に記載。

---

## 実施40：外部AI候補1（過渡ラッチ仮説）の3段判別実験——段階(i)MODEM_SYSCON全modemリセット一括パルス／段階(ii)PMU HP_WIFI電源off→on過渡／段階(iii)クロック切替え順序入替，**3段すべて空振り（各2/2再現）＝候補1を実験的に棄却確定**。段階(ii)で「終値には現れない再現性のある副作用」（MODEM_SYSCON_CLK_CONFのCLK_I2C_MST_SEL_160Mビットが電源サイクルで1→0に恒久変化）を発見したが，較正・TX/RX判定には無関係と判定。次段＝候補3（MACブロック実行時diff）・候補4（regi2c 0x20以降）へ

コーディネータ指示＝`tmp/external_ai_c5_answer_fable.md`候補1の3段最小判別実験。
実施39が「stockブート列固有の内容」を確定させ候補1の優先度を下げていたが，
advisorレビューにより「実施39が反証したのは"遷移の履歴"（同じ遷移を2回経験する
こと自体）」であり，本ラウンドが対象とする3操作（ドメインリセット・電源
off/on・クロック順序入替）そのものは実施39・実施36とも一度も試していない
ことを確認した上で，事前確率を低く見積もったまま実施した（低確度でも
「読み出し・単発値注入では原理的に触れない領域」を塞ぐ意味はある）。

### 0. 机上先行（実験前に固定）

v6.1実ソース（`~/tools/esp-idf-v6.1/components/esp_hw_support/port/esp32c5/rtc_clk.c`）で
stockのCPUクロック遷移列を精査した：

- stockの実遷移は**2段**だが，実体は「PLLソース切替え」ではなく「同一
  PLL_F240Mソース内でのdivider変更」——stage A（bootloader）：
  XTAL→PLL_F240M(div=3,80MHz)（`old_src≠new_src`につき
  `rtc_clk_update_pll_state_on_cpu_src_switching_start/end()`が実行され，
  BBPLL enable+480MHz較正がここで発生）。stage B（app，`esp_clk_init`）：
  PLL_F240M(div=3)→PLL_F240M(div=1,240MHz)は`old_src==new_src`（共に
  PLL_F240M）のため`rtc_clk_cpu_freq_set_config()`362-384行の分岐で
  switching_start/endが**呼ばれない**——ソース切替えではなくdivider書換え
  のみ。
- `rtc_clk_root_clk_switch_protect()`（IDF-11064対策，root clock自動
  ゲーティングのbypass）は**PLL_F160M⇔PLL_F240M間のソース切替えにのみ
  適用される**条件式（353-359行）——stockの実遷移はこの条件に一度も
  該当しない。IDF-11064の自動ゲーティング機構はstockの実行パス上で
  一度も発火していないことをv6.1本流の制御フローから確認した（実施21が
  「hal＝別世代コードとv6.1を混同」として棄却したPCR_FPGA_DEBUG bit31
  仮説を独立に裏付ける新規の机上確認）。
- ASP3（実施32/34）はXTAL→PLL_F240M÷1へ1段で直接切替え，BBPLL enable+
  較正はhardware_init_hook最初期でstockのstage Aとほぼ同時にカバー済み。
  実施36の監査（唯一の真のパルス=rst_wifimacはblob経由で両者同一）と
  合わせ，「stockが持っていてASP3が欠いている特定のパルス／順序」は
  机上では特定できなかった。

**結論（机上）**：候補1が原理として想定するIDF-11064型の機序は，stockの
実行パス自体に存在しない。3段実験は「stockを模倣する」のではなく「読み
出し・単発値注入では原理的に触れない領域を虱潰しに叩く」性質の実験であり，
事前確率は低いまま実施する（advisor指摘のとおり）——机上先行・各段の
事前予測全文は`tmp/.../scratchpad/r40_desk_prediction.md`（本ラウンド
成果物，非リポジトリ）に保存。

### 1. 段階(i)：MODEM_SYSCON全modemリセット一括パルス

**事前予測**：変化なし（iq_done16=1のまま，raw_adc非ゼロだが両系譜とも
「一発較正値が固まる」実施38型，AP=0のまま）。3段中もっとも機序的に妥当
（実施38が特定した「TX/RX共通のFE/シンセ段」に直接対応する`RST_FE`／
`RST_FE_ADC`／`RST_FE_DAC`／`RST_FE_PWDET_ADC`を含む）。

**実装**：`apps/wifi_scan/wifi_scan.c`に`ESP32C5_R40_MODEMRST`ガードで
`r40_modemrst_pulse()`を新設し，`esp_wifi_init()`直前（実施38/39と同じ
挿入点）で呼び出す。`MODEM_SYSCON_MODEM_RST_CONF_REG`（`0x600A9C10`）の
定義済み全ビット（`RST_WIFIBB`/`RST_WIFIMAC`/`RST_FE`系/`RST_BTBB`系/
`RST_ZBMAC`系/`RST_MODEM_ECB`/`CCM`/`BAH`/`SEC`/`RST_ETM`/`RST_BLE_TIMER`/
`RST_DATA_DUMP`，マスク`0xEFC7FF00`）へ書込み→`esp_rom_delay_us(50)`→
0クリア→読み戻し確認。

**実測（`build/c5_r40_i1`，2/2独立RTSリセット，DUT/スニファ同時二重
キャプチャ`r40_i1_boot1/2_*.log`）**：
- パルス成立を実測確認：`pre rst=00000000`→`mid(during-pulse)
  rst=efc7ff00`→`post rst=00000000`（2/2）。
- センチネル（`MODEM_SYSCON_CLK_CONF`/`CLK_CONF_POWER_ST`/`CLK_CONF1`/
  `I2C_ANA_MST_ANA_CONF0`のCAL_DONE bit24）はパルス前後で完全不変
  （`clk=00201002`/`pst=40000000`/`c1=00000000`/`ana0=2100e404`，2/2）
  ——リセットパルスがBBPLL較正状態・modem ICG設定を破壊しないことを確認。
- 較正：`iq_done16=1`到達（2/2），`0 APs found`→`RESCAN 0 APs`（2/2）。
  スニファ側`DUTtotal=0`・`DUTprobereq=0`（2/2，環境陽性対照は
  probereq 50〜89件で両ブートとも成立）——**TX/RX無・0APのまま，変化
  なし**。

**判定**：予測どおり**空振り**。

### 2. 段階(ii)：PMU HP_WIFI電源ドメインoff→on過渡パルス

**事前予測**：段階(i)よりさらに低い事前確率。実施22は`PMU_POWER_PD_
HPWIFI_CNTL`の**静的終値**（stock実測=`0x0`＝force全解除）をmid-hang
注入・before-PHY移植の両方で既に因果棄却済み——本段の新規性は終値では
なく**遷移そのもの**（force power-down→短delay→復帰）であり，実施22が
触れていない変数を狙う点を明記する（実施22の再実験ではない）。実施23の
前例（PD系force書込みをWiFi init時点で行うとJTAG捕捉の到達性が悪化した）
を踏まえ，UART単独観測でも悪化兆候が出たら即revertする方針とした。

**実装**：`ESP32C5_R40_PMUPULSE`ガードで`r40_pmupulse_cycle()`を新設。
`PMU_POWER_PD_HPWIFI_CNTL_REG`（`0x600B0108`）を`FORCE_RESET|FORCE_ISO|
FORCE_PD`（`0x23`，実際にpower-down状態へ入れる）→`esp_rom_delay_us(100)`
→ASP3の元の値（`0x1C`＝`FORCE_PU`/`NO_RESET`/`NO_ISO`=1，実施22の終値
`0x0`とは別条件に留めて変数を遷移の有無だけに絞る）へ復帰。

**実測（`build/c5_r40_ii1`）**：
- 初回sanityキャプチャで捕捉バイト数が異常に少なく（1042B）一見ハングに
  見えたが，追加キャプチャ（`r40_ii1_check2_dut.log`）で正常に
  `esp_wifi_init -> 0`〜`scan完走`まで到達することを確認——RTSリセット
  タイミングと読取り窓の競合による取りこぼしであり，真のハングでは
  なかった（悪化ではないと判定した上で先へ進んだ）。
- **新規の再現性ある副作用を発見**：`MODEM_SYSCON_CLK_CONF`のbit12
  （`CLK_I2C_MST_SEL_160M`）が電源off→on経由で`1→0`へ恒久的に変化した
  （`pre clk=00201002`→`post clk=00200002`，2/2独立ブートで完全一致）。
  段階(i)では同レジスタは不変だったため，これは電源ドメイン遷移固有の
  副作用である——ただし較正（`iq_done16=1`到達，2/2）・AP検出（`0 APs
  found`→`RESCAN 0 APs`，2/2）・スニファTX検出（`DUTtotal=0`，
  `DUTprobereq=0`，2/2，環境陽性対照probereq 50〜65件）はいずれも
  段階(i)と同型で不変——**副作用は実在するが症状には無関係**と判定。
- WDTリブートループ等の明白な退行は一度も観測しなかった（2/2とも
  クリーンな`rst:0x1 (POWERON)`のみ）。

**正直な限定事項（advisorレビュー指摘）**：段階(ii)は単一変数の実験
ではない——`PD_HPWIFI_CNTL`は元の値（`0x1C`）へ復帰させたが，副作用で
落ちた`CLK_I2C_MST_SEL_160M`（bit12）はその後の`esp_wifi_init`／scan
全体を通じて`0`のまま残った（意図した「電源遷移」に加え「クロック
セレクト低下」も同時に混入した）。それでも本段の空振りという結論は
揺るがないと判断する理由：(a) 較正（`iq_done16=1`）はBBPLL較正と同じ
`I2C_ANA_MST` regi2cマスタをより厳しく使う消費者であり，もしbit12=0が
このマスタを実用上劣化させるほどの意味を持つなら較正が先に壊れるはず
だが壊れなかった——bit12=0はここでは無害な速度セレクトであり，イネーブル
そのものではないと解釈できる。(b) 結果は基準ブートとも，bit12を一切
変更していない段階(i)/(iii)とも同一（0AP・TX無・RX無）——「遷移がRXを
救うがbit12=0が相殺的に壊す」という打ち消し合いのシナリオは，較正が
無傷である以上考えにくい。この交絡は再実験のコストに見合わない
（結果はすでに収束的証拠で裏付けられている）と判断し，本ラウンドでは
再実行しない。

**判定**：予測どおり**空振り**（ただし実施22が触れなかった「遷移」変数を
初めて塞いだ点で判別力は保たれている。同一ラウンド内でのTX検出陽性対照は
用意していないが，環境probereq 50〜89件が毎ブート安定検出されている＝
スニファのDUT-MACフィルタが載る解析パス自体が毎回生きていることの実質的な
確認になっている——実施38のTX検出陽性対照に依拠）。

### 3. 段階(iii)：クロック切替え順序入替

**事前予測**：3段中もっとも事前確率が低い（0節の机上調査で「stockが
modemクロックを全停止してから切替える」という具体的な順序要件は確認
できなかった）。

**実装**：`asp3/target/esp32c5_espidf/target_kernel_impl.c`に
`ESP32C5_R40_CLKREORDER`ガードで`esp32c5_r40_modem_icg_disable_bracket()`
を新設し，`esp32c5_r32_cpu_clock_switch()`内・`esp32c5_r34_bbpll_
configure_480mhz()`完了後～PCRのソース切替え（`PCR_SYSCLK_CONF`他）の
直前でmodem ICGを一旦無効化（`icg_modem.code=0`＋`MODEM_SYSCON`/
`MODEM_LPCON`のST_MAPを0クリア＋即時反映パルス）→PCR切替え実行→
`esp32c5_r34_modem_icg_enable_min()`で再有効化，という順序に組み替えた。

なお机上先行（0節）で判明したとおり，`esp32c5_r34_bbpll_configure_
480mhz()`自体がregi2c経由の較正であり，そのregi2cマスタ自体が
`esp32c5_r34_modem_icg_enable_min()`のICGでゲートされている（実施34の
発見）——「BBPLL較正中もmodem系クロックを全停止する」という文字どおりの
実装は不可能であり，本段は「CPUルートクロック切替えの瞬間だけmodem系を
止める」という，候補1が想定するIDF-11064型状態を模した最小のブラケット
として実装したことを正直に記録する。

**実測（`build/c5_r40_iii1`）**：
- 起動退行なし（2/2ともバナー・`esp_wifi_init -> 0`まで正常到達，
  `rst:0x1 (POWERON)`のみ）。
- `iq_done16=1`到達（2/2），`0 APs found`→`RESCAN 0 APs`（2/2），
  スニファ`DUTtotal=0`・`DUTprobereq=0`（2/2，環境陽性対照probereq
  60〜67件）——**変化なし**。

**判定**：予測どおり**空振り**。

### 4. 総合判定：候補1（過渡ラッチ仮説）を実験的に棄却確定

3段すべて空振り（各2/2，計6回の独立ブートで一貫して0AP・TX無・RX無・
較正は完走）。事前予測どおりの結果であり，「読み出し・単発値注入では
原理的に触れない領域」（ドメインリセット・電源遷移・クロック順序）を
虱潰しに塞いだ上での棄却のため，候補1は本ラウンドで**実験的に閉じた**
と判断する。実施39（stockブート列固有の内容が鍵）との整合性：本ラウンド
は実施39が反証しなかった「操作そのもの」を追加で塞いだだけであり，
両ラウンドは矛盾しない——「過渡・履歴・操作」という切り口全体が尽きた。

段階(ii)で発見した「電源サイクルがMODEM_SYSCON_CLK_CONF bit12を書き換える」
という副作用自体は，症状への因果はないものの，**PMUドメイン間に静的
レジスタ比較では見えない相互作用が実在する**という一次証拠であり，将来
候補3/4の実験で「なぜこの値なのか」を考える際の参考情報として記録する
（本ラウンドでは追跡しない）。

### 5. 変更ファイル

- `apps/wifi_scan/wifi_scan.c`：`ESP32C5_R40_MODEMRST`（段階(i)，
  `r40_modemrst_pulse()`）・`ESP32C5_R40_PMUPULSE`（段階(ii)，
  `r40_pmupulse_cycle()`）を新設，いずれも`esp_wifi_init()`直前の
  同一挿入点から択一的に呼出し。共通の観測用レジスタ定義
  （`R40_MODEM_CLK_CONF_REG`/`R40_ANA_CONF0_REG`）は両ガードで共有できる
  よう独立ブロックへ切り出した。既定（いずれも未定義）ビルドは
  nm symbol table完全一致（0行diff，`/tmp/r40_regress2.nm`対
  `/tmp/r34_wifi80.nm`）で無影響を確認済み。
- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `ESP32C5_R40_CLKREORDER`ガードで`esp32c5_r40_modem_icg_disable_
  bracket()`を新設し，`esp32c5_r32_cpu_clock_switch()`のPCRソース
  切替え前後にブラケット。既定ビルドは同上のnm diffで無影響を確認済み。
- 本doc：実施40節追加。
- スクラッチ（`260d98fa…/scratchpad/`）：`r40_desk_prediction.md`
  （0節，机上先行・各段事前予測の全文）・`r40_i1_*`（段階(i)，
  sanity+boot1/2のDUT/スニファログ）・`r40_ii1_*`（段階(ii)，
  sanity+check2+boot1/2）・`r40_iii1_*`（段階(iii)，sanity+boot1/2）・
  `r40_final_state_*`（5節，終了処理確認）。ビルド：
  `build/c5_r40_i1`（段階(i)，`ESP32C5_R38_RXINSTR=1;ESP32C5_R40_
  MODEMRST=1`）・`build/c5_r40_ii1`（段階(ii)，`...;ESP32C5_R40_
  PMUPULSE=1`）・`build/c5_r40_iii1`（段階(iii)，`...;ESP32C5_R40_
  CLKREORDER=1`）・`build/c5_r40_regress`（既定フラグ無し，nm比較用）。
- git commitは行っていない（コーディネータのレビュー後のcommit&push運用）。

### 6. 検証（実施40）

- ビルド：4構成（i/ii/iii/regress）すべて成功。regressはnm symbol
  table完全一致（既定ビルド無影響）を確認。
- 各段2/2独立RTSリセット，DUT/スニファ同時二重キャプチャで再現性確認
  （`r39_dualcap.py`のDTR修正版をそのまま再利用）。スニファ環境陽性
  対照は全6ブートで成立（probereq 50〜89件，beacon増加継続）。
- 段階(ii)着手時の1回だけ短時間キャプチャ（1042B）で見た目上の異常を
  検知したが，追加キャプチャで正常完走を確認し「悪化ではない」と判定
  してから先へ進んだ（rigor doc「recurrence」型の誤判定を避けた）。
- 終了処理：C5#1を`build/c5_r34_wifi80`（240MHz出荷構成，実験パス無し）
  でフル4MB `write_flash 0x0` 書き戻し。RTSリセット後45秒キャプチャ
  （`r40_final_state_dut.log`）：`0 APs found`→`RESCAN 0 APs`×3・
  WDTリセット0件——冷間症状の回帰を確認。C5#2はスニファ（2.4GHz ch6版）
  のまま未書換え・維持。接触禁止2台（C6 board C `14:C1:9F:E0:5A:9C`／
  UARTブリッジ`125a266b…`）は全工程で無接触（by-id照合徹底）。

### 次段への申し送り

外部AI回答の残候補（実施39の推奨順のまま，本ラウンドで候補1が確定的に
閉じたことにより優先度がさらに明確化）：

1. **候補3（MACブロックの「scan実行中」系譜diff＋LP RAM全域）**：
   セルフハンドオフ機構（実施39資産）を使い，同一ゲストバイナリで
   WORKS（stockハンドオフ）/FAILS（冷間・セルフハンドオフ）を切替えた
   scan中の同一チャネルdwellでWiFi MAC制御ブロック全域＋LP RAM 16KB＋
   LP_AON STOREをdiffする。
2. **候補4（regi2c 0x20以降＝RF-synth残存穴）**：blob逆アセンブルで
   reg引数最大値を確認→0x20以上が実在すれば追加スイープ。
3. 候補2（残差19語の同時注入）：引き続き優先度は低い（実施35で既知
   非因果カテゴリに帰属済み）。

## 実施41：外部AI回答候補2＝実施35残差19語の「同時」注入——19語のうち5語はレジスタヘッダ精査でRO/生きたRNG出力と再分類し除外，残り14語（うち9語は個別注入が本ラウンドが初回）を一括注入したが**AP/TX/RX/promiscすべて変化なし（2変種×各2/2＝計4/4）**。候補4（regi2c reg引数の最大値）も机上で決着：blob全122呼出箇所の最大reg=0x1D，0x20以上の呼出しはゼロ——実施25の0x00〜0x1Fスイープが実際に使われる全範囲を既にカバーしていたと確定。**静的レジスタ値としての消尽を正式に宣言**，次段は候補3（MACブロック実行時diff＋LP RAM全域）

### 背景・目的

外部AI回答（`tmp/external_ai_c5_answer_fable.md`）候補2＝「実施35の残差19語は
個別注入ではすべて非因果と分類済みだが，組合せマスキング（複数同時でしか
効かない）は未検証」。本ラウンドはこれを直接判別し，「変化なし」でも
初めて「静的レジスタ値としての探索空間の消尽」を言い切れる（外部AI回答の
明文）。時間が許せば候補4（regi2c reg引数の最大値，机上部分）も消化する。

### 1. 19語の再確定：スクラッチ資産から計算し直し，実施35の記述と完全一致

実施35のJTAGスナップショット2点（本セッションのscratchpad
`r35/asp3cold_full_cand4_boot1.json`＝候補1〜4込みASP3冷間，
`r35/handoff_blocks_boot1.json`＝クロスカーネル・ハンドオフWORKS状態，
いずれも別ラウンドの本セッション内に現存）をPythonで単純diffし直した
ところ，**19語**が得られ，アドレス・両側の値とも実施35 10節の記述と
完全一致することを確認した：

```
PMU        0x600b0028  cold=c0048060  handoff=c004afd0
PMU        0x600b00f8  cold=0000001c  handoff=00000000
PMU        0x600b00fc  cold=0000001c  handoff=00000000
PMU        0x600b0100  cold=0000001c  handoff=00000000
PMU        0x600b010c  cold=0000001c  handoff=00000000
PMU        0x600b0110  cold=ff000000  handoff=f0000000
PMU        0x600b0130  cold=00000000  handoff=00020000
PMU        0x600b0198  cold=f3580003  handoff=f3480003
LP_CLKRST  0x600b0404  cold=000007ff  handoff=00000000
LP_CLKRST  0x600b0418  cold=2b000000  handoff=19000000
LP_AON     0x600b1004  cold=00358b20  handoff=00359755
LPPERI     0x600b2800  cold=7f000000  handoff=41000000
LPPERI     0x600b2808  cold=005cf43a  handoff=d07f47cf
LPPERI     0x600b2824  cold=a9000fff  handoff=07000fff
LPPERI     0x600b2828  cold=9a41f894  handoff=d7bd8791
LP_ANA     0x600b2c00  cold=0ffc0100  handoff=6ffc02c0
LP_ANA     0x600b2c0c  cold=ffffffc3  handoff=ffffffc1
LP_ANA     0x600b2c18  cold=00000000  handoff=80000000
PVT_MONITOR 0x600190d8 cold=4d800269  handoff=c9800269
```

### 2. RO/除外判定：ESP-IDF v6.1レジスタヘッダをビット単位で照合——5語を除外

task手順の「RO語は除外し，その旨記録」に従い，各語を`soc/pmu_reg.h`・
`soc/lp_clkrst_reg.h`・`soc/lp_aon_reg.h`・`soc/lpperi_reg.h`・
`soc/lp_analog_peri_reg.h`・`soc/pvt_reg.h`（`~/tools/esp-idf-v6.1/
components/soc/esp32c5/register/soc/`）でビットフィールド単位に照合した。
実施35の分類（4件を「PVT/温度駆動の動的値」「読取専用ステータス」と
記載）より精密に，**5語**が注入不能（RO，または差分ビットがRO）と
確定した：

| アドレス | レジスタ名 | 判定 |
|---|---|---|
| `0x600B0198` | `PMU_CLK_STATE0_REG` | 全ビットRO（クロックmux状態表示） |
| `0x600190D8` | `PVT_COMB_PD_SITE2_UNIT0_VT1_CONF1_REG` | 差分ビット(`delay_num_o`[30:23]・`timing_err`[31])はRO。R/Wビット(`monitor_en`・`delay_limit`)は両条件で**既に完全一致**（デコード済み：cold=handoff=`monitor_en=1,delay_limit=154`）——差分は生きたPVT/timingセンサ読値そのもの |
| `0x600B2808` | `LPPERI_RNG_DATA_REG` | 全ビットRO（RNG生データ） |
| `0x600B2828` | `LPPERI_RNG_DATA_SYNC_REG` | 全ビットRO（RNG同期データ） |
| `0x600B2824` | `LPPERI_RNG_CFG_REG` | 差分は`RNG_SAMPLE_CNT`[31:24]（RO統計カウンタ）のみ。R/W設定ビット[11:0]（`SAMPLE_ENABLE`/`TIMER_PSCALE`/`RNG_TIMER_EN`/`RTC_TIMER_EN`）は両条件で`0xFFF`と**既に完全一致** |

このうちLPPERI+0x24（`RNG_CFG`）は`target_kernel_impl.c`の
`ESP32C5_R31_LPPERI_RNG_CFG`（実施31，`esp32c5_r31_bootloader_random_
cycle()`が両ビルドで無条件・同一に書き込む対象）と同一アドレスであり，
「同一ソフトウェアで既に同一に駆動されている」という実施35 7〜8節の
前提とも整合する。実施22は「LPPERI+0x8/+0x24」を漠然と「BOD/グリッチ
検出器ノイズ」に分類していたが，本ラウンドのヘッダ照合で**正確には
「RNGブロックの生きた出力/統計カウンタ」**（BOD検出器とは無関係）と
判明した——分類の方向性は正しかったが根拠レジスタの同定は誤っていた，
という訂正。

### 3. 残り14語：R/W確認・注入設計

残り14語は全ビットまたは差分ビットがR/W（設定可能）とヘッダで確認した
（詳細は各語のフィールド一覧を個別に確認済み，省略）。内訳：

- **個別注入済み・既にcausal refute（5語）**：`0x600B0028`
  (`PMU_HP_ACTIVE_HP_REGULATOR0`，実施35候補3——PVT自動dbiasループが
  書込み直後に上書きすることも確認済み，動的値)・`0x600B00F8/FC/100/
  10C`（PD_TOP/HPAON/HPCPU/LPPERI force，実施23/24）・`0x600B0110`
  （PD_MEM_CNTL，同force系）・`0x600B1004`（LP_AON STORE1=RTC_SLOW_
  CLK_CAL，実施35候補2）。
- **個別注入は未実施・本ラウンドが初回（9語）**：`0x600B0130`
  （SLP_WAKEUP_CNTL3）・`0x600B0404`（LP_CLKRST_LP_CLK_PO_EN）・
  `0x600B0418`（LP_CLKRST_FOSC_CNTL）・`0x600B2800`（LPPERI_CLK_EN_
  REG）・`0x600B2C00/2C0C/2C18`（LP_ANA BOD_MODE0_CNTL/FIB_ENABLE/
  INT_ENA）。これらは実施22/23が「机上棄却」（レジスタ意味論からの
  推定のみ，実注入なし）していた語を含む。

  副産物として`LP_CLKRST_FOSC_CNTL`（RC_FAST発振器トリム，`DFREQ`
  フィールド）を精読したところ，cold側はPOR既定値そのまま（172）・
  handoff側はstockが較正した値（100）——実施35は「PVT/温度駆動の動的値
  かもしれない（未確認，優先度低）」と記載していたが，フィールド定義上
  はPVT自動ループの言及がなく，`rtc_clk_cal()`相当の**一回きりの較正
  結果**である可能性の方が高いと再評価した（ただし本ラウンドでは値の
  注入のみ試験し，較正機構自体の実装は対象外）。

値はいずれもハンドオフ(WORKS)側の実測スナップショット（本ラウンドで
再計算した上表の値）を使用。注入ハーネスは実施35/36のソースシード方式
を踏襲し，`asp3/target/esp32c5_espidf/target_kernel_impl.c`に
`esp32c5_r41_combined_seed()`（14語を書込み→即座に読み戻し，`.bss`配列
`esp32c5_r41_readback[14]`と不一致件数`esp32c5_r41_mismatch_count`へ
記録，かつLP_AON STORE2〜4へも鏡写し）を新設した。

### 2変種（外部AI Q3-3対策）

「初期化時に一度だけ読まれる」語の偽陰性を排除するため，2変種を実装・
独立にビルド・テストした：

- **変種A（主変種，`ESP32C5_R41_CALL_ESPWIFIINIT`）**：`apps/wifi_scan/
  wifi_scan.c`の`esp_wifi_init()`直前（実施36と同一呼出し位置）。
- **変種B（`ESP32C5_R41_CALL_BOOTHOOK`）**：`hardware_init_hook()`末尾
  （実施31のRNGサイクル等，ASP3自身のboot時処理が全て完了した直後，
  esp_wifi_initよりずっと早い）。

両変種とも，`esp_wifi_init`直前で14語の**「生」再読取り**
（`esp32c5_r41_dump_live()`，`.bss`ではなく実レジスタを直接再読取り）を
行い，注入からこの時点までの保持状況を確認した——変種Bでは特に，
boot-hook末尾からesp_wifi_init直前までの長い区間（CPUクロック切替え・
RNGサイクル等を挟む）を跨いだ持続性の確認になる。

安全対策として，PD_TOP/HPAON/HPCPU/LPPERI force解除（実施23がJTAG
特有のブート到達性悪化を報告した語群）を含むため，読み戻し確認は
JTAG haltに依存しないソフト自己完結方式（上記`.bss`＋LP_AON STORE
鏡写し）を主手段とし，外部JTAGのhalt/mdwは本ラウンドでは使用しなかった
（実施23が踏んだ経路を回避）。

### 4. 事前予測（測定前に固定，詳細は`r41_prediction.md`）

「AP数・スニファDUTprobereq/DUTtotal・DUT promiscuousカウンタは
いずれも変化なし」と予測した。根拠：(1)19語中5語はRO/ノイズで注入対象
外，(2)残り14語中5語は既に個別causal refute済みで複数同時でも新規機序
が生じる理論的根拠が薄い，(3)残る9語（初回注入）もレジスタ意味論上
sleep遷移/BOD検出器/クロックpad出力enableでありWiFi RF/PHYのRX/TXパス
とは機序上無関係，(4)実施38-40の「TX無・RX無」という症状の性質上，壁は
PMU/LPドメインの補助レジスタではなくフロントエンド/シンセ段（TX/RX
共通）にあると推定される。

### 5. 実機結果：4/4ボートすべて「変化なし」，予測どおり

★環境注意（本ラウンドで発見）：JTAG用USB-JTAG-serialの`/dev/ttyACM1`/
`ACM2`番号がdocs記載値と入れ替わって列挙されていた（`D0:CF:13:F0:A7:44`
がACM2）。UARTブリッジのby-idパス（`b04e3bcf…`→ttyUSB0，`3e7bd19f…`
→ttyUSB2）は安定しており，本ラウンドの操作は全てUARTブリッジ経由
だったため実害なし——罠早見表へ追記済み（上記）。

`build/c5_r41_espwifiinit`（変種A）・`build/c5_r41_boothook`（変種B）を
それぞれビルド（既定nm確認で`esp32c5_r41_*`シンボルが両方に存在，
`esp32c5_r41_dump_live`は当初`ESP32C5_R41_CALL_ESPWIFIINIT`のみを渡した
際にリンクされず欠落するミスを発見・`ESP32C5_R41_COMBINED19`も明示的に
渡すよう修正——呼出し側マクロが定義側マクロを自動含意するガードも
`target_kernel_impl.c`冒頭へ追加），C5#1（`b04e3bcf…`経由）へ交互に
書込み，`r41_dualcap.py`（実施39の`r39_dualcap.py`のDTR修正版をそのまま
再利用，DUT=`b04e3bcf…`／スニファ=`3e7bd19f…`）で同時二重キャプチャした。

初回試行でR41の読み戻しsyslogが盛大に文字化けした（`esp_wifi_init`直前
の複数syslog呼出しがblob自身の並行UART出力と衝突——rigor docの「syslog
バースト損失」と同型）。各syslog呼出し間に`tslp_tsk(50000)`（50ms）を
挿入して再ビルドしたところ完全に解消し，以降は全ボートでクリーンな
読み戻しログが得られた。

**変種A（`ESP32C5_R41_CALL_ESPWIFIINIT`）2/2**：

```
wifi_scan: R41SEED called (ESPWIFIINIT variant)
wifi_scan: R41 seed_mismatch=1 live_mismatch=1 first_mismatch_addr=600b0028
wifi_scan: R41 live0-3 c004b180 0 0 0
wifi_scan: R41 live4-7 0 f0000000 20000 0
wifi_scan: R41 live8-11 19000000 41000000 6ffc02c0 ffffffc1
wifi_scan: R41 live12-13 80000000 359755
wifi_scan: DIAG promisc_rx_count=0
wifi_scan: 0 APs found (err=0)
wifi_scan: RESCAN 0 APs (err=0)  ×6
```

（2ブートとも完全に同一パターンで再現）。`live_mismatch=1`＝
`0x600B0028`（`PMU_HP_ACTIVE_HP_REGULATOR0`，PVT自動dbiasループ）のみが
`c004afd0`→`c004b180`へ書込み直後に上書きされていた——実施35 6節の
既知挙動と完全一致する予測どおりの単一例外であり，**他13語は
esp_wifi_init直前まで注入値をそのまま保持**していたことを確認した。

**変種B（`ESP32C5_R41_CALL_BOOTHOOK`）2/2**：

```
wifi_scan: R41 seed_mismatch=0 live_mismatch=1 first_mismatch_addr=...
wifi_scan: R41 live0-3 c004b180 0 0 0
wifi_scan: R41 live4-7 0 f0000000 20000 0
wifi_scan: R41 live8-11 19000000 41000000 6ffc02c0 ffffffc1
wifi_scan: R41 live12-13 80000000 359755
wifi_scan: DIAG promisc_rx_count=0
wifi_scan: 0 APs found (err=0)
wifi_scan: RESCAN 0 APs (err=0)  ×6
```

（2ブートとも完全に同一）。`seed_mismatch=0`＝boot-hook末尾での注入
直後は14語全て正しく書き込まれていたことを確認した上で，`live_
mismatch=1`＝esp_wifi_init直前（CPUクロック切替え・実施31のRNGサイクル
等を挟んだ後）でも変種Aと同一の単一例外（PVT dbias）のみが上書きされ，
**他13語はboot-hook末尾という早い時点からesp_wifi_init直前までの長い
区間を通して一貫して保持**されていた——外部AI Q3-3が懸念した「一度だけ
読まれる語の偽陰性」は本ラウンドの範囲内では否定された。

**判定（4/4ボート共通）**：
- AP検出数：`0 APs found`→`RESCAN 0 APs`×6，全ボートで不変。
- C5#2スニファのDUTtotal／DUTprobereq：全ボートで`0`のまま（一方
  スニファ自身のprobereq/beacon/mgmt総数は各ボートで継続増加＝
  スニファの陽性対照は健全，`SNIFF/s=NN total=NNNN ... DUTtotal=0
  DUTprobereq=0`）。
- DUT側`promisc_rx_count`：全ボートで`0`。
- `rst:`：全ボート`0x1 (POWERON)`のみ，WDTリセット等の異常なし。

**事前予測は的中——4/4すべて「変化なし」。組合せマスキングは本ラウンドの
範囲内では棄却された。**

### 6. 候補4（regi2c reg引数の最大値）机上部分：完了・0x20以上ゼロ件で決着

時間が許したため，task手順3の「時間が許せば候補4の机上部分」を消化した。
`build/c5_r41_espwifiinit/asp.elf`を全体逆アセンブルし（
`riscv32-esp-elf-objdump -d`），blobのregi2c低レベル関数群
（`phy_chip_i2c_writeReg`/`.constprop.2`／`phy_chip_i2c_readReg`／
`phy_i2c_writeReg`（4引数ラッパ）／`phy_i2c_writeReg_Mask`／
`phy_i2c_readReg`／`phy_i2c_readReg_Mask`）への全呼出し箇所（122箇所）
を機械的に抽出し，各呼出し直前の定数ロード（`c.li`/`li`/`addi`）を
簡易データフロー追跡してreg引数（各関数のABIをまず実測で確定：
`phy_chip_i2c_writeReg.constprop.2(a0=block,a1=reg,a2=value)`・
`phy_chip_i2c_readReg(a0=block,a1=host,a2=reg)`・4引数/Maskラッパは
いずれもreg=a2で下位関数へ委譲）を復元した。

結果：**122箇所中121箇所でreg引数を定数として復元でき（残り1箇所は
`phy_i2c_readReg_Mask`内部からの`phy_chip_i2c_readReg`呼出しで，外側の
呼出し元で既にカウント済みの引数をそのまま転送しているだけの内部実装
であり実質的なギャップではない），最大値は`0x1D`（29）——0x20以上の
呼出しは1件も存在しない**ことを確認した。実施25が実施した0x00〜0x1F
スイープは，blobが実際に呼び出す全regi2cレジスタ範囲を既にカバーして
いたことが逆アセンブルで直接裏付けられた。**候補4は机上で決着
（0x20以上の追加スイープは不要）**——ただし本結果はASP3ビルドに
リンクされたPHYオブジェクト（v6.1版）内の静的呼出しのみを対象として
おり，ROM側の別経路や実行時に動的計算されるreg値（本ラウンドでは
発見されなかった）までは排除しきれない点に留意。

### 7. 終了処理

`build/c5_r34_wifi80`（240MHz出荷構成，候補1のみ常時有効・候補2〜4は
既定無効という実施35 13節の出荷時構成，かつ本ラウンドの新規コードは
すべて`ESP32C5_R41_COMBINED19`系ガードで既定無効）を現ソースツリーから
再ビルドし，**MD5が本ラウンド開始前の既存バイナリと完全一致
（`8cef5fbb7a7c55b4936c29ba302933bc`）** することを確認した上で
（＝本ラウンドの`target_kernel_impl.c`/`wifi_scan.c`変更は既定ビルドへ
一切影響しない），C5#1へフル4MB `write-flash 0x0`書き戻した。RTS
リセット後40秒キャプチャ（`r41_final_state_dut.log`）：`rst:0x1
(POWERON)`のみ・`promisc_rx_count=0`・`0 APs found`→`RESCAN 0 APs`×2
——冷間症状の回帰を確認。C5#2はスニファ（2.4GHz ch6版）のまま未書換え。
接触禁止2台（`14:C1:9F:E0:5A:9C`・UARTブリッジ`125a266b…`）は全工程で
無接触（by-idパスのみで照合，罠早見表の新規追記どおりttyACM番号は
不使用）。

### 変更ファイル

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r41_words[14]`（ハンドオフ実測値テーブル）・
  `esp32c5_r41_combined_seed()`（一括書込み＋即時読み戻し，`.bss`＋
  LP_AON STORE2〜4への鏡写し）・`esp32c5_r41_dump_live()`（esp_wifi_init
  直前の「生」再読取り）を`ESP32C5_R41_COMBINED19`ガード下に新設。
  `ESP32C5_R41_CALL_BOOTHOOK`定義時に`hardware_init_hook()`末尾から
  `esp32c5_r41_combined_seed()`を呼出し。呼出し側マクロ
  （`ESP32C5_R41_CALL_BOOTHOOK`/`ESP32C5_R41_CALL_ESPWIFIINIT`）が
  `ESP32C5_R41_COMBINED19`を自動含意するガードをファイル冒頭に追加
  （取り違え防止）。全て既定では未定義＝既定ビルド無影響（MD5一致で
  確認済み）。
- `apps/wifi_scan/wifi_scan.c`：`ESP32C5_R41_CALL_ESPWIFIINIT`定義時に
  `esp_wifi_init()`直前で`esp32c5_r41_combined_seed()`を呼出し。
  `ESP32C5_R41_COMBINED19`定義時（変種A・B共通）に`esp32c5_r41_dump_
  live()`＋読み戻し結果のsyslog出力（syslogバースト損失対策の
  `tslp_tsk(50000)`区切り込み）を追加。
- `docs/c5-bringup.md`：本節（実施41）・罠早見表1行追加。
- スクラッチ（`260d98fa…/scratchpad/r41/`）：`r41_prediction.md`（事前
  予測）・`r41_dualcap.py`（`r39_dualcap.py`のコピー，ポート不変のため
  無改変）・`r41a_boot{1,2}_{dut,sniff}.log`・`r41b_boot{1,2}_{dut,
  sniff}.log`・`r41_final_state_{dut,sniff}.log`・`full_disasm.txt`
  （候補4机上部分，asp.elf全体逆アセンブル，158514行）。
  ビルド（本リポジトリ内，gitignore対象）：`build/c5_r41_espwifiinit`
  （変種A）・`build/c5_r41_boothook`（変種B）。
- git commitは行っていない（指示どおり，コーディネータのレビュー後の
  commit&push運用）。

### 8. 検証

- ビルド：変種A・変種B・既定ビルド（`c5_r34_wifi80`再ビルド）の3構成
  すべて成功。既定ビルドはMD5完全一致で無影響を確認（nm上も
  `esp32c5_r41_*`シンボル0件）。
- 各変種2/2独立RTSリセット，DUT/スニファ同時二重キャプチャで再現性
  確認（`r41_dualcap.py`）。スニファ環境陽性対照は全4ボートで成立
  （probereq/beacon/mgmt総数が継続増加）。
- 全14語（RO 5語除外後）の書込み・読み戻しをソフト自己完結（`.bss`＋
  syslog）で機械確認——4ボート共通で13/14語が完全一致，残り1語
  （PVT自動dbias）は実施35で既知の動的上書きと一致するため想定内。
- 終了処理：C5#1を`build/c5_r34_wifi80`（240MHz出荷構成，MD5一致確認
  済み）でフル4MB書き戻し，冷間症状回帰を確認（`0 APs found`・
  `promisc_rx_count=0`・WDTリセット0件）。C5#2・接触禁止2台は全工程で
  無接触（by-idパスのみ使用，ttyACM番号のずれを検知したため特に注意）。

### 次段への申し送り

候補2（本ラウンド）・候補4机上部分（本ラウンド）とも決着し，**「静的
レジスタ値としてのRXキー」の探索空間は，実施35が示唆した消尽を，
初めて組合せ注入という直接的な反証実験で裏付けた形で正式に消尽したと
宣言する**。次段は実施39/40が申し送った残り2項目のうち：

1. **最優先＝候補3（MACブロックの「scan実行中」系譜diff＋LP RAM
   全域）**：実施39のセルフハンドオフ資産（`ASP3_selfhandoff`機構，
   V1/V2両変種）を使い，WORKS（stockハンドオフ）/FAILS（冷間・
   セルフハンドオフ）を同一ゲストバイナリで切替え，scan中の同一
   チャネルdwellでWiFi MAC制御ブロック全域（C6の0x600A4000域16KB相当
   のC5版アドレスを特定する必要あり，本ラウンドでは未着手）＋LP(RTC)
   RAM 16KB全域＋LP_AON STORE全レジスタ（STORE1以外未検証）をdiffする。
   promiscuous>0なのに0 APならこれが最有力だが，本ラウンド含め
   promiscuous=0が一貫しているため，「MACフィルタの手前」という
   当初の位置付けの再検証も必要——候補0（2×2判定，実施38）の結果
   （TX無・RX無）と整合する形で仮説を再構成すること。
2. 候補4の実機部分（0x20以上への追加スイープ）は机上判定により**不要**
   と確定——次段では省略してよい。
3. 外部AI Q3の残り2項目（別解釈：「bootloaderだけで足りる」説の
   stock標準app image化，注入タイミングの系統的偽陰性の一般論）は
   本ラウンドで候補2固有の偽陰性は排除できたため優先度低のまま。
4. C6の85ラウンド調査が到達した「シーケンス/タイミング/レジスタに
   現れないアナログ特性」という軸への統合は，rigor docの教訓どおり
   引き続き避け，候補3（C5固有の未踏領域）を先に潰すこと。

---

## 実施42：候補3（外部AI回答・MACブロック実行時diff＋LP RAM＋LP_AON STORE＋補足のEFUSE/APM/TEE）——**根本原因を特定・因果注入で症状を完全に反転（0AP→16-23AP・TX無→TX有，2/2再現）**：ASP3のDirect Bootはstockのアプリスタートアップが必ず実行する`bootloader_init_mem()`（APM全コントローラのctrl filter解除＋全バスマスタをTEEモードへ昇格）を一度も呼ばない。結果，WiFi/BTモデム・ハードウェア自身（APM master id=4=MODEM）がPOR既定のREE2モードのまま，HP_APMのctrl filter（POR既定で有効）がモデムのバスマスタとしてのHP SRAM（TX/RXパケットバッファ）アクセスを恒常的にブロックしていた——CPU側のMMIO/regi2c設定・較正はブロック対象外（HPCOREは常にTEEモード）のため全て正常に見え，35ラウンドの静的レジスタ診断を完全にすり抜けていた。stock相当のAPM解除処理を`hardware_init_hook()`末尾に追加注入したところ，**他は一切変更していない同一の冷間Direct Bootビルドで**AP検出・TX放射とも完全に復活した（診断用ガード付き実装のまま，昇格は次段の判断に委ねる）。C6 deaf-RX調査（実施85で凍結，held-over gapの一つが「APM/TEE+PMP/PMA/cacheが85ラウンド未比較」）にも同型の適用余地がある。

### 背景・目的

コーディネータ指示＝外部AI回答（`tmp/external_ai_c5_answer_fable.md`）候補3
（実施41時点で「静的レジスタ値としての消尽」を宣言した後の最終未消化項）：
scan実行中の同一地点でWiFi MAC制御ブロック全域・LP RAM 16KB全域・LP_AON
STORE全域をdiffし，未包含ならEFUSE/APM/TEEブロックも追加する。

### 1. 机上準備：対象アドレスの確定

advisorレビュー（実験前）の指摘に従い，盲目の16KB全域ダンプではなく，
実施41のblob全disasm資産（`r41/full_disasm.txt`，7.9MB）から`lui rX,imm`
＋直後の`addi/ori/lw/sw`のペアリングで実アドレス計算を機械的に再構成する
スクリプト（`r42/pair_lui.py`）を新規作成し，各候補領域への実アクセスを
機械的に列挙してから対象を確定した（4739件のアドレス計算を復元）。

- **WiFi MACブロック候補**（外部AI回答が言及する「C6の0x600A4000域16KB
  相当」）：0x600a4000-0x600a7fffへのlui参照が172件・**89個の実アクセス
  先**を確認（0x600a0000域＝実施15-20で調査済みのトーン測定チェーン
  MMIOとは明確に別クラスタ）。この89アドレスのみを対象にした
  （盲目の16KB全域ではない）。
- **LP RAM**（0x50000000/16KB）：blobアクセスは**1箇所のみ**
  （0x50000028）。逆アセンブルで文脈を確認したところ，このアクセスは
  ROM/blob由来ではなく`apps/wifi_scan/wifi_scan.c`内`main_task`自身の
  診断計装コード（実施38 RXINSTR系のカウンタ差分計算）がLP RAMを
  スクラッチ領域として流用していただけと判明——**blobは実質的にLP RAM
  を使わない**（advisorが事前に指摘した「vacuousの可能性」が的中）。
  形式上は16KB全域を4-wayで採取したが，解釈は「参考値」に留めた。
- **EFUSE**：blobアクセスは3語のみ（`0x600b4844`/`0x600b484c`/
  `0x600b486c`，objdumpが`<EFUSE+0x44>`等とシンボル注釈——
  `register_chipv7_phy`近傍でのeFuse較正トリム読出しと判明）。この3語
  のみ対象（EFUSE物理値はチップ固有で系譜非依存のはずだが，読出し経路
  の生死を確認する意味で採取）。
- **LP_AON STORE**：実施35/36の11ブロックsweepが既に0x600b1000+
  0x00-0x7C（STORE0-9含む）をカバー済みと判明。未カバーの尾部
  （PUF_MEM_SW/ISO/DISCHARGE，+0x80/+0x84/+0x88）のみ追加。
- **★EFUSE/APM/TEE の"補足"指示への対応**：実施35の11ブロックリスト
  （`r35/blockdump.py`のBLOCKS）を確認したところMODEM_SYSCON/MODEM_
  LPCON/MODEM1/MODEM_PWR0/PMU/LP_CLKRST/LP_AON/LP_I2C_ANA_MST/LPPERI/
  LP_ANA/PVT_MONITORの11個のみで，**EFUSE/APM/TEEブロックはこれまで
  一度も系譜diffの対象に入っていなかった**ことを確認した（外部AI回答の
  懸念が的中）。blob自身はAPM/TEEレジスタを直接読み書きしない（disasm
  0件）が，**ソース監査**でesp-idf-v6.1の
  `components/bootloader_support/src/bootloader_mem.c`の
  `bootloader_init_mem()`が`!defined(BOOTLOADER_BUILD)`ガード
  （＝2nd-stage bootloaderではなくAPP側スタートアップでリンクされる，
  stockの`call_start_cpu0`相当の経路）で無条件に
  ```c
  apm_hal_enable_ctrl_filter_all(false);
  apm_hal_set_master_sec_mode_all(APM_SEC_MODE_TEE);
  ```
  を実行することを発見した。ソースコメント：「デフォルトではこれらの
  アクセスパスフィルタは有効で，マスタがTEEモードの場合のみアクセスを
  許可する。HP CPU以外の全マスタはREEモードで起動するため，デフォルト
  設定ではHP CPU以外の全マスタへのアクセスが拒否される」。`apm_master_
  id_t`（`esp_hal_security/include/hal/apm_types.h`）に
  **`APM_MASTER_MODEM = 4`**が明示的に存在し，WiFi/BTモデムがCPUとは
  別のバスマスタとしてこの許可管理の対象であることを確認した。ASP3の
  Direct BootはstockのAPPスタートアップを一切経由しないため，この処理
  は一度も呼ばれない——**候補3の「未踏領域」として最有力**と判断し，
  机上準備の段階でAPM/TEEを主対象に格上げした（MAC/LP RAM/EFUSE/
  LP_AONは並行して採取）。

### 2. 採取ハーネス（`r42/r42_sweep.py`，実施36の壁時計固定遅延手法を継承）

- RTSリセット→t=15.0s（実施36と同一，両系譜とも「初回scan完了後の
  RESCANループ中」という同一ソフトウェア地点）でJTAG halt→対象を
  読み取り→resume。UARTキャプチャをhalt跨ぎで継続し，**非摂動性**
  （採取後の同一ブートでRESCANが正常完走すること）を毎回確認した。
- **advisor指摘の動的レジスタ対策**：MACブロック候補89語は同一halt
  セッション内で300ms間隔の2回読みを行い，変化した語を「ハードウェア
  自律更新（CPU halt中も進行するMACステートマシン）」として除外して
  から系譜比較にかけた——3語（`0x600a40a0`/`0x600a708c`/`0x600a7128`）
  が動的と判明し除外。
- 対象：(A) APM/TEE系7ブロック（TEE master mode ctrl 0x60098000+
  0x00-0x28，HP_APM 0x60099000+0x00-0x120，LP_APM0 0x60099800+
  0x00-0xe0，CPU_APM 0x6009a000+0x00-0x120，LP_TEE 0x600b3400+
  0x00-0x100，LP_APM 0x600b3800+0x00-0xf0，LP_AON tail 0x600b1080+
  0x00-0x08）＋TEE_BUS_ERR_CONF/TEE_CLOCK_GATE単語，(B) MAC候補89語，
  (C) EFUSE 3語，(D) LP_AON tail（(A)に含む），(E) LP RAM全16KB
  （4096語）。冷間2ブート・ハンドオフ1ブート（後述4節参照）採取。

### 3. 採取結果

**(A) APM/TEE——系譜決定的差分・genuinely latched exception（本ラウンドの核）**

冷間（2ブート完全一致・再現）：
```
HP_APM_FUNC_CTRL_REG   (0x600990c4) = 0x0000001f   （全5経路フィルタ有効＝ROM/POR既定）
HP_APM_M1_STATUS_REG   (0x600990d8) = 0x00000001   （許可違反ラッチ！bit0=permission restriction）
HP_APM_M1_EXCEPTION_INFO0 (0x600990e0) = 0x00130001
  → EXCEPTION_MODE[17:16]=3=REE2, EXCEPTION_ID[22:18]=4=APM_MASTER_MODEM, REGION[15:0]=1
HP_APM_M1_EXCEPTION_INFO1 (0x600990e4) = 0x40806858  （ブロックされたアクセス先アドレス，HP SRAM内）
TEE_M4_MODE_CTRL_REG   (0x60098010) = 0x00000003   （master 4=MODEM，REE2モードのまま）
LP_APM0/LP_APM/CPU_APM の FUNC_CTRL = 0x1/0x3/0x3（いずれも解除されていない）
```

ハンドオフ（stock ESP-IDFブート列経由，20AP前後で正常動作）：
```
HP_APM_FUNC_CTRL_REG   = 0x00000000   （全経路フィルタ解除）
HP_APM_M1_STATUS_REG   = 0x00000000   （例外なし）
TEE_M4_MODE_CTRL_REG   = 0x00000000   （master 4=MODEM，TEEモードへ昇格済み）
```

**解釈**：HP_APMのM1経路例外レコードは「WiFi/BTモデム・ハードウェア
自身（バスマスタID=4）が，REE2モードのまま，HP SRAM内のアドレス
`0x40806858`（TX/RXパケットバッファ相当の領域）へアクセスしようとして
APMにブロックされた」という**ハードウェアが記録した一次証拠**そのもの
である。CPU（HPCORE，master 0）は常にTEEモードでこのフィルタの対象外
のため，CPU発行のMMIO read/write・regi2c較正アクセスは冷間でも一切
影響を受けず全て正常に完了する——だからこそ実施15-41の35ラウンドに
わたる静的レジスタ診断（値の読み書き一致確認）がことごとくすり抜けて
いた。「TX無・RX無だが完走報告」という実施38の核心的観測と，「モデムの
バスマスタとしてのSRAM aアクセスだけが恒常的に拒否される」という本
機序は完全に整合する。

**(B) MACブロック候補（89語，動的3語除外後86語）**：系譜決定的差分
6語（`0x600a40e4`/`0x600a40f4`/`0x600a4d6c`/`0x600a7430`/
`0x600a7848`/`0x600a7ce8`）。後述4節の因果注入実験で「これらを一切
変更せずにAPMのみ修正した場合に症状が完全復帰する」ことを確認したため，
**この6語は非因果と結論**（詳細は4節）。

**(C) EFUSE**：3語とも冷間2ブート・ハンドオフ1ブートで完全一致
（`0x600b4844=0x13f0a744`／`0x600b484c=0x21500310`／
`0x600b486c=0x7ad0cb30`）——予想どおり系譜非依存（物理eFuse値，
読出し経路も両系譜で正常）。

**(D) LP_AON tail**：3語とも全条件で完全一致（`00000001,00000000,
00000000`）——PUF_MEM系は無関係と確認。

**(E) LP RAM 16KB**：形式的に4096語を採取したが，**同一ビルドの冷間
boot1 vs boot2で3700/4096語（90%超）が既に食い違う**（未初期化SRAM
残渣そのもの）。冷間 vs ハンドオフの差分も3678/4096語とほぼ同水準——
「系譜差」ではなく「起動毎に無関係に変動するノイズ」であることが
数値的に確定した（1節の机上予測どおり，blobが実質使っていない領域の
形式的採取は解釈不能なノイズしか返さない）。**この領域は本ラウンドで
正式にvacuousと結論**。

### 4. 因果検証：APM/TEE解除の注入——**2/2で症状完全反転**

`asp3/target/esp32c5_espidf/target_kernel_impl.c`に
`ESP32C5_R42_APMFIX`ガード（既定無効）で`esp32c5_r42_apm_unblock()`を
新設し，`hardware_init_hook()`末尾（実施41のBOOTHOOKと同位置，
esp_wifi_init/PHY較正より前）から呼び出した。処理内容はstockの
`bootloader_init_mem()`の直接移植：

1. HP_APM/LP_APM0/LP_APM/CPU_APMの`FUNC_CTRL_REG`を全て0へ
   （4コントローラの経路フィルタを解除）
2. `TEE_M0..M31_MODE_CTRL_REG`（0x60098000+4×id，32語）を全て0
   （全バスマスタをTEEモードへ昇格）
3. `HP_APM_M1_STATUS_CLR_REG`にラッチクリアを書込み（事後の新規例外
   有無を確認可能にするため）

**事前予測**：APM/TEEのみを修正し，他（MAC候補6語の差分・LP RAM等）は
一切変更しない「単一変数」注入とし，症状（AP数・スニファTX検出）が
反転すればAPM/TEEが根本原因，反転しなければ他要因（MAC候補6語等）を
追加で当たる。

**ビルド**：`build/c5_r42_apmfix`（`ESP32C5_R38_RXINSTR=1;
ESP32C5_R42_APMFIX=1`，既定の冷間ビルドと同一ソース・同一クロック
構成・**stockブート列は一切経由しない純粋なDirect Boot**）。フル4MB
`write-flash 0x0`。

**実測（2独立RTSリセット，各回DUT/スニファ同時二重キャプチャ
`r39_dualcap.py`流用）**：

| 試行 | 初回scan | RESCAN | promisc/MAC割込み | スニファDUT検出 |
|---|---|---|---|---|
| boot1 | （初回ログ未捕捉，RESCANで確認） | **22 APs**→**16 APs** | 生存（promisc 1000+，macint同期） | **DUTtotal=9・DUTprobereq=9**（安定） |
| boot2 | **20 APs found (err=0)** | **23 APs**×2 | 生存 | **DUTtotal=9・DUTprobereq=9**（安定） |

環境陽性対照（スニファ側total/mgmt/probereq/beacon）は両ブートとも
継続的に増加——検出系そのものは生きている前提を満たした上での
DUT検出。**42ラウンドの調査史上初めて，冷間Direct BootでAP検出・TX
放射の両方が確認された**。

**mechanism closure（advisor指摘により追加，post-fix読み戻し）**：
修正済みビルド起動後，t=15sでJTAG halt→再読取り：
```
HP_APM_FUNC_CTRL_REG = 0x00000000   （注入どおり解除）
HP_APM_M1_STATUS_REG = 0x00000000   （★新規の許可違反が一度も発生していない）
TEE_M4_MODE_CTRL_REG = 0x00000000   （MODEM，TEEモードへ昇格）
LP_APM0/LP_APM/CPU_APM FUNC_CTRL   = 0x00000000（いずれも解除）
```
ハンドオフ（stock経由，WORKS）の値と完全一致——「AP数が増えた」という
結果論ではなく，「モデムのバスマスタとしてのアクセスがブロックされて
いた経路が，注入後は実際に例外を出さずに通過している」ことをハード
ウェアの例外ラッチ自体で確認した（結果と機序の両方が閉じた）。

**MAC候補6語の非因果確認**：本注入はMAC候補ブロックの6差分語には
一切触れていない（`esp32c5_r42_apm_unblock()`はAPM/TEEレジスタのみ
書込み）。それにもかかわらず症状が完全反転したことから，**3節(B)の
6語は非因果**と結論する（今回のビルドにもその6語の差分はそのまま
残存していたはず——別途読み戻し確認はしていないが，注入コードが
MACブロックへ一切アクセスしないことはソースレベルで自明）。

### 5. 総合評価と申し送り

- **候補3（外部AI回答の最終項）は完全に的中**——「blobが環境値で分岐する」
  という原型仮説そのものではなく，**「blobより下層のアクセス制御
  ハードウェア（APM）がモデムのバスマスタ権限を系譜依存で決定していた」**
  という，より根源的な形で的中した。42ラウンドにわたり静的MMIO/regi2c
  比較・過渡ラッチ・セルフハンドオフ・19語同時注入等あらゆる「blobが
  読み書きする値」の探索が尽くされていた一方で，**「blob自身が触れない
  が，blobの動作結果を裏で決定するハードウェア権限レイヤ」**は一度も
  対象に入っていなかった——実施39のPMA発見（Direct BootはROM残置
  PMAでSRAM実行を禁止していた）と同型の「Direct Bootがstockの
  スタートアップ処理を丸ごとスキップすることで生じる別クラスの
  抜け」であり，PMAが「CPU自身の命令フェッチ権限」だったのに対し，
  APMは「CPU以外のバスマスタ（モデム等）のメモリアクセス権限」という，
  一段階抽象度の異なる抜けだった。
- **★C6 deaf-RX調査への示唆（最重要の申し送り）**：
  `memory/project_c6_agc_investigation.md`（実施85でheld-over evidence
  gapとして明記）に「APM/TEE+PMP/PMA/cacheが85ラウンド未比較」という
  項目がある。C6もASP3のDirect Bootでstockのapp スタートアップ
  （＝`bootloader_init_mem()`を含む）を一切経由しない点はC5と共通の
  構造である。**C6のTEE/APM実装（`apm_master_id_t`にMODEM masterが
  存在するか，`bootloader_mem.c`のC6分岐が同型か）を確認し，同型の
  ギャップがあれば同じ修正パターンを試す価値が非常に高い**——本ラウンド
  で確立した診断ハーネス（`r42_sweep.py`のAPM/TEEレジスタ読取り部分）
  はC6のレジスタマップに合わせてそのまま移植できる。
- **昇格判断は次段へ**（advisor指摘）：`esp32c5_r42_apm_unblock()`は
  診断用ガード（`ESP32C5_R42_APMFIX`，既定無効）のまま据え置いた。
  `hardware_init_hook()`は**全C5アプリ**（wifi_scan以外も含む）で
  実行されるため，恒久化（ガード解除）の前に最低限：
  1. `test/porting`等，WiFi以外のC5アプリでの回帰確認（本ラウンドは
     wifi_scanのみで検証）。
  2. 「全4コントローラ解除＋全32マスタTEE昇格」という広い変更が本当に
     必要か，最小変更（HP_APM解除＋MODEM masterのみTEE昇格等）で足りる
     かの絞り込み。
  3. APM/TEEを本来使う想定（将来のセキュア/非セキュア分離，LPコア
     分離等）がASP3側に無いことの確認（現状は無いと判断しているが
     明文化）。
  を満たしてからの恒久化（ガード解除・`asp3/target/esp32c5_espidf`側
  への統合）を推奨する。
- **MAC候補6語・LP RAM**：MAC候補6語は3節(B)/4節のとおり非因果と結論。
  LP RAMは3節(E)のとおりvacuousと結論（同一ブート内変動がノイズの
  水準を規定）。いずれも追加調査は不要と判断する。
- **候補0〜5，外部AI回答の全項目が本ラウンドで消化完了**——42ラウンドに
  わたる冷間0AP/TX無RX無の調査は，**根本原因の特定と因果注入による
  症状反転の確認**をもって実質的に解決フェーズへ移行した（恒久化は
  上記3項目の確認後）。Espressifへの問い合わせパッケージ（外部AI回答
  Q4）は，本ラウンドの結果により**不要**と判断する（原因を自力特定
  できたため）。

### 6. 両ボード最終状態（終了処理）

- **C5#1**：`build/c5_r34_wifi80`（240MHz出荷構成，実施42の変更点
  無効＝`ESP32C5_R42_APMFIX`未定義）を`write-flash 0x0`でフル4MB
  書き戻し。RTSリセット後45秒キャプチャ（`r42/final_state_console.log`）：
  `rst:0x1 (POWERON)`のみ・`esp_wifi_init -> 0`・`0 APs found (err=0)`→
  `RESCAN 0 APs (err=0)`×3——**冷間0AP症状の回帰を確認**（恒久修正は
  意図的に未適用のまま，次段の判断に委ねる）。0x200000は本ラウンドの
  ハンドオフ検証書込み後，本ステップのフル4MB書込みで意図的に消去済み
  （実施37罠どおり，次回ハンドオフ検証時は`r38_guest_640k.bin`の
  再書込みが必要）。
- **C5#2**：スニファ（2.4GHz ch6版，実施38以来）のまま未書換え・維持。
- 接触禁止2台（C6 board C `14:C1:9F:E0:5A:9C`／UARTブリッジ
  `125a266b…`）は全工程で無接触（by-idピン留め徹底）。

### 変更ファイル（実施42）

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  `esp32c5_r42_apm_unblock()`（`ESP32C5_R42_APMFIX`ガード，既定無効）
  を新設し，`hardware_init_hook()`末尾から呼出し。既定ビルドは
  nm symbol table完全一致（0行diff，`build/c5_r34_wifi80`対
  `build/c5_r42_regress`）・objdump -d完全一致（ビルド時刻文字列
  5バイトのみの差，10行diff）で無影響を確認済み。
- 本doc：本節（実施42）追記。
- スクラッチ（`260d98fa…/scratchpad/r42/`）：`pair_lui.py`（blob
  disasmからのlui+addi/oriペアリング，MAC/LP RAM/EFUSE/APM/TEE各
  候補領域への実アクセス列挙）・`r42_sweep.py`（APM/TEE＋MAC候補＋
  EFUSE＋LP_AON tail＋LP RAM統合採取ハーネス，動的レジスタ2パス
  フィルタ付き）・`cold_boot1/2.result.json`・`handoff_boot1.result.json`
  ・各`.uart.log`／`.openocd.log`・`apmfix_test1/2_dut.log`／
  `apmfix_test1/2_sniff.log`（因果注入実測）・`postfix_check2.openocd.log`
  （mechanism closure確認）・`final_state_console.log`（終了処理確認）。
  ビルド：`build/c5_r42_apmfix`（因果注入検証用）・`build/c5_r42_regress`
  （既定ビルド無影響確認用）。
- git commitは行っていない（コーディネータのレビュー後のcommit&push運用）。

### 検証（実施42）

- 机上：blob全disasm（`r41/full_disasm.txt`）からのlui+addi/oriペア
  リングでMAC候補89語・LP RAM 1語（自前計装の流用と判明）・EFUSE 3語
  を機械的に確定。ソース監査（esp-idf-v6.1）で`bootloader_init_mem()`
  の存在と`APM_MASTER_MODEM=4`を確認。
- 採取：冷間2ブート完全一致（HP_APM例外ラッチ含め再現性確認済み）・
  ハンドオフ1ブートとの系譜決定的差分を確認。非摂動性（採取後の
  同一ブートでRESCAN正常完走）を全ブートで確認。
- 因果注入：**2/2独立RTSリセットでAP検出（16-23AP）・スニファTX検出
  （DUTtotal=9・DUTprobereq=9）とも完全復帰**。mechanism closure
  （post-fix HP_APM_M1_STATUS=0，新規例外なし）を追加確認。
- 既定ビルド無影響：nm完全一致・objdump実質完全一致（ビルド時刻文字列
  のみ差）。
- 終了処理：C5#1を240MHz出荷構成（`ESP32C5_R42_APMFIX`無効）へ書き戻し，
  冷間0AP症状の回帰をUART実測で確認。C5#2は未書換え。接触禁止2台は
  全工程で無接触。

---

## 実施43：実施42のAPM/TEE修正を最小サブセット判別（filter解除・TEE昇格は独立にそれぞれ単独で十分と実機確定）のうえ恒久化——`ESP32C5_R42_APMFIX`診断ガードを撤去し`hardware_init_hook()`から無条件呼出しへ昇格。test_porting 6/6×2・冷間Direct Boot AP検出 2/2×2（診断ビルド／最終出荷ビルド）で全回帰確認。C5#1は恒久修正込み240MHz出荷構成へ書き戻し完了。実施12以来のWi-Fi bringup総括とC6への申し送りを記載

### 背景・目的

実施42でASP3のDirect Boot 0AP症状の根本原因をAPM (Access Permission
Management) / HP-TEEブロックのバスマスタ権限として特定し，因果注入で
症状の完全反転（0AP→16-23AP，TX無→TX有，2/2再現）を確認済み。ただし
実装は`ESP32C5_R42_APMFIX`診断ガード（既定無効）付きのままで，コーディ
ネータ指示（実施42「5. 総合評価と申し送り」）により以下3条件を満たして
からの恒久化が推奨されていた：(1) WiFi以外のC5アプリでの回帰確認，
(2) 「全4コントローラ解除＋全32マスタTEE昇格」という広い変更が最小変更
で足りるかの絞り込み，(3) ASP3側でAPM/TEEを本来使う想定が無いことの
確認。本ラウンドはこれら3条件の消化と恒久化，および実施12以来のWiFi
bringup全体の総括執筆を目的とする。

### 1. 環境確認

C5#1（`D0:CF:13:F0:A7:44`，JTAG=ttyACM2・UARTブリッジ`b04e3bcf…`=
ttyUSB0）・C5#2（`D0:CF:13:F0:C8:94`，JTAG=ttyACM1・UARTブリッジ
`3e7bd19f…`=ttyUSB2，実施38以来2.4GHz ch6スニファのまま）を`udevadm`/
`esptool chip-id`のMAC照合で確認。接触禁止2台（JTAG `14:C1:9F:E0:5A:9C`
=ttyACM0・UARTブリッジ`125a266b…`=ttyUSB1）は全工程で無接触。

### 2. 最小サブセット判別実験（1変数ずつ，各2ブート）

実施42の`esp32c5_r42_apm_unblock()`はstockの`bootloader_init_mem()`が
行う2処理――(1)4コントローラ（HP_APM/LP_APM0/LP_APM/CPU_APM）全ての
`FUNC_CTRL_REG`解除，(2)全32バスマスタの`TEE_Mn_MODE_CTRL_REG`を
TEEモード(0)へ昇格――を丸ごと再現していた。`ESP32C5_R43_APM_VARIANT`
マクロ（1=フィルタ解除のみ／2=master 4(MODEM)のみTEE昇格／3=既定，
両方=stock完全相当）を新設し，1変数ずつ実機判別した。

- **変種1（フィルタ解除のみ，TEE昇格なし）**：`build/c5_r43_filteronly`。
  ```
  boot1: 20 APs found → RESCAN 20/22/21/21/21          sniff DUTtotal=18
  boot2: 18 APs found → RESCAN 21/17/18/16/16          sniff DUTtotal=19
  ```
  **2/2で症状解消**（AP検出・TX放射とも成立）。
- **変種2（master 4のみTEE昇格，フィルタは有効のまま）**：
  `build/c5_r43_teeonly`。
  ```
  boot1: 14 APs found → RESCAN 17/16/18/21/18          sniff DUTtotal=13
  boot2: 20 APs found → RESCAN 22/21/23/19/20          sniff DUTtotal=15
  ```
  **2/2で症状解消**（変種1と同水準）。

**機構確認（JTAG post-boot読み戻し，`r43_apmcheck.py`）**：

| 変種 | HP_APM_FUNC_CTRL | HP_APM_M1_STATUS | TEE_M4_MODE_CTRL |
|---|---|---|---|
| filteronly | `0x00000000`（全解除） | `0x00000000`（例外なし） | `0x00000003`（REE2のまま） |
| teeonly    | `0x0000001f`（全経路有効=POR既定のまま） | `0x00000000`（例外なし） | `0x00000000`（TEEへ昇格） |

**結論**：HP_APMの許可判定は「経路フィルタが有効」かつ「マスタが非TEE」
の**論理積**で例外を出す一枚ゲートであり，直列に両方満たす必要はない
――どちらか一方の条件を外すだけでゲートが開く。フィルタが`0x1f`
（POR既定のまま有効）でもmaster 4だけをTEE化すれば例外は発生しない
（teeonly実測），逆にフィルタを解除すればmaster 4がREE2のままでも
例外は発生しない（filteronly実測）。したがって「最小サブセット」は
一意には定まらず，**(1)単独・(2)相当（master4のみ）単独のいずれも
独立に十分条件**という機構的理解を得た（実施21/22等の過去ラウンドで
繰り返し確認された「1変数ずつ判別」の方法論をそのまま適用）。

### 3. 恒久化の実装

コーディネータ指示どおり，恒久実装は**stock同等（両方）**を採用した
――理由は，stockが両方行うのは`bootloader_mem.c`のコメントにある通り
「TEE初期化コードが後段でマスタ単位のセキュリティモードを使い分ける」
前提のためであり，ASP3側で将来その使い分けを行う可能性を閉じないため
の保守的選択（2節の判別実験はあくまで機構理解目的であり，どちらか
片方だけを恒久実装として採用する動機は無い）。

`asp3/target/esp32c5_espidf/target_kernel_impl.c`：
- 旧`ESP32C5_R42_APMFIX`ガード（関数宣言・定義・呼出しの3箇所）を全て
  撤去し，`esp32c5_r42_apm_unblock()`を`hardware_init_hook()`末尾
  （実施41のBOOTHOOKと同じ位置，`esp_wifi_init`/PHY較正より前）から
  **無条件**呼出しへ昇格した。
- `hardware_init_hook()`は全C5アプリ（WiFi無効ビルドを含む）で実行
  されるため，本呼出しもWiFi無効ビルドを含め常時実行される（APM/TEE
  レジスタはWiFi有無に関係なく全ESP32-C5チップに存在するペリフェラル
  であり，アクセス許可を緩める処理のため実害は無い）。
- `ESP32C5_R43_APM_VARIANT`マクロ（既定=3=stock相当）は2節の判別実験
  用の診断フックとしてソースに残置（1/2を指定すれば当時の実験を再現
  できる。実施35の`ENABLE_REFUTED_CANDIDATES`と同型の運用）。
- 関数直上のコメントを実施42の発見内容＋実施43での恒久化・撤去内容へ
  更新。

### 4. 全回帰確認

**(a) 非WiFi回帰（test_porting）**：`build/c5_r43_tp`
（`ASP3_APPLDIR=asp3/asp3_core/test/porting`，`ASP3_EXTRA_APP_C_FILES=
.../tap.c`，`ESP32C5_WIFI=OFF`）。実機2ブート，両方とも：
```
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
**2/2で6/6維持**——APM/TEEレジスタへの書込みが非WiFiアプリの動作
（syslog/timer/task/semaphore/eventflag/alarm）に悪影響を与えないこと
を確認した。

**(b) WiFiフルパス（診断ビルド，`ESP32C5_R38_RXINSTR=1`のみ）**：
`build/c5_r43_permanent`。冷間Direct Boot 2ブート：
```
boot1: esp_wifi_init -> 0 / esp_wifi_scan_start -> 0 / 20 APs found → RESCAN 26/23/24/24/24  sniff DUTtotal=20
boot2: esp_wifi_init -> 0 / esp_wifi_scan_start -> 0 / 20 APs found → RESCAN 23/22/22/23/25  sniff DUTtotal=13
```
**2/2でAP検出・TX放射とも成立**。post-boot JTAG読み戻し
（`r43_apmcheck.py`）で`HP_APM_FUNC_CTRL=0`・`HP_APM_M1_STATUS=0`・
`TEE_M4_MODE_CTRL=0`・`LP_APM0/LP_APM/CPU_APM FUNC_CTRL`全て`0`を確認
――実施42のhandoff（stock経由，正常動作）実測値と完全一致するstock
相当の状態が，診断フラグなしで再現されていることをハードウェアの
状態そのもので確認した。

**(c) 最終出荷ビルド（診断フラグ完全無し，`ASP3_EXTRA_COMPILE_DEFS`
未指定）**：`build/c5_r43_shipping`。冷間Direct Boot 2ブート：
```
boot1: 20 APs found → RESCAN 24/23/25/26/23  sniff DUTtotal=14
boot2: 20 APs found → RESCAN 28/26/23/23/25  sniff DUTtotal=14
```
**2/2でAP検出・TX放射とも成立**——これが本ラウンドの終了処理で
C5#1へ書き戻す最終構成そのもの（5節参照）。

**(d) 既定ビルドの変更範囲確認**：`riscv32-esp-elf-objdump -d`で
`build/c5_r34_wifi80`（実施34時点の240MHz出荷構成，APM修正なし）対
`build/c5_r43_shipping`を比較。`nm`のシンボル名集合は完全一致（新規
関数は`static`かつ小さいため`hardware_init_hook()`へインライン化され
別シンボルとして現れない）。FLASH textサイズは494336→495184バイト
（+848バイト，追加コードの分のみ）。objdump全体の行差分は大きい
（後続コード全体のアドレスがシフトするため）が，これは実施34の
BBPLL修正時にも見られた予期される挙動であり，シンボル集合一致と
サイズ差の妥当性から意図しない変更が無いことを確認した。

### 5. ASP3側のAPM/TEE隔離依存の確認（ソース監査）

```
grep -rniE "apm_hal|apm_ll|apm_master|apm_sec_mode|tee_mode|APM_MASTER|apm_ctrl|SOC_APM" \
  asp3/ apps/
```
`asp3/target/esp32c5_espidf/target_kernel_impl.c`（本ラウンドで追加した
`esp32c5_r42_apm_unblock()`自身）以外に一致は無し。ASP3の`asp3/`・
`apps/`ツリー内にAPM/TEEのセキュア/非セキュア分離やLPコア分離を前提と
した既存コードは存在しないことをソースレベルで確認した——実施42の
申し送り「ASP3側でAPM/TEEを本来使う想定が無いことの確認」を満たす。

### 6. 悪影響チェック（JTAGデバッグ・flash書込み運用）

本ラウンド自体が，恒久修正込みのビルドに対して2節〜4節を通じて
OpenOCD JTAG（halt/mdw/resume）・esptool write-flash・UARTブリッジ
RTSリセットを多数回（filteronly/teeonly/permanent/shipping各2ブート＋
機構確認JTAG読み戻し3回）繰り返し実行しており，いずれも実施42以前と
同じ手順・所要時間で問題なく機能した。APM/TEEのアクセス制御緩和は
CPU（HPCORE，master 0）のアクセス経路には最初から影響しない
（実施42で確立済みの理解どおり，フィルタはHPCORE以外のバスマスタが
対象）ため，JTAGデバッガ自体のバスアクセスやSPI flashコントローラの
動作に変化はないと理論的にも予想され，実測もこれと整合した。

### 7. 総括：実施12〜43，Wi-Fi bringupの全体像

**壁の最終内訳（時系列）**：

| 順 | 壁 | 特定ラウンド | 内容 |
|---|---|---|---|
| 1 | ICG | 実施13 | PHY校正ループの原因＝WiFi BBクロックのICGゲート（PMU `icg_modem.code=0`）。stock相当のICG解除で修正 |
| 2 | CLIC割込み出口 | 実施28 | 最初のwake-from-idleでの`dispatch_r`復帰時，`mintstatus.mil`固着により全割込みが凍結。`irc_begin_int`のsynthetic `mret`によるmil即時降格で解消 |
| 3 | CPUルートクロック未切替 | 実施32 | Direct Bootは`PCR_SYSCLK_CONF.soc_clk_sel`をXTAL(0)のまま一度も切替えない（実CPUクロックはXTAL48MHz直結）。stockの`bootloader_clock_configure()`相当の明示的切替えを実装 |
| 4 | SWDキー | 実施33 | `LP_WDT_SWD_WPROTECT`の解錠キーが誤記（`0x8F1D312A`→正`0x50D83AA1`）。約8秒周期のSUPER_WDT_RESETでscanがハングしていた |
| 5 | BBPLL 40MHz XTALプロファイル | 実施34 | ROMがregi2c I2C_BBPLLブロックを誤って40MHz XTALプロファイルで較正（実効576MHz相当にロック）。正規の48MHzプロファイルで較正し直す修正を実装 |
| 6 | regi2cマスタICG | 実施34 | `I2C_MASTER`ICGドメイン（regi2cマスタ自体の機能クロック）がゲートされ第1回実装でregi2cが無反応になった副次発見。ICGドメインの解除順序を修正 |
| 7 | APM/TEE | 実施42→43 | WiFi/BTモデム（APM master id=4）がPOR既定のREE2モードのままHP_APMのctrl filterに遮断——ハードウェアのバスマスタ権限層。実施43で恒久化 |

全項目に共通する性質：**「Direct BootにおけるESP-IDF標準ブート列
（2nd-stage bootloader〜`call_start_cpu0`〜`esp_system_init`）相当
処理の欠落・ROM既定値の未検証仮定・（BBPLLのように）誤ったプロファイル
の借用値」ファミリー**である。ASP3のDirect BootはROM起動直後に自前の
`hardware_init_hook()`へ直接ジャンプし，stockが順に実行する多段の
初期化列（クロック確立・WDT解除・電源ドメイン・APM/TEE解除等）を
一切経由しない。42ラウンドにわたる調査は，実質的にこの「stockブート列
が暗黙に確立する前提条件」を1つずつ発見し直す作業だったと総括できる。

**なぜAPMは35ラウンド（実施15〜41）映らなかったか**：全ての静的
レジスタ診断はCPU（JTAG経由のMMIO read/write，regi2c較正の読み書き）
を主体としていたが，HPCORE（master 0）はPOR時点で既にTEEモードに
固定されており，そもそもHP_APMのフィルタ対象外である。したがって
CPU視点でのMMIOレジスタ値・regi2cトランザクション列は冷間/handoff間で
完全に正常一致し（実施15-25の全ビットスイープ含む），差分ゼロという
結果しか出なかった。ブロックされていたのはWiFi/BTモデム・ハードウェア
自身がバスマスタとして発行するアクセス（TX/RXパケットバッファへの
DMA的アクセス）であり，これは**CPU視点のレジスタ値としては一切現れ
ない**——「値の一致・不一致」という35ラウンドの探索パラダイム全体の
原理的な盲点だった（実施39のPMA発見＝Direct BootがROM残置PMAで
SRAM実行を禁止していた，と同型の「CPU自身の権限」対「CPU以外の
バスマスタの権限」という抽象度の違い）。

決め手となった実験は，実施42でのHP_APM例外ラッチの直接読取り
（`HP_APM_M1_STATUS_REG`=1・`M1_EXCEPTION_INFO0`=`0x00130001`
[mode=REE2, master_id=4=MODEM, region=1]・`M1_EXCEPTION_INFO1`=
`0x40806858`[HP SRAM内アドレス]）だった。これはハードウェアが自ら
記録した一次証拠（「誰が・いつ・どこへアクセスしようとして拒否
されたか」）であり，値の一致比較では絶対に出てこない**事象ログ**
だった点が，35ラウンドの静的値比較との質的な違いである。ソース監査
（`bootloader_mem.c`の`bootloader_init_mem()`とその呼び出しガード
`!defined(BOOTLOADER_BUILD)`）でAPM/TEEが「stockのAPPスタートアップが
必ず実行するが、ASP3のDirect Bootには対応する呼出しが存在しない処理」
であることを事前に特定できていたことが，この決め手実験へ直行できた
理由でもある。

**C6への申し送り（最重要，具体的手順）**：

`memory/project_c6_agc_investigation.md`に記録されている実施85での
held-over evidence gap「APM/TEE+PMP/PMA/cacheが85ラウンド未比較」は，
本ラウンドで確立したC5の知見と構造的に完全に符合する。C6のdeaf-RX
再開時，以下の手順を最優先で実施することを推奨する：

1. **C6のAPM実装確認（机上，実機不要）**：`~/tools/esp-idf-v6.1`の
   C6向けヘッダ（`components/soc/esp32c6/register/soc/`配下の
   `hp_apm_reg.h`/`tee_reg.h`等，または対応する`apm_types.h`の
   `apm_master_id_t`）を確認し，C6にWiFi/BTモデム用の
   `APM_MASTER_MODEM`相当のマスタIDが存在するか，レジスタオフセット
   がC5と同一か差分があるかを机上で特定する（C5とC6は世代が異なり
   APMブロックの構成が異なる可能性がある——本ラウンドの`r42_sweep.py`/
   `r43_apmcheck.py`のレジスタオフセットをそのまま転用できるとは
   限らない点に注意）。
2. **C6実機でのHP_APM例外ラッチ確認（実機JTAG，最優先）**：C6ボードで
   冷間Direct Boot後，`HP_APM_M1_STATUS_REG`相当（またはC6の対応
   レジスタ）がラッチされているか，`M1_EXCEPTION_INFO`にモデムの
   バスマスタIDが記録されているかを直接読む。これは実施85までの
   85ラウンドで一度も実施されていない，本質的に新しい観測点であり，
   C5同様に静的MMIO/regi2c比較では原理的に見えない情報である。
3. **ラッチがあれば**：C5の`esp32c5_r42_apm_unblock()`と同型の処理
   （C6のAPM/TEEレジスタオフセットへ移植）を注入し，因果検証
   （2/2再現）でdeaf-RX症状が反転するか確認する。
4. **ラッチが無ければ**：C6は本ラウンドの機序（APMバスマスタ遮断）
   ではないと確定でき，85ラウンドの「software/JTAG + physical-RF
   刺激investigation exhausted」の結論はそのまま維持される
   （実施85の残る選択肢(a)genuinely register-invisible analog RF
   characteristics／(b)実施70-72のPHY one-shot timing，へ回帰）。

**アーキテクチャ転換案（app_main→sta_ker）への影響**：実施42/43で
Direct Boot継続時代の根本課題（APM/TEE）が解決されたことにより，
Direct Bootを維持したままWiFi機能を完全に動作させることが技術的に
可能であると実証された。したがって，stockの`app_main`から`sta_ker`
（ASP3カーネル）へ制御を移す形へアーキテクチャを転換する動機は，
もはや「動かすために必須」ではなくなった。転換の採否は，今後
（a）ESP-IDFアップデート追従のたびに新たな「stockブート列の暗黙前提」
が発見される保守コストと，（b）Direct Boot固有の実装（本ラウンドまでに
蓄積した`hardware_init_hook()`内の多数のstock処理再現コード）を
維持するコストの比較という，**純粋な設計判断**としてユーザーに委ねる
のが適切である。本ラウンドはこの判断のための技術的材料（Direct Boot
経路が実際に完全動作することの実証）を提供するに留め，転換の当否には
踏み込まない。

**Espressif問い合わせの要否再評価**：実施42で「原因を自力特定できた
ため不要」と判断済みであり，本ラウンドの恒久化・全回帰確認はこの判断
を追認する。唯一の残る検討価値は，「APM/TEEのPOR既定値がWiFi/BT
モデムのバスマスタアクセスを（stockの`bootloader_init_mem()`を経由
しない限り）遮断する」という仕様が公式ドキュメント（Technical
Reference Manual等）に明記されているか，独自ブートローダ実装者向けの
既知の注意点として案内されているかどうかである。ESP-IDFのソース
コメント（`apm_hal.c`所在の説明文）には仕様の説明はあるが，これは
ESP-IDFを使わない独自ブートローダ実装者が見落としやすい非自明な罠
であり，Espressifへの問い合わせ（または将来のドキュメント改善提案）
としての価値はゼロではない。ただし本調査の完了に必須ではないため，
実施の要否はユーザー判断に委ねる。

### 8. 罠早見表への追記

| 罠 | 対処 | 出典 |
|---|---|---|
| OpenOCD telnet応答はコマンドエコー直後に1バイトの`\x00`を挟むことがあり，行分割ベースの簡易パーサだと`mdw`結果行が壊れて`None`を返す | 受信データ全体から`.replace("\x00", "")`してから行分割・パースする | 実施43 |
| `test/porting`（tap.c利用アプリ）のC5ビルドは`ASP3_EXTRA_APP_C_FILES=.../test/porting/tap.c`を明示しないと`tap_ok`/`tap_done`がリンクエラーになる | cmake呼出しに`-DASP3_EXTRA_APP_C_FILES=$PWD/asp3/asp3_core/test/porting/tap.c`を追加 | 実施43（過去ラウンドの実績を再確認） |
| `-DCMAKE_TOOLCHAIN_FILE=`に相対パスを渡すと，ビルドディレクトリが新規作成の場合にファイルが見つからないことがある | 常に`$PWD/asp3/asp3_core/cmake/toolchain-riscv64.cmake`のように絶対パスで渡す | 実施43 |

### 9. 両ボード最終状態（終了処理）

- **C5#1**：`build/c5_r43_shipping/asp_flash.bin`
  （**恒久修正込み**の240MHz出荷構成，`ASP3_EXTRA_COMPILE_DEFS`
  未指定＝診断フラグ完全無しの最終形）を`write-flash 0x0`でフル4MB
  書き戻し済み。冷間Direct Boot 2/2で`esp_wifi_init -> 0`→
  `esp_wifi_scan_start -> 0`→**20 APs found**→RESCAN 23〜28件を確認，
  スニファ側も両ブートでDUT検出（DUTtotal=14）——これが新しい出荷状態
  （4節(c)参照）。0x200000（ハンドオフ検証用ゲストバイナリ領域）は
  本ラウンドのフル4MB書込みで意図的に消去済み（実施37罠どおり，次回
  ハンドオフ検証時は再書込みが必要）。
- **C5#2**：スニファ（2.4GHz ch6版，実施38以来）のまま未書換え・維持。
- 接触禁止2台（JTAG `14:C1:9F:E0:5A:9C`／UARTブリッジ`125a266b…`）は
  全工程で無接触（by-idパスのみで照合，MAC/serial一致を都度確認）。

### 変更ファイル（実施43）

- `asp3/target/esp32c5_espidf/target_kernel_impl.c`：
  - `esp32c5_r42_apm_unblock()`を`ESP32C5_R43_APM_VARIANT`
    （既定=3=stock相当，1/2は実施43の判別実験用診断フックとして残置）
    で分岐可能な実装へ拡張。
  - 旧`ESP32C5_R42_APMFIX`診断ガード（宣言・定義・呼出しの3箇所）を
    撤去し，`hardware_init_hook()`から**無条件**呼出しへ恒久化。
  - 関連コメントを実施42の発見内容＋実施43の恒久化・最小サブセット
    判別結果へ更新。
- 本doc：本節（実施43）追記，罠早見表3行追加。
- スクラッチ（`260d98fa…/scratchpad/r43/`）：`r43_apmcheck.py`
  （APM/TEE主要レジスタのみを対象にした簡略JTAG読取りハーネス，
  `r42_sweep.py`のOOCDクラスを簡略転用・telnet `\x00`混入対策込み）・
  `filteronly_boot1/2_{dut,sniff}.log`・`teeonly_boot1/2_{dut,sniff}.log`・
  `permanent_boot1/2_{dut,sniff}.log`・`shipping_boot1/2_{dut,sniff}.log`・
  `filteronly_mech.openocd.log`／`teeonly_mech3.openocd.log`／
  `permanent_mech.openocd.log`（機構確認JTAG読み戻し）。
  ビルド：`build/c5_r43_filteronly`・`build/c5_r43_teeonly`・
  `build/c5_r43_both`（3節の変種3=既定値がリファクタ後も実施42の
  `c5_r42_apmfix`とobjdump完全一致＝ビルド時刻文字列のみ差，であることの
  確認用）・`build/c5_r43_permanent`（`ESP32C5_R38_RXINSTR=1`のみの
  診断ビルド）・`build/c5_r43_tp`（test_porting）・`build/c5_r43_shipping`
  （診断フラグ完全無しの最終出荷ビルド，C5#1へ書き戻し済み）。
- git commitは行っていない（コーディネータのレビュー後のcommit&push運用）。

### 検証（実施43）

- 最小サブセット判別：filteronly/teeonly各2/2独立RTSリセットでAP検出
  （14-22件）・スニファTX検出（DUTtotal=12-19）とも成立。JTAG機構
  確認でHP_APM_FUNC_CTRL/HP_APM_M1_STATUS/TEE_M4_MODE_CTRLの値が
  各変種の設計どおりであることを確認。
- リファクタ影響確認：`ESP32C5_R43_APM_VARIANT`導入後の変種3（既定値）
  ビルドが実施42の`c5_r42_apmfix`とobjdump上ビルド時刻文字列以外
  完全一致（10行diff中2行のみ日時差）であることを確認——リファクタが
  動作を変えていないことをバイナリレベルで保証。
- 非WiFi回帰：test_porting 2/2独立RTSリセットで6/6 PASS維持。
- WiFiフルパス（診断ビルド・最終出荷ビルドとも）：冷間Direct Boot
  各2/2でAP検出（14-28件）・TX放射（スニファDUT検出）を確認。post-boot
  JTAG読み戻しでHP_APM/TEE状態がstock handoff実測値と完全一致する
  ことを確認（mechanism closure）。
- ソース監査：`grep`でASP3の`asp3/`・`apps/`ツリーに
  `esp32c5_r42_apm_unblock()`自身以外のAPM/TEE依存コードが無いことを
  確認。
- 既定ビルド変更範囲確認：`nm`シンボル名集合完全一致・objdump差分は
  アドレスシフトのみ（FLASH text +848バイト，追加コード相当）。
- 悪影響チェック：本ラウンド全体を通じてOpenOCD JTAG（延べ約10回の
  attach/halt/resume）・esptool write-flash（延べ8回）が実施42以前と
  同じ手順・所要時間で問題なく機能したことを実地で確認。
- 終了処理：C5#1を恒久修正込み240MHz出荷構成（診断フラグ完全無し）へ
  書き戻し，冷間Direct Boot 2/2でAP検出・TX放射を最終確認。C5#2は
  未書換え。接触禁止2台は全工程で無接触。

## 実施44：TCP/UDP（lwIP）配線 — connect経路で新しい壁（WiFi blob内
`cnx_sta_associated`のLoad access fault）を発見。scan／build全域は
健全（回帰なし）。TCP/UDP実証はこの壁により未達（申し送り）

### 背景・目的

C3で実績のあるlwIP統合（`docs/tcpip-integration.md`）をC5へ横展開し，
**STA connect→DHCP→gateway ping→TCP echo→UDP echo**を実機で実証する
のが目標だった。C5でSTA connectを試すのは本ラウンドが初めて
（実施1-43はすべてscanまで）。

### 配線（コード変更）

`net/`層（`sys_arch.c`／`netif_esp32c3.c`／`lwipopts.h`／`cc.h`／
`sys_arch.h`／`net.cfg`／`net_cfg.h`）を精査した結果，**チップ固有の
レジスタ・アドレスに一切依存しない**（`esp_wifi_internal_tx`／
`esp_wifi_internal_reg_rxcb`／`esp_read_mac`等のblob APIのみに依存）
ことを確認した。esp_shim_libc.c等が既に確立している「chip非依存部は
`C3_TARGETDIR`から直接取込む」パターン（コピーしない）をそのまま
踏襲し，`asp3/target/esp32c5_espidf/target.cmake`に`ESP32C5_LWIP`
オプションを追加した（C3節と完全に対応する構造。ソースは
`${C3_TARGETDIR}/net/...`を直接参照，コピーはゼロ）。

`apps/wifi_dhcp`・`apps/tcp_socket_echo`・`apps/tcp_socket_client`・
`apps/udp_socket_echo`は`netif_esp32c3.h`をinclude・
`netif_esp32c3_*()`をそのまま呼ぶC3向けコードだが，関数名・型が
chip非依存のため**アプリ側は無変更でそのままC5でも使えた**
（include pathに`${C3_TARGETDIR}/net`が乗るため解決する）。

### ビルド確認（全て成功）

- `wifi_dhcp`（`-DESP32C5_WIFI=ON -DESP32C5_LWIP=ON`）：RAM 81.26%
  （384KB中）。
- `tcp_socket_echo`／`udp_socket_echo`（2.4GHz＝`WIFI_SSID="<SSID-2G>"`，
  5GHz＝`WIFI_SSID="<SSID-5G>"`のそれぞれで個別ビルド）：RAM
  82.33%（4ビルドとも成功）。
- `wifi_scan`回帰ビルド（`ESP32C5_LWIP`無指定）：RAM 76.22%，正常。

### 実機検証：connect到達も新しい壁で停止

`tcp_socket_echo`（2.4GHz）をC5#1へ書込み，UARTブリッジで観測：

```
mode : sta (d0:cf:13:f0:a7:44)
tcp_socket_echo: esp_wifi_connect -> 0
(connect自体は受理された)
Load access fault.
t0 = 0x420019fe, t1 = 0x0000000f, t2 = 0x00000001, t3 = 0xf427264b
...
a0 = 0x4080b8b8, ...
ra = 0x42045d7a, tp = 0x40839c10
pc = 0x42045dca, mstatus = 0x00001881
```

`esp_wifi_connect() -> 0`（受理）後，AP側からの応答（association）を
処理する過程でWiFi blob内部の`cnx_sta_associated`（`riscv32-esp-elf-
addr2line`で確認，シンボルは`cnx_sta_associated`）が**Load access
fault**で落ちる。**scan未到達だったこれまでの壁（実施1-43）とは
別の，connect経路にのみ存在する新しい壁**である
（★注意で予告されていた「connect経路で新しいシム欠落が露見する
可能性」が的中）。

### JTAG追加調査（1ラウンド分に限定）

OpenOCD（`adapter serial D0:CF:13:F0:A7:44`）でhaltすると，OpenOCD
自身が**`Halt cause (5) - (PMP Load access fault)`**と表示し，CSR
読取りで`mepc=0x42045dca`（fault命令と一致）・`mcause=0x38000005`・
`mtval=0x00000004`を確認した（複数回のflash+hard-reset独立試行で
再現性あり，値も完全一致）。静的逆アセンブル（`riscv32-esp-elf-
objdump`）ではfault命令は`lbu a5,535(s1)`で，直前の直線コードでは
`s1`にblobのグローバル`g_ic`（`0x4084cfb0`，`.bss`内・有効なマップ
済みRAM，`SOC_DRAM_HIGH=0x40860000`の範囲内）が代入される経路に
見えた。ただし**fault時点のs1の実測値は取得できていない**：

- ハードウェアブレークポイント（`bp 0x42045dca 4 hw`）を設置して
  `resume`しても，フラッシュ起動直後の自然な実行では一度もbreakに
  ヒットせず，代わりに（bp未経由で）自然にfaultした状態でhaltが
  返る＝s1はfaultハンドラのその後の実行で上書き済みのstale値
  （`0x00000003`，2回とも同一値だが物理的に無意味）しか読めなかった。
- gdb（`riscv32-esp-elf-gdb` 16.2）で`monitor reset halt`→
  `break *0x42045dca`→`continue`を試したところ，JTAGリセット
  （デバッグモジュール経由のCPUリセット）からの起動では60秒待っても
  一度もconnect経路（このPC）へ到達しなかった＝JTAGリセットが
  実機POR/esptool hard-resetと同じブート系列を再現していない可能性
  （C5では既知の罠カテゴリ．真の初期化が必要なWiFi/PHY周辺機構が
  JTAGリセットで完全には再現されない）。
- PMP（`pmpcfg0/1=0`＝全エントリOFF）・PMA（`pma_cfg0-11`は`addr=0`
  で未使用，アクティブな`pma_cfg12-15`は`0x10xxxxxx`/`0x14xxxxxx`
  帯＝flash関連領域のみを covers し，RAM領域`0x408xxxxx`には一切
  掛かっていない）を実読したが，いずれもこのfaultを直接説明しない。
  JTAGのメモリ読取り（`mdw`．デバッグモジュール経由のSBAアクセス）
  では該当アドレス（`0x4084d1c0`・`0x4084cfb0`）は正常に読めた
  （CPU発行のloadとは異なる経路のため，これはCPU側の制限を否定する
  証拠にはならない）。

**結論（厳密性を保った記述）**：OpenOCDは本faultを「PMP Load access
fault」と分類しているが，標準PMP・Espressif PMAいずれも該当領域を
制限していないことを実測で確認しており，**真因は未確定**。s1の
fault時点の実測値が取れなかったため，「有効アドレスへのCPU権限系
拒否」と「未初期化ポインタによるnear-nullデリファレンス」の2つの
仮説は現時点では**判別できていない**（後続ラウンドへの最優先課題）。

### 5GHz（`<SSID-5G>`）検証：同じ壁でブロック

コーディネータ経由の追加指示により，C5はデュアルバンド機であるため
5GHz AP（`<SSID-5G>`，パスワードは2.4GHz側と同一）への接続も
検証対象に追加された。`tcp_socket_echo`／`udp_socket_echo`の5GHz版
ビルド（`-DASP3_EXTRA_COMPILE_DEFS='WIFI_SSID="<SSID-5G>";...'`）
は成功したが，**5GHz接続も2.4GHzと全く同じconnectコードパス
（`cnx_sta_associated`）を通るため，同じ壁でブロックされる**（実機
検証は2.4GHz側の壁が解消してから行うのが合理的であり，本ラウンドでは
5GHz側の実機投入は行わなかった）。なお`wifi_scan`では`<SSID-5G>`
がAP一覧に検出されること自体は確認済み（下記スキャン回帰ログ参照）
＝5GHz APは電波環境として実在・到達可能。

### 回帰確認（全て健全）

- **wifi_scanの実機回帰**：C5#1へ`build/c5_r44_scan_regress`
  （`ESP32C5_LWIP`無指定の素のscanビルド）を書込み，`20 APs found`
  →`RESCAN 28 APs`を確認。一覧に`<SSID-2G>`（ch1）・`<SSID-5G>`
  の両方が含まれることを確認（一部行はWiFi blobの内部ログとUART上で
  競合し文字化けするが，スキャン自体の完走・件数は明瞭に確認できた）。
  scanは退行していない。
- **C3ビルド回帰**：`ESP32C3_LWIP=ON`構成（`tcp_socket_echo`）を
  フルビルド，RAM 95.98%で成功（既存ドキュメント値95.90-95.91%と
  同水準，差は本ラウンドで対象アプリを変えたことによる差）。C3の
  `net/`ソース自体は一切変更していない。
- 変更ファイルは`asp3/target/esp32c5_espidf/target.cmake`
  （`ESP32C5_LWIP`節追加）のみ。C3側の`net/`・`wifi/`・`apps/`は
  無変更のため，回帰リスクは構造的に低い。

### C6側の状況（詳細は`docs/wifi-shim-c6.md`実施89参照）

同一パターンで`ESP32C6_LWIP`を配線・ビルド成功（`tcp_socket_echo`/
`udp_socket_echo`ともRAM 95.08%）。C6は接続まで到達する前に，
コンソール出力がWiFi blobの内部ログ（ROM printf系と推測される）と
ASP3 syslogタスクの出力がUART上で競合し文字化けする問題があり
（`build/c6_wifi_scan_uart`の実施88時点で検証済みのバイナリを本
ラウンドで再書込みしても同じ文字化けパターンが再現），本ラウンドの
セッション内では"N APs found"のクリーンな確認が取れなかった
（board Cのscan機能自体が壊れているかは未確定——コード上の回帰では
ないことは，実施88の検証済みバイナリを無変更のまま再書込みして
同じ文字化けが再現したことから確認済み）。

### 環境メモ（本ラウンドのビルド環境固有の発見）

C6のWiFiビルドはEspressifの`riscv32-esp-elf-gcc`14.2.0では**ビルド
できない**（`hal/components/esp_phy/src/phy_init.c`の`_lock_acquire`/
`_lock_release`，`mbedtls`の`fgets`/`setbuf`が`hal_stub`の意図的に
最小限なスタブヘッダでは宣言されず，GCC14はimplicit-function-
declarationを既定でerror化するため）。実施88等，過去の成功ビルドは
すべて**デフォルトツールチェーン**（`/usr/bin/riscv64-unknown-elf-
gcc` 13.2.0，`-DRISCV64_TOOLCHAIN_PREFIX`を指定しない＝警告のみ）を
使っていたことを`CMakeCache.txt`の`CMAKE_C_COMPILER_AR`から突き止めた
（本リポジトリの変更ではなく，環境のツールチェーン選択の問題。C5は
`~/tools/esp-idf-v6.1`側の`phy_init.c`を使うため影響されない）。
本ラウンドはこの発見以降，C6ビルドはすべてデフォルトツールチェーンで
統一した。

### 終了処理（両DUT最終状態）

- **C5#1**：`build/c5_r44_scan_regress/asp_flash.bin`（`ESP32C5_WIFI`
  のみ・`ESP32C5_LWIP`無し・診断フラグ無し，実施43出荷構成相当の
  素のscanビルド）へ書き戻し済み。connectで確実に落ちる
  `tcp_socket_echo`ビルドは残していない。
- **board C（C6）**：`build/c6_wifi_scan_uart/asp_flash.bin`
  （実施88の検証済みバイナリそのもの，無変更）を再書込みした状態の
  まま。
- 接触禁止2台（C5#2・board B）は本ラウンドで無接触。

### 変更ファイル（実施44）

- `asp3/target/esp32c5_espidf/target.cmake`：`ESP32C5_LWIP`オプション
  節を追加（C3の`net/`を`C3_TARGETDIR`経由でそのまま再利用。ソース
  コピーなし）。
- `asp3/target/esp32c6_espidf/target.cmake`：同パターンで
  `ESP32C6_LWIP`を追加（詳細は`docs/wifi-shim-c6.md`実施89）。
- `apps/`・C3側`net/`・`wifi/`は無変更。
- git commitは行っていない（コーディネータのレビュー後commit&push
  運用）。

### 申し送り（次ラウンド最優先）

1. **`cnx_sta_associated`のLoad access faultのs1実測**：自然boot
   （フラッシュ書込み直後のhard-reset）ではJTAG hw breakpointが
   ヒットしない一方，JTAGリセットからの起動ではconnect経路へ到達
   しない，という新しい罠が判明した。ソフトウェアブレークポイント
   （`ebreak`命令の一時書換え）や，esptool hard-reset直後に間を
   置かず素早くattachしてbp設置する等の手法変更が必要。
2. C6のUART文字化け問題（WiFi blob ROM printfとASP3 syslogタスクの
   競合と推測）の切り分け：JTAGでのAP検出数直接確認（`wifi_scan`の
   結果変数をmdwで読む）等，UARTに依存しない手法へ切替えるとよい。
3. 5GHz実機接続は，2.4GHz側の壁解消後に着手するのが効率的。

## 実施45：★connectクラッシュ根治——真因＝blobグローバル`g_misc_nvs`のNULLデリファレンス（シムの旧定義はROM ldに上書きされリンクすらされていなかった）。修正1行で**C5史上初のconnect→DHCP→ping→TCP/UDPエコー全マトリクス完走（2.4GHz/5GHz両帯）**。5GHz実通信の初実証（ch48/RSSI-61）込み

### 背景・目的

実施44の申し送り＝`esp_wifi_connect()`受理後にWiFi blob内
`cnx_sta_associated`で発生するLoad access fault
（mepc=0x42045dca・mcause=0x38000005・mtval=0x00000004）の根治と，
C5でのconnect→DHCP→gateway ping→TCPエコー→UDPエコー全マトリクス
（2.4GHz→5GHz）の完遂。

### 手法1：DEF_EXCフォルト捕捉ハンドラの移植（実施06/08の手法）

実施44で「JTAG hw-bpは自然ブートで不着火／JTAGリセットブートは
connect経路に到達しない」という二律背反が確認済みのため，live-bpを
放棄し，実施06/08で確立したDEF_EXC捕捉方式を`apps/wifi_dhcp`へ移植
した：

- `fault_capture_handler()`（wifi_dhcp.c）：mepc/mcause/mtval＋
  T_EXCINFの全保存レジスタ＋**callee-saved s0-s3/sp**（T_EXCINFに
  含まれないため`register uint32_t x asm("s1")`で関数先頭値を直接
  束縛して回収——例外エントリはcallee-savedを変更しないため
  フォルト瞬間の値がそのまま取れる）をRTC-RAM `0x50000000`
  （magic `0xFA017C05`）へ保存し，無限ループで凍結。
- `wifi_dhcp.cfg`：`DEF_EXC(EXCNO_FAULT_FETCH/IINST/MISALIGNED_LOAD/
  FAULT_LOAD/MISALIGNED_STORE/FAULT_STORE)`の6例外を登録
  （EXCNO_*は全riscv_gccターゲット共通の数値定数．cfgの`#ifdef`は
  cfg独自プリプロセッサでCMakeの`-D`を見ないため無条件登録とした．
  フォルトしない限り無害＝C3ビルド回帰も後述のとおり確認済み）。
- 回収は自然ブート（esptool hard-reset）→凍結後にOpenOCDで
  **リセット無しattach**→`mdw 0x50000000 32`。実施44の罠
  （JTAGリセット系の非再現性）を完全に回避できた。

### 物証（フォルトレジスタ・独立2ブート＋ビルド3種で完全一致）

```
magic  = 0xFA017C05
mcause = 0x38000005（Load access fault）
mtval  = 0x00000004（＝NULL+4）
mepc   = cnx_sta_associated+0xea（lbu a5,535(s1)）
         ※実施44の0x42045dca も当該ビルドの同+0xeaと一致（nm照合済み）
ra=cnx_sta_associated+0x9a, a0=0x4080b8b8(wifiタスクスタック内),
s1=g_ic（有効な.bss内アドレス）, s2=0x40860000, mstatus=0x1881
```

**決定的な矛盾と解消**：mepcの命令`lbu a5,535(s1)`はs1=g_ic（実測，
callee-savedのため例外時値がそのまま残る）なら有効アドレス
`g_ic+535`のロードであり，mtval=4を説明できない。一方mtval=4を
説明できる唯一の近傍ロードは**mepcの直前の分岐元**：

```
cnx_sta_associated+0x9a:  lw a5,g_misc_nvs   # 0x4085ff7c から読む
                +0xa2:  lw a4,4(a5)        # ★a5=NULL → アドレス4のロード
                +0xa4:  beqz a4, +0xea     # 分岐先＝mepcの命令
```

＝**C5のロードアクセスフォルトはバスエラー到着が遅い非精密
（imprecise）配送**であり，mepcは真のフォルト命令（+0xa2）ではなく
「フォルトしたロードの結果0で分岐した先（+0xea）」を指す。
JTAG実測で凍結状態の`g_misc_nvs`（0x4085ff7c）＝**0x00000000**を
確認——mtval=4＝NULL+4と完全整合。

**プローブ実験（フォルト位置解釈の裏取り）**：`g_ic[535]`の
CPUロード自体は (a)起動直後（esp_wifi_init前）・(b)esp_wifi_start後
connect直前，の両時点で正常に完走（`ESP32C5_R45_PROBE_GIC`計装，
RTC-RAMマーカで確認）＝「g_icや当該オフセットのロードがフォルトする」
説を直接棄却し，非精密配送＋g_misc_nvs説を裏付けた。

**HE/AX無効化A/B（棄却）**：`_wifi_disable_ac_ax`→true（HE無効）の
A/Bビルドでも同一フォルトが再現＝HE機能ゲートは無関係と棄却
（計装は撤去済み）。

### 根因（2段階の構造的問題）

1. **シムの旧定義`void *g_misc_nvs = NULL;`はリンクされていなかった**。
   `esp32c5.rom.net80211.ld`（ROM ldスクリプト）が
   `g_misc_nvs = 0x4085ff7c;`（ROMインターフェースデータ領域＝
   実施27の0x4085fb80〜0x4085ffc4帯）を供給し，リンカスクリプト定義が
   オブジェクト定義に優先するため（nmで`A`＝絶対シンボル解決を確認）。
   ＝このセルの初期化は実行時代入でしか行えない（POR後は0）。
2. **v9 blobは`g_misc_nvs`をNULLチェック無しでデリファレンスする**。
   全9参照サイト（cnx_sta_associated/cnx_auth_done/
   scan_profile_check/scan_parse_beacon/ieee80211_assoc_req_construct/
   ieee80211_sta_new_state×2/sta_rx_eapol/
   esp_wifi_get_wps_status_internal）を逆アセンブルで確認，全てが
   +4（WPS有効フラグ相当）・+8（WPSステータス，2/3と比較）を
   直接lwする。connect経路（association）で初めて到達するため
   scan onlyの実施1-43では露見しなかった。
   **C3の同blob世代コードも全く同じ無ガード構造**（C3ビルドELFの
   `cnx_sta_associated`逆アセンブル照合済み）だが，C3ではアドレス4の
   ロードがトラップせずバスが黙って値を返すため「動いていた」＝
   C3は潜在バグを踏み抜けていただけ，という新事実も判明。

### 修正（1件）

`asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`：
blobが`esp_wifi_init`内（`wifi_init_in_caller_task`，jal実測で確認）
で必ず呼ぶ`misc_nvs_init()`で，静的ゼロ構造体
`misc_nvs_storage[16]`（.bss＝64バイト）を`g_misc_nvs`へ代入する
よう変更（C定義は`extern`宣言へ．deinitでもNULLへは戻さない）。
全参照が+4/+8の読みだけであるため，全ゼロ＝「WPS無効・NVS無し」の
安全な振る舞いになる。修正後objdumpで
`sw a5,386(a4) # 4085ff7c <g_misc_nvs>`（=&misc_nvs_storage書込み）を
確認，実機JTAG読み戻しで`g_misc_nvs=0x408333b8`（=storage実体）・
フォルトmagic不発生を確認（mechanism closure）。

### 決定実験：全マトリクス（★全項目PASS）

DUT＝C5#1（`D0:CF:13:F0:A7:44`，UARTブリッジ`b04e3bcf…`＝by-id照合），
AP＝2.4GHz `<SSID-2G>`／5GHz `<SSID-5G>`（認証情報は非コミット，
CMakeの`ASP3_EXTRA_COMPILE_DEFS`で注入）。

| 帯域 | connect | DHCP | gateway ping | TCPエコー(port8) | UDPエコー(port9) |
|---|---|---|---|---|---|
| 2.4GHz | PASS（STA_CONNECTED，独立2ブート） | PASS（192.168.1.70） | PASS（DUT発・連続OK／ホスト発2/2） | PASS（3メッセージ完全一致） | PASS（3メッセージ完全一致） |
| 5GHz | **PASS（channel=48, rssi=-61）＝C5の5GHz実通信初実証** | PASS（192.168.1.70） | PASS（DUT発・連続OK／ホスト発2/2） | PASS（3メッセージ完全一致） | PASS（3メッセージ完全一致） |

- wifi_dhcp（2.4GHz）は独立2ブートで connect→DHCP→ping を再現
  （フォルト捕捉領域をJTAGでクリアしてから再ブート→magic不発生も
  同時確認）。
- TCP/UDPエコーはホストから`nc`/`nc -u`で各3メッセージ送信・
  エコーバック一致（往復）を確認。5GHz側はtcp/udpビルドがそれぞれ
  独立のconnect+DHCPを完走しているため，5GHz接続は計3回独立再現。
- 接続チャンネル/RSSIの実測は本ラウンドで`wifi_dhcp`に追加した
  `esp_wifi_sta_get_ap_info()`のログで記録（5GHz=ch48/RSSI-61）。

### 回帰確認

- **scan回帰（C5）**：修正込み`wifi_scan`ビルド（`ESP32C5_LWIP`無し）
  で20 APs found→RESCAN 31-34 APs，`<SSID-2G>`(ch1)/`<SSID-5G>`
  両SSID視認＝実施44（20-37APs）と同水準，退行なし。
- **C3ビルド回帰**：共有アプリ`apps/wifi_dhcp`を編集したため
  C3構成（`ESP32C3_WIFI=ON/LWIP=ON/QEMU=OFF`，デフォルト
  ツールチェーン=riscv64-unknown-elf-gcc 13.2）でフルビルド成功
  （RAM 94.72%）。C3側`net/`・`wifi/`・targetファイルは無変更
  （並行C3負荷試験セッションの作業ファイル`esp32c3_espidf/wifi/
  esp_shim.c`の変更は本ラウンドとは無関係・無接触）。
  ※Espressif riscv32-esp-elf-gcc 14.2でのC3 WiFiビルド不成立は
  実施44記載の既知の環境問題（本変更とは無関係）を再確認したのみ。
- C5の`esp_wifi_adapter.c`はHE A/B計装を完全revert（`git diff`空）。

### 変更ファイル（実施45）

- `asp3/target/esp32c5_espidf/wifi/esp_shim_blobglue.c`：恒久修正
  （misc_nvs_init→静的ゼロ構造体代入）＋実施45知見のコメント全面
  書換え。
- `apps/wifi_dhcp/wifi_dhcp.c`／`.cfg`／`.h`：DEF_EXCフォルト捕捉
  ハンドラ（恒久の診断資産として残置，フォルト無しなら無害）・
  `ESP32C5_R45_PROBE_GIC`ガード付きg_icプローブ（既定無効）・
  接続AP情報（channel/RSSI）ログを追加。
- git commitは行っていない（コーディネータのレビュー後commit&push
  運用）。

### C5#1最終状態

`build/c5_r45_scan_regress/asp_flash.bin`（**恒久修正込み**・
`ESP32C5_WIFI`のみ・`ESP32C5_LWIP`無し・診断フラグ無しの素のscan
ビルド）をフル4MB `write-flash 0x0`済み。scan完走（20 APs→RESCAN
31-34 APs）を書込み後に実機確認済み。TCP/UDP動作ビルドは
`build/c5_r45_{tcp,udp}_socket_echo{,_5g}`に保存（いずれも修正込み，
必要なら書込みだけで再現可）。

### 申し送り・示唆

1. **C3/C6への水平展開（推奨・低優先）**：C3のblobも同一の
   無ガードg_misc_nvsデリファレンスを持つ（実測でアドレス4読みが
   トラップしないため動作しているだけ）。C3/C6の
   `esp_shim_blobglue.c`にも同じmisc_nvs_init修正を入れるのが
   安全側（C6はROM ldに`g_misc_nvs = 0x4084ff7c;`があり，C5と同じ
   「C定義がリンクされない」構造を持つ可能性が高い——要nm確認）。
   本ラウンドはC3並行作業への非干渉のため見送り。
2. **C5の非精密Load access fault**：mepcが真のフォルト命令を
   指さない（バスエラー遅延配送でmtvalだけが真実を語る）ことを
   実測で確定。今後のC5フォルト調査ではmepcを信用せず，
   「mtvalと整合するロード命令を近傍から探す」こと。callee-saved
   レジスタはハンドラで直接回収できる（本ラウンドの手法を流用可）。
3. `no time event is processed in hrt interrupt.`ログが通信中に
   多発する（実害は未観測・通信は全て正常）。将来のスループット
   試験時に要観察。

## 実施46：負荷試験（S3欠陥A移植＋TCP/UDP持続負荷）——欠陥AはC5にも実在し固定プール化を移植・★C3の「負荷誘発リンク完全停止」がC5でも完全再現（3/3run・2.4GHz/5GHz両帯・同一シグネチャ）＝C3固有ではなく家族的バグと確定

### 目的・位置づけ

C3負荷試験ラウンド（`docs/load-test-c3c5c6.md`実施1）の2つの成果——
(1) S3欠陥A（キューのメッセージ毎malloc・ISR内malloc）の固定プール化
修正，(2) 新規発見「負荷誘発リンク完全停止」——をC5へ横展開し，特に
(2)の**C5での再現有無**（＝C3固有か家族的バグかの横断判別）を確認する。

**注記（並行作業への非干渉）**：`docs/load-test-c3c5c6.md`は本ラウンド
時点でC6水平展開エージェントが編集中のため，本ラウンドの結果は本節
（c5-bringup.md実施46）に記録する。負荷試験ドキュメントへの統合は
コーディネータが後で行う。

### 1. S3欠陥AのC5照合と移植

`asp3/target/esp32c5_espidf/wifi/esp_shim.c`のキュー実装
（旧432〜547行：`esp_shim_queue_create`/`_delete`/`_send`/
`_send_from_isr`/`_recv`）を行単位照合した結果，**C3修正前と同一の
malloc-per-messageパターン＝欠陥AはC5にも実在**：
- `_send`/`_send_from_isr`が送信毎に`esp_shim_malloc(q->item_size)`
  （`_send_from_isr`はWiFi MAC ISR文脈から呼ばれうる）
- `_recv`が受信毎に`esp_shim_free`

C3の固定プール化修正（S3 commit dd7a76d移植）を**無改変で移植**した
（`SHIM_QUE`にpool/free_stk/depth/free_top追加・生成時1回確保・
スロット番号をDTQで運ぶ・`shim_que_slot_alloc/free`ヘルパ）。C5固有差
は皆無：`SHIM_LOCK`/`SHIM_UNLOCK`はC3と同一実装，`esp_shim_cfg.h`/
`esp_shim.cfg`はC3_TARGETDIR経由でC3と共有（`ESP_SHIM_DTQ_CNT=256`）。
C3独自の`esp_shim_queue_reset`（NimBLE用）はC5に存在しないため移植
不要（コメントに明記）。

欠陥B（動的TXバッファ）はC3と同様**opt-in扱い**（下記アプリの
`-DLOAD_TEST_STATIC_TXBUF`。既定OFF・本ラウンドでは実機未使用）。

実機での修正動作確認：全ブートで
`esp_shim: queue create depth=200 item=8 pool=1600B`（WiFiドライバ
タスクのイベントキュー200深がプール化されて生成）をログ確認。
connect/DHCP/echo全機能が修正込みで動作（回帰なし）。

### 2. 負荷試験アプリとビルド

新規`apps/load_test_c5/`（`apps/load_test_c3`の複製・C5調整。
load_test_c3自体は無変更）：TCP echo（port 8）＋UDP echo（port 9）＋
mon_task（5秒周期でheap_free・累積カウンタをsyslog）。C5調整点は
(1) `esp_wifi_sta_get_ap_info()`のchannel/RSSIログ（帯域証跡．
wifi_dhcp実施45由来），(2) DEF_EXCフォルト捕捉ハンドラの安全網同梱
（実施45の機構移植。停止がCPU例外由来である可能性を排除するため）
の2点。

ビルド（ツールチェーン=riscv32-esp-elf-gcc 14.2.0，いずれも成功）：
- `build/load_test_c5_qfix`（2.4GHz `<SSID-2G>`）：RAM 83.95%
- `build/load_test_c5_qfix_5g`（5GHz `<SSID-5G>`）：RAM 83.95%
- `build/c5_r46_scan_regress`（wifi_scan・LWIP無し・シム修正込み）：
  RAM 76.25%＝実施45の76.22%と同水準（ビルド回帰なし）
認証情報はASP3_EXTRA_COMPILE_DEFSで注入（docs非記載・非コミット）。

### 3. 負荷試験の実施と結果

**環境**：DUT＝C5#1（`D0:CF:13:F0:A7:44`，UARTブリッジ`b04e3bcf…`，
全操作by-id照合）。ホスト＝同一LAN有線192.168.1.48。DUTのDHCP取得IP
=192.168.1.70（全run同一）。ホスト側ハーネスはC3実施1と同一系
（スクラッチ`load_test.py`：TCP 1400B持続ストリーム＋UDP 512B・
50pps・シーケンス番号内容検証＋`ping -D`2秒周期疎通プローブ＋
シリアル持続ログ）。**判定基準（事前固定・C3と同一）**：ヒープ
非減少／エコー完全一致／10分完走 or 停止シグネチャの精密記録。

**C5固有の手順注意**：CP2102NブリッジをpyserialでopenするとDTR/RTS
アサートで**チップがリセットされる**（実施39の既知ハザード。open直後
のsetDTR/RTS(False)でダウンロードモード落ちは防げるがリセット自体は
不可避）。このため各runは「シリアルロガー起動（＝リセット）→25秒
待ち（boot+connect+DHCP）→ホストping precheck→負荷印加」の順で
駆動した（毎runクリーンブートになる利点もある）。

**アイドルベースライン**：クリーンブート後の無負荷観測（約219秒窓）で
デバイス発ゲートウェイping連続OK・heap完全一定・ホスト発ping 208/219
（t+200〜210s付近に約4秒の欠落2回のみ＝RF環境の一過性ロス，持続的
停止なし）。＝**アイドルでは停止しない**（C3と同じ）。

#### ★主結果：C3の負荷誘発リンク完全停止がC5で完全再現（3/3 run）

| run | 帯域 | 負荷時間 | 停止まで | 停止前エコー実績（完全一致） | 停止時heap_free | デバイス側エラー |
|---|---|---|---|---|---|---|
| 1 (qfix600) | 2.4GHz ch1/RSSI-58 | 600s | **約2〜4s** | TCP 9,680B(ホスト)/8,608B(DUT計数)・UDP 64dgram=32,768B | 158,760（一定） | 0 |
| 2 (repro180) | 2.4GHz ch1 | 180s | **約4〜6s** | TCP 15,396B/14,324B・UDP 105dgram | 158,752（一定） | 0 |
| 3 (g5_180) | **5GHz ch48/RSSI-61** | 180s | **約8〜10s** | TCP 18,612B/17,540B・UDP 289dgram=147,968B | 158,736（一定） | 0 |

（TCPのホスト受信値とDUT計数値の差は，UARTログの既知の文字化け・
MONサンプリング境界による記録誤差の範囲。UDPはDUT計数=dgram数×512B
が3run全てで厳密一致）

**停止シグネチャ（C3実施1の記録項目と同一・3run全て一致）**：
- **双方向完全死**：ホスト発ping死（run1は負荷開始+4s以降0/21，
  run2は+6s以降0，run3は+10s以降0）＋**デバイス発ゲートウェイping
  も同時刻に停止**（ping OKログが負荷開始直後を最後に消失）＝TX方向
  死亡を含む
- `STA_DISCONNECTED`イベント一切なし（blobは接続中と認識のまま）
- `heap_free`完全一定（断片化・リークなし）
- デバイス側エラーカウンタ0・パニックなし・**CPU例外なし**（DEF_EXC
  フォルト捕捉ハンドラ同梱＝フォルト時は凍結するはずだが，MONログは
  5秒周期で正確に出続けた。run1は停止後さらに約10分，計uptime=685s
  まで健全稼働を確認）
- 不回復：run1は停止後約9.7分の継続負荷中TCP再接続104回全て失敗・
  UDP 1,147連続ロスト。run2は負荷終了後の約2分アイドルでも回復なし
- リセット（次runのブート）では毎回正常に再接続（3/3）

#### C3とのシグネチャ比較（横断判別の結論）

| 項目 | C3実施1 | C5実施46 |
|---|---|---|
| 停止までの時間 | 約5〜30s（変種により） | 約2〜10s |
| 双方向死（デバイス発TX含む） | 一致 | 一致 |
| STA_DISCONNECTEDなし | 一致 | 一致 |
| heap一定・エラー0・カーネル健全 | 一致 | 一致 |
| アイドルでは発生しない | 一致 | 一致 |
| 自然回復なし・リセットで復旧 | 一致 | 一致 |
| エコー内容（停止前） | バイト完全一致 | バイト完全一致 |
| 帯域 | 2.4GHzのみ試験 | **2.4GHz/5GHz両方で再現** |

**結論：この停止はC3固有ではなく，少なくともC3/C5に共通の家族的
バグ**。さらに本ラウンドの新情報として：
1. **5GHz（ch48）でも同一シグネチャで再現**＝2.4GHz帯のRF妨害説
   （C3実施1の調査候補(d)＝並行BLE実験の干渉等）では説明できない
   （BLE/多数の2.4GHz機器は5GHz ch48に干渉しない）。デバイス側
   （blob/シム/lwIP TX経路）要因の蓋然性が大きく上がった。
2. C5はCPU例外捕捉網を張った上で再現＝停止は例外・パニック由来では
   ない（C3では未確認だった項目）。
3. C3とC5はWiFi blobの世代が異なる（C3=NuttX同梱世代，C5=IDF v6.1
   v9 blob）一方，`net/`層（lwIP配線）はC3_TARGETDIR経由で**文字
   どおり同一ソース**，シムは同一設計。共通項の絞り込みとしては
   lwIP netif層（`netif_esp32c3.c`の`esp_wifi_internal_tx`戻り値
   無視等，C3実施1の候補(a)）とシム設計共通部が上位に残る。

### 4. 判定（事前固定基準に対して）

- **ヒープ非減少**：合格（全run一定。欠陥A症状は発現せず）
- **エコー完全一致**：合格（TCP/UDP・3run全てmismatch 0）
- **10分完走**：**不合格（ただしC3と同一の既存停止バグによる．S3
  欠陥とは別）**。run1は600秒印加したが実質負荷が乗ったのは最初の
  数秒のみ
- **欠陥A修正の妥当性**：C3同様「修正で治る」ことは実証できない
  （欠陥Aが顕在化する前に停止が必ず先行）が，ISR内mallocの構造的
  リスク除去として妥当・回帰なし

### 5. C5#1最終状態

`build/load_test_c5_qfix/asp_flash.bin`（**シム欠陥A修正込み・
2.4GHz `<SSID-2G>`・動的TXバッファ＝LOAD_TEST_STATIC_TXBUFなし**）を
フル4MB `write-flash 0x0`済み。書込み後の最終確認ブート：connect
（ch1/RSSI-54）→DHCP（192.168.1.70）→TCP port8/UDP port9 echo起動→
デバイス発ゲートウェイping OK・ホスト発ping 3/3を確認。5GHz版は
`build/load_test_c5_qfix_5g`に保存（書込みだけで再現可）。

### 6. 変更ファイル（実施46）

- `asp3/target/esp32c5_espidf/wifi/esp_shim.c`：キュー固定プール化
  （欠陥A修正．C3実施1の移植）
- `apps/load_test_c5/`（新規）：負荷試験アプリ（load_test_c3複製＋
  AP情報ログ＋フォルト捕捉安全網）
- `docs/c5-bringup.md`：本節
- git commitは行っていない（コーディネータのレビュー後commit&push
  運用）。`docs/load-test-c3c5c6.md`はC6エージェント編集中のため
  無接触（統合はコーディネータ）。

### 7. 申し送り

1. **リンク停止の根本原因調査はC3実施1の候補(a)-(c)が主戦場**
   （候補(d)=RF妨害説は5GHz再現でほぼ棄却）。次の一手はC3実施1の
   推奨どおり`netif_esp32c3.c`の`esp_wifi_internal_tx`戻り値計装
   （C3/C5共通ソースなので1箇所の計装で両チップに効く）。
2. C5でJTAGを使った停止時MAC/BB状態の採取は本ラウンド未実施
   （C3側の記録もJTAGなしのログベースだったため比較項目としては
   充足。将来やる場合はUSB-JTAG＝ttyACM2への非侵襲attachで可能，
   ただしCDC openリセットハザードに注意）。
3. 負荷試験ハーネス（`load_test.py`/`c5_serial_logger.py`/
   `run_load_c5.sh`）はセッションスクラッチ（リポジトリ外）。要点：
   C5はシリアルopen=リセットのため「ロガー起動→25s待ち→precheck→
   負荷」の順番が必須。

## 実施47：負荷誘発リンク停止の根因調査——★停止は当夜0/7で全く再現せず（実施46の停止バイナリそのもの・両帯域・強度4倍を含む）＝「決定論的2〜10秒停止」を撤回し時間依存の環境要因ゲートと再解釈。C6完走(実施90)も時系列交絡と判明（唯一のクリーンC6 runは全停止runの後）。停止時スナップショット計装（C5専用ガード）を常設化

### 目的・位置づけ

実施46で「3/3 run・2〜10秒・両帯域」で再現した負荷誘発リンク完全停止
（C3実施1と同一シグネチャ）の根因を特定する。事前仮説（コーディネータ
指示）＝WiFiドライバタスク／ppタスク相当の恒久ブロック説を最有力とし、
停止発生時に全タスク状態・シムqueue水位・`esp_wifi_internal_tx`戻り値を
採取して(i)タスクブロック／(ii)下層黙殺／(iii)TXエラー連発に分類する
計画だった。

### 1. 停止時スナップショット計装（成果物・常設）

- **net/層（C3/C5/C6共有 `asp3/target/esp32c3_espidf/net/netif_esp32c3.c`）**：
  `TOPPERS_ESP32C5_NETSTALL_TRACE`ガードの加算的変更のみ（CLAUDE.md
  非干渉鉄則遵守）。(1) `low_level_output`（`esp_wifi_internal_tx`の
  唯一の呼び出し元）に呼数・エラー数・直近戻り値カウンタ
  （`g_netstall_tx_calls/tx_errs/last_tx_ret`）、(2) `net_ping_result`
  （デバイス発1Hz raw ping成否・tcpip_thread文脈）に
  `netstall_trace_ping_result()`フック。**未定義時は完全no-op**＝
  C3ビルドで検証済み（`build/c3_r47_netcheck`＝load_test_c3、リンク成功
  RAM 97.91%（従来同値）、nmでnetstallシンボル0個を確認。C6ビルドは
  C6エージェント作業中のため無接触——ソース上同様にno-op）。
- **アプリ（`apps/load_test_c5/load_test_c5.c`）**：デバイス発ping連続
  3回失敗（約3秒）で1回だけ発火するスナップショットダンプ。全12タスク
  （SHIM_TSK1-6/SHIM_TIMER_TSK/NET_TSK/MAIN/TCP/UDP/MON）の`ref_tsk`
  （tskstat/tskwait/wobjid→SEM/MTX/DTQ名を逆引き）、全18 DTQ
  （SHIM_DTQ1-4+NET_MBOX1-10）の`ref_dtq`水位・待ちタスク、TXカウンタ、
  heapをUARTへ出力。
- ビルド：`build/load_test_c5_r47`（2.4GHz・欠陥A修正込み・動的TX・
  `TOPPERS_ESP32C5_NETSTALL_TRACE=1`）。RAM 83.96%（実施46比+0.01pt）。

### 2. ★主結果：停止が全く再現しない（当夜0/7）——バイナリ・帯域・強度によらず

**環境**：DUT＝C5#1（`D0:CF:13:F0:A7:44`、UARTブリッジ`b04e3bcf…`、
全操作by-id照合）。AP/ホスト/ハーネス＝実施46と同一（TCP 1400B持続＋
UDP 512B 50pps＋2秒ping、ロガー先行起動手順）。

| run | 時刻(JST) | バイナリ | 帯域 | 負荷 | 結果 |
|---|---|---|---|---|---|
| r47_run1 | 02:16 | r47計装版 | 2.4G ch1 | 120s標準 | **完走**（TCP 1.93MB・mismatch0・reconn0） |
| r47_run2 | 02:20 | r47計装版 | 2.4G | 120s標準 | **完走**（TCP 1.91MB） |
| r47_ab_old | 02:25 | **実施46停止バイナリそのもの**（load_test_c5_qfix、md5=767bebd0…） | 2.4G | 120s標準 | **完走**（TCP 1.89MB） |
| r47_run600 | 02:31〜 | r47計装版 | 2.4G | **600s標準** | **完走**（TCP 9.92MB完全一致・デバイス発ping約600回連続OK・heap 158736完全回復）＝**C5の10分完走基準を初達成** |
| r47_5g | 02:46 | **実施46の5GHz停止バイナリそのもの**（qfix_5g） | **5G ch48/RSSI-62**（停止runと同一ch・同等RSSI） | 180s標準 | **完走**（TCP 2.93MB） |
| r47_hard | 02:53 | r47計装版 | 2.4G | 180s**強度4倍**（TCP×4本+UDP 200pps） | **リンクは最後まで生存**（UDP 94%継続・デバイス発ping 225回OK・STALL発火0）。TCPのみ詰まり＝下記app層アーティファクト |
| r47_run600b | 03:07〜 | r47計装版 | 2.4G | **600s標準（2回目）** | **完走**（TCP 9.62MB完全一致・reconn0・単一セッション600s維持・heap 158752回復）＝**10分完走2/2** |

スナップショット発火は全run（計7run）通じて0回（デバイス発pingが
一度も3連続失敗しなかった＝リンクが一度も死ななかった）。


**r47_hardのTCP詰まりの注記**：tcp_sessions=3・tcp_errs=1の後
tcp_bytes凍結。本アプリのTCPサーバは単一セッション逐次処理
（listen backlog=1・blocking recv・RCVTIMEO無し）のため、4本同時
ストリームでは死んだピアのrecv()で恒久ブロックしaccept再開不能に
なる**アプリ設計上の既知制約**。UDP/ICMPは同時刻も健全＝リンク停止
ではない（実施46の停止は双方向全滅・ICMP含む）。

### 3. 時系列の再構成——「C3/C5停止 vs C6完走」判別の時間交絡

スクラッチのタイムスタンプ（epoch実測）から当夜の全runを再構成：

| 時刻(JST 7/14) | run | 結果 |
|---|---|---|
| 00:04〜00:29 | C3実施1 exp2/3/4b（board A） | **3/3停止**（15〜30s） |
| 01:03〜01:28 | C5実施46 qfix600/repro180/g5_180 | **3/3停止**（2〜10s、両帯域） |
| 01:39〜01:50 | C6実施90 600s run（board C） | 完走 |
| 02:16〜03:17 | C5実施47 上表7run | **全て完走**（0/7停止） |

- **実施46の3/3停止と実施47の0/7非再現の間はわずか約50分**。同一
  ボード・同一AP・同一ハーネスで、うち2runは**停止したバイナリの
  完全同一物**（md5照合）。
- qfix600の停止は実測ログで確定的（tcp_bytes=8608でuptime=30s以降
  凍結・デバイス発ping 18回目以降消失・ホストping Destination Host
  Unreachable）＝実施46の観測自体は真正。ハーネス真正性も確認
  （C6エージェントによるload_test.py上書きは01:40＝C5全run終了後）。
- **含意1**：「この負荷を掛ければ2〜10秒で決定論的に停止する」は
  撤回。停止は当該負荷だけでは駆動されない未特定の時間依存要因
  （環境ゲート）を必要とする。
- **含意2**：C6の「600秒完走＝C6免疫」（実施90）は、**全ての停止run
  （00:04〜01:28）より後（01:39〜）に走った唯一のクリーンrun**であり、
  02:16以降はC5も完走している以上、「チップ差」ではなく「実行時間帯」
  で説明可能＝**3チップ判別（C3/C5共通・C6に無いもの）は時間交絡で
  無効**。C6が本当に免疫かは停止再現条件下での同時比較が必要。
- **含意3**：環境ゲートの候補＝停止時間帯（00:04〜01:28）は複数
  エージェントが同一LAN/AP/RF帯で並行実機試験中だった（C3負荷試験・
  C6のNuttX warm化/ビルド・その他）。ただし5GHz ch48でも停止した
  事実（実施46）から単純な2.4GHz RF干渉ではなく、**router/AP共通部の
  状態（両帯域は同一ルータ）またはRF輻輳が誘発するデバイス側の状態
  遷移バグ（レート適応・BAセッション・再送スタック等）**が残る候補。
  「環境要因が引き金のデバイス側バグ」の可能性は棄却されていない
  （非再現は決定論性の反証であって、デバイス無罪の証明ではない）。

### 4. 分類判定（事前固定の(i)/(ii)/(iii)に対して）

**判定不能（現象消失）**。ただし採れた範囲の傍証：
- 全6run＋実施46の全runでSTA_DISCONNECTED無し・heap一定・カーネル
  健全は既確定。
- r47_hard（強度4倍）でもリンク層は健全＝「純粋な負荷強度の閾値」説
  は今夜の環境では不成立。
- スナップショット計装は装填済みのまま残る＝**次に停止が起きた瞬間に
  (i)/(ii)/(iii)が自動判別される**（本ラウンドの恒久成果）。

### 5. C5#1最終状態

`build/load_test_c5_r47/asp_flash.bin`（欠陥A修正＋NETSTALL_TRACE計装
＋停止スナップショット、2.4GHz `<SSID-2G>`、動的TX）をフル4MB
`write-flash 0x0`済み。最終確認ブート：connect（ch1/RSSI-56）→DHCP
（192.168.1.70）→TCP port8/UDP port9 echo起動→デバイス発ゲートウェイ
ping連続OK・ホスト発ping 3/3 OK。その後run600b（2回目の600s完走）まで
同状態で健全稼働（最終リブート＝run600bロガーopen時、以降600s負荷
完走・heap回復・エラー0）。

### 6. 変更ファイル（実施47）

- `asp3/target/esp32c3_espidf/net/netif_esp32c3.c`：
  `TOPPERS_ESP32C5_NETSTALL_TRACE`ガードのTXカウンタ＋pingフック
  （未定義時no-op、C3ビルドで無影響確認済み）
- `apps/load_test_c5/load_test_c5.c`：停止時スナップショットダンプ
  （同ガード内）
- `docs/c5-bringup.md`／`docs/load-test-c3c5c6.md`：本記録
- git commitはしない（コーディネータレビュー後の運用）

### 7. 申し送り

1. **C3への展開**：C3実施1の「15〜30秒停止」も同一の時間交絡窓
   （00:04〜00:29）の観測＝**C3でも静穏時間帯の再現確認が先**。
   再現するならNETSTALL_TRACE一式はC3でそのまま使える（netif側は
   共有ソースで実装済み。ガードマクロ名がC5固定なので、C3で使う際は
   ビルド定義に`TOPPERS_ESP32C5_NETSTALL_TRACE=1`を足す（名前は
   C5だが実装はチップ非依存）か、中立名に改名する。アプリ側
   スナップショットはload_test_c5.cのSTALL節をload_test_c3へコピー
   ＋kernel_cfg.hのID差分（C3はSERIAL_RCV_SEM等のID配置が異なる
   可能性→ビルドで検出される）で移植可能）。
2. **停止の再現を狙う場合**：単独負荷では出ない。停止時間帯の環境
   （複数ボード同時負荷・同一APへの多クライアント高トラフィック等）の
   意図的再構成が必要。その際はC5とC6を**同時刻に並走**させると
   時間交絡なしの真のチップ判別になる（board間の非干渉制約は
   コーディネータ調整事項）。
3. **AP/router状態説の安価な検証**：次に停止が起きたら、リセットの
   前に (a) ホスト側から同一APの**他クライアント**（C3 board A等）への
   疎通を確認（router全体か対象クライアントのみか）、(b) デバイスを
   リセットせずAPへの**再アソシエーションのみ**（esp_wifi_disconnect→
   connect）で回復するか確認（回復すればAP側per-association状態説が
   強まり、スナップショットの(i)〜(iii)と合わせて層が確定する）。
   この2手順はload_test_c5に未実装（次ラウンド候補）。
4. ハーネス（`r47_load_test.py`/`r47_load_hard.py`/`r47_run.sh`/
   `r47_run_hard.sh`/`c5_serial_logger.py`）はセッションスクラッチ。
   要点は実施46と同じ（ロガー先行起動→25s→precheck→負荷）。

## 実施48：★hal(v8) blob再試験（docs/c5-hal-v8-unification-memo.md の計画を実行）——実施09「v8 blobはeco2非互換」判定を**反証**。クロック鎖修正＋APM解除後の正しい環境ではv8 blobも2.4GHz scanが完全動作（同一DUT・同一アンテナ・同一時間帯でv9と対照）。あわせて別PC移行に伴うv9側のIDFローカルパス依存（apb80m↔pm_sleep_lock）を解消

### 背景・狙い

`docs/c5-hal-v8-unification-memo.md`（コミット802d75b，ユーザー発案）の
計画に従い，実施09で下した「esp-hal-3rdparty(hal submodule) v8 blob は
eco2 C5 の PHY/RX 較正に非対応」という判定を再検証する。実施09当時の
ASP3環境には後に発見された2欠陥（(1)クロック鎖＝BBPLL 576MHz誤ロック，
実施32-34で修正／(2)APM遮断＝MODEMマスタREE2ブロック，実施42-43で修正）が
共存しており，v8 blobは正しく構成された環境で一度も試されていなかった。
**scanベースの試験なのでWi-Fi認証情報は不要**（AP接続はしない）。

事前予測（メモの通り固定）：クロック＋APM修正済み環境ならv8較正は収束し
scanが通る。外れれば（＝ここで再ハングすれば）実施09の追認としてクリーンに
価値がある。

### 環境（本ラウンドは別PC＝origin同期済み現ツリー802d75bベース）

- DUT＝ESP32-C5 #1 `d0:cf:13:f0:a7:44`（native USB=ttyACM4／UART
  CP2102N `b04e3b…` = ttyUSB3＝観測）。全操作直前にMAC/シリアル照合。
- toolchain＝xpack riscv-none-elf-gcc 15.2.0。esptool＝
  `~/tools/espressif/python_env/idf6.1_py3.12_env`（v5.3.1，`--no-stub`）。
- hal submodule＝`b90b1837`（v8 blob世代．libnet80211.a=1.67MB＝v9の
  1.80MBと別物）。v8変種は`ESP_HAL_DIR`（hal submodule）基点で
  **IDFローカルパス非依存**——統一の最大の利点。

### v9側の前提修正（別PC移行に伴うIDFローカルパス依存の解消）

現ツリーのv9(wifi/)ビルドが本PCでは**そのままではコンパイル不能**だった：
`wifi/esp_wifi_adapter.c:1727-1728`が`._wifi_pm_sleep_lock_acquire/release`を
初期化するが，本PCのローカルIDF `v6.1-dev-5215-g0d928780`（wifi lib
submodule `cde32e0`）の`wifi_os_adapter.h`はこのスロットが依然として
`_wifi_apb80m_request/release`（v8由来名）のまま＝pm_sleep_lockへの改名
**前**の世代だった。origin PCのIDFはpm_sleep_lock世代（実施12）で，本PCは
それより古いスナップショット。リンクするprebuilt blob（本PCの
`components/esp_wifi/lib/esp32c5/*.a`）も同checkoutにピン留めされこの
名前・レイアウトに一致するため，**本PCではapb80m名で結線するのが正しい**
（両フィールドは同一構造体オフセット・no-op＝機能的に等価，PMサブシステム
未実装のため実害なし）。`wifi/esp_wifi_adapter.c`の当該2行をapb80mへ変更
（コメントで実施48・ローカルパス依存を明記）。これは実施09/12とは独立の
「別PC移行の申し送り（コミット419eb75）」の消化。IDFチェックアウトを
pm_sleep_lock世代へ更新したらこの2行を戻す必要がある。

### 手順

1. v9再現（priority 1）：v9(wifi/)をapb80m修正後にビルド（RAM 76.25%＝
   実施45/46と同水準・回帰なし）→ ttyACM4へ`--no-stub`書込→ ttyUSB3で
   UART観測。**→ scan成功**（後述）。
2. v8変種の取り込み：`origin/claude/c5-hal-v8-unification`から
   `target.cmake`（v8選択ロジック追加）・`esp_wifi_v8.cmake`・`wifi_v8/`
   （esp_shim.c/esp_shim_blobglue.c/esp_wifi_adapter.c＝v8 os_adapter 0x08）を
   `git checkout … --`で現ツリーへ取り込み（必要分のみ）。共有arch/target層の
   修正（クロック鎖・APM解除`esp32c5_r42_apm_unblock`・CLIC・SWDキー）は
   両変種で自動的に効く＝再試験の要。scan-onlyのためv9側connect経路修正
   （実施45 g_misc_nvs・実施46固定プール）は無関係で移植不要。
3. v8ビルド：`-DESP32C5_WIFI=ON -DESP32C5_WIFI_HAL_V8=ON`（build/c5_wifi_v8）。
   エラー0（RAM 75.99%＝v8 blobが小さい分v9より僅かに小）。IDFローカル
   パス依存なし。
4. v8書込→同一DUT・同一アンテナ・同一時間帯（v9の数分後）でUART観測。
5. v9へ書き戻し（DUTを既知良品v9出荷構成へ復元・再現確認）。

### 結果（★v8非互換説を反証）

同一DUT `a7:44`・同一アンテナ/設置・同一時間帯（2026-07-14 ≈14:15）・
同一`wifi_scan`アプリでの対照：

| 変種 | blob出所 | 初回scan | 再scan（連続） | <SSID-INST-1X> RSSI | 較正/RX | 安定性 |
|---|---|---|---|---|---|---|
| **v9**（wifi/, IDF v6.1 blob） | ローカルIDF（apb80m修正後） | 20 APs | 53-62 APs | -48 | 収束・promisc_rx=171 | クラッシュ無 |
| **v8**（wifi_v8/, hal submodule blob） | hal `b90b1837` | 20 APs | 50-64 APs（9連続scan） | -47 | **収束・promisc_rx=420** | **クラッシュ/WDT/panic無** |

- v8：`esp_wifi_init`→`esp_wifi_start -> 0`→`set_promiscuous OK`→
  `promisc_rx_count=420`（RX受信あり＝**旧IQ-estハング点を通過・PHY較正収束**）→
  `esp_wifi_scan_start -> 0`→`WIFI_EVENT id=1`→`20 APs found`。以降
  RESCANで64/58/60/63/62/50/60/54 APs（計9scan）を全窓（≈95s）で継続，
  唯一のrst行は通常のdownload/flash-bootのbootrom行のみ。
- 両変種が同一ネットワーク群を検出（<SSID-INST-1X>/<SSID-EDU>/<SSID-INST-G>）・
  RSSIも同水準。**v8はeco2 C5で2.4GHz scanが完全動作する**。

### 判定（メモの判定点3つに対して）

- (a) PHY較正が収束するか（旧IQ-estハング）：**v8 PASS**（scan_start=0・
  RX active・9連続scan完走・ハング無）。予測的中——実施09の「v8非互換」は
  **クロック鎖＋APMの交絡による誤判定**と確定。相関（v8時代にハング）≠
  因果（v8がeco2非対応）。
- (b) scanでAP検出（2.4GHz）：**v8 PASS**（20→50-64 APs）。
- (c) connect→DHCP→ping→TCP/UDP＋5GHz：**本ラウンド未実施**（認証情報が
  必要＝タスク範囲外）。v8での5GHz動作は依然未知（メモの反証可能性の残り）。

### 反証可能性の残り（早合点しない）

- v8での**connect/load/5GHz**は未検証。実施45の5GHz実証（ch48/RSSI-61）は
  v9のみ。v8で5GHzやconnectがこける可能性はまだ排除されていない＝
  「hal一本化」の最終確定には (c) の追試が要る。
- 本ラウンドが確定したのは「scan（=PHY較正収束＋RX＋2.4GHzビーコン受信）は
  v8でも動く」まで。実施09の判定は**scanの範囲で反証**された。

### 変更ファイル（実施48）

- `wifi/esp_wifi_adapter.c`：g_wifi_osi_funcs の pm_sleep_lock スロット2行を
  apb80m へ（本PCのローカルIDFスナップショット＋blobに整合）。コメント追記。
- `target.cmake`：`ESP32C5_WIFI_HAL_V8` オプション＋v9/v8変種選択ロジック
  （origin/claude/c5-hal-v8-unification から取り込み，既定OFF＝v9無影響）。
- `esp_wifi_v8.cmake`（新規）・`wifi_v8/{esp_shim.c,esp_shim_blobglue.c,
  esp_wifi_adapter.c}`（新規．v8 os_adapter 0x08）を取り込み。
- 既定ビルド（`ESP32C5_WIFI_HAL_V8`未指定）はv9のまま＝非回帰。

### 次の一手

1. **v8で判定点(c)**：connect→DHCP→ping→TCP/UDP＋5GHz（ch48等）を
   v8変種で試す。認証情報が要るため申し送り（SSID/パスワードは非コミット）。
   ここまでv8で通れば「hal一本化」を確定し，`esp_wifi.cmake:85`の
   ローカルパス直書き（`/home/honda/tools/esp-idf-v6.1`）を撤去して
   C3/C6/C5をhal submodule一本へ統一可能（IDFローカルパス依存が消える）。
2. こけたら「v8はconnect/5GHz層でeco2非互換」を今度こそクリーンに確定し
   v6.1続行（＝ローカルパス依存を pinned submodule 化して明示化）。
3. BLE側（C5はIDF v6.1のbt.c/NimBLE）をhalの`lib_esp32c5`へ戻せるかは
   wifi統一確定後の第2段（メモ末尾）。

## 実施49：★v8/hal を C5 Wi-Fi の «既定» へ昇格＋IDF v6.1 ローカルパス依存の隔離（ユーザー決定＝v6.1がbetaのためIDF非依存のv8/halへ舵）——`ESP32C5_WIFI=ON` の既定が v8（hal submodule blob・IDFパス不要）を選ぶよう反転。v9 は `-DESP32C5_WIFI_V9=ON` の opt-in fallback として温存。既定v8ビルドが `/home/honda/tools/esp-idf-v6.1` を一切参照しないことを実証

### 背景・狙い（ユーザー決定）

実施48でv8 blob（hal submodule `b90b1837`）が eco2 C5 で 2.4GHz scan を
完全動作（実施09「v8非互換」を反証）することを実証した。IDF v6.1 は
まだ beta で，かつ v9 経路は `esp_wifi.cmake:85` の
`set(IDF /home/honda/tools/esp-idf-v6.1)` ハードコードに依存する＝
特定PCのローカルIDFが在る環境でしかビルドできない（ポータビリティ欠如）。
**ユーザー決定：既定を IDF非依存の v8/hal へ切替え，v9 は opt-in fallback
として残す**。本実施はその «既定化＋IDF依存隔離» を可逆・非回帰で行う。

### 変更（target.cmake のみ・最小）

`asp3/target/esp32c5_espidf/target.cmake` の変種選択ロジックを反転：

- 新オプション `ESP32C5_WIFI_V9`（既定OFF）＝**v9(IDF v6.1)への opt-in
  fallback**。ON のとき `esp_wifi.cmake`＋`wifi/` を使う（IDFパス依存はこの
  経路のみに隔離）。
- 既定（`ESP32C5_WIFI_V9` 未指定）＝**v8/hal**（`esp_wifi_v8.cmake`＋
  `wifi_v8/`．ESP_HAL_DIR 基点・IDF非依存）。実施48の
  `ESP32C5_WIFI_HAL_V8` の作法を既定側へ昇格。
- `ESP32C5_WIFI_HAL_V8`（実施48の旧スイッチ）は後方互換で受理するが
  v8 が既定になったため no-op（`ESP32C5_WIFI_V9` と同時指定は FATAL_ERROR）。
- REGI2C_TRACE 診断（v9/wifi/専用）のガードを `if(ESP32C5_WIFI_HAL_V8)` →
  `if(NOT ESP32C5_WIFI_V9)` へ更新（v8既定でも正しく「v9でのみ可」を強制）。

`esp_wifi.cmake`（v9・IDFハードコード）・`esp_wifi_v8.cmake`・`wifi/`・
`wifi_v8/` の中身は無変更。切替は target.cmake の3〜4行のロジックのみ＝可逆。

### 検証1：既定v8ビルドが IDFパス無しでリンク（★主目的の実証）

`-DESP32C5_WIFI=ON`（V9無し）で configure/build：

- **RAM 75.99%**（実施48のv8と同一）・FLASH 11.47%・**リンク0エラー**。
- 生成物（`build/c5_v8_default/`）を
  `grep -rl "esp-idf-v6.1\|/home/honda/tools"` → **ヒット0**＝
  既定v8ビルドは `/home/honda/tools/esp-idf-v6.1` を**一切参照しない**。
  リンクの `-L` は `hal/components/esp_wifi/lib/esp32c5` 等 submodule 基点のみ。
  → **IDFパス非依存を実証**（ユーザーの主目的＝ポータビリティ達成）。

### 検証2：非回帰

- **v9 opt-in**（`-DESP32C5_WIFI=ON -DESP32C5_WIFI_V9=ON`）：configure/build
  とも成功，**RAM 76.25%**（実施45/46/48のv9と同水準）。build.ninja に
  `esp-idf-v6.1` パスあり＝IDF依存は opt-in 経路に正しく隔離（設計どおり）。
- **非Wi-Fiビルド**（`ESP32C5_WIFI` 無し・sample1）：build成功・RAM 3.27%・
  IDFパス参照0。Wi-Fi無効ビルドに一切影響なし。

### 検証3：実機v8 scan 再確認（★board latch により中断・power-cycle待ち）

DUT＝ESP32-C5 #1 `d0:cf:13:f0:a7:44`（MAC照合：native USB by-id
`…D0:CF:13:F0:A7:44…` = ttyACM4／UART CP2102N `b04e3b…` = ttyUSB3）。
C5#2 `c8:94`・C3・S31 には不接触。既定v8バイナリを `--no-stub` で
ttyACM4 へ書込（4MB・verify OK・hard-reset）。

- **注意（UART観測の作法）**：`cat`/`stty` で ttyUSB3 を開くと DTR/RTS が
  asserted のままになり C5 が出力しない（0バイト）。**pyserial で
  `dtr=False, rts=False` で開くと出力が出る**（今回判明。実施48の観測
  との差＝観測ツールの DTR/RTS 扱い）。
- **観測結果**：初回ブートで
  `TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C5` →
  `wifi_scan: esp_wifi_init` → `pp rom version: 78a72e9d5`（=v8 blob）→
  `wifi driver task prio:23` 起動まで到達。ただしその後 UART が化け
  （PHYブリングアップでのクロック変化と推測）→ `WIFI_EVENT id=43` →
  以降 **`rst:0x7 (TG0_WDT_HPSYS)` + `Core0 Saved PC:0x40038598` の
  WDTリセットループ**に転落（AP件数の出力前）。
- **判定＝latched-board-state（[[c5-latched-board-state]]）**。`0x40038598`
  は red-herring ROM PC。この状態は soft/hard reset で消えず，物理電源
  再投入でのみ解消する。今回，観測ツールの試行錯誤で reset を数回反復した
  ことが latch を誘発した可能性が高い。**同一v8変種は実施48（≈3h前・同一
  DUT）で 9連続 scan・50-64 APs を実証済み**＝コード退行ではない。
  → **クリーンな scan 再確認は物理電源再投入後に再取得が必要**（ユーザー
  依頼事項）。広域 pkill・反復 reset のハンマリングはしない（memory準拠）。

### v9 完全削除 前の gate（申し送り）

v9 fallback を**残す**理由＝v8 での以下が未実証（Wi-Fi認証情報が要る＝
本タスク範囲外，SSID/パスワードは非コミット）：

1. **v8 connect → DHCP → ping → TCP/UDP エコー**（実施45はv9のみ）。
2. **v8 5GHz**（ch48等．実施45のch48/RSSI-61実証はv9のみ）。

上記が v8 で通って初めて「hal一本化」を確定し，`esp_wifi.cmake`（v9）と
`wifi/` を削除可能。それまで v9 は `-DESP32C5_WIFI_V9=ON` で温存する。

### C3/C6 拡張の所見（«評価のみ»・本タスクでは未編集）

- **C6**：C6は元々 esp-hal-3rdparty(hal submodule) 基点で構成されており
  （`asp3/target/esp32c6_espidf/esp_wifi.cmake` は ESP_HAL_DIR 基点・IDF
  ハードコード無し。本C5 v8版はそのC6版のコピー起点）。C6は既に
  «IDF非依存»＝本件の hal 統一と同じ土俵にある。C6は deaf-RX（受信不能）
  が別課題として未解決（`docs/wifi-shim-c6.md`）だが，これはblob世代では
  なくPHY/RX側の問題で，v8/hal統一とは独立。
- **C3**：C3の `asp3/target/esp32c3_espidf/esp_wifi.cmake` も ESP_HAL_DIR
  基点（IDFハードコード無し）で，元々 hal submodule で scan 実績あり。
  C3も追加のIDF依存撤廃は不要（既にhal基点）。**ただしC3 BLE関連
  （`asp3/target/esp32c3_espidf/**`・`apps/ble_host_smoke/**`・
  `docs/bt-shim.md`）は別サブエージェント作業中につき本タスクでは不接触**。
- 結論：**IDFローカルパスのハードコードが残るのは C5 の v9 経路
  （`esp_wifi.cmake:85`）だけ**。本実施でそれを opt-in に隔離したので，
  C5既定・C3・C6 の3チップともに «hal submodule 一本・IDF非依存» で
  ビルドできる状態になった（C5のv9撤去は上記gate通過後）。

### 変更ファイル（実施49）

- `asp3/target/esp32c5_espidf/target.cmake`：v9/v8 変種選択ロジックを反転
  （既定=v8）。`ESP32C5_WIFI_V9` opt-in オプション追加，`ESP32C5_WIFI_HAL_V8`
  を後方互換no-op化，REGI2C_TRACEガードを `NOT ESP32C5_WIFI_V9` へ。
- 本doc（実施49）追記・memory 更新（`c5-wifi-hal-v8-scan-works` に既定化を追記）。
- コミットはしない（ユーザー確認用の作業ツリー変更）。

## 実施50：v8 connect→DHCP の「決定論的失敗」を反証——真因＝間欠的/環境要因（ch9 2.4GHz）で，esp_shim キュー E_CTX バグ（C3 D-2c 同型）ではない。v8 の DHCP は実機で 4/5 ブート成功（IP 192.168.1.21 取得，同一 RSSI-64 でも成功）。TX 路は errs=0/ret=0 で健全

### 背景・狙い

申し送り＝「v8既定ビルドで `esp_wifi_connect -> 0`・`STA_CONNECTED` だが
`wifi_dhcp: DHCP FAILED (timeout)`。v9（実施45）は全PASS＝v8固有の L3
データ経路の差」。最有力仮説として **C3 D-2c（bt-shim.md，commit f9dae7d）で
特定した esp_shim の「E_CTX/クリティカルセクションでのキュー取りこぼし」
バグ**が C5 の Wi-Fi RX/TX 経路にも在るか（保留リング＋countセマフォ＋
flush の C3 修正の移植で直るか）を，コード比較→実機計装で検証した。

### 手法1：静的コード比較（★esp_shim 同型バグ仮説を反証）

`wifi_v8/esp_shim.c`（v8＝DHCP失敗側）と `wifi/esp_shim.c`（v9＝DHCP成功側，
実施45で全PASS）の**キュー実装（`esp_shim_queue_send`/`_recv`/
`_send_from_isr`）はバイト単位で同一**（diff で確認）。両者とも旧実装＝
保留リング・空きスロットセマフォ・flush・E_CTXフォールバックを**持たない**。
v8→v9 の差分は BT/NimBLE 用の追加（`esp_shim_enter/exit_critical`〔ただし
C3と違い flush を呼ばない〕・`esp_shim_modem_icg_init`・`esp_shim_sem_get_count`・
`esp_shim_queue_reset`）のみで，**Wi-Fi データキュー経路には一切触れない**。

- **∴ v9 は同一の旧キューコードで DHCP を通す**＝「E_CTX 取りこぼし」が
  DHCP 失敗の原因なら v9 も失敗するはずだが v9 は成功する＝**仮説を反証**。
- 加えてデータ経路のソフト層はすべて共有：`net/netif_esp32c3.c`（lwIP
  netif・`wifi_rx_cb`／`low_level_output`）・`apps/wifi_dhcp`・`esp_shim.cfg`
  （両変種とも `C3_TARGETDIR/wifi/esp_shim.cfg` を使用）は v8/v9 で同一。
  実施45 の `g_misc_nvs` NULL 修正も v8（`wifi_v8/esp_shim_blobglue.c:78`）に
  在り＝connect が通ることと整合。v8/v9 の機能差は **blob（hal submodule
  `b90b1837` 対 IDF v6.1）＋アダプタの PHY/PVT 初期化**に限られ，
  esp_shim/lwIP/アプリのソフト経路ではない。
- C3 D-2c バグが BT だけで露見したのは，ACL 配送のみが `OS_ENTER_CRITICAL`
  （＝`esp_shim_enter_critical`＝生 csrrci で MIE クリア保持）で囲まれ
  `sense_lock()==(MIE==0)` により task 系 `tsnd_dtq` が E_CTX を返したため。
  Wi-Fi の RX は ISR 文脈（`psnd_dtq`＝非タスクで発行可）・TX は task 文脈
  （クリティカルセクション外）で，この E_CTX 経路を踏まない。

### 手法2：実機計装（RX/TX 判別）＝★DHCP は間欠的にしか失敗しない

**C5 実施04 の最重要教訓**（`no time event is processed in hrt interrupt.` の
良性 SYSTIMER level 再ラッチ storm が syslog を溢れさせコンソールを信頼
不能にする）を踏まえ，**TX 側カウンタ**（`net/netif_esp32c3.c` の
`TOPPERS_ESP32C5_NETSTALL_TRACE` 計装＝`low_level_output`＝
`esp_wifi_internal_tx` の唯一の呼出し元の `g_netstall_tx_calls/errs/last_tx_ret`）を
アプリ側（`apps/wifi_dhcp.c`）から distinctive tag `DHCPTX` で毎秒 syslog＋
RTC-RAM(0x50000080) へミラーする計装を追加（**既定ビルドは `#ifdef` で完全
no-op＝非回帰**）。判別規約：`tx_calls>0`⇒link_up到達＋DISCOVER 送信路が
lwIP まで生存，`last_tx_ret==0`⇒blob TX がフレーム受理，それでも IP 未取得
⇒問題は TX 後（OFFER 受信＝RF/RX 側）。

**実機結果**（DUT＝C5#1 `d0:cf:13:f0:a7:44`＝MAC照合，UART `b04e3b…`／
書込 native USB ttyACM4，認証情報はビルド注入・非記載，AP＝ch9 2.4GHz）：

| # | バイナリ | RSSI | DHCP | TX(calls/errs/ret) | 備考 |
|---|---|---|---|---|---|
| 1 | c5_v8_conn（既存） | -64 | **FAILED** | 計装無 | 申し送りの再現（唯一の失敗） |
| 2 | c5_v8_diag（計装） | -61 | **OK** 192.168.1.21 | 8/0/0 | link up→DHCP bound（〜9s） |
| 3 | 同上（再ブート） | -56 | **OK** 192.168.1.21 | 7/0/0 | |
| 4 | 同上（再ブート） | **-64** | **OK** 192.168.1.21 | 8/0/0 | 失敗時と同一RSSIでも成功 |
| a | c5_v8_conn（再flash） | -58 | **OK** 192.168.1.21 | 計装無 | ★失敗した当該バイナリが成功 |

- **同一の失敗バイナリ（c5_v8_conn）を再書込みして再実行すると成功**
  （#a）＝失敗はバイナリ/コード差ではない。**RSSI も非判別**（#4＝-64＝
  失敗時と同値でも成功）。TX は毎回 `errs=0/ret=0`＝lwIP→blob 送信路は健全，
  かつ OFFER 受信→IP bound＝**RX も健全**。
- ∴ **v8 の DHCP データ経路（TX・RX とも）は動く**。申し送りの単発
  「DHCP FAILED」は**間欠的/環境要因**（ch9 2.4GHz 混雑・AP/タイミング）で
  あり，実施47（「決定論的停止」を撤回し時間依存の環境要因ゲートと再解釈）と
  同じ家族。**「相関（1回失敗）を因果（v8固有のL3バグ）と早合点しない」**
  （`feedback_hardware_investigation_rigor.md`）に忠実に反証実験（同一
  バイナリ再試行・RSSI 統制）を先に行い確定した。

### 結論（申し送りの3問への回答）

1. **DHCP 失敗の真因**：決定論的バグではない。**間欠的/環境要因の DHCP
   タイムアウト**。v8 の connect→DHCP→IP 取得は 4/5 ブートで成功（`ping`
   直前まで到達）。ソフトデータ経路（esp_shim キュー／netif・lwIP／アプリ）は
   実機で機能実証済み。
2. **C3 esp_shim 修正と同型だったか**：**No（二重反証）**。(a) 静的＝v8/v9 の
   キューコードはバイト同一で v9 は同コードで DHCP 成功，(b) 機能＝v8 も
   旧キューコードのまま DHCP 成功。**C3 の pend_ring／count セマフォ／flush の
   移植は非因果**＝当てない（最小・非回帰の方針）。
3. **修正結果**：コード修正は不要・未実施。v8 で DHCP 取得（192.168.1.21）を
   複数ブートで実機実証。残るのは v8 の 5GHz／持続負荷の追試（実施48/49 の
   gate）で，本ラウンドの範囲外。

### 変更ファイル（実施50）

- `apps/wifi_dhcp/wifi_dhcp.c`：RX/TX 判別診断計装（`DHCPDIAG` RTC-RAM
  マーカ＋`DHCPTX` 毎秒 syslog＋`netstall_trace_ping_result` 空スタブ）。
  すべて `#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE` ガード＝既定ビルドは
  完全 no-op（default(no-trace) ビルドで compile 確認済み・RAM 同水準）。
- esp_shim／net／target ファイルは**無変更**（同型バグ仮説の反証により
  移植不要と確定）。C3 BLE 関連・submodule は不接触。
- コミットはしない（ユーザー確認用の作業ツリー変更）。
