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
#  Wi-Fi統合（wifi_v8/・esp_wifi_v8.cmake）はフェーズ2b（B-2a．docs/
#  c5-port-design.md §5.4・§6）で実装済み。既定はOFF（-DESP32C5_WIFI=ON
#  で有効化）。ESP32C5_WIFIブロックの中身がesp_wifi_v8.cmakeをincludeする。
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
#  ------------------------------------------------------------------
#  供給元（supply root）の選択＝HAL依存撤去
#  （.steering/20260716-c3c5c6-esp-idf-supply-migration）
#  ------------------------------------------------------------------
#
#  ASP3_ESPIDF_SUPPLY=ON（既定）＝ESP-IDF submodule（v5.5.4タグ＝
#  735507283d）から全ESPコンポーネント（ヘッダ・ソース・blob・ROM ld）を
#  供給する。OFF＝従来のesp-hal-3rdparty（hal submodule）＝可逆fallback。
#
#  ★実測に基づく前提（evidence-c5-02）：esp-hal-3rdpartyは
#  **ESP-IDFの再パッケージ**であり，本ターゲットが参照する75パスのうち
#  **72パスがesp-idf v5.5.4へ1:1で対応**する（差は下記2点のみ）：
#    1. halは IDF の単体`hal`コンポーネントを`esp_hal_*`へ**分割**している
#       （esp_hal_clock/timg/rtc_timer/pmu/gpio/security/ana_conv/usb）。
#       esp-idf側では全て `components/hal` に集約＝下の ESP_SUP_HAL_* で吸収。
#    2. mbedtls が **4.0.0(tf-psa-crypto分離) → 3.6.5(classic)** の版差
#       （esp_wifi_v8.cmake §3で吸収）。
#  加えて hal の nuttx/（NuttXシムconfig）はesp-idfに存在しない＝
#  ESP-IDF本来の mbedtls port（esp_config.h）へ寄せる。
#
#  ★ヘッダとソースは**必ず揃えて**同じ供給元から取る。片方だけ移すと
#  esp-hal-3rdpartyのリネーム（実測：`hal/lp_timer_hal.h`→`rtc_timer_hal.h`，
#  `hal/timer_ll.h`→`timg_ll.h`，`soc/clkout_channel.h`→`hal/clkout_channel.h`）
#  が未解決参照として噴出する。逆に揃えれば**リネーム問題は消滅する**
#  （両ツリーの当該ソースは実測でリネーム以外同一）。
#
if(NOT DEFINED IDF_V554)
    #  外部ターゲット規約（PORTING_GUIDE.md §6）に従い CMAKE_CURRENT_LIST_DIR 相対。
    #  A/B用に -DIDF_V554=<path> で別treeへ差し戻せる（可逆）。
    get_filename_component(IDF_V554 ${CMAKE_CURRENT_LIST_DIR}/../../../esp-idf ABSOLUTE)
endif()

option(ASP3_ESPIDF_SUPPLY
    "Supply ESP components (headers/sources/blobs/ROM ld) from the esp-idf submodule (v5.5.4 tag) instead of esp-hal-3rdparty. Default ON = HAL-free. OFF = hal fallback (reversible)"
    ON)

if(ASP3_ESPIDF_SUPPLY)
    set(ESP_SUP_DIR ${IDF_V554})
    #  供給元の版差を共有ソース（C3/C6と共用のshim等）で吸収するための
    #  ガード。S3(LX6/LX7)の同名ガードと同じ役割・命名。
    #  既知の版差（実測）：
    #    esp_event_post() の event_data が hal=`void *` /
    #    esp-idf v5.5.4=`const void *`（esp_event.h）。
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESPIDF_SUPPLY=1)
else()
    set(ESP_SUP_DIR ${ESP_HAL_DIR})
endif()

#
#  esp-hal-3rdpartyが分割した`esp_hal_<x>`コンポーネントの供給元別パス。
#  esp-idfでは全て`components/hal`に集約されている（実測：
#  esp_hal_*配下から実際にincludeされるヘッダ16本はすべて
#  esp-idfの components/hal/{include,esp32c5/include} 及び
#  components/soc/esp32c5/include で解決する）。
#  いずれの供給元でも `${ESP_SUP_HAL_<x>}/include` と
#  `${ESP_SUP_HAL_<x>}/esp32c5/include` の2パターンで参照できる形に揃える。
#
foreach(_esp_hal_c clock timg rtc_timer pmu gpio security ana_conv usb)
    if(ASP3_ESPIDF_SUPPLY)
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_SUP_DIR}/components/hal)
    else()
        set(ESP_SUP_HAL_${_esp_hal_c} ${ESP_HAL_DIR}/components/esp_hal_${_esp_hal_c})
    endif()
endforeach()

#
#  hal_stub（libc互換ヘッダ．ツールチェーンにnewlib実体が無い環境向け）
#  はESP32-C3用のものをそのまま再利用する（チップ非依存＝トゥール
#  チェーンのギャップを埋めるだけの内容）。
#
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

list(APPEND ASP3_INCLUDE_DIRS
    ${C3_TARGETDIR}/hal_stub/include
    ${TARGETDIR}/sdkconfig_stub
    ${ESP_SUP_DIR}/components/hal/esp32c5/include
    ${ESP_SUP_DIR}/components/hal/include
    ${ESP_SUP_DIR}/components/hal/platform_port/include
    ${ESP_SUP_HAL_usb}/esp32c5/include
    ${ESP_SUP_HAL_usb}/include
    ${ESP_SUP_DIR}/components/soc/esp32c5/include
    ${ESP_SUP_DIR}/components/soc/esp32c5/register
    ${ESP_SUP_DIR}/components/soc/include
    ${ESP_SUP_DIR}/components/esp_common/include
    #  esp_rom_sys.h（esp_rom_set_cpu_ticks_per_us宣言．
    #  target_kernel_impl.cが使用）。C6版のtarget.cmakeはこれを
    #  Wi-Fi限定（esp_wifi.cmake内）でしか積んでおらず，Wi-Fi無し
    #  ビルドでは本来ここが欠落する潜在バグだった（本ポートで気付いた
    #  ため無条件で積む．C5固有の問題ではなくC6にも共通する既存の
    #  ギャップ）。
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_rom/esp32c5/include
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
    -L${ESP_SUP_DIR}/components/soc/esp32c5/ld
)

#
#  ブート方式（既定＝Direct Boot．evidence-c5-03参照）
#
#  OFF（既定）＝Direct Boot XIP：ROMがflashをセルフマップしてASP3の
#               エントリへ直行する（2nd-stage bootloader無し）。
#               scan 20AP・W1（GOT IP＋ping×30）を実機達成済みの構成。
#  ON          ＝seam：実ESP-IDF v5.5.4 の C5用 2nd-stage bootloader を
#               経由してASP3自前エントリへ入る（FreeRTOS非リンクのまま）。
#               ★C5のbootloaderオフセットは 0x2000（0x0ではない．
#               Kconfig.projbuild:11「default 0x2000 if IDF_TARGET_ESP32P4
#               || IDF_TARGET_ESP32C5 || IDF_TARGET_ESP32H4」＝先頭2セクタは
#               key manager用に予約）。ptable@0x8000／app@0x10000。
#
#  可逆：ON/OFFの切替はld・app_desc・イメージ生成だけに閉じており，
#  カーネル／arch／チップ依存部／Wi-Fi供給には一切触れない。
#
option(ASP3_SEAM_BOOT
    "Boot via the real ESP-IDF v5.5.4 2nd-stage bootloader (seam) instead of ASP3's Direct Boot. Default OFF = Direct Boot (the only configuration with scan/W1 hardware evidence)"
    OFF)

if(ASP3_SEAM_BOOT)
    set(ASP3_LDSCRIPT ${TARGETDIR}/esp32c5_seam.ld)
else()
    set(ASP3_LDSCRIPT ${TARGETDIR}/esp32c5.ld)
endif()

#
#  ターゲット依存部のソース
#
list(APPEND ASP3_TARGET_C_FILES
    ${TARGETDIR}/target_kernel_impl.c
    ${TARGETDIR}/target_timer.c
    ${TARGETDIR}/flash_header.S
)

#
#  seam では esp_app_desc（segment #0先頭）が要る．Direct Boot では
#  未参照＝リンクされない（非回帰）．理由は seam_appdesc.c の冒頭コメント．
#
if(ASP3_SEAM_BOOT)
    list(APPEND ASP3_TARGET_C_FILES
        ${TARGETDIR}/seam_appdesc.c
    )
    #
    #  ★-u が必須：ASP3_TARGET_C_FILES は静的ライブラリ（libasp3.a）へ
    #  入るため，どこからも参照されない app_desc は**アーカイブメンバごと
    #  ロードされない**（ldスクリプトの KEEP はロード済みセクションの
    #  gc を止めるだけで，未ロードのメンバは救えない）。実測：-u 無しでは
    #  nm に asp3_seam_app_desc が現れず，.flash.appdesc の先頭が rodata に
    #  なって elf2image が
    #    「Contents of segment at SHA256 digest offset 0xb0 are not zero」
    #  で停止した（0xb0 = ファイル先頭0x20 + app_elf_sha256 offset 144）。
    #
    list(APPEND ASP3_LINK_OPTIONS
        -Wl,-u,asp3_seam_app_desc
    )
endif()

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

#
#  hal(v8)統一＝唯一の Wi-Fi 実装（docs/c5-bringup.md 実施48〜51，ユーザー決定）：
#  ESP32C5_WIFI=ON は esp-hal-3rdparty(hal submodule) の v8 blob
#  （esp_wifi_v8.cmake ＋ wifi_v8/）を使う。v8 は ESP_HAL_DIR（submodule）
#  基点で **IDFローカルパス非依存**＝どのPC/CI でもビルドできる（統一の主目的）。
#  実施48で 2.4GHz scan，実施50で 2.4GHz connect→DHCP，実施51で 5GHz(ch40)
#  connect→DHCP を実機実証済み（実施09の「v8 eco2非互換」判定はクロック鎖＋
#  APM交絡による誤判定と確定）。
#
#  かつての opt-in fallback だった v9（IDF v6.1 blob ＝ esp_wifi.cmake ＋ wifi/）
#  は，`/home/honda/tools/esp-idf-v6.1` へのローカルパスハードコード依存で
#  ポータビリティを損ねるため **削除**した（実施52）。v9 の履歴は git 及び
#  docs/c5-bringup.md（実施08〜49）に残る。BT単体ビルドも wifi_v8/ を使う。
#
option(ESP32C5_WIFI "Enable Wi-Fi (esp_wifi blob + os_adapter shim; hal(v8), IDF-independent. Phase B-2a scan 〜 実施51 5GHz)" OFF)
set(ESP32C5_WIFI_CMAKE_FILE ${TARGETDIR}/esp_wifi_v8.cmake)
set(ESP32C5_WIFI_SRCDIR ${TARGETDIR}/wifi_v8)
if(ESP32C5_WIFI OR ESP32C5_BT)
    if(ESP32C5_WIFI AND NOT EXISTS ${ESP32C5_WIFI_CMAKE_FILE})
        message(FATAL_ERROR
            "ESP32C5_WIFI=ON was requested, but ${ESP32C5_WIFI_CMAKE_FILE} "
            "was not found. See docs/c5-port-design.md.")
    endif()
    list(APPEND ASP3_INCLUDE_DIRS
        ${ESP32C5_WIFI_SRCDIR}
        ${C3_TARGETDIR}
        ${C3_TARGETDIR}/wifi
    )
    list(APPEND ASP3_CFG_FILES ${C3_TARGETDIR}/wifi/esp_shim.cfg)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${ESP32C5_WIFI_SRCDIR}/esp_shim.c
        #  esp_shim_blobglue.cはWiFi blob（net80211/pp/core）専用の
        #  グルーが大半だが，esp_sleep_pd_config／esp_sleep_clock_config／
        #  esp_deep_sleep_register_phy_hook／_esp_error_check_failed等
        #  BTも要求する汎用スタブ（modem_clock.c／phy_init.cが参照）を
        #  同居させているため，BT単体ビルドでもリンクする（--gc-sections
        #  でWiFi専用の未参照部分は落ちる．C6のBLE実施01で確認済みの
        #  パターンをC5でも踏襲．v8統一後は WiFi/BT いずれのビルドでも
        #  ESP32C5_WIFI_SRCDIR=wifi_v8/ で一本化）．
        ${ESP32C5_WIFI_SRCDIR}/esp_shim_blobglue.c
        ${C3_TARGETDIR}/wifi/esp_shim_libc.c
    )
endif()
if(ESP32C5_WIFI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_WIFI)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${ESP32C5_WIFI_SRCDIR}/esp_wifi_adapter.c
        ${C3_TARGETDIR}/wifi/esp_event_shim.c
        ${C3_TARGETDIR}/wifi/esp_coex_adapter.c
    )

    include(${ESP32C5_WIFI_CMAKE_FILE})
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
#  esp_rom_set_cpu_ticks_per_us フォールバック（WiFi/BT両OFF時のリンク不可修正）
#
#  target_kernel_impl.c の hardware_init_hook が無条件で呼ぶROM関数
#  esp_rom_set_cpu_ticks_per_us()（実体はROM関数ets_update_cpu_frequency
#  へのPROVIDEエイリアス．esp32c5.rom.ld＋esp32c5.rom.api.ldが供給）は，
#  従来 ESP32C5_WIFI/ESP32C5_BT ON時のみesp_wifi_v8.cmake/esp_bt.cmake経由
#  で-Wl,-T注入されていたため，素の sample1／test_porting（WiFi/BT両OFF）
#  が未定義参照でリンク不可だった（C3と共通の既存不具合．target.cmake
#  上部のESP_ROM_SYS.H includeコメント参照——インクルードパスは既に無
#  条件化済みだったが，リンク側は未修正だった）．WiFi/BT両OFF時に限り
#  同じ2ファイルを直接注入する（ON時は既にesp_wifi_v8.cmake/esp_bt.cmake
#  が積むため二重処理を避ける）．
#
if(NOT (ESP32C5_WIFI OR ESP32C5_BT))
    list(APPEND ASP3_LINK_OPTIONS
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c5/ld/esp32c5.rom.ld
        -Wl,-T,${ESP_SUP_DIR}/components/esp_rom/esp32c5/ld/esp32c5.rom.api.ld
    )
endif()

#
#  フラッシュイメージ生成等（aspターゲット定義後に取込み）
#
set(ASP3_TARGET_RUN_CMAKE ${TARGETDIR}/run.cmake)
