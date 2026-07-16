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
#  ------------------------------------------------------------------
#  供給元（supply root）の選択＝HAL依存撤去
#  （.steering/20260716-c3c5c6-esp-idf-supply-migration．C5の
#   esp32c5_espidf/target.cmake で確立した型の転写＝新規設計ではない）
#  ------------------------------------------------------------------
#
#  ASP3_ESPIDF_SUPPLY=ON（既定）＝ESP-IDF submodule（v5.5.4タグ＝
#  735507283d）から全ESPコンポーネント（ヘッダ・ソース・blob・ROM ld）を
#  供給する。OFF＝従来のesp-hal-3rdparty（hal submodule）＝可逆fallback。
#
#  ★実測に基づく前提（evidence-c5-02＝C5で確立．C6でも同一構造）：
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
    #  従来C6は外部絶対パス /home/honda/tools/esp-idf を参照していたが，これは
    #  (1) このマシン固有＝再現性が無く，(2) 変数名 `IDF_V554` に反して
    #  **v5.5.4タグではない**（version.hが5.5.4系を表示するため気づきにくい）。
    #  実測（evidence-c6-01 §1）：本PCの ~/tools/esp-idf は **v5.5(=v5.5.0,
    #  8c750b08) の shallow clone**＝C6のBT/WiFi blobはv5.5.4タグとも
    #  v6.1とも全て相違し，**どのラウンドでも検証されていない版**だった。
    #  外部ターゲット規約（PORTING_GUIDE.md §6）に従い CMAKE_CURRENT_LIST_DIR 相対。
    #  A/B用に -DIDF_V554=<path> で従来の外部treeへ差し戻せる（可逆）。
    get_filename_component(IDF_V554 ${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf ABSOLUTE)
endif()

option(ASP3_ESPIDF_SUPPLY
    "Supply ESP components (headers/sources/blobs/ROM ld) from the esp-idf submodule (v5.5.4 tag) instead of esp-hal-3rdparty. Default ON = HAL-free. OFF = hal fallback (reversible)"
    ON)

if(ASP3_ESPIDF_SUPPLY)
    set(ESP_SUP_DIR ${IDF_V554})
    #  供給元の版差を共有ソース（C3/C5と共用のshim等）で吸収するための
    #  ガード。S3(LX6/LX7)・C5の同名ガードと同じ役割・命名。
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESPIDF_SUPPLY=1)
else()
    set(ESP_SUP_DIR ${ESP_HAL_DIR})
endif()

#
#  esp-hal-3rdpartyが分割した`esp_hal_<x>`コンポーネントの供給元別パス。
#  esp-idfでは全て`components/hal`に集約されている。いずれの供給元でも
#  `${ESP_SUP_HAL_<x>}/include` と `${ESP_SUP_HAL_<x>}/esp32c6/include` の
#  2パターンで参照できる形に揃える（C5 target.cmake と同一）。
#
foreach(_esp_hal_c clock timg rtc_timer pmu gpio security ana_conv usb)
    if(ASP3_ESPIDF_SUPPLY)
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_SUP_DIR}/components/hal)
    else()
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_HAL_DIR}/components/esp_hal_${_esp_hal_c})
    endif()
endforeach()

#
#  sdkconfig.h の供給元。esp-idf では Kconfig 生成物のため
#  チェックアウトに存在しない＝本ディレクトリへ vendor したものを使う
#  （sdkconfig_stub/sdkconfig.h＝hal の nuttx/esp32c6 版の verbatim コピー．
#  CONFIG_* は1ビットも変えていない＝供給移行で設定を同時に動かさない）。
#  hal fallback 時は従来どおり hal の nuttx/ を直接参照する。
#
if(ASP3_ESPIDF_SUPPLY)
    set(ESP_SUP_SDKCONFIG_DIR ${CMAKE_CURRENT_LIST_DIR}/sdkconfig_stub)
else()
    set(ESP_SUP_SDKCONFIG_DIR ${ESP_HAL_DIR}/nuttx/esp32c6/include)
endif()

#
#  hal_stub（libc互換ヘッダ．ツールチェーンにnewlib実体が無い環境向け）
#  はESP32-C3用のものをそのまま再利用する（チップ非依存＝トゥール
#  チェーンのギャップを埋めるだけの内容．esp32c3_espidf/hal_stub/
#  README相当のコメントは各ヘッダ先頭参照）．
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

list(APPEND ASP3_INCLUDE_DIRS
    ${C3_TARGETDIR}/hal_stub/include
    ${ESP_SUP_DIR}/components/hal/esp32c6/include
    ${ESP_SUP_DIR}/components/hal/include
    ${ESP_SUP_DIR}/components/hal/platform_port/include
    ${ESP_SUP_HAL_usb}/esp32c6/include
    ${ESP_SUP_HAL_usb}/include
    ${ESP_SUP_DIR}/components/soc/esp32c6/include
    ${ESP_SUP_DIR}/components/soc/esp32c6/register
    ${ESP_SUP_DIR}/components/soc/include
    ${ESP_SUP_DIR}/components/esp_common/include
    ${ESP_SUP_SDKCONFIG_DIR}
    #  esp_rom_sys.h（esp_rom_set_cpu_ticks_per_us宣言．target_kernel_impl.c
    #  が無条件で使用）。従来はWi-Fi/BT限定（esp_wifi.cmake/esp_bt.cmake内）
    #  でしか積んでおらず，WiFi/BT両OFFの素のビルド（sample1/test_porting）
    #  だと本ヘッダがfatal errorで欠落するbuild hygiene上の不具合だった
    #  （C5のtarget.cmakeで先に気付き修正済み——同じギャップがC6にも
    #  共通してあったため合わせて修正．esp_rom_set_cpu_ticks_per_us自体の
    #  リンク側フォールバックは本ファイル末尾を参照）。
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_rom/esp32c6/include
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
#  TOPPERS_SUPPORT_TLS：タスク実行開始時(start_r)のTLS(スレッドローカル
#    ストレージ)初期化(tp設定)を有効化．picolibcのrand()等TLS依存libc
#    関数を使うとtp未初期化(=0)でLoad access faultになるため常時有効
#    （詳細はasp3_coreのarch/riscv_gcc/esp32c6/chip_asm.incの
#    init_additional_regs_start_r参照）．
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
    -L${ESP_SUP_DIR}/components/soc/esp32c6/ld
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

#
#  Bluetooth（BLE．esp32c6/c5世代コントローラ＋platform/os.hシム．
#  既定OFF．Phase D-1＝controller init+VHCI，BLE実施01）
#
#  ESP32C3_BTと同じ理由でESP32C6_WIFIとの同時ONは現状未対応（RAM予算．
#  esp_bt.cmake参照）．shim基盤（wifi/esp_shim.[ch]／
#  esp_shim_blobglue.c）はWi-Fi・BT共有のためESP32C6_WIFI単独ゲートから
#  (ESP32C6_WIFI OR ESP32C6_BT)へ拡張する（C3のtarget.cmakeと同じ
#  パターン．docs/bt-shim.md「target.cmake」節）．
#
option(ESP32C6_BT "Enable Bluetooth (BLE embedded controller V1 + platform/os.h shim, Phase D-1)" OFF)
if(ESP32C6_BT AND ESP32C6_WIFI)
    message(FATAL_ERROR "ESP32C6_BT + ESP32C6_WIFI is not supported yet (RAM budget; C3の前例踏襲)")
endif()

option(ESP32C6_WIFI "Enable Wi-Fi (esp_wifi blob + os_adapter shim, Phase B-2a scan)" OFF)
if(ESP32C6_WIFI OR ESP32C6_BT)
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
        #  でWiFi専用の未参照部分は落ちる．BLE実施01で確認）．
        ${TARGETDIR}/wifi/esp_shim_blobglue.c
        ${C3_TARGETDIR}/wifi/esp_shim_libc.c
    )
endif()
if(ESP32C6_WIFI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_WIFI)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${TARGETDIR}/wifi/esp_wifi_adapter.c
        ${C3_TARGETDIR}/wifi/esp_event_shim.c
        ${C3_TARGETDIR}/wifi/esp_coex_adapter.c
        #  DIAGNOSTIC (temporary, RX-enable --wrap trace)．
        #  docs/wifi-shim-c6.md「実施12」参照．調査終了後に削除する．
        ${TARGETDIR}/wifi/wifi_trace.c
    )
endif()
include(${TARGETDIR}/esp_wifi.cmake)
include(${TARGETDIR}/esp_bt.cmake)

#
#  TCP/IP統合（lwIP．Wi-Fi必須＝ESP32C6_WIFIが前提。実施89）
#
#  net/層（sys_arch・netif・lwipopts等）はチップ非依存（esp_wifi_
#  internal_tx/reg_rxcb／esp_read_mac等のblob APIのみに依存し，
#  C6固有のレジスタ・アドレスには一切触れない）ため，C3側
#  （${C3_TARGETDIR}/net）をコピーせずそのまま再利用する．
#  esp_shim_libc.c等と同じ「chip非依存部はC3_TARGETDIRから直接取込む」
#  既存パターンを踏襲（docs/tcpip-integration.md，docs/wifi-shim-c6.md
#  実施89）．
#
option(ESP32C6_LWIP "Integrate lwIP (TCP/IP + BSD sockets, requires ESP32C6_WIFI)" OFF)
if(ESP32C6_LWIP)
    if(NOT ESP32C6_WIFI)
        message(FATAL_ERROR "ESP32C6_LWIP requires ESP32C6_WIFI=ON")
    endif()

    get_filename_component(LWIP_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../lwip ABSOLUTE)
    include(${LWIP_DIR}/src/Filelists.cmake)

    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_LWIP)

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
#  esp_rom_set_cpu_ticks_per_us フォールバック（WiFi/BT両OFF時のリンク不可修正）
#
#  target_kernel_impl.c の hardware_init_hook が無条件で呼ぶROM関数
#  esp_rom_set_cpu_ticks_per_us()（実体はROM関数ets_update_cpu_frequency
#  へのPROVIDEエイリアス．esp32c6.rom.ld＋esp32c6.rom.api.ldが供給）は，
#  従来 ESP32C6_WIFI/ESP32C6_BT ON時のみesp_wifi.cmake/esp_bt.cmake経由で
#  -Wl,-T注入されていたため，素の sample1／test_porting（WiFi/BT両OFF）が
#  未定義参照でリンク不可だった（C3/C5と共通の既存不具合）．WiFi/BT両OFF
#  時に限り同じ2ファイルを直接注入する（ON時は既にesp_wifi.cmake/
#  esp_bt.cmake（またはesp_bt_idf61.cmake）が積むため二重処理を避ける）。
#
if(NOT (ESP32C6_WIFI OR ESP32C6_BT))
    list(APPEND ASP3_LINK_OPTIONS
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c6/ld/esp32c6.rom.ld
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c6/ld/esp32c6.rom.api.ld
    )
endif()

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
