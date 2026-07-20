/*
 *  ★D-2c準備（GAP_SERVICE以下の追加分）はビルド未検証（IDF v6.1環境
 *  （`/home/honda/tools/esp-idf-v6.1`）が本開発環境に存在しないため）．
 *  C6での同種変更（本ファイル内GAP_SERVICE節のコメント参照）はビルド
 *  検証済みだが，C5はhal submoduleではなくIDF v6.1のnimbleソースを
 *  使うため，esp_nimble_cfg.hのgate位置がC6と完全一致する保証はない
 *  （C6のesp-nimble submoduleとv6.1は別バージョンの可能性）．実機/
 *  ビルド再開時は必ずビルドで確認すること．
 *  docs/ble-c5c6-plan.md「8. D-2c準備の横展開」節にも同旨を記載．
 *
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
/*
 *  ★ビルド未検証（IDF v6.1環境で要ビルド．ファイル冒頭コメント参照）．
 *  D-2c準備の横展開（C3 wip 8476b55 / C6実施の内容をC5へ移植．
 *  docs/ble-c5c6-plan.md「8. D-2c準備の横展開」節）：標準GAPサービス
 *  （Device Name / Appearance キャラクタリスティック）を有効化．
 *  C6と同じくble_svc_gap.c内の`MYNEWT_VAL(BLE_GATTS) &&
 *  CONFIG_BT_NIMBLE_GAP_SERVICE`ゲートを満たすため必須と推定
 *  （esp_nimble_cfg.hはApache Mynewt NimBLE系の共通ヘッダのため，
 *  C6/hal submoduleと同じgate構造をIDF v6.1も持つ可能性が高いが，
 *  v6.1ソース自体をこの環境で確認できないため未検証）．
 */
#define CONFIG_BT_NIMBLE_GAP_SERVICE 1
/*  GAPサービスの各キャラクタリスティック構成（ESP-IDF Kconfig既定値．
    C3/C6と同じ値）．CENT_ADDR_RESOLUTION=-1，PPCP=0（無効）．C6同様，
    esp_nimble_cfg.hのMYNEWT_VAL_BLE_SVC_GAP_PPCP_*が`#ifndef`
    フォールバックをGAP_SERVICEのifdefの外に持つ構造だと推定されるため，
    GAP_SERVICEを立てる時点でこの4つも定義しておく（★同上・未検証）．  */
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
 *  ★D-2d bond 真因修正（docs/bt-shim.md「D-2d bond 真因確定」節）：
 *  MYNEWT_VAL(BLE_HS_PVCY) が 0 だと ble_sm.c:2365-2426 の «responder の
 *  Identity 鍵配布（Identity Info/Address の ble_sm_tx）» が丸ごと
 *  `#if MYNEWT_VAL(BLE_HS_PVCY)` でコンパイルアウトされる．SC では
 *  ble_sm_key_dist が ENC フラグをクリアするため，our_key_dist=ENC|ID でも
 *  «送る鍵が1つも残らず» ble_sm_tx=0＝Pairing Response で約束した Identity 鍵を
 *  送れずに peer が待ち→30s SM timeout→ENC_CHANGE ETIMEOUT で bond 不成立
 *  （実機 RXTRACE で確定：sm_tx=0・delta=30s）．working S3 も同一理由で
 *  CONFIG_BT_NIMBLE_HS_PVCY=1 を設定（bt_nimble_config.h:202）．
 *  HOST_BASED_PRIVACY（RPA 解決変種）は bonding には不要のため 0 のまま．
 *  ble_hs_pvcy.c/ble_hs_resolv.c は esp_bt.cmake に既にリンク済＝1 で body 有効化．
 */
#define CONFIG_BT_NIMBLE_HS_PVCY 1
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

/*
 *  CONFIG_BT_NIMBLE_SM_SIGN_CNT（config監査 §3.11.2/§8）：ESP-IDF v6.1 Kconfig
 *  既定 y に対し全チップ変種で未定義＝MYNEWT_VAL(BLE_SM_SIGN_CNT)=0 に fallback
 *  していた «PVCY 型» の共通欠落（chip 横断の共通欠落のため chip 間 diff では
 *  見えない）．有効化で ATT Signed Write の sign-counter replay 保護（受信：
 *  ble_att_svr.c:2548 の ble_sm_incr_peer_sign_counter，送信 API：ble_sm.c:2721
 *  の ble_sm_incr_our_sign_counter）が働く．マクロは #if 文脈のみで使用
 *  （配列サイズ等なし＝定義は安全），bond/接続そのものには無影響．
 */
#define CONFIG_BT_NIMBLE_SM_SIGN_CNT 1

#endif /* TOPPERS_BT_NIMBLE_CONFIG_H */
