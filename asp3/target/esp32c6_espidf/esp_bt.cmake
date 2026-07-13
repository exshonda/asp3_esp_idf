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

set(BT_TARGETDIR ${TARGETDIR}/bt)
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

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
    #  D-1＝controller-onlyスモークテスト（NimBLEホスト無し）．
    CONFIG_BT_CONTROLLER_ONLY=1
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
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
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
    ${ESP_HAL_DIR}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    ${BT_TARGETDIR}/bt_shim.c
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

endif()
