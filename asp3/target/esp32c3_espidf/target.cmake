#
#		ターゲット依存部のCMake定義（ESP32-C3 esp-hal統合用）
#
#  asp3_core の target/esp32c3_gcc を外側リポジトリ（asp3_esp_idf）へ
#  コピーして外部ターゲット化したもの（ASP3_TARGET_DIR方式．
#  PORTING_GUIDE.md §6「外部（SDK）ターゲットの置き方」）：
#    - 自分自身（target_*）は CMAKE_CURRENT_LIST_DIR 相対
#    - チップ依存部・共通arch・カーネル本体は submodule（ASP3_ROOT_DIR）側
#  esp-hal-3rdparty との統合（Phase B）はこのターゲット上で行う．
#  経緯は asp3_core の docs/dev/esp-idf-integration.md．
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

list(APPEND ASP3_INCLUDE_DIRS
    ${TARGETDIR}/hal_stub/include
    ${ESP_HAL_DIR}/components/hal/esp32c3/include
    ${ESP_HAL_DIR}/components/hal/include
    ${ESP_HAL_DIR}/components/hal/platform_port/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/esp32c3/include
    ${ESP_HAL_DIR}/components/esp_hal_usb/include
    ${ESP_HAL_DIR}/components/soc/esp32c3/include
    ${ESP_HAL_DIR}/components/soc/esp32c3/register
    ${ESP_HAL_DIR}/components/soc/include
    ${ESP_HAL_DIR}/components/esp_common/include
    ${ESP_HAL_DIR}/nuttx/esp32c3/include
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
#  QEMU／実機の切り替え（既定：QEMU）
#
#  OFF（実機ESP32-C3-DevKit）にすると TOPPERS_USE_QEMU を定義しない：
#    - target_exit のセミホスティング終了を行わない
#  実機のロード手段（esptool書込み手順）は実機対応時に整備する．
#
option(ESP32C3_QEMU "Build for QEMU esp32c3 (OFF: real ESP32-C3 board)" ON)

#
#  コンソールの選択（chip.cmake参照）．既定はQEMU=UART0・実機=USB
#  Serial/JTAG（UARTブリッジを持たないネイティブUSBボードを想定．
#  UART配線のあるボードでは -DESP32C3_CONSOLE=uart0 を指定する）．
#
if(ESP32C3_QEMU)
    set(_esp32c3_console_default uart0)
else()
    set(_esp32c3_console_default usbjtag)
endif()
set(ESP32C3_CONSOLE ${_esp32c3_console_default}
    CACHE STRING "Console device: uart0 or usbjtag")

#
#  コンパイル定義
#
#  USE_TIM_AS_HRT：高分解能タイマにSYSTIMERを使用（Machine Timer不使用）
#  TOPPERS_SUPPORT_TLS：タスク実行開始時(start_r)のTLS(スレッドローカル
#    ストレージ)初期化(tp設定)を有効化．picolibcのrand()等TLS依存libc
#    関数を使うとtp未初期化(=0)でLoad access faultになるため常時有効
#    （詳細はasp3_coreのarch/riscv_gcc/esp32c3/chip_asm.incの
#    init_additional_regs_start_r参照）．
#
list(APPEND ASP3_COMPILE_DEFS
    USE_TIM_AS_HRT
    TOPPERS_SUPPORT_TLS
)

if(ESP32C3_QEMU)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_USE_QEMU)
endif()

#
#  リンクオプション
#
list(APPEND ASP3_LINK_OPTIONS
    -Wl,--print-memory-usage
    -Wl,--gc-sections
    -Wl,--build-id=none
    -L${ESP_HAL_DIR}/components/soc/esp32c3/ld
)

set(ASP3_LDSCRIPT ${TARGETDIR}/esp32c3.ld)

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
include(${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c3/chip.cmake)

#
#  USB Serial/JTAGコンソールドライバをesp-hal LL層版に差し替える
#  （Phase B-1．公開シンボルは同一のためchip_serial.cはそのまま）
#
if(ESP32C3_CONSOLE STREQUAL "usbjtag")
    list(REMOVE_ITEM ASP3_SYSSVC_TARGET_C_FILES
        ${ASP3_ROOT_DIR}/arch/riscv_gcc/esp32c3/esp32c3_usbjtag.c)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/esp32c3_usbjtag_hal.c)
endif()

#
#  QEMUによる実行（cmake --build <dir> --target run）
#
#  Espressif版QEMU（esp32c3マシンを持つフォーク）が必要．PATHにない
#  場合は -DQEMU_SYSTEM_RISCV32_ESP=/path/to/qemu-system-riscv32 で
#  指定する．QEMUはELFではなくフラッシュイメージ（asp_flash.bin＝
#  ポストビルドで生成．run.cmake参照）から起動する．
#
if(ESP32C3_QEMU)
    set(QEMU_SYSTEM_RISCV32_ESP qemu-system-riscv32
        CACHE STRING "Path to Espressif qemu-system-riscv32 (esp32c3 machine)")
    set(ASP3_RUN_COMMAND
        ${QEMU_SYSTEM_RISCV32_ESP} -M esp32c3 -nographic
        -drive file=${CMAKE_BINARY_DIR}/asp_flash.bin,if=mtd,format=raw
        -semihosting
    )
else()
    #
    #  実機への書込み（cmake --build <dir> --target run）．
    #  同じasp_flash.bin（Direct Boot形式）をesptoolでフラッシュ先頭へ
    #  書き込む．コンソールは書込みと同じUSBポート（USB Serial/JTAG＝
    #  /dev/ttyACM*）に出る（ESP32C3_CONSOLE=usbjtag時）．
    #
    set(ESP32C3_ESPTOOL esptool
        CACHE STRING "Path to esptool")
    set(ESP32C3_PORT /dev/ttyACM0
        CACHE STRING "Serial port of the ESP32-C3 board")
    set(ASP3_RUN_COMMAND
        ${ESP32C3_ESPTOOL} --chip esp32c3 --port ${ESP32C3_PORT}
        write-flash 0x0 ${CMAKE_BINARY_DIR}/asp_flash.bin
    )
endif()

#
#  Wi-Fi（esp_wifi blob＋os_adapter shim．既定OFF＝素のASP3ターゲット）
#
#  ONにすると，shim基盤（wifi/esp_shim.*＝静的プールとプリミティブ）と
#  esp_wifi.cmake（NuttX Wireless.mk移植＝wpa_supplicant/mbedtls/blob
#  リンク）を取り込む．経緯はdocs/wifi-shim.md．
#
option(ESP32C3_WIFI "Enable Wi-Fi (esp_wifi blob + os_adapter shim)" OFF)

#
#  shim基盤（wifi/esp_shim.*）はWi-Fi固有ではなく，ASP3静的プール上に
#  FreeRTOS風プリミティブ（sem/mutex/queue/task/timer/malloc）を提供
#  する汎用層（esp_shim.h先頭コメント参照）．Bluetooth統合（Phase D．
#  docs/dev/esp-idf-integration.md）もこれを再利用するため，
#  ESP32C3_WIFIとは独立にESP32C3_BTからも取り込めるよう分離する
#  （Wi-Fi固有のosi/coex/eventアダプタ層は従来通りESP32C3_WIFI限定）．
#
if(ESP32C3_WIFI OR ESP32C3_BT)
    list(APPEND ASP3_INCLUDE_DIRS ${TARGETDIR}/wifi)
    list(APPEND ASP3_CFG_FILES ${TARGETDIR}/wifi/esp_shim.cfg)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_shim.c
        ${TARGETDIR}/wifi/esp_shim_libc.c
        ${TARGETDIR}/wifi/esp_shim_blobglue.c
    )
endif()

if(ESP32C3_WIFI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_WIFI)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_wifi_adapter.c
        ${TARGETDIR}/wifi/esp_event_shim.c
        ${TARGETDIR}/wifi/esp_coex_adapter.c
    )
endif()
include(${TARGETDIR}/esp_wifi.cmake)

#
#  Bluetooth（BLE．NimBLE＋os_adapter shim．既定OFF）
#
#  Phase D-1＝コントローラ起動＋VHCIループバック（ホストスタック無し）．
#  RAM予算のためWi-Fiとの同時ONは現時点で未対応（要求はしない．
#  docs/dev/esp-idf-integration.md Phase D参照）．
#
option(ESP32C3_BT "Enable Bluetooth (BT controller + freertos shim, Phase D-1)" OFF)
if(ESP32C3_BT)
    if(ESP32C3_WIFI)
        message(FATAL_ERROR "ESP32C3_BT + ESP32C3_WIFI is not supported yet (RAM budget; Phase D-1 is BT-only)")
    endif()
endif()
include(${TARGETDIR}/esp_bt.cmake)

#
#  TCP/IP統合（lwIP．Wi-Fi必須＝ESP32C3_WIFIが前提）
#
#  lwIP（submodule）はNO_SYS=0（BSDソケット／netconn API）で使用する．
#  lwIP自身が生成する唯一のスレッド（tcpip_thread）はcfg生成の
#  NET_TSK（port/sys_arch.c参照）に割り当て，netif/配下のnetifドライバ
#  （esp_wifi_internal_tx/reg_rxcb上のethernet netif）と組み合わせる．
#  経緯・設計はdocs/tcpip-integration.md．
#
option(ESP32C3_LWIP "Integrate lwIP (TCP/IP + BSD sockets, requires ESP32C3_WIFI)" OFF)
if(ESP32C3_LWIP)
    if(NOT ESP32C3_WIFI)
        message(FATAL_ERROR "ESP32C3_LWIP requires ESP32C3_WIFI=ON")
    endif()

    get_filename_component(LWIP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../lwip ABSOLUTE)
    include(${LWIP_DIR}/src/Filelists.cmake)

    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_LWIP)

    list(APPEND ASP3_INCLUDE_DIRS
        ${LWIP_DIR}/src/include
        ${LWIP_DIR}/contrib/apps/ping
        ${LWIP_DIR}/contrib/apps/tcpecho_raw
        ${TARGETDIR}/net/port/include
        ${TARGETDIR}/net
    )

    list(APPEND ASP3_CFG_FILES ${TARGETDIR}/net/net.cfg)

    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${lwipcore_SRCS}
        ${lwipcore4_SRCS}
        ${lwipapi_SRCS}
        ${LWIP_DIR}/src/netif/ethernet.c
        ${LWIP_DIR}/contrib/apps/ping/ping.c
        ${LWIP_DIR}/contrib/apps/tcpecho_raw/tcpecho_raw.c
        ${TARGETDIR}/net/port/sys_arch.c
        ${TARGETDIR}/net/netif_esp32c3.c
    )
endif()

#
#  esp_rom_set_cpu_ticks_per_us フォールバック（WiFi/BT両OFF時のリンク不可修正）
#
#  target_kernel_impl.c の hardware_init_hook が無条件で呼ぶROM関数
#  esp_rom_set_cpu_ticks_per_us()（実体はROM関数ets_update_cpu_frequency
#  へのPROVIDEエイリアス．esp32c3.rom.ld＋esp32c3.rom.api.ldが供給）は，
#  従来 ESP32C3_WIFI/ESP32C3_BT ON時のみesp_wifi.cmake/esp_bt.cmake経由で
#  -Wl,-T注入されていたため，素の sample1／test_porting（WiFi/BT両OFF）が
#  未定義参照でリンク不可だった．WiFi/BT両OFF時に限り同じ2ファイルを
#  直接注入する（ON時は既にesp_wifi.cmake/esp_bt.cmakeが積むため二重
#  処理を避ける．esp_wifi.cmake/esp_bt.cmakeが積むROM ld一式のうち，
#  本シンボルの供給に必要な最小の2ファイルのみを選ぶ）．
#
if(NOT (ESP32C3_WIFI OR ESP32C3_BT))
    list(APPEND ASP3_LINK_OPTIONS
        -Wl,-T,${ESP_HAL_DIR}/components/esp_rom/esp32c3/ld/esp32c3.rom.ld
        -Wl,-T,${ESP_HAL_DIR}/components/esp_rom/esp32c3/ld/esp32c3.rom.api.ld
    )
endif()

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
