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
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/*
 *  実施91のICGアンロックをBTクロック初期化の一部として呼ぶ（bt/bt_shim.c，
 *  bt_smoke_c6と同一）．esp_bt_controller_init()より前に呼ぶこと．
 */
extern void esp_shim_bt_clock_init(void);

/*
 *  ★C6 E_CTX修正（C3 f9dae7dのC6移植．wifi/esp_shim.c）：保留リング
 *  (pend_ring)・保留セマフォgiveの周期flush用（下記main_task定常ループ
 *  から呼ぶ）．保留0なら即return＝非回帰．
 */
extern void esp_shim_queue_flush_pending(void);
extern void esp_shim_sem_flush_pending(void);

#ifdef TOPPERS_ESP32C6_BT_SM
/*
 *  D-2d(SM)：bond store初期化（C3/C5と同じ．S3 BT-5が正）．ble_store_ram.c
 *  はIDF文脈（BLE_USED_IN_IDF=1）で空になるため使えず，ble_store_config
 *  （BLE_STORE_CONFIG_PERSIST=0＝RAM保持）を使う．公開ヘッダが無いので
 *  自前externする（stock bleprphも同様）．
 */
extern void ble_store_config_init(void);
#endif

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

/*
 *  D-2c/D-2d本体（C3 1190be9/369a86aのC6移植．docs/ble-c5c6-plan.md
 *  「D-2c/D-2d本体のC6移植」）：C6のLP_AON STORE0-9は実施02〜本ラウンドで
 *  10個全て割当て済み（STORE0 sync/STORE1 noise回避/STORE2 adv試行/
 *  STORE3 adv-return rc/STORE4-5 割込みレートミラー/STORE6 reset_cb/
 *  STORE7 intr_allocトレース/STORE8-9 CONNECT・DISCONNECT）＝新規の
 *  空きレジスタは無い．C5が2884922でSTORE10/11非実在と判明しSTORE6を
 *  ENC/PAIRING共用へ切替えた前例（タグで判別する設計）に倣い，C6でも
 *  «書込み頻度が低い既存レジスタ»を«タグ判別»で共用する：
 *    - STORE3（adv-return rc，タグ0xAD00xx）：接続中はstart_advertising()
 *      が呼ばれない（再adv契機は切断/adv失敗のみ）ため接続中は不変．
 *      WRITE特性(0xABF3)受信マーカ（タグ0x7717）を共用させる．
 *    - STORE6（reset_cb，タグ0x5E00xxxx）：ble_hsリセットは稀（本ビルドの
 *      通常経路では発火しない）．ENC_CHANGEマーカ（タグ0x5DE0）を共用．
 *    - STORE7（intr_allocトレース，タグ0xA1xxxxxx．bt_shim.c，controller
 *      init時のみ書込み，以後セッション中は不変．C3のBLE_PAIR_MARK_ADDR
 *      が0x54＝同種の«init後は不変»レジスタを選んだのと同じ理由）：
 *      PAIRING_COMPLETEマーカ（タグ0x5DC0）を共用．
 *  いずれも上位ニブル/バイトでタグが異なるため誤読の恐れは無い．
 */
#define BLE_WRITE_MARK_ADDR	((void *) LP_AON_STORE3)	/* 0xABF3 write受信（STORE3共用） */
#ifdef TOPPERS_ESP32C6_BT_SM
#define LP_AON_STORE_ENC	LP_AON_STORE6		/* ENC_CHANGE（STORE6=reset_cb共用） */
#define LP_AON_STORE_PAIR	BT_INTR_TRACE_REG	/* PAIRING_COMPLETE（STORE7=intr_allocトレース共用） */
#endif

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

/*
 *  D-2c/D-2d：自前GATTサービス＋notify状態（C3 1190be9のC6移植）．S3の
 *  BT-5（notify/subscribe追跡・SM）を«動作済みの正»としてC3が逐語移植し
 *  たものをさらにC6へ移植する．16bit UUID：service 0xABF0／
 *  chr 0xABF1(READ,"BT4-OK")／0xABF2(READ|NOTIFY,32bit LEカウンタ)／
 *  0xABF3(WRITE．受信値をログ＋RTCマーカ＋putカウンタ)．notify送出は
 *  main_taskの定常ループ（1秒周期・アプリタスク文脈）からsubscribe中の
 *  み行う．main_task文脈での送出はC6へ移植したesp_shim.cの保留リングが
 *  クリティカルセクション内送出を救済する．
 */
volatile uint16_t	g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
volatile uint8_t	g_notify_enabled;	/* CCCD notifyビット */
static uint16_t		g_notify_val_handle;	/* notify特性のvalue handle */
volatile uint32_t	g_notify_counter;	/* notify値（32bit LE） */
volatile uint32_t	g_notify_sent;		/* 送出成功回数 */
volatile uint32_t	g_notify_fail;		/* 送出失敗回数 */
volatile int32_t	g_notify_last_rc;	/* 最後の失敗rc */
volatile uint32_t	g_write_count;		/* write特性の受信回数（putカウンタ） */
volatile uint32_t	g_write_last;		/* 直近writeの先頭バイト＋長さ */
#ifdef TOPPERS_ESP32C6_BT_SM
volatile uint32_t	g_conn_secs;		/* 接続経過秒（SecReq遅延用．S3 BT-5移植） */
volatile uint8_t	g_sec_initiated;	/* slave Security Request送出済みフラグ */
#endif

#define BLE_DEVICE_NAME		"ASP3-C6-BLE"

static int gap_event_cb(struct ble_gap_event *event, void *arg);

/*
 *  D-2c/D-2d：自前GATTサービス（S3 BT-5→C3 1190be9を«正»として逐語移植）．
 *    service 0xABF0（PRIMARY）
 *      chr 0xABF1  READ            固定値"BT4-OK"（42 54 34 2d 4f 4b）
 *      chr 0xABF2  READ | NOTIFY   32bit LEカウンタ（subscribe中のみ周期送出）
 *      chr 0xABF3  WRITE           受信値をログ＋RTCマーカ＋putカウンタ
 */
static int
gatt_read_access(uint16_t conn_handle, uint16_t attr_handle,
				 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	static const char	val[] = "BT4-OK";

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		return (os_mbuf_append(ctxt->om, val, sizeof(val) - 1) == 0
				? 0 : BLE_ATT_ERR_INSUFFICIENT_RES);
	}
	return BLE_ATT_ERR_UNLIKELY;
}

/*  notify特性（0xABF2）のREAD：現在のカウンタ値（notifyペイロードと同形式）  */
static int
gatt_notify_access(uint16_t conn_handle, uint16_t attr_handle,
				   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	uint32_t	v = g_notify_counter;
	uint8_t		buf[4];

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		buf[0] = (uint8_t) (v & 0xffU);
		buf[1] = (uint8_t) ((v >> 8) & 0xffU);
		buf[2] = (uint8_t) ((v >> 16) & 0xffU);
		buf[3] = (uint8_t) ((v >> 24) & 0xffU);
		return (os_mbuf_append(ctxt->om, buf, sizeof(buf)) == 0
				? 0 : BLE_ATT_ERR_INSUFFICIENT_RES);
	}
	return BLE_ATT_ERR_UNLIKELY;
}

/*  write特性（0xABF3）：受信データをログ＋RTCマーカ＋putカウンタ加算  */
static int
gatt_write_access(uint16_t conn_handle, uint16_t attr_handle,
				  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	uint16_t	len;
	uint8_t		first = 0U;

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		len = OS_MBUF_PKTLEN(ctxt->om);
		if (len > 0U) {
			(void) os_mbuf_copydata(ctxt->om, 0, 1, &first);
		}
		g_write_count++;
		g_write_last = ((uint32_t) len << 8) | (uint32_t) first;
		sil_wrw_mem(BLE_WRITE_MARK_ADDR,
					0x77170000UL
					| ((g_write_count & 0xffUL) << 8)
					| ((uint32_t) first & 0xffUL));
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c6: GATT WRITE len=%d first=0x%02x count=%d",
			   (int_t) len, (int_t) first, (int_t) g_write_count);
		return 0;
	}
	return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def custom_chrs[] = {
	{
		.uuid = BLE_UUID16_DECLARE(0xABF1),
		.access_cb = gatt_read_access,
		.flags = BLE_GATT_CHR_F_READ,
	},
	{
		.uuid = BLE_UUID16_DECLARE(0xABF2),
		.access_cb = gatt_notify_access,
		.val_handle = &g_notify_val_handle,
		.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
	},
	{
		.uuid = BLE_UUID16_DECLARE(0xABF3),
		.access_cb = gatt_write_access,
		.flags = BLE_GATT_CHR_F_WRITE,
	},
#ifdef TOPPERS_ESP32C6_BT_SM
	/*  D-2d(bond確認)：暗号必須READ特性（0xABF4）．未ペアのcentralがこれを
	    READすると NimBLE が insufficient-authentication を返し，central が
	    pairing/bondingを«決定論的に»開始する（C3 1190be9と同一の入口）．  */
	{
		.uuid = BLE_UUID16_DECLARE(0xABF4),
		.access_cb = gatt_read_access,
		.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
	},
#endif
	{ 0 }	/* 終端 */
};

static const struct ble_gatt_svc_def custom_svcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(0xABF0),
		.characteristics = custom_chrs,
	},
	{ 0 }	/* 終端 */
};

/*  subscribe中なら1回notifyを送る（1秒周期ループから呼ぶ．S3 bt5_notify_tick）  */
static void
notify_tick(void)
{
	struct os_mbuf	*om;
	uint32_t		v;
	uint8_t			buf[4];
	int				rc;

	if (g_notify_enabled == 0U || g_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
		return;
	}
	v = ++g_notify_counter;
	buf[0] = (uint8_t) (v & 0xffU);
	buf[1] = (uint8_t) ((v >> 8) & 0xffU);
	buf[2] = (uint8_t) ((v >> 16) & 0xffU);
	buf[3] = (uint8_t) ((v >> 24) & 0xffU);
	om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
	if (om == NULL) {
		g_notify_fail++;
		g_notify_last_rc = -1;
		syslog(LOG_ERROR, "ble_host_smoke_c6: notify mbuf alloc fail (v=%u)",
			   (unsigned) v);
		return;
	}
	rc = ble_gatts_notify_custom(g_conn_handle, g_notify_val_handle, om);
	if (rc == 0) {
		g_notify_sent++;
	}
	else {
		g_notify_fail++;
		g_notify_last_rc = rc;
		syslog(LOG_ERROR, "ble_host_smoke_c6: notify rc=%d (v=%u)",
			   (int_t) rc, (unsigned) v);
	}
}

#ifdef TOPPERS_ESP32C6_BT_SM
/*
 *  D-2d(bond)：接続経過秒を加算し，接続5秒後に«未暗号»ならslave Security
 *  Requestを1回だけ送る（1秒周期ループから呼ぶ．C5 369a86a／S3 BT-5
 *  bt5_security_tick を逐語移植）．
 */
static void
bt6_security_tick(void)
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
		return;		/*  既に暗号化済みなら不要  */
	}
	rc = ble_gap_security_initiate(ch);
	syslog(LOG_NOTICE,
		   "ble_host_smoke_c6: BT6 security_initiate(slave SecReq) rc=%d", (int_t) rc);
}
#endif /* TOPPERS_ESP32C6_BT_SM */

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
		if (event->connect.status == 0) {
			/*  D-2c/D-2d：notify送出先の接続ハンドルを記録  */
			g_conn_handle = event->connect.conn_handle;
#ifdef TOPPERS_ESP32C6_BT_SM
			/*  D-2d(bond)：接続5秒後にslave SecReqを送るための計時開始  */
			g_conn_secs = 0U;
			g_sec_initiated = 0U;
#endif
		}
		else {
			g_adv_active = 0U;
			start_advertising();
		}
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		g_gap_disc_count++;
		g_adv_active = 0U;
		g_conn_handle = BLE_HS_CONN_HANDLE_NONE;	/* D-2c/D-2d */
		g_notify_enabled = 0U;						/* D-2c/D-2d */
#ifdef TOPPERS_ESP32C6_BT_SM
		g_sec_initiated = 0U;						/* D-2d(bond) */
#endif
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
	case BLE_GAP_EVENT_SUBSCRIBE:
		/*  D-2c/D-2d：0xABF2のCCCD変化を追跡（attr_handle==val_handle）．
		    cur_notifyをそのまま反映（on/off両対応．C3 1190be9と同一）．  */
		syslog(LOG_NOTICE,
			   "ble_host_smoke_c6: GAP SUBSCRIBE attr=%d cur_notify=%d reason=%d",
			   (int_t) event->subscribe.attr_handle,
			   (int_t) event->subscribe.cur_notify,
			   (int_t) event->subscribe.reason);
		if (event->subscribe.attr_handle == g_notify_val_handle) {
			g_notify_enabled = (uint8_t) event->subscribe.cur_notify;
		}
		break;
	case BLE_GAP_EVENT_MTU:
		syslog(LOG_NOTICE, "ble_host_smoke_c6: GAP MTU value=%d",
			   (int_t) event->mtu.value);
		break;
#ifdef TOPPERS_ESP32C6_BT_SM
	case BLE_GAP_EVENT_ENC_CHANGE:
		/*  D-2d(SM)：SMPペアリング/暗号化の結果（status=0が成功）．
		    STORE6共用（reset_cbタグ0x5E00とは上位バイトで判別）：
		    tag 0x5DE0＝ENC_CHANGE到達，下位バイト=status．  */
		sil_wrw_mem((void *) LP_AON_STORE_ENC,
					0x5DE00000UL | ((uint32_t) event->enc_change.status & 0xffUL));
		syslog(LOG_NOTICE, "ble_host_smoke_c6: GAP ENC_CHANGE status=%d",
			   (int_t) event->enc_change.status);
		{
			struct ble_gap_conn_desc	desc;

			if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
				syslog(LOG_NOTICE,
					   "ble_host_smoke_c6: sec_state enc=%d auth=%d bond=%d keysz=%d",
					   (int_t) desc.sec_state.encrypted,
					   (int_t) desc.sec_state.authenticated,
					   (int_t) desc.sec_state.bonded,
					   (int_t) desc.sec_state.key_size);
			}
		}
		break;
	case BLE_GAP_EVENT_PASSKEY_ACTION:
		/*  IO=NoInputNoOutput（Just Works）では来ない想定．来たらログのみ  */
		syslog(LOG_NOTICE, "ble_host_smoke_c6: GAP PASSKEY_ACTION action=%d",
			   (int_t) event->passkey.params.action);
		break;
	case BLE_GAP_EVENT_REPEAT_PAIRING:
		/*  central側が既存bondを無視して再ペアリング＝旧bondを消しRETRY
		    （stock bleprphと同型．C3/S3 BT-5と同一）  */
		{
			struct ble_gap_conn_desc	desc;

			if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
								  &desc) == 0) {
				ble_store_util_delete_peer(&desc.peer_id_addr);
			}
			syslog(LOG_NOTICE,
				   "ble_host_smoke_c6: GAP REPEAT_PAIRING -> deleted old bond, retry");
		}
		return BLE_GAP_REPEAT_PAIRING_RETRY;
	case BLE_GAP_EVENT_PARING_COMPLETE:
		/*  SMP手続き完了（status=0成功／BLE hostエラーコード）．STORE7共用
		    （intr_allocトレースのタグ0xA1とは上位バイトで判別）：
		    tag 0x5DC0＝PAIRING_COMPLETE．DUT側bond store件数
		    （our_sec/peer_sec）をパックしbond成立可否を判定する．  */
		{
			int	our_cnt = 0, peer_cnt = 0;

			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &our_cnt);
			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peer_cnt);
			sil_wrw_mem((void *) LP_AON_STORE_PAIR,
						0x5DC00000UL
						| (((uint32_t) event->pairing_complete.status & 0xffUL) << 8)
						| (((uint32_t) our_cnt & 0xfUL) << 4)
						| ((uint32_t) peer_cnt & 0xfUL));
			syslog(LOG_NOTICE,
				   "ble_host_smoke_c6: GAP PAIRING_COMPLETE status=%d bonds our=%d peer=%d",
				   (int_t) event->pairing_complete.status,
				   (int_t) our_cnt, (int_t) peer_cnt);
		}
		break;
#endif /* TOPPERS_ESP32C6_BT_SM */
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

	/*
	 *  ★クロスカーネル・ハンドオフ判定用（docs/ble-c5c6-plan.md §19）：
	 *  main_task最初の実行文としてsynth-lock status(0x600a00cc)を読む．
	 *  Direct Bootでは常に0x25824e50(bit8=0)．stockからのジャンプ直後に
	 *  ここで既にbit8=1が見えれば「物理PLLロックはジャンプを跨いで
	 *  生存する」ことの直接証拠になる（ASP3が何か書く前の値）．
	 */
	syslog(LOG_NOTICE, "ble_host_smoke_c6: HANDOFF entry synth(0x600a00cc)=0x%08x",
		   (uint_t) *(volatile uint32_t *) 0x600a00ccU);

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
	 *  D-2c本体（C3 1190be9のC6移植）：標準GAP／GATTサービスに加え，
	 *  自前サービス0xABF0（read/notify/write）を登録する（接続後にホスト
	 *  からサービスディスカバリで見えるようにするため）．ble_svc_*_init／
	 *  ble_gatts_add_svcsはサービス定義をキューへ積むだけで，実際の
	 *  ATT属性登録は ble_hs_start→ble_gatts_start（ホストタスク側）で
	 *  行われる．したがって nimble_port_freertos_init より前に呼ぶ．
	 *  esp_nimble_init（=ble_hs_init）済みが前提．
	 */
#ifdef TOPPERS_ESP32C6_BT_SM
	/*
	 *  D-2d(SM)：bond storeを初期化してからSMパラメータを設定する
	 *  （S3 BT-5 §5：store未初期化だとPairing Request直後に
	 *  ble_sm_chk_store_overflow→ble_store_readがENOTSUPで即Pairing
	 *  Failedになる．C3/C5と同一の対策）．
	 */
	ble_store_config_init();

	/*  Just Works / Secure Connections（C3/C5 D-2dと同一設定）．IO=NoIO・
	    bonding=1・MITM=0・SC=1．鍵配布はENC|ID（bond再利用＋IRK交換）．  */
	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
	ble_hs_cfg.sm_bonding = 1;
	ble_hs_cfg.sm_mitm = 0;
	ble_hs_cfg.sm_sc = 1;
	ble_hs_cfg.sm_our_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
	ble_hs_cfg.sm_their_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#endif /* TOPPERS_ESP32C6_BT_SM */

	ble_svc_gap_init();
	ble_svc_gatt_init();
	{
		int	rc;

		/*  D-2c：自前サービス0xABF0（0xABF1 READ／0xABF2 NOTIFY／
		    0xABF3 WRITE）を登録．count_cfgでATT属性数を予約→add_svcsで
		    キューへ積む（実登録はble_hs_start→ble_gatts_start）．  */
		rc = ble_gatts_count_cfg(custom_svcs);
		if (rc == 0) {
			rc = ble_gatts_add_svcs(custom_svcs);
		}
		if (rc != 0) {
			syslog(LOG_ERROR, "ble_host_smoke_c6: gatts svc reg rc=%d", (int_t) rc);
		}
		rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
		if (rc != 0) {
			syslog(LOG_ERROR, "ble_host_smoke_c6: gap_device_name_set rc=%d",
				   (int_t) rc);
		}
	}

	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;

	syslog(LOG_NOTICE, "ble_host_smoke_c6: nimble_port_freertos_init");
	nimble_port_freertos_init(ble_host_task);

	syslog(LOG_NOTICE, "ble_host_smoke_c6: init done, waiting for ble_hs SYNC");

	{
		int	retry;
		for (retry = 0; retry < 200 && g_ble_sync_done == 0U; retry++) {
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

	syslog(LOG_NOTICE, "ble_host_smoke_c6: entering steady loop (notify tick)");

	/*
	 *  D-2c/D-2d：定常ループ（1秒周期・アプリタスク文脈）．subscribe中の
	 *  みnotifyを1回送る（notify_tick）．SM有効時は接続5秒後にslave
	 *  Security Requestも送る（bt6_security_tick）．main_taskを返さず
	 *  advertising/接続/notify/pairingを無期限に続ける（C3 1190be9/
	 *  369a86aと同一の設計．ユーザーがスマホで追試できるようadv継続の
	 *  本番ビルドを残す）．ログは60秒毎のハートビートのみ．
	 */
	{
		uint32_t	sec = 0U;
		uint32_t	sub = 0U;

		for (;;) {
			/*
			 *  ★C6 E_CTX修正（安全網）：保留リング(pend_ring)に滞留した
			 *  ACL RXを«100ms周期»でflushする（C3 f9dae7d/1190be9と同一の
			 *  設計判断．暗号化後のSMP PDUがE_CTXフォールバックで
			 *  pend_ringへ退避された後，以後キュー交通が途絶えると
			 *  exit_critical/queue-opの機会的flushが走らず滞留する
			 *  リスクへの保険）．独立タスク(main_task)から高頻度flush
			 *  すれば滞留が≤100msで解ける．pend残0なら即return＝非回帰．
			 */
			esp_shim_queue_flush_pending();
			esp_shim_sem_flush_pending();
			(void) tslp_tsk(100000);	/* 100ms */
			if (++sub < 10U) {
				continue;		/* notify/security/HBは1秒毎 */
			}
			sub = 0U;
			notify_tick();
#ifdef TOPPERS_ESP32C6_BT_SM
			bt6_security_tick();	/* 接続5秒後にslave SecReq（C3/C5 D-2d移植） */
#endif
			sec++;
			if ((sec % 60U) == 0U) {
				syslog(LOG_NOTICE,
					   "ble_host_smoke_c6: ss t=%us conn=%u disc=%u ntf=%u/%u wr=%u",
					   (unsigned) sec, (unsigned) g_gap_conn_count,
					   (unsigned) g_gap_disc_count, (unsigned) g_notify_sent,
					   (unsigned) g_notify_fail, (unsigned) g_write_count);
			}
		}
	}
}
