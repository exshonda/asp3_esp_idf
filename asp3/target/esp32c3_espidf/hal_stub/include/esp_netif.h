/*
 *  esp_netif.h コンパイル用スタブ
 *
 *  esp_netif（ESP-IDFのネットワークインタフェース抽象化）は独立した
 *  大きなコンポーネントで，esp-hal-3rdparty（本リポジトリのhal
 *  submodule＝RTOS非依存の下層のみ収録）には含まれない．
 *  esp_wifi/include/esp_wifi_default.h が型・プロトタイプ宣言として
 *  参照するのみで，本リポジトリのコンパイル対象ソース（wifi_init.c等）
 *  はesp_netif_*APIを実際には呼び出していない（grep確認済み）ため，
 *  型が引けるだけの不完全型宣言で足りる．
 *
 *  esp_netif自体の実装（Wi-Fi⇔IPスタック接続）はPhase B-2以降，
 *  ASP3側のネットワークスタック方針が決まった時点で別途検討する。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_ESP_NETIF_H
#define TOPPERS_HAL_STUB_ESP_NETIF_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_netif_obj              esp_netif_t;
typedef struct esp_netif_inherent_config  esp_netif_inherent_config_t;

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_HAL_STUB_ESP_NETIF_H */
