/*
 *  ESP32-C6/C5世代BTコントローラ（controller/esp32c6/bt.c）用の
 *  "platform/os.h" 互換ヘッダ（BLE実施01）
 *
 *  C6/C5世代のbt.cはFreeRTOS APIを直接呼ばず，"platform/os.h" が提供
 *  する esp_os_*（task/intr）関数群を経由する（NuttXが hal/nuttx/
 *  include/platform/os.h + src/platform/os.c で自前実装しているのと
 *  同じ構図）．esp-hal-3rdparty同梱の
 *  components/esp_system/include/platform_port/platform/os.h は
 *  マクロ（OS_PORT_*）のみでesp_os_*関数群を宣言しない（このhal
 *  submoduleは実質NuttX専用に保守されており，genuine ESP-IDF側の
 *  esp_os_*実装はこのツリーに同梱されていない）．
 *
 *  一方，C3のWi-Fi/BT統合で既に用意した
 *  asp3/target/esp32c3_espidf/hal_stub/include/platform/os.h は
 *  OS_TASK_PRIO_MAX・クリティカルセクションマクロ（OS_ENTER/EXIT_
 *  CRITICAL_NO_LOCK*）等を提供済み（esp_task.h・esp_private/
 *  critical_section.h＝modem_clock.c等が要求）．本ヘッダはそれを
 *  #include_next で継承しつつ，esp_os_*（task/intr）の宣言だけを
 *  追加する．本ディレクトリ（bt/stub/include）をincludeパスの
 *  hal_stub より前に置くことで，"platform/os.h" の解決は必ず本ファイル
 *  から始まる（esp_bt.cmake参照）．
 */
#ifndef TOPPERS_BT_C6_PLATFORM_OS_H
#define TOPPERS_BT_C6_PLATFORM_OS_H

/*  hal_stub版（OS_TASK_PRIO_MAX・クリティカルセクション等）を継承  */
#include_next "platform/os.h"

/*
 *  tskNO_AFFINITY：bt.cのtask_create_wrapperがcore_id引数の比較に使う
 *  （NuttX版platform/os.hと同じ定義．CONFIG_FREERTOS_NUMBER_OF_CORES=1
 *  なのでtskNO_AFFINITY=1）．C3のbt/stub/include/freertos/task.hは
 *  独自定義（0x7fffffff）を持つが，そちらはfreertos/task.h名前空間
 *  （npl_os_freertos.c用）でありbt.c本体（platform/os.h名前空間）とは
 *  別のヘッダなので衝突しない．
 */
#ifndef tskNO_AFFINITY
#define tskNO_AFFINITY ((1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1)
#endif

#ifndef TOPPERS_MACRO_ONLY
#include "esp_intr_alloc.h"		/* intr_handle_t／intr_handler_t */

/*
 *  タスク管理型（NuttX版platform/os.hと同一の型形状．実体はpid_t
 *  ではなく，ASP3側shimスロットへのポインタをそのまま収める）．
 */
typedef void *esp_os_task_handle_t;
typedef void (*esp_os_task_function_t)(void *arg);

/*
 *  bt.cのtask_create_wrapper/task_delete_wrapperが直接呼ぶ（実体は
 *  bt/bt_shim.c，esp_shim_task_create/deleteへ委譲）．
 */
extern int esp_os_create_task_pinned_to_core(esp_os_task_function_t task_func,
											  const char *name,
											  uint32_t stack_depth,
											  void *arg,
											  int priority,
											  esp_os_task_handle_t *handle,
											  int core_id);
extern void esp_os_task_delete(esp_os_task_handle_t handle);

/*
 *  bt.cのesp_intr_alloc_wrapper/esp_intr_free_wrapperが直接呼ぶ（実体は
 *  bt/bt_shim.c．INTMTX＋PLIC_MXルーティング＋多重登録安全なスロット
 *  配列．C3のbt_shim.cのesp_intr_alloc設計とS3由来の教訓
 *  （docs/s3-bt-intr-source-overwrite-fix-for-c3.md）を踏襲する）．
 */
extern esp_err_t esp_os_intr_alloc(int source, int flags,
									intr_handler_t handler, void *arg,
									intr_handle_t *ret_handle);
extern esp_err_t esp_os_intr_free(intr_handle_t handle);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_C6_PLATFORM_OS_H */
