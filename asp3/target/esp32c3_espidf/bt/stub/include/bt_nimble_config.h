/*
 *  NimBLE（Phase D-2）用の CONFIG_BT_* 補完ヘッダ（force-include）
 *
 *  hal/nuttx/esp32c3/include/sdkconfig.h では NimBLE/コントローラ設定
 *  （CONFIG_BT_NIMBLE_* / CONFIG_BT_CONTROLLER_ENABLED / CONFIG_BT_CTRL_*_EFF）
 *  一式が `#ifdef CONFIG_ESPRESSIF_BLE` ブロックにある．ところが同
 *  sdkconfig.h は CONFIG_ESPRESSIF_WIFI を常時定義しているため，
 *  CONFIG_ESPRESSIF_BLE を立てると
 *    `#if defined(ESPRESSIF_BLE) && defined(ESPRESSIF_WIFI)`
 *  により CONFIG_ESP_COEX_SW_COEXIST_ENABLE=1 → CONFIG_SW_COEXIST_ENABLE=1
 *  となり，bt.c の coex 経路（COEX_SCHM_CALLBACK_TYPE_BT 等）が有効化されて
 *  しまう（D-1 は coex OFF で検証済み＝挙動を変えたくない）．
 *
 *  そこで CONFIG_ESPRESSIF_BLE は立てず，NimBLE に必要な CONFIG_BT_* だけを
 *  本ヘッダで直接定義する（sdkconfig.h の当該ブロックからの写しで，
 *  coex/WiFi は一切含めない）．sdkconfig.h の後・syscfg.h の前に
 *  force-include する（esp_nimble_cfg.h が CONFIG_BT_NIMBLE_* を読むため）．
 *
 *  CONFIG_BT_ENABLED は esp_bt.cmake の -D で既に定義済みのため含めない．
 */
#ifndef TOPPERS_BT_NIMBLE_CONFIG_H
#define TOPPERS_BT_NIMBLE_CONFIG_H

/*
 *  npl_os_freertos.c は ESP_IDF_VERSION / ESP_IDF_VERSION_VAL を #if で
 *  使うが同ファイルは esp_idf_version.h を include しない（ESP-IDF では
 *  暗黙include前提）．force-include 本ヘッダで供給する．
 */
#include <esp_idf_version.h>

#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_CONTROLLER_ENABLED 1
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
#define CONFIG_BT_NIMBLE_SECURITY_ENABLE 1
#define CONFIG_BT_NIMBLE_SM_LEGACY 1
#define CONFIG_BT_NIMBLE_SM_SC 1
#define CONFIG_BT_NIMBLE_LL_CFG_FEAT_LE_ENCRYPTION 1
#define CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME "nimble"
#define CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN 31
#define CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU 256
#define CONFIG_BT_NIMBLE_SVC_GAP_APPEARANCE 0x0
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
/*
 *  D-2c：GATTサーバ有効化（MYNEWT_VAL_BLE_GATTS=CONFIG_BT_NIMBLE_GATT_SERVER，
 *  esp_nimble_cfg.h:269-272）．D-2bのadvストーム根治（source多重登録バグ／
 *  (1)(o)）後の再挑戦．旧D-2bの「adv経路NULL関数ポインタ」はストーム
 *  （CPU飽和）状態下の観測であり，storm根治後に再評価する（docs/bt-shim.md
 *  「Phase D-2c」）．
 */
#define CONFIG_BT_NIMBLE_GATT_SERVER 1
/*  ATT サーバのprepared-write（long write）エントリ数．GATT_SERVER=1で
    ble_att_svr.c が MYNEWT_VAL_BLE_ATT_SVR_MAX_PREP_ENTRIES 経由で参照
    （esp_nimble_cfg.h:616）．ESP-IDF既定は64だがRAM節約のため小さく取る
    （本ビルドは最小GATTサービスのみ＝long writeはほぼ発生しない）．  */
#define CONFIG_BT_NIMBLE_ATT_MAX_PREP_ENTRIES 6
/*  標準GAPサービス（Device Name / Appearance キャラクタリスティック）を
    有効化．ble_svc_gap.c:28 の `MYNEWT_VAL(BLE_GATTS) && CONFIG_BT_NIMBLE_GAP_SERVICE`
    ゲートを満たすため必須（未定義だと ble_svc_gap_defs/ble_svc_gap_init が
    コンパイルされずリンクエラー）．  */
#define CONFIG_BT_NIMBLE_GAP_SERVICE 1
/*  GAPサービスの各キャラクタリスティック構成（ESP-IDF Kconfig既定値）．
    CENT_ADDR_RESOLUTION=-1（Central Address Resolutionキャラクタ非サポート），
    PPCP=0（Peripheral Preferred Connection Params 無効）．いずれも
    esp_nimble_cfg.h が #ifdef CONFIG_BT_NIMBLE_GAP_SERVICE で参照する．  */
#define CONFIG_BT_NIMBLE_SVC_GAP_CENT_ADDR_RESOLUTION -1
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_MAX_CONN_INTERVAL 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_MIN_CONN_INTERVAL 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_SLAVE_LATENCY 0
#define CONFIG_BT_NIMBLE_SVC_GAP_PPCP_SUPERVISION_TMO 0
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
#define CONFIG_BT_NIMBLE_LEGACY_VHCI_ENABLE 1
/*
 *  以下2つは上記ブロックには無いが esp_nimble_cfg.h が
 *  MYNEWT_VAL_BLE_TRANSPORT_EVT_(DISCARDABLE_)COUNT の元として参照する．
 *  HCI event buffer 数（HI/LO）に合わせる．
 */
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_COUNT 30
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_DISCARD_COUNT 8
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_SIZE 255
#define CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE 70
#define CONFIG_BT_NIMBLE_L2CAP_COC_SDU_BUFF_COUNT 1
#define CONFIG_BT_NIMBLE_EATT_CHAN_NUM 0
/*
 *  transport.c の POOL_ACL_COUNT は（LL=非native／HS=native の本構成では）
 *  BLE_TRANSPORT_ACL_FROM_LL_COUNT に等しい．ble_transport_deinit は
 *  pool_acl を無条件参照するため 0 だと未定義参照でコンパイル不能．
 *  ACL バッファ数（CONFIG_BT_NIMBLE_ACL_BUF_COUNT=24）に合わせて 24 とする．
 */
#define CONFIG_BT_NIMBLE_TRANSPORT_ACL_FROM_LL_COUNT 24

#define CONFIG_BT_BLE_RPA_TIMEOUT 900
#define CONFIG_BT_BLE_42_FEATURES_SUPPORTED 1
#define CONFIG_BT_BLE_42_DTM_TEST_EN 1
#define CONFIG_BT_BLE_42_ADV_EN 1
#define CONFIG_BT_BLE_42_SCAN_EN 1
#define CONFIG_BT_BLE_VENDOR_HCI_EN 1
#define CONFIG_BT_CTRL_MODE_EFF 1
#define CONFIG_BT_CTRL_BLE_MAX_ACT 6
#define CONFIG_BT_CTRL_BLE_MAX_ACT_EFF 6
#define CONFIG_BT_CTRL_BLE_STATIC_ACL_TX_BUF_NB 0
#define CONFIG_BT_CTRL_PINNED_TO_CORE 0
#define CONFIG_BT_CTRL_HCI_MODE_VHCI 1
#define CONFIG_BT_CTRL_HCI_TL 1
#define CONFIG_BT_CTRL_ADV_DUP_FILT_MAX 30
#define CONFIG_BT_BLE_CCA_MODE_NONE 1
#define CONFIG_BT_BLE_CCA_MODE 0
#define CONFIG_BT_CTRL_HW_CCA_VAL 75
#define CONFIG_BT_CTRL_HW_CCA_EFF 0
#define CONFIG_BT_CTRL_CE_LENGTH_TYPE_ORIG 1
#define CONFIG_BT_CTRL_CE_LENGTH_TYPE_EFF 0
#define CONFIG_BT_CTRL_TX_ANTENNA_INDEX_0 1
#define CONFIG_BT_CTRL_TX_ANTENNA_INDEX_EFF 0
#define CONFIG_BT_CTRL_RX_ANTENNA_INDEX_0 1
#define CONFIG_BT_CTRL_RX_ANTENNA_INDEX_EFF 0
#define CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_P9 1
#define CONFIG_BT_CTRL_DFT_TX_POWER_LEVEL_EFF 11
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_FLOW_CTRL_SUPP 1
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_FLOW_CTRL_NUM 100
#define CONFIG_BT_CTRL_BLE_ADV_REPORT_DISCARD_THRSHOLD 20
#define CONFIG_BT_CTRL_BLE_SCAN_DUPL 1
#define CONFIG_BT_CTRL_SCAN_DUPL_TYPE_DEVICE 1
#define CONFIG_BT_CTRL_SCAN_DUPL_TYPE 0
#define CONFIG_BT_CTRL_SCAN_DUPL_CACHE_SIZE 100
#define CONFIG_BT_CTRL_DUPL_SCAN_CACHE_REFRESH_PERIOD 0
#define CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_DIS 1
#define CONFIG_BT_CTRL_COEX_PHY_CODED_TX_RX_TLIM_EFF 0
#define CONFIG_BT_CTRL_HCI_TL_EFF 1
#define CONFIG_BT_CTRL_CHAN_ASS_EN 1
#define CONFIG_BT_CTRL_LE_PING_EN 1
#define CONFIG_BT_CTRL_DTM_ENABLE 1
#define CONFIG_BT_CTRL_BLE_MASTER 1
#define CONFIG_BT_CTRL_BLE_SCAN 1
#define CONFIG_BT_CTRL_BLE_SECURITY_ENABLE 1
#define CONFIG_BT_CTRL_BLE_ADV 1
#define CONFIG_BT_ALARM_MAX_NUM 50
#define CONFIG_MAC_BB_PD 0
/*  modem sleep 無効（D-1 と同じ．CONFIG_PM 非使用相当）  */
#define CONFIG_BT_CTRL_SLEEP_MODE_EFF 0
#define CONFIG_BT_CTRL_SLEEP_CLOCK_EFF 0

/*
 *  ★D-2d bond 真因修正（docs/bt-shim.md「D-2d bond 真因確定」節，C5 で実機実証）：
 *  MYNEWT_VAL(BLE_HS_PVCY)=0 だと ble_sm.c:2365-2426 の «responder の Identity 鍵
 *  配布（Identity Info/Address の ble_sm_tx）» が `#if MYNEWT_VAL(BLE_HS_PVCY)` で
 *  丸ごとコンパイルアウトされ，SC では ble_sm_key_dist が ENC もクリアするため
 *  our_key_dist=ENC|ID でも送る鍵が残らず ble_sm_tx=0＝約束した Identity 鍵を送れず
 *  peer が待ち→30s SM timeout→bond 不成立．C5 実機で PVCY=1 にして sm_tx=2・
 *  ENC status=0・bond 成功を実証（C3 も同一 blob 非依存の同一失敗＝同じ真因）．
 *  working S3 も CONFIG_BT_NIMBLE_HS_PVCY=1（bt_nimble_config.h:202）．
 *  ble_hs_pvcy.c/ble_hs_resolv.c は esp_bt.cmake:364-365 に既にリンク済．
 *
 *  ★connect不可切り分け（docs/c3-ble-connect-plan.md 段階1）用トグル：
 *  esp_bt.cmake の option(ESP32C3_BT_PVCY) が OFF のとき TOPPERS_C3_BT_PVCY_OFF
 *  が -D 定義され，PVCY=0（＝起動時 resolving-list HCIバースト無し・
 *  ただし responder Identity 鍵配布がコンパイルアウト＝bond不可）になる．
 *  これは A/B 切り分け専用の一時ビルド．**恒久ビルドの既定は PVCY=1**
 *  （ESP32C3_BT_PVCY=ON＝-D 無し）のまま変えない．
 */
#ifdef TOPPERS_C3_BT_PVCY_OFF
#define CONFIG_BT_NIMBLE_HS_PVCY 0
#else
#define CONFIG_BT_NIMBLE_HS_PVCY 1
#endif

#endif /* TOPPERS_BT_NIMBLE_CONFIG_H */
