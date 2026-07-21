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
set(BT_TARGETDIR ${ESP_CHIP_DIR}/bt)

#
#  ★C3 BT blob統一（docs/blob-unify-v554.md「C3のv5.5.4切替」節）：
#  WiFi（全チップ）とBT（C5/C6）は既にESP-IDF v5.5.4へ統一済み．C3のBTだけ
#  hal submodule（esp-hal-3rdparty）のblobを使い続けていた最後の1個．
#  ASP3_BT_IDF_V554=ONで **controller（bt.c）＋PHYソース＋4つのblob
#  （libbtdm_app/libphy/libbtbb/libcoexist）＋それらが要するesp-component
#  include/libパス** だけをv5.5.4（~/tools/esp-idf＝WiFi/C5/C6統一と同じツリー）
#  へ切替える．**NimBLEホスト＋bt/stub＋wifi/esp_shim.c はhalのまま**——
#  ホストとコントローラのABI境界はHCI（VHCI）であり，C3のbond修正
#  （INTMTX split-intr-lines・esp_shim queue port・CONFIG_BT_NIMBLE_HS_PVCY=1）は
#  全てホスト/シム側でblob非依存のため（memory c3-ble-d2d-gatt-notify-sm）．
#
#  決定的事実（md5実測）：C3 controller blob libbtdm_app.a は
#  hal=dfdadb9d… / v5.5.4=v6.1=d9753a31…（v5.5.4≡v6.1バイト同一・halと相違）．
#  libphy.a・libbtbb.aも同様（hal相違・v5.5.4≡v6.1）＝実体のあるblob変更．
#  v5.5.4のbt.cはhalと91行差＝OSI_VERSION 0x0001000A→0x0001000B ＋ osiテーブルに
#  新フィールド _malloc_retention 追加．よってblobに合わせ **bt.cもv5.5.4へ
#  切替が必須**（halのbt.cのままではosiテーブルABIが不一致）．v5.5.4 bt.cの
#  新依存＝RTC_CNTL_ATOMIC()/PERIPH_RCC_ATOMIC()（hal/v5.5.4双方のesp_hw_support
#  にあり，esp_hw_supportはhalのまま流用）／MALLOC_CAP_RETENTION（下の-Dで供給）／
#  ble_min_conn_interval_enable（v5.5.4 blobにT定義・hal blobには無い．
#  esp_bt.hがCONFIG_BT_CTRL_CHECK_CONFIG_EFF未定義時に
#  CONFIG_BT_CTRL_BLE_MIN_CONN_INTERVAL_ENABLE=1を定義→call有効化→v5.5.4 blobで解決）．
#
#  ★2026-07-21：hal fallback は撤去済み（./hal submodule ごと削除）。
#  BT ツリーは常に esp-idf submodule（真の v5.5.4）から供給する。
#
#  ★2026-07-17：`option(ASP3_BT_IDF_V554 … OFF)` の宣言はここから
#  **下の供給移行ブロックへ移した**（既定を `ASP3_ESPIDF_SUPPLY` に追従させ，
#  供給元の混成を FATAL_ERROR で禁止するため）。**ここで宣言すると，先に
#  cache へ OFF が焼かれて追従ロジックが効かない**（実測：本移行中に踏んだ）。
#
#
#  ------------------------------------------------------------------
#  ★2026-07-17 provenance訂正（evidence-c3-01 §4）：上のブロックの
#  「決定的事実（md5実測）」は **真のv5.5.4タグに対しては成立しない**。
#  ------------------------------------------------------------------
#
#  従来ここは `set(BT_IDF $HOME/tools/esp-idf)` と外部絶対パスを
#  直書きし，それを「v5.5.4」と呼んでいた。**その名前は嘘だった**：
#
#    tree                       libbtdm_app.a   bt.c OSI_VERSION / _malloc_retention
#    -------------------------  --------------  ----------------------------------
#    repo submodule esp-idf/    859e8c8e…       0x0001000B / 有り  ← **真のv5.5.4タグ**
#    hal(b90b1837)              dfdadb9d…       0x0001000A / 無し
#    ~/tools/esp-idf-v6.1       d9753a31…       0x0001000B / 有り
#    ~/tools/esp-idf（本PC）    93abf3c7…       0x0001000A / 無し  ← **v5.5.0**
#
#  ★上の旧コメントが「v5.5.4=v6.1=d9753a31…」と記録した値は，本ラウンドの
#  実測では **v6.1 の値そのもの**＝当時の `~/tools/esp-idf` は **+1169
#  （≡v6.1系）** であり，それを版名で「v5.5.4」と誤認していた
#  （C6 evidence-c6-01 §2.1-3 と同一の罠．**旧記録自身の数値がそれを証明する**）。
#  ⇒ 旧コメントの「libphy.a・libbtbb.aも hal相違」も**真のタグでは偽**
#     （実測：libphy/libbtbb/libcoexist は **真のv5.5.4タグ ≡ hal でバイト一致**）。
#     halと相違するのは **libbtdm_app.a のみ**（＝C6とは構造が違う。C6は
#     BT blob 4/4 が hal と一致＝「v5.5.4統一」が「halへ戻す」と同義だった）。
#
#  ★本PCで従来のパスを使うと **v5.5.0**（bt.c OSI 0x0001000A・retention無し
#  ＝旧コメントの説明と食い違う第4のツリー）を掴む＝再現性が無い。
#  ⇒ 外部絶対パスを撤去し，**既定を submodule（真のv5.5.4タグ）** とする
#     キャッシュ変数へ変更した。`ASP3_BT_IDF_V554` の**既定は OFF のまま**
#     ＝**既定ビルドの挙動は不変**（従来どおり hal）。
#
#  ★★重要な申し送り（BT供給移行は本ラウンドのスコープ外）：
#  HANDOFF §4-4 が記録する「C3 BT の v5.5.4 切替は実機 bond 失敗
#  （AuthenticationTimeout）ゆえ既定 OFF」という**実機エビデンスは，
#  上記のとおり «+1169（≡v6.1系）» に対して得られたものであり，
#  «真のv5.5.4タグ» を一度も測っていない**。∴ 本変更後の
#  `-DASP3_BT_IDF_V554=ON` は **未測定の新しい構成**であって，
#  「bondが失敗すると分かっている構成」ではない。**再測が要る**。
#  （libbtdm_app.a のみ hal と相違＝C3では真の A/B が成立する＝
#   C6 と違って「v5.5.4統一」に実体がある。）
#
set(ASP3_BT_IDF_V554_DIR ${IDF_V554}
    CACHE STRING "ESP-IDF tree supplying the C3 BT controller/phy/blobs when ASP3_BT_IDF_V554=ON. Default = repo submodule esp-idf/ (the TRUE v5.5.4 tag, 735507283d). Override for A/B, e.g. -DASP3_BT_IDF_V554_DIR=/path/to/esp-idf-v6.1")

#
#  ------------------------------------------------------------------
#  ★BT供給移行（2026-07-17．C5 esp_bt.cmake の型を転写＝新規設計ではない）
#  ------------------------------------------------------------------
#
#  C5 は `esp_bt.cmake` の `ESP_HAL_DIR` を **0箇所**にして hal 参照 0 を達成した
#  （実測）。C3 も同じ構造（35箇所）だったので同じ写像を当てた：
#    - **BTツリー**（blob・ROM ld・bt/* ヘッダ・controller bt.c・NimBLE）＝`${BT_IDF}`
#    - **基盤コンポーネント**（esp_hw_support/esp_rom/heap/log/riscv/efuse/…）＝`${ESP_SUP_DIR}`
#    - **hal が分割した `esp_hal_<x>`** ＝ `${ESP_SUP_HAL_<x>}`（esp-idf では `components/hal` に集約）
#
#  ★**移行前は «BTツリー自体» が混成していた**（実測）：
#    `bt/include/...` は `${BT_IDF}` から採るのに，`bt/common/`・`bt/porting/` は
#    `${ESP_HAL_DIR}` 固定だった ⇒ `ASP3_BT_IDF_V554=ON` にすると
#    **esp_bt.h だけ v5.5.4・porting は hal** という**版の混成**が起きていた。
#    C5 は `bt/*` を全て同一ツリーから採る。本移行でそれに揃えた。
#
#  ★**供給元の «混成» を構造的に禁止する**（HANDOFF §4-3-5／C6 evidence-c6-01 §4-7）：
#  hal 内・esp-idf 内はそれぞれ整合しているが，**混ぜると壊れる**
#  （実測＝`shared_periph_module_t`／`soc_root_clk_circuit_t` 未定義）。
#  ∴ `ASP3_BT_IDF_V554`（BTツリー）は **`ASP3_ESPIDF_SUPPLY`（基盤）に追従**させ，
#  食い違う指定は **FATAL_ERROR で即座に落とす**（＝「混ぜた」ことに起因する
#  難解なコンパイルエラーを，設定段階の明示的なエラーに置き換える）。
#
if(NOT EXISTS ${ASP3_BT_IDF_V554_DIR}/components/bt/controller/${BT_CHIP_SERIES}/bt.c)
    message(FATAL_ERROR
        "ASP3_BT_IDF_V554_DIR='${ASP3_BT_IDF_V554_DIR}' does not look like an esp-idf tree "
        "(bt/controller/${BT_CHIP_SERIES}/bt.c not found). Default is the repo submodule "
        "esp-idf/ (true v5.5.4 tag); init it with `git submodule update --init esp-idf`, "
        "or point -DASP3_BT_IDF_V554_DIR=<tree>.")
endif()
set(BT_IDF ${ASP3_BT_IDF_V554_DIR})

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
    #  ★MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL/MALLOC_CAP_RETENTION の -D は
    #  撤去した（.steering/20260716-c3c5c6-esp-idf-supply-migration の
    #  C3 toolchain 整合ラウンド）．経緯＝esp_wifi.cmake の同節と，
    #  下の set_source_files_properties を参照．
    #  ★撤去できた理由＝**旧コメントの事実認識が誤っていた**：
    #  「bt.c は esp_heap_caps.h を #include せず直値のビットマスクを期待する」
    #  は実測で **偽**．bt.c は hal 版・esp-idf 版とも :13 で
    #  **無条件に** esp_heap_caps.h を #include している（MALLOC_CAP_RETENTION
    #  を使う esp-idf 版 :1077 も同様）．bt_osi_mem.c・esp_mem.c も同じ．
    #  ⇒ MALLOC_CAP_* を真に欠いていたのは **phy_init.c だけ**であり，
    #  それは下の force-include で本物のヘッダから供給される．
    #  ⇒ -D は不要なだけでなく有害だった（"16384" vs 本物の "(1<<14)" で
    #  トークン列が違うため "MALLOC_CAP_RETENTION redefined" 警告を実際に出していた）．
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
#  ★ASP3_BT_IDF_V554：bt/include（esp_bt.h＝controller API．bt.cとconfig既定を
#  供給）・esp_phy/*（phy_init.c/bt.cが要するesp_phy_init.h・esp_private/phy.h）・
#  esp_wifi/include（phy_init.cのesp_private/wifi.h）・esp_coex/include
#  （bt.cのprivate/esp_coexist_internal.h＋共有esp_coex_adapter.cのadapter struct）
#  を ${BT_IDF} 経由でblobと同じ世代へ揃える．v5.5.4のesp_bt.hはesp_err/sdkconfig/
#  esp_task/esp_assertしか#includeせず，bt/common・bt/portingへは波及しない
#  （＝ホストが使うhal版bt/common・bt/portingヘッダと衝突しない．実測確認済）．
#  それ以外（esp_hw_support/esp_system/esp_rom/heap/log/riscv/gpio/clock/efuse/
#  esp_event）はチップHAL＝blob世代非依存のためhalのまま．
list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_COMMON_DIR}/bt/stub/include
    ${ESP_CHIP_DIR}/wifi
    ${BT_IDF}/components/bt/include/${BT_CHIP_SERIES}/include
    ${BT_IDF}/components/bt/common/include
    ${BT_IDF}/components/bt/common/ble_log/include
    ${BT_IDF}/components/bt/porting/include
    ${BT_IDF}/components/bt/porting/include/os
    ${ESP_SUP_DIR}/components/esp_hw_support/include
    ${ESP_SUP_DIR}/components/esp_hw_support/include/soc
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c3/include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c3/private_include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/include
    ${ESP_SUP_DIR}/components/esp_system/include
    ${BT_IDF}/components/esp_wifi/include
    ${BT_IDF}/components/esp_phy/include
    ${BT_IDF}/components/esp_phy/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_pm/include
    ${ESP_SUP_DIR}/components/esp_timer/include
    ${BT_IDF}/components/esp_coex/include
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}/include/${BT_CHIP_SERIES}
    ${ESP_SUP_DIR}/components/esp_rom/${BT_CHIP_SERIES}
    ${ESP_SUP_DIR}/components/heap/include
    ${ESP_SUP_DIR}/components/log/include
    ${ESP_SUP_DIR}/components/riscv/include
    ${ESP_SUP_HAL_gpio}/include
    ${ESP_SUP_HAL_gpio}/${BT_CHIP_SERIES}/include
    ${ESP_SUP_HAL_clock}/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/efuse/include
    ${ESP_SUP_DIR}/components/efuse/${BT_CHIP_SERIES}/include
    ${ESP_SUP_DIR}/components/esp_event/include
)

#  v5.5.4のesp_wifi_types_generic.hはv6.1/halと異なり"esp_interface.h"を
#  直接includeする（phy_init.cがesp_private/wifi.h→esp_wifi_types_generic.h
#  経由でこのヘッダ連鎖を辿る）．v5.5.4のesp_interface.h実体は
#  esp_hw_support/includeにある（halには存在しないファイル＝hal側の
#  esp_hw_support/includeが先にあっても衝突せず素通り）．C5/C6の
#  ASP3_BT_IDF_V554と同一の壁・同一の解決（memory c5c6-bt-blob-v554-feasibility）．
list(APPEND ASP3_INCLUDE_DIRS
    ${BT_IDF}/components/esp_hw_support/include
)

#
#  ------------------------------------------------------------------
#  2. ソースファイル
#  ------------------------------------------------------------------
#
list(APPEND ASP3_CFG_FILES ${BT_TARGETDIR}/bt.cfg)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    #  ★ASP3_BT_IDF_V554：controller本体bt.c＝blob（libbtdm_app）と同世代．
    #  v5.5.4 bt.cはosiテーブルに_malloc_retention追加（OSI_VERSION …0B）＝
    #  v5.5.4 blobのABIに一致させる（halのbt.cのままは不可）．
    ${BT_IDF}/components/bt/controller/${BT_CHIP_SERIES}/bt.c
    ${BT_TARGETDIR}/bt_shim.c
    #  esp_wifi.cmakeと同じ理由でPHY/クロック/ペリフェラルの実ソースを
    #  採用する（BTもWi-Fiと同じ無線ハードウェアを使うため必要）．
    #  ★ASP3_BT_IDF_V554：phyソースは ${BT_IDF}＝libphy.a（v5.5.4）と同世代
    #  （matched set）．periph_ctrl等の下位はチップHAL＝halのまま．
    ${BT_IDF}/components/esp_phy/src/phy_init.c
    ${BT_IDF}/components/esp_phy/src/phy_common.c
    ${BT_IDF}/components/esp_phy/${BT_CHIP_SERIES}/phy_init_data.c
    ${BT_IDF}/components/esp_phy/src/lib_printf.c
    ${ESP_SUP_DIR}/components/esp_hw_support/periph_ctrl.c
    ${ESP_SUP_DIR}/components/esp_hw_support/esp_clk.c
    ${ESP_SUP_DIR}/components/esp_hw_support/port/${BT_CHIP_SERIES}/rtc_clk.c
    #  Wi-Fiと共有のcoexアダプタ（docs/wifi-shim.md．ダミーno-opテーブル
     #  登録＝ROM側coexist_funcs NULL回避）．BT単体でも要求される．
    ${ESP_COMMON_DIR}/wifi/esp_coex_adapter.c
)

#
#  ------------------------------------------------------------------
#  phy_init.c へ esp_heap_caps.h を force-include する（esp_wifi.cmake と同型）
#  ------------------------------------------------------------------
#  phy_init.c は heap_caps_malloc() を呼ぶが esp_heap_caps.h を #include
#  していない（hal 版 :470・esp-idf 版 :479．**両供給とも**．実測）．
#  詳細な経緯・設計判断は esp_wifi.cmake の同名の節を正本とする．
#  ここは BT 経路（${BT_IDF} 供給の phy_init.c）に同じ処置を当てる．
#
#  ★esp_heap_caps.h の解決はインクルードパス（${ESP_SUP_DIR}/components/
#  heap/include）に委ねる＝phy_init.c が自分で #include していたら
#  解決したのと同じヘッダになる．ASP3_ESPIDF_SUPPLY=OFF かつ
#  ASP3_BT_IDF_V554=ON のような混成でも安全：hal 版と esp-idf 版の
#  esp_heap_caps.h は heap_caps_malloc の署名も MALLOC_CAP_* の値も
#  一致することを実測で確認済み（void *heap_caps_malloc(size_t, uint32_t)．
#  DMA=(1<<3)・INTERNAL=(1<<11)・RETENTION=(1<<14)）．
#
set_source_files_properties(
    ${BT_IDF}/components/esp_phy/src/phy_init.c
    PROPERTIES COMPILE_OPTIONS "-include;esp_heap_caps.h"
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
#  ★ASP3_BT_IDF_V554：4つのblob（libbtdm_app/libphy/libbtbb/libcoexist）を
#  ${BT_IDF}から採る．hal/v5.5.4でサブパス構造は同一
#  （bt/controller/lib_esp32c3_family/esp32c3・esp_phy/lib/esp32c3・
#  esp_coex/lib/esp32c3）＝${BT_IDF}差替えのみで両対応．
list(APPEND ASP3_LINK_OPTIONS
    -L${BT_IDF}/components/bt/controller/lib_esp32c3_family/${BT_CHIP_SERIES}
    -L${BT_IDF}/components/esp_phy/lib/${BT_CHIP_SERIES}
    -L${BT_IDF}/components/esp_coex/lib/${BT_CHIP_SERIES}
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
set(BT_ROM_LD_DIR ${BT_IDF}/components/esp_rom/${BT_CHIP_SERIES}/ld)
set(ESP_BT_ROM_LD_FILES
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.api.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libgcc.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.newlib.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.libc-suboptimal_for_misaligned_mem.ld
    ${BT_ROM_LD_DIR}/${BT_CHIP_SERIES}.rom.version.ld
    ${BT_IDF}/components/riscv/ld/rom.api.ld
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
#  RAM 予算のため既定は OFF．NimBLE を要するアプリ（ble_host_smoke_c3）では
#  自動で ON にする（D-1 の bt_smoke_c3 を痩せたまま保つ）．
#
option(ESP32C3_BT_NIMBLE "Enable NimBLE host stack on top of BT controller (Phase D-2)" OFF)
if(ASP3_APPLNAME STREQUAL "ble_host_smoke_c3")
    set(ESP32C3_BT_NIMBLE ON)
endif()

if(ESP32C3_BT_NIMBLE)

    set(NIMBLE_ROOT ${BT_IDF}/components/bt/host/nimble/nimble/nimble)
    set(NIMBLE_PORTING ${BT_IDF}/components/bt/host/nimble/nimble/porting)
    set(BT_ROOT ${BT_IDF}/components/bt)
    set(TINYCRYPT_ROOT ${BT_IDF}/components/bt/host/nimble/nimble/ext/tinycrypt)

    #  ---- D-2d：SMP（ペアリング／ボンディング）有効化 ----
    #  S3 BT-5（.steering/20260710-ble-bt5-security-notify）を «正» として
    #  移植．ON時は NIMBLE_BLE_SM=SM_LEGACY||SM_SC を 1 にし（下の -D...=0 の
    #  蓋を外す），SC=ECDH P-256 の crypto を vendored tinycrypt で供給
    #  （mbedTLS は CONFIG_ESPRESSIF_BLE 非定義のため選択されず，WiFi系TLSと
    #  分離を維持）．bond store は ble_store_ram（IDF文脈で空）ではなく
    #  ble_store_config（PERSIST=0＝RAM保持）を使う（S3 §5.2 の真因対策）．
    #  OFF に戻せば D-2c までの sync/接続/GATTディスカバリ構成に完全復帰
    #  （可逆）．
    option(ESP32C3_BT_SM "Enable NimBLE SMP pairing/bonding (Phase D-2d, tinycrypt)" ON)

    #
    #  ★stale 接続の検出と解消（rc-c3 P1-3b）．既定 OFF．
    #
    #  実機で確定した障害 L1：リンクが静かに死んだとき supervision-timeout 由来の
    #  切断イベントが host に配送されず（DISC=0），host が接続を保持し続けるため
    #  **広告が再開されず «スマホから見えない»**（復帰は reset のみ）。
    #  本オプションは «接続保持中に GAP 事象が 45 秒動かない» を stale と判定して
    #  ble_gap_terminate、それでも畳めなければ ble_hs_sched_reset でホストを
    #  作り直し、広告を自動復帰させる（2サイクル連続で実機実証済み）。
    #
    #  ★これは «根治» ではなく緩和策（本丸＝なぜリンクが死ぬかは未解明）。
    #  証跡＝.steering/…/evidence-rc-c3-P1-wedge-mbuf-exhaustion.md（別ブランチ
    #  claude/c3-smp-death-plan）。
    #
    option(ESP32C3_BT_CONN_WD "Detect+terminate stale connection and restore advertising (rc-c3 P1-3b). Default OFF (mitigation, not a root fix)" OFF)

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
        #  （bt_smoke_c3=コントローラのみ のビルドはプールを増やさない）．
        TOPPERS_ESP32C3_BT_NIMBLE
    )
    #  ---- 段階1 A/B切り分け用：PVCY トグル（既定=ON＝bond有効・恒久ビルド） ----
    #  connect不可の切り分け（docs/c3-ble-connect-plan.md 段階1）専用．
    #  OFF にすると TOPPERS_C3_BT_PVCY_OFF を -D 定義し，bt_nimble_config.h が
    #  CONFIG_BT_NIMBLE_HS_PVCY=0 とする＝起動時 resolving-list HCIバースト
    #  （LE Set Address Resolution Enable/Clear/Add）が出なくなる代わりに，
    #  responder Identity 鍵配布がコンパイルアウトされ bond不可になる．
    #  よってこれは A/B 用の一時ビルド専用．**恒久ビルドの既定は ON のまま**．
    #    使い方（切り分け専用ビルド）：cmake ... -DESP32C3_BT_PVCY=OFF
    option(ESP32C3_BT_PVCY "NimBLE host privacy / resolving-list startup burst (bond needs ON; OFF is A/B diag only)" ON)
    if(NOT ESP32C3_BT_PVCY)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_C3_BT_PVCY_OFF)
    endif()

    #  ---- connect不可 恒久修正候補(i)（ドラフト・既定OFF） ----
    #  docs/c3-ble-connect-plan.md 段階1で「PVCY=0でconnect成功／PVCY=1で失敗」＝
    #  候補1が確定した場合の恒久修正候補(i)．app（ble_host_smoke_c3.c on_sync）で
    #  アドバタイズ開始前に ble_hs_pvcy_set_resolve_enabled(0) を呼び，起動時
    #  バーストが有効化したアドレス解決を「元に戻す」．PVCY=1 を保つため bond
    #  （Identity 鍵配布のコンパイル時ゲート）は維持される．本アプリは
    #  privacy=0（ble_hs_id_infer_auto(0,…)＝RPA非使用）のためアドレス解決OFFは
    #  機能に影響しない見込み（HW確認は phase-2）．**実機で候補1確定後に投入**．
    #    使い方：cmake ... -DESP32C3_BT_CONNECT_FIX_UNDO_RESOLV=ON
    option(ESP32C3_BT_CONNECT_FIX_UNDO_RESOLV "Undo startup addr-res-enable in on_sync (connect-fix draft i, default OFF)" OFF)
    if(ESP32C3_BT_CONNECT_FIX_UNDO_RESOLV)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_C3_BT_CONNECT_FIX_UNDO_RESOLV)
    endif()

    if(ESP32C3_BT_SM)
        #  D-2d：SMP 有効．app 側（ble_host_smoke_c3.c）の SM 設定・store 初期化を
        #  有効化する識別子．NIMBLE_BLE_SM は bt_nimble_config.h の
        #  CONFIG_BT_NIMBLE_SM_LEGACY/SC=1 から自動で 1 になる（蓋をしない）．
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_SM)
    endif()
    if(ESP32C3_BT_CONN_WD)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_CONN_WD)
    endif()

    #
    #  ★SM を 0 に蓋するのは「SM を使わないビルド」のときだけ。
    #
    #  【2026-07-21 修正】この `else()` は **`ESP32C3_BT_CONN_WD` の else に
    #  繋がっていた**（本来は `ESP32C3_BT_SM` の else であるべき）。
    #  `CONN_WD` は既定 OFF なので、**`ESP32C3_BT_SM=ON` にしても常に
    #  `MYNEWT_VAL_BLE_SM_LEGACY/SC=0` が定義され、`NIMBLE_BLE_SM` が 0 に
    #  落ちて SM 一式がコンパイルアウトされていた**。
    #  症状＝`ble_gap_security_initiate()` が `#if NIMBLE_BLE_SM` に阻まれ
    #  **`rc=8`(`BLE_HS_ENOTSUP`)** を返し、Security Request が空中に出ない
    #  ⇒ central から見ると「接続はできるがペアリング要求が来ない・bond できない」。
    #  実機実測＝btsnoop で **SMP PDU 0件**、DUT ログで
    #  `BT5 security_initiate(slave SecReq) rc=8`、`nm` で `ble_sm_slave_initiate`
    #  等が **未リンク**（証跡＝evidence-05-c3-smp-notsup.md）。
    #  すぐ上のコメント「SM=ON なら蓋をしない」が実装と食い違っていた。
    #
    if(NOT ESP32C3_BT_SM)
        #  D-2c まで：SECURITY off．NIMBLE_BLE_SM=SM_LEGACY||SM_SC を 0 に落とし
        #  （nimble_opt_auto.h），ble_sm*.c を near-empty 化して tinycrypt/mbedTLS
        #  リンクを回避する．
        list(APPEND ASP3_COMPILE_DEFS
            MYNEWT_VAL_BLE_SM_LEGACY=0
            MYNEWT_VAL_BLE_SM_SC=0
        )
    endif()
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
    if(ESP32C3_BT_SM)
        #  D-2d：tinycrypt（SC の uECC P-256 ＋ AES-CMAC）ヘッダ
        list(APPEND ASP3_INCLUDE_DIRS ${TINYCRYPT_ROOT}/include)
    endif()

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
    )
    if(ESP32C3_BT_SM)
        #  D-2d：bond store は ble_store_config（PERSIST=0＝RAM，NVS不使用）．
        #  ble_store_ram.c は IDF文脈（BLE_USED_IN_IDF=1）で空になり使えない
        #  （S3 §5.2 の真因）．＋ tinycrypt 必要5ソース（ble_sm_alg.c の
        #  参照シンボル：tc_aes128_set_encrypt_key/tc_aes_encrypt/tc_cmac_*/
        #  uECC_make_key/uECC_valid_public_key/uECC_shared_secret に対応．
        #  ecc_platform_specific.c は /dev/urandom 用で不要＝RNG は
        #  ble_sm_alg_ecc_init→ble_hs_hci_util_rand 経由で供給）．
        #
        #  ★bond ストアの不揮発化（残課題C．可逆オプション・既定OFF）
        #    ON にすると `ble_store_config`（PERSIST=0＝RAM保持．電源断で鍵が
        #    消える）の代わりに、フラッシュ末尾の予約領域へ直接保存する
        #    自前バックエンド（esp/common/bt/ble_store_flash.c）を使う。
        #    NVS を使わない理由＝本リポジトリは Direct Boot でパーティション
        #    テーブルが存在せず、`nvs_flash` は esp_partition 前提かつ C++＋
        #    ヒープ多用のため（設計の経緯＝evidence-04-bond-nvs.md）。
        #    アプリ側は ble_store_config_init() の代わりに
        #    asp3_ble_store_flash_init() を呼ぶ（TOPPERS_BLE_STORE_FLASH で分岐）。
        #
        option(ESP32C3_BLE_STORE_FLASH
               "Persist BLE bonds to flash instead of RAM-only ble_store_config" OFF)
        if(ESP32C3_BLE_STORE_FLASH)
            list(APPEND ASP3_COMPILE_DEFS TOPPERS_BLE_STORE_FLASH)
            list(APPEND ASP3_SYSSVC_TARGET_C_FILES
                ${CMAKE_CURRENT_LIST_DIR}/../common/bt/ble_store_flash.c)
        endif()

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

    #
    #  （D-2d bond診断）HCI EVT 経路計装（既定OFF＝非回帰）．ON時のみ
    #  evt_trace.c を追加し host 側 ble_hs_hci_evt_process を --wrap して，
    #  controller が «LE LTK Request»(0x3E/0x05) と «Encryption Change»(0x08)
    #  を生成しているかを RTC STORE0(0x50) へ記録する．暗号有効化タイムアウト
    #  (ENC_CHANGE=13=ETIMEOUT) の真因を LL/コントローラ層 vs shim/host 層で
    #  確定する決定的計装．詳細=docs/bt-shim.md「D-2d bond診断」．
    #
    #
    #  （D-2d bond診断）esp_shim のサービスコールで «想定外» のエラー
    #  （非E_OK かつ 非E_CTX/E_TMOUT/E_QOVR）を SVC_PERROR で file:line 付き
    #  ログする（sample1 の SVC_PERROR 相当）．暗号後の鍵配布で失敗する
    #  サービスコールを特定する．既定OFF＝非回帰．
    #
    option(ESP32C3_BT_APIERR_TRACE "Log unexpected esp_shim svc-call errors (SVC_PERROR, D-2d bond diag)" OFF)
    if(ESP32C3_BT_APIERR_TRACE)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_APIERR_TRACE)
    endif()

    option(ESP32C3_BT_EVT_TRACE "Trace HCI EVT (LTK Req/Enc Change) via --wrap (D-2d bond diag)" OFF)
    if(ESP32C3_BT_EVT_TRACE)
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${BT_TARGETDIR}/evt_trace.c)
        #
        #  ★TOPPERS_C3_EVT_FAST_MAP が要る（2026-07-21 修正）。
        #    `evt_trace.c` の TX 側 wrap 実体 `__wrap_ble_transport_to_ll_acl_impl`
        #    （およびイベント配送カウンタの一部）は `#ifdef TOPPERS_C3_EVT_FAST_MAP`
        #    の中にあるが、本 cmake はこのマクロを定義せずに `-Wl,--wrap=` だけを
        #    張っていた ⇒ **リンク不能**
        #      undefined reference to `__wrap_ble_transport_to_ll_acl_impl'
        #    ＝このオプションは ON にすると必ずビルドが落ちる状態だった
        #    （既定 OFF のため気づかれていなかった）。
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_C3_EVT_FAST_MAP)
        list(APPEND ASP3_LINK_OPTIONS
            -Wl,--wrap=ble_hs_hci_evt_process
            -Wl,--wrap=ble_mqueue_put
            -Wl,--wrap=ble_mqueue_get
            -Wl,--wrap=ble_l2cap_rx
            #  ★TX側（evidence-c3-05）＝「沈黙」か「誤答」かの判別。
            #  ble_transport_to_ll_acl は **両ツリーで同一シグネチャ**
            #  （ble_hs.c:880 の ble_hs_tx_data からクロスTU 呼出し）。
            -Wl,--wrap=ble_transport_to_ll_acl_impl
        )
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_EVT_TRACE)
    endif()

    #
    #  ---- connect不可 恒久修正候補(ii)（ドラフト・既定OFF） ----
    #  docs/c3-ble-connect-plan.md 段階1で候補1が確定した場合の恒久修正候補(ii)．
    #  shim/transport 層で出ていくHCIコマンドを --wrap で捕まえ，
    #  «LE Set Address Resolution Enable»（OGF=0x08/OCF=0x2D＝opcode 0x202D）の
    #  enable バイトを 0 に潰して __real_ へ渡す（Command Complete は偽造せず，
    #  コントローラに有効なコマンドを送り正常応答させる＝候補(i)と違い解決を
    #  «一度も» 有効化しない）．clear/add は応答処理を要するため触らない．
    #  段階1で「どのコマンドで詰まるか」実機判明後に対象opcodeを hci_pvcy_filter.c
    #  へ追加する．**実機で候補1確定後に投入**．
    #  ★wrap 対象は esp_vhci_host_send_packet（esp_nimble_hci.c から名前付き直 jal
    #    で呼ばれ確実に on-path．ble_hci_trans_hs_cmd_tx を wrap すると同モジュール
    #    内 tail-jump 経路のため inert＝objdump で確認済み）．acl_trace も同関数 wrap 実績．
    #    使い方：cmake ... -DESP32C3_BT_PVCY_FILTER=ON
    #
    option(ESP32C3_BT_PVCY_FILTER "Neutralize LE Set Address Resolution Enable in HCI TX (connect-fix draft ii, default OFF)" OFF)
    if(ESP32C3_BT_PVCY_FILTER)
        list(APPEND ASP3_SYSSVC_TARGET_C_FILES ${BT_TARGETDIR}/hci_pvcy_filter.c)
        list(APPEND ASP3_LINK_OPTIONS -Wl,--wrap=esp_vhci_host_send_packet)
        list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C3_BT_PVCY_FILTER)
    endif()

    #  ★Low#5：これらの診断 --wrap は同一シンボルを定義＝同時 ON で multiple definition
    #  （ACL_TRACE∩EVT_TRACE=__wrap_ble_mqueue_put/get・__wrap_ble_l2cap_rx／
    #   ACL_TRACE∩PVCY_FILTER=__wrap_esp_vhci_host_send_packet）．早期に FATAL で弾く．
    if(ESP32C3_BT_ACL_TRACE AND ESP32C3_BT_EVT_TRACE)
        message(FATAL_ERROR "ESP32C3_BT_ACL_TRACE と ESP32C3_BT_EVT_TRACE は同一 --wrap シンボル(__wrap_ble_mqueue_put/get・__wrap_ble_l2cap_rx)を定義＝相互排他．片方だけ ON にすること．")
    endif()
    if(ESP32C3_BT_ACL_TRACE AND ESP32C3_BT_PVCY_FILTER)
        message(FATAL_ERROR "ESP32C3_BT_ACL_TRACE と ESP32C3_BT_PVCY_FILTER は __wrap_esp_vhci_host_send_packet を両方定義＝相互排他．片方だけ ON にすること．")
    endif()

endif()

endif()
