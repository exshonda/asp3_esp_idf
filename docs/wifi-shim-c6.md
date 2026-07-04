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

### 到達段階（2026-07-04）

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

`esp_wifi_init()`自体は成功（戻り値0）。しかし`phy_init`の途中で
ASP3のEXCNO_BREAKPOINT例外（`core_kernel_impl.c`の"Breakpoint."ログ）
が発生し，同一のpc/レジスタ値で再現性あり。libphy.a内部の
`ebreak`命令（assert相当）に到達していると推定される（Wi-Fi shim側の
BLE統合（`docs/bt-shim.md`）で見つかった`libbtdm_app.a`の
`emi.c:164`内部アサートと同じ「クローズドソースblob内部で実機でしか
踏めない経路」の可能性が高い）。

**未解決＝原因未特定**。次のセッションはJTAG（OpenOCD-esp32＋
riscv32-esp-elf-gdb．C6の`docs/dev/esp32c6-target.md`に記録済みの
「クラッシュ後にリセット無しでアタッチしhaltのみで読み取る」手法が
このボードでは必須＝物理USBポート1つでJTAG/コンソール共用のため
`continue`はハングする既知の制約）でpc=0x4202432a周辺を単一ステップ
逆アセンブルし，ebreakの直接の呼び出し元・引数（レジスタ値
t0〜t6/a0〜a7が両方の発生で完全一致＝同一の呼び出しパターン）を
手がかりに原因を特定することを推奨する。

### 検証結果

| テスト | 実施 | 結果 |
|---|---|---|
| POSIX | − | 対象外 |
| QEMU | − | 対象外（Espressif版QEMU forkにesp32c6マシンなし．Phase Aで確認済み） |
| 実機 | ○ | ビルド・リンク0エラー・`esp_wifi_init()`成功．`phy_init`途中でBreakpoint例外により停止（原因未特定） |
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
