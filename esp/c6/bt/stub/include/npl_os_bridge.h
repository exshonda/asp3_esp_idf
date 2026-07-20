/*
 *  npl_os_*ブリッジ関数（bt/bt_shim.c実装）のプロトタイプ補完
 *  （force-include．BLE実施02で追加）
 *
 *  hal/components/bt/controller/esp32c6/bt.c は npl_os_funcs_init/get/
 *  deinit・npl_os_mempool_init/deinit・npl_os_set_controller_npl_info を
 *  プロトタイプ無しで直接呼ぶ（BLE実施01が特定した上流ドリフト——実体は
 *  bt/bt_shim.cのnpl_freertos_*への1:1橋渡し．docs/ble-c5c6.md
 *  「BLE実施01」参照）．実施01時点のツールチェーンでは暗黙宣言が
 *  警告どまりで実害が無かったが，本ラウンド（BLE実施02）で使用した
 *  GCC 14.2.0は既定でimplicit-function-declaration／int-to-pointer
 *  変換をハードエラーにする（GCC14の既定変更）ため，bt.c単体（D-1，
 *  apps/bt_smoke_c6）でも新規にビルド不能になっていた．hal/は編集
 *  できないため，target側でプロトタイプを補い-includeで強制する
 *  （esp_intr_alloc.h等，既存の同種の罠と同じ対処．esp_bt.cmake参照）．
 *
 *  npl_os_freertos.cが呼ぶesp_timer_is_active/esp_timer_get_expiry_time
 *  も同じ理由（esp_timer.hを自ファイルでincludeしない上流ドリフト）で
 *  同様に暗黙宣言エラーになる．★ただし対処は「-include esp_timer.h」
 *  では効かない：C6/C3共有の`asp3/target/esp32c3_espidf/hal_stub/
 *  include/esp_timer.h`（Wi-Fi統合時にesp_etm.h依存を避けるために作った
 *  意図的な簡略スタブ．esp_timer_get_time/create/delete/start_once/
 *  start_periodic/stopのみ宣言）が，同名ファイルとして本物
 *  （hal/components/esp_timer/include/esp_timer.h）より先にインクルード
 *  パス上で見つかるため，"-include esp_timer.h"はこのスタブを再度
 *  見つけるだけで新しい宣言は増えない（実機ビルドで確認：
 *  esp_timer_create等5関数はエラーにならない＝スタブ経由で解決済み，
 *  is_active/get_expiry_timeの2つだけがスタブに無く実際にエラーに
 *  なった）．スタブ自体はWi-Fiビルドとも共有のため編集せず，本ヘッダに
 *  不足2関数のプロトタイプを追加する方式で補う．
 *
 *  -includeは翻訳単位の先頭で処理される（当該ファイル自身の#include
 *  より前）ため，本ヘッダは自己完結させる（ble_npl_count_info_tの
 *  定義元nimble/npl_freertos.hを自前でincludeする．esp_timer_handle_tは
 *  esp_bt.cmakeの-include順序により本ヘッダより先に処理される
 *  "-include esp_timer.h"＝上記hal_stub版で既に定義済み）．
 */
#ifndef TOPPERS_NPL_OS_BRIDGE_H
#define TOPPERS_NPL_OS_BRIDGE_H

#include <stdint.h>
#include "nimble/nimble_npl.h"		/* ble_npl_time_t／ble_npl_error_t等
									   （npl_freertos.hが前提とする型．
									   npl_os_freertos.c自身も同じ順序で
									   includeしている） */
#include "nimble/npl_freertos.h"	/* ble_npl_count_info_t */

struct npl_funcs_t;

void npl_os_funcs_init(void);
void npl_os_funcs_deinit(void);
struct npl_funcs_t *npl_os_funcs_get(void);
int npl_os_mempool_init(void);
void npl_os_mempool_deinit(void);
int npl_os_set_controller_npl_info(ble_npl_count_info_t *ctrl_npl_info);

/*
 *  esp_timer_is_active／esp_timer_get_expiry_time：hal_stub/include/
 *  esp_timer.h（上記コメント参照）に無い2関数．esp_timer_handle_tは
 *  そのスタブが既に定義済み（本ヘッダより前に-includeされる）．
 *  実体はbt/bt_shim.cが提供する（D-2a／BLE実施02で追加．
 *  esp_bt_ctrl側では未使用のためD-1回帰には影響しない）．
 */
esp_err_t esp_timer_get_expiry_time(esp_timer_handle_t timer, uint64_t *expiry);
bool esp_timer_is_active(esp_timer_handle_t timer);

#endif /* TOPPERS_NPL_OS_BRIDGE_H */
