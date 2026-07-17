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
#  ------------------------------------------------------------------
#  ツールチェーン検証（C5 evidence-c5-08 §2.1 からの転写）
#  ------------------------------------------------------------------
#
#  asp3_core の toolchain-riscv64.cmake は既定プレフィクス
#  riscv64-unknown-elf- を PATH 経由で解決するため，
#  -DRISCV64_TOOLCHAIN_PREFIX の渡し忘れが «ビルドは通るのに間違った
#  コンパイラ» を生む（実測：build/ 配下 320 構成のうち 164 構成が
#  Ubuntu汎用GCCでビルドされていた）．
#
#  ★本ラウンドで C6 でも実際に踏んだ：toolchain file を渡し忘れると
#  C5（本検証あり）は configure 時に «-dumpmachine : x86_64-linux-gnu»
#  と即座に落ちたのに対し，C6（本検証なし）は configure が通ってしまい，
#  ビルド途中で «cc: error: unrecognized argument in option -mabi=ilp32»
#  という原因の分かりにくいエラーで落ちた．＝この2行の有無がそのまま
#  «診断可能な失敗» と «謎の失敗» の差になる．
#
#  検証はここ（target.cmake＝本リポジトリ側）で行い，チップ非依存の
#  実体は asp3/cmake/ に置く．C6 の chip.cmake は asp3_core（submodule＝
#  編集禁止）だが，target.cmake は本リポジトリなので submodule を
#  触らずに転写できる．
#
#  推奨する configure：
#    -DCMAKE_TOOLCHAIN_FILE=<repo>/asp3/cmake/toolchain-esp32-riscv32.cmake
#
#  -DASP3_ESP_EXPECTED_TOOLCHAIN=<tag> で意図的に別版を許可できるよう，
#  素の set() ではなく «未定義のときだけ» 既定を与える（素の set() だと
#  コマンドラインの -D を黙って上書きし，エラーメッセージが案内する
#  退避先が «効かない案内＝嘘» になる．C5 で実際に発火させて検出した）．
#
if(NOT DEFINED ASP3_ESP_EXPECTED_TOOLCHAIN)
    set(ASP3_ESP_EXPECTED_TOOLCHAIN esp-14.2.0_20260121)
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/esp_toolchain_check.cmake)

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

#
#  ★既定値は「WiFi/素のビルド＝ON（HAL-free）」「BT＝OFF（hal基盤）」とする。
#  ------------------------------------------------------------------
#  【なぜBTだけ既定OFFか（実測に基づく．evidence-c6-01 §6）】
#  C6のBTは実機エビデンス（§13 D-1／§14 D-2a/D-2b／§15 D-2c/D-2d，board C 2/2）が
#  すべて **「hal基盤 ＋ BTだけIDF v6.1」** の構成で得られている。基盤を
#  esp-idf v5.5.4へ移すと，BT側のcmake（esp_bt.cmake／esp_bt_idf61.cmakeは
#  esp_hw_support等をhalから採る）との間で **供給元の混成**が生じ，
#  実測で `shared_periph_module_t` 未定義が噴出する
#  （hal の `esp_private/esp_modem_clock.h` が要求する型が，esp-idf v5.5.4 の
#  `soc/periph_defs.h` には **存在しない**＝実測。hal内／esp-idf内は
#  それぞれ整合しており，**壊れているのは «混ぜたこと» そのもの**）。
#  ＝HANDOFF §4-3-5 が予告した罠に一致する。
#
#  ∴ 本ラウンドではBTの供給移行は**行わず**，検証済み構成をそのまま維持する
#  （「無理に成功を作らない」）。BTを移すなら esp_bt*.cmake の
#  ESP_HAL_DIR（計122箇所）も**同時に**移す必要があり，かつ C6 BT が要求する
#  blob は v6.1（＝submodule に無い）なので「submodule供給」には到達しない
#  ——BTのhal離脱は v6.1 ツリー参照によって達成される（下記 ESP_IDF61_DIR）。
#  詳細と実機ラウンドへの申し送りは evidence-c6-01 §6/§7。
#
#  ユーザーが明示的に -DASP3_ESPIDF_SUPPLY=ON を渡せばBTでも試せる（可逆）。
#
#
#  ★★【2026-07-17 evidence-c6-08：BT の «例外» を撤去し、全構成で既定 ON にした】
#  上の «BT だけ OFF» という例外の根拠は2つあったが、両方とも実測で消えた：
#    (1)「hal-base + v6.1-BT の構成にしか実機実績が無い」
#        → evidence-c6-05/06/07：**submodule v5.5.4 供給で D-1／D-2b／D-2c／D-2d を
#          真cold・warm ともに達成**（v6.1 は warm のみ）＝実績は v554 側が «上»。
#    (2)「esp-idf base と hal の esp_hw_support を混ぜると壊れる（shared_periph_module_t）」
#        → ★**«混ぜたこと» が原因**であって esp-idf 供給が原因ではなかった
#          （HANDOFF §4-3-5／C3 の同一知見）。**esp_bt_idf61.cmake の ESP_HAL_DIR を
#          全廃して «BT ツリーごと» esp-idf 供給へ寄せたら、この壁は消えた**（実測）。
#  ⇒ **BT でも既定 ON**＝`ninja -t deps` の hal 参照 **0**（全5指標 0．evidence-c6-08 §3）。
#  ★可逆：`-DASP3_ESPIDF_SUPPLY=OFF` で従来どおり hal 供給へ完全復帰する。
#
#
#  ★2026-07-17：docstring 末尾の「OFF = full revert to the hal supply」は
#  **BT ビルドでは嘘だった**ので «実測された挙動» へ書き換えた（挙動は不変）。実測：
#    C6 WiFi + OFF : hal 7181 / esp-idf    0            ＝真の hal fallback（docstring は真）
#    C6 BT   + OFF : hal 1932 / esp-idf  119（うち BT 88）＝**混成（MIXED）**（docstring は偽）
#  ＝evidence-c6-09 §5 が «副作用» として記録した「基盤だけ hal・BT は常に esp-idf」
#  そのもの。hal 経路は同ラウンドで削除済み（`esp_bt.cmake` の `ESP_HAL_DIR`＝**0箇所**）
#  ＝**「BT を hal へ戻す」は原理的に不可能**なのに docstring は "full revert" と
#  言い続けていた。★C3 は同じ混成を両方向の FATAL_ERROR で禁止（`esp_bt.cmake:133-145`）。
#  C6 にガードは無い（実測：cross guard 0箇所）＝**黙って混成が通る**。
#  ガード追加は «挙動変更» なのでユーザー判断（本ラウンドでは提案に留める）。
#
option(ASP3_ESPIDF_SUPPLY
    "Supply ESP components (headers/sources/blobs/ROM ld) from the esp-idf submodule (true v5.5.4 tag) instead of esp-hal-3rdparty. Default ON for ALL configurations (WiFi / BT / plain) = HAL-free. For ESP32C6_BT=ON this became the default in evidence-c6-08: the v5.5.4-submodule supply reaches D-1/D-2b/D-2c/D-2d at both warm and TRUE COLD, and the old 'shared_periph_module_t' wall was caused by *mixing* an esp-idf base with hal's esp_hw_support, which disappears once the BT tree itself is moved. OFF = a true hal fallback ONLY for WiFi/plain builds (measured: hal 7181 / esp-idf 0). WARNING for ESP32C6_BT=ON: OFF reverts the BASE components only -- the BT tree independently follows ASP3_BT_IDF_V554 (default ON = esp-idf submodule), so -DASP3_ESPIDF_SUPPLY=OFF alone silently yields a MIXED build (measured: hal 1932 / esp-idf 119, of which 88 = components/bt; evidence-c6-09 section 5). It does build, but it is NOT a hal fallback: the hal BT path was REMOVED in evidence-c6-09 (esp_bt.cmake references ESP_HAL_DIR 0 times), so no all-hal BT configuration exists for C6. The BT supply choices are ASP3_BT_IDF_V554=ON (esp-idf submodule v5.5.4) or =OFF (external v6.1 via ESP_IDF61_DIR). Unlike C3 (esp_bt.cmake:133-145), C6 has no FATAL_ERROR guard against the mixture"
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

#
#  ★evidence-c6-04：pmu_init() を «.data 初期化後»（software_init_hook）で
#  呼ぶ．既定 ON．
#
#  §20 は pmu_init() を hardware_init_hook から呼んだが，start.S は
#      jal hardware_init_hook → .bss クリア → .data コピー → jal software_init_hook
#  の順（実機バイナリの逆アセンブルで確認）＝**hardware_init_hook は .data
#  初期化より前**．stock の PMU_instance() は初期化子つき static（.data 配置．
#  nm 実測 `d pmu_context.0`）を返し，pmu_hp_system_init() が `ctx->hal->dev`
#  を辿って PMU 記述子を書く．∴ hardware_init_hook から呼ぶと **真cold では
#  ゴミポインタで走り PMU が設定されない**（warm は前ブートの .data が SRAM に
#  残るので «たまたま» 動く＝cold/warm 分岐の機序）．
#
#  OFF＝§20 の呼出し位置（hardware_init_hook）に戻す＝**逆方向対照**用．
#  詳細＝.steering/20260716-c3c5c6-esp-idf-supply-migration/evidence-c6-04-*.md
#
option(ESP32C6_PMU_INIT_LATE
    "Call pmu_init() from software_init_hook (after .data init) instead of hardware_init_hook (evidence-c6-04)"
    ON)
if(ESP32C6_PMU_INIT_LATE)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_PMU_INIT_LATE)
endif()

#
#  ★evidence-c6-04：PMU_instance()->hal を LP_AON STORE8（hardware_init_hook＝
#  .data 前）／STORE9（software_init_hook＝.data 後）へミラーする診断．
#  既定 OFF（恒久ビルド非影響）．bt_smoke_c6 専用（STORE8/9 は
#  ble_host_smoke_c6 が GAP マーカに使う）．ESP32C6_BT_PMU_INIT=ON が前提
#  （PMU_instance() の実体を積むのは esp_bt_idf61.cmake だけ）．
#
option(ESP32C6_PMU_DIAG
    "Mirror PMU_instance()->hal to LP_AON STORE8/9 to measure the pre-.data garbage-pointer window (evidence-c6-04)"
    OFF)
if(ESP32C6_PMU_DIAG)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_PMU_DIAG)
endif()

#
#  ★evidence-c6-04：真cold のアナログ整定待ち（ms）．既定 0＝無効．
#  stock は phy_init に t≈477ms で到達するが ASP3 は t≈83ms＝POR から
#  約 1/6 の時刻で PHY 較正を始めている（evidence-c6-03 §4.1）．
#  «時間» だけを動かして「欠けているのは初期化動作か時刻か」を判別する。
#
#
#  ★evidence-c6-04：stock の recalib_bbpll() 相当（BBPLL 停止→再較正）を
#  Direct Boot に補う．既定 OFF（実測で決める）．
#  ASP3 は「ROM が 160MHz 設定済みだから触らない」と明示判断しているが，
#  stock は「bootloader が用意した PLL は十分安定でない」として再較正する
#  ＝「stock がやっていることを我々がやめた」型．実体＝bt/bt_pmu_init_c6.c．
#  ★ESP32C6_BT_PMU_INIT=ON が前提（rtc_clk.c と実体を積むのがそこだけ）．
#
#
#  ★★evidence-c6-04【真cold ハングの真因の修正】CPU/SOC ルートクロックを
#  PLL@160MHz へ明示設定する（stock の 2nd-stage bootloader の rtc_clk_init()
#  相当）．既定 ON．
#
#  ★実測（真cold・sentinel で真cold 証明済み）：ROM は Direct Boot へ
#  **XTAL@40MHz** のまま渡してくる（STORE5=0xbb110280＝src=0/40MHz）。
#  ASP3 の「ROM が SPLL/160MHz 設定済みだから触らない」という明示判断は
#  **warm でしか成立していない**（PCR は warm で前ブートの SOC_CLK_SEL を保持）。
#  SOC_ROOT_CLK=XTAL のままだと PLL 由来の modem/PHY クロックが所定周波数に
#  ならず RF シンセ PLL がロックできない＝真cold JTAG で確定したハング
#  （PC=ram_set_chan_freq_sw_start+0x1e＝0x600a00cc bit8 永久スピン）。
#
#  OFF＝従来（ROM 任せ）＝**逆方向対照**用。
#  ★ESP32C6_BT_PMU_INIT=ON が前提（rtc_clk.c と実体を積むのがそこだけ）。
#
option(ESP32C6_COLD_CPU_PLL
    "Explicitly set CPU/SOC root clock to PLL@160MHz at boot like stock rtc_clk_init() (evidence-c6-04)"
    ON)
#  ★WiFi・BT の両方で必要（同じ真cold ハングが両経路で起きる）。
#  rtc_clk.c は esp_wifi.cmake / esp_bt_idf61.cmake の双方が既にリンク済み。
#  素のビルド（WiFi/BT 両OFF）は rtc_clk.c を積まないので対象外＝PHY も使わない。
if(ESP32C6_COLD_CPU_PLL AND (ESP32C6_WIFI OR ESP32C6_BT))
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${TARGETDIR}/cold_clk_init_c6.c)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_COLD_CPU_PLL)
endif()

option(ESP32C6_COLD_RECALIB_BBPLL
    "Re-calibrate BBPLL at boot like stock recalib_bbpll() (evidence-c6-04; superseded by ESP32C6_COLD_CPU_PLL)"
    OFF)
if(ESP32C6_COLD_RECALIB_BBPLL)
    if(NOT ESP32C6_BT_PMU_INIT)
        message(FATAL_ERROR "ESP32C6_COLD_RECALIB_BBPLL requires ESP32C6_BT_PMU_INIT=ON (rtc_clk.c/shim live there)")
    endif()
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_COLD_RECALIB_BBPLL)
endif()

set(ESP32C6_COLD_SETTLE_MS "0" CACHE STRING
    "Busy-wait this many ms in software_init_hook before the kernel starts (evidence-c6-04 cold-settle discriminator)")
if(NOT ESP32C6_COLD_SETTLE_MS STREQUAL "0")
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_COLD_SETTLE_MS=${ESP32C6_COLD_SETTLE_MS})
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
        #
        #  ★【2026-07-17 evidence-c6-11】ここに在った «DIAGNOSTIC (temporary)» の
        #  コメントは残置していたが、**wifi_trace.c 自体は if(ESP32C6_WIFI) 配下に
        #  «残す»**（下記の理由）。**除去したのは esp_wifi.cmake の `-Wl,--wrap=` 群**
        #  ＝**実害（ホットパスのコスト）の本体**。
        #
        #  ★なぜ wifi_trace.c を消せないか（実測）：
        #  **apps/wifi_scan/wifi_scan.c が `#ifdef TOPPERS_ESP32C6_WIFI`（＝機能マクロ）
        #  配下で wifi_trace_reset()／wifi_regi2c_patch_install()／wifi_regsnap_reset()
        #  等を «直接呼んで» いる**ため、消すと wifi_scan がリンクできない。
        #  ★さらに同ブロックには **live なレジスタ書込み**
        #  `*(volatile uint32_t *)0x600af018U = 0x7U;`（「追記10：根本原因テスト／
        #  クロック再アサート」）が在り、**これは診断ではなく «恒久修正の代行» の
        #  可能性がある**（0x600af018=MODEM_LPCON_CLK_CONF＝bt_shim.c が |=0x1 を
        #  恒久修正として入れている register と同一）。**外すと wifi_scan が壊れうる**。
        #  ⇒ **app 側の整理は «別の判断» としてユーザーへ返す**（evidence-c6-11 §5）。
        #
        #  ★`--wrap` が無ければ __wrap_* は誰からも呼ばれず、wifi_regsnap_capture
        #  （AGC 1024ワード読み）も --gc-sections で落ちる＝**ホットパスのコストは消える**。
        #  wifi_dhcp（W1）は wifi_trace を一切参照しない＝**ELF から完全に消える**（§4 で実測）。
        #
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

#
#  ★evidence-c6-04【最小集合の同定用】ESP32C6_COLD_CPU_PLL の中の
#  clk_ll_mspi_fast_set_hs_divider(6) だけを外す（既定 OFF＝分周比を設定する）．
#  ON にすると «MSPI 分周比を設定せずに PLL へ切替える» ＝その1手が
#  最小集合に含まれるかどうかを単一変数で判定できる．
#
option(ESP32C6_COLD_CPU_PLL_NO_MSPI
    "Drop the MSPI HS divider step from ESP32C6_COLD_CPU_PLL (minimal-set discriminator, evidence-c6-04)"
    OFF)
if(ESP32C6_COLD_CPU_PLL_NO_MSPI)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_COLD_CPU_PLL_NO_MSPI)
endif()
