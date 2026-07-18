/*
 *  NimBLE host スモークテスト（Phase D-2）
 *
 *  D-1（apps/bt_smoke）で確立したBLEコントローラ起動シーケンスの上に，
 *  NimBLE ホストスタックを載せる．狙いは ble_hs の sync コールバック到達
 *  （＝ホストがコントローラと同期し起動完了）．
 *
 *  ホスト初期化の配線について：
 *    本ポート（hal/components/bt）の nimble_port_init() は
 *    CONFIG_BT_CONTROLLER_ENABLED のとき *内部で* esp_bt_controller_init/
 *    enable を BT_CONTROLLER_INIT_CONFIG_DEFAULT() で行う．しかし D-1 で
 *    実機検証したコントローラ設定は apps/bt_smoke の手書き cfg であり，
 *    その値を正確に使いたい（かつコントローラ二重初期化を避けたい）ため，
 *    ここでは:
 *      esp_bt_controller_init(&cfg)  ← bt_smoke と同じ手書き cfg
 *      esp_bt_controller_enable(BLE)
 *      esp_nimble_init()             ← ホストのみ初期化（内部で
 *                                       esp_nimble_hci_init も呼ぶ．
 *                                       コントローラには触れない）
 *      ble_hs_cfg.sync_cb = on_sync
 *      nimble_port_freertos_init(ble_host_task)
 *    とする（nimble_port_init() は呼ばない＝コントローラ二重初期化回避）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include "ble_host_smoke.h"

#include "esp_bt.h"
#include "esp_shim.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "host/ble_hs_adv.h"
#ifdef TOPPERS_C3_BT_CONNECT_FIX_UNDO_RESOLV
#include "host/ble_hs_pvcy.h"	/*  connect-fix draft (i): ble_hs_pvcy_set_resolve_enabled  */
#endif
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/*
 *  BLEベースバンドのクロックを立てる（emi.c:164対策．bt/bt_shim.c）．
 *  esp_bt_controller_init() より前に呼ぶ必要がある．詳細はdocs/bt-shim.md．
 */
extern void esp_shim_bt_clock_init(void);
/*  D-2d bond修正：保留リング(pend_ring)・保留セマフォgiveの周期flush用（下記
    main_task 定常ループから呼ぶ）．保留0なら即return＝非回帰．  */
extern void esp_shim_queue_flush_pending(void);
extern void esp_shim_sem_flush_pending(void);

#ifdef TOPPERS_ESP32C3_BT_SM
/*
 *  D-2d(SM)：bond store 初期化（S3 BT-5 と同じ）．ble_store_ram.c は
 *  IDF文脈（BLE_USED_IN_IDF=1）で空になるため使えず，stock bleprph と同じ
 *  ble_store_config（BLE_STORE_CONFIG_PERSIST=0＝RAM保持）を使う．公開
 *  ヘッダが無いので自前 extern する（stock bleprph も同様）．  */
extern void ble_store_config_init(void);
#endif

/*
 *  ble_hs sync 到達マーカ（JTAGで容易に観測できるよう固定アドレスへも
 *  記録する）．
 */
volatile uint32_t	g_ble_sync_done;

/*
 *  D-2b：GAPアドバタイズ観測用グローバル（JTAGでシンボル読出し）．
 */
volatile uint32_t	g_adv_active;		/* 1=接続可能アドバタイズ実行中 */
volatile int32_t	g_adv_rc = -1;		/* 最後の ble_gap_adv_start 戻り値 */
volatile uint32_t	g_gap_conn_count;	/* CONNECT イベント回数 */
volatile uint32_t	g_gap_disc_count;	/* DISCONNECT イベント回数 */
volatile uint32_t	g_gap_event_count;	/* 全 GAP イベント回数 */
volatile uint8_t	g_own_addr_type;
volatile int32_t	g_reset_reason = 0x7fffffff;	/* ble_hs_cfg.reset_cb の reason */
volatile uint32_t	g_reset_count;		/* ホストリセット回数 */

/*
 *  D-2d：自前GATTサービス＋notify 状態．S3 の BT-5（notify/subscribe追跡・
 *  SM）を «動作済みの正» として C3 へ逐語移植（docs/bt-shim.md Phase D-2d）．
 *  S3と同じ 16bit UUID：service 0xABF0／chr 0xABF1(READ,"BT4-OK")／
 *  0xABF2(READ|NOTIFY,32bit LE counter)．C3固有として write 特性 0xABF3
 *  （WRITE．受信値をログ＋RTCマーカ＋putカウンタ）を追加する．
 *  notify送出は main_task の定常ループ（1秒周期・アプリタスク文脈）から
 *  subscribe中のみ行う．カウンタはsubscribe中のみ加算するので central 側の
 *  受信欠番＝ロスと機械判定できる．main_task文脈での送出はD-2cで移植した
 *  保留リング（esp_shim.c）がクリティカルセクション内送出を救済する．
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
#ifdef TOPPERS_ESP32C3_BT_SM
volatile uint32_t	g_conn_secs;		/* 接続経過秒（SecReq遅延用．S3 BT-5移植） */
volatile uint8_t	g_sec_initiated;	/* slave Security Request 送出済みフラグ */
#endif

/*
 *  D-2b調査（HRT凍結検証）：SYSTIMER HWカウンタ直読みプローブ．
 *  カーネルHRT割込み/timeout処理とは独立に，SYSTIMER HWが
 *  advertising中に進み続けるかを記録する（JTAGライブhaltがRF中に
 *  OpenOCDを落とす問題を回避＝busy-loopでグローバル/RTCへ記録し，
 *  単発attachで事後読み）．
 */
#define HRT_PROBE	1
extern uint32_t	_kernel_current_hrtcnt;
volatile uint32_t	g_probe_systimer;	/* 直近のSYSTIMER HW値（下位32bit） */
volatile uint32_t	g_probe_systimer_first;	/* プローブ開始時のSYSTIMER値 */
volatile uint32_t	g_probe_hrtcache;	/* 直近の _kernel_current_hrtcnt キャッシュ */
volatile uint32_t	g_probe_hrtcache_first;	/* プローブ開始時の hrtcnt キャッシュ */
volatile uint32_t	g_probe_count;		/* プローブ反復回数 */

/*  SYSTIMER unit0 の52bitカウンタ下位32bitを raw レジスタで直読み（snapshot）  */
static uint32_t
raw_systimer_lo(void)
{
	int	guard;

	sil_wrw_mem((void *) 0x60023004UL, 0x40000000UL);	/* UNIT0_UPDATE */
	for (guard = 0; guard < 100000; guard++) {
		if ((sil_rew_mem((void *) 0x60023004UL) & (1UL << 29)) != 0U) {
			break;			/* VALUE_VALID */
		}
	}
	return sil_rew_mem((void *) 0x60023044UL);		/* UNIT0_VALUE_LO */
}

#define BLE_DEVICE_NAME		"ASP3-C3-BLE"

/*  RTC_CNTL scratch（STORE系）．リセットを跨いで残り，JTAG読出し容易．
    ★D-2b再開ラウンドで再割当て：0x54=esp_intr_alloc呼出しトレース
    （bt_shim.c），0x58=CPU線2ディスパッチ数（esp_shim.c）に使うため，
    advマーカは0x5cへ移動・connマーカのRTC書込みは廃止（グローバル
    g_gap_conn_countで観測）．probe_taskのRTC書込みも廃止．  */
#define BLE_SYNC_MARK_ADDR	((void *) 0x60008050UL)
#define BLE_SYNC_MARK_VAL	0x5ADE51C0UL
#define BLE_ADV_MARK_ADDR	((void *) 0x6000805CUL)	/* adv開始マーカ */
#define BLE_ADV_MARK_VAL	0x0ADE5000UL
/*  D-2c：GAP接続／切断マーカ（JTAG不要のesptool read-mem事後読み）．
    STORE6(0xC0)/STORE4(0xB8)を使用＝D-2cでは storm probe を無効化
    （storm根治はD-2b実施(1)(o)で構造的に確定済み・0x54のallocトレースは
    probe非依存で継続）してこの2regを接続観測へ明け渡す．
    connect: 0x604E<status:8><conn_count:8>／disconnect: 0xD15C<reason:8><disc_count:8>．  */
#define BLE_CONN_MARK_ADDR	((void *) 0x600080C0UL)
#define BLE_DISC_MARK_ADDR	((void *) 0x600080B8UL)
/*  D-2d：write特性（0xABF3）受信マーカ．STORE2(0x60008058)は本番ビルド
    （ACL_TRACE OFF）では未使用のため転用（ACL_TRACE ON時のみ acl_trace が
    使うので衝突するが，本番/検証は OFF）．
    0x7717<write_count:8><先頭バイト:8> をパックし単発 dump-mem で確認する．  */
#define BLE_WRITE_MARK_ADDR	((void *) 0x60008058UL)
/*  D-2d(bond確認)：PAIRING_COMPLETE を ENC_CHANGE(0x58) と別レジスタへ分離＝
    SM最終段（鍵配布→bond登録）まで到達したかを独立に観測する（0x58 は
    ENC_CHANGE が «後勝ち» で上書きするため PAIRING_COMPLETE が隠れる＝
    bt-shim.md D-2d「次の一手#1」）．
    ★レジスタ選定：8個の STORE は全て使用中（0x50 SYNC/0x54 alloc/0x58 ENC・
    write/0x5C ADV/0xB8 DISC/0xBC esp_shim/0xC0 CONN/0xC4 adv-rc）．うち
    «接続→ペアリング→切断→再adv» の間に書かれないのは 0x54（bt_shim の
    esp_intr_alloc トレース＝BTコントローラ init 時のみ書込み，以後セッション
    中は不変）だけ．0xC4 は start_advertising 毎に adv-rc(0xAD00xxxx)で上書き
    されるため «切断→再adv» で PAIRING マーカが消える＝不可．よって 0x54 を使う．
    値：0x5DC0<status:8><our_sec:4><peer_sec:4>．status=0 かつ our_sec>=1 で
    «DUT側 bond 成立»．«未発火» は init の alloc トレース値が残る＝0x5DC0 タグの
    有無で判別（alloc トレースは 0x5DC0xxxx を生成しない）．  */
#define BLE_PAIR_MARK_ADDR	((void *) 0x60008054UL)

/*
 *  ★一時計装（広告非到達 判別ラウンド・既定OFF＝TOPPERS_C3_BOOT_TRACE 未定義時は
 *  no-op で非回帰）：Direct Boot / download-latch 脱出と «main_task の停止点» を
 *  無条件マーカで確定する．STORE2(0x60008058)へ 0xB00000NN の進捗コードを順に
 *  上書き＝到達した最遠点が残る．cbr.sh がブート前に 0x58 を sentinel(0xCA5E0058)
 *  でプリロードするため:
 *    0x58=0xCA5E0058 → main_task 未到達＝latch（かつ RTC survive/read 経路健全）
 *    0x58=0            → 測定無効（RTC 非生存 or 書込み不達．latch と誤読しない）
 *    0x58=0xB00000NN   → 起動．NN と 0x54(alloc)/0x50(sync) で停止点を確定
 *  NN: 1=main_task entry 2=clock_init後 3=controller_init直前 4=init OK
 *      5=enable OK 6=nimble_init OK 7=host task起動（sync待ち）
 *  0x54=0xA1020805(bt_shim alloc trace)は controller_init 中に立つ＝NN3〜4間の
 *  細分点．0x50=0x5ADE51C0 は sync 到達．  */
#ifdef TOPPERS_C3_BOOT_TRACE
#define BOOT_TRACE(nn)	sil_wrw_mem((void *) 0x60008058UL, 0xB0000000UL | (uint32_t) (nn))
#else
#define BOOT_TRACE(nn)	((void) 0)
#endif

static int gap_event_cb(struct ble_gap_event *event, void *arg);

/*
 *  自前GATTサービス（0xABF0：READ/NOTIFY/WRITE(+暗号READ)）は3チップ逐語
 *  同一のため共有 .inc に集約（dedup Tier2c）．契約シンボル
 *  （g_notify_counter/g_write_count/g_write_last/g_notify_val_handle・
 *   BLE_WRITE_MARK_ADDR）は本ファイル上部で定義済み．
 */
#define	BLE_SMOKE_LOG_TAG	"ble_host_smoke"
#ifdef TOPPERS_ESP32C3_BT_SM
#define	BLE_SMOKE_HAS_ENC_CHR	1
#endif
#include "../common_ble/ble_gatt_smoke_svc.inc"

#ifdef TOPPERS_C3_GATTS_REGDIAG
/*
 *  ★一時計装（0xABF0不可視 判別ラウンド・既定OFF）：ble_gatts_start が
 *  実際にATTサーバへ積んだ結果を register_cb で観測し，接続前に読める
 *  0x600080C0(CONN・main_task start でクリア済・接続まで不変) へパックする．
 *    値：0x5EED<f><svc:4><chr:8>
 *      f  bit15 = OP_SVC で UUID==0xABF0 が来た（=自前サービス登録成立）
 *      svc = 登録された service 数（GAP/GATT/0xABF0 で 3 が期待値）
 *      chr = 登録された chr 総数（全サービス合算．0xABF0 分は SM=ON で 4）
 *  C6=可視／C3=不可視の差分（C3のみ 0xABF4 READ_ENC を持つ）を，svc定義が
 *  ble_gatts_start で «丸ごと弾かれた»(f=0)のか «積まれたがスマホ側キャッシュ»
 *  (f=1)のかで決定的に判別する．esptool の cleared-boot-read で 0xC0 を読む．
 */
static void
gatts_regdiag_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
	static uint8_t	n_svc = 0U;
	static uint8_t	n_chr = 0U;
	static uint8_t	saw_abf0 = 0U;

	(void) arg;
	if (ctxt->op == BLE_GATT_REGISTER_OP_SVC) {
		n_svc++;
		if (ble_uuid_u16(ctxt->svc.svc_def->uuid) == 0xABF0) {
			saw_abf0 = 1U;
		}
	}
	else if (ctxt->op == BLE_GATT_REGISTER_OP_CHR) {
		n_chr++;
	}
	sil_wrw_mem((void *) 0x600080C0UL,
				0x5EED0000UL | ((uint32_t) saw_abf0 << 15)
				| (((uint32_t) n_svc & 0xFUL) << 8)
				| ((uint32_t) n_chr & 0xFFUL));
}
#endif /* TOPPERS_C3_GATTS_REGDIAG */

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
		syslog(LOG_ERROR, "ble_host_smoke: notify mbuf alloc fail (v=%u)",
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
		syslog(LOG_ERROR, "ble_host_smoke: notify rc=%d (v=%u)",
			   (int_t) rc, (unsigned) v);
	}
}

#ifdef TOPPERS_ESP32C3_BT_SM
/*
 *  D-2d(bond)：接続経過秒を加算し，接続5秒後に «未暗号» なら slave
 *  Security Request を1回だけ送る（1秒周期ループから呼ぶ．S3 BT-5
 *  bt5_security_tick を逐語移植）．S3 は connect で «ペアリング要求が出る»＝
 *  ペリフェラル発の SecReq が central にペアリングを開始させるため．C3 の
 *  D-2d 逐語移植ではこの接続時セキュリティ開始が欠落しており，暗号必須特性
 *  0xABF4 の READ でしかトリガできなかった．S3 と同一トリガに揃えることで
 *  «暗号有効化タイムアウト» が 0xABF4-read 経路特有か普遍かも切り分けられる．
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
		return;		/*  既に暗号化済みなら不要  */
	}
	rc = ble_gap_security_initiate(ch);
	syslog(LOG_NOTICE,
		   "ble_host_smoke: BT5 security_initiate(slave SecReq) rc=%d", (int_t) rc);
}
#endif /* TOPPERS_ESP32C3_BT_SM */

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

	/*  アドレス型を自動決定（privacy=0＝public/static）  */
	rc = ble_hs_id_infer_auto(0, (uint8_t *) &g_own_addr_type);
	if (rc != 0) {
		g_adv_rc = rc;
		syslog(LOG_ERROR, "ble_host_smoke: id_infer_auto rc=%d", (int_t) rc);
		return;
	}

	/*  アドバタイズデータ：flags＋完全ローカル名  */
	memset(&fields, 0, sizeof(fields));
	fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
	fields.name = (const uint8_t *) BLE_DEVICE_NAME;
	fields.name_len = (uint8_t) strlen(BLE_DEVICE_NAME);
	fields.name_is_complete = 1;

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		g_adv_rc = rc;
		syslog(LOG_ERROR, "ble_host_smoke: adv_set_fields rc=%d", (int_t) rc);
		return;
	}

	memset(&adv_params, 0, sizeof(adv_params));
	adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
	adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

	rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
						   &adv_params, gap_event_cb, NULL);
	g_adv_rc = rc;
#ifdef HRT_PROBE
	/*  （D-2b(1)(i)）ble_gap_adv_startが返ったことをROM生存reg 0xC4へ
	    記録＝ストームが止まりホストタスクが再開し0x200a完了が届いた証拠．
	    0xAD0000xx=戻り値xxで復帰／0=依然ブロック（ストーム継続）．  */
	sil_wrw_mem((void *) 0x600080C4UL, 0xAD000000UL | ((uint32_t) rc & 0xffUL));
#endif
	if (rc == 0) {
		g_adv_active = 1U;
		sil_wrw_mem(BLE_ADV_MARK_ADDR, BLE_ADV_MARK_VAL);
		syslog(LOG_NOTICE,
			   "ble_host_smoke: advertising started as '%s' (own_addr_type=%d)",
			   BLE_DEVICE_NAME, (int_t) g_own_addr_type);
	}
	else {
		syslog(LOG_ERROR, "ble_host_smoke: adv_start rc=%d", (int_t) rc);
	}
}

/*
 *  GAPイベントコールバック（接続/切断/アドバタイズ完了を最小処理）．
 */
static int
gap_event_cb(struct ble_gap_event *event, void *arg)
{
	(void) arg;
	g_gap_event_count++;

	switch (event->type) {
	case BLE_GAP_EVENT_CONNECT:
		g_gap_conn_count++;
		/*  D-2c物証：接続イベントをRTCへ記録（JTAG不要read-mem）  */
		sil_wrw_mem(BLE_CONN_MARK_ADDR,
					0x604E0000UL
					| (((uint32_t) event->connect.status & 0xffUL) << 8)
					| (g_gap_conn_count & 0xffUL));
		syslog(LOG_NOTICE,
			   "ble_host_smoke: GAP CONNECT status=%d handle=%d",
			   (int_t) event->connect.status,
			   (int_t) event->connect.conn_handle);
		if (event->connect.status == 0) {
			/*  D-2d：notify送出先の接続ハンドルを記録  */
			g_conn_handle = event->connect.conn_handle;
#ifdef TOPPERS_ESP32C3_BT_SM
			/*  D-2d(bond)：接続5秒後に slave SecReq を送るための計時開始  */
			g_conn_secs = 0U;
			g_sec_initiated = 0U;
#endif
		}
		else {
			/*  接続確立失敗＝再アドバタイズ  */
			g_adv_active = 0U;
			start_advertising();
		}
		break;
	case BLE_GAP_EVENT_DISCONNECT:
		g_gap_disc_count++;
		g_adv_active = 0U;
		g_conn_handle = BLE_HS_CONN_HANDLE_NONE;	/* D-2d */
		g_notify_enabled = 0U;						/* D-2d */
#ifdef TOPPERS_ESP32C3_BT_SM
		g_sec_initiated = 0U;						/* D-2d(bond) */
#endif
		sil_wrw_mem(BLE_DISC_MARK_ADDR,
					0xD15C0000UL
					| (((uint32_t) event->disconnect.reason & 0xffUL) << 8)
					| (g_gap_disc_count & 0xffUL));
		syslog(LOG_NOTICE, "ble_host_smoke: GAP DISCONNECT reason=%d",
			   (int_t) event->disconnect.reason);
		start_advertising();	/*  切断後に再アドバタイズ  */
		break;
	case BLE_GAP_EVENT_ADV_COMPLETE:
		g_adv_active = 0U;
		syslog(LOG_NOTICE, "ble_host_smoke: GAP ADV_COMPLETE reason=%d",
			   (int_t) event->adv_complete.reason);
		break;
	case BLE_GAP_EVENT_SUBSCRIBE:
		/*  D-2d：0xABF2 の CCCD 変化を追跡（attr_handle==val_handle）．
		    cur_notify をそのまま反映（on/off両対応）．S3 BT-5 と同一．  */
		syslog(LOG_NOTICE,
			   "ble_host_smoke: GAP SUBSCRIBE attr=%d cur_notify=%d reason=%d",
			   (int_t) event->subscribe.attr_handle,
			   (int_t) event->subscribe.cur_notify,
			   (int_t) event->subscribe.reason);
		if (event->subscribe.attr_handle == g_notify_val_handle) {
			g_notify_enabled = (uint8_t) event->subscribe.cur_notify;
		}
		break;
	case BLE_GAP_EVENT_MTU:
		syslog(LOG_NOTICE, "ble_host_smoke: GAP MTU value=%d",
			   (int_t) event->mtu.value);
		break;
#ifdef TOPPERS_ESP32C3_BT_SM
	case BLE_GAP_EVENT_ENC_CHANGE:
		/*  D-2d(SM)：SMPペアリング/暗号化の結果（status=0が成功）  */
		/*  SMP診断マーカ（STORE2 0x58）：tag 0x5DE0＝ENC_CHANGE 到達，
		    下位バイト=status．esptool dump-mem で serial 非開放のまま読める．  */
#ifdef TOPPERS_ESP32C3_BT_EVT_TRACE
		/*  ★反証実験：HCI Encryption Change(0x08) 到着〜この app ENC_CHANGE
		    通知までの «実秒»．status=13(ETIMEOUT) のとき，〜30 なら «実30秒
		    待ち»＝NimBLE の SM タイマは正常＝真因は Identity PDU(ACLデータ)
		    欠落／≪30（数秒）なら «NPL タイマ早発火»＝Codex 説．byte1 に格納：
		    0x5DE0<delta秒:8><status:8>．  */
		{
			extern volatile uint32_t	g_evt_enc_hrt;
			uint32_t	dsec = 0U;

			if (g_evt_enc_hrt != 0U) {
				dsec = (fch_hrt() - g_evt_enc_hrt) / 1000000U;
				if (dsec > 255U) {
					dsec = 255U;
				}
			}
			sil_wrw_mem((void *) 0x60008058UL,
						0x5DE00000UL | (dsec << 8)
						| ((uint32_t) event->enc_change.status & 0xffUL));
		}
#else
		sil_wrw_mem((void *) 0x60008058UL,
					0x5DE00000UL | ((uint32_t) event->enc_change.status & 0xffUL));
#endif
		syslog(LOG_NOTICE, "ble_host_smoke: GAP ENC_CHANGE status=%d",
			   (int_t) event->enc_change.status);
		{
			struct ble_gap_conn_desc	desc;

			if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0) {
				syslog(LOG_NOTICE,
					   "ble_host_smoke: sec_state enc=%d auth=%d bond=%d keysz=%d",
					   (int_t) desc.sec_state.encrypted,
					   (int_t) desc.sec_state.authenticated,
					   (int_t) desc.sec_state.bonded,
					   (int_t) desc.sec_state.key_size);
			}
		}
		break;
	case BLE_GAP_EVENT_PASSKEY_ACTION:
		/*  IO=NoInputNoOutput（Just Works）では来ない想定．来たらログのみ  */
		syslog(LOG_NOTICE, "ble_host_smoke: GAP PASSKEY_ACTION action=%d",
			   (int_t) event->passkey.params.action);
		break;
	case BLE_GAP_EVENT_REPEAT_PAIRING:
		/*  central側が既存bondを無視して再ペアリング＝旧bondを消しRETRY
		    （stock bleprphと同型．S3 BT-5）  */
		{
			struct ble_gap_conn_desc	desc;

			if (ble_gap_conn_find(event->repeat_pairing.conn_handle,
								  &desc) == 0) {
				ble_store_util_delete_peer(&desc.peer_id_addr);
			}
			syslog(LOG_NOTICE,
				   "ble_host_smoke: GAP REPEAT_PAIRING -> deleted old bond, retry");
		}
		return BLE_GAP_REPEAT_PAIRING_RETRY;
	case BLE_GAP_EVENT_PARING_COMPLETE:
		/*  SMP手続き完了（status=0成功／BLE host error code）．enc_cb系
		    （bond再利用のLTK再暗号化）でも発火する（S3 BT-5注記）  */
		/*  D-2d(bond確認)：ENC_CHANGE と別 reg(STORE7 0xC4)へ分離記録し，同時に
		    DUT側 bond store 件数（our_sec/peer_sec）をパックする＝鍵が実際に
		    保存され bond が成立したかを status と併せて判定する（0x58 共用では
		    ENC_CHANGE 後勝ちで PAIRING_COMPLETE が見えなかった）．  */
		{
			int	our_cnt = 0, peer_cnt = 0;

			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &our_cnt);
			(void) ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peer_cnt);
			sil_wrw_mem(BLE_PAIR_MARK_ADDR,
						0x5DC00000UL
						| (((uint32_t) event->pairing_complete.status & 0xffUL) << 8)
						| (((uint32_t) our_cnt & 0xfUL) << 4)
						| ((uint32_t) peer_cnt & 0xfUL));
			syslog(LOG_NOTICE,
				   "ble_host_smoke: GAP PAIRING_COMPLETE status=%d bonds our=%d peer=%d",
				   (int_t) event->pairing_complete.status,
				   (int_t) our_cnt, (int_t) peer_cnt);
		}
		break;
#endif /* TOPPERS_ESP32C3_BT_SM */
	default:
		break;
	}
	return 0;
}

static void
on_sync(void)
{
	g_ble_sync_done = 1U;
	sil_wrw_mem(BLE_SYNC_MARK_ADDR, BLE_SYNC_MARK_VAL);
	syslog(LOG_NOTICE, "ble_host_smoke: ble_hs SYNC, host up");

#ifdef TOPPERS_C3_BT_CONNECT_FIX_UNDO_RESOLV
	/*  connect不可 恒久修正候補(i)（既定OFF・docs/c3-ble-connect-plan.md 段階1）．
	    PVCY=1 の起動時HCIバースト（ble_hs_startup_go→ble_hs_pvcy_set_our_irk）が
	    有効化したコントローラ内アドレス解決を，アドバタイズ開始前にここで無効へ
	    戻す．on_sync は ble_hs_startup_go 完了後（ble_hs.c: sync_cb）に呼ばれる
	    ため，このタイミングでバーストは既に出終わっている．PVCY=1 のまま
	    （bond の Identity 鍵配布ゲートは維持）．本アプリは privacy=0＝RPA非使用
	    のためアドレス解決OFFは機能に無影響の見込み（HW確認は phase-2）．  */
	{
		int rc_res = ble_hs_pvcy_set_resolve_enabled(0);
		syslog(LOG_NOTICE,
		       "ble_host_smoke: connect-fix(i) undo addr-res-enable rc=%d",
		       rc_res);
	}
#endif

	/*  sync完了＝コントローラ同期済み．接続可能アドバタイズを開始する
	    （ble_gap_*はsync後のイベント文脈で呼ぶのが作法．NimBLE設計）．  */
	start_advertising();
}

static void
on_reset(int reason)
{
	/*  D-2b調査：ble_hs_sched_reset の reason を timing非依存で記録．
	    JTAGで g_reset_reason / g_reset_count / 0x6000805c を読む．  */
	g_reset_reason = (int32_t) reason;
	g_reset_count++;
	/*  タグ付きでRTC 0x60008050へ記録＝esptool read-mem（JTAG不要）で
	    事後読み．上位バイト0x5E=reset識別，下位=reason，中位=count下位．
	    （計装番地0x54/58/5cとは別番地＝衝突回避）  */
	sil_wrw_mem((void *) 0x60008050UL,
				0x5E000000UL | ((g_reset_count & 0xffUL) << 8)
				| ((uint32_t) reason & 0xffUL));
	syslog(LOG_ERROR, "ble_host_smoke: ble_hs RESET, reason=%d", (int_t) reason);
}

/*
 *  NimBLE ホストタスク本体．default event queue を処理し続ける．
 */
static void
ble_host_task(void *param)
{
	(void) param;
	syslog(LOG_NOTICE, "ble_host_smoke: nimble host task started");
	nimble_port_run();		/*  戻らない（nimble_port_stop まで）  */
	nimble_port_freertos_deinit();
}

void
main_task(EXINF exinf)
{
	esp_bt_controller_config_t	cfg;
	esp_err_t					err;

	(void) exinf;

	BOOT_TRACE(1);		/* ★無条件起動マーカ：main_task 到達＝latch 脱出 */

	esp_shim_initialize();
	esp_shim_coex_adapter_register();

	/*  D-2c：GAP接続／切断マーカを既知値(0)へ初期化（前回boot残値との
	    混同回避．watchdog-resetはRTC domainを消さないため明示クリア）．  */
	sil_wrw_mem(BLE_CONN_MARK_ADDR, 0U);
	sil_wrw_mem(BLE_DISC_MARK_ADDR, 0U);

	/*  D-2d(bond確認)：PAIRING_COMPLETE マーカ(0x54)は bt_shim の
	    esp_intr_alloc トレースと共用．init で alloc トレース値が上書きする
	    ため boot クリアはしない＝«発火/未発火» は 0x5DC0 タグの有無で判別する
	    （BLE_PAIR_MARK_ADDR のコメント参照）．  */

#ifdef TOPPERS_ESP32C3_BT_ACL_TRACE
	/*  D-2c：RX-data dispatch 計装のカウンタ（RTC STORE2）を0クリア．  */
	{
		extern void esp_acl_trace_reset(void);
		esp_acl_trace_reset();
	}
#endif

#ifdef TOPPERS_ESP32C3_BT_EVT_TRACE
	/*  D-2d bond診断：HCI EVT 計装のカウンタ（RTC STORE0=0x50）を0クリア
	    ＝SYNC マーカ転用を明示（wrapper が最初の EVT で上書きする）．  */
	{
		extern void esp_evt_trace_reset(void);
		esp_evt_trace_reset();
	}
#endif

	/*
	 *  emi.c:164対策：BLEベースバンド(BB)のクロックを有効化する
	 *  （bt_smoke と同じ．詳細はdocs/bt-shim.md）．
	 */
	esp_shim_bt_clock_init();
	BOOT_TRACE(2);		/* ★clock_init 通過 */

	/*
	 *  BLEコントローラ設定（apps/bt_smoke と同一の手書き値．
	 *  BT_CONTROLLER_INIT_CONFIG_DEFAULT() は使わない）．
	 */
	memset(&cfg, 0, sizeof(cfg));
	cfg.magic = ESP_BT_CTRL_CONFIG_MAGIC_VAL;
	cfg.version = ESP_BT_CTRL_CONFIG_VERSION;
	cfg.controller_task_stack_size = ESP_TASK_BT_CONTROLLER_STACK;
	cfg.controller_task_prio = ESP_TASK_BT_CONTROLLER_PRIO;
	cfg.controller_task_run_cpu = 0;
	cfg.bluetooth_mode = ESP_BT_MODE_BLE;
	cfg.ble_max_act = 6;
	cfg.sleep_mode = 0;
	cfg.sleep_clock = 0;
	cfg.ble_st_acl_tx_buf_nb = 0;
	cfg.ble_hw_cca_check = 0;
	cfg.ble_adv_dup_filt_max = 30;
	cfg.coex_param_en = false;
	cfg.ce_len_type = 0;
	cfg.coex_use_hooks = false;
	cfg.hci_tl_type = ESP_BT_CTRL_HCI_TL_VHCI;
	cfg.hci_tl_funcs = NULL;
	cfg.txant_dft = 0;
	cfg.rxant_dft = 0;
	cfg.txpwr_dft = 11;
	cfg.cfg_mask = 1;
	cfg.scan_duplicate_mode = 0;
	cfg.scan_duplicate_type = 0;
	cfg.normal_adv_size = 100;
	cfg.mesh_adv_size = 0;
	cfg.coex_phy_coded_tx_rx_time_limit = 0;
	cfg.hw_target_code = 0x01010000UL;
	cfg.slave_ce_len_min = 5;
	cfg.hw_recorrect_en = 1;
	cfg.cca_thresh = 75;
	cfg.scan_backoff_upperlimitmax = 0;
	cfg.dup_list_refresh_period = 0;
	cfg.ble_50_feat_supp = false;
	cfg.ble_cca_mode = 0;
	cfg.ble_data_lenth_zero_aux = 0;
	cfg.ble_chan_ass_en = 1;
	cfg.ble_ping_en = 1;
	cfg.ble_llcp_disc_flag = 0;
	cfg.run_in_flash = false;
	cfg.dtm_en = false;
	cfg.enc_en = true;
	cfg.qa_test = false;
	cfg.connect_en = true;
	cfg.scan_en = true;
	cfg.ble_aa_check = false;
	cfg.adv_en = true;

#ifdef HRT_PROBE
	/*  （D-2c）storm probe は無効化．D-2b(1)(o)でstorm根治は構造的に確定
	    （0x54のallocトレース=0xA1020805はprobe非依存でbt_shimが継続記録）．
	    STORE4(0xB8)/STORE6(0xC0)はD-2cのGAP接続／切断マーカへ転用するため
	    probeの連続RTC書込みを止める（probe=0でも割込み配送自体は不変）．  */
	esp_shim_isr_storm_probe = 0U;
#endif
	BOOT_TRACE(3);		/* ★controller_init 直前（このあと 0x54 alloc trace が立つ） */
	syslog(LOG_NOTICE, "ble_host_smoke: esp_bt_controller_init");
	err = esp_bt_controller_init(&cfg);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_bt_controller_init -> %d",
			   (int_t) err);
		return;
	}
	BOOT_TRACE(4);		/* ★controller_init OK */

	syslog(LOG_NOTICE, "ble_host_smoke: esp_bt_controller_enable(BLE)");
	err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_bt_controller_enable -> %d",
			   (int_t) err);
		return;
	}
	BOOT_TRACE(5);		/* ★controller_enable OK（C6 の PHY-init ハングはここで停止する） */

	/*
	 *  NimBLE ホストスタックを初期化（コントローラは既に起動済みなので
	 *  esp_nimble_init を使い，nimble_port_init は使わない）．
	 *  esp_nimble_init は内部で esp_nimble_hci_init（VHCI ブリッジ登録・
	 *  バッファ確保）を行う．
	 */
	syslog(LOG_NOTICE, "ble_host_smoke: esp_nimble_init (host)");
	err = esp_nimble_init();
	if (err != ESP_OK) {
		syslog(LOG_ERROR, "ble_host_smoke: esp_nimble_init -> %d", (int_t) err);
		return;
	}
	BOOT_TRACE(6);		/* ★esp_nimble_init OK */

	/*
	 *  D-2c：標準GAP／GATTサービスを登録する（接続後にホストから
	 *  サービスディスカバリで見えるようにするため）．ble_svc_*_init は
	 *  ble_gatts_add_svcs でサービス定義をキューへ積むだけで，実際の
	 *  ATT属性登録は ble_hs_start→ble_gatts_start（ホストタスク側）で
	 *  行われる．したがって nimble_port_freertos_init より前に呼ぶ．
	 *  esp_nimble_init（=ble_hs_init）済みが前提．
	 */
#ifdef TOPPERS_ESP32C3_BT_SM
	/*
	 *  D-2d(SM)：bond store を初期化してから SM パラメータを設定する
	 *  （S3 BT-5 §5：store未初期化だと Pairing Request 直後に
	 *  ble_sm_chk_store_overflow→ble_store_read が ENOTSUP で即 Pairing
	 *  Failed になる．これが SM 即時失敗の真因だった）．  */
	ble_store_config_init();

	/*  Just Works / Secure Connections（S3 BT-5 と同一設定）．IO=NoIO・
	    bonding=1・MITM=0・SC=1．鍵配布は ENC|ID（bond再利用＋IRK交換）．  */
	ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
	ble_hs_cfg.sm_bonding = 1;
	ble_hs_cfg.sm_mitm = 0;
	ble_hs_cfg.sm_sc = 1;
	ble_hs_cfg.sm_our_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
	ble_hs_cfg.sm_their_key_dist =
		BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
#endif /* TOPPERS_ESP32C3_BT_SM */

	ble_svc_gap_init();
	ble_svc_gatt_init();
	{
		int	rc;

		/*  D-2d：自前サービス 0xABF0（0xABF1 READ／0xABF2 NOTIFY／
		    0xABF3 WRITE）を登録．count_cfg でATT属性数を予約→add_svcs で
		    キューへ積む（実登録は ble_hs_start→ble_gatts_start）．  */
		rc = ble_gatts_count_cfg(custom_svcs);
		if (rc == 0) {
			rc = ble_gatts_add_svcs(custom_svcs);
		}
		if (rc != 0) {
			syslog(LOG_ERROR, "ble_host_smoke: gatts svc reg rc=%d", (int_t) rc);
		}
#ifdef TOPPERS_C3_GATTS_REGDIAG
		/*  診断：count_cfg→add_svcs 後の rc（キュー時点の受理可否）を
		    0x600080B8(DISC・接続前は0) へ．0xADD5<rc16>．rc=0=キュー受理．  */
		sil_wrw_mem((void *) 0x600080B8UL,
					0xADD50000UL | ((uint32_t) rc & 0xFFFFUL));
#endif
		rc = ble_svc_gap_device_name_set(BLE_DEVICE_NAME);
		if (rc != 0) {
			syslog(LOG_ERROR, "ble_host_smoke: gap_device_name_set rc=%d",
				   (int_t) rc);
		}
	}

	/*
	 *  sync/reset コールバックを登録してからホストタスクを起動する．
	 */
	ble_hs_cfg.sync_cb = on_sync;
	ble_hs_cfg.reset_cb = on_reset;
#ifdef TOPPERS_C3_GATTS_REGDIAG
	/*  診断：ble_gatts_start の登録結果を register_cb で 0x600080C0 へ観測．  */
	ble_hs_cfg.gatts_register_cb = gatts_regdiag_cb;
#endif

	syslog(LOG_NOTICE, "ble_host_smoke: nimble_port_freertos_init");
	nimble_port_freertos_init(ble_host_task);
	BOOT_TRACE(7);		/* ★host task 起動＝sync 待ち（あとは 0x50 sync 到達を待つ） */

	syslog(LOG_NOTICE, "ble_host_smoke: init done, waiting for ble_hs SYNC");

	/*
	 *  sync 到達を待つ（実機観測用．JTAGでは g_ble_sync_done か
	 *  0x60008050 の 0x5ADE51C0 を見る）．
	 */
	{
		int	retry;
		for (retry = 0; retry < 100 && g_ble_sync_done == 0U; retry++) {
			(void) tslp_tsk(100000);	/* 100ms */
		}
		if (g_ble_sync_done != 0U) {
			syslog(LOG_NOTICE, "ble_host_smoke: Phase D-2 milestone reached");
		}
		else {
			syslog(LOG_NOTICE, "ble_host_smoke: no SYNC yet (check on HW)");
		}
	}
	syslog(LOG_NOTICE, "ble_host_smoke: entering steady loop (notify tick)");

	/*
	 *  D-2d：定常ループ（1秒周期・アプリタスク文脈）．subscribe中のみ
	 *  notify を1回送る（notify_tick）．常用イメージが advertising/接続/
	 *  notify を無期限に続けられるよう main_task を返さず保持する
	 *  （ユーザーがスマホで追試できるよう adv 継続の本番ビルドを残す）．
	 *  ログは60秒毎のハートビートのみ（notify毎ログは抑制）．
	 */
	{
		uint32_t	sec = 0U;
		uint32_t	sub = 0U;

		for (;;) {
			/*
			 *  ★D-2d bond修正（安全網）：保留リング(pend_ring)に滞留した
			 *  ACL RX を «100ms周期» でflushする．ペアリング/鍵配布の SMP PDU
			 *  （ACLデータ）が E_CTX フォールバックで pend_ring に退避された後，
			 *  以後キュー交通が途絶えると exit_critical / queue-op の機会的
			 *  flushが走らず滞留し，NimBLE の SM proc が対向 PDU を待って
			 *  最終的に 30秒で BLE_HS_ETIMEOUT する（docs/bt-shim.md「D-2d
			 *  bond診断」で暗号後の Identity PDU 滞留＝30秒待ちを実測確定）．
			 *  独立タスク(main_task)から高頻度flushすれば滞留が ≤100ms で解け，
			 *  対話的PDU交換にも鍵配布にも間に合う．pend残0なら即return＝
			 *  非回帰・非侵襲（本ループはBLE常駐アプリの心臓部）．
			 */
			esp_shim_queue_flush_pending();
			esp_shim_sem_flush_pending();	/* ★保留セマフォgiveも精算 */
			(void) tslp_tsk(100000);	/* 100ms */
			if (++sub < 10U) {
				continue;		/* notify/security/HBは1秒毎 */
			}
			sub = 0U;
			notify_tick();
#ifdef TOPPERS_ESP32C3_BT_SM
			bt5_security_tick();	/* 接続5秒後に slave SecReq（S3 BT-5移植） */
#endif
			sec++;
			if ((sec % 60U) == 0U) {
				syslog(LOG_NOTICE,
					   "ble_host_smoke: ss t=%us conn=%u disc=%u ntf=%u/%u wr=%u",
					   (unsigned) sec, (unsigned) g_gap_conn_count,
					   (unsigned) g_gap_disc_count, (unsigned) g_notify_sent,
					   (unsigned) g_notify_fail, (unsigned) g_write_count);
			}
		}
	}
}

#ifdef HRT_PROBE
/*
 *  HRT凍結検証プローブタスク（最低優先度＝アイドル時に回る）．
 *  SYSTIMER HW値（直読み）とカーネルHRTキャッシュをbusy-loopで記録．
 *  カーネルtimeout（tslp/dly_tsk）に一切依存しないので，HRTが凍結して
 *  いてもこのループは回り，SYSTIMER HWがadvertising中に進むか否かが
 *  分かる．JTAG単発attachで g_probe_systimer/g_probe_count/g_probe_hrtcache
 *  または RTC 0x60008058(SYSTIMER)/0x6000805c(count) を事後読み．
 */
void
probe_task(EXINF exinf)
{
	(void) exinf;
	/*  開始時の基準値（グローバルのみ＝JTAG読み．★D-2b再開ラウンド：
	    RTC 0x54/0x58/0x5cはalloc計装・線2カウント・advマーカに再割当て
	    したため，本タスクのRTC書込みは廃止．HRT凍結問題自体は解決済み
	    （CPU飽和が真因）でこのタスクはCPU飽和検知の生存確認用に残す）  */
	g_probe_systimer_first = raw_systimer_lo();
	g_probe_hrtcache_first = _kernel_current_hrtcnt;
	for (;;) {
		g_probe_systimer = raw_systimer_lo();
		g_probe_hrtcache = _kernel_current_hrtcnt;
		g_probe_count++;
		{
			volatile int	k;
			for (k = 0; k < 20000; k++) { }	/* 短いbusy間隔 */
		}
	}
}
#endif /* HRT_PROBE */
