# dedup Tier 2 — `esp_shim.c` コア共有 設計・計画（2026-07-18・branch `claude/target-dedup-tier2`）

正本の背景・判断＝`memory/project-target-dedup-plan`（Codex 意見＋esp32_s3 precedent）。本doc は実行設計。

## 目的
`asp3/target/esp32c{3,5,6}_espidf/wifi/esp_shim.c`（~1655行・C3↔C5 90%／C3↔C6 74% 同一）の
**共有コアを1ファイルに集約**し fix-once 化（#3/#5/#6/#8/#9 は 3×手作業複製した＝重複の実コスト）。

## 関数レベル分類（3チップ diff・実測 2026-07-18）
### ★共有（0 diff・~43関数）→ `common_espidf/wifi/esp_shim_core.c`
int_disable, int_restore, tick_to_tmo, log_write, heap_initialize, malloc, free, calloc, realloc,
heap_free_size, sem_create, sem_delete, sem_flush_pending, signal_or_pend, wakeup_flush_pending,
mutex_create/delete/lock/unlock, queue_create, queue_delete, slot_alloc, slot_free, slot_free_notify,
slot_alloc_debt_copy, pend_push_slot, pend_push, queue_flush_pending, queue_send, queue_send_from_isr,
queue_recv, queue_msg_waiting, queue_reset, task_entry, task_delete, task_get_current, task_yield,
thread_semphr_get, timer_find, timer_setfn, timer_disarm, timer_done, set_isr

### チップ固有（differ）→ per-chip `wifi/esp_shim_chip.c` + `esp_shim_chip_config.h`
- **genuine chip**：`random`（RNG 番地 C3=0x600260B0/C5C6 別）・`shim_int_dispatch`（ISR/番地/storm）・
  `time_us`（systimer）・`initialize`（PMU/modem init）・`task_create`（優先度）・`svc_perror`
- **C6 のみ differ（stale 診断の疑い）**：`enter_critical`(+10)・`exit_critical`(+20)・`sem_give`(+10)・
  `task_delay`(+22)・`timer_arm_us`(+6)・`timer_task`(+7)・`sem_get_count`(+2)／`sem_take`(全3差18)
  →当面 per-chip 据え置き（診断整理は別途）。

## ★主リスク＝跨ぐ file-scope static の extern 化
共有関数と «チップ関数» が両方使う static を extern 化する必要がある：
- `shim_sem_id[]`/`shim_sem_used[]`/`shim_sem_pend[]`/`shim_sem_pend_total`/カウンタ群
  （sem_create/delete〔core〕× sem_take/give〔chip〕）
- `shim_tsk[]`（task_* core × task_create/delay chip）
- `shim_isr_tbl[]`/`esp_shim_int_count[]`（set_isr〔core〕× shim_int_dispatch〔chip〕）
- `SHIM_LOCK/UNLOCK` マクロ・`esp_shim_int_disable`（多数）→共有 header か core に。
→ **«小さいパイロット» 不可＝共有コアは一括移動**（依存が絡むため）。

## 実行手順（段階・各段でビルド）
1. `common_espidf/wifi/esp_shim_core.c` に共有43関数＋それらが使う static＋マクロを移す。
   跨ぐ static は core で定義し `esp_shim_core.h`(or 既存 esp_shim.h) に extern 宣言。
2. 各 chip の `esp_shim.c` を «チップ関数のみ» に縮小（→ `esp_shim_chip.c`）。跨ぐ static は extern 参照。
3. 各 `target.cmake`：`${COMMON}/wifi/esp_shim_core.c` ＋ 自 chip の `esp_shim_chip.c` をコンパイル、
   旧 `esp_shim.c` を外す。include path に common を追加。
4. **全14構成ビルド**（wifi/bt × C3/C5/C6 ＋ idf61 等）を green に。undefined/duplicate symbol を潰す。
5. **実機で1チップ再確認**（少なくとも C3 か C5 の wifi+BT）してから本線へ merge。

## 設計判断（Codex）
- **専用 `common_espidf/`**（C3-canonical でなく＝隠れ依存を避ける）。
- **common に `#ifdef TOPPERS_ESP32Cx` を入れない**（チップ差は per-chip config header/hooks）。
- **番地は soc symbolic 名へ寄せると #ifdef が減る**（esp32_s3 は esp_shim.c で #ifdef 0）。
- C5 は wifi_v8・別 blob 世代＝`esp_wifi_adapter.c`/`blobglue` は共通化しない（本Tier は esp_shim.c コアのみ）。

## 状態
- branch `claude/target-dedup-tier2`（本線 `claude/c5-espidf-supply-migration` は push 済み・無傷）。
- 本doc がコミット第1歩。次＝手順1から実装（大改修＝段階＋各段ビルド）。
