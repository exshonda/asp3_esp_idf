/*
 *  ★IDF v6.1 版（BLE実施14／esp_bt_idf61.cmake の ESP32C6_BT_IDF61_NIMBLE）
 *  専用．hal 版（bt/stub/include/bt_nimble_config.h）と #define 値は同一だが，
 *  hal 版ディレクトリには platform/os.h・npl_os_bridge.h・nimble/
 *  nimble_port_os.h（hal ドリフト吸収シム）が同居し，v6.1 nimble ソースの
 *  ヘッダを shadow して壊すため，本 stub_idf61/include は bt_nimble_config.h
 *  «のみ» を置く独立ディレクトリとし，C3 stub より前に PREPEND する
 *  （C5 esp_bt.cmake の bt/stub/include と同じ構成＝LEGACY_VHCI=0 を保証）．
 *
 *  NimBLE（Phase D-2a/D-2b）用の CONFIG_BT_NIMBLE_* 補完ヘッダ
 *  （force-include．esp32c6_espidf専用——C3版を流用しない）
 *
 *  hal/nuttx/esp32c6/include/sdkconfig.h の CONFIG_ESPRESSIF_BLE ブロック
 *  はコントローラ設定（CONFIG_BT_LE_*）のみを持ち，NimBLEホスト設定
 *  （CONFIG_BT_NIMBLE_*．esp_nimble_cfg.hがMYNEWT_VAL_*へ写像する側）は
 *  一切含まない．BLE実施01と同じ理由でCONFIG_ESPRESSIF_BLEは立てない
 *  （CONFIG_ESPRESSIF_WIFIと同時に立つとCONFIG_SW_COEXIST_ENABLE=1になり
 *  coex経路が有効化されD-1のcoex-OFF検証済み挙動が壊れるため）．
 *  必要なCONFIG_BT_NIMBLE_*を本ヘッダで直接定義し，esp_bt.cmakeが
 *  sdkconfig.h/syscfg.hの前後にforce-includeする．
 *
 *  ★C3のbt/stub/include/bt_nimble_config.hをそのまま流用しないこと
 *  （advisorレビュー指摘＝重要な相違点）：
 *  C3（旧世代コントローラ．SOC_ESP_NIMBLE_CONTROLLER非該当）は
 *  esp_nimble_hci.c＋hci_esp_ipc_legacy.c（LEGACY VHCI経由でD-1の
 *  esp_vhci_host_*へブリッジ）を使うため CONFIG_BT_NIMBLE_LEGACY_
 *  VHCI_ENABLE=1 が正しい．
 *  C6/C5（新世代．SOC_ESP_NIMBLE_CONTROLLER=1）はesp_nimble_init()内部で
 *  esp_nimble_hci_init()の呼出し自体が`#if !SOC_ESP_NIMBLE_CONTROLLER ||
 *  !CONFIG_BT_CONTROLLER_ENABLED`でコンパイルアウトされるため，LEGACY
 *  VHCI経路は存在しない（hal/components/bt/host/nimble/nimble/porting/
 *  nimble/src/nimble_port.c参照）．実際のトランスポートは
 *  hci_transport.c＋hci_driver_nimble.c＋
 *  host/nimble/nimble/nimble/transport/esp_ipc/src/hci_esp_ipc.cで
 *  ある（esp_bt.cmakeのESP32C6_BT_NIMBLEブロックが追加）．
 *  CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLEは単なるビルド選択フラグでは
 *  なくble_hs_hci.c／ble_hs_mbuf.cのmbufヘッダ余白計算を分岐する
 *  （＝実際のトランスポートと不整合だと実行時にバッファ破壊/
 *  オフセットずれを起こしうる）ため，0にすることが必須．
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
 *  定義と不整合になりビルド不能になる（実機ビルドで判明）．GATTSを
 *  明示的に1にして自己整合させる．GATTCは未使用（本ビルドはPERIPHERAL
 *  ロールのみ）のため0のまま．
 */
#define CONFIG_BT_NIMBLE_GATT_SERVER 1
/*
 *  GATT_SERVER=1化に伴い，esp_nimble_cfg.hが#ifndefフォールバック無しで
 *  直接参照する残りのCONFIG_BT_NIMBLE_*一式（esp32c6/Kconfig.inの
 *  既定値相当）．実機ビルドでの逐次発覚に基づき追加．
 */
#define CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES 64
/*
 *  D-2c準備の横展開（C3 wip 8476b55の内容をC6へ移植．
 *  docs/ble-c5c6-plan.md「8. D-2c準備の横展開」節）：標準GAPサービス
 *  （Device Name / Appearance キャラクタリスティック）を有効化．
 *  ble_svc_gap.c:28 の `MYNEWT_VAL(BLE_GATTS) && CONFIG_BT_NIMBLE_
 *  GAP_SERVICE` ゲートを満たすため必須（未定義だとble_svc_gap_defs/
 *  ble_svc_gap_initがコンパイルされずリンクエラー．esp_nimble_cfg.h
 *  L1846-1868で確認済み——C3と同一のMYNEWT_VAL_BLE_SVC_GAP_*写像
 *  コードをC6のnimble submoduleでも確認した）．
 */
#define CONFIG_BT_NIMBLE_GAP_SERVICE 1
/*  GAPサービスの各キャラクタリスティック構成（ESP-IDF Kconfig既定値．
    C3のD-2c wipと同じ値）．CENT_ADDR_RESOLUTION=-1（Central Address
    Resolutionキャラクタ非サポート），PPCP=0（Peripheral Preferred
    Connection Params 無効）．esp_nimble_cfg.hのMYNEWT_VAL_BLE_SVC_GAP_
    PPCP_*は`#ifndef`フォールバックがGAP_SERVICEのifdefの外にある
    （esp_nimble_cfg.h L1893-1911）ため，GAP_SERVICEを立てる時点で
    この4つの定義も必須（未定義のままだと後段でCONFIG_BT_NIMBLE_SVC_
    GAP_PPCP_*という未宣言識別子への参照でコンパイル不能になる）．  */
#define CONFIG_BT_NIMBLE_SVC_GAP_CENT_ADDR_RESOLUTION -1
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_MAX_CONN_INTERVAL 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_MIN_CONN_INTERVAL 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_SLAVE_LATENCY 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_SUPERVISION_TMO 0
#define CONFIG_BT_NIMBLE_L2CAP_ENHANCED_COC 0
#define CONFIG_BT_NIMBLE_DYNAMIC_SERVICE 0
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_ITVL 1000
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_THRESH 2
#define CONFIG_BT_NIMBLE_HS_FLOW_CTRL_TX_ON_DISCONNECT 0
#define CONFIG_BT_NIMBLE_HOST_BASED_PRIVACY 0
/*
 *  ★D-2d bond修正（C3 da5d02d／ae21e7aのC6移植．docs/bt-shim.md「D-2d
 *  bond真因確定」）：MYNEWT_VAL(BLE_HS_PVCY)が0だとble_sm.c:2365-2426の
 *  «responderのIdentity鍵配布（Identity Info/Addressのble_sm_tx）»が丸ごと
 *  `#if MYNEWT_VAL(BLE_HS_PVCY)`でコンパイルアウトされる．SCでは
 *  ble_sm_key_distがENCフラグをクリアするため，our_key_dist=ENC|IDでも
 *  «送る鍵が1つも残らず»ble_sm_tx=0＝Pairing Responseで約束したIdentity鍵を
 *  送れずpeerが待ち→30s SM timeout→ENC_CHANGE ETIMEOUTでbond不成立
 *  （C3/C5実機RXTRACEで確定．C5は実機bond成功で本修正を実証済み）．
 *  working S3もCONFIG_BT_NIMBLE_HS_PVCY=1を設定．HOST_BASED_PRIVACY
 *  （RPA解決変種）はbondingには不要のため0のまま．ble_hs_pvcy.c／
 *  ble_hs_resolv.cはesp_bt.cmakeに既にリンク済＝1でbody有効化・
 *  undefined refなし．
 */
#define CONFIG_BT_NIMBLE_HS_PVCY 1
#define CONFIG_BT_NIMBLE_SM_LVL 0
#define CONFIG_BT_NIMBLE_SMP_ID_RESET 0
#define CONFIG_BT_NIMBLE_SM_SC_DEBUG_KEYS 0
#define CONFIG_BT_NIMBLE_MAX_EADS 1
/*
 *  D-2a（sync到達）はセキュリティ不要．SM_LEGACY/SM_SC=0（esp_bt.cmakeの
 *  MYNEWT_VAL_BLE_SM_LEGACY/SC=0と対）でble_sm*.cをnear-empty化し
 *  mbedTLS/tinycryptのリンクを回避する（C3のD-2a節と同じ判断）．
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
 *  揃える．porting/mem/os_msys_init.cはCONFIG_BT_LE_MSYS_*しか見ないため
 *  実効値としては前者が効くが，esp_nimble_cfg.hのコンパイルには必要）．
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
/*  ★重要：C6/C5は新HCIトランスポート経由（上のファイル冒頭コメント参照）  */
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
