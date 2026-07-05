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

### 残課題：スキャンは完走するが発見AP数0（WiFi MAC割込みが一切発生しない・原因は未特定だがMAC/PHY側に絞り込み済み）

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
- **esp_timer/ets_timer機構はC3と共有・無改造**：チャネルホップ等が
  依存するタイマ機構（`wifi/esp_shim.c`のets_timerタスク）はC3の
  B-2a成功時と同一実装であり，本チップ固有の変更は入れていない。
- **`slowclk_cal_get_wrapper()`の較正値変換ミスは発見したがscanには無関係**：
  実機で読み取った値は`0x003ad511`（LP_AON_STORE1＝
  `RTC_SLOW_CLK_CAL_REG`．レジスタ自体は真正リファレンスと同一で
  アドレスは正しい）。しかし真正のC6リファレンス
  （`hal/components/esp_coex/esp32c6/esp_coex_adapter.c`の
  `esp_coex_common_clk_slowclk_cal_get_wrapper`）はこの値を
  `>> (RTC_CLK_CAL_FRACT(=19) - SOC_WIFI_LIGHT_SLEEP_CLK_WIDTH(=12))`
  ＝7ビット右シフトしてからblobへ渡すのに対し，本ファイル（C3から
  そのまま流用）は無変換で返しており，約128倍過大な値をblobへ渡して
  いる（本来は136kHz相当のところ，シフトなしでは約1kHz相当に見える
  計算になる）。**ただしC3の同名関数も全く同じ無変換の実装であり
  （C3もこのバグを持ったまま），C3はB-2aで16〜17個のAP発見に成功して
  いる**。かつ`esp_wifi_set_ps(WIFI_PS_NONE)`でPower Save機能自体を
  無効化しているため，この較正値が実際に消費されるコードパス
  （light-sleep関連のduty-cycle計算）はスキャン中は実行されないと
  考えられる。よってこれは実在する独立したバグだが，今回の「AP数0」
  とは無関係と判断し，深追いを打ち切った（修正は別タスクとして
  独立に行うのが妥当）。
- **esp_rom_printf経由のPHY診断ログ（`ESP_LOGI`等）は本ポートでは
  一切コンソールに出力されない**：`ESP_LOG_EARLY_IMPL`（`NON_OS_BUILD=1`
  経路）は`esp_log()`ではなく`esp_rom_printf()`を直接呼ぶため，ASP3の
  `syslog()`／`syslog_msk_log()`優先度マスクの対象外である。実機実験で
  `ets_install_putc1()`によるフック＋実機コンソールと同じレジスタ
  直書きputcを試したが，`esp_rom_printf`の出力は依然コンソールに
  現れなかった（原因未特定．ROM側の別チャネル選択・バッファリングの
  可能性）。**この診断チャネル自体は今回は使えなかった**――以後PHY
  較正状況を診断する場合は，`esp_wifi_adapter.c`のラッパー関数に
  直接`syslog()`（ASP3標準機構．確実に届く）を仕込む方式を使うこと
  （`phy_enable_wrapper()`前後に`syslog(LOG_NOTICE, ...)`を仕込んで
  `esp_phy_enable()`自体はクラッシュも長時間ブロックもせず正常に
  復帰することは確認済み＝PHY初期化呼び出し自体は完走している）。

#### 決定的な切り分け：WiFi MAC割込みソースが実機で一切アサートされない（実機JTAG，2026-07-04）

`wifi/esp_shim.c`に既存の`esp_shim_int_count[]`（割込み線ごとの
ディスパッチ回数カウンタ．新規実装不要で既存コードに既にあった）を
`apps/wifi_scan/wifi_scan.c`から一時的に参照し，スキャン完了直後に
`intno1`（WiFi MAC/PWRの共有CPU割込み線）のディスパッチ回数を出力：

```
wifi_scan: intno1 count=0
```

**スキャン中（約2.4秒間）に一度も割込みがディスパッチされていない**。
これだけでは「CPU側でマスクされている」か「MAC自体が割込み要求を
一切出していない」かを区別できないため，実機JTAG（`docs/dev/
esp32c6-target.md`記載の「クラッシュ／意図的な無限ループでの停止後，
リセット無しでアタッチしhaltのみで読み取る」手法．本セッションで
`adapter serial <MAC>`によるC3/C6デバイス選択の重要性を確認――
両ボードとも同一VID:PID 0x303a:0x1001でUSB列挙されるため無指定だと
誤って別ボードにアタッチする）で以下を直接確認した：

| レジスタ | 値 | 意味 |
|---|---|---|
| `INTMTX_BASE+0`（src=0＝WIFI_MAC routing） | `0x00000001` | CPU割込み線1へ正しくルーティング済み |
| `INTMTX_BASE+8`（src=2＝WIFI_PWR routing） | `0x00000001` | 同上 |
| `PLIC_MX_BASE+0x000`（ENABLE） | `0x00030002` | bit1（線1＝WiFi）・bit16/17（線16/17＝コンソール/タイマ）とも許可済み |
| `PLIC_MX_BASE+0x014`（PRI，線1） | `0x00000002` | `set_intr_wrapper`が書き込んだ優先度2 |
| `PLIC_MX_BASE+0x090`（THRESH） | `0x00000001` | 起動時既定（`intmtx_set_thresh(0)`→レジスタ値=0+1）．優先度2はこの閾値を超えており通過するはず |

ルーティング・優先度・許可ビット・閾値のいずれもCPU側の設定として
正しく，割込みがCPU側の設定で塞き止められている形跡はない。

そこで**ソース側の生ステータス**（`INTMTX_STATUS0`＝
`INTMTX_BASE+0x134`．bit0=WIFI_MAC，bit2=WIFI_PWRの生の要求状態．
CPU側のルーティング／許可／閾値を一切経由しない未加工値）を，
スキャン待機ループ内で50ms間隔でポーリングしOR蓄積する実験を実施：

```
wifi_scan: INTMTX_STATUS0 OR'd = 0x00000000 (bit0=MAC bit2=PWR)
```

**スキャン中の約2.4秒間，WIFI_MAC・WIFI_PWRいずれの生ステータスも
一度も立たなかった**。これは「CPU側の設定不備で塞き止められている」
可能性を排除し，**WiFi MACハードウェア自体が割込み要求を一切
出していない**ことを実機で直接確認したことを意味する。

これは同じ症状クラスがC3のB-2a初期ブロッカー（`docs/wifi-shim.md`
「hal_initハング」節）でも観測されていたことと符合する：C3では
`0x60033D14`という`hal/`ヘッダに一切定義のないWIFI_MAC内部レジスタの
bit0を`wifi_hw_start`が無限ポーリングしており，これはモデムクロック
未初期化が原因だった（`SYSTEM_WIFI_CLK_EN_REG`直書きで解消）。C6では
同じクラスの「WIFI_MAC内部の未文書化な状態」が原因である可能性が
高いが，C3のような公開されたクロックイネーブルレジスタでの解決とは
異なり，**C6は既にmodem_clock実ソースを正しく経由しており
（`esp_wifi_init`は成功しwifi_hw_start相当の段階もハングせず通過），
クロック供給自体は疑わしい形跡がない**。すなわちC3で有効だった対処
（クロックイネーブルの直書き）に相当する既知の一手が見当たらない。

#### 結論・次セッションへの申し送り

- **原因はソフトウェア側の設定（割込みルーティング・優先度・閾値・
  タイマ・国コード）ではなく，WiFi MACハードウェアが割込みを一切
  発生させていないという事実に絞り込み済み**。
- 残る仮説：(1) MAC内部の未文書化イネーブル/リセット手順が
  Direct Bootで欠落している（C3の`0x60033D14`と同種だが，C6版の
  該当レジスタ・条件は未特定），(2) PHY較正（`register_chipv7_phy`）
  自体は完走する（クラッシュ・ハングなし）が結果が無効でRF的に
  何も受信できていない，(3) libphy.a/libnet80211.a等，closed-source
  blob内部のC6固有の初期化ステップが本来必要（esp-hal-3rdpartyの
  ソースからは見えない）。
- (1)(2)の切り分けには，C3の`0x60033D14`探索時と同じ手法
  （実機JTAGでwifi_hw_start/hal_init相当のコードパスを単一ステップ
  逆アセンブルし，MAC内部レジスタへの読み書きを洗い出す）が有効と
  思われるが，本セッションでは時間の都合で未実施。
- この状態は`emi.c:164`（BLE）と違い，**クローズドソースblob内部の
  assertには一切到達していない**（scanは正常にSCAN_DONEで完了する）
  ため，「明確な閉じたデッドエンド」ではなく，引き続きJTAG単一
  ステップ調査で前進できる可能性がある，という点で`emi.c:164`とは
  性質が異なる。

### 検証結果

| テスト | 実施 | 結果 |
|---|---|---|
| POSIX | − | 対象外 |
| QEMU | − | 対象外（Espressif版QEMU forkにesp32c6マシンなし．Phase Aで確認済み） |
| 実機 | ○ | ビルド・リンク0エラー・`esp_wifi_init/start/scan_start`とも成功・スキャン完走（約2.4秒）．発見AP数0＝WiFi MAC割込みソース自体が実機で一切アサートされないことをJTAGで確認済み（原因はMAC/PHY側に絞り込み・未特定．上記「残課題」参照） |
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
- 関連コミット範囲：`4e39105`〜`2c40b0b`

## 追加調査（アンテナ接続確認後の再調査，2026-07-05）

### 発端

前回セッション終了時点では「WiFi MAC割込みソースが実機で一切
アサートされない」ところまで絞り込んだが，このとき使用していた
実機基板には**アンテナが未接続**だった（RSSI -82〜-94dBm）。

コーディネータ側で対照実験を実施：同一基板に**アンテナを接続**した
状態で，素のESP-IDF v5.5 `examples/wifi/scan`をビルド・書き込み。
結果：**AP 10〜11個検出，最良RSSI -64〜-66dBm**。基板・アンテナの
ハードウェア健全性が確定した。

本セッションでは，まずこの前提を検証してから調査を再開した。

### 実施1：アンテナ接続下でのASP3再検証（回帰確認）

同一基板・同一ポート（`/dev/ttyACM0`，`esptool chip-id`で機種を
都度確認。セッション中に一度C3実機が取り外されており，ポート
番号は固定と仮定しないこと）で：

1. コーディネータ提供の参照ビルド（`idf_c6_scan/build/scan.elf`他）
   を書き込み直し，AP 10〜11個・RSSI -66〜-95dBm（最良-66dBm）を
   再現 → アンテナ接続・基板健全性を自分の手元でも確認。
2. ASP3ビルド（変更なし）を同一基板へ書き込み直し → **依然AP 0個**。

アンテナ有無に関わらずASP3側は0個のまま＝「弱電界のせいで見えて
いないだけ」という可能性は排除できた。ソフトウェア側の実際の
ギャップであることが二重に確定した。

### 実施2：MAC/PHYレジスタの広域バイナリ比較（JTAG）

前回セッションでは`0x600a7128`（AGC）・`0x60033d14`（C3由来の
旧hal_initハング調査レジスタ）の2箇所のみをスポット比較していたが，
今回は**範囲を広げた無差別バイナリdump比較**を実施した。

手法：
- 参照ビルド（idle，`esp_cpu_wait_for_intr`で静止）とASP3ビルド
  （`wifi_scan.c`に一時的な`while(1){}`スピンマーカーを挿入し，
  `esp_wifi_scan_start()`後・`scan_done`待ちループの後で停止）の
  両方をJTAG（`monitor halt`のみ，`reset`なし）で停止させ，
  `dump binary memory`で同一アドレス範囲を採取。
- 採取範囲：`0x60030000`-`0x60034000`（MAC，16KB）／
  `0x600a7000`-`0x600a8000`（PHY/AGC，4KB）／
  `0x600a9800`-`0x600aa800`（modem syscon，4KB）。
- `cmp -l`でバイト単位比較。

結果：

| 範囲 | サイズ | 差分バイト数 |
|---|---|---|
| MAC (`0x60030000`) | 16KB | **0**（完全一致） |
| PHY/AGC (`0x600a7000`) | 4KB | 59（複数箇所に分散） |
| modem syscon (`0x600a9800`) | 4KB | **0**（完全一致） |

MAC制御ブロック16KB全域・modem syscon 4KB全域が参照実装と
**バイト単位で完全一致**。PHY/AGC領域の差分は，オフセット
`0x128`（＝`0x600a7128`のAGC値そのもの）を含む複数クラスタに
分散しており，個々の値が起動毎に変動する実環境依存のRSSI/
ノイズフロア追跡値と整合する（両ビルドとも「idle」到達までの
経過時間・電波環境が異なるため，動的トラッキング値が食い違うのは
当然）。

**結論**：少なくとも本比較範囲では，MAC制御レジスタ・modem
syscon設定に静的な設定ミスは見当たらない。（※本比較はどちらも
scan完了後のidle状態同士であり，スキャン実行中のリアルタイムな
MAC挙動を捉えたものではない点に注意。scan実行中の同時比較
（timing-matchedなmid-scan比較）はUSB再接続タイミングの不確実性
とJTAGネイティブリセットが正規の電源投入起動と異なる挙動
（`esp_cpu_wait_for_intr`に即座に落ちる＝パニックハンドラ経由の
疑いあり）を示したため今回は断念した。次セッションでの課題。）

### 実施3：blob内蔵ログゲート（`g_log_level`）の発見と操作

参照ログ（`c6_scan_output.log`）に現れる`"wifi:"`接頭辞付きの
詳細診断行（バッファ数・`mac_version`・`ACK_TAB0`/`CTS_TAB0`・
`(agc)`較正行）が，ASP3側では**一度も出力されない**という
未解決の謎があった。静的解析（`libnet80211.a`の`ieee80211_debug.o`
を`ar x`→`nm`/`objdump`）の結果，これらの行は`wifi_log()`という
blob内部関数を経由しており，グローバル変数`g_log_level`
（`libcore.a`の`.bss`シンボル，実行時は`0x40801800`）でゲートされて
いることが判明した。

**検証**：`wifi_scan.c`の`esp_wifi_init()`呼び出し**前**に
`g_log_level = 0xffffffffU`をセットしても効果なし。理由を追うため
`esp_wifi_init()`の直後に読み直したところ，**値が`0x00000003`に
上書きされていた**＝`esp_wifi_init()`自身がこの変数を既定値へ
リセットすることを実測で確認した。

そこで書き込み位置を`esp_wifi_init()`の**後**（`esp_wifi_start()`
呼び出し前）に移し，再度`0xffffffff`にセットしたところ，blob内部の
詳細トレース（`"scan number 0"`・`"set_scan_id=%d"`等）が実際に
出力され始めた。**この操作自体は成功する**ことを実機で確認した。

ただし2点の重要な副作用・限界が見つかった：

1. **メッセージ消失**：ログ量が急増し`"295 messages are lost."`
   （blob側のログキュー溢れを示す文字列）が出た。ASP3側の
   `TCNT_SYSLOG_BUFFER`を128→512に，`log_writev_wrapper`の
   スタック上バッファを128→256バイトに一時的に拡張したところ
   `"295 messages are lost."`は解消したが，今度は別の破損が
   顕在化した（次項）。
2. **一部メッセージの破損**：`"d=128, s"`のような断片や，制御
   文字混じりの行が頻発する。原因を`syssvc/vasyslog.c`
   （`tt_syslog`）まで遡って確認したところ，ASP3の`syslog()`は
   フォーマット文字列と引数を**即座に文字列化せず，ポインタと
   スカラー値のまま`SYSLOG`構造体に格納し，出力時に遅延整形する**
   設計であることが分かった。`log_writev_wrapper`は
   `vsnprintf(buf, ..., format, args); syslog(LOG_NOTICE, "%s", buf);`
   という実装になっており，**`buf`はスタックローカル変数**である。
   この`"%s"`引数（`buf`へのポインタ）がリングバッファに保存され，
   `log_writev_wrapper`の呼び出しフレームが終了した後（別の関数
   呼び出しでスタックが再利用された後）に遅延整形されると，
   既に無効な内容を指すポインタを読むことになり文字化けする，
   という筋の通った説明ができる。

   **この破損バグ自体は実在するが，`g_log_level`を人為的に
   引き上げた場合にのみ顕在化する診断専用の副作用であり，既定
   （`g_log_level=0x3`）での通常動作では`log_writev_wrapper`が
   ほぼ何も出力しないためこの経路は踏まれない**。すなわち今回の
   「AP 0個」問題の直接原因ではないと判断したが，将来この
   ラッパーを診断強化のために使う場合は要修正（`"%s"`＋一時
   バッファではなく，`vsyslog`相当の即時出力関数を使うか，
   ASP3の`syslog()`自体を「即時整形して固定長にコピーする」
   実装に変える必要がある）。

3. **標準ESP-IDFタグ付きログ（`"wifi:Init data frame..."`等）は
   `g_log_level`とは別系統**：これらは恐らくESP-IDF標準の
   `esp_log`（タグ`"wifi"`のレベルテーブル）経由であり，blobの
   `wlog_*`（`g_log_level`ゲート）とは異なるメカニズムである。
   本セッションでは`esp_wifi_init()`実行**後**にしか`g_log_level`
   を書き換えられなかったため，init中に出るはずのこれらの行を
   遡って確認する術がなく，「出力されない＝該当初期化コードが
   実行されていない」という結論は導けない（後から書いても
   間に合わないだけの可能性が高い）。

### 実施4：blob内部トレースから得られた新しい手がかり

`g_log_level`を`0x7`（error+warning+info相当，ただし正確な
ビット意味は未解読）にした状態でスキャン一巡を採取したところ，
以下が読み取れた（値そのものは毎回破損なく再現）：

- `"scan number 0"`：スキャン実行中に何度も出力され，**常に値が0**。
- `"set_scan_id=0"`：スキャン開始時に一度出力。
- **`"scan_done: arg=%p, status=%d, cur_time=%d, scan_id=%d, scan state=%x"`
  （`ieee80211_scan.o`，VERBOSEタグ）が，`g_log_level=0xffffffff`
  （全ビット）にしても一度も出力されない。**

`scan_done: ...`はblobが**通常のスキャン完了処理**で必ず経由する
はずの診断行（`libnet80211.a`の`ieee80211_scan.o`に実在を確認済み）
であるにも関わらず一度も出力されないことから，ASP3側で観測している
`WIFI_EVENT_SCAN_DONE`は，この通常完了パスではなく**別の経路
（タイムアウト/フォールバック相当）で発火している可能性が高い**。
これは前回確認済みの事実（`INTMTX_STATUS0`が終始`0x00000000`，
`esp_shim_int_count[1]`が終始`0`＝WiFi MACが割込みを一切
アサートしていない）と整合する仮説であり，「MACが何も受信して
いない」→「per-channelの正常完了ロジックが一度も走らない」→
「タイムアウトで強制的にSCAN_DONEへ落ちる」という一連の流れを
矛盾なく説明する。

### 結論・次セッションへの申し送り（更新）

- **アンテナ接続下でも症状は不変**：ハードウェア要因は完全に排除。
- **MAC制御ブロック（16KB）・modem syscon（4KB）は既知良品と
  バイト単位で完全一致**：静的レジスタ設定のミスという仮説は
  大きく後退した（ただし比較はidle同士。mid-scanの動的比較は
  タイミング制御の難しさから未達成）。
- **blobの正常スキャン完了パス（`scan_done: arg=...`診断）が
  一度も実行されていない**ことが新たに判明：ASP3で見えている
  `SCAN_DONE`イベントはタイムアウト/フォールバック経由である
  可能性が高く，「MACが一切受信していない」という既存の結論と
  整合する，最も具体的な新しい手がかり。
- **`g_log_level`はesp_wifi_init()呼び出し直後に上書きされる
  ため，init内部の詳細ログ（buffer数・mac_version等）を捉える
  には，init前ではなく，blobの該当関数を個別にフックする
  （またはNVS実装を追加してblobにログレベルをNVS経由で恒久的に
  渡す）必要がある**：今回は未着手。
- **残る仮説**（優先順位順）：
  1. MAC内部の未文書化なRXアーム/イネーブル手順が抜けている
     （`scan_done`の正常経路が踏まれない＝おそらくRX側が一度も
     フレームを受理していない）。C3の`0x60033D14`と同種だが，
     C6版の該当レジスタ・条件は依然未特定。
  2. PHY較正（`register_chipv7_phy`）は完走する（クラッシュ・
     ハングなし）が，結果が実際には無効でRF的に何も受信できて
     いない（アンテナ有無で症状が変わらないため，較正の入力値
     自体ではなく較正結果の適用/反映側を疑うべき）。
  3. `wifi_create_queue_wrapper`等，キュー・バッファプール
     （`ESP_SHIM_NUM_DTQ=4`，各深さ256）の値自体は枯渇していない
     ことを確認済み（`esp_shim_malloc`の失敗ログ・
     `esp_shim_queue_create`の"queue pool exhausted"ログとも
     一度も出ていない）ため，このプール設計自体が直接原因である
     可能性は低い。
- **次の一手の提案**：
  - mid-scan（スキャン実行中，完了前）のMAC/PHYレジスタ広域比較を
    タイミング制御込みで再挑戦する（JTAGネイティブリセットでは
    なく，esptoolのRTSベースリセット＋十分な待ち時間で参照側を
    確実にscan実行中に捕まえる。あるいは参照側アプリを改造して
    scan開始直後に`while(1){}`スピンを仕込んだカスタムビルドを
    作るのが最も確実）。
  - `scan_done`本来の呼び出し元（`ieee80211_scan.o`内，`scan_done`
    という関数名または類似シンボル）を逆アセンブルし，どの条件
    分岐でこの診断ログ付きパスに入るか（＝MACから何らかの
    完了通知を受け取った場合のみ入るのか）を特定する。
  - もしタイムアウト経由でのSCAN_DONE発行が確定した場合，
    「タイムアウトまでの待ち時間」がESP-IDF既定値と一致するかを
    確認し（既に確認済みの約2.4秒という所要時間が，正常スキャン
    の所要時間なのか，タイムアウト値そのものなのかを見極める）。

これは`emi.c:164`（BLE）のような閉じたデッドエンドではなく，
引き続き実機JTAG・blob静的解析の両輪で前進できる状態にある。

## 追加調査（動的シーケンス比較・クロックドメイン検証，2026-07-05）

コーディネータからの指示：「scan_done未発火」を軸に，(1) MAC RX-arm
シーケンスの単一ステップ比較，(2) PHY較正結果の反映（適用）漏れ，
(3) 割込みイネーブルのタイミング競合（エッジ検出漏れ），(4)
`log_writev_wrapper`のスタック破損バグ修正，の4点を調査。

### 実施5：INTMTX raw statusの高頻度サンプリング（hrt割込み経由）

前回の50ms間隔ソフトポーリングでは短いエッジパルスを取りこぼす懸念
があったため，`target_hrt_handler()`（既存のSYSTIMER割込み，実測で
1回のスキャン中に約18回発火）にOR蓄積の診断コードを一時追加し，
`INTMTX_STATUS0/1/2`（`0x60010134/138/13C`）を割込みコンテキストで
継続サンプリングした。

結果：`status0_or=0x00000000`（変わらず），**`status1_or=0x00000004`
（bit2）が新たに検出された**。ビット位置から`ETS_MODEM_PERI_TIMEOUT_
INTR_SOURCE`（インデックス34＝STATUS1のbit2）と特定し，これがモデム
ドメイン（WiFi/BT）へのバスアクセスタイムアウト監視割込みであると
判明。詳細レジスタ`HP_SYSTEM_MODEM_PERI_TIMEOUT_ADDR_REG`
（`DR_REG_HP_SYSTEM_BASE(0x60095000)+0x28`）を読むと
**`addr=0x600a2868`**（`PWDET_CONF_REG=0x600a0810`と
`IEEE802154_REG_BASE=0x600a3000`の間＝WiFiベースバンド系の
未公開レジスタ領域）を指しており，一見「ベースバンドへのバス
アクセスがタイムアウトしている」という強い手がかりに見えた。

### 実施6：モデムクロックドメイン修正の試みと，タイムアウトの正体（ラッチの罠）

`HP_SYSTEM_MODEM_PERI_TIMEOUT_{ADDR,UID}_REG`はクリアするまで最初の
発生アドレスを保持し続ける**ラッチ式**レジスタである。この事実に
気づく前に，まず仮説（WIFIPWRクロックドメイン未有効化）を検証し，
実際に修正を投入した：

- `esp_perip_clk_init()`（esp_system/port/soc/esp32c6/clk.c）が通常
  起動時に`modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,...)`
  を呼び，これが副作用として`modem_clock_hal_enable_wifipwr_clock()`
  （`modem_lpcon.CLK_CONF_REG`＝`0x600af018`のbit0＝`CLK_WIFIPWR_EN`）
  を有効化する。Direct Bootではこの関数自体が一切呼ばれないため，
  この専用クロックゲートが未有効化のまま残る可能性がある。
- `wifi_clock_enable_wrapper()`に`modem_clock_deselect_all_module_lp_
  clock_source()`＋`modem_clock_select_lp_clock_source(PERIPH_WIFI_
  MODULE, MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0)`を追加。リンクに必要な
  `efuse_hal_chip_revision()`（`hal/efuse_hal.c`＋チップ固有の
  `hal/esp32c6/efuse_hal.c`を採用）・`esp_sleep_pd_config()`／
  `esp_sleep_clock_config()`（重量級の`sleep_modes.c`を避け，
  `esp_shim_blobglue.c`に常時`ESP_OK`を返す最小スタブを追加）も
  合わせて解決。

その後，`HP_SYSTEM_MODEM_PERI_TIMEOUT_CONF_REG`のbit16
（`INT_CLEAR`）へ書き込んでラッチをクリアしてから各初期化段階
（`main_task`冒頭・`wifi_clock_enable_wrapper`前後・
`phy_enable_wrapper`前後・`esp_wifi_scan_start`直前）で再読出しする
一時計測を追加したところ，**`main_task`冒頭で一度クリアした後は，
WiFi初期化〜スキャン完了までの全期間を通して一度も再アサートされ
なかった**（`status1_or`も`0x00000000`に変化）。

**結論**：`addr=0x600a2868`のタイムアウトは，WiFi初期化より**前**
（起動直後・cpu_start前後のどこか）で一度きり発生した，WiFiとは
無関係の既存事象がラッチに残っていただけだった。「バスアクセス
タイムアウトが起きている」という前回セッションの解釈は誤りで
あり，実施5・6前半の発見は**赤信号（red herring）**だったと結論
する。

### 修正の要否をJTAG実測で直接検証

上記により「バスタイムアウト」という修正の根拠は消えたが，
`clk_wifipwr_en`ビット自体の要否は実機レジスタ比較で独立に検証
できるため実施した：

| 状態 | `0x600af018`の値 |
|---|---|
| 修正なし（`wifi_clock_enable_wrapper`の追加呼出しを`#if 0`で無効化） | `0x00000006`（bit0=0） |
| 修正あり（現在のコード） | `0x00000007`（bit0=1） |
| 参照実装（素のESP-IDF v5.5，同一基板，アイドル時） | `0x00000001`（bit0=1） |

**`clk_wifipwr_en`（bit0）は，修正がないと0，修正があると1になり，
参照実装の1と一致する**。すなわちこの修正はレジスタ状態を参照
実装に合わせる，独立に正当化できる実際の修正である（`bit1`
＝`CLK_COEX_EN`と`bit2`＝`CLK_I2C_MST_EN`は両ビルドの本ASP3側で
のみ余分に立っており，`esp_shim_coex_adapter_register()`経由と
見られるが，参照実装にはなくWiFiスキャン単体には不要と見られる
＝実害はないが要検討の小さな乖離として記録）。

**ただし，この修正を適用した状態でも「AP 0個」問題は解消しな
かった**（スキャンは正常に完了し，`0 APs found`のまま）。これは
advisorの助言に基づく決定的な切り分けであり，**「クロック／
電源ドメインの有効化漏れ」という仮説クラス全体を実測で棄却した**
ことを意味する：MAC/PHYの静的レジスタ設定は既知良品と完全一致し
（前回セッション），今回追加確認したWIFIPWRクロックゲートも
正しく有効化されている状態で，それでもなお発見AP数は0のままである。

### 結論・次セッションへの申し送り（第2回更新）

- **クロック／電源ドメイン有効化漏れという仮説クラスは実測で
  棄却**。`clk_wifipwr_en`修正は正当化された実修正として維持する
  （`wifi_clock_enable_wrapper()`・`efuse_hal.c`×2・
  `esp_sleep_{pd,clock}_config`スタップ）が，これ単独では
  「AP 0個」を解消しない。
- **`HP_SYSTEM_MODEM_PERI_TIMEOUT_ADDR_REG`はラッチ式**という
  実装上の注意点が判明：この種のレジスタは，読む前に一度クリア
  してから再アサートの有無で切り分けないと，起動時の無関係な
  一度きりの事象を「継続的な問題」と誤認する。今後同様の診断を
  行う際の教訓として記録する。
- **残る有力仮説**は実質的に1つに収束した：MAC内部（blob非公開
  領域）のRX-arm/開始トリガのシーケンス自体が抜けている，または
  誤った順序で発行されている。これは静的レジスタ比較（今回
  MAC制御ブロック16KB・modem syscon 4KB・modem_lpcon・PCRいずれも
  参照実装と一致または正当化済みの差分のみ）では検出できない
  クラスの不具合であり，実機JTAGでの**単一命令ステップ実行**
  （`wifi_hw_start`のMAC内部レジスタアクセスを1命令ずつ追い，
  参照実装の同一関数と比較する）が必要になる。
- **セッションの総括**：本セッション複数回にわたり，有望に見えた
  手がかり（`g_log_level`ゲート，MACブロックのバイト一致，
  そして今回のMODEM_PERI_TIMEOUT）が，詳しく調べるといずれも
  「赤信号」または「見た目ほど決定的でない」ことが判明するという
  パターンが繰り返された。これは，現在のアプローチ（自分の
  ビルドを計装し，レジスタ・ログを比較して推論する）の限界に
  達しつつあることを示唆している。次の一手（blob内部の単一命令
  ステップ実行によるRX-armシーケンス比較）は難度・時間コストが
  大きく上がるため，**このセッションでの継続よりも，より強力な
  リソース（上位モデル・専任の長時間デバッグセッション等）への
  引き継ぎ，または一旦停止しての方針再検討が適切な時点**と判断
  する。
- 未着手のまま残した項目（優先度は低い）：`log_writev_wrapper`の
  `"%s"`＋一時バッファがASP3の遅延整形`syslog()`と衝突する疑い
  （診断ログを大量に流したときのみ顕在化する副作用で，既定動作
  には影響しない）／`esp_shim_coex_adapter_register()`経由と
  見られる`CLK_COEX_EN`・`CLK_I2C_MST_EN`の不要な有効化。

## NuttX参照実装の実機動作確認（追加調査）

ユーザーの提案により，同じesp-hal-3rdparty＋blob方式でC6 Wi-Fiを
実装している他RTOS（NuttX）を，このボードで実際にビルド・実機動作
させて比較参照とする調査を実施した。

### NuttXビルド・実機動作結果

- `apache/nuttx`（`master`，1コミットのみ浅くclone）と`apache/nuttx-apps`
  をスクラッチ領域（`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`，
  ASP3リポジトリ外）に取得。
- board config `esp32c6-devkitc:wifi`が既に用意されており，これに
  `CONFIG_ESPRESSIF_USBSERIAL=y`／`CONFIG_ESPRESSIF_UART0`無効化を
  重ねてコンソールをUSB Serial/JTAG経由に変更（このボードはUART0が
  未配線＝ASP3ポートと同じ制約のため）。
- ビルドで**このプロジェクトのシステム既定toolchain
  （`riscv64-unknown-elf-gcc`＝newlib非搭載のベアメタル専用ビルド）
  では`math.h`等が無くリンクエラー**になることが判明。ESP-IDF
  インストール同梱の`riscv32-esp-elf-gcc`
  （`/home/honda/tools/espressif/tools/riscv32-esp-elf/...`＝完全な
  newlib搭載）を`CROSSDEV=`で明示指定してビルドすることで解決。
  （ASP3ポート自体はnewlib非搭載を前提にhal_stub等で自己完結する
  設計のため，この制約はNuttXのビルド設定にのみ影響し，ASP3ポート
  の設計判断が誤っていたわけではない）
- `kconfig-tweak`（kconfig-frontends）がこの環境に無く`sudo`も
  使えなかったため，`--file --enable/--disable`相当のテキスト置換を
  行う最小限の代替スクリプトをその場で用意した（正式なkconfig木を
  解決するものではないが，今回必要な単純な有効/無効化には十分）。
- 実機（同一のC6ボード，同一アンテナ状態）に書込み・起動。
  NuttShell（NSH）が起動し，以下を実行：

```
nsh> ifconfig wlan0 up
nsh> wapi scan wlan0
bssid / frequency / signal level / encode / ssid
38:4f:f0:c0:91:36	2412	-95	0800	PS3-1757991
cc:1a:fa:c9:65:ca	2412	-91	0800	F660A-MM5T-G
10:66:82:09:f7:76	2452	-86	0800	ctc-g-21f920
76:00:7d:fc:e8:df	2437	-76	0800	ctc-2g-zkeb2h
76:00:7d:fc:e8:de	2437	-75	0800	ctc-zkeb2h
18:c2:bf:dc:59:f1	2412	-68	0800	<SSID-2G>
```

**NuttXは同じボードで実際に6件のAPを検出**（`<SSID-2G>`含む，
RSSI -68dBm）。これはos_adapter shim方式（ESP-IDFのネイティブ
ドライバスタックとは異なりASP3と同じ設計思想）でこのボード・
このシリコンrevisionでWi-Fiが動作しうることの動かぬ証拠であり，
「blob／シリコン側の問題」という仮説を排除する。ASP3ポートの
自作shimコード側に，まだ見つかっていない具体的な不具合が残って
いると確定的に言える。

### 簡易的なライブ比較（限定的）

NuttXでのスキャン実行中にJTAGで`INTMTX_STATUS0`（`0x60010134`）を
2回スナップショット読取りしたが，いずれもCPUがアイドルループ
（`esp_cpu_wait_for_intr`）で停止しており，読取り時点でも値は
`0x00000000`だった。これは「NuttXでも常に0」という意味ではなく，
**単発のJTAG halt挟みでは，MAC割込みが実際に立つ短い瞬間を捉え
損ねている可能性が高い**ことを示すに留まる（レベル型ステータスの
瞬間値は，割込みが処理された直後には既にクリアされているため）。
真に有効な比較には，ASP3側で既に確立している「ソフトウェアの
継続ポーリングループで全スキャン期間を監視する」手法を，NuttX側にも
同様に一時的な計装として追加する必要がある（今回は時間の制約で
未実施）。

### 残されたNuttXビルド（次の調査で再利用可能）

- ソース＋ビルド成果物：`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`
  （`nuttx/nuttx`＝ELF，`nuttx/nuttx.bin`＝書込み済みイメージ）
- 再ビルドコマンド例：
  ```bash
  cd /home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/nuttx
  export PATH=/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/kconfig-venv/bin:$PATH
  make -j$(nproc) CROSSDEV=/home/honda/tools/espressif/tools/riscv32-esp-elf/esp-14.2.0_20241119/riscv32-esp-elf/bin/riscv32-esp-elf-
  make flash CROSSDEV=... ESPTOOL_PORT=/dev/ttyACM0 ESPTOOL_BINDIR=./
  ```
- 現状ボードにはこのNuttXイメージが書き込まれたまま（ASP3イメージは
  上書きされている）。次回ASP3側を検証する際は`asp_flash.bin`を
  再書込みすること。

### 次の一手（推奨）

NuttXの`arch/risc-v/src/esp32c6/esp_wifi_adapter.c`の`wifi_hw_start`
呼出し経路に，ASP3の`esp_shim_log_write`のような一時計装を追加し，
スキャン実行中の`INTMTX_STATUS0`を継続ポーリングして，**MAC割込み
ソースが実際に一度でも立つか**を確定させる。もし立つなら，ASP3側
との唯一の違い（割込みハンドラ登録タイミング／MAC初期化呼出しの
抜け）を1行単位で追い込めるはずである。もし（意外にも）NuttX側でも
一度も立たない場合，むしろ「MAC割込みに頼らずポーリングでRXを
検出している」可能性が浮上し，調査の前提そのものを見直す必要が
生じる。

## 継続ポーリング計装による決定的比較（追加調査，2026-07-05）

コーディネータの指示により，上記「次の一手」を実施した：ASP3側
（`target_hrt_handler`のOR蓄積＋scan待ちループの`tslp_tsk`間隔を
500usへ短縮）とNuttX側（`esp_wifi_start_scan()`内に直接埋め込んだ
タイトなbusy-pollループ）の両方に，JTAG halt頼みではない**実行中の
継続計装**を追加し，同じ`INTMTX_STATUS0/1/2`（`0x60010134/138/13C`）
を比較した。

### ASP3側：高密度サンプリングでも変わらず0

`target_hrt_handler`（既存のSYSTIMER割込み）でOR蓄積する既存手法を
再度用いたが，scan待ちループの`tslp_tsk`間隔を1秒→500usに短縮する
ことでカーネルのhrtアラームの実質発火頻度を大幅に引き上げた。

結果：**約2.4秒のスキャン期間中に4702回サンプリング**（従来の約18回
から大幅に高密度化）してもなお，`status0_or=0x00000000`・
`status1_or=0x00000000`・`status2_or=0x00000000`のまま。これは
下記2件の修正（WIFIPWRクロック・割込みLEVEL型設定）を適用した
**後**の結果であり，これらの修正だけでは「MAC割込みソースが一度も
アサートされない」という状況が変わらないことを実測で確認した。

### NuttX側：ビルドを修正し，MAC割込みソースが実際に立つことを確認

NuttXの`arch/risc-v/src/common/espressif/esp_wifi_utils.c`の
`esp_wifi_start_scan()`（`wapi scan`が最終的に呼ぶ関数．
`esp_wifi_scan_start()`のNuttX側呼出し元）に，同じ3レジスタを
OR蓄積するbusy-pollループを直接埋め込んだ。

**罠**：この計装は最初，`wlerr()`マクロ（NuttXの`nuttx/debug.h`が
提供する診断ログマクロ）で出力していたが，**このビルドconfigは
`CONFIG_DEBUG_FEATURES`が無効**であり，`wlerr`/`wlinfo`等は
コンパイル時に完全に消える（`_none`へ展開される）ため，何度実行
しても出力が一切現れなかった。`printf()`＋`fflush(stdout)`に
差し替えて解決した（NSHのコンソール出力は`CONFIG_DEBUG_FEATURES`
と無関係のため確実に見える）。もう一つの罠：busy-pollを4000万回の
単一ループで実装したところ（推定数秒間ノーイールド）出力が一切
現れなくなった（タスク実行時間監視系のwatchdogに引っかかって
リブートしていた可能性が高い）。400チャンク×2万回反復＋チャンク間
`usleep(1000)`という，短い区切りで頻繁にイールドする構成に変更して
解決した。

結果（`wapi scan wlan0`実行時）：

```
DIAG intmtx status0_or=0x00000001 status1_or=0x0a000000 status2_or=0x00000000 iters=8000000
```

**`status0_or`のbit0＝`ETS_WIFI_MAC_INTR_SOURCE`（WiFi MAC割込み
ソースそのもの）が実際にアサートされている**（`status1_or`の
bit25/27はNuttX自身のタイマ系＝無関係のノイズ）。

### 結論

**この同一シリコン・同一blobにおいて，WiFi MAC割込みソースは
「原理的に一度も立たない」わけではなく，NuttXの実装下では実際に
立つ**。ASP3側は，静的レジスタ設定（MAC制御ブロック16KB・modem
syscon 4KB・今回のWIFIPWRクロックドメイン・割込みLEVEL型設定）を
すべて既知良品と一致させた後でも，高密度サンプリング（4702回／
約2.4秒）で一度もアサートを検出できていない。したがって：

- ハードウェア／blob内部の問題という仮説は完全に排除される。
- 静的な設定不備（レジスタ値そのものの誤り）という仮説も，主要な
  候補はほぼ排除された（クロックドメイン・割込み型の2件は実際に
  見つかり修正したが，どちらも単独では解消せず）。
- 残る仮説は，**osi関数のいずれかの実装内容**（フィールド自体は
  ASP3・NuttX間で完全に一致することを確認済み──`grep`で全
  `wifi_osi_funcs_t`フィールドを比較し，欠落なし）に，まだ見つ
  かっていない具体的な差異があるという一点に収束する。

### フィールド構造の比較（差異なし）

`asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`と
NuttXの`arch/risc-v/src/esp32c6/esp_wifi_adapter.c`から
`._xxx = yyy_wrapper`形式の行をすべて抽出して比較した結果，
**フィールド名の集合は完全に一致**（ASP3側にのみ存在する
`._coex_condition_set`・`._coex_schm_get_phase_by_idx`・
`._esp_timer_get_time`はcoex/timer関連でRX経路とは無関係）。
`phy_enable_wrapper`／`phy_disable_wrapper`の呼出し順序
（`esp_phy_enable`→`phy_wifi_enable_set`，逆順で無効化）も
バイト単位で同一のロジック。

### 次の一手（更新・最有力仮説）

TXおよびスキャンステートマシン自体は正常に機能している可能性が
高い（アクティブスキャンの所要時間が約2.4秒と，実機での正常な
チャネル毎dwell timeの合計と整合的であり，固定タイムアウトへ
すぐ落ちるような挙動ではない）。一方でRX側だけが機能していない
とすれば，**MACのRXバッファ／ディスクリプタプールの提供**
（`esf_buf`まわり，`libpp.a`の`esf_buf.o`が扱う領域）が最有力な
残り仮説である：

- ESP32のWiFi MACは内部DMAでRXバッファへ受信フレームを書き込む
  設計であり，バッファが無い（またはDMA非対応領域にある）場合，
  MACはフレームを黙って破棄し，**割込み自体を一度も発生させない**
  という設計は十分にありうる（「バッファが無ければ受信通知しない」
  というハードウェア的な自己防御は一般的）。
- 本セッションで`esp_shim_malloc`の失敗ログ・
  `esp_shim_queue_create`の"queue pool exhausted"ログが一度も
  出ていないことは確認済みだが，これは「確保が失敗していない」
  ことの確認であって，「確保されたメモリがMACのDMAエンジンから
  見て正しい種類・アライメントか」は未確認。
- 次セッションでの具体的な確認事項：(1) `_malloc`/`_zalloc`系
  osi関数が要求するサイズ・呼出し回数をログし，静的ヒープ
  （`esp_shim.c`の`heap_area[]`，192KB）からの割当てが，esf_buf
  初期化で期待される個数（既定：static rx buffer num・dynamic rx
  buffer num等，`WIFI_INIT_CONFIG_DEFAULT()`のKconfig既定値）
  だけ実際に成功しているかをカウントする，(2) ESP32-C6の
  DMA対応SRAM領域（`MALLOC_CAP_DMA`相当）の実アドレス範囲を確認し，
  `esp_shim`の`heap_area`静的配列がその範囲内に収まっているかを
  リンカマップで確認する。

### 保存物・再現用コマンド（このセッションで追加）

- ASP3側の計装（`target_hrt_handler`のOR蓄積＋`tslp_tsk(500)`）は
  診断専用のため，本コミットには含めず元に戻した（再現する場合は
  本セクションの説明どおりに一時追加すること）。
- NuttX側の計装は`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`
  （ASP3リポジトリ外のスクラッチ領域）に残っている：
  `arch/risc-v/src/common/espressif/esp_wifi_utils.c`の
  `esp_wifi_start_scan()`内，`ret = esp_wifi_scan_start(...)`の
  直後に busy-poll ブロックを追加済み。再ビルド・書込みコマンドは
  上記「残されたNuttXビルド」節と同一（`printf`使用のため
  `CONFIG_DEBUG_FEATURES`の状態に関わらず出力が見える）。
- 現状ボードにはNuttXイメージが書き込まれたまま。次回ASP3側を
  検証する際は`asp_flash.bin`を再書込みすること。

### 本セッションで実装・維持した実修正（2件）

いずれも実機JTAGレジスタ比較で「既知良品と不一致→修正後に一致」を
確認した，独立に正当化できる実修正（ただしどちらも単独では
「AP 0個」を解消しない）：

1. **WIFIPWRクロックドメイン有効化**
   （`wifi_clock_enable_wrapper()`に`modem_clock_select_lp_clock_
   source(PERIPH_WIFI_MODULE, MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0)`
   を追加）。`modem_lpcon.CLK_CONF_REG`（`0x600af018`）のbit0
   （`clk_wifipwr_en`）が修正前0・修正後1（参照実装も1）。
2. **WiFi MAC割込み線のLEVEL型設定**
   （`set_intr_wrapper()`に`PLIC_MXINT_TYPE_REG`
   （`0x20001004`）の該当ビットクリアを追加）。参照実装
   （`hal/components/esp_wifi/esp32c6/esp_adapter.c`・NuttX双方）が
   `esprv_int_set_type(intr_num, INTR_TYPE_LEVEL)`を呼ぶのに対し，
   ASP3側はこの呼出しを欠いていた。修正後`TYPE_REG`は全ビット0
   （全線LEVEL）で確認済み。

## RXバッファ／キャッシュ整合性仮説の検証（追加調査，2026-07-05）

コーディネータの指示により，「MACのRXバッファ／ディスクリプタ供給
がキャッシュ非対応領域／DMA対応領域を要求している」という仮説を
検証した。

### 仮説1：`_calloc_internal`/`_zalloc_internal`/`_realloc_internal`がDMA属性を要求

参照実装（`hal/components/esp_wifi/esp32c6/esp_adapter.c`）の該当
関数を確認したところ：

```c
return heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
return heap_caps_calloc(n, size, MALLOC_CAP_8BIT | MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
```

確かに`MALLOC_CAP_DMA`付きの専用アロケータ（ESP-IDFの
`heap_caps`サブシステム）を使っている。ASP3側はこれらのフィールド
を単純に`esp_shim_malloc`/`esp_shim_calloc`（静的配列上の汎用
アロケータ，DMA属性の区別なし）へマッピングしているのみで，一見
仮説と合致するように見えた。

**しかし，同じフィールドに対するNuttXの実装を確認したところ，
NuttX（実際に動作することが確認済み）は`kmm_realloc`/`kmm_calloc`/
`kmm_zalloc`──**NuttXの汎用カーネルヒープをそのまま使っており，
DMA属性・特殊な確保先の区別は一切ない**。`_wifi_malloc`/
`_wifi_calloc`/`_wifi_zalloc`（もう一系統のバッファ確保系フィール
ド）に至ってはNuttX側は単なる`malloc()`/`zalloc()`（同じくカーネル
ヒープ）である。

**結論**：DMA属性を要求する専用アロケータが必須ではないことを，
NuttXの動作実績が直接反証している。ESP-IDFの`heap_caps_calloc(...,
MALLOC_CAP_DMA...)`はESP-IDF自身の複雑なメモリ配置（複数ヒープ
リージョン）に起因する設計選択であり，「MACがDMA属性メモリ以外を
受理しない」という仮説とは別の話である。この仮説は棄却する。

### 仮説2：ESP32-C6のキャッシュがSRAM/DRAMを覆っており，コヒーレンシ問題がある

`hal/components/soc/esp32c6/include/soc/ext_mem_defs.h`の
`DCACHE_MMU_SIZE`等，キャッシュ／MMU関連定義はすべて
**`ext_mem`（外部メモリ＝SPI Flash／PSRAM）専用**であることを
ヘッダ名・定義群から確認した。ASP3のリンカスクリプト
（`asp3/target/esp32c6_espidf/esp32c6.ld`）が使うRAM領域
（`RAM(rwx) : ORIGIN = 0x40800000, LENGTH = 448k`）は内蔵SRAM
（IRAM/DRAM共用の単一物理領域）への直接アドレッシングであり，
ESP32系の一般的な設計（キャッシュ／MMUはFlash XIPアクセス専用，
内蔵SRAMアクセスはキャッシュを経由しない直結）と整合する。

また，ASP3側の起動コード（`asp3/asp3_core/arch/riscv_gcc/esp32c6/`
以下）にはキャッシュ・MMU関連の初期化コードが一切無く（Direct Boot
のためROM既定のままに依存），NuttX側の該当コードとの「有効化有無の
差」も存在しない（どちらも触っていない）。

**結論**：本ポートの静的ヒープ（`esp_shim.c`の`heap_area[]`）が
配置されるSRAM領域は，そもそもキャッシュの対象外であり，DMAとの
キャッシュコヒーレンシ問題は原理的に発生しえない。この仮説も棄却
する。

### 総括

コーディネータ提案のキャッシュ／DMA仮説は，具体的な検証（NuttXの
実装比較・ESP32-C6のキャッシュ対象範囲の確認）の結果，**いずれも
支持されなかった**。これで以下の仮説クラスがすべて実測で棄却
された：

- 静的レジスタ設定の誤り（MAC制御ブロック・modem syscon・
  クロックドメイン・割込み型）
- osi関数フィールドの欠落（構造的完全一致を確認済み）
- ハードウェア／blob内部の欠陥（NuttXが同一基板で動作実績あり）
- DMA対応メモリ／キャッシュコヒーレンシ要求（NuttXは無条件の
  汎用ヒープで動作しており，ASP3側のSRAM領域もそもそも非
  キャッシュ対象）

### このセッションでの結論（引き継ぎ）

これだけの仮説クラスを実測で潰してもなお，NuttXでは実際にアサート
される（`INTMTX_STATUS0`bit0=1）WiFi MAC割込みソースが，ASP3側では
高密度連続ポーリング（4702回／約2.4秒）で一度も観測されない。
残る可能性は，`wifi_osi_funcs_t`のいずれかの関数の**実装内容の
細部**（フィールド自体は完全一致，しかし個々の実装ロジックの中に
未発見の差異がある）に絞られるが，本セッションで比較検討した
主要な候補（クロック・割込み型・バッファ確保・phy_enable順序）は
いずれも一致または修正済みであり，残りの候補を当てずっぽうで
洗うフェーズに入っている。

このセッション（複数パスにわたる）で，一見有望に見えた手がかりが
詳しく調べると悉く「赤信号」（stale artifact・NuttXでも同じ設計・
そもそも該当しない）と判明するパターンが繰り返された。これは
「自分のビルドを計装し，レジスタ・ログ・実装を比較して推論する」
という現在のアプローチが，実質的にやり尽くされた状態にあることを
示している。次に必要なのは，おそらく**blob内部（`wifi_hw_start`
またはその等価物）の実際の命令列をNuttX・ASP3の両方で単一命令
ステップ実行し，具体的にどの命令の後でNuttX側だけがMAC割込み
ソースをアサートするか**を特定する作業であり，これは今回までの
「静的比較」パスとは質的に異なる，時間のかかる実機JTAG逐次デバッグ
である。

**コーディネータの基準（`docs/wifi-shim-c6.md`の当該節参照）に
従い，本セッションでの継続よりも，別リソース（上位モデル・専任の
長時間デバッグセッション，または新しい視点）への引き継ぎを推奨
する。** ここまでに排除した仮説・確立した事実（NuttX実機動作確認・
継続ポーリングでのMAC割込み実測比較・osi関数構造比較・レジスタ
2件の実修正）はすべて本ドキュメントに記録済みであり，次のセッ
ションは「振り出しに戻る」のではなく，単一命令ステップ実行という
次の具体的な一手から再開できる状態にある。

## 他AI（セカンドオピニオン）による再分析と反証：TX側も無音（追加調査，2026-07-05）

上記の引き継ぎ文書一式を別のAIに読ませてセカンドオピニオンを求めた
ところ，本ドキュメントの推論に対する重要な反論が得られた：

> 「スキャンが正常な約2.4秒で完走している→TX側は機能している」という
> 切り分けは誤りの可能性が高い。チャネル滞在時間はASP3のOSタイマ駆動
> であり，実際のTX/RX成否には依存しない。PHY/RFが完全に死んでいても，
> タイマだけで全チャネルを「巡回」しタイムアウト経由で`SCAN_DONE`に
> 到達できる。さらに，アクティブスキャンであればprobe request送信の
> たびに**TX完了割込み**（RXと同じWiFi MAC割込みソース＝
> `INTMTX_STATUS0`のbit0）が立つはずである。`status0_or`が終始0という
> 事実は，**TX完了割込みも一度も出ていない**ことを意味する。

この指摘は，本ドキュメントが確立した既存の事実（`scan_done: ...`
正常完了診断ログが一度も出ない）と矛盾せず，むしろ「MACが一切受信
していない」という従来の解釈を「**送受信ともに一切機能していない**」
という，より広い（しかしより単純な単一原因で説明できる）解釈へ
差し替えるものである。

新しい最有力仮説として，**`regi2c`**（ESP32系のRFシンセサイザ／
PA・LNA・バイアス回路を制御する，メモリマップされていない内部
アナログI2C的バス）が，Direct Bootで初期化されないまま残っている
可能性が提示された。この経路が死んでいれば：

- `chip_v7_set_chan`等のチャネル設定・RF起動がサイレントにno-op化
  （エラーは返らない）
- MAC・BB（ベースバンド）のデジタル側レジスタは正常値のまま保たれる
  →これまでのJTAGでの静的レジスタ比較（MAC制御ブロック16KB・
  modem syscon 4KB完全一致）と矛盾しない（比較対象がそもそも
  regi2c配下のアナログブロックを含んでいないため）
- TXもRXも電波レベルで成立せず，MAC割込みは一切発生しない
- スキャンはタイマで完走し，AP 0件

という，これまでの全観測事実を単一原因で説明できる。

### 実験A：sniffer実機観測（決定的・実施済み）

上記仮説の検証として，ESP32-C3実機（本プロジェクトの既存ボード．
`/dev/ttyACM1`）をESP-IDF標準の`esp_wifi_set_promiscuous()`APIで
プロミスキャスモード化し，チャネル1に固定して待受けさせ，C6実機
（`/dev/ttyACM0`）のscanを**チャネル1固定**（`wifi_scan_config_t.
channel=1`．診断専用の一時的な変更．検証後に元へ戻し済み＝
`apps/wifi_scan/wifi_scan.c`はコミット差分なし）に変更して実行し，
C6のMACアドレス（OUI `58:e6:c5`）を送信元とするフレームが実際に
電波として観測されるかを直接確認した。

**アプリ**：`/home/honda/.claude/jobs/494f98a3/tmp/c3_sniffer2/`
（ASP3リポジトリ外のスクラッチ領域．素のESP-IDF v5.5でビルド。
`esp_wifi_set_promiscuous_rx_cb()`で全フレームを受け，802.11
ヘッダのaddr2（オフセット10〜15＝送信元MAC）のOUIが`58:e6:c5`に
一致するものだけを検出ログに出力，かつ全フレーム数もハートビートで
常時出力）。

**結果**：

```
I (78515) sniffer: heartbeat: total_frames=7683 target_frames=0
I (80515) sniffer: heartbeat: total_frames=7889 target_frames=0
... (以下C6のscan実行ウィンドウを挟んで継続) ...
I (116515) sniffer: heartbeat: total_frames=11353 target_frames=0
```

`total_frames`（周辺の実在AP等，任意の送信元からの全フレーム数）は
2秒ごとに180〜220件のペースで着実に増加しており，**sniffer自体は
チャネル1上で正常にフレームを受信し続けていた**（sniffer側の設定・
動作に問題がないことの確認）。この期間中にC6実機の書込み・リセット
・`esp_wifi_init`〜`esp_wifi_scan_start`〜約2.4秒のスキャン完了までの
一連の流れを実行したが，**`target_frames`は終始0のまま**——C6の
MACアドレスを送信元とするフレームは，物理的に10cm離れた（アンテナは
C6側は内部アンテナのみ，外部アンテナ未接続）distanceで，一度も
観測されなかった。

### 結論

**この結果は「TX完了割込みが一度も出ていない」という数値的事実
（`INTMTX_STATUS0`終始0）を，独立した手法（電波の直接観測）で
裏付けるものである。C6実機は，スキャン試行中に一切何もオンエア
していない。** これにより：

- 「MACが受信できていない（RXバッファ供給の不備等）」という従来の
  最有力仮説は，**RX限定の問題という前提が崩れたため後退**する
  （esf_buf仮説は，TXが生きているという誤った前提の上に立って
  いた）。
- 「TX・RXとも電波レベルで完全に沈黙している」という，より広範で
  単純な原因（PHY/RFアナログ経路全体の未初期化．regi2c仮説が最有力）
  への転換が支持される。

### 申し送り：次の一手（更新）

セカンドオピニオンが提案した，本実験に続く具体的な次のステップ
（未実施．次セッションへの申し送り）：

- **実験B（regi2c生存確認）**：ROM関数（`rom_i2c_readReg`相当．
  正確な名称はesp-hal-3rdpartyのregi2c関連ヘッダ
  ［`regi2c_*.h`．C6版のパスは未確認］で確認すること）で既知のRF
  レジスタ（BIASブロック等）を読み，NuttX動作時（既に実機動作
  実績あり）の同レジスタ値とJTAGで比較する。
- **実験C（未比較レジスタブロックの全域diff）**：これまで
  `modem_lpcon`は`CLK_CONF_REG`（`0x600af018`）の1ビットのみ比較
  済みで，ブロック全体は未比較。特に`clk_i2c_mst_en`相当のregi2c
  マスタクロック許可ビットを含む全域を，NuttX動作中（アイドル時
  ではなくスキャン実行中）vs ASP3側で比較する。`PCR`
  （`0x60096000`〜）・`PMU`（`0x600B0000`〜，RF/アナログ電源
  ドメイン．Direct Bootは`pmu_init()`も呼んでいない可能性が高い）
  も同様に未比較。
- **実験D（Direct Boot初期化漏れの網羅的差分表）**：ESP-IDFの
  `start_cpu0`〜`esp_perip_clk_init`/`pmu_init`/`rtc_clk_init`/
  `esp_phy_*`という起動時初期化列と，本ポートのDirect Bootパスとの
  差分を機械的に洗い出す。`clk_wifipwr_en`と同型の初期化漏れが，
  他にも複数残っている前提で潰す。
- 単一命令ステップ実行（従来の申し送り）は，これらのregi2c／
  電源ドメイン系の仮説を先に検証してからの，さらに後の手段として
  位置づけ直す。

sniffer実験の再現用ビルドは`/home/honda/.claude/jobs/494f98a3/tmp/
c3_sniffer2/`に保存済み（C3実機・`/dev/ttyACM1`向け．チャネル1固定・
OUI `58:e6:c5`検出）。実機は現状C6側にASP3の`wifi_scan`（チャネル
固定を戻した通常版）が書き込まれた状態。

## 実施7：実験B（regi2c生存確認）— 新規バグ発見だが「AP 0個」は未解決

セカンドオピニオンの実験B〜Dに沿って`esp-hal-3rdparty`のregi2c関連
実ソースを読み，実機で直接検証した．

### 発見1：`clk_i2c_mst_en`未有効化（新規バグ・修正済み）

`hal/components/hal/esp32c6/include/hal/regi2c_ctrl_ll.h`より，regi2c
（RFシンセサイザ/PA/LNA/バイアスの内部アナログ較正バス）の前提クロッ
クは`MODEM_LPCON.clk_conf.clk_i2c_mst_en`（`modem_lpcon.CLK_CONF_REG`
＝`0x600af018`のbit2．`clk_wifipwr_en`＝bit0・`clk_coex_en`＝bit1の
兄弟ビット）．このビットは`hal/components/bootloader_support/src/
esp32c6/bootloader_esp32c6.c`の`bootloader_hardware_init()`が

```c
_regi2c_ctrl_ll_master_enable_clock(true); // keep ana i2c mst clock always enabled in bootloader
regi2c_ctrl_ll_master_configure_clock();
```

として**第2段ブートローダ内で恒久的に有効化**する設計（`esp_hw_
support/regi2c_ctrl.c`の参照カウント式`ANALOG_CLOCK_ENABLE/DISABLE`
はSAR_ADC等の動的経路用で，PHYブロブ自身は有効化済み前提で
`esp_rom_regi2c_read/write`を直接叩く）．Direct Bootではこのブート
ローダ自体が一切実行されないため，このビットは常に0（リセット既定
値）のまま．

`asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`の
`wifi_clock_enable_wrapper()`に，`bootloader_hardware_init()`と同じ
2行を追加して修正（`_regi2c_ctrl_ll_master_enable_clock(true)` +
`regi2c_ctrl_ll_master_configure_clock()`）．JTAGで実機確認：
`0x600af018`＝修正前0x3（bit0,1のみ）→修正後0x7（bit2も含め全て1）．
`0x600af010`（`I2C_MST_CLK_CONF_REG`）も期待通り0x1（160MHz選択）．

なお，`esp_rom_regi2c_read/write`のROM実装（`regi2c_enable_block()`
内）には`assert(regi2c_ctrl_ll_master_is_clock_enabled())`があるが，
本ビルドでは`assert(x)`が`asp3/target/esp32c3_espidf/hal_stub/
include/assert.h`により`((void)0)`へ無効化されているため，このビッ
トが0のままでもクラッシュせず無言で通過していた（今回の一連の調査
でクラッシュが一度も観測されなかった理由の説明にもなる）．

### 発見2：regi2cバス自体は生きている・BBPLLは正常にロックする

修正適用後，`apps/wifi_scan/wifi_scan.c`に一時診断コードを追加し
（`esp_wifi_start()`直後，PHY較正完了後の時点で），BBPLLブロック
（`I2C_BBPLL`=0x66）を実際にregi2c越しに読み出した：

```
wifi_scan: DIAG bbpll or_lock=1 cal_end=1 cal_ovf=0 reg9=0x96
```

`OR_LOCK=1`（PLLロック済み）・`OR_CAL_END=1`（較正完了）・
`OR_CAL_OVF=0`（較正オーバーフローなし）・reg9=0x96（プレースホルダ
値ではない具体的な較正結果）．これは**regi2cバスが実際に機能し，
BBPLL較正が正常に完了・ロックしていることを示す**（ROM関数
`esp_rom_regi2c_read`/`read_mask`はC6ではROMベクタ経由で解決されず，
診断のため`hal/components/esp_rom/patches/esp_rom_hp_regi2c_esp32c6.c`
を一時的にビルドへ追加して直接呼んだ．通常ビルドでWi-Fiブロブ自身は
この外部シンボルを参照しないため未リンクのままでも支障なし＝ブロブ
は自前の内部実装を持つと推定される）。

`modem_lpcon`全域（`0x600af000`〜`0x600af04c`）もJTAGで読み出したが，
明らかな異常値（全0/全1のような放置パターン）は見られなかった．

### 結論：セカンドオピニオンの「regi2c/RFアナログ経路が死んでいる」
仮説は，本ビットの修正後は**成立しない**

修正後も`wifi_scan: 0 APs found (err=0)`は完全に同一（挙動の変化
なし）．BBPLLが正常にロックしている以上，RFフロントエンドへの
アナログ較正パスは少なくとも部分的に機能しており，「regi2cが完全に
死んでいるためTX/RXとも一切動かない」という仮説の前提が崩れる．
`PMU_RF_PWC_REG`（`0x600B0154`）もJTAGで確認：`0xfc000000`
（bit26〜31全て1＝XPD_TXRF_I2C/XPD_RFRX_PBUS/XPD_CKGEN_I2C/
XPD_PLL_I2C含め全RFアナログ電源ドメインが電源投入済み．ヘッダ上の
リセット既定値は一部bit=0だが，これはPMUの電源状態ステートマシン
（HP_ACTIVE設定）が適用した後の実測値であり，Direct Bootでも
「電源投入」自体は最初から成立している）．PCR
（`0x60096000`〜）は時間の都合で全域diffは未実施。

**新規バグ（`clk_i2c_mst_en`）は本物であり修正済みだが，「AP 0個」
問題の直接原因ではなかった**．sniffer実験（実施6）が示した「TXも
死んでいる」という事実自体は変わらないため，原因はregi2c/PLL較正
より後段（もしくは全く別系統）にあることになる．次の手がかり：

- BBPLLがロックしていてもMAC/PHY側で実際にTXを「キック」する経路
  （`wifi_hw_start`相当，ROMまたはブロブ内部）が別の未有効化ビット
  に依存している可能性（PCR/PMUの残り未比較領域，あるいはMAC自体の
  イネーブル系統）。
- 単一命令ステップ実行によるNuttX/ASP3比較（当初案）に立ち返る
  べき局面に近づいている。

diffはコミット済み（`asp3/target/esp32c6_espidf/wifi/
esp_wifi_adapter.c`のみ．診断用コード・cmake追加は全てrevert済み）。
実機は現状ASP3の`wifi_scan`（`clk_i2c_mst_en`修正込み・診断コード
なしの通常版）が書き込まれた状態。

## 実施8：セカンドオピニオン再検証 — 「WIFI_BB/FEクロックゲート」仮説も実測で反証

セカンドオピニオンの新仮説：「これまでの3件の実バグ（`wifi_reset_
mac_wrapper`の到達不能コードtrap・割込みLEVEL型欠如・`clk_wifipwr_en`
＋`clk_i2c_mst_en`欠如）は全て同型パターン＝Direct Bootが飛ばす
ブートローダ側初期化を，発見の都度1ビットずつ後追いで手動再現して
いるに過ぎない。実際のESP-IDF/NuttXは`modem_clock_module_enable
(PERIPH_WIFI_MODULE)`／`(PERIPH_PHY_MODULE)`という参照カウント式の
一括イネーブルを呼び，`modem_syscon`側のWIFI_BB（ベースバンド）・
WIFI_MAC・WIFI_APB・FE（RFフロントエンド）系クロックゲートを
まとめて有効化している。手動ビット単位の対応はこの種の兄弟ビット
見落としに構造的に弱く，FE/BBクロックが未有効化なら『デジタル的
にはMAC/PLL/電源とも正常だがTX/RXとも電波が一切出ない』という
現症状と完全に一致する」という指摘（優先度1）を検証した。

### 参照実装の確認：`wifi_module_enable()`は既に正しい経路を使っている

`hal/components/esp_wifi/esp32c6/esp_adapter.c`（実ESP-IDF本家の
C6版アダプタ．NuttXではなくESP-IDF自身のリファレンス）の
`wifi_clock_enable_wrapper()`は`wifi_module_enable();`の1行のみ。
`wifi_module_enable()`（`esp_hw_support/periph_ctrl.c`）は
`#if SOC_MODEM_CLOCK_IS_INDEPENDENT`（C6は`soc_caps.h`で1と定義済み）
の分岐で`modem_clock_module_enable(PERIPH_WIFI_MODULE)`を直接呼ぶ
実装であり，ASP3は`periph_ctrl.c`を実ソースのまま採用・
`wifi_clock_enable_wrapper()`から`wifi_module_enable()`を既に呼んで
いる（実施6以前から変更なし）。FE系クロック（`PERIPH_PHY_MODULE`の
依存＝`modem_clock_hal_enable_modem_common_fe_clock`/
`_private_fe_clock`）も，ASP3が実ソースのまま採用している
`esp_phy/src/phy_init.c`の`esp_phy_enable()`→
`esp_phy_common_clock_enable()`→`wifi_bt_common_module_enable()`
（`periph_ctrl.c`．同じく`modem_clock_module_enable(PERIPH_PHY_
MODULE)`を呼ぶ）という経路で，既存コードから変更なしに到達する
はずという仮説を立てた。

### 実機JTAG確認：WIFI_BB／WIFI_MAC／WIFI_APB／FE系は全て有効化済み

修正なしで（`clk_i2c_mst_en`修正込み・現行コードのまま）スキャンを
実行し，直後にJTAGで`modem_syscon.CLK_CONF1_REG`
（`0x600a9814`）を読み出した：

```
0x600a9814 = 0x0001e7ff
```

ビット単位で全て確認：`clk_wifibb_{22m,40m,44m,80m,40x,80x,40x1,
80x1,160x1}_en`（bit0〜8）＝**全て1**・`clk_wifimac_en`（bit9）＝
**1**・`clk_wifi_apb_en`（bit10）＝**1**・`clk_fe_80m_en`（bit13）＝
**1**・`clk_fe_160m_en`（bit14）＝**1**・`clk_fe_cal_160m_en`
（bit15）＝**1**・`clk_fe_apb_en`（bit16）＝**1**。すなわち
`modem_clock_wifi_mac_configure`/`_wifi_bb_configure`/
`_wifi_apb_configure`（`WIFI_CLOCK_DEPS`）と
`modem_clock_hal_enable_modem_common_fe_clock`/`_private_fe_clock`
（`PHY_CLOCK_DEPS`）が要求するビットが**1つも欠けることなく既に
全て有効**であることを実機で直接確認した（bit11/12/17-23は
FE_20M/FE_40M/BT_APB/BT_EN/WIFIBB_480M/FE_480M/FE_ANAMODE_*＝
BT/ZigBee専用またはWi-Fi通常動作に不要なビットで，未セットで
正常）。

### 結論：「WIFI_BB/FEクロックゲート未有効化」仮説も実測で反証される

セカンドオピニオンの優先度1の指摘（構造的パターンとしては正しい
指摘であり，今回の調査姿勢の見直しとしても有益）だが，**本件の
具体的な原因ではなかった**。ASP3は既に実ESP-IDFの`periph_ctrl.c`/
`phy_init.c`を改変なしで採用しているため，`modem_clock_module_
enable()`の一括経路は既存コードのままで正しく機能しており，
新たに置き換える修正の余地がない（＝優先度1の作業自体は「確認
した結果，既に正しかった」という形で完了）。

これでクロック/電源系統（modem_lpcon全域・modem_syscon
CLK_CONF1全域・PMU RF電源ドメイン・BBPLL較正＝実施7）は
ほぼ全域にわたって実機で「デジタル的には正常」と確認済みとなり，
デジタル設定レベルの見落としという仮説群は実質的に消尽した。

残る有力な次の手は，優先度3（NuttXとASP3で共通のブロブに対する
シンボルレベルのハードウェアブレークポイント段階的絞り込み：
`chip_v7_set_chan`・`esf_buf_setup`・`hal_init`等のブロブ内シンボル
をブレークポイント候補とし，ASP3が到達しない/到達しても違う経路を
取る最初の関数を特定してから，その関数内でのみ命令単位ステップに
進む）。優先度2（アクティブスキャン中のレジスタ比較）はクロック
イネーブル系レジスタが静的設定である以上，アイドル時比較で実質
同等の結論が得られている（`WIFI_PS_NONE`設定によりスキャン中の
省電力遷移も発生しない設計のため）。

構造的提案（コーディネータより）：`wifi_clock_enable_wrapper()`の
手動ビット操作（`clk_wifipwr_en`/`clk_i2c_mst_en`）を，将来の
チップ移植・ブロブ更新に備えて`modem_clock_module_enable()`系の
直接呼び出しへ統一することは，今回は必須ではないため未実施
（次の機会の課題として記録のみ）。今回コード変更なし（診断のみ）．

## 実施9：セカンドオピニオン第3ラウンド — coex（3者調停）no-op化を試したが「AP 0個」は不変

「デジタル設定は全て正常なのにTX/RXとも電波皆無・MAC割込みも皆無
（TX完了・エラー割込みすら1つも立たない）」という状況は，ハード
ウェア誤設定というより「ブロブ自身がTX submit自体を自発的に見送っ
ている」ことを示唆する，という再整理のもとで2件のチェックを実施．

### Check 2（先に実施・即断）：`env_is_chip`は一致

`env_is_chip_wrapper()`（wifi_osi）・`coex_env_is_chip_wrapper()`
（coex osi）とも，ASP3は`true`固定．リファレンス（`hal/components/
esp_coex/esp32c6/esp_coex_adapter.c`他，全チップ共通）も
`#ifdef CONFIG_IDF_ENV_FPGA`未定義時は`true`．一致（問題なし）．

### Check 1（coex初期化）：NuttXとASP3で設計方針が違うことが判明

`asp3/target/esp32c6_espidf/wifi/esp_wifi_adapter.c`の`_coex_*`系
（`wifi_osi_funcs_t`）は，C3の設計をそのまま踏襲し`libcoexist.a`の
実関数（`coex_init`/`coex_wifi_request`等）へ無条件に素通しする
実装だった．

NuttX（`nuttx/arch/risc-v/src/esp32c6/esp_wifi_adapter.c`）を確認
すると，`_coex_*`系ラッパーは全て`#if CONFIG_SW_COEXIST_ENABLE ||
CONFIG_EXTERNAL_COEX_ENABLE`でガードされており，**両方未定義
（BT/802.15.4非併用のWi-Fi単独構成）の場合は`coex_init`等の実体を
一切呼ばず，即座に成功値（0固定，`coex_schm_flexible_period_get`
のみ1固定）を返すno-op**になる．本ポートで実機確認したNuttXの
`.config`を確認したところ，実際に両マクロとも未定義（＝no-op構成）
のまま6AP検出に成功していた．

ASP3のC3版は無条件の実passthroughのままWi-Fi動作実績あり（C3は
Wi-Fi/BT2者調停のみでBT非併用時は素通しでも実質no-opに近い挙動に
なる可能性がある）．C6は3者調停（Wi-Fi/BLE/802.15.4）のPTIベース
アービタで前提が異なるため，「無条件passthroughのままだと，Wi-Fi
単独構成でもアービタが未知の初期化待ちになりRFグラントを永遠に
返さない」という仮説を立て，NuttXのno-op構成に合わせて`_coex_*`
全域を差し替え・実機検証した．

### 検証結果：no-op化しても`wifi_scan: 0 APs found (err=0)`は不変

`coex_init_wrapper`〜`coex_schm_flexible_period_get_wrapper`まで
NuttXの`#else`分岐と同じ実装（0固定・void no-op・`curr_phase_get`
のみNULL・`flexible_period_get`のみ1固定）に置き換え，ビルド
（flashサイズ533488→528448Bへ縮小＝libcoexist.a側コードが実際に
リンクされなくなったことを裏付け）・実機再テストした．結果は
差し替え前と1バイトも違わず完全に同一：

```
wifi_scan: esp_wifi_init -> 0
...
wifi_scan: esp_wifi_start -> 0
wifi_scan: esp_wifi_scan_start -> 0
wifi_scan: 0 APs found (err=0)
wifi_scan: done
```

coex実装の違い（実passthrough vs no-op）は，症状に一切影響しない
ことが実機で確認された．**Check 1の具体的仮説（coexが原因）も
反証**されたため，コード変更は取り消し（`git checkout --`で
`esp_wifi_adapter.c`をコミット済み状態へ復元．実機も同コミット
状態を再書込み済み）．

なお，NuttXのno-op設計自体は「BT/802.15.4非併用ならcoexは無効化
してよい」という正しい設計判断であり，ASP3も将来的に追従する価値
はある（今回は原因ではなかったため，独立した改善として次の機会に
先送り）．

### 結論

これでFableが提示した4段階の仮説（regi2c/RF電源系統＝実施7，
WIFI_BB/FEクロックゲート＝実施8，env_is_chip，coex＝実施9）を
すべて実機で反証した．残る道筋は当初案の優先度3どおり，NuttXと
ASP3で共通のブロブに対するシンボルレベルのハードウェアブレーク
ポイント段階的絞り込み（`chip_v7_set_chan`→
`ieee80211_send_probereq`相当→`pp_tx_pkt`/`lmacTxFrame`相当の
3段階でASP3がどこまで到達するかをNuttXと比較）のみ．

## 実施10：セカンドオピニオン第4ラウンド（Fable＋codex）— 構造的ABIバグを1件発見・修正，かつ決定的な新事実（RX自体が死んでいる）

「HW設定・coex・env_is_chipとも消尽した以上，答えは『NuttXでは実行
されるがASP3では実行されない何か』のはず」という再々整理のもと，
JTAG不要の安価なチェックを優先度順に実施．

### Check 1（最優先）：PM/retentionラッパーの戻り値意味論 → 調査中に構造的ABIバグを発見・修正

`wifi_osi_funcs_t`（`hal/components/esp_wifi/include/esp_private/
wifi_os_adapter.h`）の定義を精査したところ，

```c
#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61
    void (* _regdma_link_set_write_wait_content)(void *, uint32_t, uint32_t);
    void * (* _sleep_retention_find_link_by_id)(int);
#endif
```

という，C6/C5/C61専用の条件付きフィールド2個（sleep retention連携）
が存在する．ASP3のC6 Wi-Fiビルドは`CONFIG_IDF_TARGET_ESP32C6`を
**一度も定義していなかった**（`flags.make`で実際のコンパイルフラグを
確認：`TOPPERS_ESP32C6_WIFI`等ASP3独自マクロのみで，ESP-IDF標準の
チップ識別マクロは皆無）．一方，C6向けにビルドされたblob（closed
source）は当然このマクロが定義された状態でリンクされている（NuttX
の生成`sdkconfig.h`で`#define CONFIG_IDF_TARGET_ESP32C6 1`を確認済み）．

つまり**ASP3が計算する構造体レイアウトが，この2フィールド分
（RV32で8バイト）だけblobの期待するレイアウトより短い**．
`_coex_register_start_cb`以降の全フィールド
（`_coex_schm_flexible_period_set`/`_get`・`_coex_schm_get_phase_by_idx`，
末尾の`_magic`）が，blob側の期待オフセットとずれることになる．

`asp3/target/esp32c6_espidf/esp_wifi.cmake`の`ASP3_COMPILE_DEFS`へ
`CONFIG_IDF_TARGET_ESP32C6=1`を追加して修正．`nm`で確認：
`g_wifi_osi_funcs`が0x1e0→0x1e8バイト（期待通り+8バイト）に拡大．
新設された2フィールドはNuttX同様，設定せず（NULL）のままとした
（NuttXの該当テーブル初期化コードにも
`_regdma_link_set_write_wait_content`/`_sleep_retention_find_link_by_id`
への代入が存在しない＝NuttXも同じくNULLのまま運用しており，本
移植の挙動と一致）．

**【訂正・後日同一セッション内で判明】上記のABI不整合という判定は
誤りだった．** 「本マクロがどこにも定義されていない」という当初の
確認は，`flags.make`の`-D`フラグ列だけを`grep`した結果に基づいて
いたが，`hal/nuttx/esp32c6/include/sdkconfig.h`（`esp_attr.h`の
無条件`#include "sdkconfig.h"`経由で，本ビルドの`-I`パスに既に
含まれている）が既に`#define CONFIG_IDF_TARGET_ESP32C6 1`を提供して
いることを見落としていた．実際に`-DCONFIG_IDF_TARGET_ESP32C6=1`を
一時的に外して再ビルド・`nm`確認したところ，`g_wifi_osi_funcs`は
外してもなお0x1e8バイト（付けた状態と同一）のままだった＝**構造体
は最初から一度もずれていなかった**．「実測で裏付けた」という記述
自体が誤りで，実際には修正前の状態を`nm`で直接確認しないまま
「未定義のはず」という推測だけで書いた記述だった（方法論上の反省
点：`-D`フラグの有無だけでなく`#include`経由の間接定義も必ず
`gcc -E`等で実際に展開させて確認すべきだった）．コンパイル定義
自体は実害がないため残したが（同じ値への再定義は無害），コメントを
訂正し，本件は「バグではなかった」として扱う．コミットは
`3fae84d`（訂正）．

`wifi_init_config_t`自体（Check 2の対象）は本ポートの
`hal/components/esp_wifi/include/esp_wifi.h`定義を確認した限り
`#if CONFIG_IDF_TARGET_*`によるフィールド条件分岐を持たない固定
レイアウトであり，構造体ABI不整合の心配はない（値レベルの監査は
時間の都合で未実施）．

### Check 3（決定的）：プロミスキャスモードRXテスト → **RX自体が完全に死んでいる**ことが判明

`apps/wifi_scan/wifi_scan.c`に一時診断コードを追加し，
`esp_wifi_start()`直後（スキャン開始前）に
`esp_wifi_set_promiscuous_rx_cb()`+`esp_wifi_set_promiscuous(true)`を
呼び，3秒間コールバックでの受信フレーム数を数えた（スキャン状態
機械・TX submitロジックを一切経由しない，最も直接的なRX生存確認）．

実機ログ：

```
wifi_scan: esp_wifi_start -> 0
wifi_scan: DIAG set_promiscuous_rx_cb -> 0
wifi_scan: DIAG set_promiscuous(true) -> 0
wifi_scan: DIAG promisc_rx_count=0
```

**3秒間で受信フレーム数0**．同じ場所・同じ時間帯にC3をsnifferとして
使った実験（実施6）では2秒毎に180〜220フレームの周辺トラフィックを
常時観測しており，電波環境自体は密であることが分かっている．つまり
**ASP3のC6実装は，スキャンのTX（プローブ要求）だけでなく，MAC RX
そのものが一切機能していない**．

これは投稿された仮説の枠組みを大きく変える発見である：従来
「TXがsubmitされない（coex拒否等）」という推論だったが，スキャン
ロジックを完全に迂回した素のプロミスキャスモードでも0という事実は，
**問題がスキャン固有・TX固有ではなく，MAC RXイネーブル経路（もしく
はMACフロントエンド全体）がそもそも生きていない**ことを示している．
これはINTMTX_STATUS0が終始0だったという既存の観測（実施6）とも
完全に整合する．

診断コードはテスト後に完全にrevert済み（コミット差分なし）．

### 結論・申し送り

- `CONFIG_IDF_TARGET_ESP32C6`欠如は**バグではなかった**（上記訂正
  参照．sdkconfig.h経由で既に定義済み）．
- **Check 3の結果により，捜索範囲がTX submission経路から「MAC RX
  イネーブル経路全体」へ大きく絞り込まれた**．次にブレークポイント
  トレースを行うなら，TXパス（`chip_v7_set_chan`→probe request→
  `pp_tx_pkt`）ではなく，**RXイネーブル・MAC起動シーケンス自体**
  （`wifi_hw_start`相当・MACのRX許可ビット・RXデスクリプタ／
  `esf_buf`供給）を最優先で追うべき．
## 実施11：Check 2（`wifi_init_config_t`値レベル監査）完了 — RXバッファ数は一致，PM系に軽微な差分1件

`wifi_scan.c`は`WIFI_INIT_CONFIG_DEFAULT()`を使用（C3から流用の手書き
構成ではない）．実体は`CONFIG_ESP_WIFI_*`→（`hal/nuttx/esp32c6/
include/sdkconfig.h`経由で）`CONFIG_ESPRESSIF_WIFI_*`という別名連鎖
だが，肝心の数値定義は`hal/nuttx/esp32c6/include/sdkconfig.h`自体には
存在せず，`asp3/target/esp32c3_espidf/hal_stub/include/nuttx/
config.h`（C3時代に手書きされたASP3独自スタブ．C6でも共用）が
実際の値を提供している．これをNuttXの実ビルド`.config`（実施6で
実機AP検出に成功した構成そのもの）と1件ずつ突き合わせた：

| フィールド | ASP3（hal_stub） | NuttX実`.config` | 一致 |
|---|---|---|---|
| static_rx_buf_num | 10 | 10 | ○ |
| dynamic_rx_buf_num | 32 | 32 | ○ |
| rx_mgmt_buf_num | 5 | 5 | ○ |
| rx_mgmt_buf_type | 0 | 0 | ○ |
| rx_ba_win | 6 | 6 | ○ |
| ampdu_rx_enable | 1 | （未確認だが既定1） | ○相当 |
| tx_buf_type | 1（動的） | 1（動的） | ○ |
| dynamic_tx_buf_num | 32 | 32 | ○ |
| static_tx_buf_num | **0** | **16** | **✗不一致** |
| sta_disconnected_pm | **false(0)** | **true(y)** | **✗不一致** |

**RX関連バッファ数（static/dynamic RXバッファ・RX管理バッファ・
RX BA窓）は全てNuttXと完全一致**．Check 2が懸念していた「C3流用の
手書き構成でC6向けの値が0や過小になっている」という仮説は，RXに
関しては反証された．

2件の不一致を発見：
- `static_tx_buf_num`（0 vs 16）：`tx_buf_type=1`（動的バッファ）が
  両者で選択されており，ESP-IDFの設計上この場合`static_tx_buf_num`
  は使われない値のはず（`WIFI_STATIC_TX_BUFFER_NUM`マクロも
  `CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM`未定義時は0が既定）．TX
  経路の差分であり，かつ動的モードでは無関係な可能性が高いため，
  現在最優先のRX問題の説明にはならないと判断．
- `sta_disconnected_pm`（false vs true）：「未接続状態での省電力
  管理」のON/OFF．本テストは終始未接続（スキャンのみ）のためこの
  設定が有効域に入るが，**ASP3はfalse（無効）側**＝省電力で無線を
  間欠停止させない，より「常時オン」に近い設定．もしこれが原因なら
  むしろNuttX側（true＝間欠的に省電力へ入る）の方がRXに穴が空き
  やすいはずで，方向性が逆．RX完全ゼロの説明として弱いと判断し，
  優先度を下げる（真逆方向の設定でASP3の方が「常時受信」に近いのに
  0件という事実の方が重い）．

### 結論・申し送り（更新）

Check 1（PM/retention）・Check 2（config値）ともほぼ消尽．残る
デジタル比較の余地は乏しく，静的比較で説明できる範囲はここまで．
次段はコーディネータ提案の`--wrap`リンカトリック（`chip_v7_set_chan`
ではなくRXイネーブル/`esf_buf_alloc`相当・`hal_init`相当・
`_malloc`/`_calloc`/`_queue_send`/`_queue_recv`等FreeRTOS互換
ラッパーの呼び出し回数・引数・戻り値をNuttXと突き合わせるトレース
基盤）が唯一の残された具体的な次の一手．これは新規のインフラ構築
（シンボル名の実機確認・リングバッファ実装・ASP3とNuttX両方への
適用）を要する相応の作業量のため，着手前に確認を仰ぐ．

## 実施12：`--wrap`トレース基盤を構築 → `wifi_hw_start`が一度も呼ばれていないことを実機で確認（最有力の手がかり）

### 基盤構築

`nm`/`objdump -r`で`libpp.a`/`libnet80211.a`の内部シンボル（公開
ヘッダなし）を実機確認し，以下13個を`-Wl,--wrap=<sym>`でラップした
（`asp3/target/esp32c6_espidf/wifi/wifi_trace.c`／`wifi_trace.h`．
リングバッファ512件・`t_us_low`/`id`/`a0`/`a1`/`ret`を記録．ISR安全化
のためsyslogは使わずリングバッファのみ．最後に`wifi_trace_dump()`で
まとめて出力）：

`wifi_hw_start`・`wifi_hmac_init`・`wifi_lmac_init`・
`wDev_Rxbuf_Init`・`esf_buf_setup`・`esf_buf_setup_static`・
`wdev_set_promis`・`sta_rx_cb`・`wifi_recycle_rx_pkt`・
`esf_buf_alloc`・`esf_buf_alloc_dynamic`・`wdev_data_init`・
`wifi_set_rx_policy`

全13シンボルとも実際にクロスライブラリの未解決参照として存在し，
リンクが正常に通ることを確認済み（`--wrap`が機能する条件を満たす）．

### 実機トレース結果（`esp_wifi_init`→`start`→プロミスキャス3秒間）

```
wifi_trace: total=10 (showing 10, ring=512)
[0] t=27879   esf_buf_setup        a0=0 a1=0
[1] t=28074   esf_buf_alloc        a0=0 a1=5
[2] t=28209   esf_buf_alloc        a0=0 a1=5
[3] t=28216   esf_buf_alloc        a0=0 a1=5
[4] t=28221   esf_buf_alloc        a0=0 a1=5
[5] t=28681   wDev_Rxbuf_Init      a0=10(=static_rx_buf_num) a1=0
[6] t=121021  wifi_set_rx_policy   a0=0 a1=12
[7] t=123342  wifi_set_rx_policy   a0=1 a1=12
[8] t=127354  wdev_set_promis      a0=1（有効化．esp_wifi_set_promiscuous(true)に対応）
[9] t=3128410 wdev_set_promis      a0=0（無効化．3秒後のfalseに対応）
```

（`ret`列はTOPPERS syslogの1呼出あたり引数上限＝`TNUM_LOGPAR`=6
[`t_syslog.h`]に対し本呼出が6個ちょうどでほぼ限界のため，最後の
`%08x`が展開されず文字列のまま出力される軽微な表示バグが判明．
`a0`/`a1`/`id`は正しく記録・表示されており，結論への影響はない．
修正は次回の課題として残す．）

**`wifi_hw_start`・`wifi_hmac_init`・`wifi_lmac_init`・
`sta_rx_cb`・`wifi_recycle_rx_pkt`・`esf_buf_alloc_dynamic`・
`esf_buf_setup_static`・`wdev_data_init`は一度も呼ばれていない．**

### `wifi_hw_start`の呼び出し元をobjdumpで直接特定

`esp_wifi_init()`/`esp_wifi_start()`本体はESP-IDF実ソース
（`esp_wifi/src/wifi_init.c`，ASP3も無改変で採用）だが，`wifi_hw_start`
自体はどのソースファイルからも参照されておらず（`grep`で該当なし），
blob内部（`libnet80211.a`）からのみ呼ばれる．`objdump -r`で実際の
呼び出し元を特定：

```
$ objdump -r libnet80211/ieee80211_ioctl.o | grep "R_RISCV_CALL.*wifi_hw_start"
  （呼び出し元セクション＝関数）
  .text.wifi_hw_mode_switch      （複数回）
  .text.wifi_set_promis_process  （1回，オフセット0x2a）
  .text.wifi_start_process       （2回）
```

つまり：
- `esp_wifi_start()` → （blob内部）→ **`wifi_start_process`** →
  `wifi_hw_start`（呼ばれるはずの経路）
- `esp_wifi_set_promiscuous(true)` → （blob内部）→
  **`wifi_set_promis_process`** → `wifi_hw_start`（同上）

両方の経路とも，本ポートの実機トレースでは`wifi_hw_start`まで
到達していない．`wifi_set_promis_process`の逆アセンブルを見ると，
呼び出し前に条件分岐が2つある：

```asm
   8:  lbu  a4,8(a0)          # a0＝引数構造体のoffset+8（有効化フラグ？）
  1c:  lbu  a5,499(s0)        # グローバル状態g_ic＋オフセット499
  20:  beqz a4,58 <.L553>     # a4==0 なら分岐（無効化系？）
  24:  beq  a5,s1(=1),4c <.L554>  # a5==1（既に有効化済み？）なら分岐（スキップ）
  28:  li a0,2
  2a:  call wifi_hw_start     # 上記2条件を両方すり抜けた場合のみ到達
```

`wifi_start_process`側にも同様に，グローバル状態（`g_ic`+497の
バイト値・別の間接ポインタが指す状態バイト）で0/1/2の3値分岐する
コードがあり，`wifi_hw_start`への到達はこの内部状態機械に依存する．

### 結論

**現時点で最も有力な手がかり．** `wifi_hw_start`（文字通り「Wi-Fi
ハードウェアを起動する」関数）が，`esp_wifi_start()`からも
`esp_wifi_set_promiscuous(true)`からも実機では一度も呼ばれておらず，
かつ両APIとも`err=0`（成功）を返す──これは「MAC RXが一切機能しない
のに，エラーも割込みも一切出ない」という，これまでの全観測（実施6
のsniffer実験・INTMTX_STATUS0終始0・今回のプロミスキャス0件）と
完全に整合する具体的なメカニズムである．

呼び出しを抑制している条件（`g_ic`構造体の特定オフセットの値，
または`wifi_set_promis_process`第1引数構造体のoffset+8）を，
シンボルなしの逆アセンブルだけで完全に特定するのは困難．次の
一手としては：
1. NuttX側に同じ`--wrap`セットを適用し，同じ地点でトレースを取って
   `wifi_hw_start`が実際に呼ばれているか・その際の`g_ic`関連状態が
   ASP3と何が違うかを比較する（コーディネータの原案どおり．今回は
   時間の都合でASP3側のみ実施）．
2. または，`g_ic+497`／`g_ic+499`に相当するアドレスをJTAGで実機
   読み出しし，ASP3で実際にどんな値になっているかを直接確認する
   （どちらの分岐条件が満たされているのか＝a4/a5の実際の値を特定）．

トレース基盤（`wifi_trace.c`/`.h`・`esp_wifi.cmake`の`--wrap`設定・
`wifi_scan.c`のプロミスキャステスト）はコミット済み（未push）．
次回セッションでも再利用できるよう，あえてrevertせず残している．

## 実施13：`wifi_start_process`は「成功」を返しながら`wifi_hw_start`を呼ばずに完了することを確定

前回（実施12）の続きとして，`wifi_start_process`／`wifi_set_promis_process`
自体・`adc2_wifi_acquire`／`ieee80211_set_hmac_stop`／`wifi_mode_set`／
`_do_wifi_start`／`ieee80211_update_phy_country`を追加で`--wrap`し
（計20シンボル），さらに`g_ic`（`0x408476b0`．`nm`で確認）のoffset
497/499と，`g_wifi_nvs`（`0x40800890`．nvs関連ポインタ変数）が指す
先の1バイト目を，`esp_wifi_init()`直後・`esp_wifi_set_mode()`直後・
`esp_wifi_start()`直後の3点でC側から直接ピーク（JTAG不要）する
診断コードを追加した．またTOPPERS syslogの6引数上限
（`TNUM_LOGPAR`＝`t_syslog.h`）による`ret`欄未展開バグを，1エントリ
あたりのログ呼出を2回に分割することで解消した．

### 実機ログ（今回の決定版）

```
wifi_scan: esp_wifi_init -> 0
DIAG post-init   g_ic[497]=1 g_ic[499]=0   nvs_ptr=40847fbc nvs[0]=2
DIAG post-set_mode g_ic[497]=1 g_ic[499]=0
wifi_scan: esp_wifi_start -> 0
DIAG post-start  g_ic[497]=2 g_ic[499]=0   nvs_ptr=40847fbc nvs[0]=1

wifi_trace（ret欄も正しく取得）:
 [6],[7] ieee80211_set_hmac_stop           ret=1  (t=35082,35158)
 [8]     wifi_set_rx_policy  a0=0          ret=1  (t=119824)
 [9]     wifi_mode_set       a0=1          ret=0  (t=121797)
 [10]    wifi_set_rx_policy  a0=1          ret=1  (t=122232)
 [11]    ieee80211_update_phy_country      ret=0  (t=122496)
 [12]    wifi_start_process  a0=40803838 a1=8  ret=0  (t=122527)
 [13]    wdev_set_promis     a0=1          ret=0  (t=126226，esp_wifi_set_promiscuous(true)に対応)
 [14]    wifi_set_promis_process a0=40803838 a1=8 ret=0 (t=126275)
 [15][16] wdev_set_promis/wifi_set_promis_process（無効化，3秒後）
```

`adc2_wifi_acquire`（唯一のWEAKシンボル．他は全てSTRONG=T）・
`_do_wifi_start`・`wifi_hw_start`は今回も一度も出現しない．
`wifi_hw_start`・`_do_wifi_start`はSTRONGシンボルであり，同じ関数
（`wifi_start_process`）内の隣接する`--wrap`（`wifi_mode_set`・
`ieee80211_update_phy_country`）は正しく捕捉できているため，
「wrap機構が効いていないだけ」という可能性は排除できる＝**本当に
呼ばれていない**．

### 確定した事実

1. **`wifi_start_process`は実際に呼ばれ，`ret=0`（成功）を返して
   正常終了する．** クラッシュでも無限ループでもなく，「関数として
   正常終了しているが，その中でハードウェア起動（`wifi_hw_start`）
   だけが行われていない」という状態．
2. `g_ic[497]`は`esp_wifi_init()`後に1，`esp_wifi_start()`後に2へ
   遷移する（`wifi_start_process`が自身の完了マーカーとして書き込む
   値と一致．逆アセンブルで確認した`sb a5,497(s3)`＝`a5=2`のパスに
   対応）．
3. `g_wifi_nvs`が指す構造体の1バイト目は，`esp_wifi_init()`後に2，
   `esp_wifi_start()`後には1へ変化する．この値が`wifi_start_process`
   内部の分岐条件（s1）に使われている可能性が高いが，**読み出し
   タイミングの前後関係（wifi_mode_set/ieee80211_update_phy_country
   が`wifi_start_process`本体より先に呼ばれている＝これらは
   `wifi_start_process`の内部処理ではなく，呼び出し元の別ステップ
   であることが今回判明）により，チェック時点の正確な値は特定
   できていない**．
4. JTAGでの`wifi_start_process`エントリへのハードウェアブレーク
   ポイント単一命令ステップは，本ボード特有のJTAG/コンソール排他
   制約下で試みたが，リセット後のブレークポイントヒット位置が
   期待値と一致しない不安定な挙動となり，今回は断念した（実機の
   タイミングに関する既知の制約．`docs/porting/`や過去のBLE調査でも
   類似の困難が記録されている）．

### 結論・申し送り

`wifi_start_process`が「成功」を返しながら`wifi_hw_start`を一度も
呼ばずに完了する，という事実は確定した．これは偶発的なエラーでは
なく，正常系の分岐によるものである可能性が高い（TX/RXが完全に
沈黙しているのに全APIが成功を返す，という現象全体と整合する）．

次の一手（優先順）：
1. **同一の`--wrap`セット（20シンボル）をNuttXビルドへ適用**し，
   同じ地点（`esp_wifi_init`→`set_mode`→`esp_wifi_start`）で
   `wifi_hw_start`が実際に呼ばれるかどうか・`g_ic[497]`と
   `g_wifi_nvs`先頭バイトの値がASP3と何が違うかを直接比較する
   （今回はASP3側のみ．コーディネータの原案どおり，次はNuttX側）．
2. あるいは，`wifi_hw_start`が「別のイベント（scan開始・接続開始等）
   をトリガに後から呼ばれる」設計である可能性も残る．
   `esp_wifi_scan_start()`実行中も同じ`--wrap`トレースを継続
   （現状のコードは`wifi_trace_dump()`をscan開始前に呼んでいるため，
   scan中のイベントは捕捉できていない．`wifi_trace_dump()`を
   scan完了後に移動して再検証する価値がある）．
3. JTAG単一命令ステップは，タイミングの制約が緩和される条件
   （例：起動直後にあえて`tslp_tsk()`で長時間停止させ，その間に
   JTAGアタッチ・ブレークポイント設置・resumeする，など）を
   見直せば再挑戦の余地がある．

トレース基盤・診断ピークコードは全てコミット済み（未push）で
維持している．

## 実施14：`wifi_trace_dump()`をscan完了後まで延長 → スキャン中も`wifi_hw_start`は一度も呼ばれないことを確認（チャネル毎の`esf_buf_alloc`は継続）

実施13の申し送り事項2（トレース捕捉範囲をscan完了後まで延長）を
即座に実施．`wifi_trace_dump()`の呼び出し位置を，プロミスキャス
テスト直後から`esp_wifi_scan_start()`実行→`SCAN_DONE`待ち後へ移動．

### 結果：スキャン中の挙動が判明

```
[17] t=3128377  wifi_set_rx_policy a0=3            ret=1  （スキャン開始．新しいpolicy値=3）
[18] t=3129446  esf_buf_alloc      a1=2  ret=4080acf8
[19] t=3253674  esf_buf_alloc      a1=2  ret=4080adf0   （+124228us）
[20] t=3376165  esf_buf_alloc      a1=2  ret=4080af08   （+122491us）
[21] t=3498408  esf_buf_alloc      a1=2  ret=4080acf8   （+122243us）
[22] t=3620697  esf_buf_alloc      a1=2  ret=4080ae00   （+122289us）
[23] t=3742757  esf_buf_alloc      a1=2  ret=4080acf8   （+122060us）
[24] t=3865172  esf_buf_alloc      a1=2  ret=4080af18   （+122415us）
[25] t=3987210  esf_buf_alloc      a1=2  ret=4080acf8   （+122038us）
[26] t=4109966  esf_buf_alloc      a1=2  ret=4080ae00   （+122756us）
[27] t=4231765  esf_buf_alloc      a1=2  ret=4080acf8   （+121799us）
[28] t=4353445  esf_buf_alloc      a1=2  ret=4080af18   （+121680us）
[29] t=5557186  wifi_set_rx_policy a0=4            ret=1  （スキャン終了．policy値=4）
```

`esf_buf_alloc`（プールID＝a1=2．初期化時のa1=5とは別プール）が
**約122ms間隔で11回**（チャネル1〜11の2.4GHz全チャネル数と一致）
呼ばれ続けている．これはスキャンのチャネルホップ機構自体は
タイマ駆動で正常に動作し続けている証拠．`ret`は数個の固定アドレス
（`4080acf8`/`4080ae00`/`4080af18`等）を使い回しており，小さな
バッファプールを再利用している挙動と一致（クラッシュや無限ループは
していない）．

**しかし`wifi_hw_start`・`_do_wifi_start`・`sta_rx_cb`・
`wifi_recycle_rx_pkt`は，このチャネルホップの間も含め，スキャン
開始から完了まで一度も呼ばれない．**

### 最終結論

- ASP3のC6 Wi-Fiは，`esp_wifi_init`→`esp_wifi_set_mode`→
  `esp_wifi_start`→（プロミスキャス）→`esp_wifi_scan_start`→
  11チャネル分の待機→`SCAN_DONE`という**ソフトウェア側の状態機械は
  完全に正常に，タイマ駆動で最後まで動く**．
- しかしこの全区間を通じて，`wifi_hw_start`（文字通り「Wi-Fi
  ハードウェアを起動する」関数）は**一度も呼ばれない**．
- `wifi_start_process`（`esp_wifi_start()`の内部ハンドラ）は実際に
  呼ばれ，`ret=0`（成功）で正常終了する．クラッシュでも無限ループ
  でもなく，「正常系の分岐として`wifi_hw_start`を呼ばない経路を
  通っている」という状態．
- これは，これまでの全観測（sniffer実験でTX電波皆無・
  INTMTX_STATUS0終始0・プロミスキャスモードでRX0件・今回のスキャン
  でAP0件）を単一の具体的なメカニズムで完全に説明する．

ASP3側の静的・動的解析（`nm`/`objdump -r`による呼び出し元特定，
`--wrap`によるライブトレース，`g_ic`/`g_wifi_nvs`の直接ピーク）は
実質的にやり尽くした．逆アセンブルだけでは`wifi_start_process`内部の
正確な分岐条件（S1相当のレジスタが実際にどの値を取り，どちらの
分岐を通っているか）を確定できず，本ボード特有のJTAG/コンソール
排他制約下でのライブ単一命令ステップも今回は安定しなかった．

**残された最も直接的な次の一手は，コーディネータの原案どおり
NuttX側に同一の`--wrap`セット（`wifi_hw_start`・`wifi_start_process`
・`wifi_mode_set`等，計20シンボル）を適用し，同じ地点で
`wifi_hw_start`が実際に呼ばれるか，呼ばれる場合とASP3の状態
（`g_ic[497]`・`g_wifi_nvs`先頭バイト）が何が違うかを直接比較する
ことのみ．** ASP3単独でのこれ以上の深掘りは，ツール（デコンパイラ
等）なしでは収穫逓減と判断する．

## 実施15：NuttX側に同一`--wrap`トレースを適用 → `wifi_hw_start`理論を反証，真の分岐点を特定

コーディネータ指示どおり，実施12〜14でASP3に適用した20シンボルの
`--wrap`セットを，同一blob（`libpp.a`/`libnet80211.a`）を使う
NuttXビルド（`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`）へ
そのまま適用した．

### 実装

- `arch/risc-v/src/esp32c6/esp_wifi_trace.c`（新規）：ASP3の
  `wifi_trace.c`と同構造（リングバッファ512件）だが，`syslog`では
  なく`printf`+`fflush`（`wlerr`/`wlinfo`はこの構成で無効化される
  ため）．`g_ic`/`g_wifi_nvs`はハードコードアドレスではなく
  `extern uint8_t g_ic[]; extern void *g_wifi_nvs;`と直接シンボル
  参照（同一blobなのでシンボル名は共通．リンクアドレスだけが
  ビルドごとに異なるため）．
- `arch/risc-v/src/esp32c6/Make.defs`：`CONFIG_ESPRESSIF_WIFI`
  ブロック内に`CHIP_CSRCS += esp_wifi_trace.c`と`LDFLAGS +=
  --wrap=<sym>`を20行追加（同ファイル内に`--wrap=bootloader_
  print_banner`という先例が既にあり，NuttXのMakeビルドが`LDFLAGS`
  経由の`--wrap`を素で（`-Wl,`prefixなしで）受け付けることを確認
  した上で追加）．
- `arch/risc-v/src/common/espressif/esp_wifi_api.c`：
  `esp_wifi_api_adapter_init()`（`esp_wifi_init`＋
  `esp_wifi_set_mode(NULL)`を行う関数）と`esp_wifi_api_start()`
  （実際のmode設定＋`esp_wifi_start()`を行う関数）に
  `wifi_trace_reset()`／`wifi_trace_peek_state("post-init"
  /"post-set_mode_null"/"post-set_mode"/"post-start")`／
  `wifi_trace_dump()`を，ASP3の`wifi_scan.c`と対応する地点に挿入．
- `arch/risc-v/src/common/espressif/esp_wifi_utils.c`：
  `esp_wifi_start_scan()`内（既存の実施6由来のINTMTX busy-poll
  診断コードの直後）に`wifi_trace_dump()`を追加し，スキャン中の
  イベントも捕捉．

`make CROSSDEV=...`で再ビルド・実機（`/dev/ttyACM0`）へ書込み・
`wapi scan wlan0`実行．

### 結果1：`esp_wifi_init`→`set_mode`→`esp_wifi_start`まではASP3と完全に同一

```
post-init         g_ic[497]=1 g_ic[499]=0  nvs[0]=2
post-set_mode_null g_ic[497]=1 g_ic[499]=0  nvs[0]=0
post-set_mode(STA) g_ic[497]=1 g_ic[499]=0  nvs[0]=1
post-start         g_ic[497]=2 g_ic[499]=0  nvs[0]=1

トレース：esf_buf_setup → esf_buf_alloc×4(a1=5) → wDev_Rxbuf_Init(a0=10)
→ ieee80211_set_hmac_stop×2(ret=1) → wifi_set_rx_policy(a0=0,ret=1)
→ wifi_mode_set(a0=1,ret=0) → wifi_set_rx_policy(a0=1,ret=1)
→ ieee80211_update_phy_country(ret=0) → wifi_start_process(ret=0)
```

**この時点で`wifi_hw_start`は一度も現れない．** これはASP3の実施13
キャプチャと呼び出し順・引数・戻り値まで完全一致する．

**つまり`wifi_hw_start`が「呼ばれない」ことは，NuttX（実機でAP検出
に成功する構成）でも全く同じであり，これは異常でも何でもなく，
init/start段階の正常な振る舞いだった．「wifi_hw_startが呼ばれて
いないことが根本原因」という仮説（実施12〜14）はこれで反証
される．**

### 結果2：スキャン中，NuttXは`sta_rx_cb`（RX受信コールバック）が実際に発火する ── ASP3は依然として一切発火しない

`wapi scan wlan0`実行中のトレース（抜粋）：

```
[13] wifi_set_rx_policy a0=3           （スキャン開始）
[14] esf_buf_alloc a1=2
[15] esf_buf_alloc a1=8   ← 新規プールID！
[16] sta_rx_cb    a0=40830ee0          ← 受信コールバック発火！
[17] esf_buf_alloc a1=2
[18] esf_buf_alloc a1=8
[19] sta_rx_cb
[20] esf_buf_alloc a1=8
[21] sta_rx_cb
[22]-[25] esf_buf_alloc a1=2（チャネルホップ×4）
[26] esf_buf_alloc a1=8
[27] sta_rx_cb
[28] esf_buf_alloc a1=8
[29] sta_rx_cb
[30]-[34] esf_buf_alloc a1=2（チャネルホップ×5）
[35] wifi_set_rx_policy a0=4           （スキャン終了）
```

同時に取得した`DIAG intmtx`（実施6由来）：
`status0_or=0x00000001`（MAC割込み実際に発火．ASP3は終始0x0）．

**`esf_buf_alloc`のプールID`a1=8`（RX受信バッファ）と`sta_rx_cb`
（受信コールバック）が，スキャン中に計5回発火している．ASP3側の
同一区間キャプチャ（実施14）には，この2つが1回も現れない．**

### 反証実験：`esp_wifi_scan_start(NULL, false)` → 明示的`wifi_scan_config_t`への変更（効果なし）

NuttXは`esp_wifi_scan_start()`に常に非NULL（`kmm_calloc`＋
`scan_type=WIFI_SCAN_TYPE_ACTIVE`明示設定）の設定を渡すのに対し，
ASP3の`wifi_scan.c`は`NULL`を渡していた（ヘッダのドキュメント上は
NULL＝同等のデフォルトのはずだが，念のため実験）．ASP3側を
NuttXと同じ明示的構造体（`scan_type=WIFI_SCAN_TYPE_ACTIVE`，他
`memset`で0）に変更し実機再検証したが，**結果は1バイトも変わらず
`a1=8`/`sta_rx_cb`とも未発火のまま**．この仮説も反証されたため
変更は取り消した（コミット差分なし）．

### 結論

- `esf_buf_alloc(a1=8)`＋`sta_rx_cb`は，実際に受信した生フレームを
  MAC割込みハンドラ経由で処理する際に確保・呼び出される，**RX
  パイプラインの「本体」**である可能性が高い．これが一度も発火
  しないのは，独立したバグというより，**MAC RX割込みそのものが
  一度も上がらない（INTMTX_STATUS0終始0）という，実施6以来
  確立している事実の，より下流での再確認**と見るのが妥当．
- ソフトウェア呼び出しグラフ（`esp_wifi_init`→`set_mode`→
  `esp_wifi_start`→スキャン全体）は，呼び出し順・引数・戻り値まで
  **NuttXと完全に同一**であることが今回確定した．これは大きな
  前進で，残る差分の候補を大幅に絞り込む：
  1. ラップした関数**内部**で，さらなる関数呼び出しを経由せず
     直接レジスタを叩いている箇所（命令レベルトレースでしか
     見えない）．
  2. 割込みコントローラ（INTMTX/PLIC_MX）の設定状態そのもの
     （実施6で静的比較済みだが，スキャン実行中という条件下では
     未比較の可能性が残る）．
  3. 純粋なタイミング／レース条件．

### 申し送り

これは非常に長期にわたる調査の末に到達した，きわめて狭い残存
ギャップである．ソフトウェア呼び出しグラフの一致がここまで確認
できた以上，ASP3側だけでのさらなる深掘りは，命令レベルの実機
トレース（本ボードのJTAG/コンソール排他制約下では実現が難しい
ことが実施13で判明済み）か，何らかの形でのデコンパイラ導入なしに
は限界と判断する．コーディネータの原案どおり，「ここで一旦精密な
現状報告として区切る」のが適切な局面と考える．

NuttX側の変更（`esp_wifi_trace.c`新規・`Make.defs`／
`esp_wifi_api.c`／`esp_wifi_utils.c`の変更）は
`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`にのみ存在し
（asp3_esp_idfリポジトリ外・ジョブ一時領域），再現する場合は本節の
記述（関数名・挿入位置）を参照して再実装すること．

## 実施16：セカンドオピニオン第6ラウンド（Fable）— Priority 1（ROM占有RAM領域の重複監査）を実施・反証

「ソフトウェア呼び出しグラフが完全一致する以上，差分は『呼ばれるか
否か』ではなく『同じ呼び出しの効果』にある」という整理のもと，
最優先（静的解析のみ・実機不要）としてROM占有RAM領域との重複を
監査した．これは`clk_wifipwr_en`・`clk_i2c_mst_en`・（反証済みの）
`CONFIG_IDF_TARGET_ESP32C6`に続く「Direct Bootがブートローダ側の
副作用を再現できていない」という同型バグの4件目の可能性という
指摘．

### 手法

`hal/components/esp_rom/esp32c6/ld/esp32c6.rom.*.ld`全ファイルから
「Data (.data, .bss, .rodata)」セクションのアドレスを機械的に抽出．
ROM独自のグローバル状態（関数ポインタテーブル・coex/net80211/pp/
heap/spiflash等の内部管理構造体）が置かれる固定アドレス群が3つの
クラスタに分かれることを確認：

1. `0x40000280`〜`0x40000390`：ROM本体のコード/データ領域内
   （SRAM外．無関係）．
2. `0x4004fd40`〜`0x4004ffe0`：ROM専用の別RAM領域（同じくアプリ
   SRAM＝`0x40800000`〜とは無関係）．
3. **`0x4087fce8`〜`0x4087ffe8`（約4.5KB）：アプリSRAM領域内**．
   `coex_env_ptr`・`g_scan`・`g_chm`・`net80211_funcs`・
   `pTxRx`・`lmacConfMib_ptr`・`heap_tlsf_table_ptr`・
   `rom_spiflash_legacy_funcs`等，Wi-Fi/coex/heap関連のROM内部
   状態がまさにこの領域に集中している．

C6の実SRAM総容量は`soc.h`の`SOC_DRAM_LOW=0x40800000`・
`SOC_DRAM_HIGH=0x40880000`から512KB（0x80000）．

### ASP3の実際のRAM使用範囲

`asp3/target/esp32c6_espidf/esp32c6.ld`の`MEMORY`宣言：

```
RAM(rwx) : ORIGIN = 0x40800000, LENGTH = 448k    /* = 0x40870000まで */
```

**448KBは実SRAM512KBより64KB少ない．** つまりASP3は最初から
ハードウェア上限（0x40880000）ではなく，そこから64KB手前
（0x40870000）までしかRAM領域として宣言しておらず，リンカが
`.data`/`.bss`をこの範囲を超えて配置することは（リンクエラーに
なるため）原理的に不可能．

実際のビルド（`build/c6_wifi_scan/asp.elf`）で検証：
`__bss_end = 0x4084aac8`（448KB宣言のうち実際に使っているのは
先頭から約301KBのみ）．ROM占有クラスタの下端`0x4087fce8`まで
は，ASP3の宣言済みRAM上限（`0x40870000`）からもなお約63KBの
余裕があり，実際の`__bss_end`からは約215KBの余裕がある．

### 結論：**重複なし．Priority 1は反証**

`clk_wifipwr_en`／`clk_i2c_mst_en`のケースとは異なり，本件は
ASP3が「知らずに安全側に倒れていた」ケース（448kという数値の
由来は不明だが，結果として実SRAM総量より一貫して小さく，ROM
占有領域の下端まで数十KBの余裕を持って収まっている）．スタック
オーバーフロー等の動的な要因を除けば，静的な意味でのROM状態破壊は
発生し得ない．この方向の追求はここで打ち切る．

## 実施17：Priority 3（生TX注入テスト）— スキャン固有仮説を完全に閉じる

`apps/wifi_scan/wifi_scan.c`に一時診断コードを追加し，スキャン状態
機械を完全に迂回して`esp_wifi_80211_tx(WIFI_IF_STA, ...)`で最小の
802.11プローブ要求フレーム（24バイトヘッダ＋SSID IE[長さ0]＝26
バイト，宛先/BSSIDともブロードキャスト）を200ms間隔で10回，直接
注入した．同時にC3 sniffer（`/home/honda/.claude/jobs/494f98a3/
tmp/c3_sniffer2/`．OUI`58:e6:c5`検出，チャネル1固定）を稼働させ，
実機で並行キャプチャした．

### 結果

```
wifi_scan: DIAG 80211_tx[0..9] -> 0   （全10回とも成功を返す）
```

`--wrap`トレースでも，注入と同時刻に新規プールID（`esf_buf_alloc`
a1=1，非ゼロのa0引数付き）が10回発火することを確認——**ソフトウェア
側のTXバッファ確保は実際に動いている**．

しかしC3 sniffer側：

```
sniffer: heartbeat: total_frames=3150→5324（継続的に増加＝周辺の
  実トラフィックを継続受信できている）, target_frames=0（終始）
```

**C6からのフレームは1つも検出されなかった．** `esp_wifi_80211_tx()`
はスキャンの確率的な送信タイミングやチャネルホップを完全に迂回する
最も直接的なTX経路であり，これがゼロなら「スキャン固有の問題」
という可能性は完全に排除できる．

### 結論

**TXは真にスキャンと無関係に，根底から機能していない．** ソフト
ウェア層（`esp_wifi_80211_tx`の戻り値・`esf_buf_alloc`によるバッファ
確保）は全て正常に動作しているように見えるが，実際のRF出力は皆無．
これは実施6のsniffer実験（TX不在）を，より直接的な経路で再確認する
形となった．次の焦点はPriority 4（PHY較正・delay精度）へ．

診断コードは完全にrevert済み（コミット差分なし．実機は既存のコミット
状態へ再書込み済み）．

## 実施18：Priority 4（PHY較正戻り値・delay精度）— 両方とも反証

### delay精度：ROM関数そのもの，チェック不要と判断

`esp_rom_delay_us`は`hal/components/esp_rom/esp32c6/ld/
esp32c6.rom.api.ld`で`PROVIDE(esp_rom_delay_us = ets_delay_us)`と
定義され，`nm`で実機シンボルが`40000040 A esp_rom_delay_us`（ROM
本体のアドレスそのもの）であることを確認．ASP3独自の再実装は
存在しない（`grep`で該当なし）．同一の生シリコンROMコードが
NuttX/ESP-IDFとも共通で実行されるため，C6の実クロック（160MHz，
既にPhase Aで確認・較正済み）に対する精度はASP3固有の問題になり
得ない．静的確認のみで終了（実機テスト不要と判断）．

### PHY較正戻り値：`register_chipv7_phy()`の戻り値を`--wrap`トレースへ追加（21個目）

`esp_phy/src/phy_init.c`（ASP3も無改変で採用する実ソース）の
実際のコードパス（`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`
未定義＝`nvs_enable=0`のため）は

```c
#else
    register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL);
#endif
```

で戻り値を完全に破棄しており，これまで一度も確認されていなかった．
既存の`--wrap`トレース基盤に21個目のシンボルとして追加し実測．

**ASP3の実機結果：`register_chipv7_phy` ret=1
（`ESP_CAL_DATA_CHECK_FAIL`）**

一見エラーに見えるが，NuttX側に同一の`--wrap`を追加して同一地点
（`esp_wifi_init`→`esp_wifi_start`）で実測したところ，
**NuttXも全く同じ`ret=1`を返す**（両者とも`nvs_enable=0`・
`PHY_RF_CAL_FULL`モードでの，保存済み較正データなしの新規較正
という同一条件のため）．

### 結論：Priority 4も両方とも反証（差分なし）

`docs/errors.md`相当のESP-IDFヘッダコメントが示す「チェックサム
失敗または較正データが古い」という意味は，そもそも保存済み
データが存在しない新規較正では常に成立する当然の戻り値であり，
異常ではない．delay精度・PHY較正戻り値とも，ASP3とNuttXの間に
一切の差分が見られなかった．

## 実施15〜18のまとめ：Fable第6ラウンドの4優先度すべてに具体的な差分なし

| 優先度 | 内容 | 結果 |
|---|---|---|
| 1 | ROM占有RAM領域との重複 | 重複なし（63KB以上の余裕） |
| 2 | アクティブスキャン中のレジスタスナップショット | **未実施**（労力大） |
| 3 | 生TX注入テスト | TXは根底から死んでいる（スキャン非依存を確認） |
| 4 | PHY較正戻り値・delay精度 | 両方ともNuttXと完全一致 |

Priority 1・3・4はいずれも「ASP3とNuttXの間に差はない」という
結果に終わり，実施15（`--wrap`トレース比較）で確立した「ソフト
ウェア呼び出しグラフは呼び出し順・引数・戻り値まで完全に一致する」
という事実を，さらに広い範囲（PHY較正・TX注入パス）で補強する
形となった．唯一実施していないPriority 2（アクティブスキャン中の
MAC内部レジスタ／PHY・AGC/BB/FEレジスタ／regi2c読み戻しのスナップ
ショット比較）が，残された最後の具体的な次の一手である．これは
新たなwrap関数（チャネルホップ関数．未特定）へのレジスタ読み出し
埋め込みと，ASP3・NuttX両方への適用を要する，相応の実装量を伴う
作業．

## 実施19：Priority 2（アクティブスキャン中のレジスタスナップショット）— 決定的な結果：PHY/AGCがASP3では完全に凍結している

### チャネルホップの実駆動関数を特定：`scan_inter_channel_timeout_process`は誤りだった

まず`scan_inter_channel_timeout_process`（実施12〜14で仮定していた
「毎チャネル発火する」関数）を`--wrap`してみたが，**実機で0回しか
発火しなかった**．疑問を解くため，ASP3自身のタイマ機構
（`esp_shim_timer_setfn`/`esp_shim_timer_arm_us`．chip固有の
`esp32c6_espidf/wifi/esp_wifi_adapter.c`・`esp_shim.c`．blobでは
なくASP3自身のコードなので直接ログ埋め込み可能）に一時的な
診断ログを追加し，実際に登録される関数ポインタと発火状況を確認した：

- `_timer_setfn`で登録される関数の一つ（`nm`で名前解決）：
  **`chm_end_op_timeout`**（`libpp.a`．`t`＝ローカルシンボルにつき
  `--wrap`不可）．
- このタイマは`ms=120`で一度きり（`rep=0`）armされ，発火の度に
  ハンドラ自身が次回分を再armする「自己再arm」パターンで動作しており，
  **実測でスキャン中ずっと約120ms間隔（後半は360msへ変化）で
  発火し続ける**——これがチャネルホップの実体だった．

`scan_inter_channel_timeout`/`scan_enter_oper_channel`（ともにscan
モジュール側で登録されるタイマ）は登録はされるが**一度も発火しない**
ことも判明．実際のチャネル管理はより下位の`chm_*`（channel manager）
層が担っており，scanモジュール側のタイマは通常は使われない経路
（エッジケース）用と考えられる．

### レジスタスナップショットの起点：`esf_buf_alloc(a1=2)`を再利用

`chm_end_op_timeout`はローカルシンボルで`--wrap`できないため，
同じ約120ms周期で確実に発火するグローバルシンボル
`esf_buf_alloc`（スキャン中のチャネル毎バッファ確保．引数`a1=2`）を
起点として再利用した（この関数は既にASP3・NuttXとも`--wrap`済みで
挙動が一致することを実施15で確認済み）．`a1==2`の場合のみ
以下を追加採取：

- `INTMTX_STATUS0/1/2`（`0x60010134/138/13c`．実施6由来）
- `0x600a7128`（PHY/AGCブロックの特定スポット値．実施2で単発
  比較したのと同じアドレス）
- `0x600a7000`〜`0x600a7fff`（PHY/AGC 4KB全域）の32bit単位総和
  チェックサム（未知の差分も拾えるようにする簡易な「変化検知」）

診断コードはASP3側は`wifi_trace.c`の`wifi_regsnap_*`関数群として
実装（トレース基盤の一部として維持．診断専用の使い捨てコードでは
ないためrevertしない），NuttX側は同一の仕組みを
`arch/risc-v/src/esp32c6/esp_wifi_trace.c`に移植し，
`esp_wifi_utils.c`の`esp_wifi_start_scan()`内（実施6由来のINTMTX
busy-poll直後）で呼び出した。

### 結果：ASP3は完全に凍結，NuttXは活発に変動

**ASP3（11チャネル）**：

```
agc_spot: d21a79f0 d21a79f0 d21a79f0 ... （全チャネルで完全に同一値）
phy_agc_sum: 54a766b1 → 19d8b855 → 19d8b618 → 19d8b3dd → ...
             （なめらかに単調減少．一定の小さな差分で規則的に
              減っていくだけ＝実RFノイズ的な変動ではなく，
              単純なカウンタ／タイムスタンプ的な値が混じっている
              だけと考えられる）
```

**NuttX（11チャネル，同一blob・同一`--wrap`機構）**：

```
agc_spot: d20f99ee d219d800 d219a800 d2161800 d204b800 d20ea800
          d2036800 d20ea800 d216b800 d2071800 d20ec800
          （毎チャネルで大きく変動．規則性なし）
phy_agc_sum: 425356f0 3a032221 e92098e4 870bd400 d2e82036 d2614609
             2622ee4a f926032d b8c22e76 c6680d66 7bac6021
             （毎回大きく異なる．ノイズ的な変動＝実際のRF/AGC値が
              チャネル毎に反映されていることと整合）
```

同時に採取した`INTMTX_STATUS0/1/2`は両者ともチャネル毎の瞬間値では
終始`0x00000000`（実施6の別測定＝スキャン全体でのOR蓄積では
NuttXが`0x1`を記録している事実と矛盾しない．瞬間値サンプリングでは
割込みのパルス的な発生を捉えられないだけ）．

### 結論：PHY/AGCハードウェアがASP3では実際には一切駆動されていない

**これはこの調査全体を通じて最も明確・決定的な直接証拠である．**
同一blob・同一チャネル切替タイミング・同一`--wrap`機構・同一
レジスタアドレスで比較し，NuttXでは実際にRF/AGC回路が稼働している
ことを示す生々しい変動が見える一方，ASP3では該当レジスタ領域が
実質的に凍結している（緩やかなカウンタ的ドリフトのみ）．

これは，これまでの全観測（TX電波皆無・promiscuous RX0件・
INTMTX_STATUS0終始0・`wifi_hw_start`不発・スキャン完了正規経路
不使用）を，ハードウェアレベルで裏付ける最終的な証拠となる：
**PHY/RFフロントエンドがDirect Boot環境下で実際には有効化されて
いない．** ソフトウェア呼び出しグラフ・戻り値・設定値は全て
NuttXと一致するにもかかわらず，PHYチップの中身（AGC/BB/FE回路）
だけが沈黙したままという状態．

### 申し送り

具体的な原因（PHY回路群の実際の有効化に必要な，まだ発見できて
いない1個以上のレジスタ書き込みまたはシーケンス）は依然未特定．
静的・動的解析（`--wrap`トレース・レジスタ直読み・NuttXとの並行
比較）はここまでで実質的に尽くした．残る手段は：

1. `register_chipv7_phy()`内部（closed-source）の命令レベル
   トレース——本ボードのJTAG/コンソール排他制約下では不安定
   （実施13参照）．
2. ロジックアナライザ等，ソフトウェアに依存しない手段でアンテナ／
   RFスイッチ配線の実信号を直接観測する（本セッションでは実施
   不可）．

これは非常に長期にわたる調査の末に到達した，明確で具体的な現状
報告である：**どこで**（PHY/AGCレジスタ）・**何が**（ASP3では
凍結・NuttXでは活発）異なるかは完全に特定できたが，**なぜ**
（どの初期化ステップが欠けているか）は依然未解明．

## 実施20：Priority 2続き（regi2c/ROM常駐PHY関数トレース）— 「決定的な分岐発見」を自己訂正，結論は実施19のみが生存

セカンドオピニオン（Fable＋codex）から，実施19のPHY/AGC凍結所見を
受けた新ラウンドの4優先度（1＝標準ブートローダA/B・2＝regi2c書込み
ログ・3＝PHY初期化データ比較・4＝セクション初期化監査）が来た．
本ラウンドでは優先度2（regi2c書込み・ROM常駐PHY関数の呼出しトレース）
に着手し，途中で**一度「決定的な新発見」と誤認した箇所を，同じ
セッション内でJTAGによる再検証により自己訂正した**．経緯を正直に
記録する．

### 誤った「発見」：`chip_v7_set_chan_ana`がASP3では一度も呼ばれない（→誤り）

`esp32c6.rom.phy.ld`から，`libphy.a`が実際に未解決参照している
ROM常駐PHY関数（`nm`の`U`シンボルとの突合せで確認）を16個選び，
既存の`--wrap`基盤に追加した：`chip_v7_set_chan_ana`・
`set_channel_rfpll_freq`・`set_rfpll_freq`・`write_rfpll_sdm`・
`wait_rfpll_cal_end`・`enable_agc`・`disable_agc`・`mac_enable_bb`・
`fe_reg_init`・`fe_txrx_reset`・`phy_bbpll_cal`・`set_rxclk_en`・
`set_txclk_en`・`write_chan_freq`・`restart_cal`・`i2cmst_reg_init`
（後に`rxiq_cal_init`・`set_rx_gain_cal_dc_new`も追加，計39シンボル）．

ASP3実機での**syslogベースの詳細トレースダンプ**（`wifi_trace_dump()`）
を見ると，`chip_v7_set_chan_ana`・`mac_enable_bb`・`fe_reg_init`・
`i2cmst_reg_init`・`rxiq_cal_init`・`set_rx_gain_cal_dc_new`・
`set_rfpll_freq`が**一度も出現しない**ように見えた．同一トレースを
NuttXに適用すると，これらが複数回（`chip_v7_set_chan_ana`は20回等）
出現した．さらに逆アセンブルで`rxiq_cal_init`冒頭に
`phy_param+164`のbit10（0x400）による早期return分岐を発見し，
「この分岐でASP3側だけRX-IQ較正がスキップされている」という
一見筋の通った仮説を立てた．**この時点でユーザに「調査全体で
最も具体的な手がかり」と報告した．**

### 反証：`phy_param+164`は両プラットフォームで完全に同一値

しかし`phy_param+164`の実測値をASP3・NuttX双方で確認したところ，
**`0x0190d6a8`（bit10セット済み）で完全に一致**しており，スキャン中
の全11チャネルにわたり両者とも一切変化しなかった（`wifi_regsnap_t`に
`phy_param_flags`フィールドを追加して`esf_buf_alloc`起点で採取）．
分岐条件そのものは両者で同じ状態であり，これだけでは
`chip_v7_set_chan_ana`の呼出し有無を説明できないことが判明．

### 真因：syslogのバースト・ロス（ASP3側トレースが実は不完全だった）

`wifi_trace_dump()`のダンプ出力に`total=`ヘッダ行自体が欠落したり，
「**469 messages are lost**」という損失通知が出現するケースを複数回
確認した．一方NuttX側の同等ログには損失通知が皆無だった．これは
ASP3側のsyslog経由の詳細トレースダンプが，**スキャン処理自体が
生成する大量の診断出力（WIFI_EVENT等）とキューを奪い合い，
バーストで大量に欠落する**という，本調査と無関係な既知の問題
（`docs/wifi-shim-c6.md`内の「TNUM_LOGPAR=6」問題とは別）による
ものであり，「一度も呼ばれない」という結論はこの**欠落の結果生じた
見せかけ**だったと判明した．

### 修正：JTAGによる呼出し回数カウンタの直接読み出し（serial非依存）

シリアル/syslog経由の詳細ダンプが信頼できないため，`wifi_trace.c`に
ID別の呼出し回数だけを保持する軽量カウンタ配列
（`wifi_tr_count[40]`，`WIFI_TRACE_MAXID=40`）を追加し，OpenOCD
（`adapter serial <MAC>`でC6実機を明示指定）経由で
`mdw <wifi_tr_countのアドレス> 40`により直接メモリダンプで読み出した
（シリアル出力を一切経由しないため，バースト・ロスの影響を受けない）．

**ASP3実機（1スキャンサイクル，JTAG直読み）**：

| 関数 | 回数 |
|---|---|
| register_chipv7_phy | 1 |
| chip_v7_set_chan_ana | 10 |
| set_channel_rfpll_freq | 27 |
| set_rfpll_freq | 3 |
| enable_agc | 17 |
| disable_agc | 16 |
| mac_enable_bb | 2 |
| fe_reg_init | 1 |
| phy_bbpll_cal | 34 |
| set_rxclk_en | 54 |
| set_txclk_en | 68 |
| i2cmst_reg_init | 1 |
| rxiq_cal_init | 1 |
| set_rx_gain_cal_dc_new | 3 |

**NuttX（同一`--wrap`シンボル，serial経由．`register_chipv7_phy`が
2回＝2スキャンサイクル分が累積）との比率**：

`register_chipv7_phy`・`chip_v7_set_chan_ana`・`set_rfpll_freq`・
`mac_enable_bb`・`fe_reg_init`・`i2cmst_reg_init`・`rxiq_cal_init`・
`set_rx_gain_cal_dc_new`は**全て正確に2.00倍**（＝1スキャンサイクル
あたりで完全一致）．一方`set_channel_rfpll_freq`（1.48倍）・
`enable_agc`（1.18倍）・`disable_agc`（1.12倍）・`phy_bbpll_cal`
（1.18倍）・`set_rxclk_en`（1.48倍）・`set_txclk_en`（1.59倍）は
チャネル毎に発火する関数であり，各実行で捕捉したチャネル数の違いに
起因すると考えられる非整数比（=実データとして無矛盾）．

**結論**：39個の`--wrap`対象シンボル全てについて，ROM常駐PHY関数の
呼出し有無・回数はASP3・NuttXで（1初期化サイクルあたり）**完全に
一致**する．実施15〜18で確立した「ソフトウェア呼出しグラフは一致」
という所見は，ROM常駐PHY内部の較正・RF-PLL・regi2c周辺関数という
より深い粒度まで拡張して再確認された．**先に報告した
「`chip_v7_set_chan_ana`がASP3で一度も呼ばれない」という発見は誤りであり，
撤回する．**

### 生存する結論：実施19のPHY/AGCレジスタ凍結のみが唯一の確定済み乖離

実施19のレジスタスナップショット（`agc_spot`・`phy_agc_sum`）は
**JTAG不要・regsnap全11エントリが完全に出力**されており（バースト・
ロスの影響を受けていない），本ラウンドの訂正によって揺らがない．
現時点でこの調査全体を通じて確定している乖離は依然として
**実施19の1件のみ**：ソフトウェア呼出しグラフ（引数・戻り値・
呼出し回数を含め，ROM常駐PHY関数まで含めて）は完全に一致するが，
PHY/AGCレジスタの実ハードウェア挙動（ASP3では凍結・NuttXでは
チャネル毎に変動）だけが異なる．

### 方法論上の教訓：syslogベースの「一度も発火しない」という結論は要検証

本調査で**2回目**の「不完全な観測に基づく誤った発見」が発生した
（1回目は実施10の`CONFIG_IDF_TARGET_ESP32C6`「ABIバグ」誤認）．
syslogトレースダンプは高負荷時（スキャン処理自体の診断出力と
競合時）にバーストで大量に行を欠落させることがあり，`total=`
ヘッダ行や個々のエントリが無警告で消えることがある．**「trace上に
出現しない＝実際に呼ばれていない」と断定する前に，ロスレスな
経路（本件ではJTAGによるメモリ直読み，カウンタ配列方式）で
裏付けを取ること．**

### 優先度1（標準ブートローダA/B）：フィージビリティ調査の結果と設計上の分岐点

実行はせず，フィージビリティのみ調査した．`/home/honda/tools/esp-idf`
に本物のESP-IDF v5.5一式（`idf.py`含む）が存在し，2ndステージ
ブートローダの`call_start_cpu0()`（`components/bootloader/subproject/
main/bootloader_start.c`）には`bootloader_after_init()`という
**weak関数フック**が用意されている（`custom_bootloader`example向け）．
これを使えば，本物のブートローダの`bootloader_init()`（クロック／PMU
／regi2c常時オン設定等の実ハードウェア初期化）を実行させた直後に，
パーティションテーブル読込み等（ASP3のDirect Bootイメージ形式とは
非互換）を一切経由せず，直接ASP3のエントリポイントへ分岐させる
ことが，原理的には比較的小さな変更で可能に見えた．

しかし，**`bootloader_init()`完了後のflash cache/MMUマッピングは
ブートローダ自身のレイアウト向けに設定されており，ASP3のDirect
Boot（flash先頭から直接XIP実行）が前提とするマッピングとは異なる**．
フック経由でASP3のXIPエントリへ単純分岐すると，誤った命令列を
フェッチする可能性が高い．よって優先度1には設計上の分岐がある：

- (a) フック内でDirect Boot相当のcache/MMUマッピングを追加で
  再現してから分岐する（比較的小さな変更に見えるが，状態不整合の
  デバッグに時間を要するリスクあり）．
- (b) ASP3のビルド出力を正式な`esp_image_header_t`形式のapp imageに
  変換し，実パーティションテーブル経由で本物のブートローダに正規に
  ロードさせる（手戻りは大きいが，クリーンで決定的）．

どちらを取るかはコーディネータ／セカンドオピニオンへの申し送り事項
とし，本ラウンドでは実行していない．

### 変更ファイル

- `asp3/target/esp32c6_espidf/wifi/wifi_trace.c`／`.h`：ROM常駐PHY
  関数18個の`--wrap`トレース追加（永続インフラとして維持）．
  `wifi_regsnap_t`に`phy_param_flags`フィールド追加．軽量呼出し
  カウンタ（`wifi_tr_count[40]`・`wifi_trace_dump_counts()`）追加．
- `asp3/target/esp32c6_espidf/esp_wifi.cmake`：対応する
  `-Wl,--wrap=`フラグ18行追加．
- `apps/wifi_scan/wifi_scan.c`：一時的な`phy_param+164`単発ピーク
  （regsnapに統合されたため冗長）を追加後にrevert．
- NuttX側（`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c6/`．
  job-scratch，asp3_coreリポジトリ外）：同一の18シンボル`--wrap`と
  `phy_param_flags`フィールドを追加．

### 検証

- ASP3実機（`/dev/ttyACM0`）：cmake buildクリーン，`test_porting`等の
  回帰は本ラウンドでは未実行（Wi-Fi診断専用ビルドのため対象外）．
  JTAGによる`wifi_tr_count[40]`直読み（`mdw 0x40832e88 40`）で
  上記表の値を確認．
- NuttX（同一実機で入替えフラッシュ）：serial経由のトレースダンプ
  （損失通知なし）で確認．
