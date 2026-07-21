# C6 Wi-Fi調査 セッションハンドオフ（2026-07-06時点・別PC作業用）

全経緯の正本は `docs/wifi-shim-c6.md`（実施1〜66）と
`tmp/c6_step0_findings.md`（追記1〜25，別PCでの並行調査）。本書は
**作業を再開するための環境・手順・現在地**のみをまとめる。
**最新の現在地は本書末尾「実施51〜66（本セッション）：coexist_funcs
修正の実装〜deaf-RXの系統的切り分け」を参照**（以下の環境/ビルド/
観測手順は概ね共通・有効だが，OpenOCDパス等一部訂正あり，末尾の
更新差分を優先すること）。

## 現在地（1行で・最新）

★**coexist_funcs no-op化は実施52で修正済み**（`coex_pre_init()`追加，
PTIは`0x600a4dd8`=0x71でnative一致）。**しかしAP検出は依然0のまま**。
実施53〜66でdeaf-RXの原因を系統的に切り分けた結果，**送信は完全に
正常（MAC割込み約140/秒＝TX完了），受信は完全にdeaf（MAC event
0x600a4c48のbit14＝RX成功が一度も立たない）**と確定。静的設定・
アンテナ・チャネルdwell・RFPLLロック・RXクロック・RXアナログ較正
（regi2c 0x6b）・RXバッファ供給系（アロケータ/ABI/資源値/DMA到達
可能性）の**7クラスを全て反証済み**。ブロッカーは**バッファ供給より
上流＝PHY/RFが有効フレームを一度も復調しない**ことに局在化。詳細・
次の一手は本書末尾を参照。

## 必要な環境（別PCで揃えるもの）

1. **ESP-IDF v6.1**（native参照ビルド＋esptool＋python環境）
   - 本機では `$HOME/tools/esp-idf-v6.1` ＋ `IDF_TOOLS_PATH=$HOME/tools/espressif`
   - `source <idf>/export.sh` で esptool.py / riscv32-esp-elf-gcc が入る
2. **OpenOCD-esp32**（実パスは
   `~/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20250422/
   openocd-esp32`——本書に以前記載していた`20260424`は誤り，実施64以降
   で訂正済み）
   - `OPENOCD_SCRIPTS=<openocd-esp32>/share/openocd/scripts`
   - 使用cfg: `board/esp32c6-builtin.cfg`（内蔵USB-JTAG）
   - **同一USBホスト上に2枚のEspressifボードが乗っている状態が続いて
     おり，OpenOCDが誤検出することがある。必ず`adapter serial
     <MAC-18>`で対象ボードを明示指定すること**
     （もう1枚は`<MAC-40>`）。
3. **xpack riscv-none-elf-gcc**（objdump/nm用。riscv32-esp-elfでも代用可）
4. ハードウェア：ESP32-C6ボード（XIAO ESP32C6, rev v0.2）
   - **native USB → ttyACM0**（esptool書き込み・JTAG・リセット用）
   - **UART0(TX/RX) → FT232R → ttyUSB0**（コンソール観測用）
   - FT232Rは電源トグルで復帰しない＝**物理抜き差しのみ**。
     ボードのリセットは `esptool --before default-reset --after hard-reset flash-id` で行う（電源トグル不要）

## ビルド

```bash
# ASP3側（wifi_scan・Direct Boot）
export PATH=<riscv32-esp-elf>/bin:$PATH   # nm/objcopyが要る
cmake -B build/wifi_scan-c6-direct \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c6_espidf \
  -DASP3_APPLDIR=$PWD/apps/wifi_scan -DASP3_APPLNAME=wifi_scan \
  -DCMAKE_C_FLAGS="-Wno-error=implicit-function-declaration" \
  asp3/asp3_core   # （既存build/ディレクトリがあれば cmake --build のみでよい）
cmake --build build/wifi_scan-c6-direct
python3 -c "d=open('build/wifi_scan-c6-direct/asp_flash.bin','rb').read(0x100000); open('build/wifi_scan-c6-direct/asp_flash_trunc1M.bin','wb').write(d)"

# native側（参照・受信できる版。scan後ジャンプせず再scanループに改造済み）
cd tmp/c6_handoff_source && idf.py set-target esp32c6 && idf.py build
```

## フラッシュ＆観測（確立手順）

```bash
# ASP3（Direct Boot＝0x0にtrunc1M）
esptool.py --chip esp32c6 --port /dev/ttyACM0 --before default-reset --after hard-reset \
  write-flash 0x0 build/wifi_scan-c6-direct/asp_flash_trunc1M.bin
# native（2段ブート）
esptool.py --chip esp32c6 -b 460800 --port /dev/ttyACM0 --before default-reset --after hard-reset \
  write-flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
  0x10000 build/c6_handoff_source.bin
# コンソール: ttyUSB0 115200bps（ASP3は"RESCAN N APs"を約2.5秒毎に出力）
# JTAG読み:
openocd -f board/esp32c6-builtin.cfg -c init -c halt -c "mdw <addr> <n>" -c resume -c exit
```

## 次の一手：42レジスタの二分探索

1. ASP3を焼いてRESCANループ確認（0 APs）
2. `tmp/c6_jtag_tools/poke_mac.tcl` の mww 行を半分に割った版を作って実行
3. **判定＝esp_shim_int_count[1]**（MAC線ディスパッチ数）が増加し始めるか
   - アドレスは**ビルド毎にnmで確認**：
     `nm build/wifi_scan-c6-direct/asp.elf | grep esp_shim_int_count`
     配列[1]=ベース+4。移植前は11程度で固定、効くと数百/秒で増加
4. 効いたレジスタを特定したら：
   - libpp逆アセンブル（`objdump -d libpp.a`でそのアドレスのlui/store箇所）
     とNuttX（apache/nuttx arch/risc-v/src/esp32c6/）から設定経路を逆引き
   - 全42移植状態で sta_rx_cb / promisc_rx_count / RESCAN APs を確認
     （フレームがデータパスに乗るか）

## リポジトリに入っている資産

- `tmp/c6_jtag_tools/`：JTAGツール（read6b/write6b/check6b/poke_mac.tcl）
  ＋MAC空間dump生データ＋42差分リスト＋README
- `tmp/c6_handoff_source/`：native参照ビルド（再scanループ・regi2c読み戻し付き）
- `apps/wifi_scan/wifi_scan.c`：RESCANループ・RTC-RAM計測
  （0x50000080=phy_enable入場/88=register_chipv7_phy入場等）
- `tmp/c6_step0_findings.md`：全調査記録（追記1〜21）

## リポジトリに入っていないもの（別PCで再現が必要なら）

- **kernel `asp3/asp3_core/kernel/time_event.c` のログ無効化**（submodule内
  ローカル変更・未コミット＝禁則領域の一時診断）。
  再適用する場合：line 625 の `syslog_0(LOG_NOTICE, "no time event...")` を
  コメントアウト（観測性向上のみ・無くても調査は可能）。
- NuttX/Zephyrのクローン（公開GitHubから取得可）：
  `apache/nuttx`（arch/risc-v/src/esp32c6/esp_wifi_adapter.c）、
  `zephyrproject-rtos/hal_espressif`（zephyr/esp32c6/src/soc_init.c）
- **Codex CLI**（任意，本セッションで導入・使用済み）：
  `npm install -g @openai/codex`（ユーザ領域）＋ChatGPTログインで
  `codex doctor`が認証OKと出れば使える。ただしClaude Codeセッション
  自身のサンドボックス内から`codex exec -s read-only`を呼ぶと，
  Codex側のbwrapがユーザー名前空間を作成できず**ローカルFSは読めない
  ことが多い**（`exec_command`がsandbox起動時点で失敗）——その場合
  Codexは自動的に公開GitHubミラー（`exshonda/asp3_esp_idf`）の
  `github.fetch_file`にフォールバックする。**ミラーは実際にpushした
  コミットまでしか反映されない**ので，本ローカルブランチの未コミット
  分（実施51以降の変更を含む）は見えない。相談する際は，最新の状況を
  自己完結ブリーフィング（プロンプト内に直接書く）として渡すこと。

## 既知の罠（本機で踏んだもの）

- nm由来のRAMアドレスは**ビルド毎に変わる**（wifi_tr_count誤読の教訓）
  → 計測はRTC-RAM固定番地（0x50000040〜）が確実
- regi2cのRFブロック(0x6b)は**scan中のみ応答**（scan後は全0xff）
- native(IDF)ではROMのSRAM上部テーブルがheap回収で壊れる・regi2cバスは
  トランザクション毎gate → nativeでのregi2c読みは
  `regi2c_ctrl_read_reg_mask`（ELFシンボル）か生JTAGトランザクションで
- pyserialでttyACM0を読むとハングすることがある → ttyUSB0のみ読む

---

## 追記（2026-07-06夜）：根本原因確定＝coexist_funcs no-op化

**Step0診断は完了**。C6 WiFi RX不能の根本＝`asp3/target/esp32c3_espidf/wifi/
esp_coex_adapter.c` の `esp_shim_coex_adapter_register()` が `coexist_funcs`
を48個全て no-op に差し替えていること（詳細はfindings追記22-25）。
これでblobのcoex調停（0x600a4dd8 の WiFi PTI 設定）がno-op化→PTI=0→
coexがRXスロット不許可→MAC割込み不発→0 AP。

### 次PCでやること（実装フェーズ）
1. **NuttXのcoexブリングアップと比較**（同一blob・受信可）：
   scratchpad(なければ再clone) `apache/nuttx`
   arch/risc-v/src/common/espressif/ と esp32c6/ で、coexist_funcs を
   どう設定するか／coex_init/coex_enableの呼び出し順を確認。
2. no-op を外しても動くようにする：`coex_init()`後に coexist_funcs が
   有効テーブルになる状態を作る。単純除去は Illegal instruction
   （pm_disconnected_start経由のNULL）＝クラッシュするメソッドだけ
   safe stub、PTI系は本物、の部分approachも可。
3. 検証：`tmp/c6_jtag_tools/bisect.py` の rate測定で MAC割込みが
   自然に発火するか、最終的に "RESCAN N APs" が出るか。

### 追加した観測資産
- RTC固定番地計測：0x50000080=phy_enable入場, 0x50000088=register_chipv7_phy
  入場, 0x50000090=sta_rx_cb呼出し数（wifi_trace.c）
- `tmp/c6_jtag_tools/bisect.py` + `diffs.json`：42レジスタpoke/rate自動測定
- wifi_scan.c の RESCAN ループ（連続scan）

---

## 実施51〜66（本セッション，2026-07-07）：coexist_funcs修正の実装〜deaf-RXの系統的切り分け

全詳細は `docs/wifi-shim-c6.md` の実施51〜66，メモリ
`project_c6_agc_investigation.md`を参照。ここでは要点と**次に何を
すべきか**のみをまとめる。

### やったこと（時系列）

1. **実施51**：上記の別PC作業（14コミット）を`git pull --ff-only`で
   取り込み，本セッション独自の未コミット変更（`s_ticks_per_us`
   クロック較正修正・HRT診断カウンタ）とreconcile（コンフリクト1箇所
   のみ，両診断を共存させて解決）。
2. **実施52**：上記「次PCでやること」に従い，NuttXの
   `esp32c6_bringup.c`を参考に`esp_shim_coex_adapter_register()`へ
   `coex_pre_init()`呼出しを追加。**即クラッシュは解消し，PTIゲートは
   開いた**（`0x600a4dd8`=0x71，native一致）。ただしこの時点では別の
   症状（コンソール完全沈黙）が発生。
3. **実施53〜55**：この「完全沈黙」の原因調査は迷走した（block=0x63
   ハング説→反証，coex_pre_init原因説→撤回）。**教訓**：実施55で
   「未検証の測定に乗った結論をすぐ確定させない」という調査規律
   違反を自己検出・撤回した（3度目の同種の事故。
   `feedback_hardware_investigation_rigor.md`に記録済み）。
4. **実施56**：NuttX差し替えで同一ボードの再受信成功を確認
   （ハードウェア無傷を再確認）。同時に，実は「block=0x63ハング」は
   JTAG halt自体のアーチファクトで，**現行ASP3ビルドは実際には
   スキャン完走・coex PTIも開通済み**と判明（症状が実施53-55の
   混乱から実は前進していたことが後から分かった）。
5. **実施57〜66**：ここから本題＝「**送信は正常，受信は完全に
   deaf**」という症状の原因を，以下の順で**系統的に反証**：
   - 実施57: 42 MAC/WDEVレジスタ二分探索（追記21-25の路線）→
     全pokeでも`sta_rx_cb`発火せず＝**反証**（レジスタ群は
     「RXが動いた結果」であって「設定」ではなかった）。
   - 実施58: `sta_rx_cb`より上流のRX成功連鎖
     （`lmacProcessRxSucData`/`ppRxPkt`）も発火せず＝約140/秒の
     MAC割込みは**全てTX完了（bit7）**と判明。
   - 実施59: MAC event `0x600a4c48`のbit14（RX成功）を特定，
     一度も立たないことを確認。
   - 実施60: 静的PHY-RX/AGC/MAC-RX設定は受信中のNuttXと一致＝
     **反証**。
   - 実施61: アンテナ/RFスイッチGPIO3/14/15も一致＝**反証**。
   - 実施62: チャネルdwell/タイミングも正常＝**反証**。
   - 実施63: RFPLLロック・RXクロックイネーブルも正常＝**反証**。
   - 実施64: RXアナログ較正（AGC MMIO＋regi2c block=0x6b）も，
     native値へpokeしても効果なし＝**反証**。
   - 実施65: ★重要★ここまでの判別指標（bit14）自体を疑い，
     **陽性対照**（受信中のnative/NuttXで実際にbit14が立つこと）を
     初めて確認。判別指標は妥当，deaf-RXは本物と確定。
   - 実施66: 外部AI（Codex/GPT-5）相談で得た新リード
     （アロケータのDMA可能性・osi_funcs ABIスロット整合・RX資源値・
     RXディスクリプタのバッファポインタ到達可能性）を検証＝
     **全て反証**（バッファは全てDMA到達可能な内蔵SRAM・16byte
     整列・owner=1で武装済み）。

### 現在の結論

**送信は完全に正常**（TX完了割込み約140/秒＝probe request再送）。
**受信は完全にdeaf**（MAC HW RX成功ビット=bit14が一度も立たず，
RXエラー/CRCビットも立たない＝有効フレームの受信が一度も完了しない）。
同一ボードでnative ESP-IDF/NuttXは問題なく受信（native実測8 AP）＝
**ハードウェアは無傷，ASP3固有のソフトウェア/初期化問題**。

**反証済み（もう調べなくてよい）**：
static-config(60)・antenna(61)・dwell/timing(62)・RFPLL-lock(63)・
RX-clock-enable(63)・RX-FE-analog較正(64)・RXバッファ供給系(66)。

**ブロッカーの所在**：上記全てより**上流**——PHY/RFが有効フレームを
そもそも復調していない（ベースバンド復調そのもの，またはRXを
実際に有効化する未知のステップ）。

### 次にやるべきこと（実施66の申し送り，優先順）

1. **bit14が立つ瞬間の受信機状態をnative/NuttXと比較**：実施60は
   「定常状態」を比較しただけで，「RX成功が実際に起きる瞬間
   （lmacProcessRxSucDataのHWブレークポイントヒット時点）」の
   AGC(`0x600a7000`)/RXゲイン/regi2c RF系レジスタは比較していない。
   これが最優先。
2. **promiscuousモードで「任意のフレームを受信できるか」テスト**：
   フィルタ/ポリシー層の問題かPHY自体の問題かを切り分けられる。
3. **MAC空間`0x600a4000-0x600a4fff`全域の時系列diff**（native受信中
   vs ASP3同scanフェーズ）。
4. **PHY-RXの電源投入/較正ROM関数**（`set_rx_gain`/AGC-init/
   `ram_rfpll_*`等）をnative/ASP3でトレース比較——TX側は遷移するが
   RX側が「受信状態」に一度も入っていない可能性。

### 現在の作業ツリーの状態（重要：コミットされていない）

以下のファイルに未コミットの変更が残っている（`git status`で確認可）：
- `CLAUDE.md`（サブエージェント運用の教訓を追記）
- `asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c`（`coex_pre_init()`
  追加，実施52）
- `asp3/target/esp32c6_espidf/esp_wifi.cmake`
- `asp3/target/esp32c6_espidf/target_kernel_impl.c`（`s_ticks_per_us`
  修正，実施48-49／hardware_init_hookからのcoex_pre_init早期呼出し，
  実施54）
- `asp3/target/esp32c6_espidf/target_timer.c`／`target_timer.h`
  （HRT診断カウンタ，実施50・別PC分が共存）
- `asp3/target/esp32c6_espidf/wifi/esp_shim.c`（MAC割込みイベント計装，
  実施50・58・59）
- `asp3/target/esp32c6_espidf/wifi/wifi_trace.c`（regi2c read-trace，
  RX連鎖wrap，実施39・58）
- `docs/wifi-shim-c6.md`（実施39〜66追記）

`s_ticks_per_us`修正（実施48-49）と`coex_pre_init()`追加（実施52）は
**恒久的に有効な修正として維持すべき**（副作用なく実機検証済み）。
残りの診断計装は一時的だが，次セッションの継続調査にそのまま使える
資産として残置してよい。**コミットは行っていない**——次に何かを
コミットする際は，ユーザーの明示的な指示を得ること。

### ボードの現在の状態

`build/c6_wifi_scan_uart`（現行ASP3ビルド）を書込み，スキャンループ
稼働状態（MAC割込み~140/秒，`RESCAN 0 APs`表示）のまま残置。
受信成功する参照ビルドも保持済み：native ESP-IDF＝`tmp/idf_c6_scan`
（8 AP，無計装），NuttX＝`tmp/nuttx-c6/nuttx.bin`および
`$HOME/.claude/jobs/494f98a3/tmp/nuttx-c6/nuttx`（計装済み，
受信するがコンソールがprintfフラッドで埋まる）。
