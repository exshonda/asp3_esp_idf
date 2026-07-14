/*
 *  NimBLE host スモークテスト（ESP32-C6．Phase D-2a／BLE実施02）
 *
 *  apps/bt_smoke_c6（D-1）で確立したBLEコントローラ起動シーケンスの上に
 *  NimBLEホストスタックを載せる．狙いはble_hsのsyncコールバック到達
 *  （＝ホストがコントローラと同期し起動完了）．届けば接続可能な
 *  アドバタイズ（D-2b）も試す．
 *
 *  C3のapps/ble_host_smokeとの違い：
 *    - esp_bt_controller_config_tはC6/C5世代の新形状．手書きで全
 *      フィールドを埋めるとemi.c:164の教訓（値の取り違え）を再び踏む
 *      リスクがあるため，bt_smoke_c6と同じくBT_CONTROLLER_INIT_
 *      CONFIG_DEFAULT()マクロを使う（esp_bt.cmakeのCONFIG_BT_LE_*群を
 *      前提に正しく展開される設計．docs/ble-c5c6.md「BLE実施01」）．
 *    - esp_shim_bt_clock_init()（実施91のICGアンロック）をコントローラ
 *      初期化より前に呼ぶ（D-1と同じ．C3には無い手順）．
 *    - HCIトランスポートはVHCI（esp_vhci_host_*）ではなく，NimBLE ON時
 *      にesp_bt.cmakeが差し替えるhci_driver_nimble.c＋hci_esp_ipc.c
 *      経由（blob内部のr_ble_hci_trans_*／ble_transport_to_hs_*）．
 *      アプリからは直接触らない（esp_nimble_init()内部で配線される）．
 *    - HRT/SYSTIMER凍結検証プローブ（C3のHRT_PROBE．CPU飽和仮説は
 *      C3側で既に決着済み）は持ち込まない．割込みレート監視のみ
 *      最小限で持つ（storm_monitor_task）．
 *
 *  ホスト初期化の配線（C3と同じ設計判断）：
 *    nimble_port_init()は内部でesp_bt_controller_init/enableを
 *    BT_CONTROLLER_INIT_CONFIG_DEFAULT()で行うため，二重初期化を
 *    避けるためnimble_port_init()は使わず，
 *      esp_bt_controller_init(&cfg)
 *      esp_bt_controller_enable(BLE)
 *      esp_nimble_init()            （ホストのみ．C6ではesp_nimble_
 *                                     hci_init()呼出しはコンパイル
 *                                     アウトされる＝新トランスポート
 *                                     はble_transport_ll_init()経由で
 *                                     別途配線される．docs/ble-c5c6.md
 *                                     「BLE実施02」）
 *      ble_hs_cfg.sync_cb = on_sync
 *      nimble_port_freertos_init(ble_host_task)
 *    とする．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "ble_host_smoke_c6.h"

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
 *  実施91のICGアンロックをBTクロック初期化の一部として呼ぶ（bt/bt_shim.c，
 *  bt_smoke_c6と同一）．esp_bt_controller_init()より前に呼ぶこと．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  多重登録トレース（bt/bt_shim.cのesp_os_intr_allocが記録．
 *  bt_smoke_c6と同一アドレス／同一フォーマット）．
 */
#define BT_INTR_TRACE_REG	0x600B101CUL

/*
 *  HP_APM M0-M3例外ラッチ（bt_smoke_c6と同一）．
 */
#define HP_APM_M0_STATCLR	0x600990CCUL

/*
 *  WiFi/BT共有の割込みディスパッチカウンタ（wifi/esp_shim.c）．
 *  ストーム監視に使う（bt_smoke_c6と同一の仕組み）．
 */
extern volatile uint32_t esp_shim_int_count[];

/*
 *  LP_AON STORE系（usb-reset生存．docs/wifi-shim-c6.md実測で確認済み）．
 *  BT_INTR_TRACE_REG（bt_shim.c）がSTORE7（+0x1C）を使用済み，STORE1
 *  （+0x04）はノイズと記録済みのためどちらも避ける．
 */
#define LP_AON_STORE0		0x600B1000UL	/* sync マーカ */
#define LP_AON_STORE2		0x600B1008UL	/* adv開始マーカ */
#define LP_AON_STORE3		0x600B100CUL	/* adv-return (rc) マーカ */
#define LP_AON_STORE4		0x600B1010UL	/* 割込みレート：CPU線1累積ミラー */
#define LP_AON_STORE5		0x600B1014UL	/* 割込みレート：CPU線2累積ミラー */
#define LP_AON_STORE6		0x600B1018UL	/* ble_hs_cfg.reset_cb reason/count */
/*
 *  D-2c準備（C3 wip 8476b55の横展開，docs/ble-c5c6-plan.md「8. D-2c準備の横展開」）：
 *  GAP接続／切断マーカ．STORE0/2/3/4/5/6/7は実施02で使用済み，STORE1は
 *  ノイズ（実測既知）につき使用禁止のため，未使用のSTORE8/9
 *  （`hal/components/soc/esp32c6/register/soc/lp_aon_reg.h`で実在確認済み，
 *  他のC6 BLE計装との衝突なし）を新規に明け渡す．C3のようなSTORE転用
 *  （storm probe停止による明け渡し）ではなく，元から空いているレジスタを
 *  使う点がC3との違い．フォーマットはC3のwipと同一：
 *  connect: 0x604E<status:8><conn_count:8>／
 *  disconnect: 0xD15C<reason:8><disc_count:8>．
 */
#define LP_AON_STORE8		0x600B1020UL	/* GAP CONNECT マーカ */
#define LP_AON_STORE9		0x600B1024UL	/* GAP DISCONNECT マーカ */
#define BLE_CONN_MARK_ADDR	((void *) LP_AON_STORE8)
#define BLE_DISC_MARK_ADDR	((void *) LP_AON_STORE9)

#define BLE_SYNC_MARK_VAL	0x5ADE51C0UL
#define BLE_ADV_MARK_VAL	0x0ADE5000UL

/*
 *  ble_hs sync 到達マーカ（JTAG/esptool read-memで容易に観測できるよう
 *  グローバルとLP_AON STORE0の両方へ記録する）．
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

#define BLE_DEVICE_NAME		"ASP3-C6-BLE"

static int gap_event_cb(struct ble_gap_event *event, void *arg);

static void
report_intr_trace(void)
{
	uint32_t	trace = sil_rew_mem((const uint32_t *) BT_INTR_TRACE_REG);

	if ((trace >> 24) == 0xA1U) {
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c6: intr trace = 0x%08x (nalloc=%d src1=%d src2=%d)",
			   (int_t) trace, (int_t)((trace >> 16) & 0xFFU),
			   (int_t)((trace >> 8) & 0xFFU), (int_t)(trace & 0xFFU));
	}
	else {
		syslog(LOG_NOTICE, "ble_host_smoke_c6: intr trace not recorded (0x%08x)",
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
			syslog(LOG_ERROR, "ble_host_smoke_c6: HP_APM M%d exception latch SET (0x%08x)",
				   (int_t) i, (int_t) v);
		}
	}
	if (!any) {
		syslog(LOG_NOTICE, "ble_host_smoke_c6: HP_APM M0-M3 exception latch clear");
	}
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
		syslog(LOG_ERROR, "ble_host_smoke_c6: id_infer_auto rc=%d", (int_t) rc);
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
		syslog(LOG_ERROR, "ble_host_smoke_c6: adv_set_fields rc=%d", (int_t) rc);
		return;
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	sil_wrw_mem((void *) LP_AON_STORE2, BLE_ADV_MARK_VAL);	/* 開始試行マーカ（rc確定前） */

	rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
						   &adv_params, gap_event_cb, NULL);
	g_adv_rc = rc;
	sil_wrw_mem((void *) LP_AON_STORE3, 0xAD000000UL | ((uint32_t) rc & 0xffUL));

	if (rc == 0) {
		g_adv_active = 1U;
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c6: advertising started as '%s' (own_addr_type=%d)",
			   BLE_DEVICE_NAME, (int_t) g_own_addr_type);
	}
	else {
		syslog(LOG_ERROR, "ble_host_smoke_c6: adv_start rc=%d", (int_t) rc);
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
		/*  D-2c物証：接続イベントをRTCへ記録（JTAG不要read-mem，
		    C3 wip 8476b55と同一フォーマット）  */
		sil_wrw_mem(BLE_CONN_MARK_ADDR,
					0x604E0000UL
					| (((uint32_t) event->connect.status & 0xffUL) << 8)
					| (g_gap_conn_count & 0xffUL));
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c6: GAP CONNECT status=%d handle=%d",
			   (int_t) event->connect.status,
			   (int_t) event->connect.conn_handle);
		if (event->connect.status != 0) {
			g_adv_active = 0U;
			start_advertising();
		}
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		g_gap_disc_count++;
		g_adv_active = 0U;
		sil_wrw_mem(BLE_DISC_MARK_ADDR,
					0xD15C0000UL
					| (((uint32_t) event->disconnect.reason & 0xffUL) << 8)
					| (g_gap_disc_count & 0xffUL));
		syslog(LOG_NOTICE, "ble_host_smoke_c6: GAP DISCONNECT reason=%d",
			   (int_t) event->disconnect.reason);
		start_advertising();
		break;
	case BLE_GAP_EVENT_ADV_COMPLETE:
		g_adv_active = 0U;
		syslog(LOG_NOTICE, "ble_host_smoke_c6: GAP ADV_COMPLETE reason=%d",
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
	syslog(LOG_NOTICE, "ble_host_smoke_c6: ble_hs SYNC, host up");

	start_advertising();
}

static void
on_reset(int reason)
{
	g_reset_reason = (int32_t) reason;
	g_reset_count++;
	sil_wrw_mem((void *) LP_AON_STORE6,
				0x5E000000UL | ((g_reset_count & 0xffUL) << 8)
				| ((uint32_t) reason & 0xffUL));
	syslog(LOG_ERROR, "ble_host_smoke_c6: ble_hs RESET, reason=%d", (int_t) reason);
}

static void
ble_host_task(void *param)
{
	(void) param;
	syslog(LOG_NOTICE, "ble_host_smoke_c6: nimble host task started");
	nimble_port_run();		/*  戻らない（nimble_port_stopまで）  */
	nimble_port_freertos_deinit();
}

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
	esp_err_t					err;

	(void) exinf;

	esp_shim_initialize();

	/*
	 *  D-2c準備：GAP接続／切断マーカを既知値(0)へ初期化（前回boot残値との
	 *  混同回避．watchdog-resetはRTC domainを消さないため明示クリア．
	 *  C3 wip 8476b55と同一の考え方）．
	 */
	sil_wrw_mem(BLE_CONN_MARK_ADDR, 0U);
	sil_wrw_mem(BLE_DISC_MARK_ADDR, 0U);

	/*
	 *  実施91のICGアンロック．esp_bt_controller_init()より前に呼ぶこと
	 *  （coldブート時のPHY初期化ハング対策．bt_smoke_c6と同一手順）．
	 */
	esp_shim_bt_clock_init();

	syslog(LOG_NOTICE, "ble_host_smoke_c6: esp_bt_controller_init");
	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c6: esp_bt_controller_init -> %d",
			   (int_t) err);
		return;
	}
	syslog(LOG_NOTICE, "ble_host_smoke_c6: esp_bt_controller_init OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	syslog(LOG_NOTICE, "ble_host_smoke_c6: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c6: esp_bt_controller_enable -> %d",
			   (int_t) err);
		report_intr_trace();
		report_apm_latch();
		return;
	}
	syslog(LOG_NOTICE, "ble_host_smoke_c6: esp_bt_controller_enable OK (heap free=%u)",
		   (uint_t) esp_shim_heap_free_size());

	report_intr_trace();
	report_apm_latch();

	/*
	 *  NimBLEホストスタックを初期化（コントローラは既に起動済みなので
	 *  esp_nimble_initを使い，nimble_port_initは使わない＝コントローラ
	 *  二重初期化を回避．docs/ble-c5c6.md「BLE実施02」）．
	 */
	syslog(LOG_NOTICE, "ble_host_smoke_c6: esp_nimble_init (host)");
	err = esp_nimble_init();
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke_c6: esp_nimble_init -> %d", (int_t) err);
		return;
	}

	/*
	 *  D-2c準備の横展開（C3 wip 8476b55．docs/ble-c5c6-plan.md「8. D-2c
	 *  準備の横展開」節）：標準GAP／GATTサービスを登録する（接続後にホストから
	 *  サービスディスカバリで見えるようにするため）．ble_svc_*_init は
	 *  ble_gatts_add_svcsでサービス定義をキューへ積むだけで，実際の
	 *  ATT属性登録は ble_hs_start→ble_gatts_start（ホストタスク側）で
	 *  行われる．したがって nimble_port_freertos_init より前に呼ぶ．
	 *  esp_nimble_init（=ble_hs_init）済みが前提．
	 */
	ble_svc_gap_init();
	ble_svc_gatt_init();
	{
		int	rc_name = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
		if (rc_name != 0) {
			syslog(LOG_ERROR, "ble_host_smoke_c6: gap_device_name_set rc=%d",
				   (int_t) rc_name);
		}
	}

	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	syslog(LOG_NOTICE, "ble_host_smoke_c6: nimble_port_freertos_init");
	nimble_port_freertos_init(ble_host_task);

	syslog(LOG_NOTICE, "ble_host_smoke_c6: init done, waiting for ble_hs SYNC");

	{
		int	retry;
		for (retry = 0; retry < 100 && g_ble_sync_done == 0U; retry++) {
			(void) tslp_tsk(100000);	/* 100ms */
		}
		if (g_ble_sync_done != 0U) {
			syslog(LOG_NOTICE, "ble_host_smoke_c6: Phase D-2a milestone reached (sync)");
		}
		else {
			syslog(LOG_NOTICE, "ble_host_smoke_c6: no SYNC yet (check on HW)");
		}
	}

	/*
	 *  sync後さらに数秒待ち，adv-startの結果（on_sync内で試行済み）を
	 *  報告する．
	 */
	(void) tslp_tsk(3000000);	/* 3s */
	syslog(LOG_NOTICE, "ble_host_smoke_c6: g_adv_rc=%d g_adv_active=%d",
		   (int_t) g_adv_rc, (int_t) g_adv_active);

	syslog(LOG_NOTICE, "ble_host_smoke_c6: done (host task continues in background)");
}
