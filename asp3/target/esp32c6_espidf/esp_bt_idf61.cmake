#
#      ESP32-C6 Bluetooth（BLE）統合  IDF v6.1 matched-set 版（D-1のみ）
#      ── BLE実施13（docs/ble-c5c6-plan.md §13）
#
#  ESP32C6_BT_IDF61=ON のときに esp_bt.cmake から include される．
#  hal submodule 版（既定・esp_bt.cmake 本体）とトグルで排他選択する．
#
#  ★狙い：C6 BLE の PHY-init `register_chipv7_phy` RFシンセ非ロック
#  （§11/§12 で hal libphy の BT依存サブパス内部と確定，D-1未達）を，
#  bt/phy/coex/libble_app.a を **IDF v6.1（~/tools/esp-idf-v6.1）の
#  matched set** へ swap して解けるか実機判定する（C5 esp_bt.cmake の
#  IDF v6.1 構成の C6 転写）．§12 の申し送りどおり「効く保証は無い」
#  （C5実施09は WiFi も非収束→libphy版数不適合が真因だったが，C6 は
#  WiFi が同一 hal libphy で収束＝BT経路固有）．
#
#  ★v6.1 の controller/esp32c6/bt.c は **C3型プログラミングモデル**
#  （FreeRTOS API＋標準 esp_intr_alloc を直接呼ぶ．esp_os_*／npl_os_*
#  ドリフト無し＝実測確認済み）のため，本構成は hal 版ではなく
#  **C5 esp_bt.cmake（IDF v6.1）を範とする**：
#    - bt/stub は C3 の freertos stub を再利用（platform/os.h stub 不使用）．
#    - シムは bt/bt_shim_idf61.c（標準名 esp_intr_alloc/free＋§11クロック）．
#    - os_mempool.c を自前リンク（v6.1 blob が plain名で要求．hal は不要
#      だった＝C5 BLE実施03の教訓）．
#
#  ★D-1（controller-only＝bt_smoke_c6）限定．NimBLE ホスト（D-2*）は
#  本 swap では移植しない——`register_chipv7_phy` は
#  esp_bt_controller_enable→esp_phy_enable 内で発火し bt_smoke_c6 が
#  直接踏む（§11で「bt_smoke_c6も同一ハング」確認済み）ため，シンセ・
#  ロックの可否判定にホストスタックは不要（advisor指摘）．NimBLE を要する
#  アプリを IDF61 と組み合わせた場合は FATAL_ERROR で明示的に弾く．
#
#  ★§11 のクロック2修正（regi2c sel_160m＋WIFIPWR）＋実施91 ICG は
#  bt_shim_idf61.c の esp_shim_bt_clock_init() に維持（前提）．
#

set(BT_TARGETDIR ${TARGETDIR}/bt)
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

#  esp_wifi.cmake と同一の IDF v6.1 パス（eco/matched set）．
set(IDF /home/honda/tools/esp-idf-v6.1)
set(BT_CHIP_SERIES esp32c6)

if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c6")
    message(FATAL_ERROR
        "ESP32C6_BT_IDF61 は D-1（bt_smoke_c6）限定です．NimBLE ホスト "
        "（ble_host_smoke_c6）は本 IDF v6.1 swap では未対応——RFシンセ・"
        "ロック判定にホストは不要（docs/ble-c5c6-plan.md §13）．NimBLE を"
        "使う場合は既定の hal 版（ESP32C6_BT_IDF61=OFF）を使うこと．")
endif()

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C6_BT
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C6=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  D-1（controller-only）限定．CONTROLLER_ONLY と NIMBLE_ENABLED は
    #  実ESP-IDF Kconfig で排他．本 IDF61 構成は NimBLE を持たないため常に立てる．
    CONFIG_BT_CONTROLLER_ONLY=1
    #  VHCI（esp_vhci_host_* 公開API）
    CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=1
    #  msys 初期化をコントローラ内部へ委譲（esp32c6 Kconfig 既定 y）
    CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1
    CONFIG_BT_LE_MSYS_1_BLOCK_COUNT=12
    CONFIG_BT_LE_MSYS_1_BLOCK_SIZE=256
    CONFIG_BT_LE_MSYS_2_BLOCK_COUNT=24
    CONFIG_BT_LE_MSYS_2_BLOCK_SIZE=320
    CONFIG_BT_LE_MSYS_BUF_FROM_HEAP=1
    #  callout に esp_timer_*（bt_shim_idf61.c 提供）を選択
    CONFIG_BT_LE_USE_ESP_TIMER=1
    #  esp_bt_cfg.h が無条件参照する項目（esp32c6 Kconfig.in 既定値）
    CONFIG_BT_LE_COEX_PHY_CODED_TX_RX_TLIM_EFF=0
    CONFIG_BT_LE_DFT_TX_POWER_LEVEL_DBM_EFF=0
    CONFIG_BT_LE_DFT_ADV_SCHED_PRIO_LEVEL=0
    CONFIG_BT_LE_DFT_PERIODIC_ADV_SCHED_PRIO_LEVEL=1
    CONFIG_BT_LE_DFT_SYNC_SCHED_PRIO_LEVEL=1
    CONFIG_BT_LE_LL_RESOLV_LIST_SIZE=4
    CONFIG_BT_LE_LL_DUP_SCAN_LIST_COUNT=20
    CONFIG_BT_LE_LL_SCA=60
    CONFIG_BT_LE_CONTROLLER_TASK_STACK_SIZE=4096
    CONFIG_BT_LE_EXT_ADV_RESERVED_MEMORY_COUNT=2
    CONFIG_BT_LE_CONN_RESERVED_MEMORY_COUNT=2
    #  ★XTAL/CPU クロック：C6 は hal/nuttx/esp32c6/include/sdkconfig.h
    #  （カーネル共通で常時include済み）が CONFIG_XTAL_FREQ=40 と
    #  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ→CONFIG_ESPRESSIF_CPU_FREQ_MHZ の
    #  間接マクロを定義する．esp_bt.h（v6.1 esp32c6）の
    #  BT_CONTROLLER_INIT_CONFIG_DEFAULT() は CONFIG_XTAL_FREQ／
    #  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ を直接参照するため，実体の
    #  CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160 のみ -D する（C5 のように XTAL を
    #  -D すると sdkconfig.h の後勝ち redefine と競合するため定義しない．
    #  hal 版 esp_bt.cmake と同じ判断）．
    CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160
    CONFIG_ESP_IPC_ENABLE
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  強制include（C5 BLE実施03 と同一．v6.1 のソースが暗黙include前提と
#  する 5ヘッダ．hal 版の esp_timer.h／npl_os_bridge.h は不要＝v6.1 は
#  esp_timer.h を自ファイルで include し，npl_os_* ドリフトも無い．
#  代わりに os_mempool.c の BIT() マクロ用に esp_bit_defs.h を足す）．
#
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_bit_defs.h>"
    #  ★v6.1 npl_os_freertos.c は esp_timer_is_active／esp_timer_get_expiry_time
    #  を esp_timer.h を include せずに呼ぶ（上流ドリフト）が，本ビルドの
    #  esp_timer.h は hal_stub 版に解決され同2関数の宣言を持たない．
    #  bt/bt_esp_timer_ext.h（stub esp_timer.h を include し2宣言を補う）を
    #  force-include して GCC14.2 の暗黙宣言 hard error を回避する．
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt/bt_esp_timer_ext.h>"
    #  ★v6.1 phy_init.c（phy_enter_critical）は xPortInIsrContext() を呼ぶが
    #  freertos/task.h を include しない（freertos/FreeRTOS.h＋portmacro.h の
    #  みで xPortInIsrContext は task.h 側にある）．GCC14.2 は暗黙宣言を
    #  hard error にするため，C3 stub の freertos/task.h（xPortInIsrContext
    #  ＝sns_ctx() の static inline．自己完結＝先頭で FreeRTOS.h を include）を
    #  強制includeして可視化する．他の -include 群と同じ«暗黙include前提
    #  ヘッダの補完»．
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include freertos/task.h>"
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#  ★C3 の bt/stub/include（freertos/*.h＋esp_partition.h）を再利用する
#  （v6.1 の bt.c は C3 と同じプログラミングモデル）．
#
list(APPEND ASP3_INCLUDE_DIRS
    ${C3_TARGETDIR}/bt/stub/include
    ${TARGETDIR}/wifi
    ${IDF}/components/bt/include/${BT_CHIP_SERIES}/include
    ${IDF}/components/bt/common/include
    ${IDF}/components/bt/common/ble_log/include
    ${IDF}/components/bt/porting/include
    ${IDF}/components/bt/porting/include/os
    ${IDF}/components/bt/porting/npl/freertos/include
    ${IDF}/components/bt/porting/transport/include
    ${IDF}/components/bt/controller/${BT_CHIP_SERIES}
    ${IDF}/components/bt/host/nimble/port/include
    ${IDF}/components/bt/host/nimble/nimble/porting/nimble/include
    ${ESP_HAL_DIR}/components/esp_hw_support/include
    ${ESP_HAL_DIR}/components/esp_hw_support/include/soc
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/private_include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/include
    ${ESP_HAL_DIR}/components/esp_system/include
    #  esp_private/wifi.h（phy_init.c．BT も WiFi と同じ PHY 実ソース）
    ${IDF}/components/esp_wifi/include
    ${IDF}/components/esp_phy/include
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_pm/include
    ${ESP_HAL_DIR}/components/esp_timer/include
    ${IDF}/components/esp_coex/include
    ${ESP_HAL_DIR}/components/esp_rom/include
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include/${BT_CHIP_SERIES}
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}
    ${ESP_HAL_DIR}/components/heap/include
    ${ESP_HAL_DIR}/components/log/include
    ${ESP_HAL_DIR}/components/riscv/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_hal_clock/include
    ${ESP_HAL_DIR}/components/esp_hal_clock/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/efuse/include
    ${ESP_HAL_DIR}/components/efuse/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_event/include
    #  hal/pmu_ll.h・hal/pmu_hal.h（wifi/esp_shim.c の esp_shim_modem_icg_init）
    ${ESP_HAL_DIR}/components/esp_hal_pmu/include
    ${ESP_HAL_DIR}/components/esp_hal_pmu/${BT_CHIP_SERIES}/include
    #  hal/rtc_timer_hal.h（rtc_time.c）
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/include
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/${BT_CHIP_SERIES}/include
    #  hal/timg_ll.h（rtc_time.c）
    ${ESP_HAL_DIR}/components/esp_hal_timg/include
    ${ESP_HAL_DIR}/components/esp_hal_timg/${BT_CHIP_SERIES}/include
    #  modem/i2c_ana_mst_reg.h・regi2c_impl.h 等（IDF socレイアウトでのみ
    #  解決．esp_wifi.cmake §1b／C5 esp_bt.cmake と同じ理由．hal soc より後）
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/include
)

#
#  ------------------------------------------------------------------
#  2. ソースファイル（D-1最小集合＝controller-only＋VHCI）
#  ------------------------------------------------------------------
#
list(APPEND ASP3_CFG_FILES ${BT_TARGETDIR}/bt.cfg)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${IDF}/components/bt/controller/${BT_CHIP_SERIES}/bt.c
    ${IDF}/components/bt/controller/${BT_CHIP_SERIES}/ble.c
    ${IDF}/components/bt/porting/npl/freertos/src/npl_os_freertos.c
    ${IDF}/components/bt/porting/mem/os_msys_init.c
    ${IDF}/components/bt/porting/mem/bt_osi_mem.c
    #  ★os_mempool.c を自前リンク（v6.1 blob が os_memblock_*／
    #  os_mempool_* を plain名で未解決参照＝C5 BLE実施03の教訓．hal C6 は
    #  blob 同梱で不要だったが v6.1 blob は異なる．os_mbuf.c は未参照の
    #  ため引き続き含めない＝多重定義が出れば除去する）．
    ${IDF}/components/bt/porting/mem/os_mempool.c
    #  PHY／クロック／ペリフェラルの実ソース（bt/phy/coex は IDF v6.1，
    #  hw_support/clock/rtc/efuse/periph/modem_clock はチップ依存で hal 版．
    #  C5 esp_bt.cmake と同じ origin-split）
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/esp_clk_tree.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_HAL_DIR}/components/esp_hal_clock/${BT_CHIP_SERIES}/clk_tree_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_time.c
    ${IDF}/components/bt/porting/transport/src/hci_transport.c
    #  D-1＝controller-only＝hci_driver_standard.c（NimBLE 無し）
    ${IDF}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    ${BT_TARGETDIR}/bt_shim_idf61.c
    ${IDF}/components/esp_phy/src/phy_init.c
    ${IDF}/components/esp_phy/src/phy_common.c
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${IDF}/components/esp_phy/src/lib_printf.c
    ${IDF}/components/esp_phy/src/btbb_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/modem_clock.c
    ${ESP_HAL_DIR}/components/hal/${BT_CHIP_SERIES}/modem_clock_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_HAL_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    ${ESP_HAL_DIR}/components/hal/efuse_hal.c
    ${ESP_HAL_DIR}/components/hal/${BT_CHIP_SERIES}/efuse_hal.c
)

#
#  ------------------------------------------------------------------
#  3. リンクライブラリパス・ライブラリ（IDF v6.1 matched set）
#  ------------------------------------------------------------------
#  ★C6 の bt-lib は esp32c6-bt-lib/esp32c6/（無印 esp32c6．esp32c61 は
#  別チップ）．C5（esp32c5-bt-lib 直下）とはサブディレクトリ構造が異なる．
#
list(APPEND ASP3_LINK_OPTIONS
    -L${IDF}/components/bt/controller/lib_${BT_CHIP_SERIES}/${BT_CHIP_SERIES}-bt-lib/${BT_CHIP_SERIES}
    -L${IDF}/components/esp_phy/lib/${BT_CHIP_SERIES}
    -L${IDF}/components/esp_coex/lib/${BT_CHIP_SERIES}
)
list(APPEND ASP3_LINK_LIBS
    ble_app
    phy
    btbb
    coexist
)

#
#  ------------------------------------------------------------------
#  4. ROM関数ld（IDF v6.1 の esp32c6 ldセット）
#  ------------------------------------------------------------------
#  esp_wifi.cmake と同じ理由で eco3.ld は除外，net80211/pp（WiFi専用）は
#  除外．systimer.ld は IDF v6.1 に存在しない（C5 esp_bt.cmake と同じ）．
#  coexist.ld は IDF v6.1 esp32c6 に存在するため追加する．
#
set(BT_ROM_LD_DIR ${IDF}/components/esp_rom/${BT_CHIP_SERIES}/ld)
set(ESP_BT_ROM_LD_FILES
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.api.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libgcc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.newlib.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc-suboptimal_for_misaligned_mem.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.version.ld
    ${IDF}/components/riscv/ld/rom.api.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.phy.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.coexist.ld
)
foreach(_esp_bt_rom_ld ${ESP_BT_ROM_LD_FILES})
    list(APPEND ASP3_LINK_OPTIONS -Wl,-T,${_esp_bt_rom_ld})
endforeach()
