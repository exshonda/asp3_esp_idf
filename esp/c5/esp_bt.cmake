#
#               ESP32-C5 Bluetooth（BLE）統合 Phase D-1／BLE実施03
#
#  コントローラ起動＋VHCIループバック（ホストスタック無し）．設計は
#  docs/ble-c5c6-plan.md，実装ログはdocs/ble-c5c6.md「BLE実施03」．
#
#  ★ライブラリ世代の選定（本ラウンドの中心判断）：C6のBLE実施01は
#  hal submodule（esp-hal-3rdparty）のcontroller/esp32c6/bt.c＋
#  libble_app.aを採用したが，本ファイル（C5）は**ESP-IDF v6.1
#  （~/tools/esp-idf-v6.1）からbt.c／ble.c／libble_app.a／esp_phy／
#  esp_coexを採用する**——esp_wifi.cmakeが確立した「hal世代
#  （v8/os_adapter 0x08）のlibphy.aはeco2シリコンのPHY RX較正で
#  収束せずハングする（実施09）．IDF v6.1（v9/os_adapter 0x09）の
#  libphy.aは収束する（実施10）」という実機確定事実がBTにもそのまま
#  適用されるため（BTコントローラもesp_bt_controller_enable()内で
#  esp_phy_enable(PHY_MODEM_BT)を呼び，WiFiと全く同じlibphy.a／
#  register_chipv7_phy経路を通る）．hal世代のBT blobとv6.1世代の
#  PHY blobを手で混ぜる「ハイブリッド」構成は，Espressifが実際には
#  検証していないblob-ABI境界を新規に作ることになるため採らない
#  （advisorレビュー指摘．詳細はbt/bt_shim.c冒頭コメント）．
#
#  副次的な発見：IDF v6.1のcontroller/esp32c5/bt.cはhal submodule版
#  （platform/os.hのesp_os_*経由）と異なり，**C3の旧世代bt.cと同じ
#  プログラミングモデル**（FreeRTOS API＝xTaskCreatePinnedToCore／
#  vTaskDeleteを直接呼び，割込みは標準esp_intr_alloc/esp_intr_free
#  APIを直接呼ぶ）を採用している．そのためbt/stub/includeは
#  C6版のような専用platform/os.hを新設せず，**C3のbt/stub/include
#  一式（freertos/*.h＋esp_partition.h）をそのまま再利用する**
#  （下記1節）．npl_os_*→npl_freertos_*橋渡しシム・nimble_port_os.h
#  リダイレクトシム（C6のBLE実施01で必要だった上流ドリフト吸収）は
#  v6.1のソースツリー自体に当該ドリフトが存在しないため不要．
#
#  RAM予算のためESP32C5_WIFIとの同時ONは現時点で未対応
#  （FATAL_ERRORはtarget.cmake側）．
#

if(ESP32C5_BT)

set(BT_TARGETDIR ${ESP_CHIP_DIR}/bt)

#
#  esp_wifi.cmakeと同一のIDF v6.1パス（実施10で確立．eco2 C5対応の
#  matched set）．BTもここからbt/phy/coexを採る．
#
#  ★BT v5.5.4統一「実現性」判定（docs/blob-unify-v554.md「BTの
#  v5.5.4実現性判定」節）：ASP3_BT_IDF_V554=ONで~/tools/esp-idf
#  （WiFi統一と同じv5.5.4ツリー）へ切替え可能（reversible，既定OFF＝
#  v6.1のまま）．事前md5実測（同ドキュメント参照）：libble_app.a・
#  libphy.a・libbtbb.aはv5.5.4/v6.1間でC5もC6も**バイト完全一致**
#  （register_chipv7_phy含む）——「v5.5.4はv8＝hal同様ハングする
#  かもしれない」という当初懸念は，WiFi os_adapter ABIマクロ
#  （0x08/0x09）を物理blobの代理指標にした早合点で，実測で否定された．
#  唯一バイト不一致なのはlibcoexist.a（BT単体では非活性が濃厚）．
#  bt.c/ble.cソース差分は283行のみで，差分の大半はCONFIG_BT_LE_SM_
#  LEGACY/SC未定義（本ビルドでは常に0）によりコンパイル対象外の
#  死コード（mbedtls/tinycrypt鍵合意）——実働に影響しない．
#
#  ★★evidence-c5-05 §2 による上記コメントの訂正（md5実測）：
#  「v5.5.4 blob＝v6.1 blob バイト完全一致」は**誤り**であった。当時
#  「v5.5.4」として測っていた `~/tools/esp-idf` は **`v5.5.4-1169-gbb2188bf`
#  （release/v5.5 の先端）＝タグではない**。実測すると：
#      libble_app/libphy/libbtbb : +1169 ≡ **v6.1**（3/4一致）
#      真の v5.5.4 タグ(submodule) : 4/4 とも +1169/v6.1 と**別物**
#      真の v5.5.4 タグ ≡ **hal**（4/4 バイト一致）
#  ∴ 「WiFi・BT 双方 v5.5.4＝統一完了」という記録は**成立していなかった**
#  （WiFi=submodule＝真のv5.5.4／BT=+1169≡v6.1）。下の submodule 供給への
#  移行によって**初めて実際に統一される**。
#
#  ★供給移行（.steering/20260716-c3c5c6-esp-idf-supply-migration）：
#  ON（既定）は **esp-idf submodule（真の v5.5.4 タグ＝735507283d）**を指す
#  ＝WiFi（ESP_SUP_DIR）と同一ツリー・外部絶対パス非依存。
#  OFF は従来どおり外部 v6.1 ツリー＝可逆 fallback。
#
option(ASP3_BT_IDF_V554 "Use the esp-idf submodule (true v5.5.4 tag) BT controller/phy/coexist tree instead of external v6.1. Default ON = supply unified with WiFi (ESP_SUP_DIR). OFF = external v6.1 fallback (reversible)" ON)

#
#  ★2026-07-17：OFF 側の退避先だった `set(IDF $HOME/tools/esp-idf-v6.1)`
#  は **個人の絶対パス直書き**で，(a) CACHE でないので `-D` で上書きできず，
#  (b) EXISTS チェックが無いため，そのツリーが無いPCでは「v6.1 が無い」とは
#  言わずに **存在しないパスを -I/-L し続けて意味不明な未解決参照で死ぬ**。
#  ⇒ C6（`esp_bt.cmake:120` の `ESP_IDF61_DIR`）が既に確立している型を転写する
#  ＝**新規規約の発明ではない**。C3 も同型（`ASP3_BT_IDF_V554_DIR`＝ON 側の
#  ツリーを CACHE＋EXISTS で検証）。
#
set(ESP_IDF61_DIR $HOME/tools/esp-idf-v6.1 CACHE PATH
    "Path to an ESP-IDF v6.1 tree (NOT a submodule; supplies the C5 BT/BLE matched set when ASP3_BT_IDF_V554=OFF). Override with -DESP_IDF61_DIR=<path>")

if(ASP3_BT_IDF_V554)
    #  submodule（ESP_SUP_DIR と同一ツリー）。外部絶対パスを撤去。
    set(IDF ${IDF_V554})
else()
    set(IDF ${ESP_IDF61_DIR})
    if(NOT EXISTS ${IDF}/components/bt/controller/esp32c5/bt.c)
        #  ★案内する退避先は «実在するもの» だけを書く（C6 evidence-c6-09 の
        #  教訓＝旧文言が削除済みの hal 経路を案内して «存在しない指示» に
        #  なっていた）。C5 の退避先は既定へ戻すこと＝submodule の真の v5.5.4。
        message(FATAL_ERROR
            "ESP32C5_BT: -DASP3_BT_IDF_V554=OFF selects the external IDF v6.1 "
            "matched set, but no v6.1 tree was found at ESP_IDF61_DIR='${IDF}'. "
            "v6.1 is NOT vendored as a submodule, so this path only exists if you "
            "cloned it yourself. Either pass -DESP_IDF61_DIR=<path-to-esp-idf-v6.1>, "
            "or just use the default (-DASP3_BT_IDF_V554=ON = the esp-idf submodule, "
            "true v5.5.4 tag 735507283d), which is the supply that reaches "
            "D-1 / W2 / D-2c / D-2d on real hardware at TRUE COLD "
            "(.steering/20260716-c3c5c6-esp-idf-supply-migration/"
            "evidence-c5-05-bt-supply-migration.md). "
            "If the submodule itself is missing: `git submodule update --init esp-idf`.")
    endif()
endif()
set(BT_CHIP_SERIES esp32c5)

#
#  ESP32C5_BT_NIMBLEの判定を先出しする（下の「2. ソースファイル」節で
#  hci_driver_standard.c／hci_driver_nimble.cの二者択一・CONFIG_BT_
#  CONTROLLER_ONLYのD-1限定化に使うため．ブロック本体＝NimBLEホストの
#  ソース/インクルード追加は末尾のD-2a節（BLE実施05）で行う）．
#  RAM予算のため既定はOFF．NimBLEを要するアプリ（ble_host_smoke_c5）で
#  自動でON．D-1のbt_smoke_c5は痩せたまま保つ．
#
option(ESP32C5_BT_NIMBLE "Enable NimBLE host stack on top of BT controller (Phase D-2a/BLE実施05)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c5")
    set(ESP32C5_BT_NIMBLE ON)
endif()

list(APPEND ASP3_COMPILE_DEFS
    TOPPERS_ESP32C5_BT
    ESP_PLATFORM
    CONFIG_BT_ENABLED
    CONFIG_BT_CONTROLLER_ENABLED
    CONFIG_IDF_TARGET_ESP32C5=1
    CONFIG_FREERTOS_NUMBER_OF_CORES=1
    #  CONFIG_BT_CONTROLLER_ONLY=1はD-1（NimBLEホスト無し）限定．実ESP-IDFの
    #  KconfigではCONTROLLER_ONLYとNIMBLE_ENABLEDは排他選択のため，NimBLE
    #  ON時に立てない（C6のBLE実施02＝advisorレビュー指摘と同じ分離）．
    #  実際の定義は下の「2. ソースファイル」節の if(NOT ESP32C5_BT_NIMBLE) 内．
    #  VHCI（HCI_TRANSPORT_VHCI経由．esp_vhci_host_*公開API）を選択
    CONFIG_BT_LE_HCI_INTERFACE_USE_RAM=1
    #  msys（HCI ACL用共有mbufプール）の初期化をコントローラ内部へ委譲
    #  （C6のBLE実施01と同じ判断．esp32c5 Kconfig既定値もy）
    CONFIG_BT_LE_MSYS_INIT_IN_CONTROLLER=1
    CONFIG_BT_LE_MSYS_1_BLOCK_COUNT=12
    CONFIG_BT_LE_MSYS_1_BLOCK_SIZE=256
    CONFIG_BT_LE_MSYS_2_BLOCK_COUNT=24
    CONFIG_BT_LE_MSYS_2_BLOCK_SIZE=320
    CONFIG_BT_LE_MSYS_BUF_FROM_HEAP=1
    #  npl_os_freertos.cのcallout実装にesp_timer_*（bt/bt_shim.c提供）を
    #  選択し，FreeRTOSソフトタイマ経路を避ける（C6と同じ判断）
    CONFIG_BT_LE_USE_ESP_TIMER=1
    #  esp_bt_cfg.hが無条件参照する項目（esp32c5 Kconfig.inの既定値）
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
    #  CONFIG_XTAL_FREQ／CPUクロックはesp_wifi.cmakeと同じ理由で
    #  hal/nuttx/esp32c5/include/sdkconfig.hに実体が無い（C5にはNuttX
    #  ポートのsdkconfig.h一式が存在しない．esp_wifi.cmakeのCONFIG_
    #  IDF_TARGET_ESP32C5コメント参照）ため，本ブロックで明示する．
    #  ★実機ビルドで判明：esp_bt.h（IDF v6.1）のBT_CONTROLLER_INIT_
    #  CONFIG_DEFAULT()マクロがCONFIG_XTAL_FREQ／CONFIG_ESP_DEFAULT_
    #  CPU_FREQ_MHZを直接参照する（C6はhal/nuttx/esp32c6/include/
    #  sdkconfig.hの間接定義に頼れたが，C5には同ファイルが無いため
    #  ここで明示的に与える必要がある——C6のesp_bt.cmakeコメントが
    #  警告した「後勝ちで無効化される」罠はC5では発生しない＝
    #  素直に-Dで定義してよい）．値は実施32/34確定のC5実測値
    #  （48MHz XTAL直結，BBPLL経由240MHz．asp3/arch/riscv_gcc/esp32c5/
    #  esp32c5.hのCORE_CLK_MHZ=240と一致させる）．
    CONFIG_XTAL_FREQ=48
    CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=240
    CONFIG_ESPRESSIF_CPU_FREQ_MHZ=240
    #  esp_ipc.hのesp_ipc_func_t等はCONFIG_ESP_IPC_ENABLE無しでは
    #  丸ごと非公開になる
    CONFIG_ESP_IPC_ENABLE
    #  PLL温度追従は較正データ永続化前提．本ビルドは毎回フル較正の
    #  ため無効化で十分（esp_wifi.cmakeと同じ理由）
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    #  MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL：phy_init.cが直値のビット
    #  マスクを期待するため（esp_wifi.cmakeと同じ理由）
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
)

#
#  C6のBLE実施01で確立した4つの強制includeをそのまま踏襲する（理由は
#  各行コメント参照．v6.1のnpl_os_freertos.c／os_mbuf.h／os_mempool.h
#  もhal版と同じ事情——1269行中の大半が無変更移植のため，同じ罠を
#  同じ場所で踏む．実機ビルドで実際に必要か確認しながら調整する）．
#
list(APPEND ASP3_COMPILE_OPTIONS
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include soc/soc_caps.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_attr.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_idf_version.h>"
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include sys/param.h>"
    #  bt/porting/mem/os_mempool.c（BIT()マクロ．実機ビルドで判明．
    #  実ESP-IDFではビルドシステムがesp_bit_defs.hを暗黙includeする
    #  前提のヘッダで，自ファイルではincludeしない．他のBTソース
    #  （bt.c等）は別経路で既にesp_bit_defs.hが見えているため影響なし）
    "$<$<COMPILE_LANGUAGE:C>:SHELL:-include esp_bit_defs.h>"
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス
#  ------------------------------------------------------------------
#  ★C3のbt/stub/include（freertos/*.h＋esp_partition.h）を再利用する
#  （v6.1のbt.cはC3と同じプログラミングモデル）．
#  ★BLE実施05：C5独自のbt/stub/include（bt_nimble_config.h＝LEGACY_VHCI=0）を
#  PREPENDでC3のbt/stub/include（bt_nimble_config.h＝LEGACY_VHCI=1）より
#  前に置く．順序を誤るとC3版が先に見つかりLEGACY_VHCI=1になる
#  （mbufヘッダ余白計算がトランスポートと不整合＝サイレントな実行時
#  バッファバグ）．D-1（bt_smoke_c5）はbt_nimble_config.hをincludeしない
#  ため本PREPENDは無害．
#
list(PREPEND ASP3_INCLUDE_DIRS ${ESP_COMMON_DIR}/bt/stub/include)

list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_COMMON_DIR}/bt/stub/include
    ${ESP_CHIP_DIR}/wifi
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
    #  ★供給移行（evidence-c5-05）：以下は全て ESP_SUP_DIR（既定＝esp-idf
    #  submodule）から取る。**ヘッダとソースを揃えて同じ供給元から取る**
    #  （evidence-02 §1.2 の一般則）。hal 混成のままだと hal の
    #  esp_modem_clock.h が要求する `shared_periph_module_t` を
    #  esp-idf の soc/periph_defs.h が持たず未定義型エラーになる（§1.2）。
    ${ESP_SUP_DIR}/components/esp_hw_support/include
    ${ESP_SUP_DIR}/components/esp_hw_support/include/soc
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/private_include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/include
    ${ESP_SUP_DIR}/components/esp_system/include
    #  esp_private/wifi.h（phy_init.cが要求．BTもWi-Fiと同じPHY実
    #  ソースを使うため必要．esp_wifi.cmakeはIDF側を使うが，esp_private/
    #  wifi.hはESP-IDF公開APIヘッダのためIDF/hal双方に存在．IDFを優先）
    ${IDF}/components/esp_wifi/include
    ${IDF}/components/esp_phy/include
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_pm/include
    ${ESP_SUP_DIR}/components/esp_timer/include
    ${IDF}/components/esp_coex/include
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include/${BT_CHIP_SERIES}
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}
    ${ESP_SUP_DIR}/components/heap/include
    ${ESP_SUP_DIR}/components/log/include
    ${ESP_SUP_DIR}/components/riscv/include
    ${ESP_SUP_HAL_gpio}/include
    ${ESP_SUP_HAL_gpio}/${BT_CHIP_SERIES}/include
    ${ESP_SUP_HAL_clock}/include
    ${ESP_SUP_HAL_clock}/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/efuse/include
    ${ESP_SUP_DIR}/components/efuse/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_event/include
    #  hal/pmu_ll.h・hal/pmu_hal.h（wifi/esp_shim.cのesp_shim_modem_icg_init
    #  が要求．esp_wifi.cmakeにも同じ2行がある＝WiFi/BT共有ファイルの
    #  依存としてBT側でも要る）
    ${ESP_SUP_HAL_pmu}/include
    ${ESP_SUP_HAL_pmu}/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/private_include
    #  hal/rtc_timer_hal.h（hal供給時）／hal/lp_timer_hal.h（esp-idf供給時）．
    #  rtc_time.cが要求．**ソースも同じ供給元から取る**ので名前差は消える
    #  （evidence-02 §1.2）．
    ${ESP_SUP_HAL_rtc_timer}/include
    ${ESP_SUP_HAL_rtc_timer}/${BT_CHIP_SERIES}/include
    #  hal/timg_ll.h（hal）／hal/timer_ll.h（esp-idf）．同上．
    ${ESP_SUP_HAL_timg}/include
    ${ESP_SUP_HAL_timg}/${BT_CHIP_SERIES}/include
    #  esp_wifi.cmake §1bと同じ理由で必要になったパス
    #  （modem/i2c_ana_mst_reg.h・regi2c_impl.h等，IDFのsocレイアウト
    #  でのみ解決されるヘッダ．hal socより後に置く）
    ${IDF}/components/esp_phy/esp32c5/include
)

if(ASP3_BT_IDF_V554)
    #  v5.5.4のesp_wifi_types_generic.hはv6.1と異なり"esp_interface.h"を
    #  直接includeする（v6.1は不要．実機ビルドで発覚：phy_init.cが
    #  esp_private/wifi.h経由でこのヘッダ連鎖を辿る）。v5.5.4のesp_
    #  interface.h実体はesp_hw_support/includeにある（halには存在しない
    #  ファイルのため，hal側のesp_hw_support/includeが先にあっても
    #  衝突せず素通りする）。
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
    #  ★os_mempool.c は **blob 世代によって要否が逆転する**（evidence-c5-05 §4．
    #  nm で実測）．「C6のBLE実施01の教訓はC5では成立しない」という従来の
    #  記述は **v6.1(+1169) blob に限った話**だった：
    #    - v6.1/+1169 blob (c2785c98)：os_mempool.c.o を**同梱せず**，
    #      os_memblock_get 等を **plain名で未解決参照**→自前リンクが要る．
    #    - v5.5.4タグ blob (015db3db)：**os_mempool.c.o を同梱**し，
    #      内部で `r_os_memblock_get` 等（ROM側）を参照→**自前リンクは不要**
    #      （自前で持つと多重定義）．＝C6のBLE実施01の教訓がそのまま該当．
    #  stock の v5.5.4 も同じ判断をしている：bt/CMakeLists.txt の
    #  CONFIG_BT_LE_CONTROLLER_NPL_OS_PORTING_SUPPORT ブロック（:693-697）は
    #  os_mempool.c を**コンパイルしない**（:908 の os_mempool.c は
    #  `if(NOT ..._NPL_OS_PORTING_SUPPORT)` 側＝C5は非該当）．
    #  ⇒ 供給元に応じて切替える（＝「揃えて取る」の一例）．
    #  ★ヘッダと**揃えて**同じ供給元から取る（evidence-02 §1.2）．
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/esp_clk_tree.c
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_SUP_HAL_clock}/esp32c5/clk_tree_hal.c
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/rtc_time.c
    ${IDF}/components/bt/porting/transport/src/hci_transport.c
    #  hci_driver_standard.c と hci_driver_nimble.c（D-2a節）は共に
    #  hci_driver_vhci_opsを定義するため二者択一（同時リンク不可）．
    #  D-1（controller-only．bt_smoke_c5）はstandard版を下の
    #  if(NOT ESP32C5_BT_NIMBLE)ブロックで追加する．NimBLE ON時
    #  （ble_host_smoke_c5）はD-2a節がnimble版を追加する．
    ${BT_TARGETDIR}/bt_shim.c
    #  PHY／クロック／ペリフェラルの実ソース（esp_wifi.cmakeと同じIDF
    #  v6.1版．eco2対応のmatched set．BTもWiFiと同じ無線ハードウェアを
    #  使うため必要）
    ${IDF}/components/esp_phy/src/phy_init.c
    ${IDF}/components/esp_phy/src/phy_common.c
    ${IDF}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${IDF}/components/esp_phy/src/lib_printf.c
    #  btbb_init.c（esp_btbb_enable/disable．bt.cが呼ぶ．C6のBLE実施01で
    #  必要と判明．esp_phy/lib/${BT_CHIP_SERIES}にlibbtbb.aが同居）
    ${IDF}/components/esp_phy/src/btbb_init.c
    #  modem_clock.c／modem_clock_hal.cはWiFi非同時ONの制約があるため
    #  BT単体ビルドでも自前で持つ必要がある（esp_wifi.cmakeのif
    #  (ESP32C5_WIFI)ブロック内にありBTからは見えないため．chip依存の
    #  実ソースはhal側を使う——PHY/coexとは異なりmodem_clockはeco2非互換
    #  の対象外＝hal版で問題ない．esp_wifi.cmake §6のソース一覧と同一）
    #  ★modem_clock.c／periph_ctrl.c は §1.2 の版差の当事者
    #  （`shared_periph_module_t` vs `periph_module_t`）＝**ヘッダと同じ
    #  供給元から取らねばならない**．esp_wifi_v8.cmake §6 と同一の写像．
    ${ESP_SUP_DIR}/components/esp_hw_support/modem_clock.c
    ${ESP_SUP_DIR}/components/hal/${BT_CHIP_SERIES}/modem_clock_hal.c
    ${ESP_SUP_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_SUP_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_SUP_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    ${ESP_SUP_DIR}/components/hal/efuse_hal.c
    ${ESP_SUP_DIR}/components/hal/${BT_CHIP_SERIES}/efuse_hal.c
)

if(NOT ASP3_BT_IDF_V554)
    #  v6.1(+1169) blob のみ os_mempool.c を自前で要求する（上記コメント参照）．
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${IDF}/components/bt/porting/mem/os_mempool.c
    )
endif()

if(NOT ESP32C5_BT_NIMBLE)
    #  ★D-1＝controller-onlyスモークテスト（NimBLEホスト無し）限定．
    #  CONFIG_BT_CONTROLLER_ONLYとCONFIG_BT_NIMBLE_ENABLEDは実ESP-IDFの
    #  Kconfigでは同時に1にならない排他選択（C6のBLE実施02＝advisor
    #  レビュー指摘）．NimBLE ON時は立てず，hci_driver_standard.cも
    #  外す（hci_driver_vhci_opsの多重定義回避）．
    list(APPEND ASP3_COMPILE_DEFS
        CONFIG_BT_CONTROLLER_ONLY=1
    )
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${IDF}/components/bt/porting/transport/driver/vhci/hci_driver_standard.c
    )
endif()

#
#  ------------------------------------------------------------------
#  3. リンクライブラリパス・ライブラリ（IDF v6.1．esp_wifi.cmakeと
#     同じmatched set＝eco2対応版）
#  ------------------------------------------------------------------
#
list(APPEND ASP3_LINK_OPTIONS
    -L${IDF}/components/bt/controller/lib_${BT_CHIP_SERIES}/${BT_CHIP_SERIES}-bt-lib
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
#  4. ROM関数ld（IDF v6.1のesp32c5 ldセット．esp_wifi.cmakeと同じ
#     理由でeco3.ldは除外——同一のRAM版PHY関数上書き問題がBT経路にも
#     及ぶ可能性がある．net80211/pp（WiFi専用ライブラリのROM解決）は
#     BTはリンクしないため除外．systimer.ldはIDF v6.1に存在しない
#     （esp_wifi.cmake【実施10】コメント参照）．
#  ------------------------------------------------------------------
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
#  Phase D-2a／BLE実施05：NimBLE ホストスタック（IDF v6.1版）
#  ==================================================================
#
#  D-1（コントローラ＋VHCI，bt_smoke_c5）の上に NimBLE ホストを載せる．
#  C6のBLE実施02（esp32c6_espidf/esp_bt.cmakeのESP32C6_BT_NIMBLEブロック）
#  の逐語的な転写だが，★ソースはhal submoduleではなくIDF v6.1
#  （${IDF}）から採る——C5のbt/phy/coex/nimbleを同一matched setで
#  揃える（本ファイル冒頭のライブラリ世代選定と同じ理由．hal世代の
#  nimble＋v6.1世代のPHY/controller blobを混ぜない）．
#
#  C6/C5は新世代コントローラ（SOC_ESP_NIMBLE_CONTROLLER=1）のため，
#  nimble_port.cのesp_nimble_init()内部でesp_nimble_hci_init()呼出しが
#  コンパイルアウトされる＝C3のLEGACY VHCI経路は存在しない．C5は
#  hci_transport.c（D-1で既存）＋hci_driver_nimble.c＋hci_esp_ipc.cを
#  使う（D-1のhci_driver_standard.cとhci_driver_vhci_opsを取り合う
#  二者択一．上の if(NOT ESP32C5_BT_NIMBLE) で分離済み）．
#
#  ★C6のBLE実施02との差分（IDF v6.1固有）：
#    - -include sdkconfig.h は追加しない：C5はNuttX版sdkconfig.hを持たず，
#      host .c の #include "sdkconfig.h" は sdkconfig_stub/sdkconfig.h
#      （target.cmakeが全ビルド共通で追加済み）へ解決される．
#    - TRUE=1／BT_HCI_LOG_INCLUDED=0 は追加しない：v6.1の nimble_port.c は
#      bt_common.h（TRUE／BT_HCI_LOG_INCLUDED の定義元）を #if より前に
#      includeするため，C6/halで踏んだ順序バグは v6.1 には存在しない．
#      （もし実機ビルドで hci_log/bt_hci_log.h が要求されたら，そのとき
#      C6と同じ -DTRUE=1 -DBT_HCI_LOG_INCLUDED=0 を本ブロックへ追加する）．
#
if(ESP32C5_BT_NIMBLE)

    set(NIMBLE_ROOT ${IDF}/components/bt/host/nimble/nimble/nimble)
    set(BT_ROOT ${IDF}/components/bt)
    set(TINYCRYPT_ROOT ${IDF}/components/bt/host/nimble/nimble/ext/tinycrypt)

    #  ---- ★D-2d：SMP（ペアリング／ボンディング）有効化 ----
    #  C3 の ESP32C3_BT_SM の C5 版（tinycrypt は IDF パス）．ON時は
    #  MYNEWT_VAL_BLE_SM_LEGACY/SC=0 の «蓋» を外し，SC=ECDH P-256 の crypto を
    #  vendored tinycrypt で供給，bond store は ble_store_ram（IDF文脈で空）でなく
    #  ble_store_config（PERSIST=0＝RAM）を使う（S3 §5.2）．OFF で D-2a(sync/adv)
    #  構成へ完全復帰＝可逆．C5 は C3 と別 blob(015db3db)＝C3 の «2個目暗号化ACL»
    #  の壁は非該当の公算（docs/bt-shim.md「別PC引き継ぎ要点」）．
    option(ESP32C5_BT_SM "Enable NimBLE SMP pairing/bonding on C5 (Phase D-2d, tinycrypt)" ON)

    #  （D-2d bond診断）SVC_PERROR：esp_shim の «想定外» サービスコールエラー
    #  （非E_OK かつ 非E_CTX/E_TMOUT/E_QOVR）を g_svc_err_* グローバルへ記録＝
    #  app が RTC STORE へミラーして esptool で回収（C5はコンソール不安定のため
    #  RTC経由）．暗号後の鍵配布で失敗する API を特定する．既定OFF＝非回帰．
    option(ESP32C5_BT_APIERR_TRACE "Record unexpected esp_shim svc-call errors to globals (D-2d bond diag)" OFF)
    if(ESP32C5_BT_APIERR_TRACE)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_APIERR_TRACE)
    endif()

    #  （D-2d bond診断）RXTRACE：暗号確立«後» の SMP 鍵配布フェーズで «どちら向きの
    #  ACL が止まるか» を局在化する --wrap 計装。ble_hs_hci_evt_process(暗号検出ゲート)・
    #  ble_mqueue_put(RX)・ble_sm_tx(我々のSMP送出試行)・ble_transport_to_ll_acl_impl
    #  (host→controller ACL)を横取りし LP_AON STORE3 へ記録（bt/rx_trace.c）。
    #  STORE3 を使うため APIERR_TRACE とは排他（同時ONで二重書込み）。既定OFF＝非回帰。
    #  （E1／evidence-c5-09）PEND_DIAG：pend_ring の «滞留が実在するか» を測る計装。
    #  レビュー仮説①（C5 app に周期 flush が無い＝SMP PDU が pend_ring に滞留する）を
    #  «修正を書く前に» 反証するためのもの。★計器は «既存カウンタのミラーのみ»：
    #    - wifi_v8/esp_shim.c に読むだけのアクセサ esp_shim_pend_stats() を 1 個追加
    #      （★push/flush の hot path には1命令も足さない＝evidence-c3-04 の
    #        «20語 dump を hot path に置いて bond を壊した» 事故の反対）
    #    - app の既存 storm_monitor_task（200ms周期）が LP_AON STORE4 へ 1 レジスタ
    #      書込み（書込み回数は従来と同一）。STORE4 の従来値 esp_shim_int_count[1] は
    #      evidence-c5-08 §8.1/§11 が «3セルとも 0＝情報価値が尽きている» と実測記録済。
    #  ★--wrap を使わない＝«噛んだか» の確認が要る計装を避ける。既定OFF＝非回帰。
    option(ESP32C5_BT_PEND_DIAG "Mirror pend_ring dwell high-water + engage count to LP_AON STORE4 (E1 diag)" OFF)
    if(ESP32C5_BT_PEND_DIAG)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_C5_PEND_DIAG)
    endif()

    #  （E-RX／evidence-c5-10）RXTRACE は STORE4 へ «暗号前の SMP 往復生カウンタ»
    #  （タグ 0xE2・ble_l2cap_rx を追加 wrap・CID=0x0006 覗き）も記録するよう拡張。
    #  STORE4 の前用途 E1 計器（PEND_DIAG・タグ 0xE1）は evidence-c5-09 で完結済＝
    #  上書き可。★同一レジスタへの二重書込みになるため RXTRACE と PEND_DIAG の
    #  同時 ON は禁止（下の FATAL で機械的に落とす）。
    option(ESP32C5_BT_RXTRACE "Trace post-encryption RX/TX ACL pipeline via --wrap (D-2d bond diag)" OFF)
    if(ESP32C5_BT_RXTRACE AND ESP32C5_BT_PEND_DIAG)
        message(FATAL_ERROR
            "ESP32C5_BT_RXTRACE and ESP32C5_BT_PEND_DIAG both write LP_AON STORE4 "
            "(0x600B1010) and cannot be enabled together. Turn one of them OFF.")
    endif()
    if(ESP32C5_BT_RXTRACE)
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${BT_TARGETDIR}/rx_trace.c)
        list(APPEND ASP3_LINK_OPTIONS
            -Wl,--wrap=ble_hs_hci_evt_process
            -Wl,--wrap=ble_mqueue_put
            -Wl,--wrap=ble_sm_tx
            -Wl,--wrap=ble_transport_to_ll_acl_impl
            -Wl,--wrap=ble_sm_enc_change_rx
            -Wl,--wrap=ble_l2cap_rx
        )
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_RXTRACE)
    endif()

    #  ---- コンパイル定義 ----
    #  CONFIG_BT_NIMBLE_*一式はbt/stub/include/bt_nimble_config.h（C5専用版．
    #  LEGACY_VHCI=0）で供給する．SM OFF時のみ SM_LEGACY/SC=0 でble_sm*.cを
    #  near-empty化しtinycrypt/mbedTLSリンクを回避する（C3/C6のD-2aと同じ判断）．
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_NIMBLE)

    if(ASP3_BT_IDF_V554)
        #  ★上のブロックコメントが «もし実機ビルドで hci_log/bt_hci_log.h が
        #  要求されたら，そのとき C6と同じ -DTRUE=1 -DBT_HCI_LOG_INCLUDED=0 を
        #  本ブロックへ追加する» と予告していた条件が，供給移行で**現実化した**
        #  （evidence-c5-05 §4）。当該コメントは「v6.1 の nimble_port.c は
        #  bt_common.h を #if より前に include するため C6/hal で踏んだ順序バグは
        #  v6.1 には存在しない」と述べており**それ自体は正しい**が，
        #  **v5.5.4 タグ版には順序バグが存在する**（実測）：
        #      :48  #if (BT_HCI_LOG_INCLUDED == TRUE)
        #      :49  #include "hci_log/bt_hci_log.h"   ← 存在しない
        #      :51  #include "bt_common.h"            ← TRUE の定義元（**後**）
        #  ＝TRUE も BT_HCI_LOG_INCLUDED も #if 到達時点で未定義→`0==0`＝真と
        #  なり fatal error。v6.1 は :24 で esp_nimble_cfg.h を追加 include して
        #  この穴を塞いでいる（v5.5.4 には該当行が無い）。
        #  ⇒ C6 の esp_bt.cmake:437-438 と**同一の確立済み対処**を適用し
        #  `0==1`＝偽へ倒す（後段で bt_common.h が同値へ再定義＝以降無矛盾）。
        #  v6.1 fallback（OFF）は従来どおり不要なので**ガード内に閉じる**
        #  ＝可逆性・非回帰を保つ。
        list(APPEND ASP3_COMPILE_DEFS
            TRUE=1
            BT_HCI_LOG_INCLUDED=0
        )
    endif()

    if(ESP32C5_BT_SM)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C5_BT_SM)
    else()
        list(APPEND ASP3_COMPILE_DEFS
            MYNEWT_VAL_BLE_SM_LEGACY=0
            MYNEWT_VAL_BLE_SM_SC=0
        )
    endif()

    #  bt_nimble_config.h（CONFIG_BT_NIMBLE_*）・syscfg/syscfg.h（MYNEWT_VAL）
    #  を強制include．D-1の -include soc/soc_caps.h 等と衝突しないよう
    #  SHELL:接頭辞を使う．
    list(APPEND ASP3_COMPILE_OPTIONS
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include bt_nimble_config.h>"
        "$<$<COMPILE_LANGUAGE:C>:SHELL:-include syscfg/syscfg.h>"
    )

    #  ---- インクルードパス ----
    #  porting/nimble/include（syscfg/syscfg.h）・porting/npl/freertos/
    #  include・host/nimble/port/include（esp_nimble_init/mem）はD-1の
    #  インクルードリストに既に含まれる（-Iの並びで先に来る＝優先解決）．
    list(APPEND ASP3_INCLUDE_DIRS
        ${NIMBLE_ROOT}/host/include
        ${NIMBLE_ROOT}/include
        ${NIMBLE_ROOT}/transport/include
        ${NIMBLE_ROOT}/host/services/gap/include
        ${NIMBLE_ROOT}/host/services/gatt/include
        ${NIMBLE_ROOT}/host/util/include
        ${NIMBLE_ROOT}/host/store/ram/include
    )
    if(ESP32C5_BT_SM)
        #  D-2d：ble_store_config と tinycrypt（SC の uECC P-256 ＋ AES-CMAC）
        list(APPEND ASP3_INCLUDE_DIRS
            ${NIMBLE_ROOT}/host/store/config/include
            ${TINYCRYPT_ROOT}/include
        )
    endif()

    #  ---- ソースファイル ----
    #  D-1で既に npl_os_freertos.c／os_msys_init.c／bt_osi_mem.c／
    #  os_mempool.c／hci_transport.c はリンク済み．
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${BT_ROOT}/porting/transport/driver/vhci/hci_driver_nimble.c
        ${NIMBLE_ROOT}/transport/esp_ipc/src/hci_esp_ipc.c
        ${IDF}/components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c
        ${IDF}/components/bt/host/nimble/nimble/porting/npl/freertos/src/nimble_port_freertos.c
        #  ホストスタック本体（C6のBLE実施02と同一トリム集合．
        #  ble_svc_gap/gatt のみ採用．他サービス・永続ボンディング・
        #  新機能（ble_cs/ble_ead/ble_aes_ccm/ble_gattc_cache*/ble_eatt）は
        #  sync/adv到達には不要のため不採用）
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
    if(NOT ASP3_BT_IDF_V554)
        #  ★esp_nimble_mem.c も os_mempool.c と同じく **供給元で要否が変わる**
        #  （evidence-c5-05 §4．版差）：
        #    - v6.1/+1169：esp_nimble_mem.h が `nimble_platform_mem_malloc` を
        #      **`nimble_mem_malloc` へ写像**し，その実体は esp_nimble_mem.c に
        #      ある（heap_caps_* ＝esp_shim_libc.c へ委譲）＝自前リンクが要る．
        #    - v5.5.4タグ：**esp_nimble_mem.c 自体が存在しない**．
        #      esp_nimble_mem.h が `nimble_platform_mem_malloc` を
        #      **`bt_osi_mem_malloc` へ直接マクロ写像**する（同ヘッダ:61-64．
        #      「This file should be replaced with bt_osi_mem.h」というコメント
        #      どおり bt_osi_mem.h へ統合済み）＝実体は既にリンク済みの
        #      porting/mem/bt_osi_mem.c が供給する＝**自前リンク不要**．
        #      stock v5.5.4 の bt/CMakeLists.txt も esp_nimble_mem.c を積まない
        #      （:889 は nvs_port.c のみ）．
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES
            ${BT_ROOT}/host/nimble/port/src/esp_nimble_mem.c
        )
    endif()

    if(ESP32C5_BT_SM)
        #  D-2d：bond store は ble_store_config（PERSIST=0＝RAM，NVS不使用．
        #  ble_store_ram.c は IDF文脈で空＝S3 §5.2 の真因）＋ tinycrypt 必要5ソース
        #  （ble_sm_alg.c の tc_aes*/tc_cmac_*/uECC_* 参照に対応）．
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

endif()

endif()
