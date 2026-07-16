#
#      ESP32-C6 Bluetooth（BLE）統合  IDF v6.1 matched-set 版（D-1のみ）
#      ── BLE実施13（docs/ble-c5c6-plan.md §13）
#
#  ESP32C6_BT_IDF61=ON のときに esp_bt.cmake から include される．
#  hal submodule 版（既定・esp_bt.cmake 本体）とトグルで排他選択する．
#
#  ★狙い：C6 BLE の PHY-init `register_chipv7_phy` RFシンセ非ロック
#  （§11/§12 で hal libphy の BT依存サブパス内部と確定，D-1未達）を，
#  bt/phy/coex/libble_app.a を **IDF v6.1（~/tools/esp-idf-v6.1）の
#  matched set** へ swap して解けるか実機判定する（C5 esp_bt.cmake の
#  IDF v6.1 構成の C6 転写）．§12 の申し送りどおり「効く保証は無い」
#  （C5実施09は WiFi も非収束→libphy版数不適合が真因だったが，C6 は
#  WiFi が同一 hal libphy で収束＝BT経路固有）．
#
#  ★v6.1 の controller/esp32c6/bt.c は **C3型プログラミングモデル**
#  （FreeRTOS API＋標準 esp_intr_alloc を直接呼ぶ．esp_os_*／npl_os_*
#  ドリフト無し＝実測確認済み）のため，本構成は hal 版ではなく
#  **C5 esp_bt.cmake（IDF v6.1）を範とする**：
#    - bt/stub は C3 の freertos stub を再利用（platform/os.h stub 不使用）．
#    - シムは bt/bt_shim_idf61.c（標準名 esp_intr_alloc/free＋§11クロック）．
#    - os_mempool.c を自前リンク（v6.1 blob が plain名で要求．hal は不要
#      だった＝C5 BLE実施03の教訓）．
#
#  ★D-1（controller-only＝bt_smoke_c6）限定．NimBLE ホスト（D-2*）は
#  本 swap では移植しない——`register_chipv7_phy` は
#  esp_bt_controller_enable→esp_phy_enable 内で発火し bt_smoke_c6 が
#  直接踏む（§11で「bt_smoke_c6も同一ハング」確認済み）ため，シンセ・
#  ロックの可否判定にホストスタックは不要（advisor指摘）．NimBLE を要する
#  アプリを IDF61 と組み合わせた場合は FATAL_ERROR で明示的に弾く．
#
#  ★§11 のクロック2修正（regi2c sel_160m＋WIFIPWR）＋実施91 ICG は
#  bt_shim_idf61.c の esp_shim_bt_clock_init() に維持（前提）．
#

set(BT_TARGETDIR ${TARGETDIR}/bt)
get_filename_component(C3_TARGETDIR ${CMAKE_CURRENT_LIST_DIR}/../esp32c3_espidf ABSOLUTE)

#
#  ------------------------------------------------------------------
#  ★★供給元の選択（2026-07-17 に全面訂正．evidence-c6-01 §1・§2・§5）
#  ------------------------------------------------------------------
#
#  【旧記述は実測で反証された】旧コメントは
#    「事前md5実測：libble_app.a・libphy.a・libbtbb.a は v5.5.4/v6.1 間で
#      C6 もバイト完全一致（register_chipv7_phy含む）——差はlibcoexist.aのみ」
#  としていたが，**C6 では成立しない**。本PCでの md5 実測：
#
#    file           submodule(v5.5.4tag)  hal        v6.1-beta1  ~/tools/esp-idf
#    libble_app.a   75db98e5              75db98e5   c28653df    54cb6f5f
#    libphy.a       cb429107              cb429107   3fea0708    6b62ea91
#    libbtbb.a      cbe3022f              cbe3022f   d31c8865    1037b470
#    libcoexist.a   55344862              55344862   797d4daf    cd3c5cff
#
#  ＝(a) **真の v5.5.4 タグ ≡ hal（4/4 バイト一致）**，
#    (b) v5.5.4 タグ と v6.1 は **4/4 すべて相違**（「バイト同一」は誤り）。
#  旧記述が成立していたのは，当時の `~/tools/esp-idf`（＝別PCの
#  `v5.5.4-1169-gbb2188bf`＝release/v5.5 先端）が **BT blob について v6.1 と
#  一致していた**ためで，そのツリーを「v5.5.4」と**名前で誤認**した結果である
#  （実際は +1169）。＝本質は「v5.5.4≡v6.1」ではなく「**+1169≡v6.1**」。
#
#  【★外部絶対パスの撤去（本PCでは版すら違う）】旧実装は
#  `set(IDF /home/honda/tools/esp-idf)`（v5.5.4を名乗る）と
#  `set(IDF /home/honda/tools/esp-idf-v6.1)` のローカルパス直書きだった。
#  実測：**本PCの `~/tools/esp-idf` は v5.5(=v5.5.0, 8c750b08) の shallow clone**
#  ＝v5.5.4 タグでも +1169 でもない**第3の版**であり，どのラウンドでも
#  検証されていない。同じパス名が PC ごとに別版を指す＝再現性が無い。
#  ⇒ v5.5.4 は **submodule（IDF_V554＝735507283d）を相対参照**する。
#     v6.1 は submodule 化されていないため `ESP_IDF61_DIR` キャッシュ変数
#     （既定＝従来パス）とし，不在なら明示的に FATAL_ERROR で落とす。
#
#  【既定＝v6.1（OFF）とする理由】上表 (a) より，C6 で v5.5.4 を選ぶことは
#  **hal と同一の blob をリンクする**ことと等価であり，§10-12 の
#  `register_chipv7_phy` RFシンセ非ロック（D-1未達）を持ち込む側になる。
#  実機エビデンス（D-1/D-2b/D-2c）があるのは v6.1 のみ（§13-15）。
#
#  【★ASP3_BT_IDF_V554=ON は「決定的対照」実験のためにこそ在る】
#  §13 の A/B は **blob と グルー(bt.c/シム) を同時に**入れ替えていた
#  （hal＝esp_os_*/platform-os.h 型 → v6.1＝C3型＋bt_shim_idf61.c）＝
#  2変数同時変更で交絡しており，「非収束は blob の版が原因」という帰属は
#  **分離されていない**（HANDOFF §5-1「決定的対照を省くと偽の成功譚になる」）。
#  実測：**submodule v5.5.4 タグの C6 bt.c は v6.1 と同じ C3型**（hal だけが
#  esp_os_* 型）。∴ 本トグル ON ＝「**C3型グルー × hal と同一の blob**」＝
#  §13 に欠けていた **4アーム目**であり，これを実機で回すと
#    - ハングする → 原因は **blob の版**（v6.1 が必須／C6のv5.5.4統一は不可）
#    - 動作する   → 原因は **hal のグルー/シム**（blob は無罪／統一は可能）
#  が**一意に決まる**。＝実機ラウンドで最優先に測るべき対照（evidence-c6-01 §7）。
#
option(ASP3_BT_IDF_V554 "Use the esp-idf submodule (TRUE v5.5.4 tag = 735507283d) BT controller/phy/coexist instead of v6.1. Default OFF = v6.1 (the only C6 BT config with D-1/D-2b/D-2c hardware evidence). ON = the decisive 4th arm: C3-type glue x hal-identical blobs (v5.5.4 tag BT blobs are byte-identical to hal), which de-confounds §13's simultaneous blob+glue swap" OFF)

set(ESP_IDF61_DIR /home/honda/tools/esp-idf-v6.1 CACHE PATH
    "Path to an ESP-IDF v6.1 tree (NOT a submodule; supplies the C6 BLE matched set). Override with -DESP_IDF61_DIR=<path>")

if(ASP3_BT_IDF_V554)
    #  ★真の v5.5.4 タグ＝submodule（target.cmake が相対で定義済み）
    set(IDF ${IDF_V554})
else()
    set(IDF ${ESP_IDF61_DIR})
    if(NOT EXISTS ${IDF}/components/bt/controller/esp32c6/bt.c)
        message(FATAL_ERROR
            "C6 BLE default supply is IDF v6.1, but no v6.1 tree was found at "
            "ESP_IDF61_DIR='${IDF}'. v6.1 is NOT vendored as a submodule. "
            "Either pass -DESP_IDF61_DIR=<path-to-esp-idf-v6.1>, or select "
            "another supply: -DASP3_BT_IDF_V554=ON (esp-idf submodule, true "
            "v5.5.4 tag) or -DESP32C6_BT_IDF61=OFF (hal). "
            "See .steering/20260716-c3c5c6-esp-idf-supply-migration/"
            "evidence-c6-01-*.md")
    endif()
endif()
set(BT_CHIP_SERIES esp32c6)

#
#  ★BLE実施14（docs/ble-c5c6-plan.md §14）：v6.1 D-1 の上に NimBLE ホストを
#  載せて D-2a（host-controller sync）→D-2b（ble_gap_adv_start rc=0）を実機
#  到達させる．NimBLE 有効化トグル（ESP32C6_BT_IDF61_NIMBLE）で判定を先出し
#  する（下の「2. ソースファイル」節で hci_driver_standard.c／
#  hci_driver_nimble.c の二者択一・CONFIG_BT_CONTROLLER_ONLY の D-1 限定化に
#  使うため．ブロック本体＝NimBLE ソース/インクルード追加は末尾の D-2a 節）．
#  RAM 予算のため既定 OFF．NimBLE を要するアプリ（ble_host_smoke_c6）で自動 ON．
#  D-1 の bt_smoke_c6 は痩せたまま保つ．★実装は hal 版（esp_bt.cmake の
#  ESP32C6_BT_NIMBLE ブロック）ではなく **C5 esp_bt.cmake の IDF v6.1 NimBLE
#  ブロックを範とする**——本 D-1 が既に v6.1 controller/phy を使うため，
#  nimble も ${IDF} から採り hal-nimble を混ぜない（C5 冒頭のライブラリ世代
#  選定と同じ理由．検証則：v6.1 blob は nimble_mem_*／os_memblock_* を未解決の
#  まま残すため esp_nimble_mem.c と os_mempool.c の «両方» をリンクして初めて
#  v6.1 nimble に乗れている＝esp_nimble_mem.c 無しでリンクが通ったら hal-nimble を
#  誤って引いた合図＝要調査）．
#
option(ESP32C6_BT_IDF61_NIMBLE "Enable NimBLE host stack on IDF v6.1 controller (Phase D-2a/D-2b, BLE実施14)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c6")
    set(ESP32C6_BT_IDF61_NIMBLE ON)
endif()

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C6_BT
    #  ★§20 pmu_init 呼出しの有効化（target_kernel_impl.c）。実体
    #  bt/bt_pmu_init_c6.c を積むのは本ファイルだけなので，定義も本ファイル
    #  限定にする（hal 版のリンク不能を解消＝★B2／★B3(iii)．
    #  v6.1／v5.5.4 経路の挙動は不変）。
    TOPPERS_ESP32C6_BT_PMU_INIT
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C6=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  CONFIG_BT_CONTROLLER_ONLY=1 は D-1（NimBLE ホスト無し）限定．実ESP-IDF
    #  Kconfig では CONTROLLER_ONLY と NIMBLE_ENABLED は排他選択のため，NimBLE
    #  ON 時（ble_host_smoke_c6）には立てない（C5 esp_bt.cmake と同じ分離）．
    #  実際の定義は下の「2. ソースファイル」節の if(NOT ESP32C6_BT_IDF61_NIMBLE) 内．
    #  VHCI（esp_vhci_host_* 公開API）
    CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=1
    #  msys 初期化をコントローラ内部へ委譲（esp32c6 Kconfig 既定 y）
    CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1
    CONFIG_BT_LE_MSYS_1_BLOCK_COUNT=12
    CONFIG_BT_LE_MSYS_1_BLOCK_SIZE=256
    CONFIG_BT_LE_MSYS_2_BLOCK_COUNT=24
    CONFIG_BT_LE_MSYS_2_BLOCK_SIZE=320
    CONFIG_BT_LE_MSYS_BUF_FROM_HEAP=1
    #  callout に esp_timer_*（bt_shim_idf61.c 提供）を選択
    CONFIG_BT_LE_USE_ESP_TIMER=1
    #  esp_bt_cfg.h が無条件参照する項目（esp32c6 Kconfig.in 既定値）
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
    #  ★XTAL/CPU クロック：C6 は hal/nuttx/esp32c6/include/sdkconfig.h
    #  （カーネル共通で常時include済み）が CONFIG_XTAL_FREQ=40 と
    #  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ→CONFIG_ESPRESSIF_CPU_FREQ_MHZ の
    #  間接マクロを定義する．esp_bt.h（v6.1 esp32c6）の
    #  BT_CONTROLLER_INIT_CONFIG_DEFAULT() は CONFIG_XTAL_FREQ／
    #  CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ を直接参照するため，実体の
    #  CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160 のみ -D する（C5 のように XTAL を
    #  -D すると sdkconfig.h の後勝ち redefine と競合するため定義しない．
    #  hal 版 esp_bt.cmake と同じ判断）．
    CONFIG_ESPRESSIF_CPU_FREQ_MHZ=160
    CONFIG_ESP_IPC_ENABLE
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  強制include（C5 BLE実施03 と同一．v6.1 のソースが暗黙include前提と
#  する 5ヘッダ．hal 版の esp_timer.h／npl_os_bridge.h は不要＝v6.1 は
#  esp_timer.h を自ファイルで include し，npl_os_* ドリフトも無い．
#  代わりに os_mempool.c の BIT() マクロ用に esp_bit_defs.h を足す）．
#
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_bit_defs.h>"
    #  ★v6.1 npl_os_freertos.c は esp_timer_is_active／esp_timer_get_expiry_time
    #  を esp_timer.h を include せずに呼ぶ（上流ドリフト）が，本ビルドの
    #  esp_timer.h は hal_stub 版に解決され同2関数の宣言を持たない．
    #  bt/bt_esp_timer_ext.h（stub esp_timer.h を include し2宣言を補う）を
    #  force-include して GCC14.2 の暗黙宣言 hard error を回避する．
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt/bt_esp_timer_ext.h>"
    #  ★v6.1 phy_init.c（phy_enter_critical）は xPortInIsrContext() を呼ぶが
    #  freertos/task.h を include しない（freertos/FreeRTOS.h＋portmacro.h の
    #  みで xPortInIsrContext は task.h 側にある）．GCC14.2 は暗黙宣言を
    #  hard error にするため，C3 stub の freertos/task.h（xPortInIsrContext
    #  ＝sns_ctx() の static inline．自己完結＝先頭で FreeRTOS.h を include）を
    #  強制includeして可視化する．他の -include 群と同じ«暗黙include前提
    #  ヘッダの補完»．
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include freertos/task.h>"
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#  ★C3 の bt/stub/include（freertos/*.h＋esp_partition.h）を再利用する
#  （v6.1 の bt.c は C3 と同じプログラミングモデル）．
#
#  ★BLE実施14：NimBLE ON 時，C6 idf61 専用の bt_nimble_config.h（LEGACY_VHCI=0）を
#  C3 stub（LEGACY_VHCI=1 同梱）より前に PREPEND する（C5 esp_bt.cmake と同じ罠
#  対策）．順序を誤ると C3 版が先に見つかり LEGACY_VHCI=1＝mbuf 余白計算が
#  トランスポートと不整合になるサイレントな実行時バッファバグ．D-1（bt_smoke_c6）は
#  bt_nimble_config.h を include しないため本 PREPEND は無害．
list(PREPEND ASP3_INCLUDE_DIRS ${TARGETDIR}/bt/stub_idf61/include)

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
    #  §20：pmu_init.c/ocode_init.c の `#include "regi2c_ctrl.h"`（プレーン名）
    #  を解決する（esp_hw_support の private include．NON_OS_BUILD で ROM 直呼び）．
    ${ESP_HAL_DIR}/components/esp_hw_support/include/esp_private
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/private_include
    ${ESP_HAL_DIR}/components/esp_hw_support/port/include
    ${ESP_HAL_DIR}/components/esp_system/include
    #  esp_private/wifi.h（phy_init.c．BT も WiFi と同じ PHY 実ソース）
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
    #  hal/pmu_ll.h・hal/pmu_hal.h（wifi/esp_shim.c の esp_shim_modem_icg_init）
    ${ESP_HAL_DIR}/components/esp_hal_pmu/include
    ${ESP_HAL_DIR}/components/esp_hal_pmu/${BT_CHIP_SERIES}/include
    #  hal/rtc_timer_hal.h（rtc_time.c）
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/include
    ${ESP_HAL_DIR}/components/esp_hal_rtc_timer/${BT_CHIP_SERIES}/include
    #  hal/timg_ll.h（rtc_time.c）
    ${ESP_HAL_DIR}/components/esp_hal_timg/include
    ${ESP_HAL_DIR}/components/esp_hal_timg/${BT_CHIP_SERIES}/include
    #  modem/i2c_ana_mst_reg.h・regi2c_impl.h 等（IDF socレイアウトでのみ
    #  解決．esp_wifi.cmake §1b／C5 esp_bt.cmake と同じ理由．hal soc より後）
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/include
)

if(ASP3_BT_IDF_V554)
    #  C5 esp_bt.cmakeと同じ理由：v5.5.4のesp_wifi_types_generic.hが
    #  "esp_interface.h"を直接includeする（v6.1は不要）。実体は
    #  ${IDF}/components/esp_hw_support/includeにある。
    list(APPEND ASP3_INCLUDE_DIRS
        ${IDF}/components/esp_hw_support/include
    )
endif()

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
    #  ★os_mempool.c を自前リンク（v6.1 blob が os_memblock_*／
    #  os_mempool_* を plain名で未解決参照＝C5 BLE実施03の教訓．hal C6 は
    #  blob 同梱で不要だったが v6.1 blob は異なる．os_mbuf.c は未参照の
    #  ため引き続き含めない＝多重定義が出れば除去する）．
    #
    #  ★供給元による**レイアウト差**（実測．evidence-c6-01 §5）：
    #    v6.1        … components/bt/porting/mem/os_mempool.c
    #    v5.5.4タグ  … components/bt/porting/mem/ に **無い**
    #                  （在るのは host/nimble/nimble/porting/nimble/src/ の方
    #                  ＝NimBLE本家サブモジュール側の旧レイアウト）
    #  ＝v6.1 で bt/porting へ移された。∴ `${IDF}` を差し替えるだけでは
    #  configure が通らない（実測：Cannot find source file）。
    #  なお v5.5.4タグの libble_app.a は **hal とバイト同一**であり，hal は
    #  os_mempool.c を要さなかった（blob 同梱）ため，v5.5.4 側では
    #  そもそも積まないのが正しい＝下の if で分岐する。
    #  ★この事実自体が「旧 ASP3_BT_IDF_V554=ON は v5.5.4 タグを
    #  一度も指していなかった」ことの傍証：旧実装は `${IDF}` を
    #  `~/tools/esp-idf`（+1169＝v6.1系レイアウト）に向けていたため
    #  この経路が通っていた。
    #  （実体は下の if(NOT ASP3_BT_IDF_V554) で追加する）
    #  PHY／クロック／ペリフェラルの実ソース（bt/phy/coex は IDF v6.1，
    #  hw_support/clock/rtc/efuse/periph/modem_clock はチップ依存で hal 版．
    #  C5 esp_bt.cmake と同じ origin-split）
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/esp_clk_tree.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_HAL_DIR}/components/esp_hal_clock/${BT_CHIP_SERIES}/clk_tree_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_time.c
    ${IDF}/components/bt/porting/transport/src/hci_transport.c
    #  hci_driver_standard.c（D-1）と hci_driver_nimble.c（D-2a 節）は共に
    #  hci_driver_vhci_ops を定義するため二者択一（同時リンク不可）．D-1
    #  （controller-only．bt_smoke_c6）は standard 版を下の
    #  if(NOT ESP32C6_BT_IDF61_NIMBLE) ブロックで追加する．NimBLE ON 時
    #  （ble_host_smoke_c6）は D-2a 節が nimble 版を追加する．
    ${BT_TARGETDIR}/bt_shim_idf61.c
    ${IDF}/components/esp_phy/src/phy_init.c
    ${IDF}/components/esp_phy/src/phy_common.c
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${IDF}/components/esp_phy/src/lib_printf.c
    ${IDF}/components/esp_phy/src/btbb_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/modem_clock.c
    ${ESP_HAL_DIR}/components/hal/${BT_CHIP_SERIES}/modem_clock_hal.c
    ${ESP_HAL_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_HAL_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    ${ESP_HAL_DIR}/components/hal/efuse_hal.c
    ${ESP_HAL_DIR}/components/hal/${BT_CHIP_SERIES}/efuse_hal.c
    #
    #  ★§20：cold RF-synth-PLL ロック用の pmu_init 移植．stock IDF の
    #  起動シーケンスが呼ぶ PMU HP_ACTIVE 電源/アナログ初期化を ASP3 の
    #  Direct Boot が飛ばしているのが C6 BT phy_init cold ハングの真因
    #  （docs/ble-c5c6-plan.md §19.8＋§20）．pmu_init.c/pmu_param.c/
    #  ocode_init.c は hal submodule（IDF v6.1 とバイト一致）を «そのまま»
    #  リンクし，薄いシム bt_pmu_init_c6.c 経由で hardware_init_hook から
    #  早期に呼ぶ（hal は編集しない＝禁则遵守）．regi2c は ROM 直呼び
    #  （NON_OS_BUILD）でロック不要にし regi2c_ctrl.c 依存を避ける．
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/pmu_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/pmu_param.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/ocode_init.c
    #  ★実体を積むのはこの経路だけ＝呼出し側のガード
    #  TOPPERS_ESP32C6_BT_PMU_INIT も **ここでだけ**定義する
    #  （下の list(APPEND ASP3_COMPILE_DEFS ...) 参照）。hal 版
    #  （esp_bt.cmake 本体）は本ファイルを積まないため呼出しも無効化され，
    #  §20 以前と同じ挙動でリンクできる（★B2 可逆性の回復）。
    ${BT_TARGETDIR}/bt_pmu_init_c6.c
    #  esp_rom_regi2c_read/write_mask（ROM regi2c ラッパ）を提供する ROM
    #  パッチ．pmu_init/ocode/rtc_clk が NON_OS_BUILD で ROM 直呼びに落ちる
    #  ときの最下層プロバイダ．
    ${ESP_HAL_DIR}/components/esp_rom/patches/esp_rom_hp_regi2c_${BT_CHIP_SERIES}.c
)

#
#  ★os_mempool.c は v6.1 のみ（上の「レイアウト差」注記を参照）。
#  v5.5.4タグ（ASP3_BT_IDF_V554=ON）では bt/porting/mem/ に存在せず，かつ
#  当該 blob は hal とバイト同一＝hal 同様 blob 同梱で不要と予測される。
#  実測で os_memblock_*／os_mempool_* が未解決になったら，v5.5.4 側の
#  実体 `components/bt/host/nimble/nimble/porting/nimble/src/os_mempool.c`
#  を積むこと（旧レイアウト）。
#
if(NOT ASP3_BT_IDF_V554)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${IDF}/components/bt/porting/mem/os_mempool.c
    )
endif()

#  pmu_init.c/ocode_init.c/rtc_clk.c 内の REGI2C_WRITE_MASK/READ_MASK・
#  regi2c_ctrl_write_reg* を «ROM 直呼び»（esp_rom_regi2c_*，ロック無し）へ
#  解決させる（NON_OS_BUILD）．hardware_init_hook の早期（単一スレッド・
#  割込み前）で呼ぶため regi2c_ctrl.c のクリティカルセクション（esp_os_*/
#  saradc/tsens 依存）は不要＝リンク肥大を避ける．rtc_clk.c は ocode の
#  calibrate 経路（本 board=efuse blk>=1 では非実行）から参照されるため
#  同様に NON_OS へ寄せて regi2c_ctrl_write_reg 未解決を回避．
set_source_files_properties(
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/pmu_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/ocode_init.c
    ${ESP_HAL_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    PROPERTIES COMPILE_DEFINITIONS "NON_OS_BUILD=1"
)

#
#  §18：RF-cal regi2c トレース計装（既定 OFF・非回帰）．g_phyFuns テーブル
#  （0x4087f954）の write/write_mask 枠を差し替え，synth 位相の regi2c write
#  列を .bss リングバッファへ記録する．synth-lock ハング（§16/§17）の
#  C5-vs-C6 diff 用．app が esp_bt_regi2c_trace_install() を controller_init
#  より前に呼ぶ（TOPPERS_ESP32C6_BT_REGI2C_TRACE で app 側呼出しをガード）．
#
option(ESP32C6_BT_REGI2C_TRACE "Trace RF-cal regi2c writes via g_phyFuns table patch (§18 synth-lock diag)" OFF)
if(ESP32C6_BT_REGI2C_TRACE)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_BT_REGI2C_TRACE)
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${BT_TARGETDIR}/esp_bt_regi2c_trace.c
    )
endif()

if(NOT ESP32C6_BT_IDF61_NIMBLE)
    #  ★D-1＝controller-only スモークテスト（NimBLE ホスト無し）限定．
    #  CONFIG_BT_CONTROLLER_ONLY と CONFIG_BT_NIMBLE_ENABLED は実ESP-IDF の
    #  Kconfig では同時に 1 にならない排他選択（C5 esp_bt.cmake と同じ分離）．
    #  NimBLE ON 時は立てず，hci_driver_standard.c も外す
    #  （hci_driver_vhci_ops の多重定義回避）．
    list(APPEND ASP3_COMPILE_DEFS
        CONFIG_BT_CONTROLLER_ONLY=1
    )
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${IDF}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    )
endif()

#
#  ------------------------------------------------------------------
#  3. リンクライブラリパス・ライブラリ（IDF v6.1 matched set）
#  ------------------------------------------------------------------
#  ★C6 の bt-lib は esp32c6-bt-lib/esp32c6/（無印 esp32c6．esp32c61 は
#  別チップ）．C5（esp32c5-bt-lib 直下）とはサブディレクトリ構造が異なる．
#
list(APPEND ASP3_LINK_OPTIONS
    -L${IDF}/components/bt/controller/lib_${BT_CHIP_SERIES}/${BT_CHIP_SERIES}-bt-lib/${BT_CHIP_SERIES}
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
#  4. ROM関数ld（IDF v6.1 の esp32c6 ldセット）
#  ------------------------------------------------------------------
#  esp_wifi.cmake と同じ理由で eco3.ld は除外，net80211/pp（WiFi専用）は
#  除外．systimer.ld は IDF v6.1 に存在しない（C5 esp_bt.cmake と同じ）．
#  coexist.ld は IDF v6.1 esp32c6 に存在するため追加する．
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
#  Phase D-2a/D-2b／BLE実施14：NimBLE ホストスタック（IDF v6.1版）
#  ==================================================================
#
#  D-1（コントローラ＋VHCI，bt_smoke_c6）の上に NimBLE ホストを載せ，
#  D-2a（host-controller sync）→D-2b（ble_gap_adv_start rc=0）を実機到達
#  させる．★C5 esp_bt.cmake の IDF v6.1 NimBLE ブロックの逐語的な C6 転写
#  （ソースは hal submodule ではなく IDF v6.1（${IDF}）から採る——本 D-1 が
#  既に v6.1 controller/phy を使うため，nimble も同一 matched set で揃える．
#  hal-nimble ＋ v6.1-controller のブロブ ABI 境界を新規に作らない）．
#
#  C6/C5 は新世代コントローラ（SOC_ESP_NIMBLE_CONTROLLER=1）のため，
#  nimble_port.c の esp_nimble_init() 内部で esp_nimble_hci_init() 呼出しが
#  コンパイルアウトされる＝C3 の LEGACY VHCI 経路は存在しない．
#  hci_transport.c（D-1で既存）＋hci_driver_nimble.c＋hci_esp_ipc.c を使う
#  （D-1 の hci_driver_standard.c と hci_driver_vhci_ops を取り合う二者択一．
#  上の if(NOT ESP32C6_BT_IDF61_NIMBLE) で分離済み）．
#
#  ★C5 と同じく，TRUE=1／BT_HCI_LOG_INCLUDED=0／-include sdkconfig.h は
#  «追加しない»：v6.1 の nimble_port.c は bt_common.h（TRUE／
#  BT_HCI_LOG_INCLUDED の定義元）を #if より前に include するため，hal 版で
#  踏んだ順序バグは v6.1 には存在しない（もし実機ビルドで hci_log/
#  bt_hci_log.h が要求されたら，そのとき C6 hal 版と同じ -DTRUE=1
#  -DBT_HCI_LOG_INCLUDED=0 を本ブロックへ追加する）．
#
if(ESP32C6_BT_IDF61_NIMBLE)

    set(NIMBLE_ROOT ${IDF}/components/bt/host/nimble/nimble/nimble)
    set(BT_ROOT ${IDF}/components/bt)
    set(TINYCRYPT_ROOT ${IDF}/components/bt/host/nimble/nimble/ext/tinycrypt)

    #  ---- SMP（ペアリング／ボンディング，D-2d）----
    #  ★本ラウンド（BLE実施14）の目標は D-2a/D-2b（sync→adv rc=0）＝暗号不要の
    #  ため既定 OFF（C5/C6-hal は D-2d 到達済みで既定 ON だが，本 v6.1 初 bring-up
    #  はビルド面を痩せさせ tinycrypt リンクを避ける）．OFF 時は
    #  MYNEWT_VAL_BLE_SM_LEGACY/SC=0 で ble_sm*.c を near-empty 化し，bond store は
    #  ble_store_ram（IDF文脈で空＝sync/adv には十分）を使う．ON にすれば C5 と
    #  同じ tinycrypt5ソース＋ble_store_config へ切替（D-2d 拡張時）．可逆．
    option(ESP32C6_BT_IDF61_SM "Enable NimBLE SMP pairing/bonding on C6 idf61 (D-2d, tinycrypt)" OFF)

    #  ---- コンパイル定義 ----
    #  CONFIG_BT_NIMBLE_* 一式は bt/stub_idf61/include/bt_nimble_config.h
    #  （C6 idf61 専用．LEGACY_VHCI=0）で供給する（上で PREPEND 済み）．
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_BT_NIMBLE)
    if(ESP32C6_BT_IDF61_SM)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_BT_SM)
    else()
        #  D-2a/D-2b：SECURITY off．NIMBLE_BLE_SM=SM_LEGACY||SM_SC を 0 に落とし
        #  ble_sm*.c を near-empty 化して tinycrypt/mbedTLS リンクを回避する．
        list(APPEND ASP3_COMPILE_DEFS
            MYNEWT_VAL_BLE_SM_LEGACY=0
            MYNEWT_VAL_BLE_SM_SC=0
        )
    endif()

    #  bt_nimble_config.h（CONFIG_BT_NIMBLE_*）・syscfg/syscfg.h（MYNEWT_VAL）を
    #  強制 include．D-1 の -include soc/soc_caps.h 等と衝突しないよう SHELL: 接頭辞．
    list(APPEND ASP3_COMPILE_OPTIONS
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt_nimble_config.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include syscfg/syscfg.h>"
    )

    #  ---- インクルードパス ----
    #  porting/nimble/include（syscfg/syscfg.h）・porting/npl/freertos/include・
    #  host/nimble/port/include（esp_nimble_init/mem）は D-1 のインクルード
    #  リストに既に含まれる（-I の並びで先に来る＝優先解決）．
    list(APPEND ASP3_INCLUDE_DIRS
        ${NIMBLE_ROOT}/host/include
        ${NIMBLE_ROOT}/include
        ${NIMBLE_ROOT}/transport/include
        ${NIMBLE_ROOT}/host/services/gap/include
        ${NIMBLE_ROOT}/host/services/gatt/include
        ${NIMBLE_ROOT}/host/util/include
        ${NIMBLE_ROOT}/host/store/ram/include
    )
    if(ESP32C6_BT_IDF61_SM)
        list(APPEND ASP3_INCLUDE_DIRS
            ${NIMBLE_ROOT}/host/store/config/include
            ${TINYCRYPT_ROOT}/include
        )
    endif()

    #  ---- ソースファイル ----
    #  D-1 で既に npl_os_freertos.c／os_msys_init.c／bt_osi_mem.c／os_mempool.c／
    #  hci_transport.c はリンク済み．
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${BT_ROOT}/porting/transport/driver/vhci/hci_driver_nimble.c
        ${NIMBLE_ROOT}/transport/esp_ipc/src/hci_esp_ipc.c
        #  nimble_mem_malloc/calloc/free（ホスト各所が直接呼ぶ．heap_caps_* へ
        #  委譲）．v6.1 blob は plain 名で未解決参照＝C5 BLE実施05 と同じ壁．
        #  ★v6.1 に乗れている検証点：hal blob なら不要だが v6.1 では必須．
        ${BT_ROOT}/host/nimble/port/src/esp_nimble_mem.c
        ${IDF}/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c
        ${IDF}/components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c
        #  ホストスタック本体（C5/C6-hal と同一トリム集合．ble_svc_gap/gatt のみ）
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
    if(ESP32C6_BT_IDF61_SM)
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

endif()  # ESP32C6_BT_IDF61_NIMBLE
