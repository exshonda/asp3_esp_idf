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

#
#  ESP32C5_BT_NIMBLEの判定を先出しする（下の「2. ソースファイル」節で
#  hci_driver_standard.c／hci_driver_nimble.cの二者択一・CONFIG_BT_
#  CONTROLLER_ONLYのD-1限定化に使うため．ブロック本体＝NimBLEホストの
#  ソース/インクルード追加は末尾のD-2a節（BLE実施05）で行う）．
#  RAM予算のため既定はOFF．NimBLEを要するアプリ（ble_host_smoke_c5）で
#  自動でON．D-1のbt_smoke_c5は痩せたまま保つ．
#
option(ESP32C5_BT_NIMBLE "Enable NimBLE host stack on top of BT controller (Phase D-2a/BLE実施05)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c5")
    set(ESP32C5_BT_NIMBLE ON)
endif()

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C5_BT
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C5=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  CONFIG_BT_CONTROLLER_ONLY=1はD-1（NimBLEホスト無し）限定．実ESP-IDFの
    #  KconfigではCONTROLLER_ONLYとNIMBLE_ENABLEDは排他選択のため，NimBLE
    #  ON時に立てない（C6のBLE実施02＝advisorレビュー指摘と同じ分離）．
    #  実際の定義は下の「2. ソースファイル」節の if(NOT ESP32C5_BT_NIMBLE) 内．
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
#  ★C3のbt/stub/include（freertos/*.h＋esp_partition.h）を再利用する
#  （v6.1のbt.cはC3と同じプログラミングモデル）．
#  ★BLE実施05：C5独自のbt/stub/include（bt_nimble_config.h＝LEGACY_VHCI=0）を
#  PREPENDでC3のbt/stub/include（bt_nimble_config.h＝LEGACY_VHCI=1）より
#  前に置く．順序を誤るとC3版が先に見つかりLEGACY_VHCI=1になる
#  （mbufヘッダ余白計算がトランスポートと不整合＝サイレントな実行時
#  バッファバグ）．D-1（bt_smoke_c5）はbt_nimble_config.hをincludeしない
#  ため本PREPENDは無害．
#
list(PREPEND ASP3_INCLUDE_DIRS ${BT_TARGETDIR}/stub/include)

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
    #  hci_driver_standard.c と hci_driver_nimble.c（D-2a節）は共に
    #  hci_driver_vhci_opsを定義するため二者択一（同時リンク不可）．
    #  D-1（controller-only．bt_smoke_c5）はstandard版を下の
    #  if(NOT ESP32C5_BT_NIMBLE)ブロックで追加する．NimBLE ON時
    #  （ble_host_smoke_c5）はD-2a節がnimble版を追加する．
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

if(NOT ESP32C5_BT_NIMBLE)
    #  ★D-1＝controller-onlyスモークテスト（NimBLEホスト無し）限定．
    #  CONFIG_BT_CONTROLLER_ONLYとCONFIG_BT_NIMBLE_ENABLEDは実ESP-IDFの
    #  Kconfigでは同時に1にならない排他選択（C6のBLE実施02＝advisor
    #  レビュー指摘）．NimBLE ON時は立てず，hci_driver_standard.cも
    #  外す（hci_driver_vhci_opsの多重定義回避）．
    list(APPEND ASP3_COMPILE_DEFS
        CONFIG_BT_CONTROLLER_ONLY=1
    )
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${IDF}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    )
endif()

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

#
#  ==================================================================
#  Phase D-2a／BLE実施05：NimBLE ホストスタック（IDF v6.1版）
#  ==================================================================
#
#  D-1（コントローラ＋VHCI，bt_smoke_c5）の上に NimBLE ホストを載せる．
#  C6のBLE実施02（esp32c6_espidf/esp_bt.cmakeのESP32C6_BT_NIMBLEブロック）
#  の逐語的な転写だが，★ソースはhal submoduleではなくIDF v6.1
#  （${IDF}）から採る——C5のbt/phy/coex/nimbleを同一matched setで
#  揃える（本ファイル冒頭のライブラリ世代選定と同じ理由．hal世代の
#  nimble＋v6.1世代のPHY/controller blobを混ぜない）．
#
#  C6/C5は新世代コントローラ（SOC_ESP_NIMBLE_CONTROLLER=1）のため，
#  nimble_port.cのesp_nimble_init()内部でesp_nimble_hci_init()呼出しが
#  コンパイルアウトされる＝C3のLEGACY VHCI経路は存在しない．C5は
#  hci_transport.c（D-1で既存）＋hci_driver_nimble.c＋hci_esp_ipc.cを
#  使う（D-1のhci_driver_standard.cとhci_driver_vhci_opsを取り合う
#  二者択一．上の if(NOT ESP32C5_BT_NIMBLE) で分離済み）．
#
#  ★C6のBLE実施02との差分（IDF v6.1固有）：
#    - -include sdkconfig.h は追加しない：C5はNuttX版sdkconfig.hを持たず，
#      host .c の #include "sdkconfig.h" は sdkconfig_stub/sdkconfig.h
#      （target.cmakeが全ビルド共通で追加済み）へ解決される．
#    - TRUE=1／BT_HCI_LOG_INCLUDED=0 は追加しない：v6.1の nimble_port.c は
#      bt_common.h（TRUE／BT_HCI_LOG_INCLUDED の定義元）を #if より前に
#      includeするため，C6/halで踏んだ順序バグは v6.1 には存在しない．
#      （もし実機ビルドで hci_log/bt_hci_log.h が要求されたら，そのとき
#      C6と同じ -DTRUE=1 -DBT_HCI_LOG_INCLUDED=0 を本ブロックへ追加する）．
#
if(ESP32C5_BT_NIMBLE)

    set(NIMBLE_ROOT ${IDF}/components/bt/host/nimble/nimble/nimble)
    set(BT_ROOT ${IDF}/components/bt)
    set(TINYCRYPT_ROOT ${IDF}/components/bt/host/nimble/nimble/ext/tinycrypt)

    #  ---- ★D-2d：SMP（ペアリング／ボンディング）有効化 ----
    #  C3 の ESP32C3_BT_SM の C5 版（tinycrypt は IDF パス）．ON時は
    #  MYNEWT_VAL_BLE_SM_LEGACY/SC=0 の «蓋» を外し，SC=ECDH P-256 の crypto を
    #  vendored tinycrypt で供給，bond store は ble_store_ram（IDF文脈で空）でなく
    #  ble_store_config（PERSIST=0＝RAM）を使う（S3 §5.2）．OFF で D-2a(sync/adv)
    #  構成へ完全復帰＝可逆．C5 は C3 と別 blob(015db3db)＝C3 の «2個目暗号化ACL»
    #  の壁は非該当の公算（docs/bt-shim.md「別PC引き継ぎ要点」）．
    option(ESP32C5_BT_SM "Enable NimBLE SMP pairing/bonding on C5 (Phase D-2d, tinycrypt)" ON)

    #  （D-2d bond診断）SVC_PERROR：esp_shim の «想定外» サービスコールエラー
    #  （非E_OK かつ 非E_CTX/E_TMOUT/E_QOVR）を g_svc_err_* グローバルへ記録＝
    #  app が RTC STORE へミラーして esptool で回収（C5はコンソール不安定のため
    #  RTC経由）．暗号後の鍵配布で失敗する API を特定する．既定OFF＝非回帰．
    option(ESP32C5_BT_APIERR_TRACE "Record unexpected esp_shim svc-call errors to globals (D-2d bond diag)" OFF)
    if(ESP32C5_BT_APIERR_TRACE)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_APIERR_TRACE)
    endif()

    #  ---- コンパイル定義 ----
    #  CONFIG_BT_NIMBLE_*一式はbt/stub/include/bt_nimble_config.h（C5専用版．
    #  LEGACY_VHCI=0）で供給する．SM OFF時のみ SM_LEGACY/SC=0 でble_sm*.cを
    #  near-empty化しtinycrypt/mbedTLSリンクを回避する（C3/C6のD-2aと同じ判断）．
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_NIMBLE)
    if(ESP32C5_BT_SM)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_SM)
    else()
        list(APPEND ASP3_COMPILE_DEFS
            MYNEWT_VAL_BLE_SM_LEGACY=0
            MYNEWT_VAL_BLE_SM_SC=0
        )
    endif()

    #  bt_nimble_config.h（CONFIG_BT_NIMBLE_*）・syscfg/syscfg.h（MYNEWT_VAL）
    #  を強制include．D-1の -include soc/soc_caps.h 等と衝突しないよう
    #  SHELL:接頭辞を使う．
    list(APPEND ASP3_COMPILE_OPTIONS
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt_nimble_config.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include syscfg/syscfg.h>"
    )

    #  ---- インクルードパス ----
    #  porting/nimble/include（syscfg/syscfg.h）・porting/npl/freertos/
    #  include・host/nimble/port/include（esp_nimble_init/mem）はD-1の
    #  インクルードリストに既に含まれる（-Iの並びで先に来る＝優先解決）．
    list(APPEND ASP3_INCLUDE_DIRS
        ${NIMBLE_ROOT}/host/include
        ${NIMBLE_ROOT}/include
        ${NIMBLE_ROOT}/transport/include
        ${NIMBLE_ROOT}/host/services/gap/include
        ${NIMBLE_ROOT}/host/services/gatt/include
        ${NIMBLE_ROOT}/host/util/include
        ${NIMBLE_ROOT}/host/store/ram/include
    )
    if(ESP32C5_BT_SM)
        #  D-2d：ble_store_config と tinycrypt（SC の uECC P-256 ＋ AES-CMAC）
        list(APPEND ASP3_INCLUDE_DIRS
            ${NIMBLE_ROOT}/host/store/config/include
            ${TINYCRYPT_ROOT}/include
        )
    endif()

    #  ---- ソースファイル ----
    #  D-1で既に npl_os_freertos.c／os_msys_init.c／bt_osi_mem.c／
    #  os_mempool.c／hci_transport.c はリンク済み．
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${BT_ROOT}/porting/transport/driver/vhci/hci_driver_nimble.c
        ${NIMBLE_ROOT}/transport/esp_ipc/src/hci_esp_ipc.c
        #  nimble_mem_malloc/calloc/free（ホスト各所が直接呼ぶ．heap_caps_*
        #  ＝esp_shim_libc.c へ委譲）．実機リンクで未定義参照として発覚し追加．
        ${BT_ROOT}/host/nimble/port/src/esp_nimble_mem.c
        ${IDF}/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c
        ${IDF}/components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c
        #  ホストスタック本体（C6のBLE実施02と同一トリム集合．
        #  ble_svc_gap/gatt のみ採用．他サービス・永続ボンディング・
        #  新機能（ble_cs/ble_ead/ble_aes_ccm/ble_gattc_cache*/ble_eatt）は
        #  sync/adv到達には不要のため不採用）
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
    if(ESP32C5_BT_SM)
        #  D-2d：bond store は ble_store_config（PERSIST=0＝RAM，NVS不使用．
        #  ble_store_ram.c は IDF文脈で空＝S3 §5.2 の真因）＋ tinycrypt 必要5ソース
        #  （ble_sm_alg.c の tc_aes*/tc_cmac_*/uECC_* 参照に対応）．
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

endif()

endif()
