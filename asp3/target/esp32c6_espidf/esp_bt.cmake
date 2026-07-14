#
#               ESP32-C6 Bluetooth（BLE）統合 Phase D-1／BLE実施01
#
#  コントローラ起動＋VHCIループバック（ホストスタック無し）．設計は
#  docs/ble-c5c6-plan.md，実装ログはdocs/ble-c5c6.md「BLE実施01」．
#
#  C3（esp32c3_espidf/esp_bt.cmake）との違い：C6/C5世代のBTコントローラ
#  （components/bt/controller/esp32c6/bt.c）はソース配布だがFreeRTOS API
#  を直接呼ばず，"platform/os.h" のesp_os_*（task/intr）関数群と3つの
#  登録テーブル（osi_coex_funcs_t／ext_funcs_t／npl_funcs_t）を経由する．
#  bt.c自身がosi_coex_funcs_t（coex無効時は全no-op）とnpl_funcs_t
#  （porting/npl/freertos/src/npl_os_freertos.cが自前提供）を埋めるため，
#  ASP3が新規実装するのはesp_os_*（bt/bt_shim.c）とnpl_os_freertos.cが
#  要求するFreeRTOS原始プリミティブ（queue/semaphore/task/timer．C3の
#  bt/stub/include/freertos/*.hをそのまま流用）だけで良い．
#
#  RAM予算のためESP32C3_BT同様，ESP32C6_WIFIとの同時ONは現時点で未対応
#  （FATAL_ERRORはtarget.cmake側）．
#

if(ESP32C6_BT)

#
#  ★BLE実施13（docs/ble-c5c6-plan.md §13）：IDF v6.1 matched-set swap
#  トグル．ON にすると bt/phy/coex/libble_app.a を hal submodule ではなく
#  IDF v6.1（~/tools/esp-idf-v6.1）から採り，C3型 bt.c（esp_bt_idf61.cmake）
#  で D-1（controller-only）をビルドする．既定 OFF＝従来の hal 版（本
#  ファイル本体）．RFシンセ非ロック（§11/§12）が v6.1 で解けるかの実機
#  判定用．board C を戻すときは OFF で再ビルド or build/c6bt_fix を再フラッシュ．
#
option(ESP32C6_BT_IDF61 "Use IDF v6.1 matched-set (bt/phy/coex/libble_app.a) instead of hal submodule for C6 BLE D-1 (BLE実施13)" OFF)
if(ESP32C6_BT_IDF61)
    include(${CMAKE_CURRENT_LIST_DIR}/esp_bt_idf61.cmake)
else()

set(BT_TARGETDIR ${TARGETDIR}/bt)
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

#
#  ESP32C6_BT_NIMBLEの判定を先出しする（下の「2. ソースファイル」節で
#  hci_driver_standard.c／hci_driver_nimble.cの二者択一に使うため．
#  ブロック本体＝ソース/インクルード追加は末尾のD-2a節で行う）．
#
option(ESP32C6_BT_NIMBLE "Enable NimBLE host stack on top of BT controller (Phase D-2a/BLE実施02)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c6")
    set(ESP32C6_BT_NIMBLE ON)
endif()

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C6_BT
    #  bt.cは`#ifdef ESP_PLATFORM`でesp_log.hの実include可否を分岐する
    #  （未定義だとESP_LOGWがESP_LOG_LEVEL_LOCAL等へマクロ展開されず，
    #  他ヘッダ由来の素の関数宣言のみが残りリンクエラーになる．実機
    #  ビルドで判明）．
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C6=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  VHCI（HCI_TRANSPORT_VHCI経由．esp_vhci_host_*公開API）を選択．
    CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=1
    #  msys（HCI ACL用共有mbufプール）の初期化をコントローラ内部
    #  （blobのr_esp_ble_msys_init）へ委譲する．OFFにすると
    #  porting/nimble/src/os_mbuf.c／mem.c（C3の古い世代専用ツリー）が
    #  別途必要になり本ビルドのソース集合に含まれないため，必ずONにする
    #  （esp32c6 Kconfig既定値もy．詳細はdocs/ble-c5c6.md）．
    CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1
    #  ↑がyのため実行時にはr_esp_ble_msys_init(blob)へ委譲されるが，
    #  os_msys_init.cはOS_MSYS_*_BLOCK_COUNT/SIZE計算のため以下を
    #  無条件に参照する（esp32c6/Kconfig.inの既定値）．
    CONFIG_BT_LE_MSYS_1_BLOCK_COUNT=12
    CONFIG_BT_LE_MSYS_1_BLOCK_SIZE=256
    CONFIG_BT_LE_MSYS_2_BLOCK_COUNT=24
    CONFIG_BT_LE_MSYS_2_BLOCK_SIZE=320
    CONFIG_BT_LE_MSYS_BUF_FROM_HEAP=1
    #  npl_os_freertos.cのcallout実装にesp_timer_*（bt/bt_shim.c提供）を
    #  選択し，FreeRTOSソフトタイマ（xTimerCreate等）経路を避ける
    #  （C3のD-2a節と同じ判断．bt/stub/include/freertos/timers.hは型と
    #  プロトタイプのみで実体を持たないため必須）．
    CONFIG_BT_LE_USE_ESP_TIMER=1
    #  esp_bt_cfg.hがフォールバック無しで無条件参照する4項目
    #  （esp32c6/Kconfig.inの既定値をそのまま採用）．
    CONFIG_BT_LE_COEX_PHY_CODED_TX_RX_TLIM_EFF=0
    CONFIG_BT_LE_DFT_TX_POWER_LEVEL_DBM_EFF=0
    CONFIG_BT_LE_DFT_ADV_SCHED_PRIO_LEVEL=0
    CONFIG_BT_LE_DFT_PERIODIC_ADV_SCHED_PRIO_LEVEL=1
    CONFIG_BT_LE_DFT_SYNC_SCHED_PRIO_LEVEL=1
    #  BT_CONTROLLER_INIT_CONFIG_DEFAULT()（esp32c6/esp_bt.h）が参照する
    #  残りのCONFIG_*一式（esp32c6/Kconfig.inの既定値をそのまま採用．
    #  bt_smoke_c6はKconfig非経由のためここで明示的に埋める）．
    CONFIG_BT_LE_LL_RESOLV_LIST_SIZE=4
    CONFIG_BT_LE_LL_DUP_SCAN_LIST_COUNT=20
    CONFIG_BT_LE_LL_SCA=60
    CONFIG_BT_LE_CONTROLLER_TASK_STACK_SIZE=4096
    CONFIG_BT_LE_EXT_ADV_RESERVED_MEMORY_COUNT=2
    CONFIG_BT_LE_CONN_RESERVED_MEMORY_COUNT=2
    #  CONFIG_XTAL_FREQはhal/nuttx/esp32c6/include/sdkconfig.h（カーネル
    #  共通で常時include済み）が既に40を定義済み（ASP3実測40MHz XTALと
    #  一致，Phase A実機結果）のため重複定義しない．CPUクロック160MHzは
    #  同sdkconfig.hが `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
    #  CONFIG_ESPRESSIF_CPU_FREQ_MHZ` という間接マクロにしているため，
    #  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZを直接-Dしても後勝ちで無効化される
    #  （sdkconfig.hが後からredefineする）．実体のCONFIG_ESPRESSIF_
    #  CPU_FREQ_MHZ側を定義する（target_kernel_impl.cのCORE_CLK_MHZと
    #  一致させる）．
    CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160
    #  esp_ipc.hのesp_ipc_func_t等はCONFIG_ESP_IPC_ENABLE無しでは
    #  丸ごと非公開になる（C3のesp_bt.cmakeと同じ理由）．
    CONFIG_ESP_IPC_ENABLE
    #  esp_wifi.cmake／C3のesp_bt.cmakeと同じ理由（PLL温度追従は較正
    #  データ永続化前提．本ビルドは毎回フル較正のため無効化で十分）
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    #  MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL：phy_init.cがheap_caps.hを
    #  #includeせず直値のビットマスクを期待するため（値の意味自体は
    #  esp_shim_libc.cのheap_caps_*がcapsを無視するため持たない．
    #  シンボル解決のみ）
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  npl_os_freertos.cはnimble/nimble_npl.h（→nimble_npl_os.h．IRAM_ATTR
#  使用のstatic inline群）を，esp_heap_caps.h等esp_attr.hを間接的に
#  含むヘッダより前にincludeする（C3のesp_bt.cmakeの-include
#  esp_intr_alloc.hと同種の事情）．IRAM_ATTRが未定義のまま
#  static inline宣言の途中でパースエラーになるため，全Cファイルへ
#  esp_attr.hを強制includeする．
#
#
#  npl_os_freertos.cはESP_IDF_VERSION/ESP_IDF_VERSION_VAL（esp_idf_
#  version.h）を#if内で使うが，自ファイルではincludeしない
#  （実ESP-IDFではビルドシステムが暗黙includeする前提のヘッダ．
#  esp_intr_alloc.hと同種の事情）．未定義のまま#if内で関数マクロ
#  呼出し風に使われると"missing binary operator"エラーになるため，
#  esp_attr.hと合わせて強制includeする．
#
#  ★2個以上の-includeを併用する場合はSHELL:接頭辞が必須（C3の
#  esp_bt.cmake「D-2b」節の罠と同じ．無しだと2個目以降の引数分割が
#  壊れ"cannot specify -o with -c/-S/-E with multiple files"になる）．
#  esp_bt_cfg.h（BT_LL_ADV_SM_RESERVE_CNT_N等）はMIN()マクロを
#  <sys/param.h>の暗黙includeを前提に使う（実ESP-IDFはビルドシステム
#  経由でグローバルに見える）．未定義だと関数呼出しと誤認され
#  リンクエラーになる（実機ビルドで判明．hal_stub/include/sys/param.h
#  に既存のMIN定義あり＝そちらへ強制誘導する）．
#  os/os_mempool.h・os/os_mbuf.hは
#  `#if SOC_ESP_NIMBLE_CONTROLLER && CONFIG_BT_CONTROLLER_ENABLED` で
#  r_プレフィクス版（os_memblock_get→r_os_memblock_get等，blob/ROM
#  実体と一致するASP3が使うべき側）かplain名版かを切替えるが，
#  hci_driver_standard.c／npl_os_freertos.cは"soc/soc_caps.h"
#  （SOC_ESP_NIMBLE_CONTROLLERの定義元）より前に"os/os_mbuf.h"や
#  "os/os_mempool.h"をincludeするため，plain名版が選ばれてしまい
#  未定義参照になる（実機リンクで判明）．soc_caps.hを強制的に最初へ
#  includeして常にr_版を選ばせる．
#
#  ★BLE実施02（GCC14.2.0での再ビルドで発覚．D-1にも波及する既存不具合の
#  修正）：本環境のGCC 14.2.0はimplicit-function-declaration／
#  int-to-pointer変換を既定でハードエラーにする（実施01時点のツール
#  チェーンでは警告どまりだった）．npl_os_freertos.cはesp_timer.hを
#  自ファイルでincludeせずesp_timer_is_active/esp_timer_get_expiry_time
#  を直接呼ぶ（実ESP-IDFのビルドシステムが暗黙includeする前提のヘッダ．
#  上のsoc/soc_caps.h等と同種の事情）．bt.cはnpl_os_funcs_init等
#  （実体はbt/bt_shim.cのブリッジ関数）をプロトタイプ無しで直接呼ぶ
#  （BLE実施01が特定した上流ドリフト）．いずれもhal/は編集できないため，
#  target側でプロトタイプ／宣言を強制includeする．
#  npl_os_bridge.hの詳細＝同ファイル冒頭コメント参照．
#
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_timer.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include npl_os_bridge.h>"
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#
#  ★重要：BT_TARGETDIR/stub/includeを先頭へPREPENDする．
#  "platform/os.h" の解決はまず本ディレクトリのos.h（esp_os_*宣言を
#  追加）に当たり，そのos.hが#include_nextでC3のhal_stub/include版
#  （target.cmakeが既に全ビルド共通でASP3_INCLUDE_DIRSへ追加済み＝
#  この時点で既にリストに入っている）へフォールバックする設計．
#  単純APPENDだとhal_stub版が先に見つかり，esp_os_*宣言が失われる．
#
list(PREPEND ASP3_INCLUDE_DIRS ${BT_TARGETDIR}/stub/include)

list(APPEND ASP3_INCLUDE_DIRS
    ${TARGETDIR}/wifi
    ${C3_TARGETDIR}/bt/stub/include
    ${ESP_HAL_DIR}/components/bt/include/esp32c6/include
    ${ESP_HAL_DIR}/components/bt/common/include
    ${ESP_HAL_DIR}/components/bt/common/ble_log/include
    ${ESP_HAL_DIR}/components/bt/porting/include
    ${ESP_HAL_DIR}/components/bt/porting/include/os
    ${ESP_HAL_DIR}/components/bt/porting/npl/freertos/include
    ${ESP_HAL_DIR}/components/bt/porting/transport/include
    ${ESP_HAL_DIR}/components/bt/controller/esp32c6
    #  esp_nimble_mem.h（porting/mem/os_msys_init.cが要求）
    ${ESP_HAL_DIR}/components/bt/host/nimble/port/include
    #  os/os.h・os/os_trace_api.h・modlog/modlog.h（os_mbuf.cが要求．
    #  upstream nimbleツリーの共通include）
    ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/porting/nimble/include
    ${ESP_HAL_DIR}/components/esp_hw_support/include
    ${ESP_HAL_DIR}/components/esp_hw_support/include/soc
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c6/private_include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/include
    ${ESP_HAL_DIR}/components/esp_system/include
    #  esp_private/wifi.h（phy_init.cが要求．C3のesp_bt.cmakeと同じ理由
    #  ＝BTもWi-Fiと同じPHY実ソースを使うため）
    ${ESP_HAL_DIR}/components/esp_wifi/include
    ${ESP_HAL_DIR}/components/esp_phy/include
    ${ESP_HAL_DIR}/components/esp_phy/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_pm/include
    ${ESP_HAL_DIR}/components/esp_timer/include
    ${ESP_HAL_DIR}/components/esp_coex/include
    ${ESP_HAL_DIR}/components/esp_rom/include
    ${ESP_HAL_DIR}/components/esp_rom/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_rom/esp32c6/include/esp32c6
    ${ESP_HAL_DIR}/components/esp_rom/esp32c6
    ${ESP_HAL_DIR}/components/heap/include
    ${ESP_HAL_DIR}/components/log/include
    ${ESP_HAL_DIR}/components/riscv/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_hal_clock/include
    ${ESP_HAL_DIR}/components/esp_hal_clock/esp32c6/include
    ${ESP_HAL_DIR}/components/efuse/include
    ${ESP_HAL_DIR}/components/efuse/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_event/include
    #  hal/pmu_ll.h・hal/pmu_hal.h（wifi/esp_shim.cのesp_shim_modem_icg_init
    #  が要求．esp_wifi.cmakeにも同じ2行がある＝WiFi/BT共有ファイルの
    #  依存としてBT側でも要る）
    ${ESP_HAL_DIR}/components/esp_hal_pmu/include
    ${ESP_HAL_DIR}/components/esp_hal_pmu/esp32c6/include
    #  hal/rtc_timer_hal.h（rtc_time.cが要求）
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/include
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/esp32c6/include
    #  hal/timg_ll.h（rtc_time.cが要求）
    ${ESP_HAL_DIR}/components/esp_hal_timg/include
    ${ESP_HAL_DIR}/components/esp_hal_timg/esp32c6/include
)

#
#  ------------------------------------------------------------------
#  2. ソースファイル（D-1最小集合＝controller-only＋VHCI）
#  ------------------------------------------------------------------
#  hal/components/bt/CMakeLists.txt L65-106（CONFIG_BT_CONTROLLER_ENABLED）
#  ＋L694-743（CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT＝新世代npl
#  ＋CONFIG_BT_LE_HCI_INTERFACE_USE_RAM＝VHCI，非NimBLE分岐）で確認した
#  最小ソース集合．
#
list(APPEND ASP3_CFG_FILES ${BT_TARGETDIR}/bt.cfg)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_HAL_DIR}/components/bt/controller/esp32c6/bt.c
    ${ESP_HAL_DIR}/components/bt/controller/esp32c6/ble.c
    ${ESP_HAL_DIR}/components/bt/porting/npl/freertos/src/npl_os_freertos.c
    ${ESP_HAL_DIR}/components/bt/porting/mem/os_msys_init.c
    ${ESP_HAL_DIR}/components/bt/porting/mem/bt_osi_mem.c
    #  ★os_mbuf.c/os_mempool.cは自前で持たない：libble_app.a自身が
    #  os_mbuf.c.o（g_msys_pool_list等）を同梱しており，自前リンクすると
    #  多重定義になる（実機リンクで判明．最初の判断
    #  ＝「os_mbuf.cが必要」は誤り，nmでの"U"表示はアーカイブ内members間
    #  の相互参照を見誤ったもの）．hci_driver_standard.cが要求する
    #  r_os_msys_get_pkthdr等はlibble_app.aから自動的に解決される．
    #  esp_clk_tree_lp_slow_get_freq_hz（bt.cが直接呼ぶ．esp_wifi.cmakeの
    #  ような既存リンクが無いためBT側で自前リンク．実体はcommon.c側）
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c6/esp_clk_tree.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_HAL_DIR}/components/esp_hal_clock/esp32c6/clk_tree_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c6/rtc_time.c
    ${ESP_HAL_DIR}/components/bt/porting/transport/src/hci_transport.c
    ${BT_TARGETDIR}/bt_shim.c
    #  hci_driver_standard.c と hci_driver_nimble.c（D-2a節）は共に
    #  hci_driver_vhci_opsを定義するため二者択一（同時リンク不可）．
    #  D-1（controller-only．bt_smoke_c6）はstandard版を使う．
    #  NimBLE ON時（ble_host_smoke_c6）はD-2a節がnimble版を追加する．
    #  esp_wifi.cmakeと同じ理由でPHY/クロック/ペリフェラルの実ソースを
    #  採用する（BTもWi-Fiと同じ無線ハードウェアを使うため必要）．
    #  modem_clock.c／modem_clock_hal.cはWiFi非同時ONの制約があるため
    #  BT単体ビルドでも自前で持つ必要がある（esp_wifi.cmakeのif
    #  (ESP32C6_WIFI)ブロック内にありBTからは見えないため）．
    ${ESP_HAL_DIR}/components/esp_phy/src/phy_init.c
    ${ESP_HAL_DIR}/components/esp_phy/src/phy_common.c
    ${ESP_HAL_DIR}/components/esp_phy/esp32c6/phy_init_data.c
    ${ESP_HAL_DIR}/components/esp_phy/src/lib_printf.c
    #  btbb_init.c（esp_btbb_enable/disable．bt.cが呼ぶ．CONFIG_SOC_BT_
    #  SUPPORTED時は必須．hal/components/esp_phy/CMakeLists.txtの
    #  has_libbtbb分岐と同じ判断——実機リンクでlibble_app.aの
    #  esp_ble_interface.c.oがglobal_ext_bb_funcs経由でbt_bb_*シンボルを
    #  直接参照することが判明し，「btbb相当は不要」という設計書6節1の
    #  当初判断を訂正した．libbtbb.aは`bt/CMakeLists.txt`ではなく
    #  esp_phy/lib/esp32c6/に同居している）
    ${ESP_HAL_DIR}/components/esp_phy/src/btbb_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/modem_clock.c
    ${ESP_HAL_DIR}/components/hal/esp32c6/modem_clock_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_HAL_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c6/rtc_clk.c
    #  efuse_hal_chip_revision()：BT_CONTROLLER_INIT_CONFIG_DEFAULT()と
    #  modem_clock_select_lp_clock_source()の両方が参照する（esp_wifi.cmake
    #  と同じ理由．ESP32C6_WIFI限定ブロック内のためBT側でも自前で持つ）．
    ${ESP_HAL_DIR}/components/hal/efuse_hal.c
    ${ESP_HAL_DIR}/components/hal/esp32c6/efuse_hal.c
)

if(NOT ESP32C6_BT_NIMBLE)
    #  ★D-1＝controller-onlyスモークテスト（NimBLEホスト無し）限定．
    #  CONFIG_BT_CONTROLLER_ONLYとCONFIG_BT_NIMBLE_ENABLEDは実ESP-IDFの
    #  Kconfigでは同時に1にならない排他選択（advisorレビュー指摘）．
    #  両方1のまま動かした実害箇所は現時点で未確認だが（hci_transport.c
    #  のACL rxガードはNIMBLE_ENABLED&&ROLE_*のORで別途真になるため
    #  無害と確認済み），未検証の組合せをC6実機のBLE初回結果に持ち込む
    #  リスクを避けるためD-1限定に閉じ込める．
    list(APPEND ASP3_COMPILE_DEFS
        CONFIG_BT_CONTROLLER_ONLY=1
    )
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${ESP_HAL_DIR}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    )
endif()

#
#  ------------------------------------------------------------------
#  3. リンクライブラリパス・ライブラリ
#  ------------------------------------------------------------------
#  ★実施92メモ（設計書リスク2）：無印C6（esp32c6サブディレクトリ）を
#  選ぶ．esp32c61は別チップ（hal/components/bt/CMakeLists.txtの
#  CONFIG_IDF_TARGET_ESP32C61分岐）で本リポジトリの対象外．
#
list(APPEND ASP3_LINK_OPTIONS
    -L${ESP_HAL_DIR}/components/bt/controller/lib_esp32c6/esp32c6-bt-lib/esp32c6
    -L${ESP_HAL_DIR}/components/esp_phy/lib/esp32c6
    -L${ESP_HAL_DIR}/components/esp_coex/lib/esp32c6
)
list(APPEND ASP3_LINK_LIBS
    ble_app
    phy
    btbb
    coexist
)

#
#  ------------------------------------------------------------------
#  4. ROM関数ld（esp_wifi.cmakeと同じ一覧．BT専用ldは存在しない
#     ＝コントローラ本体はflash blob(libble_app.a)に完全常駐のため
#     eco3_bt_funcs.ld相当は不要．設計書6節1で確認済み）．
#  ------------------------------------------------------------------
#
set(BT_ROM_LD_DIR ${ESP_HAL_DIR}/components/esp_rom/esp32c6/ld)
set(ESP_BT_ROM_LD_FILES
    ${BT_ROM_LD_DIR}/esp32c6.rom.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.api.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.libc.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.libgcc.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.newlib.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.libc-suboptimal_for_misaligned_mem.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.version.ld
    ${ESP_HAL_DIR}/components/riscv/ld/rom.api.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.phy.ld
    ${BT_ROM_LD_DIR}/esp32c6.rom.systimer.ld
)
foreach(_esp_bt_rom_ld ${ESP_BT_ROM_LD_FILES})
    list(APPEND ASP3_LINK_OPTIONS -Wl,-T,${_esp_bt_rom_ld})
endforeach()

#
#  ==================================================================
#  Phase D-2a／BLE実施02：NimBLE ホストスタック
#  ==================================================================
#
#  D-1（コントローラ＋VHCI，bt_smoke_c6）の上に NimBLE ホストを載せる．
#  ★C3（esp32c3_espidf/esp_bt.cmake）のD-2ブロックとの決定的な違い：
#  C6/C5は新世代コントローラ（SOC_ESP_NIMBLE_CONTROLLER=1）のため，
#  hal/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c
#  の esp_nimble_init() 内部で esp_nimble_hci_init() の呼出し自体が
#  `#if !SOC_ESP_NIMBLE_CONTROLLER || !CONFIG_BT_CONTROLLER_ENABLED`で
#  コンパイルアウトされる＝C3が使うLEGACY VHCI経路（esp_nimble_hci.c＋
#  hci_esp_ipc_legacy.c）はC6には存在しない．C6は
#  hci_transport.c（D-1で既存）＋hci_driver_nimble.c＋hci_esp_ipc.cという
#  別のHCIトランスポートを使う（D-1のhci_driver_standard.cとは
#  hci_driver_vhci_ops一つを取り合う関係＝二者択一．NimBLE ON時は
#  hci_driver_standard.cを外しhci_driver_nimble.c+hci_esp_ipc.cへ差替える）．
#  詳細はdocs/ble-c5c6.md「BLE実施02」．
#
#  RAM予算のため既定はOFF．NimBLEを要するアプリ（ble_host_smoke_c6）では
#  自動でONにする（D-1のbt_smoke_c6は痩せたまま保つ）．
#  ★ESP32C6_BT_NIMBLEのoption()/自動ON判定はファイル冒頭（「2. ソース
#  ファイル」節でのhci_driver二者択一に使うため）で既に行っている．
#

if(ESP32C6_BT_NIMBLE)

    set(NIMBLE_ROOT ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/nimble)
    set(BT_ROOT ${ESP_HAL_DIR}/components/bt)
    set(TINYCRYPT_ROOT ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/ext/tinycrypt)

    #  ---- ★D-2d：SMP（ペアリング／ボンディング）有効化 ----
    #  C5のESP32C5_BT_SM（369a86a）のC6版．ON時はMYNEWT_VAL_BLE_SM_LEGACY/SC=0
    #  の«蓋»を外し（tinycrypt/mbedTLSリンク回避の蓋を外す），SC=ECDH P-256の
    #  cryptoをvendored tinycryptで供給する．bond storeはble_store_ram
    #  （IDF文脈=BLE_USED_IN_IDF=1で空）ではなくble_store_config
    #  （BLE_STORE_CONFIG_PERSIST=0＝RAM保持，NVS不使用）を使う（S3 §5.2の
    #  真因対策，C3/C5と同じ判断）．OFFに戻せばD-2c（GATTディスカバリ・
    #  自前サービス）までの構成へ完全復帰（可逆）．C6はC3/C5とは別世代の
    #  コントローラ（blob）だが，SM/tinycrypt/ble_store_configはいずれも
    #  NimBLEホスト側（チップ非依存）の機能のため，C3の「2個目暗号化ACL
    #  不達」の壁もC5同様に非該当の公算．
    option(ESP32C6_BT_SM "Enable NimBLE SMP pairing/bonding on C6 (Phase D-2d, tinycrypt)" ON)

    #  ---- コンパイル定義 ----
    #  ESP_PLATFORM／CONFIG_BT_CONTROLLER_ENABLEDはD-1で既に定義済み．
    #  MYNEWT_VAL_BLE_SM_LEGACY/SCはble_sm*.cをnear-empty化しmbedTLS/
    #  tinycryptのリンクを回避する（C3のD-2a節と同じ判断．sync到達に
    #  暗号は不要）．bt_nimble_config.h（本ディレクトリのC6専用版．
    #  C3版を流用しない＝ファイル冒頭コメント参照）で CONFIG_BT_NIMBLE_*
    #  一式を供給する．
    #  ★nimble_port.cの上流ドリフト（順序バグ）：`#if (BT_HCI_LOG_INCLUDED
    #  == TRUE)`（41〜50行目付近）を，その定義元`bt_common.h`のinclude
    #  （50行目）より前に評価する．TRUE／FALSEはstdbool.hのtrue/falseでは
    #  なく，bt_common.h自身が`#define TRUE true`／`#define FALSE false`
    #  として定義するESP-IDF独自マクロ（大文字）——つまりTRUEも
    #  BT_HCI_LOG_INCLUDEDも，最小includeチェーンではこの#ifに到達する
    #  時点で共に未定義（0として評価）のため`0==0`＝真となり，存在しない
    #  hci_log/bt_hci_log.hをincludeしようとしてfatal errorになる
    #  （実機ビルドで判明．stdbool.hを強制includeしてもtrue/falseが
    #  増えるだけでTRUE/FALSEは変わらないため無効——一度試して確認済み）．
    #  TRUE=1を明示的に-Dし，BT_HCI_LOG_INCLUDED=0と組み合わせて
    #  `0==1`＝偽へ確実に倒す（後段でbt_common.hが両方とも同じ値へ
    #  再定義するため以降は無矛盾．再定義警告のみ＝他の-D/-include群と
    #  同種の無害な警告）．
    list(APPEND ASP3_COMPILE_DEFS
        TRUE=1
        BT_HCI_LOG_INCLUDED=0
    )

    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_BT_NIMBLE)
    if(ESP32C6_BT_SM)
        #  D-2d：SMP有効．app側（ble_host_smoke_c6.c）のSM設定・store初期化を
        #  有効化する識別子．MYNEWT_VAL_BLE_SM_LEGACY/SCは-Dで上書きしない
        #  （bt_nimble_config.hのCONFIG_BT_NIMBLE_SM_LEGACY/SC定義が#ifdefで
        #  拾われ自動的に1になる．esp_nimble_cfg.hの実装＝定義の「値」でなく
        #  「定義の有無」で1/0を決めるため）．
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_BT_SM)
    else()
        #  D-2cまで：SECURITY off．NIMBLE_BLE_SM=SM_LEGACY||SM_SCを0に落とし
        #  （nimble_opt_auto.h），ble_sm*.cをnear-empty化してtinycrypt/mbedTLS
        #  リンクを回避する．
        list(APPEND ASP3_COMPILE_DEFS
            MYNEWT_VAL_BLE_SM_LEGACY=0
            MYNEWT_VAL_BLE_SM_SC=0
        )
    endif()

    #  sdkconfig.h（CONFIG_*）・bt_nimble_config.h（CONFIG_BT_NIMBLE_*）・
    #  syscfg/syscfg.h（MYNEWT_VAL）の順で強制include．D-1の
    #  -include soc/soc_caps.h／esp_attr.h／esp_idf_version.h／
    #  sys/param.h と衝突しないようSHELL:接頭辞を使う（C3の罠と同じ）．
    list(APPEND ASP3_COMPILE_OPTIONS
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sdkconfig.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt_nimble_config.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include syscfg/syscfg.h>"
    )

    #  ---- インクルードパス ----
    #  ★porting/nimble/include・porting/npl/freertos/include・
    #  host/nimble/port/includeはD-1のインクルードリストに既に含まれて
    #  いる（本節より前にAPPEND済み＝-Iの並びで先に来るため優先解決
    #  される）．重複追加すると"どちらの版が先に見つかるか"という
    #  暗黙の前提を増やすだけなので，ここでは追加しない
    #  （★npl_freertos.h／nimble_npl_os.h／nimble_port_freertos.hは
    #  upstreamツリー側にも同名ファイルが存在するが内容が異なる
    #  ——D-1のporting/npl/freertos/{include,src}が対のペアで正しい．
    #  D-1のディレクトリが先に来る現在の順序を変えないこと）．
    list(APPEND ASP3_INCLUDE_DIRS
        ${NIMBLE_ROOT}/host/include
        ${NIMBLE_ROOT}/include
        ${NIMBLE_ROOT}/transport/include
        ${NIMBLE_ROOT}/host/services/gap/include
        ${NIMBLE_ROOT}/host/services/gatt/include
        ${NIMBLE_ROOT}/host/util/include
        ${NIMBLE_ROOT}/host/store/ram/include
    )
    if(ESP32C6_BT_SM)
        #  D-2d：ble_store_configとtinycrypt（SCのuECC P-256＋AES-CMAC）
        list(APPEND ASP3_INCLUDE_DIRS
            ${NIMBLE_ROOT}/host/store/config/include
            ${TINYCRYPT_ROOT}/include
        )
    endif()

    #  ---- ソースファイル ----
    #  D-1で既に npl_os_freertos.c／os_msys_init.c／bt_osi_mem.c／
    #  hci_transport.c はリンク済み（新npl経路．上のコメント参照）．
    #  ★hci_driver_standard.cはNimBLE ON時は使わない
    #  （hci_driver_vhci_opsの多重定義を避ける．D-1専用のbt_smoke_c6は
    #  従来どおりhci_driver_standard.cを使う＝上のD-1ソース節は不変）．
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${BT_ROOT}/porting/transport/driver/vhci/hci_driver_nimble.c
        ${NIMBLE_ROOT}/transport/esp_ipc/src/hci_esp_ipc.c
        ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c
        ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c
        #  ホストスタック本体（C3のesp32c3_espidf/esp_bt.cmakeと同一
        #  トリム済み集合．ble_svc_gap/gatt のみ採用・他サービス
        #  （ans/bas/dis/hr/htp/ias/ipss/lls/prox/cts/tps/hid/sps/cte/
        #  ras）・ble_store_config/nvs（永続ボンディング）・ble_cs／
        #  ble_ead／ble_aes_ccm／ble_gattc_cache*／ble_eatt（新機能，
        #  sync/adv到達には不要）は不採用）
        ${NIMBLE_ROOT}/transport/src/transport.c
        ${NIMBLE_ROOT}/host/util/src/addr.c
        ${NIMBLE_ROOT}/host/services/gap/src/ble_svc_gap.c
        ${NIMBLE_ROOT}/host/services/gatt/src/ble_svc_gatt.c
        ${NIMBLE_ROOT}/host/src/ble_att.c
        ${NIMBLE_ROOT}/host/src/ble_att_clt.c
        ${NIMBLE_ROOT}/host/src/ble_att_cmd.c
        ${NIMBLE_ROOT}/host/src/ble_att_svr.c
        ${NIMBLE_ROOT}/host/src/ble_eddystone.c
        ${NIMBLE_ROOT}/host/src/ble_gap.c
        ${NIMBLE_ROOT}/host/src/ble_gattc.c
        ${NIMBLE_ROOT}/host/src/ble_gatts.c
        ${NIMBLE_ROOT}/host/src/ble_gatts_lcl.c
        ${NIMBLE_ROOT}/host/src/ble_hs.c
        ${NIMBLE_ROOT}/host/src/ble_hs_adv.c
        ${NIMBLE_ROOT}/host/src/ble_hs_atomic.c
        ${NIMBLE_ROOT}/host/src/ble_hs_cfg.c
        ${NIMBLE_ROOT}/host/src/ble_hs_conn.c
        ${NIMBLE_ROOT}/host/src/ble_hs_flow.c
        ${NIMBLE_ROOT}/host/src/ble_hs_hci.c
        ${NIMBLE_ROOT}/host/src/ble_hs_hci_cmd.c
        ${NIMBLE_ROOT}/host/src/ble_hs_hci_evt.c
        ${NIMBLE_ROOT}/host/src/ble_hs_hci_util.c
        ${NIMBLE_ROOT}/host/src/ble_hs_id.c
        ${NIMBLE_ROOT}/host/src/ble_hs_log.c
        ${NIMBLE_ROOT}/host/src/ble_hs_mbuf.c
        ${NIMBLE_ROOT}/host/src/ble_hs_misc.c
        ${NIMBLE_ROOT}/host/src/ble_hs_mqueue.c
        ${NIMBLE_ROOT}/host/src/ble_hs_periodic_sync.c
        ${NIMBLE_ROOT}/host/src/ble_hs_pvcy.c
        ${NIMBLE_ROOT}/host/src/ble_hs_resolv.c
        ${NIMBLE_ROOT}/host/src/ble_hs_shutdown.c
        ${NIMBLE_ROOT}/host/src/ble_hs_startup.c
        ${NIMBLE_ROOT}/host/src/ble_hs_stop.c
        ${NIMBLE_ROOT}/host/src/ble_ibeacon.c
        ${NIMBLE_ROOT}/host/src/ble_l2cap.c
        ${NIMBLE_ROOT}/host/src/ble_l2cap_coc.c
        ${NIMBLE_ROOT}/host/src/ble_l2cap_sig.c
        ${NIMBLE_ROOT}/host/src/ble_l2cap_sig_cmd.c
        ${NIMBLE_ROOT}/host/src/ble_sm.c
        ${NIMBLE_ROOT}/host/src/ble_sm_alg.c
        ${NIMBLE_ROOT}/host/src/ble_sm_cmd.c
        ${NIMBLE_ROOT}/host/src/ble_sm_lgcy.c
        ${NIMBLE_ROOT}/host/src/ble_sm_sc.c
        ${NIMBLE_ROOT}/host/src/ble_store.c
        ${NIMBLE_ROOT}/host/src/ble_store_util.c
        ${NIMBLE_ROOT}/host/src/ble_uuid.c
    )
    if(ESP32C6_BT_SM)
        #  D-2d：bond storeはble_store_config（PERSIST=0＝RAM，NVS不使用．
        #  ble_store_ram.cはIDF文脈で空＝S3 §5.2の真因）＋tinycrypt必要5ソース
        #  （ble_sm_alg.cのtc_aes*/tc_cmac_*/uECC_*参照に対応）．
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES
            ${NIMBLE_ROOT}/host/store/config/src/ble_store_config.c
            ${TINYCRYPT_ROOT}/src/aes_encrypt.c
            ${TINYCRYPT_ROOT}/src/cmac_mode.c
            ${TINYCRYPT_ROOT}/src/ecc.c
            ${TINYCRYPT_ROOT}/src/ecc_dh.c
            ${TINYCRYPT_ROOT}/src/utils.c
        )
    else()
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES
            ${NIMBLE_ROOT}/host/store/ram/src/ble_store_ram.c
        )
    endif()

endif()  # ESP32C6_BT_NIMBLE

endif()  # ESP32C6_BT_IDF61 (hal 版 else ブロック終端)

endif()  # ESP32C6_BT
