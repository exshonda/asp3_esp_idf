#
#               ESP32-C6 Wi-Fi統合（esp-hal-3rdparty prebuilt libs）
#
#  NuttX の arch/risc-v/src/common/espressif/Wireless.mk（および同ディレクトリの
#  Wireless.cmake）からの移植．参照コミット：
#    apache/nuttx  master（本ファイル作成時点のワークツリー，Wireless.mk/
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
#  意図的な差分（NuttXとの相違点）：
#    1. BLE関連（tinycrypt/nimble/bt/…）は移植しない．ESP32-C6のBLEコント
#       ローラ移植はPhase Bの対象外（本ファイルはCONFIG_ESPRESSIF_WIFI相当
#       ＝Wireless.mk/cmakeの97行目以降＝WiFiパスのみ）．
#    2. CONFIG_WPA_WAPI_PSK（WAPI）はOFF固定．libwapi.aは積まない．
#    3. CONFIG_ESPRESSIF_WIFI_* のSAE/OWE/GCMP/GMAC等のオプション拡張define
#       はNuttX Kconfigのデフォルト（未設定＝OFF）に倣い，本ファイルでは
#       付与しない（必要になった時点でオプション化する）．
#    4. wpa_supplicant/port/os_xtensa.c はファイル名にXtensaを含むが中身は
#       ISA非依存のOS抽象化層（eloop用のtime/mem wrapper）であり，NuttXも
#       RISC-V(ESP32-C6等)でそのまま使っている．asp3側でも同名のまま採用。
#       ただし実体（loc_cpu/get_tim等ASP3 API呼び出し）はPhase Bのos_adapter
#       shimで別途実装が必要（本ファイルはソースを積むのみ）．
#    5. ROM関数ld（esp32c6.rom*.ld）はWireless.mk/cmake自体には現れない
#       （NuttXではhal_esp32c6.cmakeのチップ共通部で全ビルドに対し常時リンク
#       される）．asp3の既存esp32c6.ld/chip.cmakeはROM ldを一切リンクして
#       いない（libc_stub.cで自前providesのため今まで不要だった）ため，
#       prebuilt wifi/phy/coexistライブラリがROM関数を要求する分だけ本
#       ファイルで追加する．BLE専用のld（*_bt_funcs.ld・eco3*.ld）は上記
#       差分1の理由で対象外．
#
#  検証：末尾のシェルループでリストした全ファイルの実在を確認済み
#  （mbedtls submoduleは取得済みのためスキップなし）．存在しなかった
#  ものはコメントアウト＋注記（現時点ではゼロ）．
#
#  ビルド状況（2026-07-03時点）：コンパイルは全123ファイル通過
#  （`cmake --build build/wifi` でerror: 0件）．不足していた
#  esp-hal-3rdparty側インクルードパスは §1b に追記，newlib由来の
#  libcヘッダ／NuttXビルド専用ヘッダ（platform/os.h・esp_netif.h等）
#  は hal_stub/include 配下にコンパイル用スタブとして追加した
#  （実体はPhase B-2のos_adapter shimで提供．詳細は各スタブの
#  ファイル先頭コメント）．
#  リンクも現状は成功する（--gc-sectionsにより，現在のサンプル
#  アプリからesp_wifi_init()等のWi-Fiエントリポイントが一切
#  参照されないため，未実装のosi_funcs等はそもそもリンク対象に
#  含まれない）．os_adapter実装後，実際にWi-Fi APIを呼び出す
#  アプリコードを書いた時点で初めて未定義シンボルが顕在化する
#  （個々のオブジェクトに対する`nm -u`ベースの未定義シンボル
#  分類はdocs/wifi-shim.mdの実施結果／作業ログを参照）．
#

option(ESP32C6_WIFI "Integrate esp-hal-3rdparty prebuilt Wi-Fi libs (wpa_supplicant/esp_wifi/mbedtls; Phase B, compiles clean)" OFF)

if(ESP32C6_WIFI)

#
#  ★2026-07-21：hal（esp-hal-3rdparty）供給は撤去済み（submodule ごと削除）。
#  ESP コンポーネントは常に esp-idf submodule（ESP_SUP_DIR＝真の v5.5.4）から供給する。
#

set(WIFI_CHIP_SERIES esp32c6)

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
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c6/include
    #  esp_hw_log.h（rtc_clk.c経由．C6固有のrtc_clk.c採用に伴う追加）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/include
    #  esp_private/esp_wifi_private.h・esp_wifi_types_generic.h等
    ${ESP_SUP_DIR}/components/esp_wifi/include
    #  esp_heap_caps.h（mbedtls/port/esp_mem.c等）
    ${ESP_SUP_DIR}/components/heap/include
    #  esp_efuse.h（mbedtls/port/psa_driver/esp_mac/*.c）
    ${ESP_SUP_DIR}/components/efuse/include
    ${ESP_SUP_DIR}/components/efuse/esp32c6/include
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
    ${ESP_SUP_HAL_gpio}/esp32c6/include
    #  esp32c6/rom/ets_sys.h（wpa_supplicant/port/eloop.c）
    ${ESP_SUP_DIR}/components/esp_rom/esp32c6/include/esp32c6
    #  esp_pm.h（esp_wifi/src/wifi_init.c）
    ${ESP_SUP_DIR}/components/esp_pm/include
    #  esp_phy_init.h（esp_wifi/src/wifi_init.c）
    ${ESP_SUP_DIR}/components/esp_phy/include
    #  hal/clk_gate_ll.h（esp_hw_support/periph_ctrl.c．§6参照）
    ${ESP_SUP_HAL_clock}/esp32c6/include
    ${ESP_SUP_HAL_clock}/include
    #  hal/pau_types.h（esp_hw_support/include/esp_private/esp_regdma.h経由）・
    #  hal/pmu_hal.h（esp_hw_support/include/esp_private/esp_pmu.h経由）．
    #  C6以降の新コンポーネント＝C3のesp_wifi.cmakeには無い追加分
    ${ESP_SUP_HAL_pmu}/include
    ${ESP_SUP_HAL_pmu}/esp32c6/include
    #  pmu_param.h（esp_pmu.h経由．PMU＝C6以降の新サブシステム）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c6/private_include
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
#  phy_common.c（phy_set_modem_flag等の補助関数）・esp32c6向け
#  phy_init_data.c（PHY初期化パラメータの既定バイナリ．
#  CONFIG_ESP_PHY_INIT_DATA_IN_PARTITION未定義＝アプリバイナリ
#  内蔵版）を採用し，esp_shim_blobglue.cの簡易実装は削除する．
#
#  較正モード：sdkconfig.h（hal/nuttx/esp32c6/include/sdkconfig.h．
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
    #  の区別が不要なESP32-C6向け設計．docs/wifi-shim.md参照）ため，
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
    #  CONFIG_IDF_TARGET_ESP32C6：hal/components/esp_wifi/include/
    #  esp_private/wifi_os_adapter.hのwifi_osi_funcs_t定義が
    #  `#if CONFIG_IDF_TARGET_ESP32C6 || ...C5 || ...C61`で
    #  _regdma_link_set_write_wait_content／
    #  _sleep_retention_find_link_by_idの2フィールドを条件付き
    #  追加する（C6/C5/C61専用のsleep retention連携）．
    #  【訂正】当初はこのマクロがどこにも定義されておらず構造体ABIが
    #  不整合を起こしていると判断したが，実際には
    #  hal/nuttx/esp32c6/include/sdkconfig.h（esp_attr.h経由で全ファイル
    #  から到達可能）が既に`#define CONFIG_IDF_TARGET_ESP32C6 1`を
    #  提供しており，このコンパイル定義を追加する前から構造体サイズは
    #  既に0x1e8バイト（2フィールド込み）だった．実際に本行を
    #  一時的にコメントアウトして再ビルド・nm確認し，サイズが変化
    #  しない（0x1e8のまま）ことを検証済み＝ABI不整合は最初から
    #  存在しなかった（誤判定．詳細はdocs/wifi-shim-c6.md「実施10」
    #  訂正部分参照）．本行は実害はないが，ヘッダ側の暗黙のinclude
    #  経由ではなくこのビルドの意図を明示するドキュメント目的として
    #  残す（冗長だが害はない）．
    CONFIG_IDF_TARGET_ESP32C6=1
)

list(APPEND ASP3_INCLUDE_DIRS
    #  phy_init_data.h／phy_init_deps.h（esp32c6向けデフォルトPHY
    #  初期化データ配列＋PHY_INIT_MODEM_CLOCK_REQUIRED_BITS）
    ${ESP_SUP_DIR}/components/esp_phy/esp32c6/include
)

list(APPEND ASP3_SYSSVC_TARGET_C_FILES
    ${ESP_SUP_DIR}/components/esp_phy/src/phy_init.c
    ${ESP_SUP_DIR}/components/esp_phy/src/phy_common.c
    ${ESP_SUP_DIR}/components/esp_phy/esp32c6/phy_init_data.c
)

#
#  ------------------------------------------------------------------
#  2. リンクライブラリパス・ライブラリ（Wireless.mk 80-88行目）
#  ------------------------------------------------------------------
#
#
#  ★【2026-07-17 evidence-c6-11】ここに在った «DIAGNOSTIC (temporary)» の
#  `-Wl,--wrap=` ブロック（**52本**）を **削除した**（コメント自身が「調査終了後に
#  このブロックごと削除する」と指示していたもの。実施12＝2026-07-05 の調査は終了済み）。
#  ★問題だったのは «診断トグルが無く if(ESP32C6_WIFI)＝機能トグルで有効化» ＝
#  **WiFi を使う全ビルドが計装込み**だったこと。--wrap は死んでおらず、
#  ELF に __wrap_* が 37 個リンクされていた（★52＝cmake 指示数／37＝リンク済み
#  シンボル数＝**別の指標**。どちらも正しい）。
#  ★再投入は診断トグル `ESP32C6_WIFI_TRACE`（既定 OFF）で行う＝**退避路は捨てない**。
#  ON のときだけ wifi/wifi_trace_wrap.cmake（--wrap 52本）を積む。
#
option(ESP32C6_WIFI_TRACE
    "DIAGNOSTIC (default OFF): C6 Wi-Fi -Wl,--wrap trace hooks (docs/wifi-shim-c6.md 実施12). Routes esf_buf_alloc/lmacProcessRxSucData/register_chipv7_phy etc. through wifi_trace.c; __wrap_esf_buf_alloc(a1==2) then costs ~1035 volatile MMIO reads (AGC 1024 words) on the packet-buffer alloc path and is NOT gated by wifi_trace_frozen. Never enable for functional/perf builds"
    OFF)
if(ESP32C6_WIFI_TRACE)
    list(APPEND ASP3_COMPILE_DEFS TOPPERS_ESP32C6_WIFI_TRACE)
    include(${CMAKE_CURRENT_LIST_DIR}/wifi/wifi_trace_wrap.cmake)
endif()

#
#  ------------------------------------------------------------------
#  供給移行に伴う版差の吸収（C5 esp_wifi_v8.cmake の同一ブロックの転写．
#  evidence-c6-01 §4）
#  ------------------------------------------------------------------
#
#  esp-idf版 esp_wifi/src/wifi_init.c が `#include "esp_netif.h"` する
#  （呼出しは0件＝型宣言のためだけ）。
list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_SUP_DIR}/components/esp_netif/include
)
#  ★esp-idf版の esp_event.h／esp_private/wifi.h／esp_wifi_private.h は
#  `#include "freertos/FreeRTOS.h"`／`task.h`／`queue.h`／`semphr.h` する。
#  hal版はOS非依存の `platform/os.h` に置換されている（＝esp-hal-3rdparty
#  がNuttX向けにFreeRTOS依存を剥がしているための差．実測：
#  hal esp_event.h:12 `platform/os.h` ／ esp-idf esp_event.h:12-15 freertos×4）。
#  ASP3はFreeRTOSを«使わない»ので，既存のBTコントローラ用FreeRTOS互換
#  スタブ（C3の bt/stub/include，実体はesp_shimへ委譲）を再利用する
#  ＝C5と同一・チップ非依存。
#  APPEND（＝後ろ）にするのは，同ディレクトリに同居する
#  bt_nimble_config.h／esp_partition.hで意図せずシャドウしないため
#  （WiFi単体ビルドではこの2つはどこからもincludeされない）。
list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_COMMON_DIR}/bt/stub/include
)

#
#  ★v5.5.4統一（docs/blob-unify-v554.md）：WiFi/PHY/coexist blobを
#  hal（esp-hal-3rdparty submodule，NuttX同期のv8）から実ESP-IDF
#  v5.5.4（`~/tools/esp-idf`，同じos_adapter v8）へ切替える。
#  旧`ASP3_WIFI_BLOB_IDF`診断枝（追記19．native(IDF v6.1)との受信可否
#  比較用の一時計装で，未検証のまま=undefined運用だった．
#  docs/blob-inventory.md §7参照）はこの統一施策に伴い廃止し，
#  C3/C5と同じ扱いへ一本化した。★2026-07-21：その後 hal 側も撤去され、
#  当時導入した `ASP3_WIFI_BLOB_HAL` トグルも削除済み＝常に v5.5.4。
#
#  ★★訂正（2026-07-17・実測．evidence-c6-01 §3）：
#  従来ここは「C6はSOC_WIFI_HE_SUPPORT=1なのでv5.5.4のwifi_os_adapter.hは
#  `_wifi_disable_ac_ax`を持つ」として idf_v554_override/ のヘッダを
#  シャドウしていたが，これは **誤り**であり有害だった。
#
#  実測：**真のv5.5.4タグ（submodule esp-idf＝735507283d）の
#  wifi_os_adapter.h は `_wifi_disable_ac_ax` を持たない**（hal のヘッダと
#  バイト同一．md5 6eaa5ad）。当該フィールドを持つのは
#  `release/v5.5` 先端（+1169）の版だけである。
#  ＝override が指す「v5.5.4」は実際には外部tree `~/tools/esp-idf` の
#  ことであり（本PCでは v5.5.0，元PCでは +1169），**タグではなかった**。
#
#  ∴ 真のv5.5.4タグ blob に対して override を被せると構造体が4バイト
#  伸び，blobが読む `_magic` のオフセットが 484→488 へずれて
#  `esp_wifi_internal_osi_funcs_md5_check` が失敗する
#  （C5 memory `c5-wifi-osi-abi-he-field` と同一の罠）。
#  ⇒ **override は撤去した**（ディレクトリごと削除）。ヘッダを供給元
#  （esp-idf submodule）から素直に取れば blob と自動的に整合する
#  ＝「ヘッダとソースを揃えて移せば版差問題は消滅する」の一例。
#
#  osi ABI差：release/v5.5先端(+1169)のwifi_os_adapter.hのみ`_wifi_disable_ac_ax`
#  を持ち`_magic`が484→488へずれる。v5.5.4タグ／halは持たない（既定OFF）。
#  `-DIDF_V554=~/tools/esp-idf`（+1169）へ差し戻すA/Bのときだけ ON にする。
option(ASP3_WIFI_OSI_HAS_DISABLE_AC_AX "wifi_osi_funcs_t has _wifi_disable_ac_ax (ONLY release/v5.5 HEAD +1169 osi ABI; _magic moves 484->488). Default OFF = v5.5.4 tag / hal ABI" OFF)
if(NOT DEFINED IDF_V554)
    #  ★target.cmake が既に submodule 相対で定義済み（外部絶対パス撤去）。
    #  ここは esp_wifi.cmake を単体 include した場合のための防御的既定。
    get_filename_component(IDF_V554 ${CMAKE_CURRENT_LIST_DIR}/../../esp-idf ABSOLUTE)
endif()
set(ASP3_WIFI_BLOB_SRC ${IDF_V554})
list(APPEND ASP3_COMPILE_DEFS ASP3_WIFI_BLOB_V554=1)
#  ★wifi_os_adapter.hのシャドウは行わない（上の§訂正を参照）。
#  v5.5.4タグblobが読む_magicオフセット484は，halのヘッダ
#  （＝v5.5.4タグとバイト同一，md5 6eaa5ad）そのままで一致する。
if(ASP3_WIFI_OSI_HAS_DISABLE_AC_AX)
    list(APPEND ASP3_COMPILE_DEFS ASP3_WIFI_OSI_HAS_DISABLE_AC_AX=1)
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
#  ROM関数ld（差分5参照）．chip共通部（hal_esp32c6.cmakeの
#  _esp32c6_rom_ld_files）のうちBLE専用(*_bt_funcs.ld・eco3*.ld)を除いた
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
    #  C6固有：net80211/pp/phy/systimer/coexistのROM常駐部分の
    #  シンボル解決に必要（実機リンクで未定義参照として発覚．
    #  C3のWi-Fi ROM ld一覧には無いファイル＝C6のROMはこれらの
    #  関数テーブル・較正データ等をより多くROM側に持つ）
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.net80211.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.pp.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.phy.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.systimer.ld
    ${ESP_ROM_LD_DIR}/${WIFI_CHIP_SERIES}.rom.coexist.ld
)
foreach(_esp_wifi_rom_ld ${ESP_WIFI_ROM_LD_FILES})
    list(APPEND ASP3_LINK_OPTIONS -Wl,-T,${_esp_wifi_rom_ld})
endforeach()

#
#  ------------------------------------------------------------------
#  3. ESP-IDFのmbedTLS（Wireless.mk 96-185行目）
#  ------------------------------------------------------------------
#
#  ★供給元による**構造差**（実測．evidence-c5-02＝C5で確立．C6も同一）：
#    hal      = mbedtls **4.0.0**（tf-psa-crypto分離＝暗号本体が
#               tf-psa-crypto/{core,drivers/builtin/src}/ へ移動．
#               `library/` にはTLS層のみ33本．**library/common.h は無い**）
#    esp-idf  = mbedtls **3.6.5**（classic＝`library/` に暗号もTLSも一括108本．
#               tf-psa-crypto ディレクトリ自体が存在しない）
#  ＝v5.5.4への統一は mbedtls の**版ダウン**（4.0.0→3.6.5）を伴う。
#  configはESP-IDF本来の port（`mbedtls/esp_config.h`）へ寄せ，
#  halのNuttXシムconfig（`hal/nuttx/include/mbedtls`）は捨てる
#  （esp-idfに存在しない＝S3(LX6)段階3・C5が確立した型と同一）。
#
set(MBEDTLS_DIR ${ESP_SUP_DIR}/components/mbedtls/mbedtls)

list(APPEND ASP3_INCLUDE_DIRS
    ${ESP_SUP_DIR}/components/mbedtls/port/include
    ${ESP_SUP_DIR}/components/mbedtls/port/include/aes
    ${ESP_SUP_DIR}/components/esp_rom/include
    ${ESP_SUP_DIR}/components/esp_system/include
    ${ESP_SUP_DIR}/components/esp_rom/esp32c6/include
    ${MBEDTLS_DIR}/include
)

#
#  mbedtls 3.6.5（classic）．ESP-IDF本家の mbedtls コンポーネントは
#  `port/include` `mbedtls/include` `mbedtls/library` の3つをPUBLICな
#  インクルードとして公開し（esp-idf/components/mbedtls/CMakeLists.txt:30），
#  wpa_supplicant は PRIV_REQUIRES mbedtls でそれを受ける（同 :250）。
#  ＝`library/` を検索パスに置くのは**本家と同じ**構成。
#
#  ★shadow注意（**版ダウン固有の新規リスク**）：3.6.5の
#  `library/common.h` は wpaの `src/utils/common.h` と**同名**。
#  mbedtls 4.0.0 には `library/common.h` が存在しないため hal 構成では
#  この衝突自体が起き得なかった＝移行で初めて生じる。
#  正しい解決先は**wpa側**（本家 esp-idf の include 順と同じ）。
#  ⇒ `library` は §4（wpa）の**後ろ**に置く（下の §4 末尾を参照）。
#
list(APPEND ASP3_COMPILE_DEFS
    MBEDTLS_CONFIG_FILE=<mbedtls/esp_config.h>
)


#
#  ------------------------------------------------------------------
#  3-idf. mbedtls 3.6.5（classic）のソース一覧
#  ------------------------------------------------------------------
#
#  hal(4.0.0)の一覧（下の else 節＝tf-psa builtin 44 + core 8 + port 6）を
#  3.6.5の `library/` 一括レイアウトへ**1:1で写像**したもの（C5 で確立した
#  写像の転写．実測による差分）：
#    - `pk_rsa.c`               … 4.0.0のみ（3.6.5では pk_wrap.c に内包）＝除外
#    - `tf_psa_crypto_config.c` … 4.0.0のみ（tf-psa分離に伴う新設）＝除外
#    - `tf_psa_crypto_version.c`… 4.0.0のみ ⇒ 3.6.5の `version.c` で代替
#      （sdkconfig_stub の CONFIG_MBEDTLS_VERSION_C=1 が要求）
#    - `pk_ecc.c` は3.6.5にも実在＝そのまま採用
#  3.6.5固有に**追加**が要るもの：`psa_crypto_aead.c`／`entropy_poll.c`／
#  `bignum_mod.c`／`bignum_mod_raw.c`。
#
#  **TLS/x509 は積まない**（hal構成と同じ機能スコープ＝暗号プリミティブのみ）。
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
#     （NuttXのhal_esp32c6.cmake＝チップ共通部で常時コンパイルされる
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
    #  Direct Bootではesp_perip_clk_init()（esp_system/port/soc/esp32c6/
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
    ${ESP_SUP_DIR}/components/hal/esp32c6/modem_clock_hal.c
    #  rtc_clk_xtal_freq_get()（modem_clock.c経由で参照。--gc-sectionsに
    #  より実際に呼ばれる関数のみリンクされるため，同ファイル内の
    #  他のPLL較正関数群は取り込まれない想定＝安全に全体を採用）
    ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c6/rtc_clk.c
    #  modem_clock_select_lp_clock_source()がefuse_hal_chip_revision()
    #  を参照する（eco0判定）。efuse_hal.cはefuse_ll.h（static inline
    #  レジスタ直読み）のみに依存する軽量な実ソースのため採用。
    #  チップ非依存の共通実装（efuse_hal_chip_revision本体）＋C6固有の
    #  efuse_hal_get_{major,minor}_chip_version（ともに--gc-sectionsで
    #  実際に呼ばれる関数のみ残る）の2ファイル構成。
    ${ESP_SUP_DIR}/components/hal/efuse_hal.c
    ${ESP_SUP_DIR}/components/hal/esp32c6/efuse_hal.c
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

endif() # ESP32C6_WIFI

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

#
#  ★evidence-c6-04：ESP32C6_COLD_CPU_PLL（cold_clk_init_c6.c）が
#  rtc_clk_cpu_freq_set_config() を «実際に呼ぶ» ようになったため，
#  従来 --gc-sections で落ちていた rtc_clk.c の PLL 較正経路
#  （＝上の「他のPLL較正関数群は取り込まれない想定」という前提）が
#  リンクされるようになり，regi2c_ctrl_write_reg / _mask が未解決になる（実測）。
#
#  esp_bt_idf61.cmake と同じ解法：rtc_clk.c 内の REGI2C_* を «ROM 直呼び»
#  （esp_rom_regi2c_*，ロック無し）へ解決させる（NON_OS_BUILD）。
#  cold_clk_init_c6.c は software_init_hook（単一スレッド・カーネル起動前・
#  割込み前）から呼ぶので regi2c_ctrl.c のクリティカルセクション
#  （esp_os_*/saradc/tsens 依存）は不要＝リンク肥大も避けられる。
#
if(ESP32C6_COLD_CPU_PLL)
    set_source_files_properties(
        ${ESP_SUP_DIR}/components/esp_hw_support/port/esp32c6/rtc_clk.c
        PROPERTIES COMPILE_DEFINITIONS "NON_OS_BUILD=1"
    )
    #  NON_OS_BUILD の最下層プロバイダ＝esp_rom_regi2c_read/write_mask を
    #  提供する ROM パッチ（esp_bt_idf61.cmake と同一の1本）。
    list(APPEND ASP3_SYSSVC_TARGET_C_FILES
        ${ESP_SUP_DIR}/components/esp_rom/patches/esp_rom_hp_regi2c_esp32c6.c
    )
endif()
