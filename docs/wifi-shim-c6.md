# Wi-Fi os_adapter shim（ESP32-C6・Phase B-2a）

## 課題

ESP32-C3向けに確立したWi-Fi os_adapter shim（`docs/wifi-shim.md`）を
ESP32-C6へ展開する。C6はWi-Fi 6対応の新しいSoCファミリで，C3とは
割込みコントローラ・一部レジスタブロックの配置が異なる。

## 設計方針：C3のshim基盤を再利用し，チップ固有部分のみ差し替え

`asp3/target/esp32c3_espidf/wifi/esp_shim.c`（静的プールのFreeRTOS風
プリミティブ層）はチップ非依存であり，無改造で流用可能と判明した。
チップ固有の差し替えが必要だったのは以下の3ファイルのみ：

- **`esp_wifi_adapter.c`の`set_intr_wrapper`等**：C3は単一の
  INTMTXブロックにソースルーティングとCPU割込み線制御が同居するが，
  C6はソースルーティング（`INTMTX_BASE=0x60010000`）とCPU割込み線制御
  （Espressif呼称"PLIC"＝`PLIC_MX_BASE=0x20001000`）が分離している
  （arch/riscv_gcc/esp32c6/intmtx_kernel_impl.hのPhase A実機確認済み
  レジスタ配置と同一の値を採用）。
- **`esp_shim.c`の`esp_shim_random()`**：C3の`SYSCON_RND_DATA_REG`
  （0x600260B0）に相当するC6の真のHW RNGレジスタは`WDEV_RND_REG`
  （soc/wdev_reg.h参照）＝`LPPERI_RNG_DATA_REG`
  （`DR_REG_LPPERI_BASE(0x600B2800)+0x8`＝0x600B2808）。C3のB-2bで
  この手のレジスタ取り違えがWPA2 SNonce不具合の原因になった教訓を
  踏まえ，最初から`wdev_reg.h`の定義通りに採用した。
- **`esp_shim_blobglue.c`の`esp_read_mac`（eFuse MACレジスタ）**：
  `DR_REG_EFUSE_BASE`がC3は0x60008800，C6は0x600B0800と異なる
  （オフセット+0x44/+0x48自体は同じ）。

割込み線の予約パターン（線1〜15をWi-Fi shim用に開放し，ターゲット
ペリフェラルは線16以降へ退避）もC3と同じ方針を踏襲。Phase A/B-1で
線1〜3を使っていたコンソール・タイマ・テスト割込みを線16〜18へ
移設した（`target_timer.h`のINTNO_TIMER/INHNO_TIMER・
`target_syssvc.h`のINTNO_SIO・`target_test.h`のINTNO1，および
`target_kernel_impl.c`のルーティング呼出し）。

### C6固有で新たに必要になった追加事項（実機ビルドで判明）

- **`esp_hal_pmu`コンポーネント**：`esp_pmu.h`経由で`hal/pau_types.h`・
  `hal/pmu_hal.h`・`pmu_param.h`が必要（C6以降の新設PMUサブシステム。
  C3のesp_wifi.cmakeには存在しない）。
- **`modem_clock`実ソースの採用**：C3はDirect Bootで欠落する
  `esp_perip_clk_init()`相当をhardware_init_hookで
  `SYSTEM_WIFI_CLK_EN_REG`直書きして代替していたが，C6は
  `periph_ctrl.c`のwifi/bt module enable経路が`modem_clock_module_
  enable/disable`という実関数を直接呼ぶ設計（新設のmodem_clock
  サブシステム）。この実ソース（`esp_hw_support/modem_clock.c`＋
  `hal/esp32c6/modem_clock_hal.c`）をそのまま採用することで，C3の
  ようなhardware_init_hookでのレジスタ直書き代替が**不要という仮説**
  を立てて実装（実機で少なくともクラッシュせず`esp_wifi_init()`が
  成功しているため，現時点でこの仮説は否定されていない）。
- **`rtc_clk.c`（C6版）の部分採用**：`modem_clock.c`が参照する
  `rtc_clk_xtal_freq_get()`のみ必要。ファイル全体（PLL較正含む
  450行）を採用したが，`-ffunction-sections`+`--gc-sections`により
  実際に参照される関数のみリンクされるため，PLL較正等の未使用関数は
  安全に脱落する想定（実機でリンク・起動とも成功しており，この想定は
  裏付けられている）。
- **リンカスクリプトのIRAM/DRAM系セクション追加**：C3の
  `esp32c3.ld`が持つ`.iram1.*`/`.dram1.*`/`.coexiram.*`/
  `.wifi0iram.*`等（blob・実ソースのIRAM_ATTR/DRAM_ATTR関数・変数用
  セクション）の明示ルールがC6の`esp32c6.ld`に存在せず，orphan
  section扱いとなって`.data`とLMAが重なるリンクエラーが発生した。
  C3と同じ全セクション名一覧を追加して解消。
- **ROM ld追加**：C3のWi-Fi ROM ldセット（rom.ld/api/libc/libgcc/
  newlib/libc-suboptimal/version）に加え，C6は
  `esp32c6.rom.{net80211,pp,phy,systimer,coexist}.ld`も必要
  （C6のROMはこれらの関数テーブル・較正データをより多く保持している
  模様）。
- **暫定スタブ3個**（`esp_shim_blobglue.c`のC6版に追加。
  scanのみのスコープでの妥協点）：
  - `putchar`：blobの一部デバッグ経路が参照。no-opスタブ。
  - `floor`：本ツールチェーン（rv32imc/ilp32マルチライブラリ）に
    libm.aが存在しないため最小実装（負の無限大方向への切り捨て）。
  - `phy_get_max_pwr`：C3や他チップは"eco*.ld"（リビジョン別errata
    ROM関数）経由でROM実体が提供されるが，本esp-hal-3rdparty
    スナップショットのC6にはeco*.ldが存在しない。固定値
    （20dBm相当）のプレースホルダで暫定対応。実際の最大送信電力を
    要する機能では要再検討。

## スコープ

- **B-2a**：shim実装＋blobリンク＋`esp_wifi_init()`成功＋scan
  （SSID一覧表示）——実機で検証中（下記「到達段階」参照）
- B-2b（AP接続）・TCP/IPはスコープ外

## 実施結果

### 到達段階（2026-07-04・初回実機実行）

コンパイル・リンクとも0エラーで完了（RAM使用率64.4%／448KB，
Wi-Fi＋lwIPを載せたC3ビルドの95%台より大幅に余裕あり）。実機
（ESP32-C6FH4 rev v0.2，`/dev/ttyACM1`）へ書込み・実行した結果：

```
wifi_scan: esp_wifi_init
esp_shim: task 'wifi' -> tskid 1 (prio 23)
wifi driver task: ..., prio:23, stack:6656, core=0
Breakpoint.
(レジスタダンプ．pc=0x4202432a固定)
wifi_scan: esp_wifi_init -> 0
I (35) phy_init:
Breakpoint.
(同一レジスタダンプ．以降出力なし＝ハング)
```

### `Breakpoint.`例外の実機JTAG調査・根本原因特定・修正（2026-07-04）

`docs/dev/esp32c6-target.md`記載の手法（クラッシュさせてから
リセット無しでOpenOCD＋GDBをアタッチし，`monitor halt`＋メモリ／
レジスタ直接読出しのみで`continue`は使わない）で実機調査した。

**CPUTAPIDについて**：`esp32c6-target.md`は「実機の実際のJTAG IDCODE
0x00005c25が既定値0x0000dc25と不一致」と記録していたが，本調査では
**実機は既定値0x0000dc25のまま一致し，パッチ不要**だった（`adapter
serial <MAC>`でC3/C6の2枚のUSB-JTAGデバイスを正しく区別する方が
重要＝両者ともVID:PID 0x303a:0x1001で無指定だと誤って別ボードに
アタッチしてしまう）。旧記録は別の個体差・別のOpenOCD版に基づく
可能性がある。

**確定した根本原因**：`monitor halt`後のバックトレースはASP3の
`ext_ker()`終了処理（`_kernel_target_exit`の`while(1);`）で安定して
おり，これは"Breakpoint."例外ハンドラ（`default_exc_handler`→
`ext_ker()`）が正常に完走した結果＝**クラッシュ後の正常な終了状態**
だった。実際の切迫点はシリアルログの`pc=0x4202432a`（クラッシュ
瞬間のレジスタダンプで確定済み）。ELFを静的逆アセンブルした結果：

```
periph_module_reset+0:  jal loc_cpu
periph_module_reset+12: li a5,3
periph_module_reset+14: bltu a5,s0,+56      # module(s0) > 3 なら+56へ
periph_module_reset+18: ...（module 0〜3の正規テーブル参照）...
periph_module_reset+56: lw a5,0(zero)       # ← GCCが生成した
periph_module_reset+60: ebreak               #   「到達不能」トラップ
wifi_module_enable+0:   li a0,4
                        j modem_clock_module_enable
```

`periph_module_reset()`（`esp_hw_support/periph_ctrl.c`，実ソース）は
`shared_periph_module_t`という**狭い**enum（`soc/periph_defs.h`：
`TIMG0=0,TIMG1=1,UHCI0=2,SYSTIMER=3,WIFI=4,BT=5,...`）を受け取るが，
実装（`hal/clk_gate_ll.h`の`periph_ll_get_rst_en_mask`等）はTIMG0/
TIMG1/UHCI0/SYSTIMERの4種のみをswitchで扱い，それ以外はdefaultで
`return 0`としている。しかしGCCはこのenum型の値域を0〜3（4値）と
静的に確定できるため，「4以上は到達不能」と判断し，`default:`分岐を
`__builtin_trap()`相当（`lw a5,0(zero); ebreak`）へ最適化していた
（`-O2`のenum値域最適化．ソースコード上は一見安全な`default: return 0;`
だが，実行時には未定義動作としてtrapになる）。

`wifi_reset_mac_wrapper()`（本ファイルのC3版を流用した箇所）が
`periph_module_reset(PERIPH_WIFI_MODULE)`を直接呼んでいたが，C6は
modem_clockサブシステム導入によりWIFI/BTのリセットは
`modem_clock_module_mac_reset()`（`esp_hw_support/modem_clock.c`）
経由に変わっている。真正のESP-IDF/NuttX版C6リファレンス
（`hal/components/esp_wifi/esp32c6/esp_adapter.c:306`）も
`modem_clock_module_mac_reset(PERIPH_WIFI_MODULE)`を使っており，
`periph_module_reset()`は呼んでいない。加えて，本ファイルは
`esp_private/periph_ctrl.h`が無い前提で`PERIPH_WIFI_MODULE`をC3の
値（24＝C3の`periph_module_t`という別の広いenumでの値）に決め打ち
していた——C6の`shared_periph_module_t`ではWIFIは4であり，二重に
誤っていた。

**修正**（`wifi/esp_wifi_adapter.c`）：
- `wifi_reset_mac_wrapper()`を`modem_clock_module_mac_reset(PERIPH_WIFI_MODULE)`
  呼出しに変更。
- `PERIPH_WIFI_MODULE`のハードコード値（24）を廃し，`soc/periph_defs.h`
  （既存インクルードパスから到達可能）の実enumを使うよう
  `#include "soc/periph_defs.h"`・`#include "esp_private/esp_modem_clock.h"`
  を追加。

実機で再検証（`/dev/ttyACM1`）：

```
wifi_scan: esp_wifi_init -> 0
mode : sta (58:e6:c5:12:d4:d0)
wifi_adapter: set_intr src=2 intno=1 prio=1
wifi_adapter: set_intr src=0 intno=1 prio=1
esp_shim: set_isr intno=1 handler=4206248a
esp_event: WIFI_EVENT id=2
wifi_scan: esp_wifi_start -> 0
wifi_scan: esp_wifi_scan_start -> 0
（約2.4秒後）
esp_event: WIFI_EVENT id=1
wifi_scan: 0 APs found (err=0)
wifi_scan: done
```

`Breakpoint.`例外は解消し，`esp_wifi_init`/`esp_wifi_start`/
`esp_wifi_scan_start`とも成功，スキャンは約2.4秒間（複数チャネルの
巡回に相応の実時間）実行されてから正常に`WIFI_EVENT_SCAN_DONE`で
完了する。**しかし発見AP数は0**——同一の実験環境でC3のB-2b時に
AP16〜17個をスキャン済みの実績があるため，環境側の問題ではなく
C6ドライバ側の残課題と判断する。

### 残課題：スキャンは完走するが発見AP数0（原因未特定）

以下を切り分け済み：

- **国コード／許可チャネル制限ではない**：`esp_wifi_set_country()`
  （cc="01"，schan=1，nchan=13，MANUAL policy）を明示的に呼んでも
  結果は変わらず（`esp_wifi_set_country -> 0`は成功するがAP数は
  依然0）。この変更は効果がなかったため`apps/wifi_scan/wifi_scan.c`
  には反映していない（C3と同一のまま維持）。
- **割込み登録パターンはC3と同型**：`set_intr src=0,2 → intno=1`
  （WIFI_MAC・WIFI_PWRの2ソースを共有CPU線1へルーティング）は
  C3でも同じ構成で動作実績がある（osi関数テーブル経由でblob自身が
  要求するソース数はチップ非依存の`wifi_init.c`ロジックに従うため）。
  割込み経路自体を疑う根拠は薄い。
- **esp_timer/ets_timer機構はC3と共有・無改造**：チャネルホップ等が
  依存するタイマ機構（`wifi/esp_shim.c`のets_timerタスク）はC3の
  B-2a成功時と同一実装であり，本チップ固有の変更は入れていない。

未検証・次の一手候補：
- `phy_init.c`のPHY較正（`register_chipv7_phy`）が実際に成功して
  いるかの確認（`TCNT_SYSLOG_BUFFER=128`でも起動時ログバーストで
  較正結果メッセージが溢れている可能性．バッファ増量またはUART
  側のポーリング間隔短縮で全量キャプチャを試す）。
- JTAGで`monitor halt`によるチャネル巡回中の実際のチャネル
  レジスタ値の時系列サンプリング（本当にチャネルが切り替わって
  いるか，あるいは1チャネルに固定されたままかの確認）。
- C6の`hal/components/esp_wifi/esp32c6/esp_adapter.c`と本ファイル
  （`wifi/esp_wifi_adapter.c`）の全関数を1対1で再点検し，
  `wifi_reset_mac_wrapper`と同種の「C3流用時の見落とし」が
  他の関数にも残っていないか確認する（本セッションでは
  `PERIPH_WIFI_MODULE`関連のみ確認済み，他のwrapper全数は未点検）。

### 検証結果

| テスト | 実施 | 結果 |
|---|---|---|
| POSIX | − | 対象外 |
| QEMU | − | 対象外（Espressif版QEMU forkにesp32c6マシンなし．Phase Aで確認済み） |
| 実機 | ○ | ビルド・リンク0エラー・`esp_wifi_init/start/scan_start`とも成功・スキャン完走（約2.4秒）．発見AP数0（原因未特定・上記「残課題」参照） |
| 既存ビルドへの回帰 | 未確認 | 本セッションでは未実施．次セッションでtest_porting／既存C6ビルド（Wi-Fi無効）の回帰確認を推奨 |

### 変更したファイル

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c6_espidf/target.cmake` | `ESP32C6_WIFI`オプション追加。shim基盤（`wifi/esp_shim.*`）を`ESP32C6_WIFI`でも取り込むよう`ESP32C3_WIFI`と共有ゲート化 |
| `asp3/target/esp32c6_espidf/esp32c6.ld` | IRAM_ATTR/DRAM_ATTR系セクション（`.iram1.*`等）の明示ルール追加 |
| `asp3/target/esp32c6_espidf/target_timer.h` | `INTNO_TIMER`/`INHNO_TIMER`を1→16へ変更 |
| `asp3/target/esp32c6_espidf/target_syssvc.h` | `INTNO_SIO`を2→17へ変更 |
| `asp3/target/esp32c6_espidf/target_test.h` | `INTNO1`を3→18へ変更 |
| `asp3/target/esp32c6_espidf/target_kernel_impl.c` | 上記変更に合わせ`esp32c6_intmtx_route()`呼出しの線番号を更新 |

### 追加したファイル

- `asp3/target/esp32c6_espidf/esp_wifi.cmake`（C3版からWIFI_CHIP_SERIES等を全置換した上でC6固有の追加インクルードパス・ROM ld・ソースを追記）
- `asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`（C3版を割込みレジスタのみ差し替え）
- `asp3/target/esp32c6_espidf/wifi/esp_shim.c`（C3版をRNGレジスタのみ差し替え）
- `asp3/target/esp32c6_espidf/wifi/esp_shim_blobglue.c`（C3版をeFuseレジスタ差し替え＋C6固有スタブ3個追加）

### Git情報

- ベースコミット：C6 Phase B-0/B-1完了時点（`9b079cc`）
- 関連コミット範囲：本コミット
