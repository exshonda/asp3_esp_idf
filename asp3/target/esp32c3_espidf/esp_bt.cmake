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
#  NimBLE のポート層ヘッダ（nimble_port.h 等）は ESP-IDF 流に sdkconfig.h
#  が暗黙includeされている前提で CONFIG_BT_NIMBLE_* を直接参照する．本
#  ビルドはグローバルなforce-includeを持たないため，NimBLE有効時のみ
#  sdkconfig.h を先頭でforce-includeする（純粋な#define集合＝カーネル
#  ソースにも無害．esp_intr_alloc.h と同じ手法）．
#

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
#  （D-2b(1)(j) 診断）RF/AGC cal の regi2c 系列トレース計装（既定OFF＝非回帰）．
#  ON時のみ phy_cal_trace.c を追加し ROM phy_get_romfuncs を --wrap して
#  g_phyFuns の regi2c エントリをトレースラッパへ差替える．詳細=docs/bt-shim.md。
#
option(ESP32C3_BT_PHY_CAL_TRACE "Trace RF/AGC cal regi2c via g_phyFuns swap (D-2b diag)" OFF)
if(ESP32C3_BT_PHY_CAL_TRACE)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${BT_TARGETDIR}/phy_cal_trace.c)
    list(APPEND ASP3_LINK_OPTIONS -Wl,--wrap=phy_get_romfuncs)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_PHY_CAL_TRACE)
endif()

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

#
#  ==================================================================
#  Phase D-2：NimBLE ホストスタック
#  ==================================================================
#
#  D-1（コントローラ＋VHCI）の上に NimBLE ホストを載せる．C3 は
#  SOC_ESP_NIMBLE_CONTROLLER を定義しないため，ESP-IDF の npl
#  （porting/npl/freertos/src/npl_os_freertos.c）ではなく *upstream* の
#  npl＋mem/mbuf を使い，HCI は LEGACY VHCI 経路で D-1 の esp_vhci_host_*
#  にブリッジする（詳細は docs/bt-shim.md／hal/components/bt/CMakeLists.txt
#  の NimBLE ブロック lines 694-940 を参照）．
#
#  RAM 予算のため既定は OFF．NimBLE を要するアプリ（ble_host_smoke）では
#  自動で ON にする（D-1 の bt_smoke を痩せたまま保つ）．
#
option(ESP32C3_BT_NIMBLE "Enable NimBLE host stack on top of BT controller (Phase D-2)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke")
    set(ESP32C3_BT_NIMBLE ON)
endif()

if(ESP32C3_BT_NIMBLE)

    set(NIMBLE_ROOT ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/nimble)
    set(NIMBLE_PORTING ${ESP_HAL_DIR}/components/bt/host/nimble/nimble/porting)
    set(BT_ROOT ${ESP_HAL_DIR}/components/bt)

    #  ---- コンパイル定義 ----
    #  ESP_PLATFORM：syscfg.h が esp_nimble_cfg.h を読むための分岐キー．
    #  SECURITY off：sync に暗号は不要．NIMBLE_BLE_SM = SM_LEGACY||SM_SC を
    #  0 に落とし（nimble_opt_auto.h），ble_sm*.c を near-empty 化して
    #  mbedTLS/tinycrypt リンクを回避する．
    #  NimBLE/コントローラ設定（CONFIG_BT_NIMBLE_* 等）は bt_nimble_config.h
    #  で供給する（force-include）．CONFIG_ESPRESSIF_BLE は使わない
    #  （sdkconfig.h が ESPRESSIF_WIFI を常時定義するため BLE キーを立てると
    #  coex が有効化され D-1 の coex-OFF 挙動が壊れる．詳細は
    #  bt/stub/include/bt_nimble_config.h の冒頭コメント参照）．
    list(APPEND ASP3_COMPILE_DEFS
        ESP_PLATFORM
        #  NimBLE ホストを積むビルドの識別子．esp_shim の静的プール拡張
        #  （wifi/esp_shim_cfg.h ほか）をこのビルド限定にするために使う
        #  （bt_smoke=コントローラのみ のビルドはプールを増やさない）．
        TOPPERS_ESP32C3_BT_NIMBLE
        MYNEWT_VAL_BLE_SM_LEGACY=0
        MYNEWT_VAL_BLE_SM_SC=0
    )
    #  NOTE: MYNEWT_VAL_BLE_MAX_CONNECTIONS の -D 上書きは効かない
    #  （esp_nimble_cfg.h が #ifndef MYNEWT_VAL でなく #ifndef
    #  CONFIG_NIMBLE_MAX_CONNECTIONS 経由で無条件に再定義するため
    #  値は既定の 4 になり，かつ -D すると全 NimBLE ファイルで再定義
    #  警告が出る）．接続数はヒープ確保（実行時）に効くだけでリンク時
    #  RAM には影響せず，本ビルド（sync 目的）は 4 のままで収まるため
    #  上書きしない．より絞るなら CONFIG_NIMBLE_MAX_CONNECTIONS を
    #  bt_nimble_config.h で定義する（未実施）．

    #  sdkconfig.h（CONFIG_*）と syscfg/syscfg.h（MYNEWT_VAL）を先頭で
    #  強制include（上のコメント参照）．NimBLE の porting ソース
    #  （os_msys_init.c 等）は syscfg.h を明示includeせず MYNEWT_VAL を
    #  使うため，ESP-IDF 同様に force-include で補う．syscfg.h は
    #  ESP_PLATFORM 定義時に esp_nimble_cfg.h（要 sdkconfig.h）を読むので
    #  順序は sdkconfig.h → syscfg/syscfg.h．
    #  SHELL: 接頭辞で -include を1単位として扱わせる（既存の
    #  -include esp_intr_alloc.h と重複フラグと誤認され de-dup される
    #  のを防ぐ．CMake COMPILE_OPTIONS の既知の罠）．
    list(APPEND ASP3_COMPILE_OPTIONS
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sdkconfig.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt_nimble_config.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include syscfg/syscfg.h>"
    )

    #  ---- インクルードパス ----
    list(APPEND ASP3_INCLUDE_DIRS
        ${NIMBLE_ROOT}/host/include
        ${NIMBLE_ROOT}/include
        ${NIMBLE_ROOT}/transport/include
        ${NIMBLE_ROOT}/host/services/gap/include
        ${NIMBLE_ROOT}/host/services/gatt/include
        ${NIMBLE_ROOT}/host/services/ans/include
        ${NIMBLE_ROOT}/host/services/bas/include
        ${NIMBLE_ROOT}/host/services/dis/include
        ${NIMBLE_ROOT}/host/services/ias/include
        ${NIMBLE_ROOT}/host/services/ipss/include
        ${NIMBLE_ROOT}/host/services/hr/include
        ${NIMBLE_ROOT}/host/services/htp/include
        ${NIMBLE_ROOT}/host/services/lls/include
        ${NIMBLE_ROOT}/host/services/prox/include
        ${NIMBLE_ROOT}/host/services/cts/include
        ${NIMBLE_ROOT}/host/services/tps/include
        ${NIMBLE_ROOT}/host/services/hid/include
        ${NIMBLE_ROOT}/host/services/sps/include
        ${NIMBLE_ROOT}/host/services/cte/include
        ${NIMBLE_ROOT}/host/services/ras/include
        ${NIMBLE_ROOT}/host/util/include
        ${NIMBLE_ROOT}/host/store/ram/include
        ${NIMBLE_ROOT}/host/store/config/include
        ${NIMBLE_PORTING}/nimble/include
        ${NIMBLE_PORTING}/npl/freertos/include
        ${BT_ROOT}/host/nimble/port/include
        ${BT_ROOT}/host/nimble/esp-hci/include
        ${BT_ROOT}/porting/include
        ${BT_ROOT}/porting/include/os
        ${BT_ROOT}/porting/mem
        ${BT_ROOT}/common/hci_log/include
    )

    #  ---- ソースファイル ----
    #  OS porting（upstream；SOC_ESP_NIMBLE_CONTROLLER 非定義のため）
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${NIMBLE_PORTING}/nimble/src/endian.c
        ${NIMBLE_PORTING}/nimble/src/os_mempool.c
        ${NIMBLE_PORTING}/nimble/src/mem.c
        ${NIMBLE_PORTING}/nimble/src/os_mbuf.c
        ${NIMBLE_PORTING}/nimble/src/os_msys_init.c
        ${NIMBLE_PORTING}/nimble/src/nimble_port.c
        ${NIMBLE_PORTING}/npl/freertos/src/npl_os_freertos.c
        ${NIMBLE_PORTING}/npl/freertos/src/nimble_port_freertos.c
        ${BT_ROOT}/porting/mem/bt_osi_mem.c
        #  nvs_port.c は ble_store_config/ble_store_nvs 用（本ビルドは
        #  ble_store_ram のみ使用のため不要）＝リンク対象外．
        #  HCI transport（LEGACY VHCI 経路）
        ${BT_ROOT}/host/nimble/esp-hci/src/esp_nimble_hci.c
        ${NIMBLE_ROOT}/transport/esp_ipc_legacy/src/hci_esp_ipc_legacy.c
        ${NIMBLE_ROOT}/transport/src/transport.c
        #  ホストスタック本体（hal CMakeLists の CONFIG_BT_NIMBLE_ENABLED
        #  ブロックから；ble_svc_gap/gatt のみ採用．他サービスは不要）
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
        ${NIMBLE_ROOT}/host/store/ram/src/ble_store_ram.c
    )

    #
    #  （D-2c 診断）RX-data dispatch 局在化計装（既定OFF＝非回帰）．
    #  ON時のみ acl_trace.c を追加し ble_mqueue_put/get・ble_l2cap_rx・
    #  ble_hs_conn_find を --wrap して呼出し数を RTC STORE2 へ記録する．
    #  仮説(a)host未dispatch / (b)conn_find NULL を1回のdump-memで切り分け．
    #  詳細=docs/bt-shim.md Phase D-2c．
    #
    option(ESP32C3_BT_ACL_TRACE "Trace RX ACL dispatch via --wrap (D-2c diag)" OFF)
    if(ESP32C3_BT_ACL_TRACE)
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${BT_TARGETDIR}/acl_trace.c)
        list(APPEND ASP3_LINK_OPTIONS
            -Wl,--wrap=ble_mqueue_put
            -Wl,--wrap=ble_mqueue_get
            -Wl,--wrap=ble_l2cap_rx
            -Wl,--wrap=ble_hs_conn_find
            -Wl,--wrap=ble_hci_trans_hs_acl_tx
            -Wl,--wrap=esp_vhci_host_send_packet
        )
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_ACL_TRACE)
    endif()

endif()

endif()
