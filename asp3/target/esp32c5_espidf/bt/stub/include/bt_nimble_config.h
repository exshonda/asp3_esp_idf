/*
 *  NimBLE（BLE実施05／Phase D-2a）用の CONFIG_BT_NIMBLE_* 補完ヘッダ
 *  （force-include．esp32c5_espidf専用——C3版を流用しない）
 *
 *  C5はCONFIG_BT_LE_*（コントローラ設定）を esp_bt.cmake の -D と
 *  sdkconfig_stub/sdkconfig.h で供給するが，NimBLEホスト設定
 *  （CONFIG_BT_NIMBLE_*．esp_nimble_cfg.hがMYNEWT_VAL_*へ写像する側）は
 *  どちらにも含まれない．必要なCONFIG_BT_NIMBLE_*を本ヘッダで直接定義し，
 *  esp_bt.cmakeが syscfg.h の前後にforce-includeする．
 *
 *  ★C3のbt/stub/include/bt_nimble_config.hをそのまま流用しないこと
 *  （BLE実施02のC6と同じ理由＝重要な相違点）：
 *  C3（旧世代コントローラ．SOC_ESP_NIMBLE_CONTROLLER非該当）は
 *  esp_nimble_hci.c＋hci_esp_ipc_legacy.c（LEGACY VHCI経由）を使うため
 *  CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE=1 が正しい（C3版はこの値）．
 *  C5（新世代．SOC_ESP_NIMBLE_CONTROLLER=1）はesp_nimble_init()内部で
 *  esp_nimble_hci_init()の呼出し自体が`#if !SOC_ESP_NIMBLE_CONTROLLER ||
 *  !CONFIG_BT_CONTROLLER_ENABLED`でコンパイルアウトされるため，LEGACY
 *  VHCI経路は存在しない（IDF v6.1
 *  components/bt/host/nimble/nimble/porting/nimble/src/nimble_port.c参照）．
 *  実際のトランスポートは hci_transport.c＋hci_driver_nimble.c＋
 *  nimble/transport/esp_ipc/src/hci_esp_ipc.c である
 *  （esp_bt.cmakeのESP32C5_BT_NIMBLEブロックが追加）．
 *  CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLEは単なるビルド選択フラグでは
 *  なくble_hs_hci.c／ble_hs_mbuf.cのmbufヘッダ余白計算を分岐する
 *  （＝実際のトランスポートと不整合だと実行時にバッファ破壊/
 *  オフセットずれを起こしうる）ため，0にすることが必須．
 *  ★C5のインクルードパスはC3のbt/stub/include（LEGACY_VHCI=1の版を
 *  同梱）を含むため，本ファイルを含むディレクトリをC3より前に PREPEND
 *  すること（esp_bt.cmakeで実施）——順序を誤るとC3版が先に見つかり
 *  LEGACY_VHCI=1になる（サイレントな実行時バッファバグ）．
 *
 *  CONFIG_BT_ENABLED／CONFIG_BT_CONTROLLER_ENABLEDはesp_bt.cmakeの-Dで
 *  既に定義済みのため本ヘッダでは重複定義しない．
 */
#ifndef TOPPERS_BT_NIMBLE_CONFIG_H
#define TOPPERS_BT_NIMBLE_CONFIG_H

/*
 *  npl_os_freertos.cはESP_IDF_VERSION/ESP_IDF_VERSION_VALを#ifで使うが
 *  同ファイルはesp_idf_version.hをincludeしない（ESP-IDFでは暗黙include
 *  前提）．esp_bt.cmakeが既に-includeしているが，本ヘッダ単独で読まれる
 *  場合の保険として重複includeしておく（インクルードガードで無害）．
 */
#include <esp_idf_version.h>

#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL 1
#define CONFIG_BT_NIMBLE_LOG_LEVEL_INFO 1
#define CONFIG_BT_NIMBLE_LOG_LEVEL 1
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 3
#define CONFIG_BT_NIMBLE_MAX_BONDS 3
#define CONFIG_BT_NIMBLE_MAX_CCCDS 8
#define CONFIG_BT_NIMBLE_L2CAP_COC_MAX_NUM 0
#define CONFIG_BT_NIMBLE_PINNED_TO_CORE 0
#define CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE 4096
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 1
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 1
#define CONFIG_BT_NIMBLE_ROLE_BROADCASTER 1
#define CONFIG_BT_NIMBLE_ROLE_OBSERVER 1
/*
 *  esp_nimble_cfg.hはCONFIG_BT_NIMBLE_GATT_SERVER/CLIENT未定義時
 *  MYNEWT_VAL_BLE_GATTS/GATTCを0にフォールバックするが，本ビルドは
 *  ble_gatts.c／ble_gatts_lcl.c／ble_svc_gatt.cを実際にリンクしており
 *  （esp_bt.cmake），MYNEWT_VAL(BLE_GATTS)==0のままだとble_gattc.c内の
 *  一部関数（ble_gattc_process_status呼出し側のみGATTS単独ガード）が
 *  定義と不整合になりビルド不能になる（C6のBLE実施02で判明）．GATTSを
 *  明示的に1にして自己整合させる．GATTCは未使用（本ビルドはPERIPHERAL
 *  ロールのみ）のため0のまま．
 */
#define CONFIG_BT_NIMBLE_GATT_SERVER 1
/*
 *  GATT_SERVER=1化に伴い，esp_nimble_cfg.hが#ifndefフォールバック無しで
 *  直接参照する残りのCONFIG_BT_NIMBLE_*一式（Kconfig.inの既定値相当）．
 */
#define CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES 64
#define CONFIG_BT_NIMBLE_L2CAP_ENHANCED_COC 0
#define CONFIG_BT_NIMBLE_DYNAMIC_SERVICE 0
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_ITVL 1000
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_THRESH 2
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_TX_ON_DISCONNECT 0
#define CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY 0
#define CONFIG_BT_NIMBLE_SM_LVL 0
#define CONFIG_BT_NIMBLE_SMP_ID_RESET 0
#define CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS 0
#define CONFIG_BT_NIMBLE_MAX_EADS 1
/*
 *  D-2a（sync到達）はセキュリティ不要．SM_LEGACY/SM_SC=0（esp_bt.cmakeの
 *  MYNEWT_VAL_BLE_SM_LEGACY/SC=0と対）でble_sm*.cをnear-empty化し
 *  mbedTLS/tinycryptのリンクを回避する（C3/C6のD-2aと同じ判断）．
 */
#define CONFIG_BT_NIMBLE_SECURITY_ENABLE 0
#define CONFIG_BT_NIMBLE_SM_LEGACY 0
#define CONFIG_BT_NIMBLE_SM_SC 0
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION 0
#define CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME "nimble"
#define CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN 31
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 256
#define CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE 0x0
/*
 *  msys（HCI ACL用共有mbufプール）はesp_bt.cmakeのCONFIG_BT_LE_MSYS_*
 *  （controller/porting/mem/os_msys_init.c，D-1から既存）が実体．
 *  以下はesp_nimble_cfg.hが別途参照するNimBLE側の対応値（同じ数字で
 *  揃える）．
 */
#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT 12
#define CONFIG_BT_NIMBLE_MSYS_1_BLOCK_SIZE 256
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT 24
#define CONFIG_BT_NIMBLE_MSYS_2_BLOCK_SIZE 320
#define CONFIG_BT_NIMBLE_ACL_BUF_COUNT 24
#define CONFIG_BT_NIMBLE_ACL_BUF_SIZE 255
#define CONFIG_BT_NIMBLE_HCI_EVT_BUF_SIZE 70
#define CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT 30
#define CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT 8
#define CONFIG_BT_NIMBLE_GATT_MAX_PROCS 4
#define CONFIG_BT_NIMBLE_RPA_TIMEOUT 900
#define CONFIG_BT_NIMBLE_HS_STOP_TIMEOUT_MS 2000
#define CONFIG_BT_NIMBLE_ENABLE_CONN_REATTEMPT 1
#define CONFIG_BT_NIMBLE_MAX_CONN_REATTEMPT 3
#define CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_2M_PHY 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_CODED_PHY 1
#define CONFIG_BT_NIMBLE_MAX_PERIODIC_SYNCS 0
#define CONFIG_BT_NIMBLE_WHITELIST_SIZE 12
#define CONFIG_BT_NIMBLE_USE_ESP_TIMER 1
/*  ★重要：C5は新HCIトランスポート経由（上のファイル冒頭コメント参照）  */
#define CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE 0
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT 30
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT 8
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE 255
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 70
#define CONFIG_BT_NIMBLE_L2CAP_COC_SDU_BUFF_COUNT 1
#define CONFIG_BT_NIMBLE_EATT_CHAN_NUM 0
/*
 *  transport.cのPOOL_ACL_COUNTは（LL=native／HS=nativeの本構成では）
 *  BLE_TRANSPORT_ACL_FROM_LL_COUNTに等しい．ble_transport_deinitが
 *  pool_aclを無条件参照するため0だと未定義参照でコンパイル不能．
 *  ACLバッファ数（CONFIG_BT_NIMBLE_ACL_BUF_COUNT=24）に合わせて24とする．
 */
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT 24

#endif /* TOPPERS_BT_NIMBLE_CONFIG_H */
