/*
 *  esp-wifi/esp-event/mbedtls用の"platform/os.h"コンパイル用スタブ
 *
 *  esp-hal-3rdpartyには本ヘッダが2種類実在する：
 *    - components/esp_system/include/platform_port/platform/os.h
 *      （ESP-IDF本来版．FreeRTOSヘッダ一式に依存）
 *    - nuttx/include/platform/os.h
 *      （NuttXポート版．NuttXカーネル内部ヘッダ<nuttx/clock.h>
 *        <nuttx/sched.h><sched/sched.h>等に依存）
 *  本リポジトリはFreeRTOSもNuttXカーネルも同梱しない（esp-hal-3rdparty
 *  はhal/soc等RTOS非依存の下層のみを使う方針．docs/dev/esp-idf-
 *  integration.md参照）ため，どちらも採用できない．
 *
 *  実体（OSキュー・スピンロック等）はPhase B-2のos_adapter shimで
 *  ASP3のAPI（loc_cpu/get_tim/snd_dtq等）に接続する．本スタブは
 *  「コンパイルを通す」ためだけの宣言（シンボルの型・マクロのみ）。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_PLATFORM_OS_H
#define TOPPERS_HAL_STUB_PLATFORM_OS_H

#include <stddef.h>
#include <stdint.h>

/*
 *  esp_wpa3.c等が期待する戻り値（NuttX版os.hのOS_PASS/OS_FAILに相当）。
 *  OS_BLOCKはwpa_supplicant/port/include/os.hのOSI_FUNCS_TIME_BLOCKING
 *  経由で別途定義されるため，ここでは重複定義しない。
 */
#define OS_PASS   1
#define OS_FAIL   0

/*
 *  esp_task.h（Bluetooth統合．Phase D-1）が
 *  ESP_TASK_BT_CONTROLLER_PRIO等をOS_TASK_PRIO_MAX基準で計算するために
 *  参照する．ESP-IDF既定のconfigMAX_PRIORITIES(=25)相当を採用する
 *  （本ビルドは実際にはesp_shim.cの固定優先度プールへ委譲するため，
 *  この値自体はマクロ計算を成立させるためだけに使われる）。
 */
#define OS_TASK_PRIO_MAX  25

/*
 *  esp_private/wifi.h（esp_os_queue_handle_t等の型のみ．
 *  実体はesp_private/wifi_os_adapter.h側のwifi_osi_funcs_tが握る）
 */
typedef void *esp_os_queue_handle_t;
typedef uint32_t esp_os_tick_type_t;
typedef void *esp_os_intr_handle_t;
typedef void (*esp_os_intr_handler_t)(void *arg);

/*
 *  esp_event.hはTickType_t/UBaseType_t/BaseType_tを本来FreeRTOSの
 *  型として使うが，`#ifdef __NuttX__` 節でのみNuttX向け代替定義
 *  （uint32_t/uint32_t/int32_t）を用意している．本ビルドは__NuttX__を
 *  定義しない（esp_mbedtls.hのNuttX向けシンボルリネームを誘発し，
 *  mbedtls実体のシンボル名と不整合を起こすため．詳細はesp_wifi.cmake
 *  コメント参照）ので，同じ代替定義をここで肩代わりする。
 */
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int32_t  BaseType_t;

/*
 *  esp_private/critical_section.h（esp_hw_support/periph_ctrl.c等が使う
 *  クリティカルセクションマクロ）向けのシングルコア実装。
 *  OS_SPINLOCK を定義しないため（本ビルドはESP32-C3単一コア），
 *  同ヘッダは自動的に「スピンロック無し＝割込み禁止のみ」の
 *  _NO_LOCK系マクロを選択する（NuttX版platform/os.hの
 *  nuttx_enter_critical()と同じ発想）。実体はASP3のloc_cpu/unl_cpu
 *  （ネストは呼び出し側の対構造で保証される想定．periph_ctrl.c内の
 *  各関数は enter〜exit を必ず対で閉じたブロックとして使うため十分）。
 */
#ifndef TOPPERS_MACRO_ONLY
/*
 *  <kernel.h>（<t_stddef.h>経由）はassert()を「TOPPERS_assert_fail()＋
 *  TOPPERS_assert_abort()を呼ぶ版」に（再）定義する．本ヘッダを
 *  esp_err.h（<assert.h>＝hal_stub/include/assert.hのno-op版を
 *  #include済み）より後に取り込むhal/配下のファイル（例：
 *  esp_phy/src/phy_init.c）では，この再定義が以降のassert()呼び出し
 *  （phy_init.c自身のassert()や，esp_cpu.h等が使うHAL_ASSERT経由の
 *  assert()）に伝播し，t_syslog.hを#includeしていないTU（TOPPERS_
 *  assert_failがマクロ化されず素の関数参照になる＝ASP3側で実体を
 *  持たない）ではリンクエラーになる．push_macro/pop_macroで
 *  <kernel.h>取り込み前後のassert()定義を保存・復元し，hal/側コード
 *  からは常にno-op版のassert()が見えるようにする（既にassert()が
 *  定義されている場合のみ．未定義の場合はTOPPERS版をそのまま採用＝
 *  ASP3の他ファイルからの利用と同じ挙動）．
 */
#ifdef assert
#pragma push_macro("assert")
#define TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT 1
#endif
#include <kernel.h>
#ifdef TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT
#pragma pop_macro("assert")
#undef TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT
#endif
#define OS_ENTER_CRITICAL_NO_LOCK()        ((void) loc_cpu())
#define OS_EXIT_CRITICAL_NO_LOCK()         ((void) unl_cpu())
#define OS_ENTER_CRITICAL_NO_LOCK_ISR()     ((void) loc_cpu())
#define OS_EXIT_CRITICAL_NO_LOCK_ISR()      ((void) unl_cpu())
#define OS_ENTER_CRITICAL_NO_LOCK_SAFE()    ((void) loc_cpu())
#define OS_EXIT_CRITICAL_NO_LOCK_SAFE()     ((void) unl_cpu())

/*
 *  OS_IN_ISR()：esp_phy/src/phy_init.cのphy_enter_critical/
 *  phy_exit_critical（タスク文脈か非タスク文脈かでISR版クリティカル
 *  セクションマクロを使い分ける）が要求する．ASP3のsns_ctx()
 *  （TOPPERS標準API．非タスクコンテキスト＝割込みハンドラ内で真）を
 *  そのまま使う．
 */
#define OS_IN_ISR()                         (sns_ctx())
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_HAL_STUB_PLATFORM_OS_H */
