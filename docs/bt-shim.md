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

### 次にやること（Phase D-1の残作業）

- `emi.c:164`アサートの原因特定：実機JTAG／GDBでの調査が必須
  （Wi-Fi統合時のRNGレジスタ・PSA Crypto初期化と同種の「実機のみで
  見つかる」問題の可能性が高い）。パラメータ`(0, 0x1000)`の意味
  （0x1000=4096＝メモリサイズか？）から，メモリ確保／初期化順序の
  問題である可能性を優先的に調査する。
- 原因解決後，HCI Resetコマンド送信→Command Complete受信の確認へ
  進む（Phase D-1の完了基準）。
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
