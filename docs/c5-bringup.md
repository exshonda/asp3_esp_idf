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
