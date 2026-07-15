/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  Wi-Fi blob（libnet80211.a／libpp.a／libcore.a／libphy.a）が直接
 *  参照する「グローバル変数」「osi経由でない裸のextern関数」の
 *  ASP3側実体をまとめたファイル（esp_wifi_adapter.cのwifi_osi_funcs_t
 *  経由の関数はesp_wifi_adapter.c側，libc相当はesp_shim_libc.c側）．
 *
 *  esp-hal-3rdpartyのNuttXポート（ref-esp32c3/nuttx）はBLE統合まで
 *  含む古い時点のblobを前提にしており，本リポジトリが取得した
 *  esp-hal-3rdparty@b90b183のblobが要求するシンボルの一部
 *  （g_misc_nvs・g_espnow_user_oui・mesh_sta_auth_expire_time・
 *  g_log_level等）はNuttX側に対応が無い＝ヘッダ宣言も無い．
 *  型・意味はesp-hal-3rdparty内の周辺API（esp_now_set_user_oui等）
 *  から妥当な推定で決めている．詳細・妥協点はdocs/wifi-shim.md参照．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_err.h"
#include "nvs.h"
#include "esp_mac.h"
#include "esp_private/esp_sleep_internal.h"

/*
 *  ------------------------------------------------------------------
 *  1. NVS（不揮発ストレージ）関連 — 無効化スタブ
 *  ------------------------------------------------------------------
 *
 *  docs/wifi-shim.md記載のとおりNVSは本フェーズでは未実装．
 *  g_misc_nvsはblob（ieee80211_*.o）がNULLチェック後にNVSハンドルとして
 *  使うポインタと推定（ヘッダ宣言が無いため型は要旨から妥当な範囲で
 *  決定）．NULL固定のため，blobのNVS依存経路（プロファイル保存・
 *  スキャン履歴の永続化等）は実行時に「NVS無し」として振る舞う想定．
 *  もし実際にはNULL非チェックで無条件デリファレンスする経路がある
 *  場合はクラッシュしうる＝実機検証時の既知リスク（未検証）．
 */
void *g_misc_nvs = NULL;

int
misc_nvs_init(void)
{
	/*  NVS未実装のため常に「初期化した体」で成功を返す  */
	return(0);
}

void
misc_nvs_deinit(void)
{
	/*  no-op  */
}

/*
 *  ------------------------------------------------------------------
 *  2. ESP-NOW ユーザOUI — 未設定（全ゼロ）固定
 *  ------------------------------------------------------------------
 *
 *  esp_now_set_user_oui()/esp_now_get_user_oui()（esp_now.h）の
 *  バッキングストアと推定．3バイト固定（WIFI_OUI_LEN，
 *  esp_wifi_types_generic.hのvendor_oui[3]/wfa_oui[3]と同型）．
 *  ESP-NOWは本Wi-Fiスキャンデモのスコープ外のため，
 *  esp_now_set_user_oui()相当の設定APIは未実装＝常に全ゼロ．
 */
uint8_t g_espnow_user_oui[3] = { 0U, 0U, 0U };

/*
 *  ------------------------------------------------------------------
 *  3. メッシュ関連
 *  ------------------------------------------------------------------
 *
 *  ieee80211_sta.c（ieee80211_sta_new_state）が参照．ESP-MESH
 *  （libmesh.a）はリンク対象だが本デモでは有効化しない＝
 *  スキャン専用の通常STA遷移では「認証期限切れ猶予0」で影響しない
 *  よう0固定とする．
 */
uint32_t mesh_sta_auth_expire_time = 0U;

/*
 *  ------------------------------------------------------------------
 *  4. ログレベル
 *  ------------------------------------------------------------------
 *
 *  ieee80211_debug.c（wifi_log）が参照する冗長度しきい値．ASP3の
 *  syslogは別途フィルタするため，blob側の冗長ログは最小（0=エラー
 *  のみ相当）にしておく．
 */
int g_log_level = 0;

/*
 *  ------------------------------------------------------------------
 *  5. ADC2較正リンク強制（no-op）
 *  ------------------------------------------------------------------
 *
 *  esp_hw_support/include/esp_private/adc_share_hw_ctrl.hのコメント
 *  どおり「ADC2較正コンストラクタをリンクさせるためだけの空関数」．
 *  本ビルドはADC較正ソース自体を取り込んでいないため，空実装のままで
 *  意味的に正しい（較正無し＝ADC2較正コンストラクタが存在しないため
 *  リンクすべき対象も無い）．
 */
void
adc2_cal_include(void)
{
	/*  no-op（意図通り．上記コメント参照）  */
}

/*
 *  ------------------------------------------------------------------
 *  6. eFuse鍵用途・HMAC — 未プロビジョニング固定
 *  ------------------------------------------------------------------
 *
 *  mbedtls psa_driver（esp_hmac_opaque／esp_ecdsa）がハードウェア
 *  鍵（eFuse焼き込み鍵）の有無を判定するために呼ぶ．本ビルドは
 *  efuseコンポーネント本体を取り込んでいない（esp_efuse_api.c等は
 *  bootloader_support等への依存が深いため対象外＝
 *  docs/wifi-shim.md参照）．eFuse鍵は焼いていない前提のため，
 *  「用途未設定（ESP_EFUSE_KEY_PURPOSE_USER=0）」を返せば呼び出し側は
 *  ハードウェア鍵無しとして正しくソフトウェア実装側にフォールバック
 *  する．esp_hmac_calculateは呼ばれないはずだが，念のため失敗を返す
 *  スタブとする（task指示どおり-1返し）．
 */
int
esp_efuse_get_key_purpose(int block)
{
	(void) block;
	return(0);	/* ESP_EFUSE_KEY_PURPOSE_USER相当 */
}

int
esp_hmac_calculate(int key_id, const void *message, size_t message_len,
					uint8_t *hmac)
{
	(void) key_id; (void) message; (void) message_len; (void) hmac;
	return(-1);	/* ESP_FAIL相当．eFuse鍵未プロビジョニングのため常に失敗 */
}

/*
 *  ------------------------------------------------------------------
 *  7. MAC アドレス読み出し（eFuseレジスタ直読み）
 *  ------------------------------------------------------------------
 *
 *  esp_hw_support/mac_addr.c本体（esp_read_mac）はesp_efuse本体・
 *  esp_efuse_table等チップ全体のブート基盤に依存が及ぶため採用せず
 *  （esp_wifi.cmake §6コメント参照），EFUSE_RD_MAC_SPI_SYS_{0,1}_REG
 *  （soc/efuse_reg.h）を直接読む簡易実装とする．バイト順は
 *  ESP-IDFの標準的なMAC efuseレイアウト（mac1の下位16bitが上位
 *  2バイト，mac0が下位4バイト，かつワード内バイト順が逆順）に従う．
 *  typeパラメータ（STA/AP/BT/ETH）は未区別＝全て同じベースMACを返す
 *  （esp_wifi_adapter.cのread_mac_wrapperはtype無視で呼んでいる）．
 *  シグネチャはesp_mac.h（esp_err_t esp_read_mac(uint8_t *mac,
 *  esp_mac_type_t type)）に一致させる（8bで本ファイルがesp_mac.hを
 *  #includeするようになったため．esp_wifi_adapter.c側は従来どおり
 *  独自のint版externで呼ぶ＝別TUのためリンクは名前のみで解決され
 *  問題ない）．
 */
/*  DR_REG_EFUSE_BASE(0x60008800) + 0x44／0x48（reg_base.h／efuse_reg.h）  */
#define EFUSE_RD_MAC_SPI_SYS_0_REG	0x60008844U
#define EFUSE_RD_MAC_SPI_SYS_1_REG	0x60008848U

esp_err_t
esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
	uint32_t	mac0;
	uint32_t	mac1;

	(void) type;
	if (mac == NULL) {
		return(ESP_FAIL);
	}
	mac0 = sil_rew_mem((void *) EFUSE_RD_MAC_SPI_SYS_0_REG);
	mac1 = sil_rew_mem((void *) EFUSE_RD_MAC_SPI_SYS_1_REG);

	mac[0] = (uint8_t) (mac1 >> 8);
	mac[1] = (uint8_t) mac1;
	mac[2] = (uint8_t) (mac0 >> 24);
	mac[3] = (uint8_t) (mac0 >> 16);
	mac[4] = (uint8_t) (mac0 >> 8);
	mac[5] = (uint8_t) mac0;
	return(ESP_OK);
}

/*
 *  ------------------------------------------------------------------
 *  8. Wi-Fi/BTパワードメイン・PHY enable/disable／modem init-deinit／
 *     country info — esp_phy/src/phy_init.c 本実装へ置き換え
 *  ------------------------------------------------------------------
 *
 *  【2026-07-03更新】以前ここにあった簡易実装（esp_wifi_power_domain_
 *  on/off・esp_phy_enable/disable・esp_phy_modem_init/deinit・
 *  esp_phy_update_country_info）は，クロックゲートのみでlibphy.a内部
 *  の関数テーブル初期化（register_chipv7_phy）を一切呼んでいなかった
 *  ため，esp_wifi_scan_start()実行時にPHYの関数テーブル（set_chanfreq
 *  等）がNULLのまま呼び出され実機でクラッシュしていた．
 *
 *  esp_wifi.cmake §1cでesp_phy/src/phy_init.c（本体）・phy_common.c
 *  （補助関数）・esp32c3/phy_init_data.c（既定PHY初期化データ）を
 *  採用し，上記関数群はすべてそちらの実装（esp_wifi_bt_power_domain_
 *  on/off・esp_phy_enable/disable・esp_phy_modem_init/deinit・
 *  esp_phy_update_country_info）に置き換わったため，本ファイルの
 *  簡易実装は削除した（同名シンボルの重複定義を避けるため）．
 *  詳細・妥協点（PLL追従無効化・NVS較正未実装）はesp_wifi.cmake §1c
 *  およびdocs/wifi-shim.mdを参照．
 */

/*
 *  ------------------------------------------------------------------
 *  8a. NVS（PHY較正データ永続化）— 未実装スタブ
 *  ------------------------------------------------------------------
 *
 *  hal_stub/include/nvs.h参照．esp_phy/src/phy_init.cの較正データ
 *  永続化関数（`#ifndef __NuttX__`ガードのみでコンパイルされるが，
 *  本ビルドはCONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGEを定義しない
 *  ため実行経路には現れない＝毎回フル較正PHY_RF_CAL_FULL固定．
 *  esp_wifi.cmake §1c参照）が要求するシンボルを解決するためだけの
 *  スタブ．NVS「未初期化」として一貫して失敗を返す．
 */
esp_err_t
nvs_open(const char *name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle)
{
	(void) name; (void) open_mode; (void) out_handle;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

void
nvs_close(nvs_handle_t handle)
{
	(void) handle;
}

esp_err_t
nvs_get_u32(nvs_handle_t handle, const char *key, uint32_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_set_u32(nvs_handle_t handle, const char *key, uint32_t value)
{
	(void) handle; (void) key; (void) value;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
			size_t *length)
{
	(void) handle; (void) key; (void) out_value; (void) length;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
			size_t length)
{
	(void) handle; (void) key; (void) value; (void) length;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_erase_all(nvs_handle_t handle)
{
	(void) handle;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

esp_err_t
nvs_commit(nvs_handle_t handle)
{
	(void) handle;
	return(ESP_ERR_NVS_NOT_INITIALIZED);
}

/*
 *  ------------------------------------------------------------------
 *  8b. eFuse MACアドレス取得（esp_efuse_mac_get_default）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.cの較正データNVS保存関数（8a同様，本ビルド
 *  では実行経路に現れない）が参照．eFuse本体コンポーネント未採用
 *  （§7「7. MAC アドレス読み出し」コメント参照）のため，同じ
 *  EFUSE_RD_MAC_SPI_SYS_*レジスタ直読みのesp_read_mac()へ委譲する．
 */
esp_err_t
esp_efuse_mac_get_default(uint8_t *mac)
{
	/*  esp_read_mac()の戻り値は0=成功/-1=失敗＝ESP_OK/ESP_FAILと一致  */
	return((esp_err_t) esp_read_mac(mac, 0));
}

/*
 *  ------------------------------------------------------------------
 *  8c. ディープスリープPHYフック登録 — no-opスタブ
 *  ------------------------------------------------------------------
 *
 *  esp_phy_load_cal_and_init()末尾（`#ifndef __NuttX__` かつ
 *  `CONFIG_ESP_PHY_ENABLED && SOC_DEEP_SLEEP_SUPPORTED`＝本ビルドは
 *  両方成立するため実行される）がesp_deep_sleep_register_phy_hook()
 *  でPHYシャットダウン関数（phy_close_rf／phy_xpd_tsens）をディープ
 *  スリープ用フックとして登録する．本体（esp_hw_support/
 *  sleep_modes.c）はディープスリープ状態機械全体に依存が及ぶため
 *  不採用．ASP3はディープスリープへ入る経路を持たない（既存の
 *  esp_phy_modem_init/deinit移植時と同じ理由）ため，フックは受理
 *  するが登録内容を保持・呼び出しはしないno-opスタブとする．
 *  ESP_ERROR_CHECK()マクロがabort()しないよう常にESP_OKを返す．
 */
esp_err_t
esp_deep_sleep_register_phy_hook(esp_deep_sleep_cb_t new_dslp_cb)
{
	(void) new_dslp_cb;
	return(0);	/* ESP_OK相当 */
}

/*
 *  ------------------------------------------------------------------
 *  8d. ESP_ERROR_CHECK() マクロの実体（esp_err.h）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.c（esp_deep_sleep_register_phy_hook呼び出し）
 *  がESP_ERROR_CHECK()マクロ経由で参照する．失敗時は診断情報を
 *  syslogへ出力し，abort()（esp_shim_libc.c．syslog+無限ループ＝
 *  target_stddef.hのTOPPERS_assert_abort()と同じ停止方式）へ委譲する．
 *  ESP_ERROR_CHECK_WITHOUT_ABORT()用の_esp_error_check_failed_
 *  without_abort()は本ビルドで参照されないため未実装（到達不能）．
 */
void
_esp_error_check_failed(esp_err_t rc, const char *file, int line,
						const char *function, const char *expression)
{
	syslog(LOG_EMERG, "ESP_ERROR_CHECK failed: 0x%x at %s:%d (%s): %s",
		  (int_t) rc, file, line, function, expression);
	abort();
}

/*
 *  ------------------------------------------------------------------
 *  10. coexistence（未実装フィールドのno-opスタブ）
 *  ------------------------------------------------------------------
 *
 *  wifi_os_adapter.hの_coex_condition_set相当．libcoexist.aには
 *  対応する coex_condition_set 関数が存在しない（nm確認済み．
 *  coex_pti_get/coex_pti_set等は存在するが condition_set は無し＝
 *  このesp-hal-3rdpartyスナップショットでは未実装／将来予約
 *  フィールドと判断）．BLE非統合（本ターゲットはWi-Fi専用）のため
 *  実質呼ばれない想定のno-opスタブとする．
 */
void
coex_condition_set(uint32_t type, bool_t dissatisfy)
{
	(void) type; (void) dissatisfy;
}

/*
 *  ------------------------------------------------------------------
 *  11. v5.5.4統一（docs/blob-unify-v554.md）：wpa_supplicant/esp_supplicant
 *      が「blobが実装する想定」で宣言している関数（esp_wifi_driver.h）の
 *      うち，hal（esp-hal-3rdparty submodule，新しめのNuttX同期スナップ
 *      ショット）の net80211.a/pp.a には実装されているが，ESP-IDF
 *      v5.5.4（`~/tools/esp-idf`）の同blobには**存在しない**3関数．
 *      （実測：riscv-none-elf-nmでhal側libnet80211.aにT定義あり，
 *      v5.5.4側は3関数ともU/未定義すら無し＝そもそも無い）。
 *      WPA3互換モード・RSNXE override・PMKキャッシュskip制御という
 *      比較的新しいwpa_supplicant機能で，hal同梱のwpa_supplicant
 *      ソース（差し替えていない．hal/asp3_core非編集の禁則によりhal
 *      配下は不変）はこれを無条件に呼ぶが，v5.5.4のblob世代では
 *      そもそもこの機能自体が存在しない（older世代）．
 *      wifi_scanは素のopen scanのみ（WPA関連分岐は実行時に到達しない）
 *      ため，リンク解決のためのno-op／機能無効化スタブで足りる：
 *        - esp_wifi_skip_supp_pmkcaching：false（PMKキャッシュを
 *          スキップしない＝この制御が存在しなかった旧世代の暗黙の
 *          既定動作と同じ）
 *        - esp_wifi_sta_get_ie：NULL（override IE無し．呼び出し元
 *          （wpa.c/esp_wpa3.c）はNULL時は通常のRSN/RSNXE解析へ
 *          フォールバックする設計）
 *        - esp_wifi_is_wpa3_compatible_mode_enabled：false（WPA3
 *          互換モードという概念自体が無かった旧世代のblobと同じ
 *          挙動＝厳格WPA3のみ）
 *      いずれも実行時未到達（wifi_scanはSTA接続・AP機能を使わない）
 *      だが，wpa_supplicantソース全体を積む方針（Wireless.mk踏襲）の
 *      ためリンク解決だけは必要。プロトタイプはesp_wifi_driver.h
 *      （wpa_supplicant/esp_supplicant/src．__NuttX__非定義のため
 *      本ファイルではu8型が見えず素のuint8_tで揃える＝ABI上u8は
 *      typedef uint8_tのため同一）。
 *
 *      ASP3_WIFI_BLOB_V554（esp_wifi.cmake．v5.5.4 blob選択時のみ
 *      定義）でガード＝hal blob使用時（ASP3_WIFI_BLOB_HAL=ON）は
 *      hal blob自身がこの3関数を提供する（nm確認済み）ため，本shimを
 *      二重定義しない。
 *  ------------------------------------------------------------------
 */
#if ASP3_WIFI_BLOB_V554
bool
esp_wifi_skip_supp_pmkcaching(void)
{
	return false;
}

uint8_t *
esp_wifi_sta_get_ie(uint8_t *bssid, uint8_t elem_id)
{
	(void) bssid; (void) elem_id;
	return NULL;
}

bool
esp_wifi_is_wpa3_compatible_mode_enabled(uint8_t if_index)
{
	(void) if_index;
	return false;
}
#endif /* ASP3_WIFI_BLOB_V554 */
