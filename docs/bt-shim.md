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

### 変更したファイル

| ファイル | 内容 |
|---|---|
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

### Git情報

- ベースコミット：Phase C完了時点（`e6ae2e3`）
- ブランチ：`main`（Wi-Fi/lwIPと同じ．feature branchなし＝既存の
  Phase B/C各コミットと同じ運用）
