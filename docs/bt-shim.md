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

### Git情報

- ベースコミット：Phase C完了時点（`e6ae2e3`）
- ブランチ：`main`（Wi-Fi/lwIPと同じ．feature branchなし＝既存の
  Phase B/C各コミットと同じ運用）
