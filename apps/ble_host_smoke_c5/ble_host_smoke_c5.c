/*
 *  ★D-2c準備（GAP CONNECT/DISCONNECTマーカ＋ble_svc_gap/gatt_init呼出し
 *  部分）はビルド未検証（IDF v6.1環境（`/home/honda/tools/esp-idf-v6.1`）
 *  が本開発環境に存在しないため）．C6での同種変更はビルド検証済み
 *  （docs/ble-c5c6-plan.md「8. D-2c準備の横展開」節）．C5移植は静的整合のみで，
 *  実機・ビルドとも未検証．次回IDF v6.1環境で最初にビルドすること．
 *
 *  NimBLE host スモークテスト（ESP32-C5．Phase D-2a／BLE実施05）
 *
 *  apps/ble_host_smoke_c6（C6のD-2a/D-2b）をC5向けに転写した版．
 *  apps/bt_smoke_c5（D-1．BLE実施03/04で達成）で確立したBLEコントローラ
 *  起動シーケンスの上にNimBLEホストスタックを載せる．狙いはble_hsの
 *  syncコールバック到達（D-2a）．届けば接続可能アドバタイズ（D-2b）も試す．
 *
 *  C6版（ble_host_smoke_c6）との違い：
 *    - デバイス名 "ASP3-C5-BLE"．
 *    - ICGアンロックは実施13（C5固有．C6は実施91）＝同じ
 *      esp_shim_bt_clock_init()関数名（bt/bt_shim.c）．
 *    - LP_AON STOREマップをC5用に変更（後述）．C5では実施35がSTORE1
 *      （RTC_SLOW_CLK_CAL），実施41がSTORE2-4を診断ミラーに使うが，
 *      実施41の起動時書込みはESP32C5_R41_CALL_BOOTHOOK未定義の本BTビルド
 *      では走らないため，STORE2-4は「実測でreset生存かつROM非改変」を
 *      実施41が実証済みの安全な空きレジスタとして再利用する（BLE実施05）．
 *      STORE1（RTC cal）とSTORE7（bt_shim.cの多重登録トレース）は予約．
 *
 *  ★C5実施04の教訓（最重要）：C5では "no time event is processed in hrt
 *  interrupt." がsyslogバッファを溢れさせ（"N messages are lost"），
 *  sync/adv/milestoneのログをコンソール上でかき消す（良性なSYSTIMER
 *  level再ラッチstorm＝機能は正常）．そのため判定はコンソールに依存せず，
 *  下記のLP_AON STOREマーカ（esptool --no-stub read-memで回収）を主物証と
 *  する．コンソールは氾濫行を grep -av で除去してから補助的に読む．
 *
 *  ホスト初期化の配線（C6と同一の設計判断．nimble_port_init()は
 *  コントローラを二重初期化するため使わず，esp_bt_controller_init/enable
 *  を明示呼出し→esp_nimble_init()→sync_cb登録→nimble_port_freertos_init()）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "ble_host_smoke_c5.h"

#include "esp_bt.h"
#include "esp_shim.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_adv.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/*
 *  実施13のICGアンロックをBTクロック初期化の一部として呼ぶ（bt/bt_shim.c，
 *  bt_smoke_c5と同一）．esp_bt_controller_init()より前に呼ぶこと．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  多重登録トレース（bt/bt_shim.cのesp_intr_allocが記録．
 *  C5 LP_AON STORE7＝usb-reset生存．アドレスはbt_shim.cの
 *  BT_INTR_TRACE_REGと一致させること）．
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

/*
 *  HP_APM M0-M3例外ラッチ（実施42/43と同一レジスタ配置）．
 */
#define HP_APM_M0_STATCLR	0x600990CCUL

/*
 *  WiFi/BT共有の割込みディスパッチカウンタ（wifi/esp_shim.c）．
 */
extern volatile uint32_t esp_shim_int_count[];

/*
 *  LP_AON STORE系（usb-reset生存）．C5マップ（BLE実施05）：
 *    STORE0 (+0x00) sync マーカ（C6でreset生存を実証済み）
 *    STORE2 (+0x08) adv-return (rc) マーカ（実施41でreset生存実証済み）
 *    STORE3 (+0x0C) ble_hs_cfg.reset_cb reason/count（同上）
 *    STORE4 (+0x10) 割込みレート：CPU線1累積ミラー（同上）
 *    STORE5 (+0x14) 割込みレート：CPU線2累積ミラー（副次テレメトリ．
 *                    実施41未使用＝reset生存は本ラウンドで確認する）
 *    STORE7 (+0x1C) 多重登録トレース（bt_shim.c，予約）
 *  STORE1（RTC_SLOW_CLK_CAL）は避ける．
 */
#define LP_AON_STORE0		0x600B1000UL	/* sync マーカ */
#define LP_AON_STORE2		0x600B1008UL	/* adv-return (rc) マーカ */
#define LP_AON_STORE3		0x600B100CUL	/* ble_hs reset reason/count */
#define LP_AON_STORE4		0x600B1010UL	/* 割込みレート：CPU線1累積ミラー */
#define LP_AON_STORE5		0x600B1014UL	/* 割込みレート：CPU線2累積ミラー */
/*
 *  ★ビルド未検証（ファイル冒頭コメント参照）．
 *  D-2c準備（C3 wip 8476b55の横展開，docs/ble-c5c6-plan.md「8. D-2c
 *  準備の横展開」）：GAP接続／切断マーカ．STORE0/2/3/4/5/7は本アプリで使用済み，
 *  STORE1（RTC_SLOW_CLK_CAL，実施35）は既知の予約のため避ける．STORE6は
 *  本アプリ内では未使用だが，C6実施02がSTORE6をreset_cbマーカに
 *  使っており，クロスチップでの番地対応を崩さないため，C6と同一の
 *  STORE8/9（`hal/components/soc/esp32c5/register/soc/lp_aon_reg.h`で
 *  実在確認済み，本ラウンドでは未使用）を新規に明け渡す．フォーマット：
 *  connect: 0x604E<status:8><conn_count:8>／
 *  disconnect: 0xD15C<reason:8><disc_count:8>．
 */
#define LP_AON_STORE8		0x600B1020UL	/* GAP CONNECT マーカ */
#define LP_AON_STORE9		0x600B1024UL	/* GAP DISCONNECT マーカ */
#define BLE_CONN_MARK_ADDR	((void *) LP_AON_STORE8)
#define BLE_DISC_MARK_ADDR	((void *) LP_AON_STORE9)
#ifdef TOPPERS_ESP32C5_BT_SM
/*  ★D-2d（SM）：ENC_CHANGE／PAIRING_COMPLETE マーカ（LP_AON STORE10/11＝
    usb-reset生存．C3 の 0x58/0x54 相当）．値のフォーマットも C3 D-2d に揃える：
    ENC=0x5DE0<status:8>／PAIRING=0x5DC0<status:8><our_sec:4><peer_sec:4>．  */
#define LP_AON_STORE10		0x600B1028UL	/* ENC_CHANGE マーカ */
#define LP_AON_STORE11		0x600B102CUL	/* PAIRING_COMPLETE マーカ */
#endif

#define BLE_SYNC_MARK_VAL	0x5ADE51C0UL

/*
 *  ble_hs sync 到達マーカ（グローバルとLP_AON STORE0の両方へ記録）．
 */
volatile uint32_t	g_ble_sync_done;

/*
 *  D-2b：GAPアドバタイズ観測用グローバル．
 */
volatile uint32_t	g_adv_active;
volatile int32_t	g_adv_rc = -1;
volatile uint32_t	g_gap_conn_count;
volatile uint32_t	g_gap_disc_count;
volatile uint32_t	g_gap_event_count;
volatile uint8_t	g_own_addr_type;
volatile int32_t	g_reset_reason = 0x7fffffff;
volatile uint32_t	g_reset_count;
#ifdef TOPPERS_ESP32C5_BT_SM
/*  ★D-2d（SM）：接続ハンドルと slave Security Request 計時（S3/C3 D-2d 移植）．  */
volatile uint16_t	g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
volatile uint32_t	g_conn_secs;
volatile uint8_t	g_sec_initiated;
extern void			ble_store_config_init(void);
#endif

#define BLE_DEVICE_NAME		"ASP3-C5-BLE"

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void
report_intr_trace(void)
{
	uint32_t	trace = sil_rew_mem((const uint32_t *) BT_INTR_TRACE_REG);

	if ((trace >> 24) == 0xA1U) {
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c5: intr trace = 0x%08x (nalloc=%d src1=%d src2=%d)",
			   (int_t) trace, (int_t)((trace >> 16) & 0xFFU),
			   (int_t)((trace >> 8) & 0xFFU), (int_t)(trace & 0xFFU));
	}
	else {
		syslog(LOG_NOTICE, "ble_host_smoke_c5: intr trace not recorded (0x%08x)",
			   (int_t) trace);
	}
}

static void
report_apm_latch(void)
{
	uint_t		i;
	bool_t		any = false;

	for (i = 0U; i < 4U; i++) {
		uint32_t	v = sil_rew_mem((const uint32_t *)(HP_APM_M0_STATCLR + i * 0x10UL));
		if ((v & 1U) != 0U) {
			any = true;
			syslog(LOG_ERROR, "ble_host_smoke_c5: HP_APM M%d exception latch SET (0x%08x)",
				   (int_t) i, (int_t) v);
		}
	}
	if (!any) {
		syslog(LOG_NOTICE, "ble_host_smoke_c5: HP_APM M0-M3 exception latch clear");
	}
}

static void
report_intr_rate(void)
{
	uint32_t	c1_0 = esp_shim_int_count[1];
	uint32_t	c2_0 = esp_shim_int_count[2];

	(void) tslp_tsk(1000000);	/* 1s観測窓 */

	syslog(LOG_NOTICE,
		   "ble_host_smoke_c5: intr rate/1s line1=%d line2=%d (storm threshold ~ >>1000/s)",
		   (int_t)(esp_shim_int_count[1] - c1_0),
		   (int_t)(esp_shim_int_count[2] - c2_0));
}

/*
 *  割込みレート監視タスク（最低優先度に近い＝アイドル時に回る）．
 *  esp_shim_int_count[1]/[2]をLP_AON STORE4/5へ定期ミラーする．
 *  カウンタ自体はISR文脈で増分されるため，本タスクが飢餓しても
 *  カウンタ増加自体は止まらない（＝ミラーの更新頻度が落ちるだけ）．
 */
void
storm_monitor_task(EXINF exinf)
{
	(void) exinf;

	for (;;) {
		sil_wrw_mem((void *) LP_AON_STORE4, esp_shim_int_count[1]);
		sil_wrw_mem((void *) LP_AON_STORE5, esp_shim_int_count[2]);
		(void) tslp_tsk(200000);	/* 200ms */
	}
}

/*
 *  接続可能アドバタイズ（undirected connectable / general discoverable）を
 *  開始する．sync完了後および切断後に呼ぶ．
 */
static void
start_advertising(void)
{
	struct ble_hs_adv_fields	fields;
	struct ble_gap_adv_params	adv_params;
	int							rc;

	rc = ble_hs_id_infer_auto(0, (uint8_t *) &g_own_addr_type);
	if (rc != 0) {
		g_adv_rc = rc;
		sil_wrw_mem((void *) LP_AON_STORE2, 0xAD000000UL | ((uint32_t) rc & 0xffUL));
		syslog(LOG_ERROR, "ble_host_smoke_c5: id_infer_auto rc=%d", (int_t) rc);
		return;
	}

	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.name = (const uint8_t *) BLE_DEVICE_NAME;
	fields.name_len = (uint8_t) strlen(BLE_DEVICE_NAME);
	fields.name_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		g_adv_rc = rc;
		sil_wrw_mem((void *) LP_AON_STORE2, 0xAD000000UL | ((uint32_t) rc & 0xffUL));
		syslog(LOG_ERROR, "ble_host_smoke_c5: adv_set_fields rc=%d", (int_t) rc);
		return;
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
						   &adv_params, gap_event_cb, NULL);
	g_adv_rc = rc;
	/*  rc確定後に adv-return マーカ（0xAD0000|rc）を書く＝この値の
	 *  存在自体が「adv_startを試行し戻り値を得た」証拠を兼ねる  */
	sil_wrw_mem((void *) LP_AON_STORE2, 0xAD000000UL | ((uint32_t) rc & 0xffUL));

	if (rc == 0) {
		g_adv_active = 1U;
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c5: advertising started as '%s' (own_addr_type=%d)",
			   BLE_DEVICE_NAME, (int_t) g_own_addr_type);
	}
	else {
		syslog(LOG_ERROR, "ble_host_smoke_c5: adv_start rc=%d", (int_t) rc);
	}
}

static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
	(void) arg;
	g_gap_event_count++;

	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		g_gap_conn_count++;
		/*  ★ビルド未検証．D-2c物証：接続イベントをRTCへ記録
		    （JTAG不要read-mem，C3 wip 8476b55と同一フォーマット）  */
		sil_wrw_mem(BLE_CONN_MARK_ADDR,
					0x604E0000UL
					| (((uint32_t) event->connect.status & 0xffUL) << 8)
					| (g_gap_conn_count & 0xffUL));
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c5: GAP CONNECT status=%d handle=%d",
			   (int_t) event->connect.status,
			   (int_t) event->connect.conn_handle);
		if (event->connect.status != 0) {
			g_adv_active = 0U;
			start_advertising();
		}
#ifdef TOPPERS_ESP32C5_BT_SM
		else {
			/*  D-2d：接続ハンドル記録＋接続5秒後 SecReq のための計時開始  */
			g_conn_handle = event->connect.conn_handle;
			g_conn_secs = 0U;
			g_sec_initiated = 0U;
		}
#endif
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		g_gap_disc_count++;
		g_adv_active = 0U;
#ifdef TOPPERS_ESP32C5_BT_SM
		g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
		g_sec_initiated = 0U;
#endif
		/*  ★ビルド未検証（同上）  */
		sil_wrw_mem(BLE_DISC_MARK_ADDR,
					0xD15C0000UL
					| (((uint32_t) event->disconnect.reason & 0xffUL) << 8)
					| (g_gap_disc_count & 0xffUL));
		syslog(LOG_NOTICE, "ble_host_smoke_c5: GAP DISCONNECT reason=%d",
			   (int_t) event->disconnect.reason);
		start_advertising();
		break;
#ifdef TOPPERS_ESP32C5_BT_SM
	case BLE_GAP_EVENT_ENC_CHANGE:
		/*  D-2d：暗号化結果．STORE10 に 0x5DE0<status>（C3 D-2d と同形式）．  */
		sil_wrw_mem((void *) LP_AON_STORE10,
					0x5DE00000UL | ((uint32_t) event->enc_change.status & 0xffUL));
		syslog(LOG_NOTICE, "ble_host_smoke_c5: GAP ENC_CHANGE status=%d",
			   (int_t) event->enc_change.status);
		break;
	case BLE_GAP_EVENT_REPEAT_PAIRING:
		{
			struct ble_gap_conn_desc	desc;

			if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
								  &desc) == 0) {
				ble_store_util_delete_peer(&desc.peer_id_addr);
			}
		}
		return BLE_GAP_REPEAT_PAIRING_RETRY;
	case BLE_GAP_EVENT_PARING_COMPLETE:
		/*  D-2d：SMP完了（status=0成功）．STORE11 に 0x5DC0<status><our><peer>．
		    ★C5初のbond成否判定＝status=0かつour_sec>=1でDUT側bond成立．  */
		{
			int	our_cnt = 0, peer_cnt = 0;

			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &our_cnt);
			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peer_cnt);
			sil_wrw_mem((void *) LP_AON_STORE11,
						0x5DC00000UL
						| (((uint32_t) event->pairing_complete.status & 0xffUL) << 8)
						| (((uint32_t) our_cnt & 0xfUL) << 4)
						| ((uint32_t) peer_cnt & 0xfUL));
			syslog(LOG_NOTICE,
				   "ble_host_smoke_c5: GAP PAIRING_COMPLETE status=%d bonds our=%d peer=%d",
				   (int_t) event->pairing_complete.status,
				   (int_t) our_cnt, (int_t) peer_cnt);
		}
		break;
#endif
	case BLE_GAP_EVENT_ADV_COMPLETE:
		g_adv_active = 0U;
		syslog(LOG_NOTICE, "ble_host_smoke_c5: GAP ADV_COMPLETE reason=%d",
			   (int_t) event->adv_complete.reason);
		break;
	default:
		break;
	}
	return 0;
}

static void
on_sync(void)
{
	g_ble_sync_done = 1U;
	sil_wrw_mem((void *) LP_AON_STORE0, BLE_SYNC_MARK_VAL);
	syslog(LOG_NOTICE, "ble_host_smoke_c5: ble_hs SYNC, host up");

	start_advertising();
}

static void
on_reset(int reason)
{
	g_reset_reason = (int32_t) reason;
	g_reset_count++;
	sil_wrw_mem((void *) LP_AON_STORE3,
				0x5E000000UL | ((g_reset_count & 0xffUL) << 8)
				| ((uint32_t) reason & 0xffUL));
	syslog(LOG_ERROR, "ble_host_smoke_c5: ble_hs RESET, reason=%d", (int_t) reason);
}

static void
ble_host_task(void *param)
{
	(void) param;
	syslog(LOG_NOTICE, "ble_host_smoke_c5: nimble host task started");
	nimble_port_run();		/*  戻らない（nimble_port_stopまで）  */
	nimble_port_freertos_deinit();
}

#ifdef TOPPERS_ESP32C5_BT_SM
/*
 *  ★D-2d：接続5秒後に «未暗号» なら slave Security Request を1回だけ送る
 *  （S3/C3 D-2d 移植．1秒周期ループから呼ぶ）．central にペアリングを開始
 *  させる＝bond のトリガ（暗号必須特性が無くても接続だけで pair 要求が出る）．
 */
static void
bt5_security_tick(void)
{
	struct ble_gap_conn_desc	desc;
	uint16_t					ch = g_conn_handle;
	int							rc;

	if (ch == BLE_HS_CONN_HANDLE_NONE) {
		return;
	}
	g_conn_secs++;
	if (g_conn_secs < 5U || g_sec_initiated != 0U) {
		return;
	}
	g_sec_initiated = 1U;
	if (ble_gap_conn_find(ch, &desc) != 0 || desc.sec_state.encrypted) {
		return;
	}
	rc = ble_gap_security_initiate(ch);
	syslog(LOG_NOTICE,
		   "ble_host_smoke_c5: BT5 security_initiate(slave SecReq) rc=%d", (int_t) rc);
}
#endif /* TOPPERS_ESP32C5_BT_SM */

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	esp_err_t					err;

	(void) exinf;

	esp_shim_initialize();

	/*
	 *  ★ビルド未検証．D-2c準備：GAP接続／切断マーカを既知値(0)へ初期化
	 *  （前回boot残値との混同回避．C3 wip 8476b55と同一の考え方）．
	 */
	sil_wrw_mem(BLE_CONN_MARK_ADDR, 0U);
	sil_wrw_mem(BLE_DISC_MARK_ADDR, 0U);

	/*
	 *  実施13のICGアンロック．esp_bt_controller_init()より前に呼ぶこと
	 *  （coldブート時のPHY初期化ハング対策．bt_smoke_c5と同一手順）．
	 */
	esp_shim_bt_clock_init();

	syslog(LOG_NOTICE, "ble_host_smoke_c5: esp_bt_controller_init");
	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c5: esp_bt_controller_init -> %d",
			   (int_t) err);
		return;
	}
	syslog(LOG_NOTICE, "ble_host_smoke_c5: esp_bt_controller_init OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	syslog(LOG_NOTICE, "ble_host_smoke_c5: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c5: esp_bt_controller_enable -> %d",
			   (int_t) err);
		report_intr_trace();
		report_apm_latch();
		return;
	}
	syslog(LOG_NOTICE, "ble_host_smoke_c5: esp_bt_controller_enable OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	report_intr_trace();
	report_apm_latch();
	report_intr_rate();

	/*
	 *  NimBLEホストスタックを初期化（コントローラは既に起動済みなので
	 *  esp_nimble_initを使い，nimble_port_initは使わない＝コントローラ
	 *  二重初期化を回避．docs/ble-c5c6.md「BLE実施02」と同じ配線）．
	 */
	syslog(LOG_NOTICE, "ble_host_smoke_c5: esp_nimble_init (host)");
	err = esp_nimble_init();
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c5: esp_nimble_init -> %d", (int_t) err);
		return;
	}

	/*
	 *  ★ビルド未検証（ファイル冒頭コメント参照）．D-2c準備の横展開
	 *  （C3 wip 8476b55．docs/ble-c5c6-plan.md「8. D-2c準備の横展開」節，C6版と
	 *  同一の配線）：標準GAP／GATTサービスを登録する．ble_svc_*_init は
	 *  ble_gatts_add_svcsでサービス定義をキューへ積むだけで，実際の
	 *  ATT属性登録は ble_hs_start→ble_gatts_start（ホストタスク側）で
	 *  行われるため，nimble_port_freertos_init より前に呼ぶ．
	 *  esp_nimble_init（=ble_hs_init）済みが前提．
	 */
	ble_svc_gap_init();
	ble_svc_gatt_init();
	{
		int	rc_name = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
		if (rc_name != 0) {
			syslog(LOG_ERROR, "ble_host_smoke_c5: gap_device_name_set rc=%d",
				   (int_t) rc_name);
		}
	}

#ifdef TOPPERS_ESP32C5_BT_SM
	/*
	 *  ★D-2d：SMP（ペアリング／ボンディング）設定（C3/S3 と同一）．
	 *  bond store を先に初期化（未初期化だと Pairing Request 直後に
	 *  ble_store_read=ENOTSUP で即失敗．S3 §5）．Just Works / SC．
	 */
	ble_store_config_init();
	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
	ble_hs_cfg.sm_bonding = 1;
	ble_hs_cfg.sm_mitm = 0;
	ble_hs_cfg.sm_sc = 1;
	ble_hs_cfg.sm_our_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
	ble_hs_cfg.sm_their_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#endif

	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	syslog(LOG_NOTICE, "ble_host_smoke_c5: nimble_port_freertos_init");
	nimble_port_freertos_init(ble_host_task);

	syslog(LOG_NOTICE, "ble_host_smoke_c5: init done, waiting for ble_hs SYNC");

	{
		int	retry;
		for (retry = 0; retry < 200 && g_ble_sync_done == 0U; retry++) {
			(void) tslp_tsk(100000);	/* 100ms */
		}
		if (g_ble_sync_done != 0U) {
			syslog(LOG_NOTICE, "ble_host_smoke_c5: Phase D-2a milestone reached (sync)");
		}
		else {
			syslog(LOG_NOTICE, "ble_host_smoke_c5: no SYNC yet (check STORE0 on HW)");
		}
	}

	/*
	 *  sync後さらに数秒待ち，adv-startの結果（on_sync内で試行済み）を
	 *  報告する．割込みレートも再度観測（storm非発生の確認）．
	 */
	(void) tslp_tsk(3000000);	/* 3s */
	syslog(LOG_NOTICE, "ble_host_smoke_c5: g_adv_rc=%d g_adv_active=%d",
		   (int_t) g_adv_rc, (int_t) g_adv_active);
	report_intr_rate();

	syslog(LOG_NOTICE, "ble_host_smoke_c5: done (host task continues in background)");
#ifdef TOPPERS_ESP32C5_BT_SM
	/*
	 *  ★D-2d：定常ループ（1秒周期）．接続5秒後に slave Security Request を
	 *  送って bond をトリガする（bt5_security_tick）．main_task を返さず保持＝
	 *  adv/接続/pairing を無期限に続ける（実機で bond を追試できる本番形）．
	 */
	for (;;) {
		bt5_security_tick();
		(void) tslp_tsk(1000000);	/* 1s */
	}
#endif
}
