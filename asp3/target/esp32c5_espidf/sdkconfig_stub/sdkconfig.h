/*
 *  sdkconfig.h スタブ（ESP32-C5用．ASP3側で用意）
 *
 *  esp-hal-3rdparty（hal submodule）はNuttX統合向けの
 *  nuttx/esp32c5/include/sdkconfig.h を提供していない（本移植時点で
 *  hal submoduleにESP32-C5用のNuttX board支援ファイルが同梱されて
 *  いないため．C6にはhal/nuttx/esp32c6/include/sdkconfig.hが存在する
 *  が，同ファイルは`#include <nuttx/config.h>`を含みASP3（NuttX非
 *  依存）ではそのままでは使えない）。
 *
 *  CLAUDE.mdの禁則によりhal/（submodule）を直接編集できないため，本
 *  スタブをASP3側（asp3/target/esp32c5_espidf/sdkconfig_stub/）に
 *  用意し，ASP3_INCLUDE_DIRSでこちらを優先解決させる（target.cmake
 *  参照）。
 *
 *  B-0/B-1スコープ（UART0コンソール・SYSTIMER HRT・test_porting）で
 *  実際に#include "sdkconfig.h"されるヘッダは無い見込みだが（C6の
 *  ビルドでも同様にnuttx/esp32c6/include自体はinclude dirsに積まれる
 *  だけで実際には参照されていない），保険的にインクルードパス解決の
 *  受け皿として置く。
 *
 *  【フェーズ2b追記】Wi-Fi統合（docs/c5-port-design.md §5.4・§6）で
 *  esp_wifi.h（WIFI_INIT_CONFIG_DEFAULT()マクロ）が直接参照する
 *  CONFIG_ESP_WIFI_*マクロ群を以下に追加した。C6/C3は
 *  hal/nuttx/esp32c6/include/sdkconfig.h（`#include <nuttx/config.h>`
 *  経由でasp3/target/esp32c3_espidf/hal_stub/include/nuttx/config.h＝
 *  ASP3側スタブが供給するCONFIG_ESPRESSIF_WIFI_*へ転写する2段構成）を
 *  使っているが，C5にはそもそも転写元のsdkconfig.h自体が存在しないため
 *  （上記コメント参照），本ファイルで直接最終値を定義する（同じ
 *  ESP-IDF Kconfig既定値をasp3/target/esp32c3_espidf/hal_stub/include/
 *  nuttx/config.hから転記）。値そのものはチップ非依存のWi-Fi
 *  バッファ本数等のチューニング既定値であり，C5固有の変更は不要
 *  （実機での動作確認は必要．未検証の場合は【実機確認待ち】）。
 */

#pragma once

/*
 *  Wi-Fi（wifi_init_config_t既定値．esp_wifi.hのWIFI_INIT_CONFIG_DEFAULT
 *  マクロが直接参照する分＋関連する#if参照分．値はESP-IDF公式Kconfig
 *  既定値をasp3/target/esp32c3_espidf/hal_stub/include/nuttx/config.h
 *  から転記）。
 */
#define CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM         10
#define CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM        32
#define CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM         0
#define CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM        32
#define CONFIG_ESP_WIFI_TX_BUFFER_TYPE               1  /* 1=dynamic */
#define CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM          0
#define CONFIG_ESP_WIFI_DYNAMIC_RX_MGMT_BUF          0  /* 0=static管理バッファ */
#define CONFIG_ESP_WIFI_RX_MGMT_BUF_NUM_DEF          5
#define CONFIG_ESP_WIFI_CSI_ENABLED                  0
#define CONFIG_ESP_WIFI_AMPDU_TX_ENABLED             1
#define CONFIG_ESP_WIFI_TX_BA_WIN                    6
#define CONFIG_ESP_WIFI_AMPDU_RX_ENABLED             1
#define CONFIG_ESP_WIFI_RX_BA_WIN                    6
#define CONFIG_ESP_WIFI_AMSDU_TX_ENABLED             0
#define CONFIG_ESP_WIFI_NVS_ENABLED                  0
#define CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_1        0  /* シングルコア */
#define CONFIG_ESP_WIFI_SOFTAP_BEACON_MAX_LEN        752
#define CONFIG_ESP_WIFI_MGMT_SBUF_NUM                32
/*
 *  C6実機調査（docs/wifi-shim-c6.md「実施72」）で，この値が0だと
 *  blob内部のPMステートマシンがesp_phy_enable()の周期再呼出し
 *  （phy_wakeup_init／fe_txrx_resetのFEリセットパルス）に到達せず
 *  deaf-RXの原因連鎖の起点になり得ると判明した．C5でも同じ理由で
 *  1（有効）を採用する。
 */
#define CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE   1
#define CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM       7
#define CONFIG_ESP_WIFI_TX_HETB_QUEUE_NUM            1
#define CONFIG_ESP_WIFI_ENABLE_DUMP_HESIGB            0
/*
 *  WPA3拡張：esp_wifi.cmake差分3の方針（NuttX Kconfig既定＝未設定
 *  =OFF）に合わせ，SAE本体のみ有効化しSAE_PK／SAE_H2E／SoftAP SAE／
 *  OWE STAは無効（0）とする。
 */
#define CONFIG_ESP_WIFI_ENABLE_WPA3_SAE               1
#define CONFIG_ESP_WIFI_ENABLE_SAE_PK                 0
#define CONFIG_ESP_WIFI_ENABLE_SAE_H2E                0
#define CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT            0
#define CONFIG_ESP_WIFI_ENABLE_WPA3_OWE_STA           0
#define CONFIG_NEWLIB_NANO_FORMAT                     0

/*
 *  ログレベル（esp_log_level.hが直接参照．C3/C6のhal_stub/nuttx/
 *  config.hと同じくESP_LOG_INFO(=3)相当）。
 */
#define CONFIG_LOG_DEFAULT_LEVEL   3
#define CONFIG_LOG_MAXIMUM_LEVEL   3
#define CONFIG_LOG_VERSION_1       1
#define CONFIG_LOG_VERSION         1

/*
 *  Wi-Fi省電力（modem sleep）タイミング既定値．esp_wifi/src/wifi_init.c
 *  が直接参照（ESP-IDF公式Kconfig既定値をC6と同一値で採用．チップ非
 *  依存のタイミングパラメータ）。
 */
#define CONFIG_ESP_WIFI_SLP_DEFAULT_MIN_ACTIVE_TIME            50
#define CONFIG_ESP_WIFI_SLP_DEFAULT_MAX_ACTIVE_TIME            10
#define CONFIG_ESP_WIFI_SLP_DEFAULT_WAIT_BROADCAST_DATA_TIME   15

/*
 *  CONFIG_ESP_WIFI_ENABLED：esp_hw_support/periph_ctrl.cの
 *  wifi_module_enable/disable()（wifi_clock_enable_wrapper/
 *  wifi_clock_disable_wrapperが呼ぶ．esp_wifi_adapter.c参照）が
 *  `#if CONFIG_ESP_WIFI_ENABLED`でガードされているため必須。
 */
#define CONFIG_ESP_WIFI_ENABLED   1

/*
 *  CONFIG_LOG_TIMESTAMP_SOURCE_RTOS：log/include/esp_log.hのESP_LOG_LEVEL
 *  マクロ本体がこのいずれか（RTOS/SYSTEM/NON_OS_BUILD）の選択に依存する。
 *  未定義のままだと全分岐が#if 0になりESP_LOG_LEVELがマクロとして展開
 *  されず，esp_log_timestamp()（esp_shim_libc.c等が実装するRTOS時刻源）
 *  を使うRTOS版をC6と同じく採用する。
 */
#define CONFIG_LOG_TIMESTAMP_SOURCE_RTOS   1

/*
 *  RTC較正サイクル数：esp_hw_support/port/esp_clk_tree_common.c
 *  （esp_clk_tree.c経由でC5固有のクロックツリー管理コードから到達．
 *  C6には無い依存．esp_wifi.cmake参照）が直接参照する。ESP-IDF公式
 *  Kconfig既定値1024（チップ非依存の較正精度パラメータ）。
 */
#define CONFIG_RTC_CLK_CAL_CYCLES   1024

/*
 *  CONFIG_IDF_TARGET：esp_hw_support/include/esp_private/periph_ctrl.h
 *  が`__PERIPH_CTRL_ALLOW_LEGACY_API`未定義チップ（C5はC3/C2/C6/H2の
 *  リストに含まれず該当）で`__attribute__((deprecated("...on "
 *  CONFIG_IDF_TARGET)))`の文字列連結に直接使う．ESP-IDF公式Kconfigの
 *  値（クォート済み文字列．例："esp32c6"）に倣う。
 */
#define CONFIG_IDF_TARGET   "esp32c5"

/*
 *  PHY最大送信電力（esp_phy/esp32c5/phy_init_data.cが直接参照．
 *  ESP-IDF公式Kconfig既定値20（dBm換算前の内部単位）をC3/C6と同様に
 *  採用）。
 */
#define CONFIG_ESP_PHY_MAX_TX_POWER   20

/*
 *  mbedtls機能有効化（psa/crypto_config.h・mbedtls/esp_config.hが直接
 *  参照．TLSクライアント/サーバ・X.509・PK・ECP各種曲線・ハッシュ等の
 *  Kconfig既定値一式）。値はhal/nuttx/esp32c6/include/sdkconfig.hの
 *  CONFIG_MBEDTLS_*定義から転記した（この節はTLS/暗号プリミティブの
 *  機能有効化フラグでありチップのレジスタ値を含まないチップ非依存の
 *  設定＝C5固有の変更は不要。実際にmbedtls/port/esp_hardware.c等，
 *  同じソース一式をC5向けインクルードパスでコンパイルする＝esp_wifi.cmake
 *  参照）。定義が漏れるとpsa_crypto.cのcheck_crypto_config.hが
 *  「PSA_WANT_ALG_TLS12_ECJPAKE_TO_PMS defined, but not all
 *  prerequisites」等の依存関係エラーで停止するため一括で移植する。
 */
#define CONFIG_MBEDTLS_VER_4_X_SUPPORT 1
#define CONFIG_MBEDTLS_COMPILER_OPTIMIZATION_NONE 1
#define CONFIG_MBEDTLS_FS_IO 1
#define CONFIG_MBEDTLS_ERROR_STRINGS 1
#define CONFIG_MBEDTLS_VERSION_C 1
#define CONFIG_MBEDTLS_HAVE_TIME 1
#define CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN 1
#define CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN 16384
#define CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN 4096
#define CONFIG_MBEDTLS_SELF_TEST 1
#define CONFIG_MBEDTLS_X509_USE_C 1
#define CONFIG_MBEDTLS_PEM_PARSE_C 1
#define CONFIG_MBEDTLS_PEM_WRITE_C 1
#define CONFIG_MBEDTLS_PK_C 1
#define CONFIG_MBEDTLS_PK_PARSE_C 1
#define CONFIG_MBEDTLS_PK_WRITE_C 1
#define CONFIG_MBEDTLS_X509_CRL_PARSE_C 1
#define CONFIG_MBEDTLS_X509_CRT_PARSE_C 1
#define CONFIG_MBEDTLS_X509_CSR_PARSE_C 1
#define CONFIG_MBEDTLS_X509_RSASSA_PSS_SUPPORT 1
#define CONFIG_MBEDTLS_ASN1_PARSE_C 1
#define CONFIG_MBEDTLS_ASN1_WRITE_C 1
#define CONFIG_MBEDTLS_TLS_ENABLED 1
#define CONFIG_MBEDTLS_SSL_PROTO_TLS1_2 1
#define CONFIG_MBEDTLS_TLS_SERVER 1
#define CONFIG_MBEDTLS_TLS_CLIENT 1
#define CONFIG_MBEDTLS_TLS_SERVER_AND_CLIENT 1
#define CONFIG_MBEDTLS_SSL_CACHE_C 1
#define CONFIG_MBEDTLS_SSL_ALL_ALERT_MESSAGES 1
#define CONFIG_MBEDTLS_KEY_EXCHANGE_RSA 1
#define CONFIG_MBEDTLS_KEY_EXCHANGE_ELLIPTIC_CURVE 1
#define CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_RSA 1
#define CONFIG_MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA 1
#define CONFIG_MBEDTLS_SSL_SERVER_NAME_INDICATION 1
#define CONFIG_MBEDTLS_SSL_ALPN 1
#define CONFIG_MBEDTLS_SSL_MAX_FRAGMENT_LENGTH 1
#define CONFIG_MBEDTLS_SSL_RENEGOTIATION 1
#define CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS 1
#define CONFIG_MBEDTLS_SERVER_SSL_SESSION_TICKETS 1
#define CONFIG_MBEDTLS_AES_C 1
#define CONFIG_MBEDTLS_CCM_C 1
#define CONFIG_MBEDTLS_CIPHER_MODE_CBC 1
#define CONFIG_MBEDTLS_CIPHER_MODE_CFB 1
#define CONFIG_MBEDTLS_CIPHER_MODE_CTR 1
#define CONFIG_MBEDTLS_CIPHER_MODE_OFB 1
#define CONFIG_MBEDTLS_CIPHER_MODE_XTS 1
#define CONFIG_MBEDTLS_GCM_C 1
#define CONFIG_MBEDTLS_AES_ROM_TABLES 1
#define CONFIG_MBEDTLS_CMAC_C 1
#define CONFIG_MBEDTLS_BIGNUM_C 1
#define CONFIG_MBEDTLS_RSA_C 1
#define CONFIG_MBEDTLS_ECP_C 1
#define CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_SECP384R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_SECP521R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_SECP256K1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_BP256R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_BP384R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_BP512R1_ENABLED 1
#define CONFIG_MBEDTLS_ECP_DP_CURVE25519_ENABLED 1
#define CONFIG_MBEDTLS_ECP_NIST_OPTIM 1
#define CONFIG_MBEDTLS_ECDH_C 1
#define CONFIG_MBEDTLS_ECDSA_C 1
#define CONFIG_MBEDTLS_PK_PARSE_EC_EXTENDED 1
#define CONFIG_MBEDTLS_PK_PARSE_EC_COMPRESSED 1
#define CONFIG_MBEDTLS_ECDSA_DETERMINISTIC 1
#define CONFIG_MBEDTLS_MD_C 1
#define CONFIG_MBEDTLS_MD5_C 1
#define CONFIG_MBEDTLS_SHA1_C 1
#define CONFIG_MBEDTLS_SHA256_C 1
#define CONFIG_MBEDTLS_SHA384_C 1
#define CONFIG_MBEDTLS_SHA512_C 1
#define CONFIG_MBEDTLS_ROM_MD5 1
#define CONFIG_MBEDTLS_ECC_OTHER_CURVES_SOFT_FALLBACK 1
#define CONFIG_MBEDTLS_GCM_SUPPORT_NON_AES_CIPHER 1
#define CONFIG_MBEDTLS_AES_USE_INTERRUPT 1
#define CONFIG_MBEDTLS_AES_INTERRUPT_LEVEL 0
#define CONFIG_MBEDTLS_AES_HW_SMALL_DATA_LEN_OPTIM 1
#define CONFIG_MBEDTLS_CTR_DRBG_C 1
#define CONFIG_MBEDTLS_HMAC_DRBG_C 1
#define CONFIG_MBEDTLS_BASE64_C 1
#define CONFIG_MBEDTLS_PKCS5_C 1
#define CONFIG_MBEDTLS_PKCS7_C 1
#define CONFIG_MBEDTLS_PKCS1_V15 1
#define CONFIG_MBEDTLS_PKCS1_V21 1
