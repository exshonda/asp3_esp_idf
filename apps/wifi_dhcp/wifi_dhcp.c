/*
 *  Wi-Fi DHCP接続デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  wifi_connect（Phase B-2b）にlwIP（Phase C）を重ね，WPA2 AP接続後に
 *  DHCPでIPアドレスを取得し，デフォルトゲートウェイへraw APIでping
 *  する（実通信の確認）．本アプリ自体はlwIP APIには一切触れず，
 *  netif_esp32c3_notify_link()でnet_task（net/netif_esp32c3.c）へ
 *  リンク状態を伝えるのみ（単一実行文脈の原則．設計はdocs/
 *  tcpip-integration.md）．接続先は -DWIFI_SSID=... -DWIFI_PASSWORD=...
 *  で指定する．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_dhcp.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

/*
 *  実施50診断（RX/TX判別）：C5では "no time event is processed in hrt
 *  interrupt." の良性storm（C5実施04の最重要教訓）がsyslogを溢れさせ
 *  コンソールが信頼できない．そこでDHCP経路の物証はRTC-RAMマーカ
 *  （0x50000080〜．esptool read-memで回収．C5実施45/D-2cと同一方式）を
 *  主物証とする．TX側カウンタ（net/netif_esp32c3.cのNETSTALL_TRACE計装＝
 *  low_level_output＝esp_wifi_internal_txの唯一の呼出し元）をミラーする：
 *    tx_calls>0 ⇒ dhcp_start到達＋DISCOVER送信路がlwIPまで生きている
 *                （＝link_upコールバックが走りDHCP開始＝net-task健全）．
 *    last_tx_ret==0(ESP_OK) ⇒ blob TXがフレームを受理．
 *    上記が成り立つのにIP未取得 ⇒ 問題はTX後（OFFER受信＝RF/RX側）．
 *  既定ビルド（NETSTALL_TRACE未定義）は完全no-op＝非回帰．
 */
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE
extern volatile uint32_t g_netstall_tx_calls;
extern volatile uint32_t g_netstall_tx_errs;
extern volatile int32_t  g_netstall_last_tx_ret;
#define DHCPDIAG(idx, val)	(((volatile uint32_t *)0x50000080U)[(idx)] = (uint32_t)(val))
/*
 *  net/netif_esp32c3.c の NETSTALL_TRACE 計装が ping コールバックで呼ぶ
 *  フック．本アプリ（wifi_dhcp）では ping 停止トリガは不要なので空実装
 *  （load_test_c5 は自前の実体を持つ）．TX計装カウンタだけを使う．
 */
void netstall_trace_ping_result(int ok) { (void) ok; }
#else
#define DHCPDIAG(idx, val)	((void)0)
#endif

static ID	main_tskid;
static volatile int32_t	conn_state;	/* 0=待機 1=接続 -1=切断 */
#ifdef ESP32C5_FORCE_5GHZ
static volatile bool_t	scan_done;	/* WIFI_EVENT_SCAN_DONE受信フラグ */
#endif

/*
 *  実施45診断計装：CPU例外フォルト捕捉ハンドラ
 *
 *  docs/c5-bringup.md 実施06/08の手法をwifi_dhcpへ移植．C5のconnect
 *  経路（実施44でLoad access faultを確認）はJTAG live-bp/live-reset
 *  が不安定なため，自然ブート（esptool hard-reset）のまま例外を
 *  ハンドラ側で捕捉し，mepc/mcause/mtval／全汎用レジスタをRTC-RAM
 *  （0x50000000〜．WDTリセットを跨いで保持，実施06/08/別PC再開メモの
 *  既知パターン）へ書いてから無限ループで凍結する．JTAGは後から
 *  非侵襲attach（reset無し）でき，フォルト時点のレジスタをそのまま
 *  mdwで回収できる．全ターゲット共通のEXCNO_*（アクセス系例外一式）
 *  に登録するため，フォルトが起きない限り他チップ／正常系には無害。
 */
#define FAULTCAP_BASE		((volatile uint32_t *)0x50000000U)
#define FAULTCAP_MAGIC		0xFA017C05U

void
fault_capture_handler(void *p_excinf)
{
	/*
	 *  callee-saved(s0-s3)／spはT_EXCINFに含まれない（ASP3の例外フレームは
	 *  呼出し規約でハンドラ自身が保存すると仮定するcallee-savedを積まない
	 *  最小構成）．レジスタ変数で関数先頭（プロローグ直後）の値を直接束縛し，
	 *  cnx_sta_associatedが使う base pointer（s1等）をそのまま回収する。
	 */
	register uint32_t reg_sp __asm__("sp");
	register uint32_t reg_s0 __asm__("s0");
	register uint32_t reg_s1 __asm__("s1");
	register uint32_t reg_s2 __asm__("s2");
	register uint32_t reg_s3 __asm__("s3");
	T_EXCINF	*p = (T_EXCINF *) p_excinf;
	uint32_t	mcause, mtval, mepc;
	uint32_t	sp_v = reg_sp, s0_v = reg_s0, s1_v = reg_s1,
				s2_v = reg_s2, s3_v = reg_s3;

	__asm__ volatile("csrr %0, mcause" : "=r"(mcause));
	__asm__ volatile("csrr %0, mtval"  : "=r"(mtval));
	__asm__ volatile("csrr %0, mepc"   : "=r"(mepc));

	FAULTCAP_BASE[25] = sp_v;
	FAULTCAP_BASE[26] = s0_v;
	FAULTCAP_BASE[27] = s1_v;
	FAULTCAP_BASE[28] = s2_v;
	FAULTCAP_BASE[29] = s3_v;

	FAULTCAP_BASE[0]  = FAULTCAP_MAGIC;
	FAULTCAP_BASE[1]  = mcause;
	FAULTCAP_BASE[2]  = mtval;
	FAULTCAP_BASE[3]  = mepc;
	FAULTCAP_BASE[4]  = (uint32_t) p->pc;		/* T_EXCINF保存済mepc */
	FAULTCAP_BASE[5]  = (uint32_t) p->ra;
	FAULTCAP_BASE[6]  = (uint32_t) p->a0;
	FAULTCAP_BASE[7]  = (uint32_t) p->a1;
	FAULTCAP_BASE[8]  = (uint32_t) p->a2;
	FAULTCAP_BASE[9]  = (uint32_t) p->a3;
	FAULTCAP_BASE[10] = (uint32_t) p->a4;
	FAULTCAP_BASE[11] = (uint32_t) p->a5;
	FAULTCAP_BASE[12] = (uint32_t) p->t0;
	FAULTCAP_BASE[13] = (uint32_t) p->t1;
	FAULTCAP_BASE[14] = (uint32_t) p->t2;
	FAULTCAP_BASE[15] = (uint32_t) p->tp;
	FAULTCAP_BASE[16] = (uint32_t) p->mstatus;
#ifndef __riscv_32e
	FAULTCAP_BASE[17] = (uint32_t) p->t3;
	FAULTCAP_BASE[18] = (uint32_t) p->t4;
	FAULTCAP_BASE[19] = (uint32_t) p->t5;
	FAULTCAP_BASE[20] = (uint32_t) p->t6;
	FAULTCAP_BASE[21] = (uint32_t) p->a6;
	FAULTCAP_BASE[22] = (uint32_t) p->a7;
#endif /* !__riscv_32e */
	FAULTCAP_BASE[23] = (uint32_t) p->intpri;
	FAULTCAP_BASE[24] = p->exncnt;

	for (;;) {
		/*
		 *  凍結．リブートさせず，JTAG（非侵襲attach）またはesptool
		 *  read_memで回収するまでここに留まる．
		 */
	}
}

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base;

	switch (id) {
#ifdef ESP32C5_FORCE_5GHZ
	case WIFI_EVENT_SCAN_DONE:
		scan_done = true;
		break;
#endif
	case WIFI_EVENT_STA_START:
		syslog(LOG_NOTICE, "event: STA_START");
		conn_state = 2;		/* 2=STA_START受信（main_taskがconnect） */
		(void) wup_tsk(main_tskid);
		break;
	case WIFI_EVENT_STA_CONNECTED:
		syslog(LOG_NOTICE, "event: STA_CONNECTED");
		conn_state = 1;
		netif_esp32c3_notify_link(true);
		(void) wup_tsk(main_tskid);
		break;
	case WIFI_EVENT_STA_DISCONNECTED:
		{
			wifi_event_sta_disconnected_t *d =
					(wifi_event_sta_disconnected_t *) data;
			syslog(LOG_NOTICE, "event: STA_DISCONNECTED reason=%d",
				   (int_t)(d != NULL ? d->reason : 0));
			conn_state = -1;
			netif_esp32c3_notify_link(false);
			(void) wup_tsk(main_tskid);
		}
		break;
	default:
		syslog(LOG_NOTICE, "event: WIFI_EVENT id=%d", (int_t)id);
		break;
	}
}

void
main_task(EXINF exinf)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t		wc;
	esp_err_t			err;
	int					retry;
	uint32_t			ip;

	(void) exinf;
	(void) get_tid(&main_tskid);

#ifdef ESP32C5_R45_PROBE_GIC
	/*
	 *  実施45診断：cnx_sta_associated内でLoad access fault
	 *  （s1=g_ic，offset535／mtval=4）を踏む直前と全く同じCPU発行の
	 *  読み出しを，esp_wifi_init/connectより十分前（WiFi/blob未実行）に
	 *  単独で行い，フォルトがg_ic自体・このオフセット固有の問題か，
	 *  connect実行時のシステム状態依存かを切り分ける．フォルトすれば
	 *  fault_capture_handlerがRTC-RAMへ凍結記録する（同一機構を再利用）。
	 *  フォルトしなければPROBE_OK＋読んだ値をRTC-RAMへ書く．
	 */
	{
		extern char g_ic[];
		volatile uint32_t *rtc_probe = (volatile uint32_t *)0x500000A0U;
		volatile uint8_t v;

		rtc_probe[0] = 0xB0BE0450U;	/* PROBE開始マーカ（フォルトなら上書きされない）*/
		v = ((volatile uint8_t *)g_ic)[535];
		rtc_probe[0] = 0x600DB045U;	/* PROBE完走マーカ（フォルトしなかった証拠） */
		rtc_probe[1] = (uint32_t) v;
		rtc_probe[2] = (uint32_t) g_ic;
	}
#endif /* ESP32C5_R45_PROBE_GIC */

#ifdef WIFI_IDLE_SECS
	/*
	 *  ブートカウンタ（RTC FAST RAM 0x50000008）を毎起動でインクリメント．
	 *  アイドル計測窓の途中でチップがリセット/再起動すると >1 になるので，
	 *  「真のフリーズ（boot=1・カウンタ凍結）」と「リセットループ（boot>1）」
	 *  を事後に区別できる（計測アーチファクト排除）．
	 */
	{
		volatile uint32_t *rtc_boot = (volatile uint32_t *)0x50000008u;
		volatile uint32_t *rtc_bmagic = (volatile uint32_t *)0x5000000Cu;
		if (*rtc_bmagic != 0xB007B007u) {	/* コールドスタート初期化 */
			*rtc_bmagic = 0xB007B007u;
			*rtc_boot = 0u;
		}
		*rtc_boot = *rtc_boot + 1u;
	}
#endif

	netif_esp32c3_start();
	esp_shim_initialize();
	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);
	esp_shim_coex_adapter_register();

	syslog(LOG_NOTICE, "wifi_dhcp: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_init -> %d", (int_t)err);
		return;
	}

	(void) esp_wifi_set_mode(WIFI_MODE_STA);
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
	(void) esp_wifi_set_ps(WIFI_PS_NONE);

	/*
	 *  接続先の設定（SSID／パスワード）
	 */
	memset(&wc, 0, sizeof(wc));
	strncpy((char *)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid) - 1);
	strncpy((char *)wc.sta.password, WIFI_PASSWORD,
			sizeof(wc.sta.password) - 1);
	(void) esp_wifi_set_config(WIFI_IF_STA, &wc);
	syslog(LOG_NOTICE, "wifi_dhcp: SSID='%s'", WIFI_SSID);

	err = esp_wifi_start();
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_start -> %d", (int_t)err);
		return;
	}

#ifdef ESP32C5_R45_PROBE_GIC
	/*
	 *  実施45診断・第2弾：起動直後(esp_wifi_init前)のプローブは成功した
	 *  （フォルトせず値0を読めた）。今回はesp_wifi_init/start完了後・
	 *  connect直前（同じmain_taskコンテキスト，ただし時間・WiFi内部状態は
	 *  進行済み）で同じ読み出しを行い，「時間・WiFi内部状態」要因と
	 *  「cnx_sta_associatedのタスク/割込みコンテキスト」要因のどちらに
	 *  近いかを切り分ける．
	 */
	{
		extern char g_ic[];
		volatile uint32_t *rtc_probe2 = (volatile uint32_t *)0x500000B0U;
		volatile uint8_t v;

		rtc_probe2[0] = 0xB0BE0451U;
		v = ((volatile uint8_t *)g_ic)[535];
		rtc_probe2[0] = 0x600DB051U;
		rtc_probe2[1] = (uint32_t) v;
		rtc_probe2[2] = (uint32_t) g_ic;
	}
#endif /* ESP32C5_R45_PROBE_GIC */

#ifdef ESP32C5_FORCE_5GHZ
	/*
	 *  実施NN診断（5GHz強制接続）：ドライバ既定はch9(2.4GHz)を選ぶため，
	 *  connect前にscanし WIFI_SSID に一致する 5GHz(ch>=36) BSS を探して
	 *  bssid/channel をピンする．これで «同一SSIDの5GHz radio» を明示的に
	 *  ターゲットにする．物証はRTC-RAM(0x500000C0〜．esptool read-memで回収．
	 *  C5実施04/50のHRT storm氾濫下でもコンソールに依らず読める)へ退避．
	 *  既定ビルド（ESP32C5_FORCE_5GHZ未定義）は完全no-op＝非回帰．
	 *  スキャン出力自体は氾濫で化けるので，AP一覧は使わずRTCマーカで判別する。
	 */
	{
		volatile uint32_t	*fg = (volatile uint32_t *)0x500000C0U;
		wifi_ap_record_t	*srecs;
		uint16_t		snum = 0;
		int			best = -1;
		int			match_any = 0, match_5g = 0;
		int			i, sec;

		fg[0] = 0x5F5C0001U;	/* 5GHz scan到達マーカ */
		scan_done = false;
		(void) esp_wifi_scan_start(NULL, false);	/* 非同期・全ch(両band) */
		for (sec = 0; sec < 15 && !scan_done; sec++) {
			(void) tslp_tsk(1000000);
		}
		(void) esp_wifi_scan_get_ap_num(&snum);
		fg[1] = (uint32_t) snum;
		if (snum > 40) {
			snum = 40;
		}
		srecs = (wifi_ap_record_t *)
					esp_shim_calloc(snum, sizeof(wifi_ap_record_t));
		if (srecs != NULL && snum > 0 &&
			esp_wifi_scan_get_ap_records(&snum, srecs) == 0) {
			for (i = 0; i < (int) snum; i++) {
				if (strncmp((const char *) srecs[i].ssid, WIFI_SSID,
							sizeof(srecs[i].ssid)) != 0) {
					continue;
				}
				match_any++;
				if (srecs[i].primary < 36) {
					continue;	/* 2.4GHz radio はスキップ */
				}
				match_5g++;
				if (best < 0 || srecs[i].rssi > srecs[best].rssi) {
					best = i;	/* 最強RSSIの5GHz BSSを選ぶ */
				}
			}
		}
		fg[2] = (uint32_t) match_any;
		fg[3] = (uint32_t) match_5g;
		if (best >= 0) {
			memcpy(wc.sta.bssid, srecs[best].bssid, 6);
			wc.sta.bssid_set = true;
			wc.sta.channel = srecs[best].primary;
			wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
			(void) esp_wifi_set_config(WIFI_IF_STA, &wc);
			fg[4] = (uint32_t) srecs[best].primary;
			fg[5] = ((uint32_t) srecs[best].bssid[0]) |
					((uint32_t) srecs[best].bssid[1] << 8) |
					((uint32_t) srecs[best].bssid[2] << 16) |
					((uint32_t) srecs[best].bssid[3] << 24);
			fg[6] = ((uint32_t) srecs[best].bssid[4]) |
					((uint32_t) srecs[best].bssid[5] << 8);
			fg[7] = (uint32_t)(int32_t) srecs[best].rssi;
			fg[8] = 0x5F5C0002U;	/* 5GHz BSS選択・ピン成功 */
			syslog(LOG_NOTICE,
				   "FORCE5G pinned ch=%d rssi=%d bssid=%02x:%02x:%02x:%02x:%02x:%02x",
				   (int_t) srecs[best].primary, (int_t) srecs[best].rssi,
				   srecs[best].bssid[0], srecs[best].bssid[1],
				   srecs[best].bssid[2], srecs[best].bssid[3],
				   srecs[best].bssid[4], srecs[best].bssid[5]);
		}
		else {
			fg[8] = 0x5F5CFA11U;	/* 5GHz BSS見つからず（=SSIDは5GHz非展開） */
			syslog(LOG_NOTICE,
				   "FORCE5G no 5GHz BSS: total=%d match_any=%d match_5g=%d",
				   (int_t) fg[1], match_any, match_5g);
		}
		if (srecs != NULL) {
			esp_shim_free(srecs);
		}
	}
#endif /* ESP32C5_FORCE_5GHZ */

	/*
	 *  STA_START後にmain_task文脈からconnectする（イベントハンドラ＝
	 *  WiFiタスク文脈からのconnect呼出しを避ける．Phase B-2bの知見）
	 */
	(void) tslp_tsk(2000000);
	err = esp_wifi_connect();
	syslog(LOG_NOTICE, "wifi_dhcp: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "wifi_dhcp: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "wifi_dhcp: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "wifi_dhcp: FAILED (timeout)");
		return;
	}

	/*
	 *  接続先APの実測情報（チャンネル・RSSI）を記録する（5GHz実証の
	 *  証跡用．2.4GHzでも無害）．実施45で追加．
	 */
	{
		wifi_ap_record_t	ap;

		if (esp_wifi_sta_get_ap_info(&ap) == 0) {
			syslog(LOG_NOTICE,
				   "wifi_dhcp: AP info: channel=%d rssi=%d",
				   (int_t) ap.primary, (int_t) ap.rssi);
		}
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ（net_task側で処理．
	 *  本タスクは読み出し専用ポーリングのみ）
	 */
	DHCPDIAG(0, 0xDAC00001U);	/* magic：DHCP待ちループ到達（=connect成功後） */
	DHCPDIAG(4, 1U);			/* phase=1：待機中 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE
		DHCPDIAG(1, g_netstall_tx_calls);		/* lwIP→blob TX呼出し累積 */
		DHCPDIAG(2, g_netstall_tx_errs);		/* うちesp_wifi_internal_tx失敗数 */
		DHCPDIAG(3, (uint32_t)g_netstall_last_tx_ret);	/* 直近TX戻り値（0=OK） */
		DHCPDIAG(6, (uint32_t)retry);			/* 経過秒 */
		/*  HRT storm氾濫下でも拾えるよう distinctive tag で毎秒出す
		    （grep -a "DHCPTX" で回収）．  */
		syslog(LOG_NOTICE, "DHCPTX t=%d calls=%u errs=%u ret=%d",
			   (int_t)retry, (uint_t)g_netstall_tx_calls,
			   (uint_t)g_netstall_tx_errs, (int_t)g_netstall_last_tx_ret);
#endif
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "wifi_dhcp: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			DHCPDIAG(4, 2U);	/* phase=2：IP取得 */
			DHCPDIAG(5, ip);
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "wifi_dhcp: DHCP FAILED (timeout)");
		DHCPDIAG(4, 3U);		/* phase=3：タイムアウト */
	}
	syslog(LOG_NOTICE, "wifi_dhcp: done (ping result logged by net_task)");

#ifdef WIFI_IDLE_SECS
	/*
	 *  【S3→C3 アイドルフリーズ再現テスト】
	 *  GOT IP後，送受信を一切行わず WIFI_IDLE_SECS 秒だけ接続維持する
	 *  純アイドル経路．生存秒カウンタを RTC FAST RAM（0x50000000＝
	 *  リンカ未使用・リセット非依存）へ毎秒書き，syslogでも二重に出す．
	 *  カウンタが増え続ける限り生存＝停止した秒が凍結時刻．
	 *  通常ビルドでは -DWIFI_IDLE_SECS 未定義のため一切影響しない．
	 *  詳細: docs/s3-idle-freeze-findings-for-c3.md
	 */
	{
		volatile uint32_t *rtc_alive = (volatile uint32_t *)0x50000000u;
		volatile uint32_t *rtc_magic = (volatile uint32_t *)0x50000004u;
		int idle_t;

		*rtc_magic = 0xA11EA11Eu;	/* 生存中マジック */
		*rtc_alive = 0u;
		syslog(LOG_NOTICE, "wifi_dhcp: IDLE test start (%d s), pure idle",
			   (int_t)WIFI_IDLE_SECS);
		for (idle_t = 1; idle_t <= WIFI_IDLE_SECS; idle_t++) {
			(void) tslp_tsk(1000000);	/* 1秒（送受信なし） */
			*rtc_alive = (uint32_t)idle_t;
			syslog(LOG_NOTICE, "idle t=%d s alive", (int_t)idle_t);
		}
		syslog(LOG_NOTICE, "wifi_dhcp: IDLE test SURVIVED %d s",
			   (int_t)WIFI_IDLE_SECS);
	}
#endif /* WIFI_IDLE_SECS */
}
