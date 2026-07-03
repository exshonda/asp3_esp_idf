/*
 *  esp-hal同梱のNuttX用sdkconfig.hスタブが要求するnuttx/config.hの
 *  ASP3用スタブ．NuttXのKconfig値のうち，sdkconfig.hが選択を必須と
 *  するもの（#error回避）だけを定義する．
 */
#ifndef TOPPERS_HAL_STUB_NUTTX_CONFIG_H
#define TOPPERS_HAL_STUB_NUTTX_CONFIG_H

/*  SPIフラッシュクロック（Direct Bootのボード既定＝40MHz）  */
#define CONFIG_ESPRESSIF_FLASH_FREQ_40M    1

/*
 *  ログレベル（sdkconfig.hのCONFIG_LOG_DEFAULT_LEVEL等が参照）．
 *  ESP_LOG_INFO(=3)相当．esp_log_level.hのESP_LOG_*列挙値と対応。
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
/*  省電力連動切断（Modem-sleep関連）：未使用のため無効  */
#define CONFIG_ESPRESSIF_WIFI_STA_DISCONNECT_PM            0
/*  WAPI（CONFIG_WPA_WAPI_PSK）：esp_wifi.cmake差分2の通りOFF固定  */
#define CONFIG_WPA_WAPI_PSK                                0

#endif /* TOPPERS_HAL_STUB_NUTTX_CONFIG_H */
