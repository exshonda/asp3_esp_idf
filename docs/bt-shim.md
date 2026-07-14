# Bluetooth（BLE）統合（Phase D）

## 課題

Wi-Fi統合（Phase B/C）と同じ「esp-hal-3rdparty方式＋os_adapter shim」を
Bluetooth（BLE．ESP32-C3はBT 5 LEのみ＝BR/EDR非搭載）へ展開する。
設計の全体計画はasp3_coreの`docs/dev/esp-idf-integration.md`「Phase D」
参照。本ドキュメントはPhase D-1（コントローラ起動＋VHCIループバック）
の実装詳細・実施結果を記録する。

## Wi-Fiとの本質的な違い

Wi-Fi blobは関数ポインタテーブル（`wifi_osi_funcs_t`）越しにOS依存部を
差し替える設計だったが，BTコントローラ本体
（`hal/components/bt/controller/esp32c3/bt.c`．封印済みblobではなく
ソース配布）は`freertos/FreeRTOS.h`等を直接includeしFreeRTOS APIを
**インラインで直接呼ぶ**。そのため「osi関数テーブルを実装する」のでは
なく「**freertos/\*.hヘッダ自体をASP3向けにシムする**」という一段階
違うアプローチを取った（実体は同じ`wifi/esp_shim.c`プリミティブへの
委譲．新しいプリミティブの発明はしていない）。

## 設計方針

- `asp3/target/esp32c3_espidf/bt/stub/include/freertos/{FreeRTOS,task,
  queue,semphr,portmacro}.h`：bt.cが実測で呼ぶ全19種のFreeRTOS API
  （`xTaskCreatePinnedToCore`／`xQueueCreate`／`xQueueSend(FromISR)`／
  `xQueueReceive(FromISR)`／`xSemaphoreCreateCounting`／
  `xSemaphoreCreateMutex`／`xSemaphoreTake(FromISR)`／
  `xSemaphoreGive(FromISR)`／`vQueueDelete`／`vSemaphoreDelete`／
  `vTaskDelete`／`portENTER/EXIT_CRITICAL`各種／`xPortInIsrContext`／
  `vPortYield`／`portYIELD_FROM_ISR`）を`wifi/esp_shim.c`の既存
  プリミティブ（`esp_shim_queue_*`／`esp_shim_sem_*`／
  `esp_shim_task_create`／`esp_shim_int_disable/restore`）へ委譲する
  static inline関数として実装。
  - `xSemaphoreCreateMutex`と`xSemaphoreCreateCounting`は同じ
    `esp_shim_sem_create()`に統一（FreeRTOSのSemaphoreHandle_tは生成元を
    問わず同じ型でtake/give/deleteされるため型分岐が不要．非再帰
    ミューテックスとしての用途には十分）。
  - `vPortYield`はASP3の`rot_rdq(TPRI_SELF)`（ラウンドロビン）へ委譲。
- `asp3/target/esp32c3_espidf/bt/bt_shim.c`：freertos以外の依存
  （`esp_timer_*`／`esp_pm_lock_*`／`esp_ipc_call_blocking`／
  `esp_partition_*`／`esp_intr_alloc/free/enable/disable`／
  `esp_random`）をまとめて実装。
  - `esp_timer_*`：`esp_shim.c`のets_timer機構と同じ設計（専用タイマ
    タスク＋固定プールの期限走査）を`esp_timer_handle_t`API向けに
    新規実装（プールは4個．BT本体の実測要求は1個のみ）。
  - `esp_pm_lock_*`／`esp_ipc_call_blocking`：Wi-Fi同様の電源管理
    無効化方針でno-op／単一コア前提の同期直接呼出し。
  - `esp_partition_*`：Wi-FiのNVSスタブと同じ「常に存在しない」方針
    （`find_first`はNULLを返す．該当コードパスは
    `CONFIG_BT_CTRL_LE_LOG_STORAGE_EN`未定義のため実行時未到達）。
  - `esp_intr_alloc/free/enable/disable`：`esp_wifi_adapter.c`の
    `set_intr_wrapper`と同じINTMTXレジスタ直接操作＋
    `esp_shim_set_isr()`を，標準API形状（`esp_intr_alloc()`一発呼出し）
    に合わせて実装。BTコントローラが要求するISRソースは1個のみのため，
    固定でCPU割込み線1（Wi-Fi無効時のみ空き）に割り当てる単発実装。
- `wifi/esp_shim.c`・`wifi/esp_shim_libc.c`：Wi-Fi専用だった
  `psa/crypto.h`依存（WPA2ハンドシェイク専用）を`TOPPERS_ESP32C3_WIFI`
  ガードで除外し，BT単体ビルドでもリンクできるようにした。
  `heap_caps_calloc`（`esp_shim_calloc`へ委譲）を追加。
- `target.cmake`：`wifi/esp_shim.*`（静的プール本体）を
  `ESP32C3_WIFI`単独ゲートから`(ESP32C3_WIFI OR ESP32C3_BT)`の共有
  ブロックへ分離。Wi-Fi固有のosi/coex/eventアダプタ層
  （`esp_wifi_adapter.c`等）は従来通り`ESP32C3_WIFI`限定。
- `esp_bt.cmake`（新規．`esp_wifi.cmake`と同じ構造）：BTコントローラ
  本体・PHY実ソース（`phy_init.c`／`phy_common.c`／
  `phy_init_data.c`／`lib_printf.c`．Wi-Fiと共有の無線ハードウェアの
  ため必要）・クロック/ペリフェラル実ソース（`esp_clk.c`／
  `rtc_clk.c`／`periph_ctrl.c`）・coexアダプタ（`esp_coex_adapter.c`．
  Wi-Fiと共有）をまとめる。ROM関数ldはBT固有分
  （`eco3_bt_funcs.ld`／`bt_funcs.ld`）を追加。リンクライブラリは
  `btdm_app`（コントローラ本体blob）・`phy`・`coexist`・`btbb`
  （BLEベースバンド．`coex_pti_v2`等はROM関数ではなくこちらに実体）。

## スコープ

- Phase D-1（本ドキュメント）：コントローラ起動＋VHCIループバック。
  ホストスタック（NimBLE）は対象外。
- 対応：`ESP32C3_BT`オプション（既定OFF）。`ESP32C3_WIFI`との同時ONは
  RAM予算の理由で現状未対応（`target.cmake`でFATAL_ERROR）。
- 未対応（Phase D-1の既知の残課題）：後述「実施結果」参照。

## 実施結果

### ビルド成功（2026-07-04）

`apps/bt_smoke`（`esp_bt_controller_init`→`enable`→VHCI受信コールバック
登録→HCI Resetコマンド送信のスモークテスト）を`ESP32C3_WIFI=OFF
ESP32C3_BT=ON`でビルドし，コンパイル・リンクとも0エラーで成功。

RAM使用率（`--print-memory-usage`．320KB中）：**84.78%**（277800B）。
Wi-Fi単体ビルド（88.58%）より低く，Wi-Fi＋lwIP＋BSDソケット
ビルド（95.9%）とは同時に載せない前提（`target.cmake`で強制）。

既存ビルド（`wifi_scan`／`wifi_dhcp`／`tcp_socket_echo`／
`tcp_socket_client`／`udp_socket_echo`）への回帰なし（共有ファイル
`wifi/esp_shim.c`・`esp_shim_libc.c`・`hal_stub/include/platform/os.h`・
`target.cmake`を変更したため全件再確認）。

### 実機起動確認・既知のブロッカー（2026-07-04）

実機（ESP32-C3 rev v0.4）で`asp_flash.bin`を書き込み起動：

```
TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C3
System logging task is started on port 1.
rtc_clk(warn): invalid RTC_XTAL_FREQ_REG value, assume 40MHz
bt_smoke: esp_bt_controller_init
I (19) BLE_INIT: Feature Con...
esp_shim: task 'btController' -> tskid 1 (prio 23)
I (23) BLE_INIT: ...
esp_shim: set_isr intno=1 handler=...
bt_smoke: esp_bt_controller_enable(BLE)
I (31) phy_init: ...
BLE assert emi.c 164, param 00000000 00001000
```

**到達した地点**：`esp_bt_controller_init()`成功（コントローラタスク
起動・BLE_INITログ出力・割込み登録まで機能）。`esp_bt_controller_
enable(BLE)`もPHY初期化まで進行。

**ブロッカー**：`esp_bt_controller_enable()`の途中でコントローラ内部
（`libbtdm_app.a`．ファイル名`emi.c`）のアサートに到達し停止
（`param 00000000 00001000`）。原因未特定。VHCIループバック（HCI
Reset送信・Command Complete受信）には未到達。

（`rtc_clk`警告は無害．Direct BootがESP-IDF標準のクロック初期化
シーケンスをスキップするため`RTC_XTAL_FREQ_REG`未較正のままとなり，
`esp_clk.c`がフォールバック値40MHz＝実際の値と一致で継続する仕様通り
の動作．Phase A実機結果の40MHz XTAL確認と整合）。

### `emi.c:164`アサートの実機JTAG調査（2026-07-04）

Wi-Fi統合時と同じ手法（`openocd-esp32/v0.12.0-esp32-20250422`＋
`riscv32-esp-elf-gdb`，`esp32c3-builtin.cfg`）で実機調査を実施。

**調査の経緯**：

1. `bt_bt_controller_config_t`の手動構築値をKconfig既定値と照合し，
   2件の不一致を修正（`cfg_mask`＝0→1＝`CFG_MASK_BIT_SCAN_DUPLICATE_
   OPTION`，`mesh_adv_size`＝100→0＝メッシュ重複排除未使用時の既定値，
   ついでに`txpwr_dft`＝0→11＝P9規定値）。**実機再テストの結果，
   アサートに変化なし＝この2件は原因ではなかった**（が既存の潜在バグ
   ではあるため修正は維持）。
2. JTAG接続時，OpenOCDのESP-IDF RTOS-awareness層がFreeRTOSシンボル
   不在で`continue`のたびに再接続ループに陥る問題に遭遇。
   `-c "set _RTOS hwthread"`で回避（Wi-Fi統合時のドキュメントに
   この設定の記載はなく，今回新たに判明）。
3. ハードウェアブレークポイント（`hbreak`，ソフトウェアブレーク
   `break`はflashマップ領域で無効だった）で`malloc_internal_wrapper`
   の全呼び出し（size引数）をトレース。33回目の呼び出し前にアサート
   ヒット＝`_malloc_internal`経由の失敗ではないと判明。
4. アサート発生アドレス（ROM内`r_assert_param`＝`0x40000b30`）に
   到達した瞬間の`ra`レジスタから直接の呼び出し元を特定：
   **`r_emi_get_mem_addr_by_offset+166`**（ROM関数．
   `esp-rom-elfs/20241011/esp32c3_rev3_rom.elf`のシンボルテーブルで
   解決）。同ROM関数を逆アセンブルし，`emi.c:164`の実体を特定：
   オフセット（今回は`0x1000`＝EM領域の5番目の1KBページ）に対応する
   ページ記述子テーブルの「実際の領域タイプ由来値」と「テーブルに
   格納された期待値」を比較し，不一致ならアサートするコード
   （`param1=0`＝実際値0，`param2=0x1000`＝格納値4096）。
5. `esp_shim_malloc`＝`malloc`（tail-call）である点を確認し，
   `_malloc`（`osi_funcs_ro._malloc = malloc`）と`_malloc_internal`
   （`= malloc_internal_wrapper`）が同じアロケータへ委譲することを
   確認。また`esp_shim_malloc`失敗時は必ず`"esp_shim: malloc(%u)
   failed"`をsyslog出力するが，実機ログに一度も出現しない＝
   **ヒープ枯渇／確保失敗ではない**と確認（192KB中実使用量は
   累積約25KB程度と試算，十分な空きがある）。
6. `r_emi_em_base_init`の全体を逆アセンブルし，7回の内部malloc
   （328/1080/616/200/1260/540/750バイト，すべて成功を実機トレースで
   確認）と，EMアクティビティスロット用MMIOレジスタテーブル
   （`0x60031220`起点，10エントリ）の初期化ループを確認。いずれも
   正常完了しており，ここではアサートに至らない。
7. **決定的な切り分け実験**：`cfg.ble_max_act`を6→1に変更して
   再ビルド・実機再テスト。**結果は完全に同一**
   （`param 00000000 00001000`）＝失敗するオフセット（ページ4＝
   0x1000）は`ble_max_act`（アクティビティ数）に非依存の固定値。
   活動数不足によるEM領域サイズ不足という仮説は否定された。

### 検討・却下した仮説（再調査の重複を避けるため記録）

以下の2件は追加の調査依頼で挙がった具体的な仮説だが，実機検証の
結果いずれも`emi.c:164`の原因ではないと確認済み。再調査は不要。

**仮説A：`CONFIG_IDF_TARGET_ESP32C3`未定義により`periph_module_
enable(PERIPH_BT_MODULE)`がno-opになっている（クロック未供給／
リセット未解除）**

`esp_bt_controller_init()`が呼ぶ`periph_module_enable()`の実体
（`hal/components/esp_hw_support/periph_ctrl.c`）は
`__PERIPH_CTRL_ALLOW_LEGACY_API`が定義されている場合のみ実装を持ち，
そのマクロは`CONFIG_IDF_TARGET_ESP32C3`定義時のみ有効になる
（`esp_private/periph_ctrl.h`）。この定義が本ビルドの`*.cmake`・
`hal_stub/include/*.h`のいずれにも見当たらないとの指摘があった。

実機テスト前に静的検証で却下：`hal/nuttx/esp32c3/include/
sdkconfig.h`（NuttX由来のベンダーツリー内ファイル．探索対象外の
パスにあった）が`#define CONFIG_IDF_TARGET_ESP32C3 1`を既に持ち，
`bt.c`・`periph_ctrl.h`とも`#include "sdkconfig.h"`で到達済み
（インクルードパスに`hal/nuttx/esp32c3/include`が既存）。実際の
ビルドコマンドで前処理・`nm`確認した結果，`periph_module_enable`は
既に実体を持ち（`hal/clk_gate_ll.h`の`periph_ll_enable_clk_clear_
rst()`経由でレジスタ操作するコードが既にリンクされている），
このマクロを追加しても差分ゼロ＝仮説A却下（実機再テスト不要なほど
静的に決着）。

**仮説B：ESP-IDF本家がリンクする7個のROM linker script
（`ble_master/ble_50/ble_cca/ble_smp/ble_dtm/ble_test/ble_scan.ld`）
の欠落により，ROM機能ハンドラテーブルの一部が未初期化のまま残り，
それが`emi.c:164`のページ所有権チェック不整合を招いている**

実機で二分探索検証（各組み合わせをビルド→フラッシュ→実機ログ確認）：

| 組み合わせ | 結果 |
|---|---|
| 7個全部 | **新しい別のクラッシュ**：`esp_bt_controller_init()`内部（`enable()`到達前）で`r_rwip_init+256`からNULL関数ポインタ呼び出し＝Illegal instruction（`pc=0`） |
| `ble_master`+`ble_50`+`ble_cca`+`ble_smp` | 同上（新クラッシュ再現） |
| `ble_master`+`ble_50` | 元の`emi.c:164`アサートに復帰（変化なし） |
| `ble_master`+`ble_50`+`ble_cca` | 同上（変化なし） |
| 7個中`ble_smp`以外の6個 | 同上（変化なし） |

→ **`ble_smp.ld`単体が新規クラッシュの原因**（`r_rwip_init`が本来
非NULLを期待する関数ポインタを，このファイルが明示的に0で埋めて
しまうため）。残り6個（`ble_master`/`ble_50`/`ble_cca`/`ble_dtm`/
`ble_test`/`ble_scan`）は追加しても`emi.c:164`アサートに一切変化
なし＝無害だが無意味。**仮説Bは却下**：7個いずれの追加も`emi.c:164`
を解消しない。`esp_bt.cmake`は元の状態（`eco3_bt_funcs.ld`＋
`bt_funcs.ld`のみ）に戻した。

**現時点の結論**：`emi.c:164`は`libbtdm_app.a`（ROM側`r_emi_get_
mem_addr_by_offset`含む）内部の「EMページ所有権テーブル」の整合性
チェックであり，オフセット0x1000（ページ4）に対応するテーブル
エントリが，コントローラ初期化シーケンスのどこかで正しく登録されて
いない。この登録処理自体はソースが公開されていない`libbtdm_app.a`
（およびROM）の内部実装であり，`bt_smoke.c`側のconfig値（Kconfig
既定値と一致を確認済み）・アクティビティ数・`periph_module_enable`
の有効性・追加ROM linker scriptの7個いずれにも起因しないことが
実機検証で確認できた。

**未解決／残作業**：
- 真因は「実際のESP-IDFが`esp_bt_controller_init/enable`前後で
  内部的に呼んでいる別の初期化ステップ（PHY較正・NVSキャリブレー
  ションデータ読込等）が，本shim実装の`bt_smoke.c`シーケンスには
  無い」可能性が高いが，`libbtdm_app.a`はクローズドソースのため
  ソースレベルでの裏付けは取れない。real ESP-IDF環境での同一
  configによる比較実行ができれば切り分けが進む可能性がある。
- ページ4（オフセット0x1000）のテーブルエントリをどの関数が
  設定するはずかを特定するには，`r_emi_init`／`r_lld_core_init`等
  さらに深いROM関数群の逐次トレースが必要（本セッションでは
  時間対効果の観点で未実施）。
- 原因解決後，HCI Resetコマンド送信→Command Complete受信の確認へ
  進む（Phase D-1の完了基準，未達）。
- Phase D-2（NimBLEホスト統合）はPhase D-1完了後に着手。

### emi.c:164 真因特定 — 2ボードJTAG調査（2026-07-08）

依頼`docs/bt-emi-2board-request.md`に基づき，被験機B（M5Stamp C3U，
rev v0.3，MAC `60:55:F9:57:C2:60`）に対する実機JTAGで真因を特定した。
基準機A（NuttX同一blob）の突合は，Bのみの解析で真因まで到達できたため
本セッションでは未使用（下記「残作業」参照）。

#### 実機環境・手順（確立した落とし穴と回避策）

- **OpenOCD版上げ完了**：`openocd-esp32 v0.12.0-esp32-20260703`
  （`/home/honda/tools/espressif/tools/openocd-esp32/...`）。旧dev版
  （`/usr/local/bin/openocd`）より安定。**非標準ポート**を明示
  （B: `-c "gdb port 13334" -c "telnet port 14445" -c "tcl port 16667"`）。
  Direct Bootイメージはflashマップを持たないため**gdb接続はflash
  auto-probe失敗で拒否される**（`Error: attempted 'gdb' connection
  rejected`）。回避策としてgdbを使わず**OpenOCDネイティブコマンド
  （`bp`/`wp`/`mdw`/`mww`/`mmw`/`reg`/`step`）で全調査を実施**した
  （`-c "set _RTOS hwthread"`はFreeRTOSシンボル不在の再接続ループ回避に
  従来通り必要）。
- **USB-Serial/JTAG起動の罠**：`esptool ... write-flash`後の
  「Hard resetting via RTS pin」や生のDTR/RTSトグルは，このボードでは
  **ダウンロードモード（ROM `UartConnCheck`/`ets_delay_us`で無限待機，
  PC≈`0x400462dc`）にラッチされ，アプリがDirect Bootしない**。
  RTC_CNTL_OPTION1/STOREクリアでは解けない（USB-Serial/JTAG側のラッチ）。
  **回避策＝`esptool --before usb-reset --after watchdog-reset --no-stub
  flash-id` でアプリ起動**（watchdogリセットがラッチを消費）。以後は
  OpenOCDの`reset halt`→`resume`でもアプリがDirect Bootするようになり，
  `bp`/`wp`が使える。書込み自体は`--no-stub write-flash`が確実
  （stub flasherは`no response`で失敗する）。

#### emi.c:164 アサートの完全解読（ROM逆アセンブル＋実機確認）

`esp32c3_rev3_rom.elf`と`build/*/asp.elf`の逆アセンブル，および実機
ブレークで確定：

- 実アサート関数は**`r_rwip_assert_hack`（flash `0x4201f602`）**。
  `r_assert_param`（ROM `0x40002d70`）経由で呼ばれる。実機ブレーク時の
  引数＝`a0`=`"emi.c"`文字列, `a1`=**164**, `a2`(param1)=**0**,
  `a3`(param2)=**0x1000** ＝ログの`BLE assert emi.c 164, param 00000000
  00001000`と完全一致。
- 直接の呼出し元は**ROM `r_emi_get_mem_addr_by_offset+166`（`0x40006a1c`）**。
  実機スタック追跡で判明した生きたコールチェーン（コントローラタスク）：
  `btdm_controller_task` → `r_rwip_reset` → `r_rwble_init` → `r_lld_init`
  → `r_lld_core_init(+622)` → `r_emi_em_base_init`/getter → assert。
- getterの検査ロジック：offset`0x1000`→ページ4→ROM定数表
  `em_base_reg_lut`（`0x3ff1f518`）の`[4]`＝`{region=3, 期待値=0x1000}`。
  MMIOレジスタ**`0x60031204 + region*4 = 0x60031210`**を読み，
  `(reg>>18)<<2 == 0x1000`（＝`reg`のbit[31:18]が`0x400`＝`reg`が
  `0x10000000`台）を要求。**実測`reg=0`**なので`param1=(0>>18)<<2=0`≠
  `0x1000`でアサート。
  （ページ0は期待値0で`reg=0`でもPASS＝先にアサートするのは
  期待値が非0の最初のページ＝ページ4。）

#### 決定的実験の結果 → 真因＝「BBレジスタブロックが未クロック（書込み不可）」

すべて実機JTAG（`wp`/`bp`/`mww`）で確認。判別は依頼の①未書込み／②消去の
うち**①（そもそも書けていない）に確定**したうえで，さらに「なぜ書けない
か」まで踏み込んだ：

1. **`wp 0x60031210 4 w`（書込み監視）はアサート到達まで一度も発火せず**
   （2s窓でシーケンス全体を包含：終端は`_kernel_target_exit`
   `0x420066aa`＝アサート後にカーネル終了に至る）。監視機構自体は
   `dbg_assert_block`書込みで発火検証済み＝**真の陰性**。
2. しかし`r_emi_em_base_init`（flash `0x420080d8`）は**動作しており**，
   各region基底レジスタへストア`sw s1,0(a4)`（`0x4200814a`, `a4=0x60031204`）
   を**実行している**（`wp 0x60031204`が`0x4200814a`で発火）。
   ＝CPUはストア命令を実行しているのに，`0x60031210`監視が発火しない
   理由は「そのregionまで到達する前」ではなく，**ストアがバス側で
   ドロップされている**ため。
3. **決定打**：ストア命令で停止中（`bp 0x4200814a`, `a4=0x60031204`,
   `s1`=書くべき非0値）に**JTAG直接書込み`mww 0x60031204 0x000abcde`
   → 読み戻し`0x00000000`**。CPUストアもJTAGストアも効かない＝
   **`0x60031000`台（BLEベースバンド=BB のMMIO）が未クロック／
   非活性でライトを受け付けない**。よって`r_emi_em_base_init`の
   全ストアが無効化され，EM基底レジスタは0のまま→getterがアサート。

#### 却下した下位仮説（BB未クロックの原因として，実機で反証）

ストア停止点で各レジスタを操作→`0x60031204`の書込み可否を再検査した：

- **SYSCONクロック**：`SYSCON_WIFI_CLK_EN_REG`(`0x60026014`)は
  `0xff84e030`で**BT-common bits(`0x78078F`)がクリア**。手動で
  セット（→`0xfffce7bf`）しても書込み不可のまま。
  （`periph_ll_get_clk_en_mask(PERIPH_BT_MODULE)=SYSTEM_WIFI_CLK_BT_EN_M`は
  **値0＝no-op**というC3の癖。実クロックはWIFI_BT_COMMON側。）
- **BB/MACリセット**：`SYSCON_WIFI_RST_EN_REG`(`0x60026018`)=`0`＝
  リセット非アサート（`SYSTEM_BTBB_RST`等は既に解除済み）。
- **電源ドメイン／アイソレーション**：`RTC_CNTL_DIG_PWC`(`0x60008088`)の
  `BT_FORCE_PD`(bit11)/`WIFI_FORCE_PD`(bit17)，`RTC_CNTL_DIG_ISO`
  (`0x6000808c`)の`BT_FORCE_ISO`(bit22)/`WIFI_FORCE_ISO`(bit28)は
  **全て既に0**。手動クリアしても書込み不可のまま。
- **`esp_phy_enable`後のタイミング**：`btdm_controller_enable`入口
  （`0x4200722e`，`esp_phy_enable`完了直後）でも`0x60031204`は書込み不可。
- **`bt_bb_v2_init_cmplx`との順序**：`bp 0x4201dbda`(bt_bb_init)と
  `bp 0x4200814a`(em store)で**先に発火するのはem store**＝BBの
  最終初期化(`bt_bb_v2_init_cmplx`)より前にem_base_initが走るが，
  これは実ESP-IDFでも同一blobで同順のはずで，順序単独が原因とは
  言い切れない（BB機能クロックは本来`esp_phy_enable`側で確立される想定）。

**アサートは`esp_bt_controller_enable`ではなく`esp_bt_controller_init`
中に発生する（重要な絞り込み）**：
- 通常ビルドで`bp 0x42002ce2`(`esp_bt_controller_enable`入口)を張ると，
  **enableに到達する前に`_kernel_target_exit`(`0x420066aa`)へ落ちる**
  ボートがある＝アサートがenable呼出し前に起きている。
- 決定打＝**init-onlyプローブ**（`BT_PROBE_STOP_AFTER_INIT`：
  `esp_bt_controller_init`成功直後に無限spin，enableを一切呼ばない）で，
  リセット時に`dbg_assert_block`(`0x3fcdf9f8`)を0へクリア→3s実行後に
  **0→1へ変化**＝**enableを呼ばずともemi.c:164が発火**。よって
  `r_emi_em_base_init`（BBへ書込む）は**`esp_bt_controller_init`内**で
  走っている（コントローラタスク文脈）。
- `esp_phy_enable`/`esp_phy_load_cal_and_init`/`register_chipv7_phy`は
  アサート到達まで**一度も呼ばれない**（各`bp`が3s窓・kernel_exit到達
  までに0ヒット）。`esp_phy_enable`はenable時（`esp_bt_controller_enable`
  の先頭，`0x4200329a`）の呼出しであり，**init中のem_base_initには
  間に合わない**。init中に走る`esp_phy_modem_init`(`0x4200311e`)は本ビルド
  構成では実質空（retention/sleep専用でBBクロックを立てない）。

**RF/PHYクロック仮説は反証済み**：`bt_smoke.c`に`BT_PROBE_PHY_EARLY`
（`esp_bt_controller_init`の**前**に`esp_phy_enable(PHY_MODEM_BT)`を
明示呼出し）を追加して検証。実機で`bp 0x420031ba`(load_cal)/
`bp 0x42016872`(register_chipv7_phy)が**2ヒット＝RF/PHYは完全初期化
された**にもかかわらず，**BB(`0x60031204`)は依然書込み不可・
`dbg_assert_block`=1・EM基底レジスタは0のまま・emi.c:164も解消せず**。
＝**RF/PHYアナログクロックはBBレジスタアクセスのゲートではない**。

**現時点の真因結論（更新）**：`emi.c:164`は「EM基底レジスタが未初期化」
の表層で，真因は**BB(`0x60031000`台MMIO)が`r_emi_em_base_init`実行時点で
書込みを受け付けない**こと。ゲートは，デジタルSYSCONクロック／リセット
解除／電源ドメイン／アイソレーション／RF・PHYの**いずれでもない**。
残る最有力容疑は**BB内部イネーブル（`bt_bb_v2_init_cmplx`
`0x4201dbda`等，BB自身のレジスタで行う初期化）と実行順序**：
- `r_emi_em_base_init`のBBストア(`0x4200814a`)は**`esp_bt_controller_init`
  中**（コントローラタスク`r_rwip_reset`経由）に走る。
- 一方`bt_bb_v2_init_cmplx`は`esp_bt_controller_enable`側
  （`esp_phy_enable`→`bt_bb_v2_init_cmplx`→`coex_pti_v2`）で走る＝
  **em_base_initより後**（実機で`bp`先発火はem store）。
- ＝BBを使える状態にする`bt_bb_v2_init_cmplx`より前に，BBへ書く
  `em_base_init`が走ってしまう**順序矛盾**。real ESP-IDF/NuttXでは
  同一blobでこの順序が破綻しないはず＝**ASP3側でコントローラタスクが
  init中にreset/EMセットアップを先行実行している（enable信号を待って
  いない）同期差**が本命。

**同期バグ説は一部弱まった（要注意）**：shimのキュー受信は
`esp_shim_tick_to_tmo`が`portMAX_DELAY(0xffffffff)→TMO_FEVR`を正しく
返し，`trcv_dtq`で正常にブロックする（`wifi/esp_shim.c`）。よって
「コントローラタスクがenable信号を待たず暴走」という単純なキュー
バグではない公算。em_base_initは**`btdm_controller_init`内で同期的に
（blob設計通り）走っている**可能性が高い＝real ESP-IDFでもinit中に
走るが，そのときBBはアクセス可能，という差だと思われる。

**残る核心の問い**：**何がinit中にBB(`0x60031000`)のレジスタアクセスを
可能にするのか**。RF/PHY・SYSCONクロック(common含む)・BB/MACリセット・
電源ドメイン・アイソレーションは全て反証済み。残る細かな候補＝
BB自身のレジスタ空間内の「BBクロック/クロック源(PLL)選択」ビットや，
ROM関数が設定する隠れたイネーブル等。**基準機A（NuttX同一blob）で
`bp 0x4200814a`(em store)停止時に`0x60031204`が書込み可能であることを
確認し，その時点のBB周辺レジスタ全域(0x60030000〜0x60039000,
modem/RF/SYSCON)をBと差分**するのが，欠落イネーブルを一意特定する
最短経路。修正は`bt_shim.c`/`esp_bt.cmake`側（submodule不可）で
その欠落レジスタ設定を`esp_bt_controller_init`前に補完する想定。
**本セッションでは修正実装・enable成功・VHCI往復には未到達**。

（ボート間ばらつき注意：アサート後PCは`_kernel_target_exit`
(`0x420066aa`)／`dbg_assert_block`spin／`dispatcher_2`(`0x42003c9c`)で
揺れる＝アサート後のスケジューリング差。コア所見＝BB未クロックは
複数手法で一致確認済み。`feedback_hardware_investigation_rigor.md`。）

#### 残作業（真因の最終確定＝欠落レジスタの特定）

- **基準機A（NuttX同一blob）でのA/B突合が最短**：NuttX-C3-BLE
  （enable成功構成）を基準機Aへ書き込み，**同じ`bp 0x4200814a`
  （em_base_init store）で停止し，`0x60031204`の書込み可否・`CLK_EN`
  （`0x60026014`）・RF/PLL関連レジスタを採取**して被験機Bと差分を取れば，
  「Bで欠けているBBクロック確立ステップ」が一意に判明する見込み。
  （NuttXビルドが難航する場合は，B単独で`esp_phy_load_cal_and_init`/
  `register_chipv7_phy`のブレークトレースとRF較正レジスタの精査で
  代替。）
- 真因（欠落クロック確立）が判明したら，`bt_shim.c`/`esp_bt.cmake`側
  （target/シム側。submoduleは触らない）でその初期化を補完し，
  `esp_bt_controller_enable(BLE)`成功→HCI Reset往復（VHCIループバック
  ＝Phase D-1完了基準）を確認する。**本セッションではenable成功／VHCI
  往復には未到達**。

#### 調査用プローブ（apps/bt_smoke）

`apps/bt_smoke/bt_smoke.c`に`#ifdef BT_PROBE_STOP_AFTER_INIT`ガードの
停止点を追加（`esp_bt_controller_init`成功直後で無限`tslp_tsk`）。通常
ビルドでは未定義＝無影響。プローブビルドは
`-DCMAKE_C_FLAGS=-DBT_PROBE_STOP_AFTER_INIT`で有効化（別ビルドディレクトリ
推奨）。em_base_init前後のBBレジスタ状態採取に使用。

#### S3移植向けメモ

- **チップ非依存（S3流用可能）**：
  - 真因の性質＝「Direct Bootが確立しないBB機能クロックにより，
    `r_emi_em_base_init`のBBレジスタ書込みが無効化される」という
    構造は，S3も同一系統blob（`libbtdm_app.a`）・同一`bt.c`のため
    ほぼ同様に起きる公算大。修正＝enable前にBBクロック確立を保証する
    シーケンス補完，という方針はチップ非依存。
  - `esp_bt_controller_enable`のフロー（`esp_phy_enable`→
    `btdm_controller_enable`→`bt_bb_v2_init_cmplx`）はS3も共通。
  - USB-Serial/JTAG起動の`watchdog-reset`回避策・OpenOCDネイティブ
    コマンド運用・`_RTOS hwthread`は，S3のnative USBボードでも有効。
- **C3/S3で差が出る（要読替え）**：
  - **ROM実アドレスは全て別物**：`r_emi_get_mem_addr_by_offset`,
    `r_assert_param`, `em_base_reg_lut`（`0x3ff1f518`）等はS3 ROMで
    再解決が必要。EM基底レジスタのMMIOベースもS3で要確認
    （C3は`0x60031204`起点）。
  - blobは別ファイル（C3=`dfdadb9d…`／S3=`24e347ce…`）＝flash側の
    `r_emi_em_base_init`等のアドレスも別。
  - BBクロック/電源のレジスタ（`SYSCON_WIFI_CLK_EN`/`RTC_CNTL_DIG_PWC`
    /modem clock）はS3で名称・オフセット・ビット配置が異なる
    （S3はデュアルコア・modem_clock構成が別）。
  - **S3実機での再現有無の最終確認は本依頼範囲外＝別途必須**（ROMが
    別物のため，C3修正がそのまま効くかはS3実機で検証すること）。

### 「コントローラタスク先走り＝シム同期不全」仮説の検証（2026-07-08 追検証）

最有力容疑（コントローラタスクが`enable`を待たず`r_emi_em_base_init`を
先走り実行＝シムのqueue/sem待ちが効かず素通り）を実機＋ソースで検証。
**結論：この同期不全仮説は反証。** ただし「em_base_initがinit中に走る」
現象自体は事実で，それはblob設計通り（下記）。

**検証結果**：
1. **em_base_initはコントローラタスク文脈でinit中に走る（enable前）**：
   通常ビルドで`bp 0x4200814a`(em store)と`bp 0x42002ce2`
   (`esp_bt_controller_enable`)を同時に張ると，有効試行の2/3で
   **em storeが先に発火**＝`esp_bt_controller_enable`呼出し前に
   em_base_initが走っている。em store停止時の`sp=0x3fcbf790`は
   **`_kernel_stack_SHIM_TSK3`(0x3fcbda50..0x3fcbfa50)内**＝
   コントローラタスク文脈（`_kernel_stack_MAIN_TASK`=0x3fcb3650ではない）。
2. **しかしシムのブロッキングは正しく実装されている**：
   `bt.c`の`semphr_take_wrapper`/`queue_recv_wrapper`は
   `block_time_ms==OSI_FUNCS_TIME_BLOCKING(0xffffffff)`のとき
   `portMAX_DELAY`を渡し，`esp_shim_sem_take`/`esp_shim_queue_recv`→
   `esp_shim_tick_to_tmo(0xffffffff)=TMO_FEVR`→`twai_sem`/`trcv_dtq`で
   **実ブロック**。戻り値極性も`E_OK?1:0`＝`pdTRUE=1`で正常。
3. **実機でタスクはビジースピンせずidleへ落ちる**：停止時PCが
   `dispatcher_2`(0x42003c9c＝ASP3スケジューラidle)に達する＝
   「recvが即リターンしてタスクが空転」してはいない＝ブロックは実効。
4. ＝「ブロックが即リターンして先走る」バグではない。
   **em_base_initがinit中にコントローラタスクで走るのは，blob(ESP-IDF)の
   設計＝`btdm_controller_init`が生成したコントローラタスク上で
   `rwip`初期化（EM資源確保含む）をinit時に実行する，という正規動作**と
   整合的（real ESP-IDFでも同様にinit時にem_base_initが走り，そのとき
   BBは活性という差）。

**よって真因は「init中にBBが活性化されていない」に戻る**。BBを活性化
する隠れたステップ（BB空間内クロック源/イネーブル，または
`bt_bb_v2_init_cmplx`相当のinit時実行）が，real ESP-IDF/NuttXにはあって
ASP3 Direct Bootには無い，という切り分けに帰着する。

**次段（申し送り）**：シム同期は白なので，**基準機A（NuttX同一blob）を
立てるフェーズへ移行**が必要。手順＝NuttX-C3-BLE（enable成功構成）を
基準機Aへ書込み，**同じ`bp 0x4200814a`(em store)停止時に`0x60031204`が
書込み可能であることを確認**し，その瞬間のBB周辺レジスタ全域
（0x60030000〜0x60039000／modem／RF／SYSCON `0x60026014`／
RTC `0x60008088/8c`）をBと差分。Bで欠けているBB活性化レジスタが
一意に判明する。判明後の修正は`bt_shim.c`/`esp_bt.cmake`側
（submodule不可）で`esp_bt_controller_init`前に補完。

（未解明の観測＝init-onlyプローブでの`dbg_assert_block`は2/3で1・1/3で0
と揺れる。当初watchdog-reset不発と解釈したが，後述のとおり真因は
**共有coex診断コードの間欠NULLコール**（別バグ）と判明。）

### 基準機A（NuttX同一blob）との2ボード差分 → 真因確定・修正（2026-07-08）

#### NuttX-C3-BLE基準機Aの構築
- 既存NuttXツリー（C6調査流用）をコピーし `esp32c3-devkit:ble`
  （`CONFIG_ESPRESSIF_BLE=y`，controller enableがboot時に自動実行）で
  ビルド。ツールチェーンは`riscv32-esp-elf-`（`CROSSDEV=`で上書き必須，
  system の riscv64 はnewlib欠落で不可）。**BT blob md5＝
  `dfdadb9ddc12eeeab85edfb5d26eb4bf` でboard Bと一致**（esp-hal-3rdparty
  同一pin commit `b90b1837`）＝比較妥当性OK。ビルド物
  `.../nuttx-c3ble/nuttx/nuttx.bin`（simple boot，@0x0単一イメージ）。
  基準機A（`60:55:F9:57:C9:88`）へ書込み，NuttXがboot時にem_base_init
  （`0x420076f0`＝NuttX側アドレス）へ到達しBBが活性であることを確認。

#### 決定的な2ボード差分（em_base_initストア停止時）
両機を同一の論理点（`r_emi_em_base_init`のBBストア．A=`0x420076f0`／
B=`0x4200814a`）で停止し，`0x60031204`の書込み可否と周辺レジスタを採取：

| 項目 | 基準機A（NuttX，成功） | 被験機B（ASP3，失敗） |
|---|---|---|
| `mww 0x60031204` 書込み | **可（読み戻し一致）** | 不可（読み戻し0） |
| BB領域 `0x60031000` | 非0（活性値） | 全0 |
| SYSCON CLK_EN `0x60026014` | **0xff87f850** | 0xff84e030 |
| RTC DIG_PWC `0x60008088` | 0x00000000 | 0x00555010 |
| RTC DIG_ISO `0x6000808c` | 0x00000080 | 0xaa805080 |

Bで上記3レジスタをAの値に強制設定→`0x60031204`が書込み可へ変化。
**3レジスタを個別bisect**した結果，**CLK_EN(`0x60026014`)単独で
書込み可へ変わる**（DIG_PWC/DIG_ISOは無関係）と確定。A/B差分ビット＝
**0x00031840（bit6,11,12,16,17）＝BLEベースバンドの機能クロック**。

#### 真因（確定）
**ASP3 Direct Bootが `esp_perip_clk_init()`（`esp_system/port/soc/esp32c3/
clk.c` の line318 `SET_PERI_REG_MASK(SYSTEM_WIFI_CLK_EN_REG,
SYSTEM_WIFI_CLK_EN)`）を通らないため，`SYSTEM_WIFI_CLK_EN`（=0x00FB9FCF）
に含まれるBLEベースバンドの機能クロックビットが未設定**。その結果，
`esp_bt_controller_init()`中にコントローラタスクが走らせる
`r_emi_em_base_init` のBBレジスタ書込み（`0x60031204+`）がバス側で
ドロップされ，EM基底レジスタが0のまま→`r_emi_get_mem_addr_by_offset`
が「BLE assert emi.c 164」に至る。実ESP-IDF/NuttXはブート時に
esp_perip_clk_initでこのクロックを立てるため発生しない。
実機で `SET_PERI_REG_MASK(0x60026014, 0x00fb9fcf)` を行うとBBが書込み可へ
変わることを両方向（A採取・B再現）で確認。

#### 修正（target/シム側．submodule不可を遵守）
- **`asp3/target/esp32c3_espidf/bt/bt_shim.c`**：`esp_shim_bt_clock_init()`
  を追加。`SYSCON_WIFI_CLK_EN_REG(0x60026014)` に **BBクロックの差分ビット
  `0x00031840`（bit6,11,12,16,17）のみ**をOR（`sil_rew_mem`/`sil_wrw_mem`）。
  当初は`SYSTEM_WIFI_CLK_EN`全体（0x00FB9FCF）をORしたが，それだと
  `CLK_EN=0xffffffff`となりWiFi/SDIO等の無関係クロックまで有効化され
  NuttXの正規値0xff87f850から乖離，`phy_init`が`phy_module_enable`到達前に
  クラッシュした。差分ビットのみ（→0xff87f870，NuttX 0xff87f850に近い）に
  絞ると`phy_init`が`register_chipv7_phy`まで前進する（下記）。
- **`apps/bt_smoke/bt_smoke.c`**：`esp_bt_controller_init()`の直前で
  `esp_shim_bt_clock_init()`を呼ぶ。

#### 検証結果
修正後のbt_smokeを被験機Bへ書込み実機起動（コンソール）：
```
bt_smoke: esp_bt_controller_init
I (24) BLE_INIT: Feature Con...
esp_shim: task 'btController' -> tskid 1 (prio 23)
bt_smoke: esp_bt_controller_enable(BLE)
I (34) phy_init:                 ← ここまで到達（emi.c:164は消滅）
```
**`BLE assert emi.c 164` は完全に消滅**し，`esp_bt_controller_enable`が
PHY初期化まで進行するようになった（従来はenable直後にemi.c:164で停止）。
＝**emi.c:164の真因特定・修正は完了**。

#### 残る新規ブロッカー（enable完走・VHCI往復は未達）
enableは`emi.c:164`を越えたが，**`phy_init`のRF較正（`register_chipv7_phy`）
でリセット/クラッシュ**し，enable完走（→VHCIループバック）には未到達。
- 進行状況（差分ビットのみの修正・JTAG bp命中で確認）：
  `esp_bt_controller_enable`(`0x42002ce6`)→`esp_phy_enable`(`0x420032b2`)
  →`phy_module_enable`(`0x4201e614`)→`esp_phy_load_cal_and_init`
  →**`register_chipv7_phy`(`0x42016872`)まで到達**。その後（RF較正の
  実処理中）にリセット（`rst:0x15 USB_UART_CHIP_RESET`，USB CDC切断）。
- ＝ブロッカーは**RF/PHY較正（`register_chipv7_phy`内のregi2c/RFレジスタ
  プログラミング）**。C6 Wi-Fi調査（`memory/project_c6_agc_investigation.md`，
  66ラウンドのRF/regi2c/deaf-RX）と同じ層で，**別フェーズの調査が必要**。

##### 追加調査（2026-07-08 続き）：s_ticks_per_us修正＋リセット種別確定

1. **`s_ticks_per_us`（ROMの遅延較正大域，`0x3fcdf64c`）を修正**（C6 実施48/49と同一機構）：
   実機で **B(ASP3)=`0x14`(20) / A(NuttX)=`0xa0`(160)** を確認。ASP3 Direct Bootは
   PLL160MHzへ切替えるが，その事実をROMへ通知する`esp_rom_set_cpu_ticks_per_us`
   を呼んでおらず，`esp_rom_delay_us(N)`が本来の**1/8**（N×20）しか待たない。
   → `asp3/target/esp32c3_espidf/target_kernel_impl.c`の`hardware_init_hook()`に
   `esp_rom_set_cpu_ticks_per_us(160)`を追加（無条件）。実機で`s_ticks_per_us=0xa0`へ
   修正されたことを確認。**RF較正の遅延精度に必須の修正だが，これ単独では
   リセットは解消しなかった**（リセットは別要因）。
2. **リセット種別＝チップ内部（ホスト起因ではない）と確定**：JTAGのみ接続
   （passiveコンソール読取り無し）でも，さらに**何も接続せず起動（bare run）
   →6s後にJTAG接続**しても，PCは`_kernel_start_r`(`0x42003d4c/50`)＝
   **リセットループ**，`hci_reset_done`=0（VHCI未完）。＝USB CDCのDTR/RTS揺れや
   再列挙ではなく，**RF較正中にチップが実際にリセットしている**
   （`rst:0x15 USB_UART_CHIP_RESET`）。
3. **A/Bクロック差分（`phy_module_enable`停止時）**：
   - `SYSTEM_PERIP_CLK_EN0_REG(0x600C0010)`：B=`0xf9c1e06f`（リセット既定）／
     A=`0x71806007`。NuttXは`esp_perip_clk_init`で不要ペリフェラルクロックを
     **CLEAR**するが，ASP3はORのみで未クリア＝Bに余分なビットが残る。
   - `SYSTEM_WIFI_CLK_EN(0x60026014)`：B=`0xffffffff`／A=`0xffffffdf`（bit5差）。
   - `SYSTEM_PERIP_CLK_EN1(0x600C0014)`はA/B一致（`0x200`）。
   - ただし**WIFI_CLK_EN bit5をJTAGでクリアしてもリセットは解消せず**＝
     単一ビットではない。
   - 真因候補：ASP3が`esp_perip_clk_init`の**CLEAR側**（不要クロック停止）と
     `SYSTEM_WIFI_CLK_EN`の**厳密値化**（NuttX 0xff87f850）を行っていないこと，
     またはRF較正が読むregi2c前提初期化/PHY較正データの欠落。
- 次段方針：同じ2ボード差分で`register_chipv7_phy`内のregi2c（block 0x6b/0x66/0x6d等，
  C6資産）・PHY較正データ（NVS/eFuse）・`esp_perip_clk_init`の完全再現
  （CLEAR含む）をA/B比較。**修正は`hardware_init_hook`で`esp_perip_clk_init`相当を
  より忠実に再現する方向**（ただしASP3自前のコンソール/タイマ用クロックを
  止めない配慮が必要）。C6のRF/regi2c知見が直結。

##### 追加調査（2026-07-08 さらに続き）：CLEAR側再現の反証＋WiFi回帰確認＋重要な再枠組み

1. **`esp_perip_clk_init`のCLEAR側再現はRF較正リセットを解消しない（反証）**：
   差分ビット`0x88418068`（B=リセット既定 has，A=clears）は WDG/UART1/SPI2-4/
   SPI-DMA/TIMG1 のみで，**ASP3が使うSYSTIMER(bit29)・USB-JTAG(USB_DEVICE bit23)は
   A値でも保持される**ことを静的に確認（安全）。実機で`esp_bt_controller_enable`
   入口にて `PERIP_CLK_EN0=0x71806007`＋`WIFI_CLK_EN=0xff87f850`（A値）を直接書いて
   継続→**依然リセット（`btdm_controller_enable`未到達）**。＝クロック差分は
   RF較正リセットの真因ではない。

2. **s_ticks修正はC3 WiFiを回帰させていない（Task 2）**：`wifi_scan`
   （`-DESP32C3_WIFI=ON`）をs_ticks修正**有り／無し両方**でビルド・実機起動。
   コンソール（USB-CDC passive read）では両方とも`esp_wifi_start -> 0`直後に
   `rst:0x15 USB_UART_CHIP_RESET`でリセット。**しかし何も接続しないbare run
   （esptoolでwatchdog-reset起動→CDCに触れず8s→JTAGでPC確認）ではPCが
   `dispatcher_2`(idle)＝リセットループではなくアプリが正常に走っている**。
   ＝**C3 WiFiのリセットはCDCアクセス（passive read）に誘発される計測
   アーチファクト**（このボードのUSB-Serial-JTAGがRF活動中のCDCアクセスで
   チップリセットを起こす）で，**s_ticks修正の回帰ではない**（有無で挙動同一）。

3. **★重要な再枠組み：BTのリセットはチップ内部（bare でも発生）だが，
   WiFiは bare で正常動作する**（同一ボード・同一`register_chipv7_phy`）：
   - BT bare run（s_ticks 有／無 両方）：PC=`_kernel_start_r`＝リセットループ，
     `hci_reset_done`=0。＝**BT固有のチップ内部リセット**。
   - WiFi bare run：PC=`dispatcher_2`(idle)＝正常。
   - ＝**WiFiは同一ボードで`register_chipv7_phy`（RF較正）を完走できる**＝
     RF較正自体・ボード・電源は問題なし。BTだけがRF較正中にリセットする＝
     **BT固有の初期化差**（BT enable経路の`esp_phy_enable`前後の環境設定が，
     esp_wifi_start経路と異なる）。
   - **次段の最有力アプローチ：同一ボードで「動くWiFiのphy_init経路」と
     「落ちるBTのphy_init経路」を突き合わせる**（esp_wifi_startとBT enableの
     phy_init前提設定の差＝クロック/regi2c/PHY_modem_flag/coex順序）。WiFiが
     同一ボードで動く＝RF層のA/B比較にNuttXを使わずWiFiビルドを内部参照に
     できる（より安価）。
   （なお，このボードのUSB-Serial-JTAGはRF活動中のCDCアクセスでチップ
   リセットを誘発するため，**RF段階のコンソール観測は不可**＝JTAG bare-run＋
   後追いattachでの状態採取が必須。UARTを付けても同種のUSB-JTAGリセット懸念は
   残るため，JTAG主体継続が妥当。）

##### 追加調査（2026-07-08 続きの続き）：WiFi成功 vs BT失敗のphy_init経路A/B差分

同一ボードで**動くWiFi（`wifi_scan`）** と**落ちるBT（`bt_smoke`）** を，
それぞれ`register_chipv7_phy`入口（WiFi=`0x42028076`／BT=`0x4201687c`）で
JTAG停止（OpenOCDのみ・CDC非接続なのでWiFiもリセットしない）し，直前状態を採取・diff：

| 項目 | WiFi（成功） | BT（失敗） |
|---|---|---|
| `s_phy_modem_flag` | 0 | 0（同じ＝初回enableのfull init分岐） |
| `s_is_phy_calibrated` | 0 | 0（同じ） |
| `I2C_MST[0]` (`0x6000E000`) | `0x042f0669` | `0x04000000`（既定） |
| `I2C_MST[1]` (`0x6000E004`) | `0x054b026b` | `0x04000000`（既定） |
| `I2C_MST_ANA_CONF0` (`0x6000E040`) | `0x2900e448` | `0x2900e408`（bit6差） |
| `ANA_CONFIG` (`0x6000E044`) | `0x00ffefff` | `0x0000002d` |
| `ANA_CONFIG2` (`0x6000E048`) | `0x0001fe04` | `0x00000004` |

**＝アナログI2Cマスタ／regi2c関連レジスタ（`0x6000E000`〜`E048`）がWiFiでは
設定/使用済み，BTでは既定のまま**という顕著な差を発見。ただし：
- BTの`register_chipv7_phy`入口でこれら（`E040/E044/E048`）にWiFi値を
  直接書いてから継続しても，**依然リセット**（`btdm_controller_enable`未到達）。
- `I2C_MST[0/1]`の値（`0x042f0669`/`0x054b026b`）はregi2cのトランザクション
  状態に見え，「BTに欠けている静的config」ではなく「WiFiが既にregi2cを
  使った痕跡（症状）」の可能性が高い。
- ＝**差分は見つかったが，その値の強制ではリセットは解消せず**。真因は
  regi2c値そのものより，**register_chipv7_phy内でregi2c/BBPLL較正が
  BT経路特有の前提（PLL/アナログ電源/タイミング）で破綻している**深い層
  （C6のRF/regi2c 66ラウンドと同一）にある。

**現時点の到達点（正直な区切り）**：WiFi/BT経路差分で「regi2c/ANA周辺の差」
までは特定したが，値の補完では解消せず。次は(a) `register_chipv7_phy`内の
どの命令/regi2cブロックアクセスでリセットするかをbp二分で局所化，
(b) C6資産（regi2c block 0x6b/0x66/0x6d/BBPLL自己較正，`docs/wifi-shim-c6.md`）
との突合，(c) BBPLL較正の前提（`I2C_MST_ANA_CONF0`のBBPLL_STOP_FORCE，
`regi2c_ctrl_ll_bbpll_calibration_start`）のA/B動的比較——いずれも**深いRF層**。
**続行判断を仰ぐため一旦区切る**。

##### 追加調査（2026-07-08 リセット点の局所化）：RF較正は無罪・真の再起動はコントローラ実行ループのE_CTX

前段の申し送り「(a) `register_chipv7_phy`内のどの命令/regi2cブロックで
リセットするかをbp二分で局所化」を被験機B（`60:55:F9:57:C2:60`）で実施。
手法＝OpenOCDネイティブ`bp`（8トリガ）で内部呼出しを段階的に挟み込み，
`reset halt`で各ブート先頭からクリーンに`register_chipv7_phy`入口
（`0x4201687c`）を捕捉→内部を歩進。CDC非接続・JTAG主体。

**★決定的な再枠組み：リセットは`register_chipv7_phy`（RF較正）に無い。**
細粒度bp二分の結果，`register_chipv7_phy`は**完走**する（`ret 0x420169f6`
まで全内部呼出しが順に命中）：
- `rom1_i2c_master_reset`(`0x420168f8`)／`register_chipv7_phy_init_param`／
  **`rf_init`(`0x42015ffa`)／`bb_init`(`0x42016778`)／`get_temp_init`／
  `rom_phy_bbpll_cal`(`0x420211d6`＝BBPLL較正)／`g_phyFuns[107](0x63,1,0)`
  コールバック(`0x420169d4`)** ——**全て完了**。
- ＝**regi2c/BBPLL自己較正/RF較正/ANA-I2Cはリセット要因ではない**
  （前段のWiFi/BT ANA差分は症状であって原因でない，と確定）。C6の
  regi2c/BBPLL 66ラウンド層は本ブロッカーとは無関係。

さらに下流も完走：`bt_bb_v2_init_cmplx`(`0x4201e0d4`，BB最終初期化。
`0x6000e0c4`/`0x600060fc`のRMWと`bt_bb_v2_tx_set/rx_set/filter_sel`を含む)，
`btdm_controller_enable`(`0x42007238`)，コントローラタスク
`btdm_controller_task`(`0x4201e902`)がtype-9（init）メッセージを処理し
`r_intc_enable`(`0x42008b0e`)→状態=2→`btdm_rw_run`(`0x4201e84c`)→
**`rw_schedule`(`0x42006c96`＝BLEイベントスケジューラ)まで完走**。
＝**enableは実質成功し，コントローラは定常イベントループに到達している**。

**真のリセット点＝コントローラ実行ループのブロッキング待ち**。
`btdm_controller_task`のループ先頭は
`semphr_take`(osi_funcs+52＝`_semphr_take`＝`semphr_take_wrapper`
`0x420025fe`, ブロック時間=`0xffffffff`=block-forever, `0x4201e958`で
jalr) → `queue_recv`(poll) → メッセージ種別ディスパッチ(`sp+24`のbyte0)。
最初の数メッセージ処理後，この**block-foreverの`semphr_take`が
即座に0を返し**（本来は永久ブロックして1を返すはず），
`btdm_controller_task`が`bnez a0`を通らず**エピローグ→ret(`0x4201e97a`)で
タスク終了**→直後にチップ再起動。

**根本メカニズム（実機で採取・確定）**：
- `semphr_take_wrapper`→`esp_shim_sem_take`(`0x4200106a`)→
  `twai_sem`(`0x42004b56`, TMO_FEVR)。この`twai_sem`の戻り値を
  `0x4200108e`で採取＝**`0xffffffe7` = -25 = `E_CTX`（コンテキスト
  エラー）**。`esp_shim_sem_take`は`seqz`で`E_OK?1:0`にするため
  E_CTX→**0**を返す→タスク終了。
- 失敗する`semphr_take`呼出口(`0x4201e958`)でのCPU状態＝
  **`mstatus.MIE=0`（割込み禁止＝CPUロック状態）**，一方
  `_kernel_dspflg`(`0x3fc80764`)=1（ディスパッチは許可）。
  ＝**CPUロック（割込み禁止）文脈でblock-forever待ちを呼んだため
  twai_semがブロック不可としてE_CTXを返す**（dis_dspではない）。
  直前に成功した`semphr_take`ではMIE=1だった＝**間に処理した
  メッセージハンドラが割込みを禁止したまま復帰していない
  （critical-section/割込み禁止の非対称）**。

**再起動の性質**：`RTC_CNTL_RESET_STATE_REG`(`0x60008038`)=`0x0000f0c3`
＝cause bits[5:0]=`0x03`=**RTC_SW_SYS_RESET（ソフトウェアによる
デジタルコア再起動）**。RTC-WDT(`0x60008090`)=0＝WDTではない。
ブラウンアウト/グリッチでもない。**この最終SW再起動は，計装した
全ソフト境界を素通りする**：ROM`software_reset`(`0x40000090`)／
`software_reset_cpu`(`0x40000094`)／`_esp_error_check_failed`／`abort`／
`_kernel_target_exit`／`_kernel_default_exc_handler`／
`_kernel_core_exc_entry`／`_kernel_default_int_handler`／`assert_wrapper`／
`r_assert_param`／BLE ISR`r_rwble_isr_hw_fixed`——**8本同時アームで
いずれも未発火**。`RTC_CNTL_OPTIONS0`(`0x60008000`)書込みwpも未発火
（即時リセットでデバッグモジュールが停止前にコアがリセット，
またはOPTIONS0経由でない別経路）。＝**最終リセット命令は未確定**だが，
**故障チェーンのトリガ＝コントローラのブロッキング待ちでのE_CTX**は確定。
（タスク終了自体はシムtrampoline`esp_shim_task_entry`が`ext_tsk()`を
呼ぶだけで再起動しない＝再起動はタスク終了「後」の別事象。CPUロック
残置のまま異常終了した副作用の公算。）

**C6/S3資産との対応**：これは**RF/regi2c/deaf-RX層ではなく，OSAシムの
critical-section/割込み禁止の非対称バグ**＝`MEMORY.md`がS3知見として
警告する「OSAシム共有欠陥」ファミリ。blobが
`portENTER_CRITICAL`/`_global_intr_disable`（シム経由＝`loc_cpu`/
`esp_shim_int_disable`等）で割込みを禁止し，あるメッセージハンドラが
`portEXIT_CRITICAL`/`_global_intr_restore`で復帰し切らずに次周回の
block-forever `semphr_take`へ入る，という非対称が本命。

**次段ポインタ（続行判断待ち）**：(1)成功する`semphr_take`と失敗する
`semphr_take`の間に処理されるメッセージ種別（`sp+24`のbyte0）を1つに
特定し，そのハンドラが呼ぶ割込み禁止/復帰系osiラッパの残置を突き止める。
(2)シムの`portENTER/EXIT_CRITICAL`・`_global_intr_disable/restore`・
`esp_shim_int_disable/restore`の対応表とネスト整合を監査
（`r_intc_enable`でBLE割込みをCPU線1へ有効化した後のISR/critical-section
ネストが特に疑わしい）。(3)最終SW再起動の実命令は，割込み禁止残置での
異常タスク終了→（idle/別文脈での）直接レジスタ書込みの可能性を
watchpoint不可のため要別手法。**RF/regi2cの深掘り（C6 66ラウンド層）は
不要と判明した**のが最大の収穫。

##### 修正（2026-07-08 修正フェーズ）：クリティカルセクションのネスト対応化 → enable完走・VHCI往復成功＝Phase D-1完了

前段で確定した真因（コントローラ実行ループでの割込み禁止残置→
`semphr_take`がE_CTX→タスクexit→SWリセット）の**発生源をソース監査で
一意に特定**し，修正・実機検証した。

**発生源（確定）＝`portENTER_CRITICAL`が退避値をmuxに格納する方式**。
`asp3/target/esp32c3_espidf/bt/stub/include/freertos/FreeRTOS.h`の旧実装：
```c
#define portENTER_CRITICAL(mux)  (*(mux) = esp_shim_int_disable())
#define portEXIT_CRITICAL(mux)   (esp_shim_int_restore(*(mux)))
```
`esp_shim_int_disable()`は「旧MIEを返し，MIEをクリア」，
`esp_shim_int_restore(s)`は「s≠0ならMIEをセット」で**単体では正しい
退避／復元**。しかし退避値を**呼び手のmux変数に格納**するため，
**同一muxを入れ子で取得すると内側の取得が外側の退避値（MIE=1）を
MIE=0で上書き**し，最外の解放で`restore(0)`＝**割込み禁止のまま残る**。
BTコントローラの`global_interrupt_disable/restore`（bt.cの
`_global_intr_disable/restore` osi関数）は**単一の共有mux
`global_int_mux`**（bt.c:493）を使い，RW/LLDスタックがこれを深く
ネストする＝旧方式では最外解放後もMIE=0が残る。これが実機で採取した
「失敗する`semphr_take`直前の`mstatus.MIE=0`」の正体。ESP-IDF本家の
FreeRTOSは割込み状態を**muxではなくコア単位のネストカウンタ**で退避
（最外0→1で退避・最外1→0で復元）するため破綻しない。

**修正（target/シム側のみ．submodule不可を遵守）**：
- `asp3/target/esp32c3_espidf/wifi/esp_shim.c`：**ネスト対応
  クリティカルセクション**`esp_shim_enter_critical()`/
  `esp_shim_exit_critical()`を追加。大域ネストカウンタ
  `esp_shim_crit_nest`＋退避`esp_shim_crit_saved`で，`csrrci`で
  MIEを読みつつクリア→最外(nest 0→1)でのみMIEを退避，最外(1→0)でのみ
  復元。単一コアのためmuxは不要（参照しない）。同一muxの入れ子でも
  MIEを取りこぼさない。
- `asp3/target/esp32c3_espidf/wifi/esp_shim.h`：上記のextern宣言追加。
- `asp3/target/esp32c3_espidf/bt/stub/include/freertos/FreeRTOS.h`：
  マクロを`portENTER_CRITICAL(mux) = (esp_shim_enter_critical(),(void)(mux))`
  ／`portEXIT_CRITICAL(mux) = (esp_shim_exit_critical(),(void)(mux))`へ変更
  （`_ISR`/`_SAFE`版も同一へ委譲）。`(void)(mux)`はunused警告抑止用で
  副作用なし。BT専用ヘッダのためWiFi経路は不変。
- `SHIM_LOCK`/`BT_LOCK`（局所変数に退避＝入れ子でも安全）は変更なし。

**実機検証（被験機B `60:55:F9:57:C2:60`）**＝**★enable完走・VHCI往復成功
＝Phase D-1完了基準を達成**：
- 修正版`bt_smoke_hw`を`--before usb-reset --after watchdog-reset
  --no-stub write-flash`で書込み→JTAGで状態採取（CDC非接続・
  bare-run＋attach）。
- **`hci_reset_done`(`0x3fc806f0`)=`0x00000001`＝真**。この大域は
  `vhci_notify_host_recv`でHCI Command Complete（0x04/0x0E）を受信した
  ときのみ`true`にする＝**HCI Reset送信→Command Complete受信の
  VHCIループバックが成立**＝コントローラ生存の証明。
- **PC=`dispatcher_2`(`0x42003d8c/90`)＝idle**（`_kernel_start_r`
  `0x42003dac`のリセットループではない）。**RTC_SW_SYS_RESETループは消滅**。
- **再現性**：`reset halt`→`resume`のクリーン再起動を複数回，いずれも
  4s以内に`hci_reset_done=1`＋idle到達，以後安定（遅延リセットなし）。
- コントローラがHCI Reset→Command CompleteのVHCI往復を完了した＝
  イベントループが生存し`semphr_take`が正しくブロック（E_CTX消滅）した
  ことの**エンドツーエンドの証明**（0x4201e958の単発take戻り値確認より
  強い）。
  （※修正でシンボルアドレスは移動：`dispatcher_2`=`0x42003d8c`,
  `_kernel_start_r`=`0x42003dac`, `hci_reset_done`=`0x3fc806f0`,
  `btdm_controller_task`=`0x4201e9f0`。以前の値は再ビルドで無効。）
- **WiFi非回帰**：共有`esp_shim.c`変更は関数追加のみ（既存WiFi経路の
  プリミティブ不変，マクロ変更はBT専用ヘッダ限定）。`wifi_dhcp_hw`
  （`ESP32C3_WIFI=ON`）を再ビルドし0エラーを確認。

**＝Phase D-1（コントローラ起動＋VHCIループバック）完了**。依頼
`bt-emi-2board-request.md`のPhase D-1完了基準（HCI Reset往復）を満たす。
次はPhase D-2（NimBLEホスト統合）。

**S3移植メモ（このネスト不整合修正）**：本修正は**チップ非依存でS3の
シムにも直結**する。S3も同一`bt.c`＝同じ`global_interrupt_disable`/
共有`global_int_mux`のネストを踏むため，S3側`portENTER/EXIT_CRITICAL`が
同じ「退避値をmuxに格納」方式なら同一のE_CTX→SWリセットが起きる公算大。
`MEMORY.md`のS3欠陥ファミリ（OSAシム共有欠陥）と同種。S3では
`esp_shim_enter/exit_critical`相当をネストカウンタ方式で実装し，
BT stubの`portENTER/EXIT_CRITICAL`マクロを委譲させること
（RISC-V`mstatus.MIE`はS3も同機構だがデュアルコアのためコア単位の
カウンタ／スピンロックの扱いは要検討＝S3のマルチコア臨界区間は
本C3単一コア実装をそのまま流用不可，コア別退避が必要）。

#### 併発する既知バグ（boot variance の真因）
本セッションを通した「boot variance」（em store bp命中率≈1/3，
`dbg_assert_block`揺れ）の真因は，**共有ファイル
`asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c` の
`esp_shim_coex_adapter_register()`内のC6調査用一時診断コード**（実施53/54）
と判明。`read_fn = *(0x4087f954+20*4)`（ROM PHYFUNS表エントリ）を
`read_fn(0x63,1,0)`で呼ぶが，このポインタが間欠的にNULL→
Illegal instruction（`pc=0`）でクラッシュ→kernel_exit。bt_smokeも
`esp_shim_coex_adapter_register()`を呼ぶため被る。**これはBTの機能とは
無関係の一時診断コード**であり，NULLガードするか（`if (read_fn)`），
BT経路で当該診断を無効化すれば boot が安定する見込み（本セッションでは
C6調査資産への影響を避け未修正．要判断）。

#### S3移植メモ（更新）
- **チップ非依存（S3流用可）**：真因の構造（Direct Bootが
  esp_perip_clk_init相当を通らずBBクロック未設定→em_base_init書込み
  ドロップ→emi.c:164）と修正方針（controller init前に
  WIFI_CLK_EN相当を立てる）はチップ非依存。S3もDirect Boot型なら同種。
- **C3/S3差（要読替え）**：`SYSCON_WIFI_CLK_EN_REG`アドレス・
  `SYSTEM_WIFI_CLK_EN`マスク値・BBクロックのビット配置はS3で要確認
  （S3はデュアルコア・modem clock構成が別）。ROM/blobアドレスも別。
  `phy_init`ブロッカーもS3で再現・別途対処が必要。**S3実機再現確認は
  本依頼範囲外＝別途必須**。

## Phase D-2a：NimBLE host sync（進行中）

目標＝NimBLEホストをD-1のコントローラ（VHCI往復OK）上に立ち上げ，
`ble_hs`のsyncコールバック発火（HCIハンドシェイク完了・BDアドレス確定）
まで。GAP/GATTはD-2b以降。

### Stage 1（完了）：NPLシムAPIの追加

`hal/components/bt/porting/npl/freertos/src/npl_os_freertos.c`（ESP-IDFの
NPL＝FreeRTOS+esp_timerベースのOS抽象．1269行）が要求するFreeRTOS APIの
うち，既存BTシム（D-1）に無かったものを実測で洗い出し，同じ方式
（`esp_shim_*`プリミティブへ委譲）で追加した：

| 追加API | 委譲先 | 備考 |
|---|---|---|
| `xQueueSendToBack/ToFront(+FromISR)` | `esp_shim_queue_send(_from_isr)` | ToFrontは先頭送信非対応のため通常送信で代用（NimBLE稀用途） |
| `xQueueReset` | **新規`esp_shim_queue_reset`** | `prcv_dtq`で全item排出しヒープ解放 |
| `xQueueIsQueueEmptyFromISR`／`uxQueueMessagesWaitingFromISR` | `esp_shim_queue_msg_waiting` | |
| `xSemaphoreCreateRecursiveMutex`／`Take/GiveRecursive` | **既存`esp_shim_mutex_*`**（owner/count追跡＝再帰対応が既に実装済み） | TakeRecursiveのtmo引数は無視（下地`loc_mtx`＝永久ブロック） |
| `uxSemaphoreGetCount` | **新規`esp_shim_sem_get_count`** | `ref_sem`→semcnt |
| `xTaskGetCurrentTaskHandle` | `get_tid` | tskidをhandleとして返す |
| `xTaskGetSchedulerState` | 定数`taskSCHEDULER_RUNNING` | ASP3はsta_ker後常時RUNNING |
| `xTaskGetTickCount(FromISR)` | `esp_shim_time_us()/1000` | SYSTIMERのms換算．レジスタ読取りのみでISRセーフ |
| `configTICK_RATE_HZ`＝1000 | — | tick=1ms（NPLの時間換算用） |

コールアウトは`CONFIG_BT_NIMBLE_USE_ESP_TIMER=1`（nuttx sdkconfig.h）→
`BLE_NPL_USE_ESP_TIMER=1`のため**esp_timer経路**（`bt_shim.c`が既に
`esp_timer_create/start_once/stop/delete`を提供）。追加で
`esp_timer_get_expiry_time`／`esp_timer_is_active`が必要（Stage 2で追加予定）。

変更＝`wifi/esp_shim.c`（`esp_shim_sem_get_count`／`esp_shim_queue_reset`
追加），`bt/stub/include/freertos/{queue,semphr,task,FreeRTOS}.h`（上表の
inline委譲）。**bt_smoke_hw（D-1）再ビルドで0エラー・RAM 84.88%不変
＝D-1非回帰**（追加APIはコントローラ未使用のためサイズ不変）。

### 残Stage（未着手．スコープと★RAMリスクを実地マップ済み）

ESP-IDF正本`hal/components/bt/CMakeLists.txt`（1167行）から構成を抽出：

- **Stage 2 ビルド統合（大）**：NimBLE host本体 約60ソース
  （`host/nimble/nimble/nimble/host/src/ble_hs*.c`／`ble_gap.c`／`ble_gatts*.c`／
  `ble_gattc*.c`／`ble_att*.c`／`ble_l2cap*.c`／`ble_sm*.c`／`ble_uuid.c`等）＋
  NPL（`porting/npl/freertos/src/npl_os_freertos.c`）＋mem
  （`porting/mem/{bt_osi_mem,os_msys_init}.c`）＋HCIトランスポート
  （`porting/transport/src/hci_transport.c`＋
  `porting/transport/driver/vhci/hci_driver_nimble.c`＋
  `nimble/transport/esp_ipc/src/hci_esp_ipc.c`）＋esp-hci
  （`host/nimble/esp-hci/src/esp_nimble_hci.c`）＋nimble_port
  （`porting/nimble/src/nimble_port.c`＋`porting/npl/freertos/src/nimble_port_freertos.c`）を
  `esp_bt.cmake`へ追加，インクルードパス（`host/nimble/nimble/nimble/host/include`／
  `.../nimble/include`／`porting/include`／`porting/npl/freertos/include`／
  `porting/transport/include`／`host/nimble/port/include`／`host/nimble/esp-hci/include`）を通す。
- **config**：NimBLEアプリ設定は**既存`hal/nuttx/esp32c3/include/sdkconfig.h`
  （D-1で既にinclude path）が保有**（`CONFIG_BT_NIMBLE_ENABLED=1`／
  `USE_ESP_TIMER=1`／`LEGACY_VHCI_ENABLE=1`／`MSYS_1_BLOCK=12×256`／
  `MSYS_2=24×320`／`ACL_BUF=24×255`／`HOST_TASK_STACK=4096`／`MAX_CONNECTIONS=3`等）。
  ただしESP-IDFビルド構造フラグ（`SOC_ESP_NIMBLE_CONTROLLER`＝os_mbuf/mempoolを
  ROM/コントローラ供給とするか，`CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT`／
  `HCI_INTERFACE_USE_RAM`）はsoc_caps/Kconfig由来でnuttx sdkconfigに無い＝要組立。
- **crypto決定点**：`CONFIG_BT_NIMBLE_CRYPTO_STACK_MBEDTLS=1`（sdkconfig）。
  sync単体では暗号不要だが，`ble_sm*.c`のリンクにmbedTLS（RAM重・BTで未リンク）
  or tinycrypt or SECURITY無効化のいずれかの判断が要る。sdkconfig.hは
  submodule外だが編集不可のhal配下＝**target側で上書きsdkconfigを噛ませる**要検討。
- **静的カーネルオブジェクトプール拡張**：`esp_shim`は固定プール
  （**DTQは4本のみ**／SEM24／MTX8／TSK6，`bt.cfg`/`esp_shim_cfg`）。NimBLEは
  eventq（host/transport）＋mutex数本を要求＝`bt.cfg`のDTQ/TSK/SEM/MTX増設が必要
  （＝制御ブロック＋タスクスタック分のRAM増）。
- **★RAM予算（最重要リスク）**：現状 **278124B/320KB＝84.88%，空き約48KB**。
  NimBLE最小構成の追加RAM見積り＝host taskスタック4KB＋msys(3+7.7KB)＋
  ACLバッファ約6KB＋HCI evtバッファ約2.7KB＋mempool/ble_hs .bss＋シムプール拡張
  ＝**概ね35〜50KB**。**48KBに収まるかは極めて際どく，未削減では溢れる公算**。
  削減案：`MAX_CONNECTIONS`を3→1，msys/ACLバッファ数削減，host taskスタック縮小，
  SECURITY無効化（暗号除外）等。→**続行前にRAM戦略の判断を仰ぐのが妥当**。

### Stage 2（完了）：ビルド統合＋実機sync到達＝★Phase D-2a完了

RAM戦略はオプション1（sync-only最小構成へトリム＝SECURITY無効化で
mbedTLSのRAM＋リンク負担を回避）で承認・実施。

#### 構造決定（実機ブリングアップで確定）
- **C3はupstream npl＋upstream mem/mbuf＋legacy VHCI経路**（ESP-IDF nplは
  `SOC_ESP_NIMBLE_CONTROLLER`必須で`#error`＝C3非該当）。HCIは
  `esp_nimble_hci.c`＋`hci_esp_ipc_legacy.c`＝D-1のVHCI（`esp_vhci_host_*`）へ橋渡し。
- **config**：`ESP_PLATFORM`定義で`esp_nimble_cfg.h`ブリッジ有効化
  （`hal/nuttx/esp32c3/include/sdkconfig.h`のNimBLE設定を`MYNEWT_VAL_*`へ写像）。
  必要な`CONFIG_BT_*`（coex無しの最小）は`bt/stub/include/bt_nimble_config.h`
  （force-include）で供給＝`CONFIG_ESPRESSIF_BLE`を立てると`CONFIG_ESPRESSIF_WIFI`
  も立ちcoex ONでD-1のcoex-OFF検証経路が壊れるため回避。
- **SECURITY無効化**：`MYNEWT_VAL_BLE_SM_LEGACY=0`＋`_SC=0`→`NIMBLE_BLE_SM=0`
  ＝`ble_sm*.c`は空コンパイル・mbedTLS/tinycryptを一切リンクしない。
- **RAM実績＝92.37%（302684B/320KB）＝収まった**。msys/mbuf/mempoolはヒープ確保
  （`MEM_ALLOC_MODE_INTERNAL`＝192KBシムヒープから）でリンク時RAMに載らない。
  リンク時増分の主因はシム静的プール拡張（SEM24→28/MTX8→12/DTQ4→8/TSK6→8，
  `TOPPERS_ESP32C3_BT_NIMBLE`ゲートでbt_smoke/wifi_scanのプールは不変）とhost .bss。

#### ★実機で捕捉した決定的バグ：QEMUモードでビルドされ`csrw mie`が実機で不正命令
初回ビルドは`ESP32C3_QEMU`（`target.cmake`既定＝**ON**）のまま構成され，
`TOPPERS_USE_QEMU`が定義された。C3 archの`chip_initialize`（`chip_kernel_impl.c:118`）
は`#ifdef TOPPERS_USE_QEMU`で`csrw mie,~0`を出す（QEMUのesp32c3モデルはmie経由で
割込み配送＝mie必須）が，**実機ESP32-C3はmie CSR非実装＝アクセスで不正命令例外**。
→ブート直後`chip_initialize`で不正命令→`_kernel_default_exc_handler`→`ext_ker`→
`_kernel_target_exit`のebreakでカーネル終了（アプリ未到達）。JTAGで
`mcause=2(Illegal Instruction) / mepc=chip_initialize内csrw mie / mtval=0x30479073
(csrw mie)`を採取し確定。bt_smoke_hw（実機ビルド）は`-DESP32C3_QEMU=OFF`で
`csrw mie`が無いため無害＝差分の正体。**修正＝`cmake -DESP32C3_QEMU=OFF`で
再構成・再ビルド**（`csrw mie`消滅を逆アセンブルで確認）。
（教訓：C3の実機ビルドは`ESP32C3_QEMU=OFF`必須。CLAUDE.md「mie非実装」の罠。）

#### 検証結果（被験機B `60:55:F9:57:C2:60`）＝★sync到達
`-DESP32C3_QEMU=OFF`版を書込み，JTAG bare-run＋attach（CDC非接続）で採取：
- **`g_ble_sync_done`(`0x3fc80760`)=1**（`ble_hs_cfg.sync_cb`＝`on_sync`が発火した
  ときのみセット）＝**ble_hs syncコールバック到達**＝ホスト↔コントローラの
  HCIハンドシェイク完了・BDアドレス確定。
- `ble_hs_sync_state`(`0x3fc80843`)=`0x02`（SYNC_GOOD）／`ble_hs_enabled_state`
  (`0x3fc80842`)=`0x02`（ON）＝**ホスト完全同期・稼働**。
- sync マーカ`0x60008050`=`0x5ade51c0`（on_syncの`sil_wrw_mem`書込み）＝確認。
- **PC=`dispatcher_2`(`0x4200bd1c/20`)=idle**（不正命令・リセットループではない）
  ＝host task生存。安定（複数サンプル・`reset halt`→`resume`クリーン再起動でも
  5s以内にsync到達で再現）。
- **＝Phase D-2a（NimBLE host sync）完了**。NPLシム＋HCIトランスポート＋
  nimble_port＋host taskが端から端まで機能。GAP/GATT（advertise/scan等）は
  D-2b以降。

#### アプリ配線（`apps/ble_host_smoke`）
本ポートの`nimble_port_init()`は内部で`esp_bt_controller_init/enable`を
`BT_CONTROLLER_INIT_CONFIG_DEFAULT()`で行う＝手書きcfg（D-1検証値）と二重初期化に
なるため使わず，`esp_bt_controller_init(&cfg)`（bt_smokeと同一手書きcfg）→
`esp_bt_controller_enable`→**`esp_nimble_init()`**（ホストのみ・内部で
`esp_nimble_hci_init`＝VHCIブリッジ登録．コントローラには触れない）→
`ble_hs_cfg.sync_cb=on_sync`→`nimble_port_freertos_init(ble_host_task)`とした。

#### S3移植メモ（D-2a）
- NPLシム追加API・HCIトランスポート配線・SECURITY無効化・ヒープ確保方針は
  チップ非依存でS3流用可（`MEMORY.md`のS3欠陥ファミリ／臨界区間ネスト修正と同系）。
- **`ESP32C3_QEMU`相当の実機/QEMU切替はS3でも要確認**（S3実機もmie扱いが
  C3同様なら`csrw mie`回避が必要＝実機ビルドでQEMUフラグをOFFにする）。
- upstream npl/mem採用（`SOC_ESP_NIMBLE_CONTROLLER`非該当）・legacy VHCI・
  `bt_nimble_config.h`のcoex回避はS3も同型の見込み（要確認）。

#### ★S3 BTシム着手前に必読：割込みネスト臨界区間の設計（デュアルコア対応）

Phase D-1で潰した「クリティカルセクション・ネスト不整合バグ」（コントローラ
enable後に`btdm_controller_task`がSWリセットループ）について，**S3でも
同一のバグを踏むか**を検討した結論を明記する（この調査自体がS3 FMP3移植の
BLE対応リスクを先取りするための依頼だったため）。

**結論：バグの"トリガ"はチップ非依存で確実にS3にも存在する。ただし実際に
踏むかはS3の（未実装の）BTシム実装次第であり，C3の修正はそのままは
流用できない（デュアルコア対応が必須）。**

1. **トリガはS3にもある（チップ非依存）**：
   - S3もC3と同一のBTコントローラ系列（`libbtdm_app`＋`controller/esp32c3/bt.c`）。
     `global_interrupt_disable/restore`（osiの`_global_intr_disable/restore`）が
     **単一の共有`global_int_mux`をRW/LLDスタックで深くネスト取得**する挙動は
     blob側なのでS3でも全く同じ。
   - FMP3（S3側カーネル）も，CPUロック（割込み禁止）文脈でブロッキング待ち
     サービスを呼べばコンテキストエラー（`E_CTX`相当）を返す。＝割込みが
     禁止のまま残れば同じ失敗モード（block-forever take失敗→タスクexit→
     SWリセット）になる。
   - S3が**まだBTシムを持たない**（WiFiは完動・BLEはこれから）ため，今はまだ
     踏んでいないだけ。S3のBTシムが**C3の旧実装パターン（saved-MIEをmuxに
     格納）をコピーすれば，C3と全く同じネスト上書きバグを踏む**。

2. **★C3の修正はS3にそのまま流用不可（S3はデュアルコア/SMP）**：
   C3の修正（`esp_shim_enter/exit_critical`＝**大域**ネストカウンタ＋saved-MIE，
   muxは無視。commit `b3129c9`，本doc「クリティカルセクション・ネスト対応化」節）は
   **単一コア前提**。S3では以下が必要：
   - **ネスト状態・saved割込み状態を「コア単位」で持つ**（各コアが独立した
     `mstatus.MIE`とネスト深度を持つ。C3の単一大域カウンタはSMPでは誤り＝
     別コアの状態を混ぜる）。
   - **muxを無視できない＝本物のスピンロックとして実際にacquire/releaseする**。
     SMPでは`portENTER_CRITICAL(mux)`は「割込み禁止」に加え「muxスピンロックで
     コア間相互排除」も担う。C3修正はmuxを無視するが，これはSMPではコア間
     排他を壊す。
   - ＝S3の正しい実装は**ESP-IDF SMP FreeRTOSと同じフルセマンティクス**
     （コア単位ネストカウンタ＋saved割込み状態＋muxスピンロック）。

3. **S3への推奨**：
   - `portENTER/EXIT_CRITICAL`を**最初から「コア単位ネスト＋muxスピンロック」で
     実装**する（C3の単一コア版をコピーしない）。BT stubの`portENTER/EXIT_CRITICAL`
     マクロを，S3版の`esp_shim_enter/exit_critical`相当（per-core＋spinlock）へ委譲。
   - 可能なら**BTをコア0固定**にすると，そのコアに対しては実質単一コア的に
     扱えて実装が単純化する（S3のWiFiも「コア0固定」で類似の地雷を回避した
     経緯がS3側ドキュメントにある）。ただしそれでもmuxスピンロック自体は
     残す方が安全（他コアからの臨界区間アクセスがゼロと断言できない限り）。
   - 参照：C3の単一コア実装＝`asp3/target/esp32c3_espidf/wifi/esp_shim.c`の
     `esp_shim_enter/exit_critical`（本docのPhase D-1修正節）。**構造は流用元と
     して有用だが，per-core化とspinlock追加を必ず行うこと。**

## Phase D-2b：GAP接続可能アドバタイズ（★完了＝2026-07-13保留解除．本章末尾の(1)(o)参照）

> **★2026-07-13追記**：本章の長大なストーム調査（(1)〜(1)(n)）の末に
> 「深い低レベルHW/blob層」として保留されたD-2bブロッカーは，**target層の
> `esp_intr_alloc`多重登録バグ（source8の登録がsource5の登録で上書きされる）**
> が真因と確定し，修正・実機確認済み．経緯と物証は本章末尾の
> **(1)(o)** を参照．(1)(a)の「BBハードウェアがadv実TX時に割込み線を
> 連続assert」という最終解釈（1434-1438行相当）は誤りで，実際は
> 「**source8(RWBLE)の正規割込みが，上書きされたsource5用handlerに
> 配送され続け，source8のstatus/clearに誰も触れないため即再アサート**」
> だった．status=0 spurious・EIP毎回deassert・enable bitから説明不能な
> レート，の全観測はこの1つのソフトウェアバグで過不足なく説明される．

目標＝D-2aのNimBLE host（sync到達）の上にGAPを立て，接続可能な
アドバタイズを開始しデバイスが外部から見える状態まで（bleprph相当）。

### 実装（アプリ）
`apps/ble_host_smoke/ble_host_smoke.c`を拡張：`on_sync`（sync完了後）で
`start_advertising()`を呼ぶ。`start_advertising`＝`ble_hs_id_infer_auto`→
`ble_gap_adv_set_fields`（flags＋完全ローカル名`ASP3-C3-BLE`）→
`ble_gap_adv_start`（`conn_mode=UND`/`disc_mode=GEN`, duration=`BLE_HS_FOREVER`,
`gap_event_cb`）。`gap_event_cb`はCONNECT/DISCONNECT/ADV_COMPLETEを処理
（切断時再アドバタイズ）。観測用グローバル：`g_adv_rc`（adv_start戻り値，
init=-1）／`g_adv_active`／`g_gap_conn_count`／`g_gap_event_count`＋RTC
マーカ（adv=`0x60008054`, connect=`0x60008058`）。GAP/GATTソース
（`ble_gap.c`/`ble_gatts.c`/`ble_svc_gap.c`/`ble_svc_gatt.c`等）はD-2aで
既にコンパイル済み＝`esp_bt.cmake`変更不要。

### RAM実績
アドバタイズ広告名はadvデータで送るため，接続可能advには
GATT GAPサービス（`ble_svc_gap_init`）は必須でない．最小adv構成の
RAM＝**92.38%（302700B/320KB，D-2aから±0）**。

### ★実機ブロッカー（JTAG bare-run＋attach，`ESP32C3_QEMU=OFF`）
- **sync到達（`g_ble_sync_done=1`）・startup HCI全完了・adv HCIコマンドの
  `LE Set Adv Params`(0x2006)/`LE Set Adv Data`(0x2008)も完了**まで正常
  （`ble_hs_hci_cmd_tx` 0x4200984e にopcodeを積むトレースで確認：0x0c03 Reset
  →…→0x1009 Read BD Addr→0x2006/0x2008→0x200a）。
- **ブロッカー＝`LE Set Advertising Enable`(0x200a)のcommand-completeが
  返らず，host taskが`ble_hs_hci_cmd_tx`のackセマフォ待ちでブロック**．
  ＝`ble_gap_adv_start`(0x420075b6)が戻らず`g_adv_rc`は-1のまま／
  `g_adv_active`=0．CPUはdispatcher/割込み処理で**生存**（クラッシュ・
  リセットではない）．0x2006/0x2008は完了したのに0x200aだけ未完＝
  **実際のRF送信を開始するSet Adv Enableで詰まる**＝D-1/D-2aで見た
  「RF活動開始で不調」と同系の可能性（コントローラ側でadv-enableの
  complete生成orイベント配送が破綻）．外部スキャナ未確認＝コントローラが
  実際にadv TXしているか（＝hostのack待ちだけの問題か，adv自体未起動か）は
  未判定．
- **別事象：GATTサーバ有効化（`CONFIG_BT_NIMBLE_GATT_SERVER=1`＋
  `GAP_SERVICE=1`＋`ble_svc_gap_init/gatt_init`呼出し）を入れると，adv経路で
  NULL関数ポインタ呼出し（`mcause=2` Illegal Instruction, `mepc=0`）→
  `_kernel_default_exc_handler`→`ext_ker`→`_kernel_target_exit`spinで
  カーネル終了（sync後）**．NULL呼出しは`ble_gap_adv_start.part.0`の直接
  npl_funcs呼出し（offset132/136＝いずれも非NULL）より深く，未特定．
  GATTサーバはadv接続可視には必須でないため一旦OFFに戻し（RAMも復帰），
  GATTサービス立ち上げは追調査とした．
- 非回帰：`bt_smoke_hw`（BT単体）・`wifi_dhcp_hw`（WiFi）とも0エラー再ビルド
  （D-2b設定は`ESP32C3_BT_NIMBLE`/`APPLNAME==ble_host_smoke`ゲートで両者に
  影響しない）。

### 次段（申し送り＝判断待ち）
`LE Set Adv Enable`(0x200a)のcomplete未達の切り分け：(a) コントローラが
0x200aを受理しadv TX開始しているか（外部スキャナ or VHCI RX callback
`esp_nimble_hci`のhost_recv_pktにbpして0x200a complete eventの有無確認），
(b) hostのackセマフォへのsignal経路がstartup期と定常期（on_sync文脈）で
差がないか，(c) adv-enable時のRF活動がD-1/D-2a同様にコントローラ内部で
不調を起こしていないか（bare-run+attachでコントローラ状態採取）．
GATTサーバのNULL呼出しは別途（adv経路のどのfn-ptrがNULLか，`ble_hs_cfg`
コールバック or GATT登録の未初期化を精査）．

#### 0x200a complete未達の切り分け結果（2026-07-08，焦点調査）
VHCI RXコールバック`host_rcv_pkt`(`0x42005a68`)にbpして実機観測
（bare-run+attach，QEMU=OFF）：
- **command-complete処理はインライン**（コントローラタスク文脈．
  `host_rcv_pkt`→`ble_hci_trans_ll_evt_tx`→`ble_transport_to_hs_evt`→
  `ble_hs_hci_evt_process`→`ble_hs_hci_rx_ack`が`ble_hs_hci_sem`をsignal）．
  だからstartup期にhost taskが`ble_hs_hci_cmd_tx`でsem待ちブロック中でも
  各completeが処理され進行できた（enqueue方式なら初回でデッドロックする筈＝
  インラインで確定）．
- **`host_rcv_pkt`は0x200a送信後も高頻度で呼ばれ続けるが，届くのは
  最古の2イベント＝`0c03`(Reset)と`1001`(Read Local Version)の
  command-complete(evt=0x0e, status=0x00)を無限リプレイするのみ．
  0x200a(Set Adv Enable)のcompleteは一度も来ない**（`evtrace.py`で
  distinct=`{(0x0e,0x0c03),(0x0e,0x1001)}`のみ）．
- `host_rcv_pkt`の呼出し文脈＝**SHIM_TSK1（コントローラタスク）のスタック
  （sp∈0x3fcc7a20..9a20）**，ra=ROM`0x400091ec`＝ROM側HCI-event-TX-to-host
  がコントローラタスクで走る．halt時PCはblob各所に散る＝busy（tight spin
  ではないがコントローラは0x200a completeを生成せず古いイベント再送を
  churnしている）．host task側は`ble_hs_hci_sem`(=`0x3fc87a40`,非NULL)待ちで
  ブロック，`g_adv_rc`=-1．
- **結論（切り分け＝コーディネータ判定木の「返ってこない」側）**：
  コントローラが**Set Adv Enable(RF送信開始)のcommand-completeを生成しない**
  ＝より下層．ただし症状「最古の2completeを無限リプレイ」は純RF障害より
  **ソフトのバッファ/キュー・ポインタ再生バグ**（コントローラのHCI-event-to-host
  링/mempool，またはtransport event pool）を強く示唆．RF自体はD-2a syncで
  使え，adv params/dataも受理済み＝壊れているのはadv-enable後の
  イベント配送のみ．**深掘り（ROM HCI-TX-to-host経路 or transport
  event mempool/queueの再生バグ精査）はコントローラ下層＝ユーザー判断待ち**．
- 次段候補：(1) transport event pool（`pool_evt`/`ble_transport_alloc_evt`/
  `ble_transport_free`）の消費・解放が0x200a後に破綻していないか
  （`os_memblock`のfree漏れ→リング再生），(2) コントローラのHCI-to-host
  TXキュー（ROM）の読みポインタがadv-enableでリセットされていないか，
  (3) `notify_host_send_available`(controller_rcv_pkt_ready)のクレジット/
  フロー制御経路．いずれもtarget側（`esp_nimble_hci`ラッパ/shim/config）で
  介入余地があるか要精査．

#### ★重要な再枠組み：これはデッドロックではなく「ホスト再初期化ループ」（2026-07-08 option1調査）
option1（トランスポートevt mempool）を実機で追った結果，症状の解釈が
根本的に変わった：
- **プール枯渇は反証**：`pool_evt`（`0x3fcb1864`）＝`mp_block_size=70`,
  `mp_num_blocks=30`, `mp_num_free=29`＝**29/30空き＝枯渇していない**．
  alloc/freeは均衡．「最古2件リプレイ」はプール枯渇ではない．
- **ack取りこぼしも反証**：`ble_hs_hci_rx_ack`（inline）は各completeで
  `ble_npl_sem_get_count`→`esp_shim_sem_get_count`(`0x420011ee`)を呼ぶが，
  戻り値は**常に0**（16連続確認）＝semカウントは正しく0＝ackを
  ドロップしていない．sem releaseも正常．
- **★真の症状＝ホスト再初期化ループ**：`ble_hs_hci_cmd_tx`(`0x4200984e`)は
  ack異常（wait_for_ackタイムアウト／process_ackのopcode不一致／status異常）
  のとき`ble_hs_sched_reset(reason)`(`0x42008b18`)を呼ぶ．実機で
  **`ble_hs_startup_go`(`0x4200ab6e`)が繰り返し発火**＝ホストが
  startup（Reset 0x0c03→Read Local Ver 0x1001→…）を**再送し続けている**．
  ＝**先の「コントローラが最古2completeを無限リプレイ」という解釈は誤りで，
  実体はホスト自身がreset/再initを繰り返し，そのstartup HCIトラフィック
  （0x0c03/0x1001のcomplete）を観測していた**．adv経路のあるコマンドの
  ack異常→`ble_hs_sched_reset`→ホストreset→再init→再sync→on_sync→
  再adv→また異常→ループ．`g_adv_rc`が-1のままなのは，adv_startが
  ループ内で正常復帰しないため．
- **未確定（次の1手）**：`ble_hs_sched_reset`のreason（a0）の採取に失敗
  （ループ周期が短くbpタイミングが合わず）．reasonが
  **`BLE_HS_ETIMEOUT_HCI`ならadv-enable(0x200a)のcompleteが実際に来ていない
  （2s HCI timeout）＝コントローラ/トランスポート下層**，
  **`BLE_HS_ECONTROLLER`ならcompleteは来るがopcode不一致＝
  トランスポートの順序/取り違え or ホスト側**，と切り分く．
  （※ASP3 TMO単位は**μs**＝`TMAX_RELTIM=4e9`が66分40秒＝1単位=1μs．
  `esp_shim_tick_to_tmo`のms→μs換算（×1000）は正しく，2s timeoutは
  正常に発火し得る＝timeout自体はbounded bugではない）．
- **今回コード変更なし（調査のみ）**．次段はreason採取（bp
  `0x42008b18`をループ周期に合わせて確実に捕捉，または`ble_hs_hci_cmd_tx`の
  各`goto done`直前にbpして異常種別を特定）で，コントローラ側（timeout）か
  トランスポート/ホスト側（mismatch）かを確定させてから修正方針を決める．
  ＝**boundedソフト（reset loop）だが，proximate trigger（0x200a complete
  未達 or 取り違え）の層をreasonで確定させる段階＝一旦区切って判断を仰ぐ**．

#### ★★訂正：reset loopではなく「デッドロック」＝2s HCI timeoutが発火しない（2026-07-08 reason採取）
`ble_hs_cfg.reset_cb`にアプリ`on_reset`を登録し，reasonをグローバル
（`g_reset_reason`/`g_reset_count`）＋RTC番地`0x6000805c`へtiming非依存で
記録する方式で採取したところ，**前節の「reset loop」解釈も誤りと判明**：
- **`g_reset_count=0`＝`on_reset`が一度も呼ばれない＝`ble_hs`のfull reset
  （`ble_hs_sched_reset`→`ble_hs_reset`→`reset_cb`）は起きていない**．
  正しいアドレス（再ビルドでシフト：`ble_hs_sched_reset`=`0x42008b1c`,
  `ble_hs_startup_go`=`0x4200ab72`, `on_reset`=`0x420001ca`）でbpしても，
  steady stateで`on_reset`/`sched_reset`/`startup_go`はいずれも**発火しない**．
  前節の「`startup_go`が繰り返す」は初回startupの一過性トラフィックの
  誤読だった（`ble_hs_cfg`の`reset_cb`=`on_reset`は正しく配線済みを確認）．
- **＝実体は静かなデッドロック**：`ble_gap_adv_start`が`ble_hs_hci_cmd_tx`
  (0x200a)の**ackセマフォ待ちで永久ブロック**．`g_adv_rc`=-1のまま，
  `on_reset`未発火，reset無し．
- **★真のbounded bug＝2s HCI timeoutが発火しない**：`ble_hs_hci_wait_for_ack`は
  `ble_npl_sem_pend(sem, ms_to_ticks32(BLE_HCI_CMD_TIMEOUT_MS=2000))`＝2s
  timeoutで待つ（`ms_to_ticks32`はUSE_ESP_TIMERで`ms`をそのまま返す＝2000→
  `xSemaphoreTake(handle,2000)`→`esp_shim_sem_take(sem,2000)`→
  `tick_to_tmo(2000)=2,000,000μs`→`twai_sem(sem,2s)`）．**本来2sで
  E_TMOUTするはずが，分単位ブロックし続ける＝timeoutが効いていない**．
- **疑い＝ASP3カーネルのHRT時刻が進んでいない**：`_kernel_current_hrtcnt`
  (`0x3fc808ec`)を~2.4s間隔で3回読むと**`0x00023d06`のまま凍結**．
  カーネルtimeout（`twai_sem`）はカーネル時刻基準のため，時刻が進まないと
  timeoutが永久に発火しない（esp_timerはSYSTIMERを直読みするので別系統＝
  D-1/D-2aで露見しなかった）．**ただしtickless HRTの遅延更新
  （lazy cache）の可能性も残る＝LIVE HWタイマ直読みでの確定は，
  advertising RF活動中のCDC/USB-JTAG不安定でOpenOCDが落ち，未完**．
- **切り分けの結論**：reasonを直接採れないが，論理的に「静かなデッドロック
  ＝completeが来ず，誤ったcompleteも来ない」＝もしtimeoutが効けば
  reasonは`BLE_HS_ETIMEOUT_HCI`＝**0x200a completeが本当に上がらない
  ＝コントローラ/トランスポート下層**（コーディネータ判定木のETIMEOUT側）．
  **＋target側boundedバグ2件が重畳**：(A) 2s HCI timeoutが効かない
  （カーネルHRT時刻凍結の疑い），(B) それゆえ静かにハングしreason採取も
  ブロックされる．
- **次段候補（target側で手が入る）**：(A) カーネルHRT時刻凍結の確定
  （LIVE SYSTIMER直読み，またはBLE中に`_kernel_current_hrtcnt`が更新
  されるか＝カーネルtimer割込みがBLE中に発火し続けるか）．凍結が実なら
  ＝BLE controllerがASP3のカーネルtimer（SYSTIMER比較器/割込み）を
  阻害している疑い＝target側で回避可能かも．(A)を直せばtimeoutが効き
  →cmd_tx→`sched_reset(ETIMEOUT)`→`on_reset`でreason確定＝
  「completeが来ない」を最終確認でき，下層(controller)へ進むか判断できる．
- **本ラウンドの変更**：`apps/ble_host_smoke/ble_host_smoke.c`に
  `on_reset`のreason記録（`g_reset_reason`/`g_reset_count`/RTC`0x6000805c`）を
  追加（調査計装．RAM 92.38%不変）．**HRT時刻凍結の確定・修正は未達＝
  下層(controller) vs target側(カーネルtimer)の最終判定を仰ぐため区切る**．

#### ★HRT凍結調査（2026-07-08）：S3非同根＋「静かなデッドロック」も訂正＝CPU 100%ビジー
並行S3テストの結果を受けて（**C3 WiFiは90秒アイドル接続でHRT凍結せず＝
S3アイドルフリーズとは非同根**．「RF一般がC3カーネルHRTを止める」機構は否定），
BLE-adv固有 vs 計測アーチファクトの2択に絞りSYSTIMER HW生カウンタ直読みを
狙った．
- **観測法**：advertising中はJTAG halt/reset-haltでOpenOCDが必ず落ちる（RF活動）．
  → **esptool `read-mem`（`--no-stub`．ROMローダ経由＝JTAG不要）でRTC番地を
  事後読み**する方式が有効（`0x60008050`のsyncマーカ=`0x5ade51c0`が
  download-mode reset後も残存＝RTC domainは生存・read-mem正常を確認）．
  （注：RAM番地`0x3fc8xxxx`はROMローダがDRAMを踏むため read-mem では不定＝
  RTC番地のみ信頼できる．）
- **最低優先度プローブタスク（prio16, busy-loopでSYSTIMER直読み→RTC記録）を
  投入したが，RTCプローブ番地(`0x60008054/58/5c`)は全て0＝プローブが一度も
  走っていない**＝**advertisingスタック中，CPUは最低優先度タスクにCPUを
  一度も渡さない＝100%ビジー**（過去のPCサンプルが常にint-entry/ROMで
  dispatcher idleでなかったのと整合）．**＝「静かなデッドロック」という
  前節の解釈も訂正：実体はCPUが100%ビジー（BLE ISR/blob活動が飽和）で
  ホストタスクだけがcmd_txでブロック，という状態**．
- **割込み優先度（静的確認）**：カーネルタイマ＝CPU線16／INTMTX優先度2
  （`INTPRI_TIMER=TMAX_INTPRI-1=-2`），BLEコントローラ＝CPU線1／INTMTX優先度2
  （`bt_shim.c:410` `BT_INTMTX_PRI_REG(1)=2`）＝**同一優先度**．同レベルは
  互いにプリエンプトしないが，BLE ISRが連続発火（advイベント）すると同レベルの
  タイマISR(線16)が遅延・枯渇し得る＝2s HCI timeout未発火（HRT/tmevtヒープが
  処理されない）の候補機構．
- **切り分けの到達点**：(1) S3非同根＝確定．(2) 「RF一般でHRT凍結」＝否定．
  (3) BLE-adv中はCPU 100%ビジーでlowest-prioタスク枯渇＝確定（RTCプローブ0）．
  (4) **SYSTIMER HW生カウンタが実際に凍結か（(a)）／進んでいるか＝キャッシュ
  artifact（(b)）は，プローブがCPUを貰えず・JTAG不可・read-memはsnapshot
  不可（UNIT0_UPDATE書込み不可）のため未確定**．有力仮説＝BLE-adv中の
  連続ISR/飽和でカーネルタイマISR(線16)処理が遅延/枯渇→HRT/timeout未処理
  （＝(a)寄りだがHW停止か処理遅延かは未分離）．
- **次段候補**：(i) 高優先度でゲートした単発プローブ（syncフラグ後に
  高prioで一瞬だけSYSTIMERを数回サンプル→RTC→ブロック）でHW生カウンタの
  進行を捕捉，(ii) 頻繁に呼ばれるシム関数（`esp_shim_sem_take`等，コントローラ
  タスク文脈）にSYSTIMER記録を差し込みビジー状態中の値を採取，(iii) カーネル
  タイマ優先度をBLEより上げる（INTMTX優先度）実験でtimeoutが復活するか＝
  枯渇仮説の検証．いずれもtarget側で可能．**ただしproximate root（0x200a
  completeがコントローラから来ない＝下層）と重畳しており，HRT/timeoutを
  直しても「adv失敗が綺麗にreason=ETIMEOUTで返る」までで，adv実効化には
  0x200a complete問題（controller/ROM下層）の解決が別途必要＝区切って判断を仰ぐ**．
- 本ラウンド追加計装：`probe_task`（prio16, HRT_PROBE，RAM 92.71%）＝
  SYSTIMER直読みプローブ（本ラウンドでは枯渇して無走行だが，(i)(ii)方式へ
  転用可能）．bt_smoke_hw/wifi_dhcp_hwは`ble_host_smoke`非使用で非回帰不変．

#### (A)(iii) 割込み優先度修正の結果（2026-07-08）：効果なし＝真因は割込み優先度ではなくホストタスクのスターブ
承認を受け，BTコントローラISR（CPU線1）のINTMTX優先度を2→1に下げ，
カーネルタイマISR（CPU線16・優先度2）が**プリエンプトできる**ようにした
（`bt_shim.c` esp_intr_alloc）．逆アセンブルで変更が実バイナリに入って
いることを確認（`sw a7(=1), 0x600c2118`）．
- **結果：2秒HCIタイムアウトは依然発火せず**（`on_reset`計装＝タグ付き
  RTC`0x60008050`書込みで確認．adv後8秒でも`0x60008050`=`0x5ade51c0`
  ＝syncマーカのまま＝`on_reset`が一度も走らない．`BLE_HS_ETIMEOUT_HCI`=19
  も出ず）．
- **★決定的な気付き＝優先度は的外れ**：仮にタイマISRが発火してtwai_semの
  タイムアウトを処理しても，`ble_hs_hci_cmd_tx`はタイムアウト後に
  `ble_hs_sched_reset`をホストeventqへ**enqueueするだけ**で，その
  reset eventを処理するのは**ホストタスク**．ホストタスクは
  advertising中スターブしている（プローブ枯渇＝CPU 100%ビジーで確定）
  ため，タイムアウトが効いても`on_reset`は走れない＝優先度修正では
  ETIMEOUTを取り出せない．**真のブロッカーはホストタスクのスターブ
  ＝CPUが100%ビジー**であること．
- **CPU 100%ビジーの正体（推定）＝adv-enable後の割込みストーム**：
  過去のPCサンプルが常に`core_int_entry`（割込み入口）＋ROM（ISRハンドラ）
  で，dispatcher idleに達しない＝**連続する割込み（BT ISR線1）が
  CPUを占有**している疑いが濃い．adv-enable(0x200a)後にコントローラが
  クリアされない割込みを出し続ける（storm）→CPU飽和→ホストタスク
  スターブ→adv_startが戻らず・completeも来ず・on_resetも走れない，
  という連鎖．enable/sync期は割込みが正常処理されていた（sync到達）ので，
  **adv-enable固有**．
- **優先度変更は効果未確認のため元(2)へ戻した**（コメントは残置）．
  BTコントローラのRFタイミングを乱すリスク（未実証の変更）を避ける．
  bt_smoke_hw/wifi_dhcp_hw 0エラー再ビルド確認（非回帰）．
- **切り分けの到達点＝(B)＝コントローラ/ROM下層に収束**：真のブロッカーは
  「adv-enable後のCPU飽和（割込みストーム疑い）＋0x200a complete不在」で，
  いずれもコントローラのadv-enable後の挙動に起因＝**(B)コントローラ/ROM
  下層**．(A)target側（タイマ/優先度）はブロッカーではないと確定．
- **次段候補**：(1) 割込みストームの確定＝どのCPU線/ソースが連続発火して
  いるか（INTMTX status/`0x600C2xxx`），target側のISRクリア（`esp_shim_set_isr`
  経由のINTMTX割込みクリア）が漏れていないか＝**target側で手が入る可能性**．
  (2) コントローラがadv-enableで何を出し続けるか（blob/ROM＝deep）．
  **(1)（割込みストームのソース特定＋クリア漏れ）はboundedソフトの可能性が
  あり，adv実効化に直結し得る＝ここを次に見る価値が高い．ただしJTAGが
  RF中に落ちる観測制約下＝計測法を工夫する必要．deep ROM((2))前に(1)を
  尽くすか，一旦区切って判断を仰ぐ**．

#### (1) 割込みストームのソース特定＋ISR-clear監査（2026-07-08，board B `...C2:60`）

観測法＝**ISRディスパッチ計装＋RTC STOREレジスタ事後読み**（JTAGはadv RF中に
死ぬので不可）．`esp_shim.c`の`shim_int_dispatch`（CPU線1発火の共通入口）に
実行時フラグ`esp_shim_isr_storm_probe`（既定0＝WiFi/bt_smoke非回帰，appが1で有効）
を追加し，発火ごとにRTC STORE4-7へ記録．esptool `read-mem --no-stub`（download
モードのROM文脈でRTCは生存）で事後読み．
- **RTC STORE生存性の罠**：0x600080BC(STORE5)は**usb-reset時にROMが0x13121312で
  上書き**するため使用不可．0xB8(STORE4)/0xC0(STORE6)/0xC4(STORE7)と0x50-5c
  (STORE0-3)は生存（実測）．計装は生存regのみ使用．

**確定した観測（決定的）**：
1. **ストームは実在・巨大**：`esp_shim_int_count[1]`が~8秒で3490万〜857万回
   ＝**約100万〜390万回/秒**（160MHzで~40〜160cycle/割込み＝ほぼ連続再入）．
   正常advイベント（数十回/秒）とは桁が全く違う＝CPU飽和の正体＝ホスト
   タスク飢餓の直接原因．sync marker(0x50=0x5ADE51C0)は在＝on_syncは走り
   advは開始されている．
2. **ストームsource＝`ETS_BT_BB_INTR_SOURCE`(番号5)＝BT BaseBand**（level）．
   RWBLE(8)でもRWBT(7)でもなくBT_BB．**emi.c:164のBB(0x60031xxx)と同一系統**．
   （`bt_shim.c`の`esp_intr_alloc`でblobが登録する`source`をRTCへ記録して確定．
   INTMTX mapレジスタはusb-resetでリセットされ事後読み不可のため計装で採取．）
3. **INTMTXクリアは効いている（(ii)＝シムINTMTX-clear欠落は反証）**：blob ISR
   実行**後**のINTMTX `CPU_INT_EIP_STATUS`(0x600C2110)＝**0**＝線1のpendingは
   ISR後に落ちる．INT_TYPE(0x600C2108) bit1=0＝線1はlevel．「clear書込みが
   ドロップしpendingが立ちっぱなし」型（emi.c:164の純粋なクロックゲート-clear-drop）
   なら0x600C2110 bit1が残るはずだが，残らない＝**BB sourceは一旦deassertし
   即座に再アサート**＝真の再トリガ型ストーム．

**(i) クロックビット追加実験＝REFUTED**：BBが動作準備不足でエラー割込みを
再発火する仮説の下，`esp_shim_bt_clock_init`の最小マスク`0x00031840`(EM-init用)に
`SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M=0x0078078F`（WiFi/BT共有のmodem/FE/PHY
データパスクロック；phyが不安定化したbit4(SDIO)/WiFi-MAC系は非含有）を加えて
`0x007B1FCF`にした．結果：**ストーム継続**（count=0x007f8fb6≈835万，~1M/s），
**adv_startも返らず**（0xC4のadv-return marker=0），advmark(0x54)=0，source依然5，
sync在＝phy/enableは進むが単純なCOMMONクロック欠落説は**反証**．最小マスクに戻した．

**初期トリアージ結論**：(ii)シムINTMTX-clear欠落＝反証（clearは効く）．(i)単純な
COMMONクロックビット欠落＝反証．残る候補＝**(i')別クロック**（lpclk/modem別
レジスタ，例bt.c cfgの`lpclk_sel`＝BLE低電力クロック源；BB動作準備に別系統の
クロックが要る可能性），**(ii')blob ISRハンドラ／登録の齟齬**（登録handler/argや
BBステータス読み手順），**(iii)blob/RF下層でBBが再トリガ**（RF/PLL未ロック等で
BBが"not ready"エラーを連発＝deep）．次段の推奨＝(a) blob ISR実行前後でBB
ステータスレジスタ(0x60031xxx系)を計装して**何のBB条件が再アサートするか**特定，
(b) board A NuttX-C3-BLEを焼き直しadv送信到達時のBBクロック/status/lpclkを差分
（★board A serial `...C9:88`・port 13333系・**board Bと混同厳禁**），(c) 非接続
advやDTM TXで切り分け．**判断待ち**（deep ROM前に(i')(ii')を尽くすか）．

計装（すべて非回帰・診断のみ）：`esp_shim.c`＝`esp_shim_isr_storm_probe`(既定0)
＋`shim_int_dispatch`のRTC記録，`bt_shim.c`＝`esp_intr_alloc`でsource→0xC0記録，
`ble_host_smoke.c`＝`#ifdef HRT_PROBE`でフラグ有効化＋adv-return marker(0xC4)．

#### (1)(a) BBステータス計装＝再アサート条件の特定（2026-07-08，board B）

blobのBT_BB ISRを逆アセンブルしBB割込みレジスタ群を特定：**BB割込みは
`0x60011000`ブロック**（emi.c:164のEM `0x60031xxx`とは別）．
`bb_hw_intr_set`/ROM `r_bt_bb_intr_mask_set`より **0x60011084=mask/enable,
0x6001108c=status, 0x60011090=clear**．`bb_int_init`は`bb_hw_intr_set(0x10400)`
＝**bit10+bit16のみをenable**．登録ISR＝`bt_bb_isr_wrapper`→ROM
`r_bt_bb_isr`(0x40009ac2, esp32c3_rev3_rom.elf)．ROM ISRもblobの
`r_bt_bb_isr_hack`も**同じ0x6001108cを読む**（ROMはbit18/19,hackは1/10/15/16/18/19）．
両者とも末尾で `r_plf_funcs_p[11](5)`（source5のCPU側EOI）を毎回呼ぶ．

実機計装結果（`shim_int_dispatch`拡張・RTC事後読み，複数ラウンド）：
1. **ストームは99.997%が spurious**：総ディスパッチ3.8M回のうち BB status
   `0x6001108c`≠0 は**わずか113回**（全てbit10）．113回/9s≈12回/s＝正規のBLE
   advイベント（bit10）．**残り3.8M回はstatus=0**＝BB eventレジスタに原因bitなし．
2. **enable=bit10+bit16のみ**でbit16はsticky-ORに一度も出ず＝この
   BB eventブロックからは最大でもbit10の~113回しか割込みは出ないはず．
   → **3.8M/sのストームはこのソフト可視のBB eventレジスタ由来ではない**．
3. **ISR実行後EIP_STATUS(0x600C2110)のsticky OR=0**（3M回全て）＝BT_BB線(bit1)は
   毎回blob ISRで確実にdeassertされ即再アサート＝ack不能の立ちっぱなしではなく
   **真の再トリガ**（EOIは効く．(ii')ISRハンドラ/登録/clear齟齬＝反証）．

**(i') lpclk仮説＝REFUTED**：`cfg.sleep_mode=0/sleep_clock=0`→bt.c:1656で
`lpclk_sel=ESP_BT_SLEEP_CLOCK_MAIN_XTAL`（常時稼働の40MHz水晶）．bt.c:1725
`btdm_lpclk_set_div(esp_clk_xtal_freq()/MHZ)`が分周レジスタ**0x600C0020**低12bitへ
divを書く．実機計測：**0x600C0020=0x28=40**＝div正常（`esp_clk_xtal_freq()`は
Direct Bootでも40を返す，lpclk=40MHz/40=1MHz正常）．「Direct Bootでxtal_freq=0
→div=0→lpclk破綻」説は反証．またストーム3.8M/s(≈285ns周期)は1MHz lpclkより
はるかに速い＝lpclk駆動タイマ事象では有り得ない．

**切り分け結論**：(i')lpclk・(i)COMMONクロック・(ii')ISRハンドラ＝**全て反証**．
残るは**(iii) BT_BB線(source5)がハード層で連続アサート**（BB eventレジスタ
0x6001108c=0のままsource5線が立つ）．lpclk正常・BB event処理正常なので，
**BBハードウェア自体がadv実TX時に割込み線を連続assert**している＝
RF/PLL/BB下層（emi.c由来の未完了RF-calがadv-TXで露呈する線と整合）．target側で
readily触れる(i')(ii')は尽くした．**判断待ち＝deep RF/BB層**．

推奨する次段（definitive）：**board A に NuttX-C3-BLE を焼き直し**（★board A
serial `60:55:F9:57:C9:88`・port 13333系・board B `...C2:60`と混同厳禁；S3テストで
wifi_dhcpを焼いた可能性ありゆえ要焼き直し），NuttXがadv送信到達する瞬間に
BB割込みレジスタ(0x60011084/8c/90)・source5線・RF/PLL/modemクロック状態を
board Bと差分＝欠落を一意特定．代替：ROM `r_bt_bb_isr`到達時の live JTAG halt
（ただしRF中はJTAG不安定）．

(a)フェーズ計装（非回帰・診断のみ，runtime flag/#ifdef既定OFF）：`esp_shim.c`
`shim_int_dispatch`＝0xB8総数/0xC0(用途はラウンド毎)/0xC4=BB status sticky OR，
`bt_shim.c` `esp_intr_alloc`＝0xC0/0xC4を0初期化．**keeper機能修正なし**
（lpclk等は既に正常＝変更不要）．

#### (1)(b) 2ボードNuttX差分：★関門＝NuttXはadv送信できる（2026-07-08）

board A（serial `60:55:F9:57:C9:88`）へ D-1で使ったNuttX-C3-BLE
（`/home/honda/.claude/jobs/494f98a3/tmp/nuttx-c3ble/nuttx/nuttx.bin`，
`CONFIG_BASE_DEFCONFIG=esp32c3-devkit:ble`, `CONFIG_ESPRESSIF_BLE=y`,
`CONFIG_ESPRESSIF_SIMPLE_BOOT=y`＝nuttx.bin@0x0でDirect起動）を焼き直し．

★**決定的結果**：**ホスト側BLEアダプタ(hci0)で `bluetoothctl scan le` すると
`60:55:F9:57:C9:8A "NuttX"` を強RSSI(-39〜-41)で連続検出**＝board Aの
BLEアドレス(base C9:88 + 2)．NuttXは**ストームに陥らず安定してadv送信できる**
（このNuttXビルドはboot時に自動でadvを開始，consoleなしで確認）．同一blob・
同一ボード種別でNuttXがadv可・ASP3(board B)がBT_BBストームでadv不能．

⇒ **大分岐の確定：ASP3のBT_BBストームは blob/RF本質問題ではなく，ASP3統合
（Direct Boot＝ESP-IDFスタートアップ非経由）の target側欠落**．NuttXが
adv-TX時に行っている RF/PLL/BB/クロック/リセット系の初期化のうち，ASP3が
skipしているものがある（emi.c:164と同系統＝Direct Bootがesp_perip_clk_init/
RF-PHY正規初期化列を経ないことに起因の可能性大）．

計測法メモ：**NuttX console=UART0**（`CONFIG_UART0_SERIAL_CONSOLE=y`）で
native USB-JTAG(ttyACM)には出ない＝ttyACM open はD-stateハング．が，
**ホストBLEスキャナでadv可否は外部から一意確認できた**（console不要）．
board A=ttyACM4(C9:88)/board B=ttyACM3(C2:60)，by-idで厳密区別．

次段（判断待ち）：(3) NuttX(adv正常)とASP3(storm)で**adv-TX時点**の
BB int(0x60011084/8c/90)・source5線・RF/PLLロック/PHY-cal・
MODEM/`SYSTEM_WIFI_CLK_EN(0x60026014)`/電源ドメインを差分してASP3の欠落を
一意特定．NuttX側はソース編集可（scratch tree）ゆえ同種RTC計装 or adv到達
フックを入れ得る（ただしNuttX rebuild環境要確認：前回buildログでesptool.py
未検出）．差分がtarget設定可能なら補完してadv開通を狙う．

#### (1)(c) 2ボード単発JTAGスナップショット差分＋live-poke（2026-07-08）

手法：両ボード定常状態で単発halt→レジスタ一括read→resume（OpenOCD
`v0.12.0-esp32-20260703`, `board/esp32c3-builtin.cfg`, `adapter serial`で
A/B厳密区別）．RF中でも定常の単発halt/readは成功（連続samplingで死ぬのとは別）．
さらにASP3(board B, storm継続中)へ**live-poke**（halt→mww NuttX値→resume→
1.5s後halt→storm count 0x600080B8のΔで停止判定）＝reflash不要で各仮説を即検証．

**差分（NuttX board A adv正常 vs ASP3 board B storm）と検証結果：**
| reg | NuttX(A) | ASP3(B) | 判定 |
|---|---|---|---|
| 0x60026018 SYSCON WIFI_RST_EN | 0 | 0 | **同一＝reset説(coordinator#1)反証** |
| 0x60026014 WIFI_CLK_EN | ffffffdf | ffffffff(bit5余分) | poke→**storm継続=反証** |
| 0x600C0010 PERIP_CLK_EN0 | 71806007 | f9c1e06f(reset既定) | poke(NuttX値)→**storm継続=反証** |
| 0x600C0018 PERIP_RST_EN0 | 886d9be8 | 0 | 差分bit=SPI/UART/I2C等(非BB/RF)＝無関係 |
| 0x600C001c PERIP_RST_EN1 | 1ce | 1fe(bit4,5余分) | bit4,5=CRYPTO_DS/HMAC reset(非RF)＝無関係 |
| 0x60011084 BB int mask | 0 | 10400(bit10+16) | poke→**storm継続=反証**（BB eventブロック無関係を再確認） |
| 0x6000E044 ANA_CONFIG | 00fbffff | 00fffdff(bit9/18) | poke(NuttX値)→**storm継続=反証** |
| 0x6000E040/48 ANA他, lpclk div | 一致 | 一致 | — |
| 0x60011060-78,b0 BB動作reg | rich値 | ~0 | NuttXがadv実行中の**動作状態**（volatile,root-cause config非該当） |
| 0x600C0008 CPU_PER_CONF | 5 | d(bit3) | CPUクロックconf(非BB) |

**★step-3結論（(iii)deep確定）**：coordinator最優先のreset側は
SYSCON WIFI_RST(0x60026018)＝両者0で反証．SYSTEM PERIP_RST_EN0/1の差分は
SPI/UART/CRYPTO等の非RF/BBペリフェラルで無関係．clock(WIFI_CLK_EN,
PERIP_CLK_EN0)・BB int mask・ANA_CONFIG(RF-analog)は**全てNuttX値をlive-poke
してもstorm継続＝反証**．**アクセス可能なトップレベルconfigレジスタの差分は
ストームの原因ではない**．残る差分はNuttXがadv実行中ゆえの動作状態(volatile)．
⇒ ストーム原因は**より深いRF/analog状態**（I2C_MST経由でregi2c書込みされる
PLL/AGC/RF trim値，またはRF-cal状態・シーケンス＝直接MMIO不可）にある．
これはC6の66-round deaf-RX調査と同一のregi2c/analog領域，かつemi.c期の
「regi2c/ANA-config差分は値forceでは直らない(5ac727e)」と整合．
**Direct BootがESP-IDFの完全なPHY/RF-cal初期化列を経ないことに起因**
（register_chipv7_phy到達はするがadv実TXに要るRF状態が不完全）．
target側の単純レジスタ設定では開通せず＝**(iii)deep RF/RF-cal層確定，判断待ち**．
次段候補：regi2c block(0x6b/0x66/0x6d)のtransaction単位比較（C6手法流用），
またはPHY-cal-data/phy_init_data・esp_phy_enable列の忠実再現．

#### (1)(d) regi2c RF-cal trim 2ボード比較＝★同一silicon上でSW差確定＋trim差検出（2026-07-08）

**★重要な交絡の発見と反証（rigor: cross-platform disambiguator）**：JTAG examineで
**board A=chip rev v0.0／board B=rev v0.3＝別silicon**と判明．「NuttX(A)可・ASP3(B)不可」は
SWとrevの交絡だった．→ **board B(rev3)にNuttXを焼いて確認：`60:55:F9:57:C2:62 "NuttX"`が
ホストBLEで検出＝rev3でもNuttXは正常advertise**．⇒ **revは原因でなくSW(ASP3 vs NuttX)差で確定**．
以降のregi2c比較も**同一board B(rev3, 同一ROMアドレス)上**でNuttX/ASP3を切替えて実施＝交絡排除．

**regi2c読み出し手法（C3, 確立）**：ROM `rom_chip_i2c_readReg(a0=block,a1=host_id,a2=reg)`
=`0x40038e8e`(rev3), 戻りa0=8bit値．JTAG-ROM-call（`reg a0/a1/a2/ra`設定→`resume 0x40038e8e`
→`ra=0x40000000`にhw bp→`wait_halt`→`reg a0`）．rom_phyFuns(0x3fcdf5b8)使用ゆえphy_init後有効．

**結果（block 0x66/0x6a/0x6b/0x6d reg0-9, 各プラットフォーム2 boot）**：
- ASP3: 2 boot完全一致（決定論的）．NuttX: 0x6a[5]/0x6b[1]/0x6b[2]がboot毎に±1〜bit4変動
  ＝アナログcal比較器ノイズ(rigor実施41)．他は安定．
- **決定論的プラットフォーム差（両者2 boot安定・値相違）**：
  - **0x6d[6]: NuttX=0x9c vs ASP3=0x98**（bit2差）＝クリーンな確定差．
  - 0x6b[1]: ASP3=0x17 vs NuttX=0x1e/0x1f；0x6b[2]: ASP3=0x4b vs NuttX=0x5f/0x6f
    ＝ASP3が一貫して低い（がNuttX側がノイジーなので"クリーン"ではない・示唆）．
- **★注目パターン**：NuttXがcalノイズを示すRFブロック(0x6b)で**ASP3は完全固定**
  ＝ASP3のcal比較器が反復していない/デフォルト固定の可能性（＝Direct BootでRF-cal
  未実行/不完全の示唆）．ただし2 bootでは確証不十分＝要追加boot．

**解釈と留保**：RF-cal trim(0x6b/0x6d)がASP3(storm)とNuttX(adv)で同一silicon上で
**決定論的に相違**＝RF較正結果が異なる．ただし(a)これがstorm原因かRF-TX状態相違の
**症状**かは未確定（相関≠因果；ASP3はstorm中=RF-TX破綻中に読んだ），(b)値force無効は
C6/emi.c(5ac727e)で既知＝差の"発生源"(cal-data入力/cal実行前提)を追う要あり．
C6 deaf-RXと同一のregi2c/analog領域＝`docs/wifi-shim-c6.md`と相互参照．

**次段（判断待ち）**：(step2) cal-data**入力**の同一性確認＝ASP3とNuttXが同じ
phy_init_data・同じcalモードでregister_chipv7_phy/esp_phy_load_cal_and_initを通すか
（Direct BootはNVS cal-data非ロード＝phy_init_data既定blob使用のはず；両者同一なら
trim差はcal実行前提=クロック/PLL/温度/regi2cマスタ状態の差，入力相違ならtarget補完可）．
併せてASP3-固定/NuttX-ノイジーの追加boot確証．

#### (1)(e) ★確証：ASP3のRF-cal比較器は反復しない＋cal入力は同一（2026-07-08）

**追加boot（同一board B rev3, NuttX/ASP3切替え）**：
- **ASP3＝5 boot全て完全一致**（0x66/0=18, 0x6a/5=18, 0x6b/1=17, 0x6b/2=4b, 0x6d/6=98, 0x6d/0=f0）．
- **NuttX＝3 boot(1/2/5)で 0x6a/5=19/18/19・0x6b/1=1f/1e/1e・0x6b/2=6f/5f/5f が変動**
  （successive-approximation cal比較器のアナログノイズ）．0x6d/6=9c・0x66/0=18は安定．
- ⇒ **NuttXがcalノイズを示すブロックで，ASP3は同一silicon上でも完全固定＝ASP3のRF-cal
  比較器(SAループ)が反復していない＝calが実際に走っていない/不完全，を確証**
  （同一siliconゆえアナログノイズは物理的に同じはず＝固定は「cal未実行」の強い証拠）．

**cal入力の同一性（コード解析）**：BTビルド(ble_host_smoke)は**実phy_init.c**をリンク
（esp_bt.cmake:112 `esp_phy/src/phy_init.c`）．`esp_phy_enable`(0x42003636)は
`phy_module_enable`→`s_is_phy_calibrated`==0なら`esp_phy_load_cal_and_init`(0x42003556)
→`register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL)`を呼ぶ．
`CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE`未定義＝**NVS cal-data非ロード＝毎回
PHY_RF_CAL_FULL固定・既定phy_init_data.c使用**．emi.c期の実測で
register_chipv7_phy入口`s_is_phy_calibrated=0`は両者共通（full分岐）．
⇒ **ASP3もフルRF-cal(register_chipv7_phy)を呼んでおり，入力(init_data/mode/cal_data)は
NuttXと同一**．

**★判定**：入力同一・full-cal呼出しも同じ・にもかかわらずASP3だけcal比較器が反復
しない ⇒ **差は"cal実行前提"にある**（register_chipv7_phy実行中のクロック(cal用/
regi2cマスタ)・PLLロック・bias・温度センサ等のランタイム前提）．**入力欠落ではない**．
これはtarget側で補完し得る前提欠落の可能性が高く（C6 発見1「clk_i2c_mst_en未有効化」
＝Direct Bootがregi2cマスタ前提を飛ばす、と同一クラスの疑い；ただしregi2c *read* は
成功しているので単純な同型ではない）、かつC6 deaf-RX（同じくPHY/RFがフレーム未復調）と
**同一機序＝突破すれば両者に効く可能性大**．`docs/wifi-shim-c6.md`相互参照．

**次段（判断待ち）**：(step: cal反復の"理由"を直接掴む) register_chipv7_phy内部の
SA/cal比較ループ（regi2c write_mask→read_mask反復）にbp/計装し，**イテレーション
回数をNuttX vs ASP3で比較**＝どこで反復せず早期exit/skipするか特定．反復阻害の前提
（cal用クロック/PLLロック/regi2cマスタ/bias）を掴めればtarget補完を試す．

#### (1)(f) payoff着手：cal局所化＝BBPLLは正常・RF/AGC calが非反復（2026-07-08，未完）

**register_chipv7_phy(0x42021a72, blobだがapp flashにリンク＝逆アセンブル可)の構造**：
呼出し＝`register_chipv7_phy_init_param`, `rom_phy_bbpll_cal`(+`rom2_read/write_pll_cap`),
`get_temp_init`(温度センサ), `set_rfpll_freq`×3/`rfpll_set_freq`(RF PLL), `rf_init`(0x420211f0),
`bb_init`(0x4202196e), `rf_cal_data_recovery/backup`, `chip_v7_set_chan_offset`．

**局所化（regi2c比較より）**：**BBPLLブロック0x66のtrimはNuttX=ASP3で完全一致**
＝BBPLL cal(`rom_phy_bbpll_cal`)は正常動作．**相違はRF/AGCブロック(0x6b/0x6d/0x6a)**
＝RF/AGC calステップ（`rf_init`/`bb_init`内のSAループが有力）でASP3が非反復．
⇒ 阻害前提は「BBPLLには不要だがRF/AGC calに要るもの」＝RF PLL/RF-FE電源・bias・
温度・RF系cal専用クロックが候補．

**実機確認（bounded）**：ASP3で`reset halt`→bp register_chipv7_phy(0x42021a72)→resume
＝**bp HIT確認（PC=0x42021a72, debug_reason=breakpoint）＝ASP3もcal本体を実行している**．
（reset halt時にexamine haltエラーが出るがbp自体は発火．深追いはbare-run+単発attach
推奨．）register_chipv7_phy入口に`lbu 229(s0/s1)`→bnez skip・`lbu 286(s0)`等の
**init_param/state依存の分岐**あり（cal経路にデータ依存skipが存在）．

**★正直な現状（未完）**：cal本体到達・BBPLL正常・RF/AGC非反復まで局所化したが，
**RF/AGC calのどのSAループがどの前提でASP3だけ早期exit/skipするかは未特定**
（＝阻害前提未pin，target補完未実施，adv未開通）．これはC6 66-round相当の深さ．
**次段**：rf_init(0x420211f0)/bb_init(0x4202196e)内のregi2c write→read→比較ループに
bp/`--wrap`計装し，**同一board B(rev3)でNuttX vs ASP3のイテレーション回数と分岐条件を
比較**→ASP3が反復をやめる分岐点で読むstatus/flag/reg(RF-PLLロック/温度/cal-clock/
上記offset229/286)を掴む→target補完(bt_shim/esp_shim)．計測はreset-halt不安定ゆえ
bare-run+単発attach or 対象関数の--wrap計装(C6実施38系手法)推奨．C6 deaf-RXへ相互参照．

#### (1)(g) --wrap計装着手→SAループはROM間接（g_phyFuns）＝C6と同じ壁（2026-07-08→09，未pin）

register_chipv7_phy/rf_init/bb_init を精査し，静的に追える範囲を尽くした：
- **★offset-229分岐の再解釈（rigor: 逆アセンブル再検証）**：`bnez 229(phy_param)`のジャンプ先
  0x42021b50 は「calスキップ」ではなく**rf_init/bb_init/get_temp_initを呼ぶ本体**＝
  両分岐が合流．offset-229/286が制御するのは`phy_rfcal_data_check`/`rf_cal_data_backup`
  （cal**データ**のbackup/recovery）であって**cal実行そのものではない**．
  ⇒ **rf_init/bb_init（RF/AGC calのSAループ）はASP3でも必ず走る**．非反復はその**内部**．
- **s0/s1=`phy_param`(0x3fc80160)グローバル**確定．
- **rf_init(0x420211f0)の呼出し**：`get_iq_value`×6, `rc_cal`, `rom2_tsens_read_init1`(温度),
  `bias_reg_set`, `phy_i2c_init2`/`rom1_phy_i2c_init1`(regi2c init), `ram1_fe_i2c_reg_renew`
  (FE regi2c), `i2c_bbpll_set`, `set_chan_freq_hw_init`．
- **`get_iq_value`(0x42024868)は単なるIQ値デコード補助**（ビット抽出・符号化のみ，ループ/
  regi2c/比較器なし）＝SAループではない．
- **★SAループの所在＝ROM関数（`g_phyFuns`テーブル 0x3fcca274 経由の間接jalr）**．
  rf_init/rc_cal等はg_phyFuns経由でROMのcal本体を呼ぶため，**リンク済みblobの静的
  逆アセンブルではSA反復ループ本体を追えない**（ROM内）．

**★正直な現状（未pin・C6と同じ壁）**：非反復を rf_init/bb_init 内の**ROM間接cal関数**まで
絞ったが，そのSA反復ループ本体はROM（g_phyFuns経由）にあり静的に追えない．pinには
**ROM cal関数への動的--wrap/bp計装（C6実施38系）＋boot時捕捉＋NuttX/ASP3反復回数比較**が
必須＝C6 66-round未解決と同一の「ROMアナログcal層」．**precondition未pin，target補完
未実施，adv未開通**．計測阻害前提（reset-halt不安定・RF中JTAG不安定・SAループROM内間接）を
正直に区切る．

**次段の選択肢**：(a) ROM cal関数(g_phyFuns経由の該当ROMアドレスを解決し)への--wrap/bp
計装ラウンド（深いがpin可能性）；(b) 経験的に有力preconditionをtarget補完して試行
（tsens/bias/FE-regi2c/RF-PLL；ただし値force無効の既知ゆえ"実行前提"側を狙う）；
(c) C6が同層で66-round未解決＝深RFアナログcal層と確定しD-2bを一旦保留しD-2c(scan)等へ．
C6と共通＝`docs/wifi-shim-c6.md`相互参照．

#### (1)(h) (b)経験的precondition補完＝1ラウンド実施→定常比較可能な前提は全反証＝D-2b保留（2026-07-09）

cal実行前提を NuttX参照2ボード比較（同一board B rev3切替え）で探索：
- **★tsens（温度センサ）＝REFUTED（最有力候補）**：`APB_TSENS_CTRL(0x60040058)`
  ASP3=0x00418075 / NuttX=0x00418073＝**制御構成は同一**（差はTSENS_OUT下位のみ 0x75 vs 0x73
  ＝実温度~2LSB）．`TSENS_CTRL2(0x6004005C)`両者0xc002．**tsensは両ボードで電源ON・
  有効温度を読めている**＝cal用温度前提は欠けていない．`rom2_tsens_read_init1`線は反証．
- bias（regi2c block 0x6a）：trim一致（0x6a[5]のみNuttXノイズ）＝bias_reg_set出力OK．
- クロック/reset/ANA_CONFIG/BB-mask：既に(1)(c)(d)でlive-poke反証．
⇒ **定常状態で比較可能なcal実行前提（tsens/bias/clock/reset/ANA）は全て反証**．

**残る唯一の可能性＝cal-time固有状態**（SAループ実行中のRF-PLLロック/RF-cal状態）は
定常比較では症状/原因を分離できず（ASP3=storm中/NuttX=adv中でRFモードが違う），
pinには**bp-at-cal-time＋ROM内SAループ計装**が必須＝(1)(g)で確定した**ROMアナログcal層
（C6 66-round未解決と同一）**．

**★判定＝D-2b保留（deep RFアナログcal層＝C6と統一根）**：(b)経験的補完1ラウンドで
定常比較可能な前提は尽くし全反証．経験的target補完ではRF/AGC calを反復させられず
＝真にROMアナログcal層でtarget（submodule外）から届かない．**D-1(controller+VHCI)・
D-2a(NimBLE sync)は達成済み**，D-2b(advertising)は**深RFアナログcal層ブロック**として
characterization完了・保留．統一根（ASP3 Direct BootでRF/AGC cal SAループがROM内で
反復しない）はC6 deaf-RXと同一＝**専用の深RFセッションでROM cal計装(選択肢a)により
両者同時攻略が妥当**．C6と共通知見＝`docs/wifi-shim-c6.md`相互参照．

#### (1)(i) 選択肢a着手：g_phyFunsテーブル計装の実現可能性を確立（2026-07-09，計装未実装）

deep RF-cal（ROM cal-loop計装）をC3-BLE主戦場で進めるための**足場を確立**：
- **★g_phyFunsテーブルはRAM常駐・パッチ可能**：`phy_get_romfunc_addr`(0x42020fd2)が
  ROM `phy_get_romfuncs`(0x400018fc)でテーブルptrを得て`g_phyFuns`(0x3fcca274)へ格納後，
  **テーブル内へblob関数ptrを`sw`で書込む**（例 offset184←`rom_agc_reg_init`0x4202eea8,
  188←`rom_bb_reg_init`, 224←`rom_phy_xpd_rf`, 276←`rom_set_txcap_reg`…）．
  ⇒ **テーブルは書込可能RAM＝C6実施23/38のtable差替えトレースがC3-BLEでも可能**．
- **hook点**：`phy_get_romfunc_addr`後・`register_chipv7_phy`のcal前（両者は
  esp_phy_enable→load_cal_and_init経由で順に走る）．
- **regi2c primitive**（ROM, rev3）：`rom_chip_i2c_readReg`=0x40038e8e,
  `rom_i2c_readReg_Mask`=0x400391f4, `rom_i2c_writeReg_Mask`=0x4003922a,
  `rom_i2c_writeReg`=0x400391e6．phy cal（i2c_bbpll_set/phy_get_i2c_data/phy_i2c_init2/
  rc_cal等）は**g_phyFunsのオフセット経由の間接jalr**でこれらを呼ぶ．
- **計装対象offsetの特定法**（実装時）：実機でg_phyFuns[i]を走査し
  rom_i2c_readReg_Mask/writeReg_Maskのアドレスと一致するoffsetを探す（or phy関数の
  該当jalr直前の`lw aX,off(g_phyFuns)`をboot後にJTAG読み）．

**計装プラン（次段・未実装）**：(1) g_phyFunsのread_mask/write_maskエントリを，
target側（`bt_shim.c`か新規計装ファイル，`esp_shim`＝submodule外）でトレースラッパに
差替え＝op/block/host/reg/msb/lsb/valueをRAMリングバッファへlog（passive＝JTAG halt不要
＝RF中も安全）．hookは`esp_phy_enable`前後 or `phy_get_romfunc_addr`直後．
(2) 同一board B(rev3)でASP3実行→cal中のregi2c系列をバッファへ．esptool/JTAGで事後読み．
(3) **ASP3のSAループでread_maskが同一値を返し続ける（stuck＝比較器無応答）ブロックを特定**
＝cal-time固有の非反復の正体．(4) NuttX側は同計装不可（ソース別）ゆえASP3単独で
stuck検出＋そのブロックのwrite直後前提レジスタを確認，必要ならNuttX cal中の同ブロックを
別手段（ROM bp/--wrap）で対照．(5) 阻害前提をtarget補完→cal反復回復→adv開通試行．
計装はflag/#ifdef既定OFFで非回帰維持．**深い層＝専用集中セッション推奨（板混同/RF中JTAG
不安定/長時間ゆえ；board A=NuttX C9:88, B=ASP3 C2:60 serial厳密区別）**．

#### (1)(j) ★RF-cal regi2cトレース計装＝実装・捕捉成功＋重要な訂正（2026-07-09）

**計装実装（動作確認済・非回帰）**：`bt/phy_cal_trace.c`新規＋`esp_bt.cmake`に
option `ESP32C3_BT_PHY_CAL_TRACE`(既定OFF)．ON時のみ ROM `phy_get_romfuncs`
(0x400018fc,絶対シンボル)を`-Wl,--wrap`で捕捉→`__wrap_phy_get_romfuncs`が
g_phyFunsテーブル(0x3fcdf300)内のregi2c 4関数(readReg/writeReg/readReg_Mask/
writeReg_Mask=0x40039162/0x400391e6/0x400391f4/0x4003922a)をトレースラッパへ差替え，
op/blk/reg/msb/lsb/valをRAMリングバッファ`pct_buf`へlog（passive，JTAG halt不要）．
board B(ASP3)で実測：**pct_swapped=4・pct_count=987・pct_magic=PCT1＝cal全regi2c
系列987件を捕捉成功**（<2000ゆえ溢れ無し）．非回帰：bt_smoke_hw 0-err(option OFF)．
RAM 97.6%(16KB buf,fits)．

**★★重要な訂正（rigor: 実測がこれまでの特徴づけを覆す）**：
これまで「ASP3のcal SAループが反復しない」としてきたが，**実際にはASP3のcal SA
ループは反復・sweepしている**：
- **BBPLL cal比較器＝`rdM blk=0x62 reg7 [1:1]/[2:2]`**（40回超read＝rigor実施23の
  「0x62 PLL comparator-loop」）．値は0/1変動を返す＝**ASP3で比較器は正常応答**．
  BBPLLトリム(0x66)もNuttX一致＝**BBPLL calは正常動作**．
- **RF/AGC cal(block 0x6b)のSAループもsweepする**：reg1を0x1f→0x1e→…→0x18と
  1刻みsweep→settle，reg2も上位/下位nibbleをsweep．**iterationは走っている**．
- **plain read(op0)は全て直前writeをecho**（不一致0件）＝regi2c readは書込確認で
  あり比較器ではない．⇒ coordinatorの「regi2c read_maskがstuck値」仮説は**反証**
  （regi2c比較器0x62は応答；readはecho）．

**残る核心＝RF/AGC(0x6b)の測定はMMIO経由**：0x6b sweepの判定（測定）はregi2c
readではなくMMIO status読み（本トレースに映らない）．結果，ASP3は0x6b[1]→**0x17**に
収束，NuttXは**0x1e/0x1f**（±1 noise）＝**単なるnoiseでなく収束点が別**．
⇒ **ASP3のRF/AGC cal中のMMIOアナログ測定が縮退/別値**＝RF/AGCアナログ測定が
正しく動いていない（cal-time RF frontend測定の問題）．block 0x6dはcal中regi2c 0件．

**判定＝pin一歩前進・最深部手前**：「cal非反復」は誤りで**cal SAループは走る・BBPLL
比較器は応答**．真因は狭まって**RF/AGC(0x6b)のcal中MMIOアナログ測定が縮退**
（＝RF frontendがcal時に正しく測っていない）．これはregi2cトレースの外（MMIO測定層）．
**次段**：0x6b sweep区間のMMIO read（測定status reg）を計装/bpで捕捉しNuttXと対照，
その測定を成立させるRF frontend前提（FE電源/RXパス/測定用clock）を特定→target補完．
C6 deaf-RX（RXアナログ測定が成立しない）と機序が接近＝相互参照．

#### (1)(k) ★測定チェーン完全特定＝0x6b cal測定＝TX-tone電力(FE pwdet→SAR ADC)（2026-07-09）

`phy_cal_trace.c`を拡張し各regi2c呼出しの**caller PC**（`__builtin_return_address`）も
記録（イベント3語化, PCT_N=1200, RAM97.1%）．board B(ASP3)で再捕捉（count≈1020）→
block 0x6b opsのcaller PCを解析：

- **0x6b sweepを回すcal関数＝`rfcal_txcap`（RF TXキャパシタcal, 0x42027f12, appリンク＝
  逆アセンブル可）**（他 rfpll_cap_init_cal/wait_rfpll_cal_end/rom2_write_pll_cap 等）．
  0x40039258=rom_i2c_writeReg_Mask+46（RMW内部read＝echoの正体）は測定ではない．
- **rfcal_txcap の動作**：`start_tx_tone_step`でTXトーン発生→0x6bキャパシタ値をsweep
  （デクリメント）→各試行で **`get_tone_sar_dout`(0x42024196)** で測定→測定値(SAR)が
  最大になるキャパシタ値を採用（s5=best追跡）．
- **測定チェーン**：`get_tone_sar_dout`→`pwdet_tone_start`(0x42024138, 電力検出器start)
  ＋ g_phyFuns[82]でSAR ADC出力読み(lhu sp+2)．すなわち**測定＝TXトーンの電力を
  RFフロントエンド電力検出器(pwdet)→SAR ADCで読む**．
- **`pwdet_tone_start`が触るMMIO**＝**FE(RF Front-End, 0x60006000)+0x40 bit18**(pwdet enable)，
  **RTC_I2C/ANA master 0x6000e000+0x50**（[26:24]==7 ready poll），そして測定は
  **APB_SARADC(0x60040000)**．

**★核心の特定**：ASP3が0x6b[1]→0x17に誤収束・NuttX→0x1e/1f なのは，**TXトーン電力測定
（FE pwdet→SAR ADC）がASP3で縮退**しているため（測定が最大点を正しく示さない→誤キャパ
採用）．**具体候補レジスタ領域＝RF Front-End(0x60006000)・SAR ADC(0x60040000)**．
SAR ADC(0x60040000)は既に定常比較でNuttX(0x580000c0)/ASP3(0x40038240)と**相違を実測**
（(1)(h) tsens調査時）．FE(0x60006000)領域は未比較＝次の一手．

**★C6 deaf-RXとの直結**：C6のRXアナログ測定不成立（AGC 0x600a7xxx）と機序が一致
＝**アナログ測定パス（検出器→ADC）がDirect Bootで成立しない**．FE/SAR/pwdet前提を
掴めばC6にも移植可（同機構・C6版アドレス）．`docs/wifi-shim-c6.md`相互参照．

**判定＝測定機構をpin（最深部の一歩手前）**：cal測定＝TX-tone-power(FE pwdet→SAR)まで
完全特定．真因＝この測定パスの縮退．**次段**：FE(0x60006000)＋SAR(0x60040000)＋
pwdet(0x6000e050)のcal時状態をNuttX/ASP3対照（cal時＝bp or トレース拡張で
get_tone_sar_dout戻り値＝測定値そのものをlog）→測定を成立させる前提（FE電源/pwdet bias/
SAR clock/mode）を特定→target補完でASP3測定値がNuttX並みに→0x6b収束点回復→
storm停止・adv開通試行．

#### (1)(l) ★★重要な反証：RF-cal測定は正常＝縮退説は誤り，RF-calは storm 原因でない（2026-07-09）

`phy_cal_trace.c`をさらに拡張し**測定値そのもの**（`get_tone_sar_dout`が読むSAR ADC出力＝
g_phyFuns[82]=0x4003a2cc をwrap，測定値=*(u16*)(buf+2)）をlog（op=4）．board B(ASP3)で
rfcal_txcapの0x6b sweep中の cap→SAR測定 を捕捉（swapped=5, SAR events 347件）．

- **★測定は正常＝山なり（responsive）**：reg1 sweep 0xf→0x8 で SAR=1565→1595→1641→1661→
  1692→1702→**1704(peak@0x9)**→1678（明確な山なり）．reg2も同様に山なり．**TXトーン電力
  測定(FE pwdet→SAR)はASP3で正しく応答しcalは極大を探索・収束している＝測定は縮退して
  いない**．⇒ **(1)(i)-(k)の「ASP3のcal測定が縮退」仮説は反証**．
- **0x6b→storm 因果テスト（live-poke）＝支持されず**：storm中ASP3へ 0x6b[1]=0x1e/0x6b[2]=0x5f
  （NuttX値）をROM writeRegでpoke→resume．結果，chipがreset（storm count 0x03946a7f→
  reboot後0x00069a13）し**storm再発**（~288k/s）．poke自体がRF擾乱でresetを誘発＝やや
  inconclusiveだが，**0x6b trimをNuttX値にしてもstormは止まらない**．

**★★判定＝RF-cal線は反証・storm根本ではない（大きな軌道修正）**：ASP3のRF-cal(TX-tone
電力測定→cap探索)は**正常動作**．ASP3 0x6b[1]=0x17 vs NuttX 0x1e の差は**良性のcal変動**
（測定は山なりで健全，多パス探索の着地＋noise）であり，**stormの原因ではない**．
(1)(a)以降「regi2c trim差＝storm根本」と相関から仮定し3ラウンド深掘りしたが，
**測定が正常＝相関≠因果でRF-calは無罪**．storm(BT_BB source5 spurious, status0)の
真因は**依然未特定**（clock/reset/ANA/BB-mask/lpclk/RF-cal＝全て反証済み）．

**次段の再考が必要**：RF-cal層は袋小路と判明．storm根本は別系統＝(A)BT_BB割込みが
status0でsource5線に立つ低レベルHW要因（INTMTX/割込み配線/coex/NMI系）を再検討，
(B)NuttX(adv可)とASP3(storm)でadv-enable(0x200a)**直後**のBT_BB/modem状態をより広く差分，
(C)そもそもstormが致命かを再評価（storm下でもadvパケットが少しでも出るか host hci0で確認）．
計装(phy_cal_trace)は診断用途としてoption OFF既定で温存．C6 deaf-RXとの結合は
「RF-cal共通」ではなくなった＝再評価要．

#### (1)(m) (C)over-the-air確認＝storm下でASP3は電波を出していない（2026-07-09）

安価な決定テスト：board B(ASP3, 通常advビルド=trace OFF, RAM92.71%)を焼き直し起動→
**storm状態を確定**（sync marker 0x60008050=0x5ADE51C0＝on_sync実行, storm count
0x600080B8=1.7M, PC=0x42029dd0＝bt_bb_isr_wrapper内＝storm中）→host hci0
`bluetoothctl scan le` 50秒．
- **★ASP3(C2:62 / "ASP3-C3-BLE")は50秒スキャンで全く見えず**（scannerは正常＝
  NuttX C9:8A を-55で検出）．
- ⇒ **storm下でASP3の無線はadvを出していない**．コントローラのadvスケジューラ(lld_adv)が
  回れていない＝**storm(CPU飽和)がホストタスクだけでなくコントローラのadv送信自体も阻害**．
  （測定artifact留意：flash失敗/ボード不良状態では偽陰性ゆえ，JTAGでstorm確定後に
  再スキャンして確定．NuttXが同スキャンで見える＝scanner健全．）

**★判定＝storm自体がブロッカー（電波不出）**：楽観シナリオ「無線は出てるがホストhangだけ」は
**否定**．storm(BT_BB source5 spurious status0)を止めることがadv開通の前提．D-2bの本丸は
**stormの根絶**＝相変わらず根本未特定（clock/reset/ANA/BB-mask/lpclk/RF-cal全反証）．
次段はstorm根本＝低レベルHW（BT_BB source5がstatus0で立つINTMTX/割込み配線/coex/NMI系，
またはadv-enableでコントローラがBBに要求し続ける下層）に回帰して再攻略．

#### (1)(n) ★最後の焦点ショット：coex角度＝反証．D-2b保留を最終確定（2026-07-09）

**背景**：storm根本はclock/reset/ANA/BB-mask/lpclk/RF-cal＝全反証済み．C6-WiFi
66-round調査の根本原因（`986ff62`＝`coexist_funcs`no-op化→coex PTI=0→MAC割込み
ブロック）と同型がC3-BLEのBT単体ビルドにもないか，という最後の未検証角度．

**検証1：coexist_funcs no-op化の有無**（board B, ASP3, JTAG live-read）：
- `esp_shim_coex_pre_init_ret=0, _entered=1, _done=1` ＝ **`coex_pre_init()`は
  正常に呼ばれ成功で返ってきている**（C6のno-op化パターンとは異なる）．
- `coexist_funcs = 0x3fc81258` ＝ **ダミーno-opテーブル(`dummy_coexist_table`,
  別アドレス)ではなく，coex_pre_init()が設定した実テーブル**．
- ⇒ **C6型の「coexist_funcs no-op化」バグはC3-BLEには存在しない**
  （このファイルは既にC6向けfix=`986ff62`/`de5cb2e`を含む共有実装で，
  C3側でも同じ経路で正しく初期化されている）．
- **C3にはC6の`0x600a4dd8`相当のPTI MMIOレジスタが存在しない**（C3は旧世代
  coexアーキテクチャ＝closed-source `libcoexist.a`が内部管理，公開ヘッダに
  PTIレジスタ定義なし）．直接のレジスタ比較は対象が無く適用不可．

**★副次的発見（バグ・修正済み，storm原因ではないが是正すべき潜在欠陷）**：
`esp_coex_adapter.c`は**C3(BT/WiFi)とC6(WiFi)で共有**されるファイルだが，
実施53/54由来のC6専用診断コードが，**C6のROM PHYFUNS表アドレス
`0x4087f954`をハードコードしたまま無条件でC3ビルドにもコンパイルされていた**．
C3の有効アドレス空間（DROM 0x3C000000-0x3C800000／IROM 0x42000000-0x42800000／
ROM~0x40000000台）には該当せず，`tbl[20]`の読み出しは**未定義領域の値**
（間欠的NULL/非NULLの真因．非NULL時はwild pointer callの危険）．実機JTAG読み
（board B）ではこの回で`entered=1,done=1,ret=0`かつ診断値レジスタ
`pre/post_regi2c_63=0xFFFFFFFF`（初期値のまま＝read_fnがNULLでスキップされ
今回は実害なし）を確認．**修正**：`#if defined(TOPPERS_ESP32C6_WIFI)`で
この診断ブロックをガードしC3ビルドから完全除外（C6の動作は不変）．
`asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c`．
非回帰：ble_host_smoke/bt_smoke_hw/wifi_dhcp_hw/**c6_wifi_scan**の4ターゲット
全て0-errorビルド確認（C6側は診断シンボル`esp_shim_coex_pre_regi2c_63`が
リンクに残存＝ガード方向が正しいことも確認）．

**修正後の実機再検証**：board Bへ再flash・起動．`esp_shim_coex_pre_init_ret=0`・
`coexist_funcs=0x3fc81258`（修正前と同一＝coex経路は無変更）・
sync marker=0x5ADE51C0（adv開始）・**storm count継続進行**（0x0041734e，
修正なし時と同様のオーダー）＝**修正はcoex経路に無害だがstormは止まらず**．

**★★判定＝coex反証．D-2b保留を最終確定**：coexist_funcs no-op化は再現せず，
PTI相当レジスタはC3に存在せず比較不可，副次的に発見したwild-pointer潜在バグは
修正したがstorm自体には無関係（今回は不発だった）．storm(BT_BB source5が
status0でspurious発火し続ける)の根本は，本セッションで踏んだ全ての角度
（クロック/リセット/BB mask/ANA/lpclk/RF-cal/ISR-EOI/割込み優先度/coex）を
反証してなお**未特定**．**D-2b（connectable advertising）は深い低レベルHW/
blob層のブロッカーとして保留を最終確定**する．D-1（controller+VHCI）・
D-2a（NimBLE host sync）は達成済みで確定．次のセッションで再開する場合は
INTMTXレベルの割込み配線精査，またはblob/ROM内部のsource5アサート条件の
より深い計装（本セッションの計装資産＝`phy_cal_trace.c`パターンを転用可）
から着手するのが妥当．C6 deaf-RXとの統合仮説（RF-cal共通）は反証済みにつき
解消——両者は**別の未解決根本**として個別に扱う．

#### (1)(o) ★★真因確定・D-2b保留解除＝`esp_intr_alloc`のsource多重登録バグ（2026-07-13，board B `...C2:60`）

**背景**：S3（Xtensa・別target層実装）が同型のadvストームを独立に再現した後，
追加調査で真因を**target層の`esp_intr_alloc`多重登録バグと確定・修正済み**にした
（`docs/s3-bt-intr-source-overwrite-fix-for-c3.md`，S3側コミット`5e6d4b3`）：
BTコントローラの`bt.c`（`interrupt_alloc_wrapper`）は`esp_intr_alloc`を
**2回（source8=RWBLE→source5=BT_BB）**呼ぶのに，旧target実装は単一static
handle＋固定CPU線1のため**2回目の登録が1回目のhandler/argを上書き**する．
結果，source8の正規割込みがsource5用handler（`r_bt_bb_isr`系）へ配送され
続け，そのhandlerはsource8のstatus/clearに一切触れないため即再アサート＝
status0 spuriousストーム．机上分析（`docs/bt-c3-resume-plan.md`）で
(i) C3の`bt_shim.c` 393-447行がS3修正前と構造完全一致，(ii) `hal/components/
bt/controller/esp32c3/bt.c`はS3と**文字通り同一ソース**＋同系列blob，
(iii) 既存の「source=5確定」観測は単一スカラ上書き記録のため呼出し回数を
判別できない（2回目のsource5が1回目のsource8を消す），と判定済み．
本ラウンドはその第一手＝**計装＋修正の同一パッチをboard BでA/B判定**．

**事前予測（パッチ前に固定）**：①`esp_intr_alloc`は2回（source8→source5）
呼ばれる（トレース=0xA1020805）②修正でストーム消滅（線1=数十/s・線2=
数百/sの正常域へ）③`ble_gap_adv_start`がrc=0で返る④over-the-airで
`ASP3-C3-BLE`検出．

**ベースライン再現（パッチ前・現行HEAD＝a06bd20そのまま・2ブート）**：
- boot1：sync marker在・**ストーム在**（0xB8=0x00a75966=10.97M蓄積）・
  adv-returnマーカ無（=`ble_gap_adv_start`ブロック）・probe task飢餓
  （0x54/58/5c全て0=最低優先度タスクが一度も回らない=CPU飽和）・
  EIP sticky=0（線1は毎回deassert）・BB status sticky=bit10のみ——
  保留時の記録と同一シグネチャ．
- boot2（時間管理下143.2s，watchdog-reset起点）：0xB8=0x02b59db4=45.46M
  ＝**約33万回/s**（保留時記録の1〜3.9M/sより低いが測定窓が異なる．正常adv
  イベント数十/sの4桁上＝ストームとして質的に同一）．over-the-air 40s
  スキャン：スキャナ正常（周囲5デバイス検出=陽性対照）・**ASP3-C3-BLEは
  不検出**＝(1)(m)と同じ．⇒ **時間経過後もベースラインは保留時と同一**．

**実装（1パッチ）**：
1. `bt/bt_shim.c`：`intr_handle_data_t`を`bt_intr_slot[2]`へ配列化し
   `cpu_line`メンバ追加．呼出し順でスロット割当て（1個目→CPU線1，
   2個目→CPU線2＝`esp_shim.cfg`の`DEF_INH(2)`/`esp_shim_inthdr_2`既存配線を
   流用．カーネルコンフィグ変更なし）．INTMTXルーティング・優先度(1)・
   ENABLEビットをスロットの線ごとに設定．`esp_intr_free/enable/disable`は
   handleから線を逆引きするper-handle操作へ（旧実装は固定線1決め打ち）．
   **計装**：呼出し累積回数とsource時系列をRTC STORE1(0x60008054)へ記録
   （bits[31:24]=0xA1マーカ/[23:16]=回数/[15:8]=1回目source/[7:0]=2回目source）．
2. `wifi/esp_shim.c`：`shim_int_dispatch`に線2発火数のRTC STORE2(0x60008058)
   ミラーを追加（`esp_shim_isr_storm_probe`ゲート＝既定0でWiFi非回帰）＝
   線2ルーティング着弾の機能的read-back（INTMTXレジスタ自体はusb-resetで
   初期化され事後読み不可のため，実配送数で検証する方式）．
3. `apps/ble_host_smoke`：RTC番地再割当て（advマーカ0x54→0x5c・connマーカ
   RTC書込み廃止・probe taskのRTC書込み廃止＝0x54/58/5cを計装に明け渡す．
   グローバル変数での観測は全て維持）．

**結果（パッチ後・2ブート，全て事前予測どおり）**：

| 指標 | boot1（~68s adv） | boot2（~35s adv） |
|---|---|---|
| 呼出しトレース(0x54) | **0xA1020805** | **0xA1020805** |
| 線1発火数(0xB8) | 2265（**~33/s**） | 1257（**~36/s**） |
| 線2発火数(0x58) | 6268（**~92/s**） | 3560（**~100/s**） |
| adv-return(0xC4下位) | **rc=0** | **rc=0** |
| advマーカ(0x5c) | 0x0ADE5000在 | 0x0ADE5000在 |
| sync marker(0x50) | 在 | 在 |
| over-the-air | **`ASP3-C3-BLE`検出** | **`ASP3-C3-BLE`検出（RSSI -55〜-60）** |

- **①物証**：`esp_intr_alloc`は**確かに2回・source8→source5の順**で呼ばれて
  いた（トレース0xA1020805＝S3実測と完全同一パターン）．「実測：呼出しは
  1箇所」という旧コメント・「source=5確定」という(1)の旧観測は，上書き記録の
  原理的盲点による誤認だったと確定．
- **②ストーム消滅**：~33万/s→**計~130/s**（約2600分の1，正常adVイベント域）．
  線1(source8)~33/s・線2(source5)~95/s はS3実測（30-32/s・150-184/s）と
  同オーダー．
- **③ホスト飢餓解消**：`ble_gap_adv_start`がrc=0で完走（0xADマーカ+advマーカ
  の独立2経路で確認）．
- **④電波**：ホストhci0の`bluetoothctl scan le`で`ASP3-C3-BLE`
  （`60:55:F9:57:C2:60`）を**2ブート＋終了処理後の再起動＝計3回独立に検出**．
  C3 BLE史上初のover-the-air adv到達．
- 0xC4のbit16（=0xAD01xxxx）はadv-return書込み後に線1診断がBB status bit16を
  ORしたもの（rcバイトとは独立・無害）．BB status bit16が観測されたのは
  初（従来はbit10のみ）＝source分離後にBB本来のイベントが正しく見え始めた
  ことと整合．

**非回帰**：`bt_smoke_hw`（D-1）・`wifi_scan_c3_hw`（C3 WiFi＝共有
`esp_shim.c`使用）とも0エラービルド．D-1（controller init/enable+VHCI）・
D-2a（host sync）のランタイムはble_host_smoke自体が両ブートで通過済み
（sync marker在）＝転移的に確認．`esp_shim.c`の追加はC3専用
（C5/C6は各自のesp_shim.cコピーを持つ＝grep確認）かつ`esp_shim_isr_storm_probe`
（既定0）ゲート内＝WiFiビルドに挙動変化なし．

**★判定＝D-2b保留解除・完了**．(1)〜(1)(n)で反証を尽くした9角度（clock/
reset/ANA/BB-mask/lpclk/RF-cal/ISR-EOI/優先度/coex）の外側に真因があった＝
**「深いRF/BB層」ではなくtarget層の割込みルーティング**．(1)(a)の最終解釈
（BBハードの連続assert）は撤回する．なお同ラウンド群の計装資産
（BB statusレジスタ特定・`phy_cal_trace.c`・RTC STORE生存性マップ等）は
本ラウンドの計装設計にそのまま使われており無駄ではなかった．

**「共通下層原因」説の再解釈**：`docs/s3-adv-storm-crosscheck-for-c3.md`時点の
「target層実装が別物の2チップで同種発火＝原因は共通の下層（blob/ROM）」という
推論は，**正しい観測から誤った方向を向いていた**——真の共通項はblobではなく，
(i) S3/C3が文字通り同一の`bt.c`（wrapper層）を共有すること，(ii) 両target層が
独立に「esp_intr_allocは1回しか呼ばれない」という同じ誤った前提で単発実装を
書いたこと，だった．「同一ソース・同一前提→同一バグ」であり「共通の下層
ハードウェア障害」ではない．

**申し送り（次段）**：
- **D-2c候補**：接続確立（`bluetoothctl connect`等でGAP CONNECTイベント
  確認．`g_gap_conn_count`/`g_gap_event_count`で観測可）→GATTサーバ
  （既知の別課題＝NULL関数ポインタ問題，本doc 1126-1133行相当）→scan役．
- ラウンド3（低優先）：`flags`引数（bt.cは`ESP_INTR_FLAG_LEVEL3`を要求）を
  無視している点の精査（現状INTMTX優先度1固定で動作＝品質向上項目）．
- S3側`.steering/20260709-ble-adv-storm-source/steering.md`のBT-4/BT-5知見
  （接続確立・MIC failure切り分け）がD-2c以降でそのまま参考になる．
- **board B最終状態**：本パッチ適用済み`ble_host_smoke`をflash済み・
  `ASP3-C3-BLE`としてadvertising動作中（終了処理でwatchdog-reset後に
  over-the-air再確認済み）．

**変更ファイル**：`asp3/target/esp32c3_espidf/bt/bt_shim.c`（修正+計装）・
`asp3/target/esp32c3_espidf/wifi/esp_shim.c`（線2カウントミラー，probe
ゲート内）・`apps/ble_host_smoke/ble_host_smoke.c`（RTC番地再割当て）．
submodule（`asp3/asp3_core`・`hal/`）変更なし・`.cfg`/CMake変更なし．

### 変更したファイル

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c` | （バグ修正・keeper）C6専用診断（ROM PHYFUNS表`0x4087f954`直読み）が無guardでC3ビルドにも混入していたのを`#if defined(TOPPERS_ESP32C6_WIFI)`でガードしC3から除外．C6の動作は不変．storm原因ではないが是正すべき潜在的wild-pointerバグ．D-2b(1)(n)coex調査で発見 |
| `asp3/target/esp32c3_espidf/bt/phy_cal_trace.c` | （新規・診断）RF-cal regi2c＋SAR測定値＋caller PCトレース．`--wrap phy_get_romfuncs`でg_phyFuns枠差替え．option `ESP32C3_BT_PHY_CAL_TRACE`(既定OFF)時のみ |
| `asp3/target/esp32c3_espidf/esp_bt.cmake` | option `ESP32C3_BT_PHY_CAL_TRACE`追加（ON時 phy_cal_trace.c＋`-Wl,--wrap=phy_get_romfuncs`） |
| `asp3/target/esp32c3_espidf/target.cmake` | `wifi/esp_shim.*`を`ESP32C3_WIFI OR ESP32C3_BT`の共有ブロックへ分離。`ESP32C3_BT`オプション追加（`ESP32C3_WIFI`との同時ON禁止） |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | `psa/crypto.h`依存を`TOPPERS_ESP32C3_WIFI`ガードで除外（BT単体ビルドではmbedtls非リンクのため） |
| `asp3/target/esp32c3_espidf/wifi/esp_shim_libc.c` | `heap_caps_calloc`追加（`esp_shim_calloc`へ委譲） |
| `asp3/target/esp32c3_espidf/hal_stub/include/platform/os.h` | `OS_TASK_PRIO_MAX`追加（`esp_task.h`のESP_TASK_BT_CONTROLLER_PRIO計算に必要） |

### 追加したファイル

- `asp3/target/esp32c3_espidf/esp_bt.cmake`（新規．Wi-Fiの`esp_wifi.cmake`と同型）
- `asp3/target/esp32c3_espidf/bt/`：`bt_cfg.h`／`bt.cfg`（タイマタスク静的定義）・`bt_shim.c`（freertos以外の周辺プリミティブ）・`stub/include/freertos/*.h`（bt.c用FreeRTOS互換ヘッダ）・`stub/include/esp_partition.h`（コンパイル用スタブ）
- `apps/bt_smoke/`：コントローラ起動＋VHCIループバックのスモークテスト

### 変更したファイル（2026-07-08 emi.c:164真因修正セッション）

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c3_espidf/bt/bt_shim.c` | `esp_shim_bt_clock_init()`追加＝BLEベースバンドの機能クロック（`SYSCON_WIFI_CLK_EN` bit6,11,12,16,17＝`0x00031840`）を有効化。**emi.c:164の真因修正**（Direct Bootが飛ばす`esp_perip_clk_init`のBBクロック分を補完）。 |
| `apps/bt_smoke/bt_smoke.c` | `esp_bt_controller_init()`直前で`esp_shim_bt_clock_init()`を呼ぶ。調査用プローブ`BT_PROBE_STOP_AFTER_INIT`（`#ifdef`ガード，通常ビルド無影響）を残置。 |
| `asp3/target/esp32c3_espidf/wifi/esp_coex_adapter.c` | `esp_shim_coex_adapter_register()`内のC6調査用一時診断（`read_fn(0x63)`）に**NULLガード**追加＝間欠クラッシュ（boot variance）の解消。`read_fn`が有効な場合の従来動作は不変。 |
| `asp3/target/esp32c3_espidf/target_kernel_impl.c` | `hardware_init_hook()`に`esp_rom_set_cpu_ticks_per_us(160)`追加＝ROMの`s_ticks_per_us`を実CPUクロックへ更新（`esp_rom_delay_us`が1/8時間しか待たない問題の修正、C6 実施48/49と同機構）。RF較正の遅延精度に必須（ただしRF較正リセットの解消は別要因で未達）。 |

### 変更したファイル（2026-07-08 クリティカルセクション・ネスト対応化＝Phase D-1完了）

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | **ネスト対応クリティカルセクション**`esp_shim_enter_critical()`/`esp_shim_exit_critical()`追加。大域ネストカウンタ＋退避で，最外(0→1)でMIE退避・最外(1→0)で復元＝同一muxの入れ子でもMIEを取りこぼさない。**「semphr_takeがE_CTX→タスクexit→SWリセット」の真因修正**。既存プリミティブ（`esp_shim_int_disable/restore`, `SHIM_LOCK`）は不変＝WiFi非回帰。 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.h` | 上記2関数のextern宣言追加。 |
| `asp3/target/esp32c3_espidf/bt/stub/include/freertos/FreeRTOS.h` | `portENTER_CRITICAL`/`portEXIT_CRITICAL`（`_ISR`/`_SAFE`版含む）を，退避値をmuxに格納する旧方式から`esp_shim_enter/exit_critical()`委譲へ変更。BT専用ヘッダのためWiFi不変。 |

### 変更したファイル（2026-07-13 D-2b保留解除＝source多重登録バグ修正セッション）

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c3_espidf/bt/bt_shim.c` | **D-2bストーム真因修正（keeper）**：`esp_intr_alloc`を単一static handle＋固定CPU線1から`bt_intr_slot[2]`配列＋呼出し順CPU線1/2分離へ（S3コミット`5e6d4b3`のC3移植）．`esp_intr_free/enable/disable`をper-handle化．診断計装＝呼出し回数・source時系列をRTC STORE1(0x60008054)へ記録（0xA1マーカ形式） |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | （診断）`shim_int_dispatch`に線2発火数のRTC STORE2(0x60008058)ミラー追加．`esp_shim_isr_storm_probe`（既定0）ゲート内＝WiFi非回帰．C5/C6は各自のesp_shim.cコピーのため影響なし |
| `apps/ble_host_smoke/ble_host_smoke.c` | RTC番地再割当て：advマーカ0x54→0x5c・connマーカのRTC書込み廃止・probe taskのRTC書込み廃止（0x54/58/5cを計装へ明け渡し．グローバル変数観測は維持） |

### Git情報

- ベースコミット：Phase C完了時点（`e6ae2e3`）
- ブランチ：`main`（Wi-Fi/lwIPと同じ．feature branchなし＝既存の
  Phase B/C各コミットと同じ運用）

## Phase D-2c：GATTディスカバリ開通（★達成＝2026-07-14，board B `...C2:60`）

接続はできる（GAP CONNECT status=0）が **ServicesResolved が解決せず
GATTディスカバリが完了しない**ブロッカーを実機で真因特定・修正し，
**over-the-air で `ServicesResolved: yes`＋GATT UUID 読出しを達成**した．

### 症状の再現（現ツリー 802d75b）
ホスト hci0（Intel AX210，`70:A8:D3:A1:22:45`）から
`bluetoothctl connect 60:55:F9:57:C2:60` → `Connected=true` だが
`ServicesResolved=false`・GATT子オブジェクト0．接続は数秒〜十数秒で
LL切断もしくは無応答のまま．コントローラ／ホストのクラッシュ・reset・
storm は無し．

### (a)/(b) 判別＝★(a)確定（host内 RX-data dispatch 不全）
hal 非編集の `--wrap` 計装（新option **`ESP32C3_BT_ACL_TRACE`**（既定OFF），
`bt/acl_trace.c`）で `ble_mqueue_put`／`ble_mqueue_get`／`ble_l2cap_rx`／
`ble_hs_conn_find` を捕捉し，カウンタを RTC STORE2（0x60008058．D-2cでは
storm probe OFF＝未使用）へパック．**単一 dump-mem** で採取：

| 局面 | put | get | l2cap_rx | 意味 |
|---|---|---|---|---|
| 修正前 | **1** | **0** | **0** | ATT要求は `ble_hs_rx_q` へ届く（put=1）が host が dispatch せず drain されない＝**(a)** |
| 修正後 | 13 | 13 | 13 | 全ATT PDU が dispatch→L2CAP到達＝(a)解消 |

＝**仮説(a)確定**（host task が `ble_hs_rx_q` の RX-data イベントを処理しない）．
`ble_hs_conn_find` NULL（(b)）は ACL 経路では不発＝**(b)は反証**．
（(b)候補の conn_find NULL は GAP/gattc 等の無関係経路で 1-2 件のみ．）

### 真因（確定）＝ブロッキングサービスコールの E_CTX 取りこぼし
- HCI event 配送（`ble_transport_to_hs_evt`→`ble_hs_hci_rx_evt`→`ble_hs_evq`）は
  **クリティカルセクション外**で `ble_npl_eventq_put`→`xQueueSendToBack`→
  `esp_shim_queue_send`→`tsnd_dtq` を発行し成功（＝GAP CONNECT は動く）．
- ACL 配送（`esp_nimble_hci.c` の `ble_hci_rx_acl`）は
  **`OS_ENTER_CRITICAL(sr) … ble_transport_to_hs_acl(m) … OS_EXIT_CRITICAL(sr)`**
  で囲む．`OS_ENTER_CRITICAL`＝bt-stub `portENTER_CRITICAL`＝
  `esp_shim_enter_critical`＝**生 `csrrci mstatus,8`（MIEクリア）**．
- 本RISC-Vポートの `sense_lock()`＝`(mstatus.MIE==0)`
  （`asp3_core/arch/riscv_gcc/common/core_kernel_impl.h:173`）．よって
  MIEクリア中はカーネルが**CPUロック状態**と判定し，
  **`tsnd_dtq`（待ちに入り得るサービスコール）は空きがあっても E_CTX を返す**．
- `esp_shim_queue_send`（旧実装）は E_CTX→スロット解放→**0（失敗）を返す**．
  NimBLE `npl_freertos_eventq_put` は先に `event->queued=true` を立てた後
  `xQueueSendToBack` 失敗を検出できず（`BLE_LL_ASSERT` は本ビルドで no-op），
  **mq_ev が二度と再postできず永久に配送されない**（put=1/get=0/クラッシュ無し
  と完全一致）．
- コントローラ（`bt.c`）は常に FromISR 系（`psnd_dtq`＝待たない送信）を
  クリティカルセクション内で使うため無傷＝ホストの task 系送信だけが露見した．
- 姉妹機 S3（`~/TOPPERS/esp32_s3`，同一 `esp_nimble_init` パターン）が
  BT-4調査で同一バグ（§8.5/§10/§13）を先行特定済み．本修正はその C3 移植．

### 修正（target/シム側．submodule 不可を遵守）
共有 `wifi/esp_shim.c` のキュー実装を S3 版へ移植（**CPUロック文脈でも
送信を取りこぼさない**）：
- **空きスロット数カウンティングセマフォ**（`SHIM_QSEM1..8`，`esp_shim.cfg`
  追加）を各キューに 1:1 付与＝`esp_shim_queue_send` のブロッキング契約
  （`portMAX_DELAY`＝満杯時は待つ）を `twai_sem` のタイムアウトで実現．
- **保留リング（pend_ring）＋sem_debt 会計**：E_CTX文脈（MIE==0）では
  `twai_sem`/`pol_sem`/`psnd_dtq` すら発行不可のため，カーネル呼出し無しで
  スロット確保＋item コピー＋スロット番号を平メモリのリングへ積んで**成功を
  返す**．MIE 復帰時（`esp_shim_exit_critical` 最外解除）に
  `esp_shim_queue_flush_pending()` で `psnd_dtq` へ流し込み，待機中の
  ホストタスクを起床させる．
- `queue_send`/`queue_send_from_isr`/`queue_recv`/`queue_reset`/
  `queue_msg_waiting` を保留リング対応化．`tsnd_dtq`/`trcv_dtq` が E_CTX の
  場合は `psnd_dtq`/`prcv_dtq`（待たない版）へフォールバック．
- WiFi 非回帰：ISR 経路（`queue_send_from_isr`）は従来通り `pol_sem`→
  `psnd_dtq` で即起床を維持し，`psnd_dtq` すら E_CTX の CPUロック文脈のみ
  保留リングへ退避（S3 の d94fec5 回帰＝WiFi AP接続不進を踏まえた設計）．

### 検証結果（over-the-air）
修正版を board B へ書込み，ホスト hci0 から接続：
- `Connected=true` かつ **`ServicesResolved=true`**（LL切断せず保持）．
- 計装版で **put=13/get=13/l2cap_rx=13**＝ATT MTU＋ディスカバリ全PDUが
  host内で正しく dispatch→L2CAP 到達（storm probe 無効・単一 dump-mem 採取）．
- **OTA で GATT サービス／特性 UUID を読出し**：
  - Service `0x1801`（Generic Attribute）
  - Char `0x2A05`（Service Changed）／`0x2B3A`（Server Supported Features）／
    `0x2B29`（Client Supported Features）
- **本番ビルド（計装 OFF）でも `ServicesResolved=true`＋同一 UUID を再現**＝
  修正は diag に依存しない．

### 非回帰
- `bt_smoke`（コントローラのみ）・`wifi_scan`（共有 `esp_shim.c` の WiFi 経路）
  とも 0 エラーでビルド（`esp_shim.c`/`esp_shim.cfg`/`esp_shim.h` を変更した
  ため全件再確認）．`ble_host_smoke` 本番ビルド RAM 約 93%．
- C5/C6 は各自の `esp_shim.c`/`.cfg` コピー＝本変更の影響を受けない．

### 計測・運用メモ（今回確立）
- **STORE2（0x60008058）は D-2c で自由**（storm probe OFF）＝ACL-TRACE に転用可．
  **STORE5（0x600080BC）は書込み不可**（常に ROM 残値 `0x13121312`）＝TX計装は
  読めなかった（ServicesResolved 成立で TX 応答は間接確認済み）．
- ライブ観測はホスト側 `bluetoothctl` のみ．オンターゲットは 1回の
  `esptool … --before default_reset --after no-reset dump-mem 0x60008050 0x80`．
  ただし `--after no-reset` はチップを**ブートローダに残す**ため，続けて実機
  観測するには `--before usb-reset --after watchdog-reset flash-id` で app 再起動．
- `bluetoothctl remove` 後は再 scan しても D-Bus device オブジェクトが即座に
  再登録されず connect が滑る＝**remove せず scan→connect** が確実（鮮度は
  on-target の put=13 が live OTA を直接証明するため cache 疑いは不要）．

### 変更したファイル（2026-07-14 D-2c＝RX-data dispatch 修正セッション）

| ファイル | 内容 |
|---|---|
| `asp3/target/esp32c3_espidf/wifi/esp_shim.c` | **真因修正（keeper）**：キュー実装を「空きスロットセマフォ＋保留リング＋flush」へ（S3 BT-4の C3 移植）．`esp_shim_exit_critical` 最外解除で `esp_shim_queue_flush_pending()` を呼ぶ．E_CTX時 `tsnd_dtq→psnd_dtq`／`trcv_dtq→prcv_dtq` フォールバック |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.cfg` | 各キューの空きスロット数セマフォ `SHIM_QSEM1..4`（＋NIMBLE時 `..8`）追加 |
| `asp3/target/esp32c3_espidf/wifi/esp_shim.h` | `esp_shim_queue_flush_pending`／`esp_shim_queue_reset` の宣言追加 |
| `asp3/target/esp32c3_espidf/bt/acl_trace.c` | （新規・診断）RX/TX ACL 経路の `--wrap` 計装．option `ESP32C3_BT_ACL_TRACE`（既定OFF）時のみ |
| `asp3/target/esp32c3_espidf/esp_bt.cmake` | option `ESP32C3_BT_ACL_TRACE` 追加（ON時 acl_trace.c＋6関数 `--wrap`） |
| `apps/ble_host_smoke/ble_host_smoke.c` | `TOPPERS_ESP32C3_BT_ACL_TRACE` ガードで起動時 `esp_acl_trace_reset()` 呼出し（計装 OFF ビルドは無影響） |

### 既知の周辺事項（本フェーズ範囲外）
- 現ツリーは gcc-15.2 の implicit-function-declaration（`esp_timer_is_active`／
  `heap_caps_malloc` 等，hal 側）が既定エラーで **fresh ビルドが失敗**する
  （本 D-2c 修正とは無関係の既存事象）．ビルドは
  `-DCMAKE_C_FLAGS=-Wno-error=implicit-function-declaration` を付与（32bit で
  int==pointer のため機能的に無害．前セッションの成功オブジェクトも同等）．
  恒久対策（target 層でプロトタイプ補完）は別課題．
- GAP service（`0x1800`）は BlueZ が内部処理で隠すため一覧に出ないことがある
  （`0x1801` と特性は OTA 取得＝ディスカバリ完了の物証）．

## Phase D-2d：自前GATTサービス＋notify＋(SM) 実機（★read/write/notify達成，2026-07-14, board B `...C2:60`）

D-2c（GATTディスカバリ開通）の上に，**自前GATTサービス（read/write/notify
特性）＋周期notify**を実装し，BlueZ hci0（Intel AX210 `70:A8:D3:A1:22:45`）
から over-the-air で read/write/notify を実証．さらに **SMP（ペアリング/暗号）**
を S3 BT-5（`~/TOPPERS/esp32_s3/.steering/20260710-ble-bt5-security-notify/`）を
«動作済みの正» として逐語移植して有効化した．

### 実装（S3 BT-5 の C3 逐語移植＋write特性追加）
自前サービス **0xABF0**（PRIMARY）に3特性（S3と同一 16bit UUID＋C3固有write）：

| UUID | 種別 | 動作 |
|---|---|---|
| `0xABF1` | READ | 固定値 `"BT4-OK"`（`42 54 34 2d 4f 4b`．S3と同一） |
| `0xABF2` | READ \| NOTIFY | 32bit LE カウンタ．subscribe(CCCD)中のみ 1秒周期で送出（`ble_gatts_notify_custom`） |
| `0xABF3` | WRITE | 受信値をログ＋RTCマーカ（STORE2 0x58＝`0x7717<count><先頭byte>`）＋putカウンタ加算 |

- `ble_gatts_count_cfg`/`ble_gatts_add_svcs` で登録（`ble_svc_gap/gatt_init` の後・
  `nimble_port_freertos_init` の前．標準 `0x1800`/`0x1801` は維持）．
- subscribe追跡＝`BLE_GAP_EVENT_SUBSCRIBE`（`attr_handle==val_handle` で
  `g_notify_enabled=cur_notify`）．接続で `g_conn_handle` 記録，切断でクリア．
- notify送出は **main_task の定常ループ（1秒周期・アプリタスク文脈）**．D-2cで
  移植した保留リング（`esp_shim.c`）がクリティカルセクション内送出を救済するため
  main_task 文脈からの `ble_gatts_notify_custom` 直接呼出しで安全（S3 §2.1と同型）．
  従来 main_task は sync待ち後に return していたが，adv/接続/notify を無期限に
  続けられるよう定常ループへ移行（60秒毎ハートビートのみログ）．

### ビルド（`esp_bt.cmake`，可逆オプション）
- **`option(ESP32C3_BT_SM ... ON)`** を新設．ON時：`MYNEWT_VAL_BLE_SM_LEGACY/SC=0`
  の «蓋» を外し（`NIMBLE_BLE_SM=1`），SC=ECDH P-256 の crypto を vendored
  **tinycrypt 必要5ソース**（`aes_encrypt.c cmac_mode.c ecc.c ecc_dh.c utils.c`）で
  供給（mbedTLSは `CONFIG_ESPRESSIF_BLE` 非定義で不選択＝WiFi系TLSと分離維持）．
  bond store は `ble_store_ram.c`（IDF文脈=`BLE_USED_IN_IDF=1` で空）ではなく
  **`ble_store_config.c`（`BLE_STORE_CONFIG_PERSIST=0`＝RAM保持，NVS不使用）**へ
  差替え（S3 §5.2 の真因対策）．app 側は `TOPPERS_ESP32C3_BT_SM` で SM 設定
  （`ble_store_config_init()`＋`sm_io_cap=NO_IO`/`sm_bonding=1`/`sm_mitm=0`/
  `sm_sc=1`/`sm_our_key_dist=sm_their_key_dist=ENC|ID`）を有効化．OFF に戻せば
  D-2c 構成へ完全復帰（可逆）．
- ビルド：SM入り `ble_host_smoke` = **RAM 93.48%**（320KB に収まる）・
  IROM 5.85%/DROM 6.50%，リンク0エラー．SM関連シンボル（`ble_store_config_init`・
  `uECC_make_key`・`ble_sm_alg_ecc_init`・`tc_aes_encrypt`・`ble_gatts_notify_custom`）
  リンク確認．非回帰：`bt_smoke`（BT=ON/NIMBLE=OFF＝SMブロック不関与）RAM 84.95% 0エラー．

### 検証結果（over-the-air，BlueZ hci0×board B `60:55:F9:57:C2:60`）
DUTノードは再列挙で流動（本セッション ttyACM5→ttyACM8．操作直前に毎回MAC照合）．
ライブ観測はホスト `bluetoothctl` のみ，オンターゲットは単発 `esptool dump-mem`．

| 項目 | 結果 |
|---|---|
| GATTディスカバリ | 自前 `0xABF0`＋`0xABF1`/`0xABF2`(CCCD付)/`0xABF3`＋標準`0x1801` を OTA で列挙（`gatt.list-attributes`） |
| **READ 0xABF1** | `42 54 34 2d 4f 4b` = **"BT4-OK"** ✓ |
| **WRITE 0xABF3** | `write "0xa5 0x5a 0x03"` 成功 → DUT側 RTC STORE2(0x58)=**`0x771701a5`**（tag 0x7717・count=1・先頭byte=0xa5）＝受信を DUT で確認 ✓ |
| **NOTIFY 0xABF2** | `notify on` で **カウンタ `01..09` 単調増加・1/秒・欠番ゼロ**，`notify off` で停止 ✓ |
| 安定性 | 全 connect/pair サイクルを通じ DUT は無crash・無reset（STORE0 は sync値 `0x5ade51c0` を維持，`0x5E` reset マーカ非出現）．切断はすべて clean（disc reason `0x13`＝central起点） |

### SM（ペアリング/暗号）＝★DUT側は暗号確立成功，BlueZ の Paired/Bonded 未成立
- **DUT側 SM は暗号を確立する**：SMP診断RTCマーカ（`BLE_GAP_EVENT_ENC_CHANGE`→
  STORE2 0x58 に `0x5DE0<status>`）が **`0x5DE00000`＝ENC_CHANGE status=0（暗号成立）**
  を記録．＝peripheral の SMP/tinycrypt 経路（SC/ECDH/AES-CMAC）は機能している．
- **ただし条件依存**：ENC_CHANGE が発火したのは **agent を単一 bluetoothctl
  セッション内で保持（`agent NoInputNoOutput`＋`default-agent`＋`pair` を同一
  セッションで連続）**した試行のみ．connect と pair を別セッションに分けると
  agent が落ち（`Failed to register agent object`），ENC_CHANGE 非発火（0x58 に
  0x5DE0 が書かれず旧 write マーカのまま）＝S3 §5.2 issue#3（central側 agent 状態）と
  同型の «テスト環境» 依存を C3 でも観測．
- **未達＝BlueZ の `Paired: yes`/`Bonded: yes` が立たない**：DUT が暗号成立
  （ENC_CHANGE=0）しても bluetoothctl は `Paired: no` のまま，`pair` は
  成功/失敗いずれの結果行も返さず（Pairing Request 送出後に完了イベント無し），
  最終的に central 起点 clean 切断（DUT視点 reason `0x13`）．＝**鍵配布/bond
  登録の最終段（BlueZ側 DBus 完了 or key distribution）が完了しない**．
- 判定できたこと／できていないこと：peripheral SM の暗号確立は物証（ENC_CHANGE=0）
  で確定．「BlueZ が bond を確定しない」機序は host/BlueZ側の未確定（root権限が
  無く `btmon`/`journalctl -u bluetooth` は本PCで不可＝HCIレベルの直接観測不可）．
  DUT側の PAIRING_COMPLETE status は ENC_CHANGE が同 reg を上書きするため本
  マーカ設計では未分離＝別reg分離が次段の計測改善．

### 計測・運用メモ
- SMP診断マーカは `TOPPERS_ESP32C3_BT_SM` 下で STORE2(0x60008058) を共用：
  write特性=`0x7717xxxx`／PAIRING_COMPLETE=`0x5DC0<status>`／ENC_CHANGE=`0x5DE0<status>`．
  RTC domain は reflash（RTS/DTRチップreset）を跨いで残る＝旧値混同に注意
  （本番 boot で 0x58 は初期化しないため，pair非実施なら旧 write マーカが残る）．
- **serial開放＝DUTリセット**（[[c3-usbjtag-serial-open-resets-dut]]）のためライブは
  ホスト `bluetoothctl` のみ．`bluetoothctl` は本PCで散発的に segfault/SIGTERM する
  が，デバイスキャッシュ（`bluetoothctl devices`）と単発 `connect` は機能する．
  `dump-mem` は `--after no-reset`（ブートローダ滞留）→ `--before usb-reset
  --after watchdog-reset flash-id` で app 再起動，の D-2c 手順を踏襲．

### 変更したファイル（D-2d）
| ファイル | 内容 |
|---|---|
| `apps/ble_host_smoke/ble_host_smoke.c` | 自前サービス0xABF0（read/notify/write access_cb＋svc_def）／SUBSCRIBE追跡／`notify_tick`＋定常ループ／SM設定・store_init・ENC_CHANGE/PAIRING_COMPLETE/REPEAT_PAIRING（`TOPPERS_ESP32C3_BT_SM`ガード）／write・SMP RTCマーカ |
| `asp3/target/esp32c3_espidf/esp_bt.cmake` | `option(ESP32C3_BT_SM ON)`：SM有効時に SM=0 蓋を外し・`TOPPERS_ESP32C3_BT_SM`定義・tinycrypt include＋5ソース・`ble_store_ram.c`→`ble_store_config.c` 差替（可逆） |

### 実機状態（クローズ）
board B（`60:55:F9:57:C2:60`）に **SM入り本番ビルドを書込み済み・advertising継続**
（`ASP3-C3-BLE`）．暗号を要求する特性は無い（read/write/notifyは平文で動作）ため
スマホ central は pairing 無しで GATT read/write/notify を追試できる．

### 次の一手
1. **BlueZ `Paired:yes` 未成立の切り分け**：PAIRING_COMPLETE と ENC_CHANGE を
   別RTC regに分離（現状 0x58 共用で PAIRING_COMPLETE が見えない）＋鍵配布
   （our/their key dist の実授受）を DUT側マーカで観測．root可PCなら `btmon`/
   `journalctl -u bluetooth` で HCI/SMP を host側直接観測（本PCは sudo不可）．
2. スマホ（Android/iOS）での pair/notify 追試（S3はAndroid/BlueZ完動・iOS再接続
   MIC failure の既知制限＝steering §8-9）．
3. 暗号必須特性（`BLE_GATT_CHR_F_READ_ENC` 等）で pairing を強制トリガする構成の検証．

### D-2d bond診断（2026-07-14 追試）：★実スマホ SMP で «暗号有効化がタイムアウト»（ENC_CHANGE status=13=BLE_HS_ETIMEOUT）＝bond失敗の真因を LL/コントローラ暗号有効化層に局在化

前回（次の一手#1）で計装を改善して実スマホでクリーンにペアリングを走らせ，
DUT側の SM 進行を RTC マーカで確定した。

#### 計装改善（実施済み）
- **PAIRING_COMPLETE を ENC_CHANGE(0x58) と別レジスタ 0x54 へ分離**（0x58 は
  ENC_CHANGE が後勝ちで上書きし PAIRING_COMPLETE を隠していた）。8個の STORE 中
  «接続→pair→切断→再adv» で書かれないのは 0x54（bt_shim の esp_intr_alloc
  トレース＝init時のみ）だけ。0xC4 は adv-rc(0xAD00xx)が start_advertising 毎に
  上書き＝不可。発火/未発火は 0x5DC0 タグの有無で判別。
- 値に DUT側 bond件数(our_sec/peer_sec)をパック。
- 暗号必須 READ 特性 **0xABF4(READ_ENC)** を追加＝未ペア central の READ で
  pairing を決定論的にトリガ。
- 実機 build/flash/boot/marker 読取を `tmp/c3ble.sh` に一括化（**実機は
  `-DESP32C3_QEMU=OFF` 必須**＝既定ONだと `csrw mie`→実機 Illegal instruction
  でクラッシュする罠を埋込＋elf自己検査）。commit 830a194。

#### 手順（実スマホ nRF Connect）
接続だけでは SMP は始まらない（GATT接続のみ＝CONN=1・ENC=0・PAIRING未発火が
正常）。**サービス0xABF0の 0xABF4(暗号必須)を READ** すると DUT が
insufficient-encryption を返し，nRF がペアリング要求→ユーザ許可→**BONDING**
表示→最終 **NOT BONDED**（bonding 失敗）。stale bond は事前に «デバイスを忘れる»
で排除（DUT bond store は RAM＝リブートで消えるので，リセットの度に phone 側
古い bond と非同期になり «connectできない» を誘発する。マーカ読取＝リセットは
必ず pairing 試行が «終わった後» に1回だけ）。

#### DUT側 RTC マーカ（`tmp/c3ble.sh mark`，board B `60:55:F9:57:C2:60`）
| reg | 値 | 意味 |
|---|---|---|
| 0xC0 CONN | `0x604e0002` | 接続成立（status0・2回目） |
| 0x58 ENC | **`0x5de0000d`** | **ENC_CHANGE status=`0x0d`=13=`BLE_HS_ETIMEOUT`** |
| 0x54 PAIRING | `0xa1020805`(alloc trace) | **PAIRING_COMPLETE 未発火**（0x5DC0タグ無し） |
| 0xB8 DISC | `0xd15c1302` | 切断 reason=`0x13`（HCI «Remote User Terminated»＝phone側終了） |

（`BLE_HS_ETIMEOUT=13` は `host/ble_hs.h:108` で確認。reason 0x13 は HCI 標準
エラーコード «Remote User Terminated Connection»。）

#### 切り分け（確定）
1. Pairing要求→応答→公開鍵/DHKey交換までは進む＝**crypto(tinycrypt SC/ECDH)は無罪**。
2. **暗号有効化（LL_ENC_REQ/RSP→START_ENC→Encryption Change）が確定せず SM が
   タイムアウト**（ENC_CHANGE=ETIMEOUT）。NimBLE の SM が «暗号開始» 段に達した後，
   Encryption Change の確定待ちで時間切れ（PAIRING_COMPLETE ではなく ENC_CHANGE で
   失敗＝LTK/暗号開始段まで到達の証左）。
3. PAIRING_COMPLETE 未到達→phone が reason 0x13 で切断→**NOT BONDED**。
   ∴ bond失敗の真因は **LL/コントローラの暗号有効化層**（暗号計算・host SMPロジック
   ではない）。前回の «ENC_CHANGE=status0» とは食い違う＝本追試がクリーンな実スマホ
   SMP の初物証。

#### 次の一手（本追試で更新）
1. **暗号有効化タイムアウトの機序**：NimBLE host が出す HCI LE Start/Enable
   Encryption と，コントローラからの **Encryption Change/Enc Key Refresh HCI
   イベントの授受**（esp_shim の HCI event 経路）を追う。D-2c で ACL RX-data
   dispatch の E_CTX 取りこぼしを直したのと同型で，«暗号関連 HCI イベント» が
   host へ届いていない可能性を第一に疑う（反証：BlueZ 単一セッションでは
   status0 が出た＝経路自体は通り得る→タイミング/イベント種別依存の切り分けが要る）。
2. host BlueZ からの enc必須 READ で ENC_CHANGE が 0 か 13 かを対照
   （phone固有 vs 普遍の判別。bluetoothctl の gatt.read で 0xABF4 を叩く）。
3. bond store は RAM(PERSIST=0)＝bond成立してもリブートで消える。恒久bondは
   別途 NVS 永続化が要るが，まず暗号有効化の完遂が先。

#### 追加の切り分け実験（同日）：S3方式の «接続時 Security Request» に揃えても同一タイムアウト＝トリガ経路は真因でないと確定
S3 の `ble_hs_smoke.c:532` は接続5秒後に未暗号なら `ble_gap_security_initiate`
（slave Security Request）を1回送る＝connect で «ペアリング要求が出る»。C3 の
D-2d 逐語移植ではこの接続時セキュリティ開始が**欠落**しており（0xABF4 の READ で
しかトリガできなかった），S3 と挙動が違っていた。この `bt5_security_tick` を C3 の
`ble_host_smoke.c` へ逐語移植（`TOPPERS_ESP32C3_BT_SM` ガード・接続/切断で計時
リセット・1秒ループから呼ぶ）。

結果：**実機で S3 同様「接続5秒後にペアリング要求が出る」ようになった**（移植は機能）
が，**bond は依然失敗**（スマホ Bluetooth 設定に記録されず＝NOT BONDED）。DUT側
マーカは前と**完全に同一**：CONN=`0x604e0001`／ENC=**`0x5de0000d`（ENC_CHANGE
status=13=BLE_HS_ETIMEOUT）**／PAIRING(0x54)未発火／DISC reason=`0x13`。

∴ **トリガ経路（0xABF4-read か slave SecReq か）は bond 失敗の真因ではない**と確定。
どちらのトリガでも暗号有効化（LL_ENC→START_ENC→Encryption Change）で同じく
タイムアウトする＝真因は **shim/コントローラの暗号有効化層**（次の一手#1＝S3 の
動く esp_shim との HCI イベント経路比較へ集約）。`security_initiate` 移植は S3 との
挙動差を埋める正当な修正として保持（接続で自動ペア要求＝以後の bond 試験も容易）。

#### ★決定的実験の結果（HCI EVT --wrap 計装）：診断が反転＝LL/コントローラは «暗号成功»、真因は host(NimBLE)層のタイムアウト
`ble_hs_hci_evt_process`（host が処理する全 HCI EVT を通る．ble_hs.c:655 から
クロスTU 呼出し＝--wrap 確実）を横取りし，controller→host の «LE LTK Request»
(0x3E/0x05) と «Encryption Change»(0x08) の到着数・status を RTC STORE0(0x50)へ
記録（`ESP32C3_BT_EVT_TRACE=ON`，`bt/evt_trace.c`）。--wrap 成立を elf 逆アセンブルで
確認（ble_hs.c 呼出し→__wrap→__real）。

実スマホでペアリング（NOT BONDED）後の物証：
| reg | 値 | デコード |
|---|---|---|
| 0x50 EVT | `0x23000101` | total_host_EVT=**35**／**LTK_Request=1**／**EncChange=1 status=`0x00`=成功** |
| 0x58 host ENC | `0x5de0000d` | app が見た ENC_CHANGE status=**13=ETIMEOUT** |
| 0x54 PAIRING | alloc trace | PAIRING_COMPLETE 未発火 |

**★診断反転**：controller は **Encryption Change status=0（暗号有効化成功）** を
host へ返している＝**LL/コントローラ層は無罪**（「LL層が真因」という前実験の最有力
仮説を反証）。にもかかわらず NimBLE は app に **ETIMEOUT** を報告＝**真因は
host(NimBLE)層**。機序の最有力＝**NimBLE の SM 手続きがタイムアウト（〜30s）した
«後» に，成功の Encryption Change が host 処理された**（SM proc は既に消えており
app には SM タイマ由来の ETIMEOUT が残る。0x58 は app が見た最後の enc_change＝
13）。host EVT 総数が 35/接続 と «少ない» のも host タスク処理の «遅さ» を示唆
（エージェント候補2＝LTK Reply / host タスクのスケジューリング遅延が昇格）。

**次の一手（更新）**：
1. 遅延の実測＝LTK Request 到着〜Encryption Change 到着〜SM タイムアウトの時間を
   HRT で記録し，«遅い handshake»（30s 経過）か «SM proc/状態のバグ»（速いのに
   timeout）かを分離。evt_trace.c に HRT タイムスタンプを追加するのが最小。
2. host タスク/BT ISR のスケジューリング改善＝C3 の BT ISR は INTMTX 優先度1
   （最低），S3 は2（bt_shim.c）。優先度を上げて Encryption Change が SM タイム
   アウト前に処理されるか（安直だが直れば timing 確定）。
3. NimBLE SM タイムアウト値と，ble_sm_enc_change_rx が Encryption Change を
   proc に紐付けられているか（conn handle 一致／proc 生存）を確認。

#### 追加の決定的実験（同日）：enc→ETIMEOUT 実測=30秒＋周期flush修正が «無効» ＝真因を controller/LL の «暗号化ACL鍵配布 未配送» へ確定
反証実験として (1)Encryption Change→app ETIMEOUT の実秒差を計装（`ble_host_smoke.c` の
ENC_CHANGE ハンドラで `fch_hrt() - g_evt_enc_hrt` を 0x58 byte1 へ）、(2)pend_ring の
周期flush修正（main_task の 100ms ループから `esp_shim_queue_flush_pending()`）を投入。

実機（iPhone/Android 両方、EVT_TRACE版）：
| marker | 値 | 意味 |
|---|---|---|
| 0x50 EVT | `0x00000101` | LTK_Req=1／EncChange=1／status=**0x00（暗号成立）** |
| 0x58 ENC | `0x5de01e0d` | app ENC_CHANGE status=**13(ETIMEOUT)**、Enc→通知=**30秒** |
| 0x54 PAIRING | alloc trace | **PAIRING_COMPLETE 未発火** |

**結論2つ：**
1. **Enc→ETIMEOUT=30秒（実測）** → NimBLE の SM タイマは «正確に30秒» 発火＝**Codex の «NPL 早発火» 説は反証**。proc は KEY_EXCH で正当に30秒待って timeout（＝対向 Identity PDU を待っている）。
2. **100ms 周期 flush でも bond は成立せず**（iPhone/Android とも同じ30秒 ETIMEOUT・PAIRING_COMPLETE 不発）→ **pend_ring 滞留説は反証**（滞留していれば ≤100ms flush で必ず host へ届き PAIRING_COMPLETE が発火するはず）。

**実運用テスト（ユーザー実施）**：iPhone を nRF で接続中は iOS 設定に出るが、**切断すると設定から消える＝永続 bond 不成立**。DUT の PAIRING_COMPLETE 未発火と完全一致。iPhone/Android とも永続 bond は成立しない（central 非依存）。

∴ **真因を確定的に絞り込み：central の «暗号化された Identity PDU（鍵配布 ACL データ）» が pend_ring «より手前» で失われている**＝**controller/LL が暗号リンク上の鍵配布 ACL を host の SMP 層へ配送していない**（暗号は «開始» できる=Encryption Change status0 だが、以後の «暗号化 ACL RX» が上がってこない。メモリの «iOS 再接続 MIC failure» と同族の controller LL 暗号/MIC 問題の疑い）。shim（pend_ring/flush）は無罪。

**次の一手**：ACL RX 経路の局在化＝`ESP32C3_BT_ACL_TRACE`（`ble_mqueue_put/get`・
`ble_l2cap_rx` を --wrap）で、暗号確立«後»に ACL RX が host キューに載るか観測。
put が増えない → controller/LL 未配送で確定（blob層）。put○get✗/get○l2cap✗ なら
host/L2CAP 側の別問題。計装の 100ms flush は «防御的安全網» として残置（bond は
直さないが pend_ring 滞留一般への保険。pend残0で即return＝非回帰）。

#### ★真因を controller/LL 層に確定：暗号確立後「最初の1個の暗号化ACLは通るが2個目が通らない」（RX/TX パイプライン+key_dist 実験）
反証を積み上げて真因を局在化した最終ラウンド。

**RXパイプライン計装**（evt_trace.c に `ble_mqueue_put`/`ble_mqueue_get`/`ble_l2cap_rx`
を --wrap、暗号確立時点でスナップショット→0x50 に put/get/l2cap の «暗号後» 増分を格納）。
実機（their=ID/our=ID 原本）：**暗号後 ACL = put=1, get=1, l2cap=1**（=届いた1個は
host rx_q→drain→L2CAP まで完全処理＝**RX経路は正常**）。しかし LE SC 鍵配布は central が
Identity Information + Identity Address の **2 PDU** を送る＝**2個目が来ない**（put が 1 で
止まる）。∴ 詰まりは shim/host の RX 経路ではない。

**key_dist 切替実験**（真因の裏取り）：
| 設定 | DUT 結果 |
|---|---|
| their=ID / our=ID（原本） | 暗号後 ACL 1個→**30秒 ETIMEOUT**（central 2個目 Identity PDU 未達） |
| their=**no-ID** / our=ID | **PAIRING_COMPLETE status=0（SMP完了！）** だが phone 未 bond・bond件数0 |
| their=no-ID / our=**no-ID** | 暗号手前で拒否（peripheral 鍵ゼロを central が拒否） |

their から ID を外すと DUT は central の鍵を待たず **PAIRING_COMPLETE status=0 で完了**した
（＝«待ち» が原因だった裏取り）。だが phone は依然 bond せず＝今度は **DUT 自身の IRK
（our=ID の 2 PDU）TX の2個目が central に届かない**（central 不満足で切断）。

**∴ 真因確定：C3 コントローラ(blob)の LL 暗号層で、暗号確立«後»の «2個目以降の暗号化
ACL パケット» が復号/配送されない**（RX=central の2個目 Identity PDU が put まで来ない／
TX=DUT の2個目 IRK が central に届かない）。最初の1個は RX/TX とも成功する。これは
memory の «iOS 再接続 MIC failure»（暗号リンク上の MIC/復号失敗）と同族＝**host/shim/
アプリでは回避できない controller/blob 層の問題**。shim(pend_ring/flush)・host(NimBLE)・
key_dist いずれも真因ではない（全て反証済み）。

**到達点と残**：C3 BLE は「暗号確立（Encryption Change status0）」「暗号後 最初の1パケット
の RX/TX」「their=no-ID で SMP PAIRING_COMPLETE status0」までは到達。**永続 bond は
コントローラの «複数暗号化パケット» 不成立により未達**。次は controller/blob 層＝(a)別
blob版/IDF stock コントローラでの2個目暗号化ACL挙動の対照、(b)LL 暗号(CCM/nonce/pktcnt)
関連の controller 設定・ROM 依存の確認、(c)暗号を要求しない運用（平文 GATT）での確定。
key_dist は spec 準拠（ENC|ID）へ復帰。診断計装 evt_trace.c(ESP32C3_BT_EVT_TRACE)・
100ms flush は残置（非回帰）。

#### ★★★決定的逆転：stock ESP-IDF bleprph は «同じC3ボード・同じスマホ» で BOND 成立＝真因は silicon/controller «能力» でなく我々の統合 or blob版差
真因を controller/silicon 層と結論しかけたが，反証のため **stock ESP-IDF v6.1 の
`examples/bluetooth/nimble/bleprph`（CONFIG_EXAMPLE_BONDING=y・SM_SC=y＝我々と同じ
sm_bonding/sm_sc/key_dist ENC|ID）を esp32c3 でビルドし，board B（60:55:F9:57:C2:60，
BLE=`nimble-bleprph` 60:55:F9:57:C2:62）へ焼いて実機テスト**。

**結果：ユーザーが nRF から Bond → «bonded» 成立**。＝**C3 コントローラは «複数暗号化
パケット／鍵配布／永続 bond» を完遂できる**。∴ これまでの「controller/blob が2個目の
暗号化 ACL を配送しない＝silicon限界」は **誤り**。真因は次のどちらか：
- (A) controller blob «バージョン差»：我々=esp-hal-3rdparty 版
  (`hal/components/bt/controller/lib_esp32c3_family/esp32c3/libbtdm_app.a` md5
  `dfdadb9ddc12eeeab85edfb5d26eb4bf`)／stock IDF 版(同名 md5 `d9753a31a8eeac9…`)＝**別物**。
- (B) 我々の統合（esp_shim os_adapter／手書き esp_bt_controller_config_t／init 手順）．

我々のこれまでの反証（shim pend_ring/flush・host NimBLE・key_dist・NPLタイマ・
トリガ経路 すべて無罪）は「NimBLE host 側は正しい」ことを示しており，残る差は
**controller blob版 or controller直下の統合(config/HCI/os_adapter)** に絞られる。

**次の決定的実験**：我々の ASP3 ビルドの controller blob を «IDF版» へ差し替えて
（esp_bt.cmake の `-L${ESP_HAL_DIR}/.../lib_esp32c3_family` を IDF パスへ）bond テスト。
成立→(A) blob版差が真因＝IDF blobへ差替が fix／不成立→(B) 我々の config/shim を
stock IDF と突き合わせ（特に手書き controller config vs BT_CONTROLLER_INIT_CONFIG_DEFAULT，
HCI host flow control）。実機ヘルパ tmp/c3ble.sh・診断計装 evt_trace.c は再利用可。

#### エージェント «セマフォ E_CTX give 消失» 説は反証＋SVC_PERROR 診断計装（真因は «our shim» でなく blob or controller直下の統合に絞られる）
比較エージェントは「D-2c でキューの E_CTX 救済(pend_ring)を入れたが，セマフォ
(esp_shim_sem_give)には入れ忘れ＝controller が MIE==0 から出す give が消える」を
最有力（確信度中）とした。これに沿い esp_shim_sem_give に «E_CTX 保留give»
(shim_sem_pend／exit_critical・100msループで精算)を実装し，E_CTX give 累計
(shim_sem_ectx_total)を RTC 0x50 byte3 へ計装。

**実機：`shim_sem_ectx_total=0`（接続を含む全区間で E_CTX give ゼロ）→ 反証。**
機序＝**ASP3 は `#define isig_sem sig_sem`＝sig_sem は ISR からも E_CTX 無しで呼べる**
（ユーザー指摘・kernel.h:350）。controller の `_semphr_give_from_isr` は «実 ISR»
（非タスク文脈）から呼ばれ sig_sem が成功する＝give は失われない。エージェントの
«give が E_CTX で消える» 前提は本ポートでは成立しない（キューの E_CTX は tsnd_dtq が
«ブロッキング» で ISR/CPUロック不可＝別物。sig_sem は非ブロッキングで ISR 可）。
セマフォ修正は発火せず＝bond 不成立のまま。

**SVC_PERROR 診断**（ユーザー提案，sample1 相当）：esp_shim のサービスコールで
«非E_OK かつ 非E_CTX/E_TMOUT/E_QOVR» を file:line 付きログ（`ESP32C3_BT_APIERR_TRACE`，
svc_perror は ercd を返す＝挙動不変・ログ追加のみ）。ただし **USB-JTAG コンソール取得が
本環境で不安定（開くと download-latch＝0バイト）＋ペアリングが段階でばらつく flaky
（マーカ読取=リセット毎に DUT RAM bond が消えスマホと非同期）で，鍵配布まで到達した
クリーンな失敗を安定に採れず未収穫**。

**現状の確定**：真因は «our esp_shim の sem/queue E_CTX» ではない（全て反証）。残るは
(A)controller blob(dfdadb9d，NuttXと同一)自体，(B)controller 直下の統合（init順序・
os_adapter の意味論・HCI transport）。**次の最も決定的な切り分け＝«NuttX が我々と同一
blob(dfdadb9d)で BLE bond するか»**（する→blob無罪＝統合バグ／しない→blob）．or blob-swap。
テストループ安定化（bond の NVS 永続化 or 実機コンソールの安定取得）も要検討。

### ★D-2d bond 調査 «別PC引き継ぎ» 要点（2026-07-14 一区切り）
（メモリはローカル固有で転送されないため，再開に必要な事実を本節に集約）

**確定した現状**：C3 BLE の «暗号確立» までは成功するが «永続 bond» が未達。暗号確立
（Encryption Change status=0）後の LE SC 鍵配布で，**最初の1個の暗号化 ACL は完全処理
（put=get=l2cap=1）だが2個目が host に来ない**→SM proc が30秒(実測)で BLE_HS_ETIMEOUT。
iPhone/Android 非依存。key_dist から ID を外すと SMP は PAIRING_COMPLETE status0 で完了
するが，DUT 自身の IRK(2 PDU)TX の2個目も届かず bond 不成立。

**反証済み（無罪確定）**：shim キュー E_CTX(D-2c で pend_ring 修正済)／セマフォ E_CTX
give 消失(sem_ectx=0＝ASP3 は sig_sem を ISR から呼べる)／NimBLE host・NPL タイマ(enc→
timeout=実測30秒)／トリガ経路(0xABF4/SecReq)／key_dist・controller config(手書き=DEFAULT
一致)・osi_funcs(全実装)。

**★決定的事実**：stock ESP-IDF v6.1 bleprph(bonding+SC)は «同じ C3 ボード・同じスマホ» で
**bond 成立**＝コントローラ silicon は能力あり。∴ 真因は (A)controller blob 版差 or
(B)我々の controller 直下統合。blob md5：我々=esp-hal-3rdparty `b90b1837`＝
`hal/.../lib_esp32c3_family/esp32c3/libbtdm_app.a` md5 **dfdadb9d**（NuttX apache/master が
`arch/risc-v/src/common/espressif/Make.defs` で同一 `b90b1837` をピン＝**NuttX と同一 blob**）／
stock IDF v5.5.4・v6.1 とも md5 **d9753a31**（OSI 0x0B，我々は 0x0A）。

**次の決定打（優先順）**：
1. **NuttX が «同一 blob dfdadb9d» で BLE bond するか** — する→blob 無罪＝(B)統合バグ確定／
   しない→(A)blob。NuttX 実ビルド(esp32c3+NimBLE+bonding) or docs/コミュニティ確認。
2. **blob-swap**：`esp_bt.cmake` の `-L${ESP_HAL_DIR}/.../lib_esp32c3_family` を IDF の
   `d9753a31` パスへ（＋整合する bt.c/osi 0x0B）て bond テスト。ABI 差に注意。
3. **テストループ安定化**：bond を NVS 永続化（リセットでスマホと非同期になる悪循環を断つ）＋
   実機コンソール安定取得（SVC_PERROR 診断＝`ESP32C3_BT_APIERR_TRACE=ON` を成立させる）。

**診断資産（再利用可）**：`tmp/c3ble.sh`（build/flash/boot/mark。★実機は `-DESP32C3_QEMU=OFF`
必須。by-id ポート固定・watchdog-reset で download-latch 脱出。BOARD_MAC で別機指定）／
`asp3/target/esp32c3_espidf/bt/evt_trace.c`（`ESP32C3_BT_EVT_TRACE=ON`＝暗号後 RX パイプライン
put/get/l2cap を RTC 0x50 へ）／`ESP32C3_BT_APIERR_TRACE`（SVC_PERROR）。RTC マーカ一覧＝
本節上流の各実施を参照。IDF ビルドは `IDF_TOOLS_PATH=~/tools/espressif` で export.sh。
bleprph 例は `set-target esp32c3`＋`CONFIG_EXAMPLE_BONDING=y`。

### C5 BLE ビルド回帰の修正（2026-07-14）：v9削除でC5 BT用esp_shimが失われた件を復旧
実施49(v8既定化)＋実施52(v9 `wifi/`削除)で，C5 BT/NimBLE ビルドが依存していた
«BT対応 esp_shim»（v9 `wifi/esp_shim.c`＝pend_ring/queue_reset/modem_icg_init 等）を
喪失し，`wifi_v8/esp_shim.c`（WiFi scan用 pre-D-2c サブセット）へ落ちてリンク不能に
なっていた（`undefined: esp_shim_queue_reset / esp_shim_modem_icg_init`）。

修正＝`wifi_v8/esp_shim.c` を «現行 C3 `wifi/esp_shim.c`» から再生成し C5 chip tweaks を
再適用：(1) systimer rename `esp32c3_systimer_read`→`esp32c5_systimer_read`／
`ESP32C3_SYSTIMER_TICKS_PER_US`→C5，(2) HW RNG `0x600260B0`(C3 SYSCON)→`0x600B2828`
(C5 LPPERI_RNG_DATA_SYNC_REG)，(3) ガード `TOPPERS_ESP32C3_{WIFI,BT_NIMBLE,BT_APIERR_TRACE}`
→C5，(4) C5固有 `esp_shim_modem_icg_init`（v9にのみ在り，C3版には無い）を BT_NIMBLE
ガードで非static再追加＋hal LL ヘッダ include（modem/pmu ICG 覚醒．bt_shim.c が呼ぶ）。
検証：C5 BLE(ESP32C5_BT+ble_host_smoke_c5) クリーン全ビルド link 0エラー(FLASH 9.21%/
RAM 77.44%)，C5 WiFi v8(wifi_scan) 非回帰(RAM 76.05%)。これで C5 は D-2c/D-2d 相当の
SM/GATT 追加＝bonding 試行の «土台» が復旧（C5は別blob 015db3db＝C3の壁は非該当の公算）。
残＝SM/GATT移植＋実機bondテスト（次セッション）。

### ★★★C5 実機 bond テスト（2026-07-14）：C5（別blob）も «厳密に同一» の失敗＝真因を «我々の共通 esp_shim» に確定（blob/silicon を実証で排除）
C5 に D-2d（SMP/bonding）を移植し（`ESP32C5_BT_SM`＝tinycrypt+ble_store_config、
app に SM設定/security_initiate/ENC・PAIRINGマーカ）実機テスト（board=C5#1
`d0:cf:13:f0:a7:44`）。マーカは C5 の LP_AON STORE «0-9のみ実在» のため STORE6
(0x600B1018) に ENC/PAIRING を共用（last-wins・タグ 0x5DE0/0x5DC0 で判別）。

実機（Android/スマホ）：接続→pair→切断＝**C3 と同症状**。DUT マーカ（信頼できる
STORE6）＝**`0x5de0000d`＝ENC_CHANGE status=13(BLE_HS_ETIMEOUT)・PAIRING_COMPLETE
不発**，CONN=`0x604e0001`，DISC reason=`0x13`＝**C3 と «厳密に同一の機序»**。

**★決定的な交絡排除**：C3(blob `dfdadb9d`)と C5(blob `015db3db`＝別物)が «同一» の
失敗＝bond不成立は **blob 固有ではない**。かつ stock IDF は C3(同blob)で bond 成功。
∴ **真因は blob/silicon ではなく «我々の共通 esp_shim/os_adapter»** で «実証» 確定
（2つの異なる blob で同症状）。前記「次の決定打（NuttXが同blobでbondするか／
blob-swap）」は不要＝**esp_shim（os_adapter プリミティブ）に集中**すればよい。

**残る問い**（redirect後）：我々の esp_shim の «どの» プリミティブ／挙動が IDF の
os_adapter と違い «暗号後の鍵配布 ACL» を止めるか。既反証＝キューE_CTX(pend_ring済)・
セマフォE_CTX(sem_ectx=0＝sig_semはISR可)・NPLタイマ・key_dist。次＝SVC_PERROR診断を
«C5 で» 成立させる（C5はSTOREマーカが信頼でき，非E_OK API を RTC へ記録すれば
コンソール不安定を回避できる）／IDF os_adapter との «動作時» 差分（バッファ返却・
flow control・イベント順序）を C3/C5 共通の観点で精査。C5移植は commit 369a86a、
マーカ修正は本コミット。

#### C5 SVC_PERROR 診断（同日）：esp_shim の «想定外 API エラーは無し»（SVC_PERROR=0）＝失敗は «エラーを返すコール» でなく «成功するが意味論/フローが違う» 側
C5 に SVC_PERROR を成立（`ESP32C5_BT_APIERR_TRACE`：esp_shim の非E_OK かつ
非E_CTX/E_TMOUT/E_QOVR を g_svc_err_* へ記録→app が LP_AON STORE3(0x600B100C)へ
ミラー＝esptool で回収．C5 はコンソール不安定のため RTC 経由）。実機ペアリング
（〜30秒で切断＝SM 30sタイムアウト）後：**STORE3=0x00000000＝想定外 API エラー
ゼロ**（SM=0x5de0000d＝ENC ETIMEOUT・PAIRING不発は C3 と同一）。

∴ **失敗は «esp_shim のサービスコールがエラーを返す» ものではない**（sig_sem/
tsnd_dtq/psnd_dtq 等は全て E_OK か想定内 E_CTX/E_TMOUT/E_QOVR）。＝真因は
«E_OK を返すが IDF の os_adapter と «意味論/タイミング/フロー» が違う» 側。候補：
1. **HCI フロー制御**（controller→host ACL の credit＝Host Buffer Size / Number of
   Completed Packets）．「1個目OK・2個目で止まる」「暗号前の GATT(要求応答=1個ずつ)は
   通る」に最も合致＝暗号後の «2個背中合わせ» で controller の host-ACL バッファが
   埋まり，host が完了通知を返さず controller が2個目を出さない機序．失敗するコール
   でないので SVC_PERROR に出ない．
2. セマフォ/キューの «成功するが意味論差»（give が対象タスクを起こさない・順序・credit）．
次＝IDF os_adapter（components/bt/controller/{esp32c3,esp32c5}/bt.c＋porting）との
«動作時» 差分を «HCI flow control / ACL バッファ返却・credit / イベント順序» の観点で
精査（C3/C5 共通＝どちらで直しても両方効く）．C5 は STORE マーカが信頼でき診断に好適。
