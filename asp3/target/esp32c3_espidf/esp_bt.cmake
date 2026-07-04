#
#               ESP32-C3 Bluetooth（BLE）統合 Phase D-1
#
#  コントローラ起動＋VHCIループバック（ホストスタック無し）．
#  設計・経緯は asp3_core の docs/dev/esp-idf-integration.md Phase D
#  および本リポジトリの docs/bt-shim.md．
#
#  Wi-Fi（esp_wifi.cmake）との違い：BTコントローラ本体
#  （components/bt/controller/esp32c3/bt.c）は封印済みblobではなく
#  ソース配布で，freertos/*.hを直接includeしFreeRTOS APIをインライン
#  で直接呼ぶ．そのためosi関数テーブルではなく，freertos/*.hヘッダ
#  自体をbt/stub/include/freertos/でシムする（実体はwifi/esp_shim.cの
#  既存プリミティブへ委譲．新しいプリミティブの発明はしない）．
#
#  RAM予算のためESP32C3_WIFIとの同時ONは現時点で未対応
#  （target.cmakeでFATAL_ERROR済み）．
#

if(ESP32C3_BT)

set(BT_CHIP_SERIES esp32c3)
set(BT_TARGETDIR ${TARGETDIR}/bt)

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C3_BT
    CONFIG_BT_ENABLED
    CONFIG_BLE_LOG_ENABLED=0
    CONFIG_BT_CTRL_LE_LOG_MODE_BLE_LOG_V2=0
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    CONFIG_ESP_IPC_ENABLE
    #  esp_wifi.cmakeと同じ理由（PLL温度追従は較正データ永続化前提．
    #  本ビルドは毎回フル較正のため無効化で十分）
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    #  MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL：esp_wifi.cmakeと同じ理由
    #  （phy_init.cがheap_caps.hを#includeせず直値のビットマスクを
    #  期待するため．値の意味自体はesp_shim_libc.cのheap_caps_*が
    #  capsを無視するため持たない．シンボル解決のみ）
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  bt.cはesp_intr_alloc()/intr_handle_t/ESP_INTR_FLAG_*を使うが，本体は
#  esp_intr_alloc.hを直接includeしない（ESP-IDF/NuttXのビルドシステム
#  側で暗黙にインクルードパスへ通す前提のヘッダ．Wireless.mkの1b節と
#  同種の事情）．-includeで強制的に読み込ませる．
#
list(APPEND ASP3_COMPILE_OPTIONS
    $<$<COMPILE_LANGUAGE:C>:-include$<SEMICOLON>esp_intr_alloc.h>
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#
list(APPEND ASP3_INCLUDE_DIRS
    ${BT_TARGETDIR}/stub/include
    ${TARGETDIR}/wifi
    ${ESP_HAL_DIR}/components/bt/include/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/bt/common/include
    ${ESP_HAL_DIR}/components/bt/common/ble_log/include
    ${ESP_HAL_DIR}/components/bt/porting/include
    ${ESP_HAL_DIR}/components/bt/porting/include/os
    ${ESP_HAL_DIR}/components/esp_hw_support/include
    ${ESP_HAL_DIR}/components/esp_hw_support/include/soc
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c3/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp32c3/private_include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/include
    ${ESP_HAL_DIR}/components/esp_system/include
    ${ESP_HAL_DIR}/components/esp_wifi/include
    ${ESP_HAL_DIR}/components/esp_phy/include
    ${ESP_HAL_DIR}/components/esp_phy/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_pm/include
    ${ESP_HAL_DIR}/components/esp_timer/include
    ${ESP_HAL_DIR}/components/esp_coex/include
    ${ESP_HAL_DIR}/components/esp_rom/include
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include/${BT_CHIP_SERIES}
    ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}
    ${ESP_HAL_DIR}/components/heap/include
    ${ESP_HAL_DIR}/components/log/include
    ${ESP_HAL_DIR}/components/riscv/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/include
    ${ESP_HAL_DIR}/components/esp_hal_gpio/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_hal_clock/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/efuse/include
    ${ESP_HAL_DIR}/components/efuse/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_event/include
)

#
#  ------------------------------------------------------------------
#  2. ソースファイル
#  ------------------------------------------------------------------
#
list(APPEND ASP3_CFG_FILES ${BT_TARGETDIR}/bt.cfg)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_HAL_DIR}/components/bt/controller/${BT_CHIP_SERIES}/bt.c
    ${BT_TARGETDIR}/bt_shim.c
    #  esp_wifi.cmakeと同じ理由でPHY/クロック/ペリフェラルの実ソースを
    #  採用する（BTもWi-Fiと同じ無線ハードウェアを使うため必要）．
    ${ESP_HAL_DIR}/components/esp_phy/src/phy_init.c
    ${ESP_HAL_DIR}/components/esp_phy/src/phy_common.c
    ${ESP_HAL_DIR}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${ESP_HAL_DIR}/components/esp_phy/src/lib_printf.c
    ${ESP_HAL_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_HAL_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    #  Wi-Fiと共有のcoexアダプタ（docs/wifi-shim.md．ダミーno-opテーブル
     #  登録＝ROM側coexist_funcs NULL回避）．BT単体でも要求される．
    ${TARGETDIR}/wifi/esp_coex_adapter.c
)

#
#  ------------------------------------------------------------------
#  3. リンクライブラリパス・ライブラリ
#  ------------------------------------------------------------------
#
list(APPEND ASP3_LINK_OPTIONS
    -L${ESP_HAL_DIR}/components/bt/controller/lib_esp32c3_family/${BT_CHIP_SERIES}
    -L${ESP_HAL_DIR}/components/esp_phy/lib/${BT_CHIP_SERIES}
    -L${ESP_HAL_DIR}/components/esp_coex/lib/${BT_CHIP_SERIES}
)
list(APPEND ASP3_LINK_LIBS
    btdm_app
    phy
    coexist
    btbb
)

#
#  ------------------------------------------------------------------
#  4. ROM関数ld（esp_wifi.cmakeと同じ理由．BT固有分を追加）
#  ------------------------------------------------------------------
#
set(BT_ROM_LD_DIR ${ESP_HAL_DIR}/components/esp_rom/${BT_CHIP_SERIES}/ld)
set(ESP_BT_ROM_LD_FILES
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.api.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libgcc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.newlib.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc-suboptimal_for_misaligned_mem.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.version.ld
    ${ESP_HAL_DIR}/components/riscv/ld/rom.api.ld
    #  BT固有（実機rev v0.4＝ECO3以降．Phase A実機結果参照）．
    #  eco7版が正しい可能性あり＝実機で未解決ならここを見直す。
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.eco3_bt_funcs.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.bt_funcs.ld
    #  esp_rom/CMakeLists.txt（ESP-IDF本家，esp32c3枝）は本来
    #  ble_master/ble_50/ble_cca/ble_dtm/ble_test/ble_scan/ble_smpの
    #  7個も（Kconfig既定値では）追加リンクするが，実機での二分探索
    #  検証の結果こちらでは追加しない．詳細はdocs/bt-shim.md参照
    #  （ble_smp.ldを足すとr_rwip_init内部でNULL関数ポインタ呼び出し
    #  により新たなクラッシュが発生＝害あり．残り6個は追加しても
    #  emi.c:164アサートに変化なし＝無害だが無意味．どちらも不採用）．
)
foreach(_esp_bt_rom_ld ${ESP_BT_ROM_LD_FILES})
    list(APPEND ASP3_LINK_OPTIONS -Wl,-T,${_esp_bt_rom_ld})
endforeach()

endif()
