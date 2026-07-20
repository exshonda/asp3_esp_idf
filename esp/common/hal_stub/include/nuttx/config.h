/*
 *  esp-hal同梱のNuttX用sdkconfig.hスタブが要求するnuttx/config.hの
 *  ASP3用スタブ．NuttXのKconfig値のうち，sdkconfig.hが選択を必須と
 *  するもの（#error回避）だけを定義する．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_NUTTX_CONFIG_H
#define TOPPERS_HAL_STUB_NUTTX_CONFIG_H

/*  SPIフラッシュクロック（Direct Bootのボード既定＝40MHz）  */
#define CONFIG_ESPRESSIF_FLASH_FREQ_40M    1

/*
 *  ログレベル（sdkconfig.hのCONFIG_LOG_DEFAULT_LEVEL等が参照）．
 *  ESP_LOG_INFO(=3)相当．esp_log_level.hのESP_LOG_*列挙値と対応。
 *
 *  Phase B-2b（WPA2 4-wayハンドシェイク切り分け）の調査時は一時的に
 *  4（DEBUG）＋下記CONFIG_WPA_DEBUG_PRINTを1にして
 *  wpa_printf(MSG_DEBUG,...)を可視化した（LOG_LOCAL_LEVEL＝
 *  CONFIG_LOG_MAXIMUM_LEVEL＝これでゲートされるため）．
 *  原因はesp_shim_random()のRNG_DATA_REGアドレス誤り（SNonce常時
 *  ゼロ→AP側がnonce再利用とみなしmsg1再送）と判明・修正済み
 *  （wifi/esp_shim.c）のため既定値3に戻す．RAM使用率も4では88%超と
 *  高かった（3では基準値相当に戻る）．
 */
#define CONFIG_ESPRESSIF_LOG_LEVEL 3

/*
 *  Wi-Fi機能有効化（ESP32C3_WIFIオプションON時のビルド専用）．
 *  sdkconfig.hのCONFIG_ESPRESSIF_WIRELESS/CONFIG_ESPRESSIF_WIFI配下の
 *  CONFIG_ESP_WIFI_*マクロ群（受信/送信バッファ数等）を有効化する
 *  ゲート．個々のバッファサイズ等はESP-IDFのKconfig既定値に倣う
 *  （下記，各CONFIG_ESPRESSIF_WIFI_*で個別にコメント）。
 */
#define CONFIG_ESPRESSIF_WIRELESS  1
#define CONFIG_ESPRESSIF_WIFI      1

/*
 *  CONFIG_ESPRESSIF_WIFI_* ：sdkconfig.hがCONFIG_ESP_WIFI_*へ
 *  そのまま転写するバッファ/フィーチャ値．ESP-IDF公式Kconfigの
 *  esp32c3向け既定値（sdkconfig.defaults相当）をそのまま採用する．
 */
#define CONFIG_ESPRESSIF_WIFI_STATIC_RX_BUFFER_NUM         10
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_RX_BUFFER_NUM        32
#define CONFIG_ESPRESSIF_WIFI_STATIC_TX_BUFFER_NUM         0
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_TX_BUFFER_NUM        32
/*  CONFIG_ESP_WIFI_TX_BUFFER_TYPE=1（動的）に対応する値  */
#define CONFIG_ESPRESSIF_WIFI_TX_BUFFER_TYPE               1
/*  CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF：既定は無効(0)＝静的管理バッファ */
#define CONFIG_ESPRESSIF_WIFI_DYNAMIC_RX_MGMT_BUFFER_TYPE  0
#define CONFIG_ESPRESSIF_WIFI_RX_MGMT_BUF_NUM_DEF          5
/*  A-MPDU送受信は既定で有効，BA winは6（ESP-IDF既定値）  */
#define CONFIG_ESPRESSIF_WIFI_AMPDU_TX_ENABLED             1
#define CONFIG_ESPRESSIF_WIFI_TX_BA_WIN                    6
#define CONFIG_ESPRESSIF_WIFI_AMPDU_RX_ENABLED             1
#define CONFIG_ESPRESSIF_WIFI_RX_BA_WIN                    6
/*
 *  WPA3拡張：本ビルド（esp_wifi.cmake差分3）はSAE本体のみ有効化し，
 *  SAE_PK／SAE_H2E／SoftAP SAE／OWE STAは未対応（0固定）とする方針
 *  に合わせる．
 */
#define CONFIG_ESPRESSIF_WIFI_ENABLE_WPA3_SAE              1
#define CONFIG_ESPRESSIF_WIFI_ENABLE_SAE_PK                0
#define CONFIG_ESPRESSIF_WIFI_ENABLE_SAE_H2E                0
#define CONFIG_ESPRESSIF_WIFI_SOFTAP_SAE_SUPPORT           0
#define CONFIG_ESPRESSIF_WIFI_ENABLE_WPA3_OWE_STA          0
/*
 *  実施72（docs/wifi-shim-c6.md）：この値が0だと`wifi_init_config_t.
 *  sta_disconnected_pm`がfalseになり，blob内部の`ic_init()`が
 *  `pm_enable_sta_disconnected_power_management(1)`を呼ばず，
 *  `g_pm`のPM状態バイト（オフセット289）が初期値0のまま遷移せず，
 *  `pm_disconnected_wake()`が常に早期returnし，`esp_phy_enable()`が
 *  起動時の1回しか呼ばれず（native実測は1scan中に4回），PHY
 *  wakeup経路（`phy_wakeup_init`／`fe_txrx_reset`のFEリセット
 *  パルス）が一度も実行されない——というdeaf-RXの新規かつ完全に
 *  追跡された原因連鎖の起点であることが判明した．有効化（1）に
 *  切替え，PHY wakeupサイクルが回復しRXが復活するか実機検証する．
 */
#define CONFIG_ESPRESSIF_WIFI_STA_DISCONNECT_PM            1
/*  WAPI（CONFIG_WPA_WAPI_PSK）：esp_wifi.cmake差分2の通りOFF固定  */
#define CONFIG_WPA_WAPI_PSK                                0

#endif /* TOPPERS_HAL_STUB_NUTTX_CONFIG_H */
