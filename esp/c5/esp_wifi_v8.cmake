#
#               ESP32-C5 Wi-Fi統合（esp-hal-3rdparty prebuilt libs）
#
#  esp32c6版（asp3/target/esp32c6_espidf/esp_wifi.cmake）からのコピー・
#  C5対応（docs/c5-port-design.md §5.4・§6・§9）。C6版はNuttXの
#  arch/risc-v/src/common/espressif/Wireless.mk（および同ディレクトリの
#  Wireless.cmake）からの移植．参照コミット：
#    apache/nuttx  master（C6版作成時点のワークツリー，Wireless.mk/
#                  Wireless.cmakeとも同一内容．コミットSHA固定なしのmasterチェックアウト）
#    esp-hal-3rdparty  b90b1837cb5ad24747deb4c895246037cc206ce5（本リポジトリの
#                  asp3/hal submodule＝espressif/esp-hal-3rdparty）
#
#  ASP3のCMake変数コントラクト（積み上げ式）に載せ替えただけで，ソース
#  リスト・コンパイル定義・リンクの中身はWireless.mk/cmakeを忠実に踏襲する：
#    NuttXの CHIP_CSRCS/VPATH        → ASP3_SYSSVC_TARGET_C_FILES（絶対パス化）
#    NuttXの INCLUDES                → ASP3_INCLUDE_DIRS
#    NuttXの CFLAGS -D…              → ASP3_COMPILE_DEFS
#    NuttXの EXTRA_LIBPATHS/EXTRA_LIBS → ASP3_LINK_OPTIONS(-L) / ASP3_LINK_LIBS
#
#  意図的な差分（NuttXとの相違点．C6版から踏襲）：
#    1. BLE関連（tinycrypt/nimble/bt/…）は移植しない．ESP32-C5のBLEコント
#       ローラ移植はPhase Bの対象外（本ファイルはCONFIG_ESPRESSIF_WIFI相当
#       ＝Wireless.mk/cmakeの97行目以降＝WiFiパスのみ）．
#    2. CONFIG_WPA_WAPI_PSK（WAPI）はOFF固定．libwapi.aは積まない．
#    3. CONFIG_ESPRESSIF_WIFI_* のSAE/OWE/GCMP/GMAC等のオプション拡張define
#       はNuttX Kconfigのデフォルト（未設定＝OFF）に倣い，本ファイルでは
#       付与しない（必要になった時点でオプション化する）．
#    4. wpa_supplicant/port/os_xtensa.c はファイル名にXtensaを含むが中身は
#       ISA非依存のOS抽象化層（eloop用のtime/mem wrapper）であり，NuttXも
#       RISC-V(ESP32-C5等)でそのまま使っている．asp3側でも同名のまま採用。
#       ただし実体（loc_cpu/get_tim等ASP3 API呼び出し）はPhase Bのos_adapter
#       shimで別途実装が必要（本ファイルはソースを積むのみ）．
#    5. ROM関数ld（esp32c5.rom*.ld）はWireless.mk/cmake自体には現れない
#       （NuttXではhal_esp32c5.cmakeのチップ共通部で全ビルドに対し常時リンク
#       される）．asp3の既存esp32c5.ld/chip.cmakeはROM ldを一切リンクして
#       いない（libc_stub.cで自前providesのため今まで不要だった）ため，
#       prebuilt wifi/phy/coexistライブラリがROM関数を要求する分だけ本
#       ファイルで追加する．*_bt_funcs.ld（BLE専用）は上記差分1の理由で
#       対象外．
#
#       【C6からの差分】C6にはeco*.ldが存在せずBLE専用ld一式と同列に
#       除外していたが，**C5にはesp32c5.rom.eco3.ldが実在し，しかも
#       phy_get_max_pwr（PHY最大送信電力，BLEとは無関係のWiFi/PHY共通
#       ROM関数）を提供する**（docs/c5-port-design.md §3・§9．
#       hal/components/esp_rom/esp32c5/ld/esp32c5.rom.eco3.ld参照）。
#       C5ではeco3.ldを除外せず明示的にリンクし，esp_shim_blobglue.cの
#       固定値スタブ（C6版が持っていた20dBmプレースホルダ）を廃した
#       （下記ROM ld一覧・esp_shim_blobglue.c参照）。"eco3"という
#       シリコンリビジョン固有の命名が全revで有効かは実機確認待ち
#       （docs/c5-port-design.md §8.1 9番）。
#
#    6. C6版の診断用リンクオプション（`-Wl,--wrap=...`によるRXパス
#       トレース一式・ASP3_WIFI_BLOB_IDFによるblobバージョン差し替え
#       機構）はC6のdeaf-RX調査専用の一時計装のため移植しない
#       （docs/c5-port-design.md §6.1・§8.3「C6資産の非移植方針」）。
#
#  検証：末尾のシェルループでリストした全ファイルの実在を確認済み
#  （mbedtls submoduleは取得済みのためスキップなし）。ビルド検証結果
#  （エラー0達成の有無・メモリ使用率）は本ファイル末尾に追記する。
#

option(ESP32C5_WIFI "Integrate esp-hal-3rdparty prebuilt Wi-Fi libs (wpa_supplicant/esp_wifi/mbedtls; Phase B, compiles clean)" OFF)

if(ESP32C5_WIFI)

#
#  ESP_HAL_DIR はtarget.cmakeで定義済み（asp3/hal ＝ esp-hal-3rdparty submodule）
#

set(WIFI_CHIP_SERIES esp32c5)

#
#  ------------------------------------------------------------------
#  0. ツールチェーン厳格化への対応（GCC 14+）
#  ------------------------------------------------------------------
#
#  本設計書§10のビルド検証はriscv64-unknown-elf-gcc 13.2で行われたが，
#  GCC 14以降はC99で非推奨だった以下の診断を既定で「警告」から「エラー」へ
#  昇格した（GCC 14リリースノート "stricter C"）。esp-hal-3rdparty
#  （asp3/hal submodule＝編集禁止）のprebuilt Wi-Fi glue（mbedtls/bignum.cの
#  fgets/setbuf，esp_phy/phy_init.cの_lock_acquire/_lock_release等）は
#  レガシーCのため，新しいツールチェーンではこれらでコンパイルが停止する。
#  hal配下は編集できずヘッダ追加もできないため，該当診断の重大度を
#  gcc 13.2当時（＝本移植が検証された基準）と同じ「警告」へ戻す。
#  これらはコードが実際に該当パターンを持つ場合のみ発火する診断であり，
#  ASP3カーネル本体（クリーン）には無影響。適用は本if(ESP32C5_WIFI)
#  ブロック内に閉じるためB-0/B-1（Wi-Fi無効ビルド）には及ばない。
#  （利用可能ツールチェーンはxpack 15.2／esp-idf同梱14.2・15.2のみで
#   gcc 13系が無いための対処。TODO：将来gcc 13系を用意できるなら不要）
#
list(APPEND ASP3_COMPILE_OPTIONS
    -Wno-error=implicit-function-declaration
    -Wno-error=implicit-int
    -Wno-error=int-conversion
    -Wno-error=incompatible-pointer-types
)

#
#  ------------------------------------------------------------------
#  1. インクルードパス（Wireless.mk 23-28行目．BT/BLE系(23,35-46行目)は
#     差分1により除外．esp_wifi/wifi_apps/roaming_appはWiFi共通のため採用）
#  ------------------------------------------------------------------
#
list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_SUP_DIR}/components/esp_coex/include
    ${ESP_SUP_DIR}/components/esp_wifi/wifi_apps/roaming_app/include
)

#
#  ------------------------------------------------------------------
#  1b. コンパイルを通すために追加したインクルードパス
#  ------------------------------------------------------------------
#
#  Wireless.mk/cmake（NuttXビルド）はesp-hal-3rdpartyの他コンポーネント
#  （esp_hw_support/esp_wifi/heap/efuse/esp_event/log/...）をNuttXの
#  ビルドシステム側で暗黙にインクルードパスへ通しており，Wireless.mk
#  自体には現れない．ASP3側はコンポーネント単位のインクルードパスを
#  明示する必要があるため，実際のコンパイルエラー（fatal error:
#  ヘッダ未検出）を1件ずつ辿って特定した，すべてesp-hal-3rdparty
#  （asp3/hal submodule）内に実在するヘッダである．
#
list(APPEND ASP3_INCLUDE_DIRS
    #  esp_cpu.h（esp_system/include/esp_private/startup_internal.h経由）
    ${ESP_SUP_DIR}/components/esp_hw_support/include
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/include
    #  esp_hw_log.h（rtc_clk.c経由．C6固有のrtc_clk.c採用に伴う追加）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/include
    #  esp_private/esp_wifi_private.h・esp_wifi_types_generic.h等
    ${ESP_SUP_DIR}/components/esp_wifi/include
    #  esp_heap_caps.h（mbedtls/port/esp_mem.c等）
    ${ESP_SUP_DIR}/components/heap/include
    #  esp_efuse.h（mbedtls/port/psa_driver/esp_mac/*.c）
    ${ESP_SUP_DIR}/components/efuse/include
    ${ESP_SUP_DIR}/components/efuse/esp32c5/include
    #  esp_event.h（esp_wifi/src/wifi_init.c）
    ${ESP_SUP_DIR}/components/esp_event/include
    #  esp_log.h（efuse/mbedtls/wpa_supplicantの多くが依存）
    ${ESP_SUP_DIR}/components/log/include
    #  riscv/csr.h（esp_cpu.hの内部依存）
    ${ESP_SUP_DIR}/components/riscv/include
    #  hal/hmac_types.h（mbedtls psa_driver/esp_mac）
    ${ESP_SUP_HAL_security}/include
    #  esp_hmac.h（mbedtls psa_driver/esp_mac）
    ${ESP_SUP_DIR}/components/esp_security/include
    #  hal/adc_types.h（esp_hw_support/include/esp_private/adc_share_hw_ctrl.h経由）
    ${ESP_SUP_HAL_ana_conv}/include
    #  hal/gpio_types.h・soc/gpio_num.h（esp_hw_support/esp_sleep.h経由）
    ${ESP_SUP_HAL_gpio}/include
    ${ESP_SUP_HAL_gpio}/esp32c5/include
    #  esp32c5/rom/ets_sys.h（wpa_supplicant/port/eloop.c）
    ${ESP_SUP_DIR}/components/esp_rom/esp32c5/include/esp32c5
    #  esp_pm.h（esp_wifi/src/wifi_init.c）
    ${ESP_SUP_DIR}/components/esp_pm/include
    #  esp_phy_init.h（esp_wifi/src/wifi_init.c）
    ${ESP_SUP_DIR}/components/esp_phy/include
    #  hal/clk_gate_ll.h（esp_hw_support/periph_ctrl.c．§6参照）
    ${ESP_SUP_HAL_clock}/esp32c5/include
    ${ESP_SUP_HAL_clock}/include
    #  hal/pau_types.h（esp_hw_support/include/esp_private/esp_regdma.h経由）・
    #  hal/pmu_hal.h（esp_hw_support/include/esp_private/esp_pmu.h経由）．
    #  C6以降の新コンポーネント＝C3のesp_wifi.cmakeには無い追加分
    ${ESP_SUP_HAL_pmu}/include
    ${ESP_SUP_HAL_pmu}/esp32c5/include
    #  pmu_param.h（esp_pmu.h経由．PMU＝C6以降の新サブシステム）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/private_include
    #  C5固有の追加インクルードパス（§6の追加ソース，C6の一覧には
    #  無い依存）：
    #  esp_rom_caps.h（esp_hw_support/esp_clk.c経由．C5固有のROM機能
    #  ケーパビリティ定義．C6も同名ファイルを持つがC6のビルドでは
    #  esp_clk.cを採用していないため今まで顕在化しなかった）
    ${ESP_SUP_DIR}/components/esp_rom/esp32c5
    #  hal/rtc_timer_hal.h（esp_hw_support/port/esp32c5/rtc_time.c経由）
    ${ESP_SUP_HAL_rtc_timer}/include
    ${ESP_SUP_HAL_rtc_timer}/esp32c5/include
    #  hal/timg_ll.h（同じくrtc_time.c経由．MWDTのLLレイヤ）
    ${ESP_SUP_HAL_timg}/esp32c5/include
    ${ESP_SUP_HAL_timg}/include
)

#
#  ------------------------------------------------------------------
#  1c. esp_phy 本実装（register_chipv7_phyによるRF較正．実機PHY
#      初期化クラッシュ修正）
#  ------------------------------------------------------------------
#
#  従来（esp_shim_blobglue.cの簡易esp_phy_enable）はクロックゲート
#  のみでlibphy.a内部の関数テーブル初期化（register_chipv7_phy）を
#  一切呼んでいなかったため，esp_wifi_scan_start()等がPHYの関数
#  テーブル（set_chanfreq等）をNULL callして実機でクラッシュしていた．
#  本節でesp_phy/src/phy_init.c（esp_phy_enable/disable本体）・
#  phy_common.c（phy_set_modem_flag等の補助関数）・esp32c5向け
#  phy_init_data.c（PHY初期化パラメータの既定バイナリ．
#  CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION未定義＝アプリバイナリ
#  内蔵版）を採用し，esp_shim_blobglue.cの簡易実装は削除する．
#
#  較正モード：sdkconfig.h（hal/nuttx/esp32c5/include/sdkconfig.h．
#  NuttXビルド由来だが本統合でも§7冒頭コメントの理由でそのまま採用）
#  は CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE を定義しないため，
#  phy_init.cは`#else`分岐＝
#    register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL)
#  を無条件に呼ぶ（NVS読み書き一切なし＝「毎回フル較正・永続化なし」．
#  タスク指示どおり）．esp_phy_load_cal_data_from_nvs等（NVS版）の
#  関数自体は`#ifndef __NuttX__`ガードのみでコンパイルはされるが，
#  上記により実行経路には現れない（呼び出し元が無いため
#  -ffunction-sections+--gc-sectionsでリンクからも脱落する想定）．
#  nvs.h/nvs_flash.hはhal_stub/include（NVS未実装スタブ．
#  esp_shim_blobglue.c参照）で解決する．
#
#  CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1：実機はPLL温度ドリフト追従
#  （phy_track_pll系．esp_timer周期タイマ機構が必要）を本フェーズの
#  スコープ外として無効化する．ESP-IDF本来のKconfigオプション
#  （証明書試験モード等で実際に使われる正規の分岐）を流用した
#  意図的な妥協点＝esp_timer周期タイマ実装（Phase B-2 shim未対象）
#  を要さずに済ませるため．PLL自動追従なしでも起動時の
#  register_chipv7_phy本較正自体は行われるためscan自体は動作する
#  想定．後続課題（docs/wifi-shim.md参照）。
#
list(APPEND ASP3_COMPILE_DEFS
    CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1
    #  MALLOC_CAP_DMA/MALLOC_CAP_INTERNAL：esp_phy/src/phy_init.cの
    #  esp_phy_modem_init()がheap_caps_malloc()へ渡すビットマスク引数．
    #  本来はesp_heap_caps.h（heap/include．インクルードパスは既に
    #  §1bで追加済み）が定義するが，phy_init.c自身はこのヘッダを
    #  #includeしていない（実物のESP-IDFビルドではheapコンポーネント
    #  経由で間接的に見えている）．hal/配下は編集禁止のため，
    #  esp_heap_caps.h（hal/components/heap/include）と同じ値を
    #  コンパイル定義として直接与える．esp_shim_libc.cの
    #  heap_caps_malloc()実装自体はcaps引数を無視する（DMA/internal
    #  の区別が不要なESP32-C5向け設計．docs/wifi-shim.md参照）ため，
    #  値そのものは意味を持たずシンボル解決のためだけに必要．
    #  （8=1<<3・2048=1<<11．CMakeのlist APPENDが"<<"を含む文字列を
    #  正しく1要素として扱えないため数値リテラルで与える）
    MALLOC_CAP_DMA=8
    MALLOC_CAP_INTERNAL=2048
    #  TCNT_SYSLOG_BUFFER：既定32件（syssvc/syslog.c．kernel/同様
    #  編集禁止のため上書きはコンパイル定義で行う）はesp_wifi_init()の
    #  起動時ログバースト（"config nano"・"Init dynamic rx buffer num"
    #  等，blob/wifi_init.c由来の診断ログが短時間に多数出る）で溢れ，
    #  診断に必要なメッセージ（phy_version等）が失われるため増量する．
    TCNT_SYSLOG_BUFFER=128
    #  CONFIG_IDF_TARGET_ESP32C5：hal/components/esp_wifi/include/
    #  esp_private/wifi_os_adapter.hのwifi_osi_funcs_t定義が
    #  `#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C5 ||
    #  CONFIG_IDF_TARGET_ESP32C61`で_regdma_link_set_write_wait_content／
    #  _sleep_retention_find_link_by_idの2フィールドを条件付き追加する
    #  （C6/C5/C61専用のsleep retention連携）．
    #  【C6からの差分・重要】C6版はここに「このマクロは実は
    #  hal/nuttx/esp32c6/include/sdkconfig.h経由で既に定義済みであり
    #  本行は冗長」という訂正コメントを持っていたが，**C5はこの前提が
    #  成立しない**：hal/nuttx/esp32c5/（NuttXポート由来のsdkconfig.h一式）
    #  自体が存在しない（`find hal/nuttx -iname sdkconfig.h`でesp32c6版
    #  のみ実在を確認．C5にはNuttXポートの当該ディレクトリ自体が無い）。
    #  したがってC5では本コンパイル定義が唯一のCONFIG_IDF_TARGET_ESP32C5
    #  供給源であり，**冗長ではなく必須**（欠落するとwifi_osi_funcs_tの
    #  構造体ABIがblob側の想定と食い違う恐れがある）。
    CONFIG_IDF_TARGET_ESP32C5=1
)

list(APPEND ASP3_INCLUDE_DIRS
    #  phy_init_data.h／phy_init_deps.h（esp32c5向けデフォルトPHY
    #  初期化データ配列＋PHY_INIT_MODEM_CLOCK_REQUIRED_BITS）
    ${ESP_SUP_DIR}/components/esp_phy/esp32c5/include
)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_SUP_DIR}/components/esp_phy/src/phy_init.c
    ${ESP_SUP_DIR}/components/esp_phy/src/phy_common.c
    ${ESP_SUP_DIR}/components/esp_phy/esp32c5/phy_init_data.c
)

#
#  ------------------------------------------------------------------
#  2. リンクライブラリパス・ライブラリ（Wireless.mk 80-88行目）
#  ------------------------------------------------------------------
#
#  C6版はここに「RX-enable --wrapトレース」（libpp.a／libnet80211.a内部
#  シンボルをラップしwifi_trace.cのリングバッファへ記録する）診断専用の
#  -Wl,--wrap=...一式を持っていたが，C6のdeaf-RX調査専用の一時計装の
#  ため，C5では移植しない（docs/c5-port-design.md §6.1・§8.3
#  「C6資産の非移植方針」）。
#
#  ★v5.5.4統一（docs/blob-unify-v554.md）：WiFi/PHY/coexist blobを
#  hal（esp-hal-3rdparty submodule，NuttX同期のv8）から実ESP-IDF
#  v5.5.4（esp-idf submodule＝735507283d，同じos_adapter v8）へ切替える。
#
#  ★★訂正（2026-07-16・実測）：旧版はここで idf_v554_override/ の
#  wifi_os_adapter.h を PREPEND して hal のヘッダをシャドウしていたが，
#  **その前提は誤り**であり esp_wifi_init が 0x102 を返す原因そのもの
#  だった。撤去した。根拠（すべて実測）：
#
#   (1) blobは自身がビルドされたヘッダのmd5先頭7桁を `g_wifi_osi_funcs_md5`
#       として埋め込む（hal/components/esp_wifi/test_md5/test_md5.sh）。
#       実測＝hal blob: 6eaa5ad ／ v5.5.4タグ blob: 6eaa5ad ／
#       `~/tools/esp-idf`(+1169) blob: 8651e5d。
#       ヘッダ側md5＝hal: 6eaa5ad ／ v5.5.4タグ: 6eaa5ad ／ +1169: 8651e5d。
#       ⇒ **halのwifi_os_adapter.hはv5.5.4タグと«バイト同一»**（旧コメントの
#       「halは古い版でフィールドを欠く」は+1169との比較に基づく誤り）。
#   (2) 旧override は `~/tools/esp-idf`＝**v5.5.4-1169-gbb2188bf**（release/v5.5
#       の先端．version.hが5.5.4と表示するため誤認）からのコピーで，
#       +1169でのみ追加された `_wifi_disable_ac_ax`（#if CONFIG_SOC_WIFI_HE_SUPPORT，
#       `_magic`の直前）を含む＝`_magic`が4バイト後ろにずれる。
#   (3) blob側の検査は md5 ではなく**オフセット直読み**（実測：
#       libnet80211.a(ieee80211_api.o) `wifi_osi_funcs_register`）：
#         _version は offset 0 で ==8 を要求，`_magic` は
#         **v5.5.4タグ/hal blob: offset 484** ／ **+1169 blob: offset 488**
#         を直読みし 0xdeadbeaf でなければ `return 258`(=0x102 INVALID_ARG)。
#         `esp_wifi_init` はこれをそのまま返す。
#   (4) override 有効時の実測＝`nm -S g_wifi_osi_funcs`＝size 0x1ec(492)，
#       0xdeadbeaf は offset **488** ⇒ タグblobが読む484とずれ 0x102。
#       override 撤去後は size 488／0xdeadbeaf offset **484** でタグblobと一致。
#
#  ⇒ hal・v5.5.4タグとも同一ヘッダ内容＝**overrideは不要**。
#  reversible: ASP3_WIFI_BLOB_HAL=ON でhal blobへ戻せる（ヘッダ内容が
#  同一なのでosi ABIは不変＝484のまま）。
#  なお `-DIDF_V554=~/tools/esp-idf`（+1169）へ差し戻す場合のみ osi ABI が
#  488 になるため，その時は ASP3_WIFI_OSI_HAS_DISABLE_AC_AX=1 が必要
#  （下のoption）。既定OFF＝v5.5.4タグ／hal の 484。
#
option(ASP3_WIFI_BLOB_HAL "Use hal(v8) WiFi/PHY/coexist blob instead of ESP-IDF v5.5.4(v8) unification (reversible fallback)" OFF)
#  osi ABI差：release/v5.5先端(+1169)のwifi_os_adapter.hのみ`_wifi_disable_ac_ax`
#  を持ち`_magic`が484→488へずれる。v5.5.4タグ／halは持たない（既定OFF）。
option(ASP3_WIFI_OSI_HAS_DISABLE_AC_AX "wifi_osi_funcs_t has _wifi_disable_ac_ax (ONLY release/v5.5 HEAD +1169 osi ABI; _magic moves 484->488). Default OFF = v5.5.4 tag / hal ABI" OFF)
if(NOT DEFINED IDF_V554)
    #  ★esp-idf submodule（v5.5.4タグ＝735507283d）をリポジトリ同梱で参照する
    #  （HAL依存撤去ミッション＝.steering/20260716-c3c5c6-esp-idf-supply-migration）。
    #  従来は外部絶対パス /home/honda/tools/esp-idf を -L していたが，これは
    #  (1) このマシン固有＝再現性が無く，(2) 実測すると v5.5.4-1169-gbb2188bf
    #  （release/v5.5 の先端）で **v5.5.4タグではなかった**（version.hが5.5.4と
    #  表示するため気づきにくい）。C5 WiFi blobは両者で全て相違する
    #  （libnet80211 3996ba79 vs c7b13c02 等．docs/blob-unify-v554.md §14）。
    #  外部ターゲット規約（PORTING_GUIDE.md §6）に従い CMAKE_CURRENT_LIST_DIR 相対。
    #  A/B用に -DIDF_V554=<path> で従来の外部treeへ差し戻せる（可逆）。
    get_filename_component(IDF_V554 ${CMAKE_CURRENT_LIST_DIR}/../../esp-idf ABSOLUTE)
endif()
if(ASP3_WIFI_BLOB_HAL)
    asp3_require_removed_submodule(${ESP_HAL_DIR} ASP3_WIFI_BLOB_HAL "esp-hal-3rdparty (./hal)")
    set(ASP3_WIFI_BLOB_SRC ${ESP_HAL_DIR})
else()
    set(ASP3_WIFI_BLOB_SRC ${IDF_V554})
    list(APPEND ASP3_COMPILE_DEFS ASP3_WIFI_BLOB_V554=1)
    #  ★wifi_os_adapter.hのシャドウは行わない（上の§訂正を参照）。
    #  v5.5.4タグblobが読む_magicオフセット484は，halのヘッダ
    #  （＝v5.5.4タグとバイト同一，md5 6eaa5ad）そのままで一致する。
endif()
if(ASP3_WIFI_OSI_HAS_DISABLE_AC_AX)
    list(APPEND ASP3_COMPILE_DEFS ASP3_WIFI_OSI_HAS_DISABLE_AC_AX=1)
endif()

#
#  ★HAL依存撤去：Wi-Fi経路をesp-idf供給にするために追加で要る配線
#  （.steering/20260716-c3c5c6-esp-idf-supply-migration）。
#
#  動機＝ABI整合：blobは自身がビルドされたヘッダのmd5を埋込む
#  （`g_esp_wifi_md5`等）。実測でhal版ヘッダはv5.5.4タグblobと«不一致»：
#     esp_wifi.h              hal=9f7e672 / v5.5.4タグblobの要求=a78adff
#     esp_wifi_types_generic.h hal=6773bf5 / 要求=dae1625
#     esp_wifi_driver.h        hal=50fc486 / 要求=2331a76
#  （wifi_os_adapter.h と esp_wifi_types_native.h のみ一致）。
#  ＝hal構成はhalヘッダ＋v5.5.4タグblobの混成でありscan/W1は通るものの
#  潜在的なABI skewが残っていた。esp-idf供給でblobと版一致する。
#
#  ★前ラウンド（WIP option `ASP3_WIFI_INC_IDF`）が「残り壁」として
#  記録していた3件の**実測による決着**（うち2件は引き継ぎ記述が誤り）：
#    (3) `wifi_nan_sync_config_t`→`wifi_nan_config_t` 改名
#        ⇒ **消滅**。ヘッダだけ移してソースをhalに残す構成が原因だった。
#        wifi_init.c もesp-idf供給に揃えれば改名は両側一致する。
#        ＝「ヘッダとソースは揃えて移す」原則の実例。
#    (4) `adc2_cal_include()` が C5 に実装が無く「空スタブが要る」
#        ⇒ **誤り／対処不要**。当該APIは
#        `esp_private/adc_share_hw_ctrl.h:149-163` で
#        `#if CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32C3` の
#        時だけ関数宣言され，**それ以外のチップ（C5含む）では
#        `#define adc2_cal_include()` ＝空マクロ**に展開される（実測）。
#        C5に `adc2_init_cal.c` が無いのは «正常»。
#    (5) `esp_netif.h` … ヘッダパス追加のみで足りる（`esp_netif_*` の
#        呼出しは0件＝実測）⇒ 下で `esp_netif/include` を追加。
#
if(ASP3_ESPIDF_SUPPLY)
    #  esp-idf版 esp_wifi/src/wifi_init.c が `#include "esp_netif.h"` する
    #  （呼出しは0件＝型宣言のためだけ．実測）。
    list(APPEND ASP3_INCLUDE_DIRS
        ${ESP_SUP_DIR}/components/esp_netif/include
    )
    #  esp-idf版 esp_private/wifi.h・esp_wifi_private.h は
    #  `#include "freertos/FreeRTOS.h"`／`freertos/queue.h` する（hal版は
    #  OS非依存の`platform/os.h`＝esp-hal-3rdpartyがNuttX向けにFreeRTOS
    #  依存を剥がしているための差）。ASP3はFreeRTOSを«使わない»ので，
    #  既存のBTコントローラ用FreeRTOS互換スタブ（C3の bt/stub/include，
    #  実体はesp_shimへ委譲）を再利用する。C5のesp_bt.cmakeが既に同じ
    #  ディレクトリを再利用しており，チップ非依存。
    #  実測：esp-idf esp_wifi/include配下でfreertosを要求するのは上記2本
    #  のみ，必要な型は`QueueHandle_t`1つだけ。
    #  APPEND（＝後ろ）にするのは，同ディレクトリに同居する
    #  bt_nimble_config.h／esp_partition.hで意図せずシャドウしないため
    #  （WiFi単体ビルドではこの2つはどこからもincludeされない）。
    list(APPEND ASP3_INCLUDE_DIRS
        ${ESP_COMMON_DIR}/bt/stub/include
    )
endif()

list(APPEND ASP3_LINK_OPTIONS
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_wifi/lib/${WIFI_CHIP_SERIES}
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_phy/lib/${WIFI_CHIP_SERIES}
    -L${ASP3_WIFI_BLOB_SRC}/components/esp_coex/lib/${WIFI_CHIP_SERIES}
)

list(APPEND ASP3_LINK_LIBS
    phy
    coexist
    mesh
    espnow
    core
    net80211
    pp
    # wapi（CONFIG_WPA_WAPI_PSK相当）はOFF固定．libwapi.aは積まない．
)

#
#  ROM関数ld（差分5参照）．chip共通部（hal_esp32c5.cmakeの
#  _esp32c5_rom_ld_files）のうちBLE専用(*_bt_funcs.ld)を除いた
#  WiFi/PHY/coexistが要求するセット．-Wl,-Tで個別に追加する
#  （ASP3_LDSCRIPTは単一メインリンカスクリプト用のためここでは使わない）．
#
set(ESP_ROM_LD_DIR ${ESP_SUP_DIR}/components/esp_rom/${WIFI_CHIP_SERIES}/ld)
set(ESP_WIFI_ROM_LD_FILES
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.api.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.libc.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.libgcc.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.newlib.ld
    #  strcmp/strcpy/strncmp/strncpy/memcmp/memcpy/memmove（PROVIDE．
    #  wpa_supplicant/blobが参照．"suboptimal_for_misaligned_mem"は
    #  ROM側の実装ラベルであって,こちら側の要件ではない＝速度面の
    #  最適版は別途newlib実体が必要だが本用途はリンク解決が目的）
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.libc-suboptimal_for_misaligned_mem.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.version.ld
    ${ESP_SUP_DIR}/components/riscv/ld/rom.api.ld
    #  net80211/pp/phy/systimer/coexistのROM常駐部分のシンボル解決に
    #  必要．C6の実機リンクで未定義参照として発覚したファイル群
    #  （C3のWi-Fi ROM ld一覧には無い＝C6以降のROMはこれらの関数
    #  テーブル・較正データ等をより多くROM側に持つ）で，C5にも
    #  同名ファイルが実在することを確認済み（実際にリンクが通るかは
    #  ビルド検証で確定．§6.1差分3の「link→エラー確認→ROM ld追加」の
    #  手順をC5でも踏襲する）。
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.net80211.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.pp.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.phy.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.systimer.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.coexist.ld
    #  【実機で確定・eco3.ldは意図的に«リンクしない»】esp32c5.rom.eco3.ldは
    #  phy_get_max_pwrの他に383個のROMシンボル（phy_band_i2c_set等，blobが
    #  自前でRAM版を持つPHY関数を含む）を«生ROMアドレス»で供給し，blobのRAM版を
    #  上書きしてしまう．実チップはeco2で，eco2 ROMのphy_band_i2c_setはblob(v8)の
    #  phy_rf_initの呼出し契約と食い違い，esp_wifi_init中にROM 0x40038640への
    #  store access faultで停止した（stock IDF v6.1はblobのRAM版phy_band_i2c_set
    #  ＝0x40803704を使うため無事）．よってeco3.ldは外し，blob自前のRAM版PHY関数を
    #  使わせる．eco3.ldの唯一の本来の目的だったphy_get_max_pwrは
    #  esp_shim_blobglue.cの固定値スタブで供給する（docs/c5-bringup.md実施08．
    #  設計§8.1 9番の懸念が的中）。
)
foreach(_esp_wifi_rom_ld ${ESP_WIFI_ROM_LD_FILES})
    list(APPEND ASP3_LINK_OPTIONS -Wl,-T,${_esp_wifi_rom_ld})
endforeach()

#
#  ------------------------------------------------------------------
#  3. ESP-IDFのmbedTLS（Wireless.mk 96-185行目）
#  ------------------------------------------------------------------
#
#
#  ★供給元による**構造差**（実測．evidence-c5-02）：
#    hal      = mbedtls **4.0.0**（tf-psa-crypto分離＝暗号本体が
#               tf-psa-crypto/{core,drivers/builtin/src}/ へ移動．
#               `library/` にはTLS層のみ33本．**library/common.h は無い**）
#    esp-idf  = mbedtls **3.6.5**（classic＝`library/` に暗号もTLSも一括108本．
#               tf-psa-crypto ディレクトリ自体が存在しない）
#  ＝v5.5.4への統一は mbedtls の**版ダウン**（4.0.0→3.6.5）を伴う。
#  configはESP-IDF本来の port（`mbedtls/esp_config.h`）へ寄せ，
#  halのNuttXシムconfig（`hal/nuttx/include/mbedtls`）は捨てる
#  （esp-idfに存在しない＝S3(LX6)段階3が確立した型と同一）。
#
set(MBEDTLS_DIR ${ESP_SUP_DIR}/components/mbedtls/mbedtls)

list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_SUP_DIR}/components/mbedtls/port/include
    ${ESP_SUP_DIR}/components/mbedtls/port/include/aes
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_system/include
    ${ESP_SUP_DIR}/components/esp_rom/esp32c5/include
    ${MBEDTLS_DIR}/include
)

if(ASP3_ESPIDF_SUPPLY)
    #
    #  mbedtls 3.6.5（classic）．ESP-IDF本家の mbedtls コンポーネントは
    #  `port/include` `mbedtls/include` `mbedtls/library` の3つをPUBLICな
    #  インクルードとして公開し（esp-idf/components/mbedtls/CMakeLists.txt:30），
    #  wpa_supplicant は PRIV_REQUIRES mbedtls でそれを受ける（同 :250）。
    #  ＝`library/` を検索パスに置くのは**本家と同じ**構成。
    #  これは wpa の `esp_supplicant/src/crypto/{crypto_mbedtls.c,tls_mbedtls.c}`
    #  が mbedtls 内部ヘッダ（`common.h` / `ssl_misc.h`）を直接includeする
    #  ため必要（実測：この2本のみ）。
    #
    #  ★shadow注意（S3段階3で既出の罠．**版ダウン固有の新規リスク**）：
    #  3.6.5の `library/common.h` は wpaの `src/utils/common.h` と**同名**。
    #  mbedtls 4.0.0 には `library/common.h` が存在しないため hal 構成では
    #  この衝突自体が起きていなかった。
    #  実測した重なりは **`common.h` ただ1つ**（mbedtlsが公開する
    #  port/include・mbedtls/include・library の全ヘッダ名 × wpaの
    #  src/utils・src/crypto の全ヘッダ名で照合）。
    #  正しい解決先は**wpa側**：本家 esp-idf は wpa_supplicant を
    #  `PRIV_INCLUDE_DIRS src src/utils …` ＋ `PRIV_REQUIRES mbedtls` で
    #  登録しており（CMakeLists.txt:246-250），コンポーネント自身の
    #  include が requirements より**前**に来る＝bare `common.h` は
    #  `src/utils/common.h` に解決される。実際 `crypto_mbedtls.c` は
    #  冒頭で既に `utils/common.h` をincludeしており，後続の
    #  bare `#include "common.h"` は同一ファイルの再includeで
    #  インクルードガードにより無害（＝wpa側が意図された解決先）。
    #  ⇒ `library` は §4（wpa）の**後ろ**に置く（下の §4 末尾を参照）。
    #  `tls_mbedtls.c` が要求する `ssl_misc.h` は library にしか無いため
    #  順序が後ろでも解決する。
    #
    list(APPEND ASP3_COMPILE_DEFS
        MBEDTLS_CONFIG_FILE=<mbedtls/esp_config.h>
    )
else()
    #  mbedtls 4.0.0（tf-psa-crypto分離）＝従来のhal供給
    list(APPEND ASP3_INCLUDE_DIRS
        ${MBEDTLS_DIR}/library
        ${ESP_SUP_DIR}/components/mbedtls/port/psa_driver/include
        ${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/include
        ${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/src
        ${MBEDTLS_DIR}/tf-psa-crypto/core
        ${MBEDTLS_DIR}/tf-psa-crypto/include
        ${ESP_HAL_DIR}/nuttx/include/mbedtls
    )
    list(APPEND ASP3_COMPILE_DEFS
        MBEDTLS_CONFIG_FILE=<mbedtls/esp_config.h>
        TF_PSA_CRYPTO_USER_CONFIG_FILE=\"mbedtls/esp_config.h\"
    )
endif()

if(ASP3_ESPIDF_SUPPLY)

#
#  ------------------------------------------------------------------
#  3-idf. mbedtls 3.6.5（classic）のソース一覧
#  ------------------------------------------------------------------
#
#  hal(4.0.0)の一覧（下の else 節＝tf-psa builtin 44 + core 8 + port 6）を
#  3.6.5の `library/` 一括レイアウトへ**1:1で写像**したもの。実測による差分：
#    - `pk_rsa.c`               … 4.0.0のみ（3.6.5では pk_wrap.c に内包）＝除外
#    - `tf_psa_crypto_config.c` … 4.0.0のみ（tf-psa分離に伴う新設）＝除外
#    - `tf_psa_crypto_version.c`… 4.0.0のみ ⇒ 3.6.5の `version.c` で代替
#      （sdkconfig_stub の CONFIG_MBEDTLS_VERSION_C=1 が要求）
#    - `pk_ecc.c` は3.6.5にも実在＝そのまま採用
#  上記以外の41本は同名で3.6.5 library/ に実在する（実測）。
#
#  3.6.5固有に**追加**が要るもの（実測．4.0.0ではtf-psa core側にあった
#  ものが3.6.5では別ファイルに分かれている）：
#    - `psa_crypto_aead.c`   … psa_crypto.c が psa_aead_* を参照
#    - `entropy_poll.c`      … entropy.c のプラットフォーム・エントロピ源
#    - `bignum_mod.c` / `bignum_mod_raw.c` … 3.6.5 の bignum_core/ecp が参照
#
#  **TLS/x509 は積まない**（hal構成と同じ機能スコープ＝暗号プリミティブのみ）。
#  wpaの `tls_mbedtls.c` はコンパイルされるが，WPA2-PSK経路では
#  どこからも呼ばれず `-ffunction-sections + --gc-sections` で脱落する
#  （hal構成でも同じ＝TLS本体を積まずにリンクが通っている実績がある）。
#
set(MBEDTLS_LIB_DIR ${MBEDTLS_DIR}/library)
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${MBEDTLS_LIB_DIR}/aes.c
    ${MBEDTLS_LIB_DIR}/aria.c
    ${MBEDTLS_LIB_DIR}/bignum_core.c
    ${MBEDTLS_LIB_DIR}/bignum.c
    ${MBEDTLS_LIB_DIR}/bignum_mod.c
    ${MBEDTLS_LIB_DIR}/bignum_mod_raw.c
    ${MBEDTLS_LIB_DIR}/ccm.c
    ${MBEDTLS_LIB_DIR}/cipher_wrap.c
    ${MBEDTLS_LIB_DIR}/cipher.c
    ${MBEDTLS_LIB_DIR}/cmac.c
    ${MBEDTLS_LIB_DIR}/constant_time.c
    ${MBEDTLS_LIB_DIR}/ctr_drbg.c
    ${MBEDTLS_LIB_DIR}/ecp_curves.c
    ${MBEDTLS_LIB_DIR}/ecp.c
    ${MBEDTLS_LIB_DIR}/entropy.c
    ${MBEDTLS_LIB_DIR}/entropy_poll.c
    ${MBEDTLS_LIB_DIR}/gcm.c
    ${MBEDTLS_LIB_DIR}/md.c
    ${MBEDTLS_LIB_DIR}/pkcs5.c
    ${MBEDTLS_LIB_DIR}/platform_util.c
    ${MBEDTLS_LIB_DIR}/platform.c
    ${MBEDTLS_LIB_DIR}/sha1.c
    ${MBEDTLS_LIB_DIR}/sha3.c
    ${MBEDTLS_LIB_DIR}/sha256.c
    ${MBEDTLS_LIB_DIR}/sha512.c
    ${MBEDTLS_LIB_DIR}/pk.c
    ${MBEDTLS_LIB_DIR}/pk_wrap.c
    ${MBEDTLS_LIB_DIR}/pkparse.c
    ${MBEDTLS_LIB_DIR}/ecdsa.c
    ${MBEDTLS_LIB_DIR}/asn1parse.c
    ${MBEDTLS_LIB_DIR}/asn1write.c
    ${MBEDTLS_LIB_DIR}/rsa.c
    ${MBEDTLS_LIB_DIR}/md5.c
    ${MBEDTLS_LIB_DIR}/oid.c
    ${MBEDTLS_LIB_DIR}/pem.c
    ${MBEDTLS_LIB_DIR}/hmac_drbg.c
    ${MBEDTLS_LIB_DIR}/rsa_alt_helpers.c
    ${MBEDTLS_LIB_DIR}/ecdh.c
    ${MBEDTLS_LIB_DIR}/pk_ecc.c
    ${MBEDTLS_LIB_DIR}/psa_util.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_ffdh.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_ecp.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_rsa.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_cipher.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_mac.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_hash.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_aead.c
    #  4.0.0 の tf-psa-crypto/core/ 相当（3.6.5では library/ に同居）
    ${MBEDTLS_LIB_DIR}/psa_crypto_client.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_driver_wrappers_no_static.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_slot_management.c
    ${MBEDTLS_LIB_DIR}/psa_crypto_storage.c
    ${MBEDTLS_LIB_DIR}/psa_crypto.c
    ${MBEDTLS_LIB_DIR}/psa_its_file.c
    ${MBEDTLS_LIB_DIR}/version.c
)

# mbedtls port（ESP-IDF本家．hal版のNuttX向けpsa_driver/は3.6.5に存在しない）
set(MBEDTLS_PORT_DIR ${ESP_SUP_DIR}/components/mbedtls/port)
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${MBEDTLS_PORT_DIR}/esp_hardware.c
    ${MBEDTLS_PORT_DIR}/esp_mem.c
    ${MBEDTLS_PORT_DIR}/esp_timing.c
    #  sdkconfig_stub の CONFIG_MBEDTLS_ROM_MD5=1 が MBEDTLS_MD5_ALT を
    #  立てる（esp_config.h:188）ため md5_alt 実体が要る
    #  （本家 esp-idf/components/mbedtls/CMakeLists.txt:337-339 と同じ条件）。
    ${MBEDTLS_PORT_DIR}/md/esp_md.c
)
#  ★hal版にあった `-Wl,-u,mbedtls_psa_crypto_init_include_impl` は付けない：
#  当該シンボルを供給する `port/esp_psa_crypto_init.c` は
#  **esp-hal-3rdparty独自**（NuttX向け）で esp-idf には存在しない（実測）。
#  3.6.5 では psa_crypto_init() を呼ぶ側（crypto_mbedtls.c）が直接リンクする。

else() # ASP3_ESPIDF_SUPPLY

# mbedtls builtin（tf-psa-crypto/drivers/builtin/src．Wireless.mk 114-157行目）
set(MBEDTLS_BUILTIN_DIR ${MBEDTLS_DIR}/tf-psa-crypto/drivers/builtin/src)
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${MBEDTLS_BUILTIN_DIR}/aes.c
    ${MBEDTLS_BUILTIN_DIR}/aria.c
    ${MBEDTLS_BUILTIN_DIR}/bignum_core.c
    ${MBEDTLS_BUILTIN_DIR}/bignum.c
    ${MBEDTLS_BUILTIN_DIR}/ccm.c
    ${MBEDTLS_BUILTIN_DIR}/cipher_wrap.c
    ${MBEDTLS_BUILTIN_DIR}/cipher.c
    ${MBEDTLS_BUILTIN_DIR}/cmac.c
    ${MBEDTLS_BUILTIN_DIR}/constant_time.c
    ${MBEDTLS_BUILTIN_DIR}/ctr_drbg.c
    ${MBEDTLS_BUILTIN_DIR}/ecp_curves.c
    ${MBEDTLS_BUILTIN_DIR}/ecp.c
    ${MBEDTLS_BUILTIN_DIR}/entropy.c
    ${MBEDTLS_BUILTIN_DIR}/gcm.c
    ${MBEDTLS_BUILTIN_DIR}/md.c
    ${MBEDTLS_BUILTIN_DIR}/pkcs5.c
    ${MBEDTLS_BUILTIN_DIR}/platform_util.c
    ${MBEDTLS_BUILTIN_DIR}/platform.c
    ${MBEDTLS_BUILTIN_DIR}/sha1.c
    ${MBEDTLS_BUILTIN_DIR}/sha3.c
    ${MBEDTLS_BUILTIN_DIR}/sha256.c
    ${MBEDTLS_BUILTIN_DIR}/sha512.c
    ${MBEDTLS_BUILTIN_DIR}/pk.c
    ${MBEDTLS_BUILTIN_DIR}/pk_wrap.c
    ${MBEDTLS_BUILTIN_DIR}/pkparse.c
    ${MBEDTLS_BUILTIN_DIR}/ecdsa.c
    ${MBEDTLS_BUILTIN_DIR}/asn1parse.c
    ${MBEDTLS_BUILTIN_DIR}/asn1write.c
    ${MBEDTLS_BUILTIN_DIR}/rsa.c
    ${MBEDTLS_BUILTIN_DIR}/md5.c
    ${MBEDTLS_BUILTIN_DIR}/oid.c
    ${MBEDTLS_BUILTIN_DIR}/pem.c
    ${MBEDTLS_BUILTIN_DIR}/hmac_drbg.c
    ${MBEDTLS_BUILTIN_DIR}/rsa_alt_helpers.c
    ${MBEDTLS_BUILTIN_DIR}/ecdh.c
    ${MBEDTLS_BUILTIN_DIR}/pk_ecc.c
    ${MBEDTLS_BUILTIN_DIR}/pk_rsa.c
    ${MBEDTLS_BUILTIN_DIR}/psa_util.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_ffdh.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_ecp.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_rsa.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_cipher.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_mac.c
    ${MBEDTLS_BUILTIN_DIR}/psa_crypto_hash.c
)

# tf-psa-crypto core（Wireless.mk 159-168行目）
set(TF_PSA_CORE_DIR ${MBEDTLS_DIR}/tf-psa-crypto/core)
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${TF_PSA_CORE_DIR}/psa_crypto_client.c
    ${TF_PSA_CORE_DIR}/psa_crypto_driver_wrappers_no_static.c
    ${TF_PSA_CORE_DIR}/psa_crypto_slot_management.c
    ${TF_PSA_CORE_DIR}/psa_crypto_storage.c
    ${TF_PSA_CORE_DIR}/psa_crypto.c
    ${TF_PSA_CORE_DIR}/psa_its_file.c
    ${TF_PSA_CORE_DIR}/tf_psa_crypto_config.c
    ${TF_PSA_CORE_DIR}/tf_psa_crypto_version.c
)

# mbedtls port（Wireless.mk 170-186行目）
set(MBEDTLS_PORT_DIR ${ESP_SUP_DIR}/components/mbedtls/port)
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${MBEDTLS_PORT_DIR}/esp_psa_crypto_init.c
    ${MBEDTLS_PORT_DIR}/esp_hardware.c
    ${MBEDTLS_PORT_DIR}/esp_mem.c
    ${MBEDTLS_PORT_DIR}/esp_timing.c
    ${MBEDTLS_PORT_DIR}/psa_driver/esp_mac/psa_crypto_driver_esp_hmac_opaque.c
    ${MBEDTLS_PORT_DIR}/psa_driver/esp_md/psa_crypto_driver_esp_md5.c
)

# PSA crypto初期化を確実にリンクへ含める（Wireless.mk 179行目）
list(APPEND ASP3_LINK_OPTIONS
    -Wl,-u,mbedtls_psa_crypto_init_include_impl
)

endif() # ASP3_ESPIDF_SUPPLY（mbedtlsソース一覧の供給元分岐）

#
#  ------------------------------------------------------------------
#  4. WPA Supplicant（Wireless.mk 187-339行目）
#  ------------------------------------------------------------------
#
set(WPA_SUPPLICANT_DIR ${ESP_SUP_DIR}/components/wpa_supplicant)

list(APPEND ASP3_COMPILE_DEFS
    __ets__
    CONFIG_CRYPTO_MBEDTLS
    CONFIG_ECC
    CONFIG_IEEE80211W
    CONFIG_WPA3_SAE
    EAP_PEER_METHOD
    ESP_PLATFORM=1
    ESP_SUPPLICANT
    ESPRESSIF_USE
    IEEE8021X_EAPOL
    USE_WPA2_TASK
    CONFIG_SHA256
    USE_WPS_TASK
    # ESPRESSIF_WIFI_SOFTAP_SAE_SUPPORT/SAE_PK/SAE_H2E/OWE_STA/GCMP/GMAC相当は
    # NuttX Kconfig既定OFFに倣い未定義（差分3）
)

list(APPEND ASP3_INCLUDE_DIRS
    ${WPA_SUPPLICANT_DIR}/include
    ${WPA_SUPPLICANT_DIR}/src
    ${WPA_SUPPLICANT_DIR}/src/ap
    ${WPA_SUPPLICANT_DIR}/src/common
    ${WPA_SUPPLICANT_DIR}/src/utils
    ${WPA_SUPPLICANT_DIR}/src/crypto
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/include
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src
    ${WPA_SUPPLICANT_DIR}/port/include
)

if(ASP3_ESPIDF_SUPPLY)
    #  ★mbedtls 3.6.5 の `library/` は **wpa の後ろ**に置く（§3の
    #  shadow注意を参照）：`common.h` が両者に存在し，正しい解決先は
    #  wpa側（本家 esp-idf のコンポーネント登録順と同一）。
    #  `library/` 自体は wpa の `crypto_mbedtls.c`（`common.h`）と
    #  `tls_mbedtls.c`（`ssl_misc.h`）が mbedtls 内部ヘッダを直接
    #  includeするため必要＝本家も mbedtls コンポーネントの公開
    #  include に `mbedtls/library` を含めている（CMakeLists.txt:30）。
    list(APPEND ASP3_INCLUDE_DIRS
        ${MBEDTLS_DIR}/library
    )
endif()

# wpa_supplicant/src/ap（Wireless.mk 233-243行目．7ファイル）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/ap/ap_config.c
    ${WPA_SUPPLICANT_DIR}/src/ap/ieee802_11.c
    ${WPA_SUPPLICANT_DIR}/src/ap/comeback_token.c
    ${WPA_SUPPLICANT_DIR}/src/ap/pmksa_cache_auth.c
    ${WPA_SUPPLICANT_DIR}/src/ap/sta_info.c
    ${WPA_SUPPLICANT_DIR}/src/ap/wpa_auth_ie.c
    ${WPA_SUPPLICANT_DIR}/src/ap/wpa_auth.c
)

# wpa_supplicant/src/common（Wireless.mk 245-257行目．6ファイル．
# sae_pk.cはCONFIG_ESPRESSIF_WIFI_ENABLE_SAE_PK限定＝差分3によりOFF）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/common/dragonfly.c
    ${WPA_SUPPLICANT_DIR}/src/common/sae.c
    ${WPA_SUPPLICANT_DIR}/src/common/wpa_common.c
    ${WPA_SUPPLICANT_DIR}/src/common/bss.c
    ${WPA_SUPPLICANT_DIR}/src/common/scan.c
    ${WPA_SUPPLICANT_DIR}/src/common/ieee802_11_common.c
)

# wpa_supplicant/src/crypto（Wireless.mk 259-272行目．12ファイル．
# aes-siv.cはesp_supplicant/src/crypto側で積む＝下記）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/crypto/aes-ccm.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/aes-gcm.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/aes-unwrap.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/aes-wrap.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/ccmp.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/crypto_ops.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/des-internal.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/dh_groups.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/rc4.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/sha1-prf.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/sha256-kdf.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/sha256-prf.c
)

# wpa_supplicant/src/eap_peer（Wireless.mk 274-285行目．10ファイル）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/chap.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_common.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_mschapv2.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_peap_common.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_peap.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_tls_common.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_tls.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap_ttls.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/eap.c
    ${WPA_SUPPLICANT_DIR}/src/eap_peer/mschapv2.c
)

# wpa_supplicant/src/rsn_supp（Wireless.mk 287-291行目．3ファイル）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/rsn_supp/pmksa_cache.c
    ${WPA_SUPPLICANT_DIR}/src/rsn_supp/wpa_ie.c
    ${WPA_SUPPLICANT_DIR}/src/rsn_supp/wpa.c
)

# wpa_supplicant/src/utils（Wireless.mk 293-304行目．8ファイル）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/src/utils/base64.c
    ${WPA_SUPPLICANT_DIR}/src/utils/bitfield.c
    ${WPA_SUPPLICANT_DIR}/src/utils/common.c
    ${WPA_SUPPLICANT_DIR}/src/utils/ext_password.c
    ${WPA_SUPPLICANT_DIR}/src/utils/json.c
    ${WPA_SUPPLICANT_DIR}/src/utils/uuid.c
    ${WPA_SUPPLICANT_DIR}/src/utils/wpa_debug.c
    ${WPA_SUPPLICANT_DIR}/src/utils/wpabuf.c
)

# wpa_supplicant/port（Wireless.mk 306-311行目．2ファイル．os_xtensa.cは
# 差分4参照＝ISA非依存でNuttXもRISC-Vでそのまま使用）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/port/eloop.c
    ${WPA_SUPPLICANT_DIR}/port/os_xtensa.c
)

# esp_supplicant/src（Wireless.mk 313-328行目．8ファイル）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_common.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_hostap.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_wpa_main.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_wpa3.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_wpas_glue.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_owe.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_scan.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/esp_wps.c
)

# esp_supplicant/src/crypto（Wireless.mk 330-339行目．6ファイル．
# aes-siv.cのみsrc/crypto側＝上のwpa_supplicant/src/crypto一覧には含めない）
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/crypto/crypto_mbedtls-bignum.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/crypto/crypto_mbedtls-ec.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/crypto/crypto_mbedtls-rsa.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/crypto/crypto_mbedtls.c
    ${WPA_SUPPLICANT_DIR}/esp_supplicant/src/crypto/tls_mbedtls.c
    ${WPA_SUPPLICANT_DIR}/src/crypto/aes-siv.c
)

#
#  ------------------------------------------------------------------
#  5. esp_wifi Cソース（Wireless.mk 341-343行目．3ファイル）
#  ------------------------------------------------------------------
#
#  esp_phy/esp_coexは差分5の通りprebuilt libのみ（.a）で，NuttXも
#  WiFi単体（BLE無効）時はCソースを一切コンパイルしない．
#  （esp_phy/src/btbb_init.c・lib_printf.cはBLE専用＝Wireless.mk 61,73行目．
#   esp_coex/src配下（coexist.c等）はNuttXのCHIP_CSRCSに一切現れない＝
#   コンパイル済みlibcoexist.aへ完全に閉じている）
#
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_SUP_DIR}/components/esp_wifi/src/wifi_init.c
    ${ESP_SUP_DIR}/components/esp_wifi/src/lib_printf.c
    ${ESP_SUP_DIR}/components/esp_wifi/regulatory/esp_wifi_regulatory.c
    #  esp_phy/src/lib_printf.c（wifi版とは別ファイル．phy_printf/
    #  rtc_printfを提供．libphy.aがphy_printfを直接参照する＝
    #  wait_rfpll_cal_end等）．同名staticのlib_printf()はTU内
    #  linkageのためesp_wifi版と衝突しない．
    ${ESP_SUP_DIR}/components/esp_phy/src/lib_printf.c
)

#
#  ------------------------------------------------------------------
#  6. os_adapter shimが実体を必要とするesp_hw_support由来の実ソース
#     （NuttXのhal_esp32c5.cmake＝チップ共通部で常時コンパイルされる
#     ファイルの一部．periph_ctrl.c単体は依存が軽い＝
#     esp_private/critical_section.h（シングルコアなのでloc_cpu/
#     unl_cpuへのマクロ委譲をhal_stub/include/platform/os.hに追加）と
#     hal/clk_gate_ll.h（レジスタ直叩きのstatic inline．追加コンパイル
#     対象なし）のみで閉じるため実ソースを採用．periph_module_reset・
#     wifi_module_enable/disable（未定義シンボル）に加え，
#     phy_module_enable/disable（esp_shim_blobglue.cのesp_phy_enable/
#     disable簡易実装から呼ぶ）も提供する．
#     mac_addr.c（esp_read_mac）・esp_phy/src/phy_init.c（esp_phy_enable
#     本体）はesp_efuse本体／nvs_flash／esp_timer等チップ全体のブート
#     基盤に依存が及ぶため採用せず，esp_shim_blobglue.cで簡易実装する
#     （妥協点はdocs/wifi-shim.md参照）．
#  ------------------------------------------------------------------
#
list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_SUP_DIR}/components/esp_hw_support/periph_ctrl.c
    #  C6固有：periph_ctrl.cのwifi/bt module enable経路が
    #  modem_clock_module_enable/disable等を直接呼ぶ（新設のmodem_clock
    #  サブシステム）。C3にはSYSTEM_WIFI_CLK_EN_REG直書き＝
    #  target_kernel_impl.cのhardware_init_hookで代替していたが，当初は
    #  「C6はこの実ソースが自己完結して動くため同種のhookは不要」という
    #  仮説を立てていた。**実機検証の結果，この仮説は誤りと判明**：
    #  Direct Bootではesp_perip_clk_init()（esp_system/port/soc/esp32c5/
    #  clk.c）自体が一切呼ばれないため，modem_clock_module_enable()
    #  だけでは有効化されない別系統のクロックゲート（WIFIPWRドメイン＝
    #  modem_clock_hal_enable_wifipwr_clock）が未有効化のまま残り，
    #  PWDET/AGC等ベースバンド系レジスタへのアクセスがバスタイムアウト
    #  する（HP_SYSTEM_MODEM_PERI_TIMEOUT_ADDR_REG=0x600a2868で実測
    #  確認）。修正はesp_wifi_adapter.cのwifi_clock_enable_wrapper()で
    #  modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, ...)を
    #  明示的に呼ぶ形で行った（esp_perip_clk_init()相当の代替）。
    #  詳細はdocs/wifi-shim-c6.md「実施6」参照。
    ${ESP_SUP_DIR}/components/esp_hw_support/modem_clock.c
    ${ESP_SUP_DIR}/components/hal/esp32c5/modem_clock_hal.c
    #  rtc_clk_xtal_freq_get()（modem_clock.c経由で参照。--gc-sectionsに
    #  より実際に呼ばれる関数のみリンクされるため，同ファイル内の
    #  他のPLL較正関数群は取り込まれない想定＝安全に全体を採用）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/rtc_clk.c
    #  modem_clock_select_lp_clock_source()がefuse_hal_chip_revision()
    #  を参照する（eco0判定）。efuse_hal.cはefuse_ll.h（static inline
    #  レジスタ直読み）のみに依存する軽量な実ソースのため採用。
    #  チップ非依存の共通実装（efuse_hal_chip_revision本体）＋C6固有の
    #  efuse_hal_get_{major,minor}_chip_version（ともに--gc-sectionsで
    #  実際に呼ばれる関数のみ残る）の2ファイル構成。
    ${ESP_SUP_DIR}/components/hal/efuse_hal.c
    ${ESP_SUP_DIR}/components/hal/esp32c5/efuse_hal.c
    #  C5固有の追加（C6には無い依存）：C5はsoc_caps.hで
    #  SOC_CLOCK_TREE_MANAGEMENT_SUPPORTED=1・
    #  SOC_CLK_ANA_I2C_MST_DEPENDS_ON_MODEM_APB=1を定義しており
    #  （C6のsoc_caps.hには両方とも存在しない），regi2c_ctrl.hの
    #  ANA_I2C_SRC_CLOCK_ENABLE()・modem_clock.cのmodem_clock_hal_init()が
    #  esp_clk_tree_enable_src()を実際に呼ぶ（実機リンクの未定義参照で
    #  発覚）。esp_hw_support/port/esp32c5/esp_clk_tree.cを採用して解決する。
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/esp_clk_tree.c
    #  esp_clk_tree.c（チップ依存の周波数選択ロジック）自身が
    #  クロックソース別の実装（xtal32k/lp_slow/rc_fast/lp_fast）を
    #  chip非依存の共通実装（esp_clk_tree_common.c）に委譲し，
    #  CPU/XTAL周波数取得はHAL層（clk_tree_hal.c）のclk_hal_cpu_get_freq_hz
    #  ／clk_hal_xtal_get_freq_mhzに委譲する（実機リンクの未定義参照で
    #  発覚した芋づる式の依存）。
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp_clk_tree_common.c
    ${ESP_SUP_HAL_clock}/esp32c5/clk_tree_hal.c
    #  esp_clk_tree_common.cのRTC較正呼出し（rtc_clk_cal／
    #  rtc_clk_freq_cal＝チップ依存のRTC較正シーケンス実体，
    #  esp_hw_support/port/esp32c5/rtc_time.c）・esp_clk_slowclk_cal_set
    #  （チップ非依存のグローバル較正値キャッシュ，esp_hw_support/
    #  esp_clk.c）を解決する（実機リンクの未定義参照で発覚）。
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c5/rtc_time.c
    ${ESP_SUP_DIR}/components/esp_hw_support/esp_clk.c
)

#
#  ------------------------------------------------------------------
#  参考：esp-hal-3rdparty の nuttx/ ディレクトリ由来でRTOS非依存に
#  流用できそうなソース（列挙のみ．積むかはPhase B-2のos_adapter shim
#  設計次第のため私の判断で決める＝ここではAPPENDしない）
#  ------------------------------------------------------------------
#
#   ${ESP_HAL_DIR}/nuttx/src/esp_event.c
#       esp_event(イベントループ)のNuttX向け薄glue．内部でNuttXの
#       work queue/semaphoreを呼ぶため，asp3のイベント機構
#       （set_flg/wai_flg等）へ差し替えるshimが要る．
#   ${ESP_SUP_DIR}/components/esp_timer/src/ets_timer_legacy.c
#       レガシーets_timer API．wifi_init.c等が参照．内部はesp_timer.cの
#       ラッパのため esp_timer.c 本体（esp_timer/src/esp_timer.c，
#       esp_timer_impl_systimer.c 等）も合わせて必要になる可能性が高い．
#       target_timer.c（SYSTIMER直叩き）との重複・競合を要検討．
#

endif() # ESP32C5_WIFI

#
#  ------------------------------------------------------------------
#  検証：リストした全ファイルの実在チェック（シェルループ．
#  実行結果はコミット時のコメントとして記録．本ブロック自体は
#  cmake構文には影響しない＝コメントのみ）
#  ------------------------------------------------------------------
#
#  for f in $(グレップで ASP3_SYSSVC_TARGET_C_FILES に APPEND した行のパスを展開); do
#      test -f "$f" || echo "MISSING: $f"
#  done
#
#  実行結果（2026-07-03時点，esp-hal-3rdparty@b90b183・mbedtls submodule
#  取得済み）：全123ファイル実在．MISSINGなし．
#    wpa_supplicant: 62（ap 7 + common 6 + crypto(aes-siv.c含む) 13 +
#                        eap_peer 10 + rsn_supp 3 + utils 8 + port 2 +
#                        esp_supplicant/src 8 + esp_supplicant/src/crypto 5）
#    esp_wifi:        3（wifi_init.c/lib_printf.c/esp_wifi_regulatory.c）
#    mbedtls:        58（tf-psa-crypto/drivers/builtin/src 44 +
#                        tf-psa-crypto/core 8 + port 6）
#    ROM ld:          7（-Tオプション．実在確認済み，コンパイルソースではない）
#    合計: 62 + 3 + 58 = 123（ROM ld 7本は別枠）
#
