#
#		ターゲット依存部のCMake定義（ESP32-C5 esp-hal統合用）
#
#  esp32c6版（asp3_target/esp32c6_espidf/target.cmake相当）をコピー
#  起点に，docs/c5-port-design.md §5.1・§5.2に従いrename・値差替えした
#  もの（B-0/B-1スコープ．フェーズ2a）。
#    - 自分自身（target_*）は CMAKE_CURRENT_LIST_DIR 相対
#    - チップ依存部はsubmodule外（asp3/arch/riscv_gcc/esp32c5/．
#      CLAUDE.mdの禁則によりasp3_core submoduleを直接編集しないため．
#      docs/c5-port-design.md §2.2で配置の妥当性を検証済み）
#    - 共通arch・カーネル本体は submodule（ASP3_ROOT_DIR）側
#
#  QEMU対応は【実機確認待ち】（docs/c5-port-design.md §8.1 14番．
#  Espressif版QEMU forkにesp32c5マシンが追加されているかC6のときの
#  ように「非対応」と決め打ちしていない）。本ファイルは実機書込みの
#  みを既定とする。
#
#  Wi-Fi統合（wifi/・esp_wifi.cmake）はフェーズ2b（B-2a．docs/
#  c5-port-design.md §5.4・§6）で実装済み。既定はOFF（-DESP32C5_WIFI=ON
#  で有効化）。ESP32C5_WIFIブロックの中身がesp_wifi.cmakeをincludeする。
#

set(TARGETDIR ${CMAKE_CURRENT_LIST_DIR})

#
#  esp-hal-3rdparty（submodule）のパスとインクルードディレクトリ
#
#  B-0/B-1で使用するのはRTOS非依存の下層のみ：
#    hal（LL層＝static inlineのレジスタ薄層）・soc（レジスタ定義・
#    構造体・peripherals.ld）・esp_common（esp_attr.h）．
#  sdkconfig.hはKconfig生成物を使わず，本リポジトリ側で用意した
#  スタブ（sdkconfig_stub/．下記参照）を使う（hal submoduleは
#  ESP32-C5向けのNuttX統合ファイル一式を同梱していないため）。
#
get_filename_component(ESP_HAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../hal ABSOLUTE)

#
#  hal_stub（libc互換ヘッダ．ツールチェーンにnewlib実体が無い環境向け）
#  はESP32-C3用のものをそのまま再利用する（チップ非依存＝トゥール
#  チェーンのギャップを埋めるだけの内容）。
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

list(APPEND ASP3_INCLUDE_DIRS
    ${C3_TARGETDIR}/hal_stub/include
    ${TARGETDIR}/sdkconfig_stub
    ${ESP_HAL_DIR}/components/hal/esp32c5/include
    ${ESP_HAL_DIR}/components/hal/include
    ${ESP_HAL_DIR}/components/hal/platform_port/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/esp32c5/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/include
    ${ESP_HAL_DIR}/components/soc/esp32c5/include
    ${ESP_HAL_DIR}/components/soc/esp32c5/register
    ${ESP_HAL_DIR}/components/soc/include
    ${ESP_HAL_DIR}/components/esp_common/include
    #  esp_rom_sys.h（esp_rom_set_cpu_ticks_per_us宣言．
    #  target_kernel_impl.cが使用）。C6版のtarget.cmakeはこれを
    #  Wi-Fi限定（esp_wifi.cmake内）でしか積んでおらず，Wi-Fi無し
    #  ビルドでは本来ここが欠落する潜在バグだった（本ポートで気付いた
    #  ため無条件で積む．C5固有の問題ではなくC6にも共通する既存の
    #  ギャップ）。
    ${ESP_HAL_DIR}/components/esp_rom/include
    ${ESP_HAL_DIR}/components/esp_rom/esp32c5/include
)

#
#  コンフィギュレーション関連
#
list(APPEND ASP3_CFG_FILES
    ${TARGETDIR}/target_kernel.cfg
)

list(APPEND ASP3_KERNEL_CFG_TRB_FILES
    ${TARGETDIR}/target_kernel.py
)

list(APPEND ASP3_CHECK_TRB_FILES
    ${TARGETDIR}/target_check.py
)

#
#  インクルードディレクトリ
#
list(APPEND ASP3_INCLUDE_DIRS
    ${TARGETDIR}
)

#
#  コンソールの選択（chip.cmake参照）．ボード固有のピン配線が未確定
#  （docs/c5-port-design.mdはボード実装非依存の設計）のため，既定は
#  chip.cmakeと同じuart0とする（C6のようなUSB Serial/JTAG固定ボードが
#  判明したら-DESP32C5_CONSOLE=usbjtagで切替え．usbjtag選択時は
#  arch層の生レジスタ版esp32c5_usbjtag.cが使われる＝esp-hal LL層版
#  （C6のesp32c6_usbjtag_hal.c相当）は本フェーズでは未移植）。
#
set(ESP32C5_CONSOLE uart0
    CACHE STRING "Console device: uart0 or usbjtag")

#
#  コンパイル定義
#
#  USE_TIM_AS_HRT：高分解能タイマにSYSTIMERを使用（Machine Timer不使用）
#  TOPPERS_SUPPORT_TLS：タスク実行開始時(start_r)のTLS初期化(tp設定)を
#    有効化．picolibcのrand()等TLS依存libc関数を使うとtp未初期化(=0)で
#    Load access faultになるため常時有効。
#
list(APPEND ASP3_COMPILE_DEFS
    USE_TIM_AS_HRT
    TOPPERS_SUPPORT_TLS
)

#
#  リンクオプション
#
list(APPEND ASP3_LINK_OPTIONS
    -Wl,--print-memory-usage
    -Wl,--gc-sections
    -Wl,--build-id=none
    -L${ESP_HAL_DIR}/components/soc/esp32c5/ld
)

set(ASP3_LDSCRIPT ${TARGETDIR}/esp32c5.ld)

#
#  ターゲット依存部のソース
#
list(APPEND ASP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
    ${TARGETDIR}/flash_header.S
)

#
#  チップ依存部のインクルード（submodule外．docs/c5-port-design.md §2.2）
#
include(${CMAKE_CURRENT_LIST_DIR}/../../arch/riscv_gcc/esp32c5/chip.cmake)

#
#  実機への書込み（cmake --build <dir> --target run）
#
#  【実機確認待ち】docs/c5-port-design.md §8.1 13番。esptoolの
#  pinnedバージョンが`--chip esp32c5`をサポートしているか要確認。
#
set(ESP32C5_ESPTOOL esptool
    CACHE STRING "Path to esptool")
set(ESP32C5_PORT /dev/ttyACM1
    CACHE STRING "Serial port of the ESP32-C5 board")
set(ASP3_RUN_COMMAND
    ${ESP32C5_ESPTOOL} --chip esp32c5 --port ${ESP32C5_PORT}
    write-flash 0x0 ${CMAKE_BINARY_DIR}/asp_flash.bin
)

#
#  Wi-Fi（esp_wifi blob＋os_adapter shim．フェーズ2b＝B-2a scan．
#  docs/c5-port-design.md §5.4・§6）
#
#  shim基盤（esp_shim.[ch]／esp_shim_libc.c／esp_shim_blobglue.c）は
#  C3のwifi/を土台に，チップ固有アドレス（割込みルーティング＝
#  INTMTX+CLIC，HW RNG＝LPPERI_RNG_DATA_SYNC_REG，eFuse MACレジスタ）
#  のみ差し替えたC5版を${TARGETDIR}/wifi/に置く（C6版と同じ構成）。
#  chip非依存のesp_shim.h／esp_shim_cfg.h／esp_shim_libc.c／
#  esp_event_shim.c／esp_coex_adapter.c／esp_shim.cfgはC3側をそのまま
#  再利用する（中身に変更不要）。
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

#
#  Bluetooth（BLE．esp32c6/c5世代コントローラ＋C3型直接FreeRTOS shim．
#  既定OFF．Phase D-1＝controller init+VHCI，BLE実施03）
#
#  ESP32C3_BT/ESP32C6_BTと同じ理由でESP32C5_WIFIとの同時ONは現状
#  未対応（RAM予算．esp_bt.cmake参照）．shim基盤（wifi/esp_shim.[ch]／
#  esp_shim_blobglue.c）はWi-Fi・BT共有のためESP32C5_WIFI単独ゲートから
#  (ESP32C5_WIFI OR ESP32C5_BT)へ拡張する（C6のtarget.cmakeと同じ
#  パターン．docs/ble-c5c6.md「BLE実施03」節）．
#
option(ESP32C5_BT "Enable Bluetooth (BLE embedded controller V1 + direct-FreeRTOS shim, Phase D-1)" OFF)
if(ESP32C5_BT AND ESP32C5_WIFI)
    message(FATAL_ERROR "ESP32C5_BT + ESP32C5_WIFI is not supported yet (RAM budget; C3/C6の前例踏襲)")
endif()

option(ESP32C5_WIFI "Enable Wi-Fi (esp_wifi blob + os_adapter shim, Phase B-2a scan)" OFF)
if(ESP32C5_WIFI OR ESP32C5_BT)
    if(ESP32C5_WIFI AND NOT EXISTS ${TARGETDIR}/esp_wifi.cmake)
        message(FATAL_ERROR
            "ESP32C5_WIFI=ON was requested, but ${TARGETDIR}/esp_wifi.cmake "
            "was not found. See docs/c5-port-design.md.")
    endif()
    list(APPEND ASP3_INCLUDE_DIRS
        ${TARGETDIR}/wifi
        ${C3_TARGETDIR}
        ${C3_TARGETDIR}/wifi
    )
    list(APPEND ASP3_CFG_FILES ${C3_TARGETDIR}/wifi/esp_shim.cfg)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_shim.c
        #  esp_shim_blobglue.cはWiFi blob（net80211/pp/core）専用の
        #  グルーが大半だが，esp_sleep_pd_config／esp_sleep_clock_config／
        #  esp_deep_sleep_register_phy_hook／_esp_error_check_failed等
        #  BTも要求する汎用スタブ（modem_clock.c／phy_init.cが参照）を
        #  同居させているため，BT単体ビルドでもリンクする（--gc-sections
        #  でWiFi専用の未参照部分は落ちる．C6のBLE実施01で確認済みの
        #  パターンをC5でも踏襲）．
        ${TARGETDIR}/wifi/esp_shim_blobglue.c
        ${C3_TARGETDIR}/wifi/esp_shim_libc.c
    )
endif()
if(ESP32C5_WIFI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_WIFI)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_wifi_adapter.c
        ${C3_TARGETDIR}/wifi/esp_event_shim.c
        ${C3_TARGETDIR}/wifi/esp_coex_adapter.c
    )

    #
    #  DIAGNOSTIC (temporary，実施16)：regi2c（`phy_i2c_{read,write}Reg
    #  [_Mask]`）トランザクション列トレース．PHY校正ループ入口までの
    #  read/write系列をstock ESP-IDF v6.1 examples/wifi/scanと比較する
    #  調査専用の計装．既定OFF＝通常のC5ビルドには一切影響しない．
    #  `-DESP32C5_WIFI_REGI2C_TRACE=ON`で有効化（--wrapフラグの追加は
    #  esp_wifi.cmake側）。docs/c5-bringup.md 実施16参照。
    #
    option(ESP32C5_WIFI_REGI2C_TRACE
        "DIAGNOSTIC (temporary, 実施16): trace regi2c read/write transactions via --wrap" OFF)
    if(ESP32C5_WIFI_REGI2C_TRACE)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_WIFI_REGI2C_TRACE)
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES
            ${TARGETDIR}/wifi/wifi_trace.c
        )
    endif()

    include(${TARGETDIR}/esp_wifi.cmake)
endif()
include(${TARGETDIR}/esp_bt.cmake)

#
#  TCP/IP統合（lwIP．Wi-Fi必須＝ESP32C5_WIFIが前提。実施44）
#
#  net/層（sys_arch・netif・lwipopts等）はチップ非依存（esp_wifi_
#  internal_tx/reg_rxcb／esp_read_mac等のblob APIのみに依存し，
#  C5固有のレジスタ・アドレスには一切触れない）ため，C3側
#  （${C3_TARGETDIR}/net）をコピーせずそのまま再利用する．
#  esp_shim_libc.c等と同じ「chip非依存部はC3_TARGETDIRから直接取込む」
#  既存パターンを踏襲（docs/tcpip-integration.md，docs/c5-bringup.md
#  実施44）．
#
option(ESP32C5_LWIP "Integrate lwIP (TCP/IP + BSD sockets, requires ESP32C5_WIFI)" OFF)
if(ESP32C5_LWIP)
    if(NOT ESP32C5_WIFI)
        message(FATAL_ERROR "ESP32C5_LWIP requires ESP32C5_WIFI=ON")
    endif()

    get_filename_component(LWIP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../lwip ABSOLUTE)
    include(${LWIP_DIR}/src/Filelists.cmake)

    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_LWIP)

    list(APPEND ASP3_INCLUDE_DIRS
        ${LWIP_DIR}/src/include
        ${LWIP_DIR}/contrib/apps/ping
        ${LWIP_DIR}/contrib/apps/tcpecho_raw
        ${C3_TARGETDIR}/net/port/include
        ${C3_TARGETDIR}/net
    )

    list(APPEND ASP3_CFG_FILES ${C3_TARGETDIR}/net/net.cfg)

    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${lwipcore_SRCS}
        ${lwipcore4_SRCS}
        ${lwipapi_SRCS}
        ${LWIP_DIR}/src/netif/ethernet.c
        ${LWIP_DIR}/contrib/apps/ping/ping.c
        ${LWIP_DIR}/contrib/apps/tcpecho_raw/tcpecho_raw.c
        ${C3_TARGETDIR}/net/port/sys_arch.c
        ${C3_TARGETDIR}/net/netif_esp32c3.c
    )
endif()

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
