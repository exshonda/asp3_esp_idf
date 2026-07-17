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

#
#  ------------------------------------------------------------------
#  供給元（supply root）の選択＝HAL依存撤去
#  （.steering/20260716-c3c5c6-esp-idf-supply-migration．C5の
#   esp32c5_espidf/target.cmake で確立し C6 が転写した型の再転写＝
#   新規設計ではない）
#  ------------------------------------------------------------------
#
#  ASP3_ESPIDF_SUPPLY=ON（既定）＝ESP-IDF submodule（v5.5.4タグ＝
#  735507283d）から全ESPコンポーネント（ヘッダ・ソース・blob・ROM ld）を
#  供給する。OFF＝従来のesp-hal-3rdparty（hal submodule）＝可逆fallback。
#
#  ★実測に基づく前提（evidence-c5-02＝C5で確立．C6・C3でも同一構造）：
#  esp-hal-3rdpartyは**ESP-IDFの再パッケージ**であり，差は本質的に2点：
#    1. halは IDF の単体`hal`コンポーネントを`esp_hal_*`へ**分割**している
#       （esp_hal_clock/timg/rtc_timer/pmu/gpio/security/ana_conv/usb）。
#       esp-idf側では全て `components/hal` に集約＝下の ESP_SUP_HAL_* で吸収。
#    2. mbedtls が **4.0.0(tf-psa-crypto分離) → 3.6.5(classic)** の版差
#       （esp_wifi.cmake §3で吸収）。
#  加えて hal の nuttx/（NuttXシムconfig）はesp-idfに存在しない：
#    - sdkconfig.h は本ディレクトリへ vendor 済（sdkconfig_stub/．冒頭注記参照）
#    - mbedtls config は ESP-IDF本来の port（esp_config.h）へ寄せる
#
#  ★ヘッダとソースは**必ず揃えて**同じ供給元から取る。片方だけ移すと
#  esp-hal-3rdpartyのリネームが未解決参照として噴出する。逆に揃えれば
#  **リネーム問題は消滅する**（evidence-c5-02 §1.2 の一般則）。
#
if(NOT DEFINED IDF_V554)
    #  ★esp-idf submodule（v5.5.4タグ＝735507283d）をリポジトリ同梱で参照する。
    #  従来C3は esp_wifi.cmake:222／esp_bt.cmake:53 で外部絶対パス
    #  /home/honda/tools/esp-idf を参照していたが，これは
    #  (1) このマシン固有＝再現性が無く，(2) 変数名 `IDF_V554` に反して
    #  **v5.5.4タグではない**（version.hが5.5.4系を表示するため気づきにくい）。
    #  ★本PCでの実測（evidence-c3-01 §1．C6 evidence-c6-01 §1 と同じ罠）：
    #    ~/tools/esp-idf         = v5.5（=v5.5.0, 8c750b08）の shallow clone
    #    repo submodule esp-idf/ = **v5.5.4 タグ（735507283d）＝真のv5.5.4**
    #  ⇒ **同じパス名がPCごとに別の版を指す**（HANDOFF §3-1 は +1169 と記録＝
    #  本PCには存在しない第3の版）。外部絶対パスを撤去して submodule 相対に
    #  するのはこのため（外部ターゲット規約＝PORTING_GUIDE.md §6）。
    #  A/B用に -DIDF_V554=<path> で別treeへ差し戻せる（可逆）。
    get_filename_component(IDF_V554 ${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf ABSOLUTE)
endif()

#
#  ★2026-07-17：BT も esp-idf 供給へ移行したため **BT 例外は撤去**した。
#
#  以前は「`ESP32C3_BT=ON` のときだけ既定 OFF」としていた。理由は
#  **基盤だけ esp-idf へ移すと供給元の «混成» が生じて破綻する**
#  （実測＝`shared_periph_module_t`／`soc_root_clk_circuit_t` 未定義）
#  ことにあり，当時は `esp_bt.cmake` の `ESP_HAL_DIR` 35箇所が未移行だった。
#  ⇒ 本ラウンドで **C5（`esp_bt.cmake` の `ESP_HAL_DIR`＝0箇所）の型を転写**して
#     BT ツリーごと移行したので，**混成そのものが起きなくなった**＝例外が不要になった。
#  ⇒ 供給元の食い違いは `esp_bt.cmake` 側で **FATAL_ERROR** として構造的に禁止する
#     （`ASP3_BT_IDF_V554` は本 option に追従する）。
#
option(ASP3_ESPIDF_SUPPLY
    "Supply ESP components (headers/sources/blobs/ROM ld) from the esp-idf submodule (true v5.5.4 tag) instead of esp-hal-3rdparty. Default ON = HAL-free (WiFi / BT / plain builds). OFF = hal fallback (reversible). ASP3_BT_IDF_V554 follows this so the base and the BT tree never mix"
    ON)

if(ASP3_ESPIDF_SUPPLY)
    set(ESP_SUP_DIR ${IDF_V554})
    #  供給元の版差を共有ソース（shim等）で吸収するためのガード。
    #  S3(LX6/LX7)・C5・C6 の同名ガードと同じ役割・命名。
    #  既知の版差（実測）：esp_event_post() の event_data が
    #  hal=`void *` / esp-idf v5.5.4=`const void *`（esp_event.h）。
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESPIDF_SUPPLY=1)
else()
    set(ESP_SUP_DIR ${ESP_HAL_DIR})
endif()

#
#  esp-hal-3rdpartyが分割した`esp_hal_<x>`コンポーネントの供給元別パス。
#  esp-idfでは全て`components/hal`に集約されている。
#  いずれの供給元でも `${ESP_SUP_HAL_<x>}/include` と
#  `${ESP_SUP_HAL_<x>}/esp32c3/include` の2パターンで参照できる形に揃える。
#
foreach(_esp_hal_c clock timg rtc_timer pmu gpio security ana_conv usb)
    if(ASP3_ESPIDF_SUPPLY)
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_SUP_DIR}/components/hal)
    else()
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_HAL_DIR}/components/esp_hal_${_esp_hal_c})
    endif()
endforeach()

#
#  sdkconfig.h の供給元（C6 target.cmake の ESP_SUP_SDKCONFIG_DIR と同型）。
#  sdkconfig.h は本来 Kconfig 生成物で **esp-idf のチェックアウトには
#  存在しない**ため，esp-idf供給時は本ディレクトリへ vendor したコピーを
#  使う（sdkconfig_stub/sdkconfig.h＝hal の nuttx/esp32c3 版の verbatim
#  コピー＝CONFIG_* は1ビットも変えていない）。
#  hal fallback 時は従来どおり hal の nuttx/ を直接参照する。
#
if(ASP3_ESPIDF_SUPPLY)
    set(ESP_SUP_SDKCONFIG_DIR ${CMAKE_CURRENT_LIST_DIR}/sdkconfig_stub)
else()
    set(ESP_SUP_SDKCONFIG_DIR ${ESP_HAL_DIR}/nuttx/esp32c3/include)
endif()

list(APPEND ASP3_INCLUDE_DIRS
    ${TARGETDIR}/hal_stub/include
    ${ESP_SUP_SDKCONFIG_DIR}
    ${ESP_SUP_DIR}/components/hal/esp32c3/include
    ${ESP_SUP_DIR}/components/hal/include
    ${ESP_SUP_DIR}/components/hal/platform_port/include
    ${ESP_SUP_HAL_usb}/esp32c3/include
    ${ESP_SUP_HAL_usb}/include
    ${ESP_SUP_DIR}/components/soc/esp32c3/include
    ${ESP_SUP_DIR}/components/soc/esp32c3/register
    ${ESP_SUP_DIR}/components/soc/include
    ${ESP_SUP_DIR}/components/esp_common/include
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
    -L${ESP_SUP_DIR}/components/soc/esp32c3/ld
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
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c3/ld/esp32c3.rom.ld
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c3/ld/esp32c3.rom.api.ld
    )
endif()

#
#  ★注（C5 との差．意図的に「積まない」）：C5 target.cmake は
#  esp_rom_sys.h（esp_rom_set_cpu_ticks_per_us 宣言）を無条件に積む形へ
#  直したが，**C3 は不要**＝target_kernel_impl.c:35 が当該ROM関数を
#  ローカルに `extern` 宣言しており，ヘッダに依存しない（実測）。
#  供給移行のスコープを越えて include を増やすと shadow 事故の面積が
#  広がるだけなので，ここでは積まない。
#

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
