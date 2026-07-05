#
#		ターゲット依存部のCMake定義（ESP32-C6 esp-hal統合用）
#
#  asp3_core の target/esp32c6_gcc を外側リポジトリ（asp3_esp_idf）へ
#  コピーして外部ターゲット化したもの（ASP3_TARGET_DIR方式．
#  PORTING_GUIDE.md §6「外部（SDK）ターゲットの置き方」）：
#    - 自分自身（target_*）は CMAKE_CURRENT_LIST_DIR 相対
#    - チップ依存部・共通arch・カーネル本体は submodule（ASP3_ROOT_DIR）側
#  esp-hal-3rdparty との統合（Phase B）はこのターゲット上で行う．
#  経緯は asp3_core の docs/dev/esp32c6-target.md．
#
#  C3と異なりQEMU未対応（Espressif版QEMU forkにesp32c6マシンが無い．
#  asp3_core Phase Aで確認済み）．実機専用ターゲット．
#

set(TARGETDIR ${CMAKE_CURRENT_LIST_DIR})

#
#  esp-hal-3rdparty（submodule）のパスとインクルードディレクトリ
#
#  Phase B-1で使用するのはRTOS非依存の下層のみ：
#    hal（LL層＝static inlineのレジスタ薄層）・soc（レジスタ定義・
#    構造体・peripherals.ld）・esp_common（esp_attr.h）．
#  sdkconfig.hはKconfig生成物を使わず，esp-hal同梱のNuttX用静的スタブ
#  （SOC機能フラグのみ）を流用する．
#
get_filename_component(ESP_HAL_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../hal ABSOLUTE)

#
#  hal_stub（libc互換ヘッダ．ツールチェーンにnewlib実体が無い環境向け）
#  はESP32-C3用のものをそのまま再利用する（チップ非依存＝トゥール
#  チェーンのギャップを埋めるだけの内容．esp32c3_espidf/hal_stub/
#  README相当のコメントは各ヘッダ先頭参照）．
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

list(APPEND ASP3_INCLUDE_DIRS
    ${C3_TARGETDIR}/hal_stub/include
    ${ESP_HAL_DIR}/components/hal/esp32c6/include
    ${ESP_HAL_DIR}/components/hal/include
    ${ESP_HAL_DIR}/components/hal/platform_port/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/esp32c6/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/include
    ${ESP_HAL_DIR}/components/soc/esp32c6/include
    ${ESP_HAL_DIR}/components/soc/esp32c6/register
    ${ESP_HAL_DIR}/components/soc/include
    ${ESP_HAL_DIR}/components/esp_common/include
    ${ESP_HAL_DIR}/nuttx/esp32c6/include
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
#  コンソールの選択（chip.cmake参照）．このボードはUSB Serial/JTAG
#  専用（UART0未配線．Phase Aで確認済み）のためusbjtagを既定とする．
#  UART配線のあるボードでは -DESP32C6_CONSOLE=uart0 を指定する．
#
set(ESP32C6_CONSOLE usbjtag
    CACHE STRING "Console device: uart0 or usbjtag")

#
#  コンパイル定義
#
#  USE_TIM_AS_HRT：高分解能タイマにSYSTIMERを使用（Machine Timer不使用）
#
list(APPEND ASP3_COMPILE_DEFS
    USE_TIM_AS_HRT
)

#
#  リンクオプション
#
list(APPEND ASP3_LINK_OPTIONS
    -Wl,--print-memory-usage
    -Wl,--gc-sections
    -Wl,--build-id=none
    -L${ESP_HAL_DIR}/components/soc/esp32c6/ld
)

set(ASP3_LDSCRIPT ${TARGETDIR}/esp32c6.ld)

#
#  ターゲット依存部のソース
#
list(APPEND ASP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
    ${TARGETDIR}/flash_header.S
)

#
#  チップ依存部のインクルード
#
include(${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6/chip.cmake)

#
#  USB Serial/JTAGコンソールドライバをesp-hal LL層版に差し替える
#  （Phase B-1．公開シンボルは同一のためchip_serial.cはそのまま）
#
if(ESP32C6_CONSOLE STREQUAL "usbjtag")
    list(REMOVE_ITEM ASP3_SYSSVC_TARGET_C_FILES
        ${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c6/esp32c6_usbjtag.c)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/esp32c6_usbjtag_hal.c)
endif()

#
#  実機への書込み（cmake --build <dir> --target run）．QEMU非対応
#  （Espressif版QEMU forkにesp32c6マシンが無い．asp3_core Phase Aで
#  確認済み）のため実機書込みのみ．
#
set(ESP32C6_ESPTOOL esptool
    CACHE STRING "Path to esptool")
set(ESP32C6_PORT /dev/ttyACM1
    CACHE STRING "Serial port of the ESP32-C6 board")
set(ASP3_RUN_COMMAND
    ${ESP32C6_ESPTOOL} --chip esp32c6 --port ${ESP32C6_PORT}
    write-flash 0x0 ${CMAKE_BINARY_DIR}/asp_flash.bin
)

#
#  Wi-Fi（esp_wifi blob＋os_adapter shim．既定OFF．Phase B-2a＝scanのみ）
#
#  shim基盤（esp_shim.[ch]／esp_shim_libc.c／esp_shim_blobglue.c）は
#  C3のwifi/を土台に，チップ固有アドレス（割込みルーティング＝
#  INTMTX+PLIC_MX，HW RNG＝LPPERI_RNG_DATA_REG，eFuse MACレジスタ）
#  のみ差し替えたC6版を${TARGETDIR}/wifi/に置く．chip非依存の
#  esp_shim.h／esp_shim_cfg.h／esp_shim_libc.c／esp_event_shim.c／
#  esp_coex_adapter.c／esp_shim.cfgはC3側をそのまま再利用する
#  （中身に変更不要．docs/wifi-shim.md参照）．
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

option(ESP32C6_WIFI "Enable Wi-Fi (esp_wifi blob + os_adapter shim, Phase B-2a scan)" OFF)
if(ESP32C6_WIFI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_WIFI)
    list(APPEND ASP3_INCLUDE_DIRS
        ${TARGETDIR}/wifi
        ${C3_TARGETDIR}
        ${C3_TARGETDIR}/wifi
    )
    list(APPEND ASP3_CFG_FILES ${C3_TARGETDIR}/wifi/esp_shim.cfg)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_shim.c
        ${TARGETDIR}/wifi/esp_shim_blobglue.c
        ${TARGETDIR}/wifi/esp_wifi_adapter.c
        ${C3_TARGETDIR}/wifi/esp_shim_libc.c
        ${C3_TARGETDIR}/wifi/esp_event_shim.c
        ${C3_TARGETDIR}/wifi/esp_coex_adapter.c
        #  DIAGNOSTIC (temporary, RX-enable --wrap trace)．
        #  docs/wifi-shim-c6.md「実施12」参照．調査終了後に削除する．
        ${TARGETDIR}/wifi/wifi_trace.c
    )
endif()
include(${TARGETDIR}/esp_wifi.cmake)

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
