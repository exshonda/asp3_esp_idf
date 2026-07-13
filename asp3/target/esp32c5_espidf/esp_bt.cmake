#
#               ESP32-C5 Bluetooth（BLE）統合 Phase D-1／BLE実施03
#
#  コントローラ起動＋VHCIループバック（ホストスタック無し）．設計は
#  docs/ble-c5c6-plan.md，実装ログはdocs/ble-c5c6.md「BLE実施03」．
#
#  ★ライブラリ世代の選定（本ラウンドの中心判断）：C6のBLE実施01は
#  hal submodule（esp-hal-3rdparty）のcontroller/esp32c6/bt.c＋
#  libble_app.aを採用したが，本ファイル（C5）は**ESP-IDF v6.1
#  （~/tools/esp-idf-v6.1）からbt.c／ble.c／libble_app.a／esp_phy／
#  esp_coexを採用する**——esp_wifi.cmakeが確立した「hal世代
#  （v8/os_adapter 0x08）のlibphy.aはeco2シリコンのPHY RX較正で
#  収束せずハングする（実施09）．IDF v6.1（v9/os_adapter 0x09）の
#  libphy.aは収束する（実施10）」という実機確定事実がBTにもそのまま
#  適用されるため（BTコントローラもesp_bt_controller_enable()内で
#  esp_phy_enable(PHY_MODEM_BT)を呼び，WiFiと全く同じlibphy.a／
#  register_chipv7_phy経路を通る）．hal世代のBT blobとv6.1世代の
#  PHY blobを手で混ぜる「ハイブリッド」構成は，Espressifが実際には
#  検証していないblob-ABI境界を新規に作ることになるため採らない
#  （advisorレビュー指摘．詳細はbt/bt_shim.c冒頭コメント）．
#
#  副次的な発見：IDF v6.1のcontroller/esp32c5/bt.cはhal submodule版
#  （platform/os.hのesp_os_*経由）と異なり，**C3の旧世代bt.cと同じ
#  プログラミングモデル**（FreeRTOS API＝xTaskCreatePinnedToCore／
#  vTaskDeleteを直接呼び，割込みは標準esp_intr_alloc/esp_intr_free
#  APIを直接呼ぶ）を採用している．そのためbt/stub/includeは
#  C6版のような専用platform/os.hを新設せず，**C3のbt/stub/include
#  一式（freertos/*.h＋esp_partition.h）をそのまま再利用する**
#  （下記1節）．npl_os_*→npl_freertos_*橋渡しシム・nimble_port_os.h
#  リダイレクトシム（C6のBLE実施01で必要だった上流ドリフト吸収）は
#  v6.1のソースツリー自体に当該ドリフトが存在しないため不要．
#
#  RAM予算のためESP32C5_WIFIとの同時ONは現時点で未対応
#  （FATAL_ERRORはtarget.cmake側）．
#

if(ESP32C5_BT)

set(BT_TARGETDIR ${TARGETDIR}/bt)
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

#
#  esp_wifi.cmakeと同一のIDF v6.1パス（実施10で確立．eco2 C5対応の
#  matched set）．BTもここからbt/phy/coexを採る．
#
set(IDF /home/honda/tools/esp-idf-v6.1)
set(BT_CHIP_SERIES esp32c5)

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C5_BT
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C5=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  D-1＝controller-onlyスモークテスト（NimBLEホスト無し）
    CONFIG_BT_CONTROLLER_ONLY=1
    #  VHCI（HCI_TRANSPORT_VHCI経由．esp_vhci_host_*公開API）を選択
    CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=1
    #  msys（HCI ACL用共有mbufプール）の初期化をコントローラ内部へ委譲
    #  （C6のBLE実施01と同じ判断．esp32c5 Kconfig既定値もy）
    CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1
    CONFIG_BT_LE_MSYS_1_BLOCK_COUNT=12
    CONFIG_BT_LE_MSYS_1_BLOCK_SIZE=256
    CONFIG_BT_LE_MSYS_2_BLOCK_COUNT=24
    CONFIG_BT_LE_MSYS_2_BLOCK_SIZE=320
    CONFIG_BT_LE_MSYS_BUF_FROM_HEAP=1
    #  npl_os_freertos.cのcallout実装にesp_timer_*（bt/bt_shim.c提供）を
    #  選択し，FreeRTOSソフトタイマ経路を避ける（C6と同じ判断）
    CONFIG_BT_LE_USE_ESP_TIMER=1
    #  esp_bt_cfg.hが無条件参照する項目（esp32c5 Kconfig.inの既定値）
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
    #  CONFIG_XTAL_FREQ／CPUクロックはesp_wifi.cmakeと同じ理由で
    #  hal/nuttx/esp32c5/include/sdkconfig.hに実体が無い（C5にはNuttX
    #  ポートのsdkconfig.h一式が存在しない．esp_wifi.cmakeのCONFIG_
    #  IDF_TARGET_ESP32C5コメント参照）ため，本ブロックで明示する．
    #  ★実機ビルドで判明：esp_bt.h（IDF v6.1）のBT_CONTROLLER_INIT_
    #  CONFIG_DEFAULT()マクロがCONFIG_XTAL_FREQ／CONFIG_ESP_DEFAULT_
    #  CPU_FREQ_MHZを直接参照する（C6はhal/nuttx/esp32c6/include/
    #  sdkconfig.hの間接定義に頼れたが，C5には同ファイルが無いため
    #  ここで明示的に与える必要がある——C6のesp_bt.cmakeコメントが
    #  警告した「後勝ちで無効化される」罠はC5では発生しない＝
    #  素直に-Dで定義してよい）．値は実施32/34確定のC5実測値
    #  （48MHz XTAL直結，BBPLL経由240MHz．asp3/arch/riscv_gcc/esp32c5/
    #  esp32c5.hのCORE_CLK_MHZ=240と一致させる）．
    CONFIG_XTAL_FREQ=48
    CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240
    CONFIG_ESPRESSIF_CPU_FREQ_MHZ=240
    #  esp_ipc.hのesp_ipc_func_t等はCONFIG_ESP_IPC_ENABLE無しでは
    #  丸ごと非公開になる
    CONFIG_ESP_IPC_ENABLE
    #  PLL温度追従は較正データ永続化前提．本ビルドは毎回フル較正の
    #  ため無効化で十分（esp_wifi.cmakeと同じ理由）
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    #  MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL：phy_init.cが直値のビット
    #  マスクを期待するため（esp_wifi.cmakeと同じ理由）
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  C6のBLE実施01で確立した4つの強制includeをそのまま踏襲する（理由は
#  各行コメント参照．v6.1のnpl_os_freertos.c／os_mbuf.h／os_mempool.h
#  もhal版と同じ事情——1269行中の大半が無変更移植のため，同じ罠を
#  同じ場所で踏む．実機ビルドで実際に必要か確認しながら調整する）．
#
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
    #  bt/porting/mem/os_mempool.c（BIT()マクロ．実機ビルドで判明．
    #  実ESP-IDFではビルドシステムがesp_bit_defs.hを暗黙includeする
    #  前提のヘッダで，自ファイルではincludeしない．他のBTソース
    #  （bt.c等）は別経路で既にesp_bit_defs.hが見えているため影響なし）
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_bit_defs.h>"
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#  ★C3のbt/stub/include（freertos/*.h＋esp_partition.h）をそのまま
#  再利用する．C5独自のbt/stub/includeは作らない（本ファイル冒頭の
#  「副次的な発見」参照．v6.1のbt.cはC3と同じプログラミングモデル）．
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
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c5/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c5/private_include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/include
    ${ESP_HAL_DIR}/components/esp_system/include
    #  esp_private/wifi.h（phy_init.cが要求．BTもWi-Fiと同じPHY実
    #  ソースを使うため必要．esp_wifi.cmakeはIDF側を使うが，esp_private/
    #  wifi.hはESP-IDF公開APIヘッダのためIDF/hal双方に存在．IDFを優先）
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
    #  hal/pmu_ll.h・hal/pmu_hal.h（wifi/esp_shim.cのesp_shim_modem_icg_init
    #  が要求．esp_wifi.cmakeにも同じ2行がある＝WiFi/BT共有ファイルの
    #  依存としてBT側でも要る）
    ${ESP_HAL_DIR}/components/esp_hal_pmu/include
    ${ESP_HAL_DIR}/components/esp_hal_pmu/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/private_include
    #  hal/rtc_timer_hal.h（rtc_time.cが要求）
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/include
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/${BT_CHIP_SERIES}/include
    #  hal/timg_ll.h（rtc_time.cが要求）
    ${ESP_HAL_DIR}/components/esp_hal_timg/include
    ${ESP_HAL_DIR}/components/esp_hal_timg/${BT_CHIP_SERIES}/include
    #  esp_wifi.cmake §1bと同じ理由で必要になったパス
    #  （modem/i2c_ana_mst_reg.h・regi2c_impl.h等，IDFのsocレイアウト
    #  でのみ解決されるヘッダ．hal socより後に置く）
    ${IDF}/components/esp_phy/esp32c5/include
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
    #  ★C6のBLE実施01の教訓（「os_mbuf.c/os_mempool.cは自前で持たない
    #  ＝libble_app.aが自分のos_mbuf.c.oを同梱」）はC5では成立しない
    #  ことが実機リンクで判明：C5のv6.1 blob（libble_app.a）はos_
    #  memblock_get/put/from・os_mempool_init/clear/unregister/
    #  flags_set等が未解決のまま（plain名で参照）で残り，リンクエラーに
    #  なった．IDF v6.1のos_mempool.cを自前でリンクして解決する
    #  （os_mbuf.cはosi_coex経由では未参照のため引き続き含めない．
    #  多重定義が出れば除去する）．
    ${IDF}/components/bt/porting/mem/os_mempool.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c5/esp_clk_tree.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_HAL_DIR}/components/esp_hal_clock/esp32c5/clk_tree_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c5/rtc_time.c
    ${IDF}/components/bt/porting/transport/src/hci_transport.c
    ${IDF}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    ${BT_TARGETDIR}/bt_shim.c
    #  PHY／クロック／ペリフェラルの実ソース（esp_wifi.cmakeと同じIDF
    #  v6.1版．eco2対応のmatched set．BTもWiFiと同じ無線ハードウェアを
    #  使うため必要）
    ${IDF}/components/esp_phy/src/phy_init.c
    ${IDF}/components/esp_phy/src/phy_common.c
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${IDF}/components/esp_phy/src/lib_printf.c
    #  btbb_init.c（esp_btbb_enable/disable．bt.cが呼ぶ．C6のBLE実施01で
    #  必要と判明．esp_phy/lib/${BT_CHIP_SERIES}にlibbtbb.aが同居）
    ${IDF}/components/esp_phy/src/btbb_init.c
    #  modem_clock.c／modem_clock_hal.cはWiFi非同時ONの制約があるため
    #  BT単体ビルドでも自前で持つ必要がある（esp_wifi.cmakeのif
    #  (ESP32C5_WIFI)ブロック内にありBTからは見えないため．chip依存の
    #  実ソースはhal側を使う——PHY/coexとは異なりmodem_clockはeco2非互換
    #  の対象外＝hal版で問題ない．esp_wifi.cmake §6のソース一覧と同一）
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
#  3. リンクライブラリパス・ライブラリ（IDF v6.1．esp_wifi.cmakeと
#     同じmatched set＝eco2対応版）
#  ------------------------------------------------------------------
#
list(APPEND ASP3_LINK_OPTIONS
    -L${IDF}/components/bt/controller/lib_${BT_CHIP_SERIES}/${BT_CHIP_SERIES}-bt-lib
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
#  4. ROM関数ld（IDF v6.1のesp32c5 ldセット．esp_wifi.cmakeと同じ
#     理由でeco3.ldは除外——同一のRAM版PHY関数上書き問題がBT経路にも
#     及ぶ可能性がある．net80211/pp（WiFi専用ライブラリのROM解決）は
#     BTはリンクしないため除外．systimer.ldはIDF v6.1に存在しない
#     （esp_wifi.cmake【実施10】コメント参照）．
#  ------------------------------------------------------------------
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

endif()
