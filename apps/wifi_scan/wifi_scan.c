/*
 *  Wi-Fiスキャンデモ（ASP3＋esp_wifi blob＋os_adapter shim）
 *
 *  esp_wifi_init→start→scanを実行し，見つかったAPのSSID一覧を
 *  syslogへ出力する（Phase B-2aの動作確認．docs/wifi-shim.md）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "wifi_scan.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"
#ifdef TOPPERS_ESP32C6_WIFI
#include "wifi_trace.h"	/* C6 AGC調査専用の診断計装（wifi_trace.c／esp32c6_espidfのみ） */
#endif /* TOPPERS_ESP32C6_WIFI */
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
#include "wifi_trace.h"	/* 実施16：C5 regi2cトレース（wifi_trace.c／esp32c5_espidfのみ） */
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */

/*
 *  スキャン完了通知（esp_event_shim経由）
 */
static ID	main_tskid;
static volatile bool_t scan_done;

/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
static volatile uint32_t promisc_rx_count;

/*
 *  DIAGNOSTIC (temporary): g_ic（libnet80211.aのグローバル状態構造体．
 *  nmで実機アドレス確認済み＝0x408476b0）のoffset 497/499を直接
 *  ピークする．wifi_start_process/wifi_set_promis_processが
 *  wifi_hw_start呼出しをガードする条件がここにある（実施12/13参照）．
 */
#define DIAG_G_IC_BASE 0x408476b0UL
static uint8_t
diag_g_ic_byte(unsigned int off)
{
	return(*(volatile uint8_t *)(DIAG_G_IC_BASE + off));
}

#define DIAG_G_WIFI_NVS_ADDR 0x40800890UL
static uint32_t
diag_wifi_nvs_ptr(void)
{
	return(*(volatile uint32_t *)DIAG_G_WIFI_NVS_ADDR);
}

static uint8_t
diag_wifi_nvs_byte0(void)
{
	uint32_t	p = diag_wifi_nvs_ptr();
	if (p == 0U) {
		return(0xEEU);	/* NULLポインタの目印 */
	}
	return(*(volatile uint8_t *)p);
}

#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
/*
 *  【実施24】PD_TOP/HPAON/HPCPU/LPPERI force解除shimのA/B判定用，
 *  JTAG非依存のUART計装。wifi_scan.cfgのCRE_CYCから1秒周期で呼ばれる
 *  （TNFY_HANDLER＝タスク非依存コンテキスト。main_taskがPHY較正の
 *  無限リトライループ内で停止していても，タイマ割込み経由でこの
 *  ハンドラは独立して発火し続ける）。
 *
 *  出力：トーン自己ループバック測定の生ADCサンプル（MODEM0+0x81C）と
 *  IQ_DONEビット（MODEM0+0x47C bit16）＝実施14〜23で追ってきた症状
 *  そのもの，および実施23で新規差分と確定したPMU_POWER_PD_TOP/HPAON/
 *  HPCPU/LPPERI_CNTL（0x600B00F8/FC/100/10C）の現在値（shim適用の
 *  機械確認をJTAG無しで行うため）。
 */
void
wifi_diag_cyclic_handler(EXINF exinf)
{
	uint32_t	raw_adc = *(volatile uint32_t *)0x600A081CU;
	uint32_t	done    = *(volatile uint32_t *)0x600A047CU;
	uint32_t	pd_top    = *(volatile uint32_t *)0x600B00F8U;
	uint32_t	pd_hpaon  = *(volatile uint32_t *)0x600B00FCU;
	uint32_t	pd_hpcpu  = *(volatile uint32_t *)0x600B0100U;
	uint32_t	pd_lpperi = *(volatile uint32_t *)0x600B010CU;

	(void) exinf;

	syslog(LOG_NOTICE,
		   "wifi_diag: raw_adc=0x%08x done16=%d pd_top=0x%02x pd_hpaon=0x%02x",
		   (unsigned int)raw_adc, (int_t)((done >> 16) & 1U),
		   (unsigned int)pd_top, (unsigned int)pd_hpaon);
	syslog(LOG_NOTICE,
		   "wifi_diag: pd_hpcpu=0x%02x pd_lpperi=0x%02x",
		   (unsigned int)pd_hpcpu, (unsigned int)pd_lpperi);
}
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */

static void
promisc_rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
	(void) buf; (void) type;
	promisc_rx_count++;
}

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base; (void) data;
	if (id == WIFI_EVENT_SCAN_DONE) {
		scan_done = true;
		(void) wup_tsk(main_tskid);
	}
}

void
main_task(EXINF exinf)
{
	wifi_init_config_t	cfg = WIFI_INIT_CONFIG_DEFAULT();
	uint16_t			num;
	wifi_ap_record_t	*recs;
	esp_err_t			err;
	uint16_t			i;

	(void) exinf;
	(void) get_tid(&main_tskid);

#ifdef TOPPERS_ESP32C6_WIFI
	/*
	 *  DIAGNOSTIC（Step0 option2）：RTC RAM(0x50000000〜)にosiカウンタを
	 *  累積（LP Super WDTリセットを跨いで保持されることを確認済み）．
	 *  レイアウト：[0]magic [1]boot# [2]semTake [3]semGive [4]qRecv
	 *              [5]qSend [6]qSendISR [7]timerArm
	 *  各ブート冒頭で累積値をダンプ→1サイクルあたりの増分とリセット時刻
	 *  （blobログの "W (NNN)"）から頻度を算出し，native（40〜80/s）と比較．
	 */
	{
		volatile uint32_t *g = (volatile uint32_t *)0x50000000U;
		uint_t k;
		if (g[0] != 0xC6057A11U) {	/* 電源投入時=magic無し→全クリア */
			for (k = 0U; k < 16U; k++) {
				g[k] = 0U;
			}
			g[0] = 0xC6057A11U;
		}
		syslog(LOG_NOTICE,
			   "GT-ASP3 accum boot#=%d: semTake=%d semGive=%d qRecv=%d qSend=%d",
			   (int_t)g[1], (int_t)g[2], (int_t)g[3], (int_t)g[4],
			   (int_t)g[5]);
		/*  前回runのscan結果（flood-proof：Wi-Fi起動前=氾濫前に出力）．
		 *  [8]AP件数 [9]scan_get err [10]到達マーカ(1:loop脱出 2:calloc後
		 *  3:get_ap_records後) [11]qRecv増分の目安 */
		syslog(LOG_NOTICE,
			   "GT-ASP3 prev-scan: reach=%d apcount=%d scanerr=%d timerArm=%d qSendISR=%d",
			   (int_t)g[10], (int_t)g[8], (int_t)g[9],
			   (int_t)g[7], (int_t)g[6]);
		/*  HRTスプリアス割込み計測＋Wi-Fi割込み配送数（前回run累積）：
		 *  hrtEntries=HRT割込み総数 hrtFired=alarm発火主張 hrtSpur=うち
		 *  counter<target（＝スプリアス） wifiInt=Wi-Fi線の発火総数
		 *  （0近傍ならRX割込みが配送されていない＝0 APの根因） */
		syslog(LOG_NOTICE,
			   "GT-ASP3 hrtEntries=%d hrtFired=%d hrtSpur=%d wifiInt=%d",
			   (int_t)g[12], (int_t)g[13], (int_t)g[14], (int_t)g[11]);
		g[1] = g[1] + 1U;
	}
	/*  boot dumpの3行をlogtaskに吐かせてからWi-Fi起動（blobの同時
	 *  UART出力で文字化けするのを避ける）． */
	(void) tslp_tsk(300000);
#endif /* TOPPERS_ESP32C6_WIFI */

	syslog(LOG_NOTICE, "wifi_scan: initializing shim");
	esp_shim_initialize();
#ifdef TOPPERS_ESP32C6_WIFI
	wifi_trace_reset();
	wifi_regi2c_reset();	/* DIAGNOSTIC (temporary，実施23／Priority 2) */
	wifi_regi2c_patch_install();	/* DIAGNOSTIC（実施23）：PHY初期化前に必ずインストール */
	wifi_taskdelay_reset();	/* DIAGNOSTIC（実施26／タイミング感度調査） */
#endif /* TOPPERS_ESP32C6_WIFI */

	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);

	esp_shim_coex_adapter_register();

#ifdef WIFI_SCAN_PREINIT_SPIN
	/*
	 *  DIAGNOSTIC（一時的，実施31）：esp_wifi_init()直前でスピンし，
	 *  JTAGで全域レジスタダンプを取るための同期点．実験後にrevert．
	 */
	syslog(LOG_NOTICE, "wifi_scan: PREINIT_SPIN reached, halting here");
	while (true) {
		(void) tslp_tsk(1000000);
	}
#endif /* WIFI_SCAN_PREINIT_SPIN */

#ifdef HANDOFF_SKIP_WIFI_INIT
	/*
	 *  DIAGNOSTIC（ハンドオフStep0）：ESP-IDFが既にesp_wifi_init/start
	 *  まで済ませたWi-Fiの上へジャンプしてきた場合，ASP3側で
	 *  esp_wifi_init を再実行すると二重初期化でblobの残留グローバル
	 *  （ESP-IDF時代の関数ポインタ）を踏んでIllegal instructionになる．
	 *  そこでinitをスキップし，その先（scan）だけを試す切り分け．
	 */
	syslog(LOG_NOTICE, "wifi_scan: HANDOFF_SKIP_WIFI_INIT (esp_wifi_init/start をスキップ)");
	(void) cfg;
#else /* HANDOFF_SKIP_WIFI_INIT */
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
	/*  実施16：esp_wifi_init()直前でリングバッファをリセットし，
	 *  アドレスを一度だけ出力する（JTAGでの生メモリ直読み用）。 */
	wifi_regi2c_reset();
	wifi_regi2c_dump_addr();
	/*  実施18：phy_set_txcap_reg引数トレース用の第2リングバッファも
	 *  同じタイミングでリセット・アドレス出力する。 */
	wifi_txcap_reset();
	wifi_txcap_dump_addr();
	/*  実施25：未公開regi2c block(0x63/0x68/0x6b)アクセス時の
	 *  ANA_CONF1/ANA_CONF2記録バッファも同じタイミングでリセット・
	 *  アドレス出力する。 */
	wifi_regi2c_cfgsnap_reset();
	wifi_regi2c_cfgsnap_dump_addr();
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */
#ifdef ESP32C5_R36_REGI2C_SEED
	/*
	 *  DIAGNOSTIC（一時的，実施36・点(b)因果検証）：ハンドオフ(WORKS)
	 *  実測のregi2c系譜差分8箇所を，PHY較正（esp_wifi_init内）より前に
	 *  シードする。書込み成立は読み戻し（esp32c5_r36_seed_readback[]）
	 *  で確認・syslogへ出力。既定ビルドでは未定義（実験時のみ
	 *  ASP3_EXTRA_COMPILE_DEFSで有効化）。
	 */
	{
		extern void esp32c5_r36_regi2c_seed(void);
		extern uint32_t esp32c5_r36_seed_readback[10];
		esp32c5_r36_regi2c_seed();
		syslog(LOG_NOTICE, "wifi_scan: R36SEED rb0-4 %02x %02x %02x %02x %02x",
			   (int_t)esp32c5_r36_seed_readback[0],
			   (int_t)esp32c5_r36_seed_readback[1],
			   (int_t)esp32c5_r36_seed_readback[2],
			   (int_t)esp32c5_r36_seed_readback[3],
			   (int_t)esp32c5_r36_seed_readback[4]);
		syslog(LOG_NOTICE, "wifi_scan: R36SEED rb5-8 %02x %02x %02x %02x marker=%08x",
			   (int_t)esp32c5_r36_seed_readback[5],
			   (int_t)esp32c5_r36_seed_readback[6],
			   (int_t)esp32c5_r36_seed_readback[7],
			   (int_t)esp32c5_r36_seed_readback[8],
			   (int_t)esp32c5_r36_seed_readback[9]);
	}
#endif /* ESP32C5_R36_REGI2C_SEED */
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init");
	err = esp_wifi_init(&cfg);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_init -> %d", (int_t)err);
	if (err != 0) {
		return;
	}
#endif /* HANDOFF_SKIP_WIFI_INIT */
#if !defined(HANDOFF_SKIP_WIFI_INIT) && defined(TOPPERS_ESP32C6_WIFI)
	/*  DIAG_G_IC_BASE/DIAG_G_WIFI_NVS_ADDRはC6のlibnet80211.aブロブで
	 *  実測したアドレス（実施12/13）——C3では別blob・別アドレスのため
	 *  無guardのまま実行すると無関係な値を誤ってg_ic/nvsとして表示する
	 *  （docs/wifi-scan-c3-crash.md 実施1で発見，crash自体は別要因だが
	 *  同じ「C6専用診断のguard漏れ」系統のためついでにguard）．*/
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-init nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());
#endif /* !HANDOFF_SKIP_WIFI_INIT && TOPPERS_ESP32C6_WIFI */

#ifndef HANDOFF_SKIP_WIFI_INIT
	(void) esp_wifi_set_mode(WIFI_MODE_STA);
#ifdef TOPPERS_ESP32C6_WIFI
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-set_mode g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
#endif /* TOPPERS_ESP32C6_WIFI */
	(void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
#ifdef WIFI_SCAN_PS_MIN_MODEM
	/*
	 *  DIAGNOSTIC（一時的，実施71／Codex申し送り(a)の因果実験）：
	 *  native側scanアプリはesp_wifi_set_psを一切呼ばず，STAモードの
	 *  既定値WIFI_PS_MIN_MODEM（モデムスリープ有効）のまま動作する．
	 *  実施71でこれがesp_phy_enable/esp_phy_disable（osi_funcsの
	 *  _phy_enable/_phy_disable経由）の周期的呼出し（native実測4回/
	 *  scan）を駆動し，2回目以降のesp_phy_enableがs_is_phy_calibrated
	 *  既真のため`phy_wakeup_init`（FE再初期化，`fe_txrx_reset`含む）
	 *  を実行することを突き止めた．ASP3は下のWIFI_PS_NONEにより
	 *  esp_phy_enableを一度しか呼ばず`phy_wakeup_init`に一度も到達
	 *  しない．本フラグでWIFI_PS_NONEをWIFI_PS_MIN_MODEMに切替え，
	 *  周期的なPHY re-enable／FE再初期化が起きるかを検証する
	 *  （原因か症状かの切り分け用，恒久変更ではない）．
	 *
	 *  ★実施71実測：本フラグを有効にして実機検証した結果，
	 *  WIFI_PS_MIN_MODEMに切替えてもASP3のesp_phy_enableは依然
	 *  1回しか呼ばれず，`phy_wakeup_init`にも到達しなかった（陰性
	 *  結果）．すなわちnativeのesp_phy_enable/disable周期呼出しは
	 *  単純なPS設定値の違いでは説明できない——スキャン中（未接続）は
	 *  DTIM同期を伴う本来のモデムスリープが適用される状況ではない
	 *  ため，別の要因（blob内部のスキャン専用の電源管理ロジック，
	 *  もしくはOSプリミティブ／タイマ連携の違い）が真因である可能性が
	 *  高い．本フラグは反証済みの実験として残置（削除しない）．
	 */
	(void) esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
#else /* WIFI_SCAN_PS_MIN_MODEM */
	(void) esp_wifi_set_ps(WIFI_PS_NONE);
#endif /* WIFI_SCAN_PS_MIN_MODEM */

	err = esp_wifi_start();
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_start -> %d", (int_t)err);
#ifdef TOPPERS_ESP32C6_WIFI
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start g_ic[497]=%d g_ic[499]=%d",
		   (int_t)diag_g_ic_byte(497), (int_t)diag_g_ic_byte(499));
	syslog(LOG_NOTICE, "wifi_scan: DIAG post-start nvs_ptr=%x nvs[0]=%d",
		   (int_t)diag_wifi_nvs_ptr(), (int_t)diag_wifi_nvs_byte0());
#endif /* TOPPERS_ESP32C6_WIFI */
	if (err != 0) {
		return;
	}

	/* DIAGNOSTIC (temporary, --wrap trace: promiscuous-mode RX test): */
	{
		err = esp_wifi_set_promiscuous_rx_cb(promisc_rx_cb);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous_rx_cb -> %d",
			   (int_t)err);
		err = esp_wifi_set_promiscuous(true);
		syslog(LOG_NOTICE, "wifi_scan: DIAG set_promiscuous(true) -> %d",
			   (int_t)err);
		(void) tslp_tsk(3000000);	/* 3秒間，周辺の電波を受信できるか観測 */
		syslog(LOG_NOTICE, "wifi_scan: DIAG promisc_rx_count=%d",
			   (int_t)promisc_rx_count);
		(void) esp_wifi_set_promiscuous(false);
	}
#endif /* HANDOFF_SKIP_WIFI_INIT */

#ifdef TOPPERS_ESP32C6_WIFI
	wifi_regsnap_reset();	/* DIAGNOSTIC (temporary, Priority 2) */
	/*  ★根本原因テスト（追記10）：JTAGでnative(受信OK)=0x7 vs ASP3=0x0の
	 *  差分が判明した MODEM_LPCON_CLK_CONF(0x600af018) を強制的に0x7へ．
	 *  bit0=WIFIPWR bit1=COEX bit2=I2C_MST(RF regi2c用)クロック．これで
	 *  APが検出できれば，shimのクロックenable欠落が0 APの根因と確定． */
	*(volatile uint32_t *)0x600af018U = 0x7U;
#endif /* TOPPERS_ESP32C6_WIFI */
	err = esp_wifi_scan_start(NULL, false);
	syslog(LOG_NOTICE, "wifi_scan: esp_wifi_scan_start -> %d", (int_t)err);

	{
		/*  DIAGNOSTIC（Step0 option2）：scan待ちの間，RTC-RAMのosiカウンタ
		 *  (0x50000008〜．adapterがライブ加算)を毎秒読み，per-secデルタを
		 *  出力してosi呼出しレート（388Hz storm再現の有無）を実測する．
		 *  syslogは5引数上限のため1行5値．scan_doneが来なくても
		 *  20秒で抜けて後段のダンプへ進む．*/
		volatile uint32_t *g = (volatile uint32_t *)0x50000000U;
		uint32_t lsemt = g[2], lqr = g[4], lqs = g[5], lqsi = g[6], lta = g[7];
		int sec;
		for (sec = 0; sec < 20 && !scan_done; sec++) {
#ifdef TOPPERS_ESP32C6_WIFI
			/*  0x600af018=MODEM_LPCON_CLK_CONF_REGはC6(H2/H4/H21/C61等の
			 *  新modem系統)にのみ存在する周辺で，C3のペリフェラルバスには
			 *  対応レジスタが存在しない（hal/esp32c3のreg_base.hに該当
			 *  ベースなし）．無guardのままC3でも毎秒書込みしていたが，
			 *  未使用領域への書込みでクラッシュはしないため今回のIllegal
			 *  instruction本体とは無関係——ただし同種のguard漏れなのでC6
			 *  専用にguardする（docs/wifi-scan-c3-crash.md 実施1）．*/
			*(volatile uint32_t *)0x600af018U = 0x7U;	/* 追記10：クロック再アサート */
#endif /* TOPPERS_ESP32C6_WIFI */
			(void) tslp_tsk(1000000);	/* 1秒 */
			syslog(LOG_NOTICE,
				"OSIRATE/s semTake=%d qRecv=%d qSend=%d qSendISR=%d timerArm=%d",
				(int_t)(g[2] - lsemt), (int_t)(g[4] - lqr),
				(int_t)(g[5] - lqs), (int_t)(g[6] - lqsi),
				(int_t)(g[7] - lta));
			lsemt = g[2]; lqr = g[4]; lqs = g[5]; lqsi = g[6]; lta = g[7];
		}
	}
#if defined(TOPPERS_ESP32C6_WIFI) && 0
	/*  DIAGNOSTIC（Step0 option2）：これらの重いダンプはlogtaskを溢れさせ
	 *  "APs found"行を飲み込むため一時無効化．スキャン完走/AP件数の確認を
	 *  優先する（調査完了後に復帰）．*/
	wifi_trace_dump_counts();
	wifi_regi2c_dump_count();
	wifi_trace_dump();
	wifi_regsnap_dump();
	wifi_regi2c_dump();
	wifi_taskdelay_dump();
	wifi_phyinit_dump();
	wifi_trace_dump_addr();
#endif /* TOPPERS_ESP32C6_WIFI */

	*(volatile uint32_t *)0x50000028U = 1U;	/* [10]=reach 1: loop脱出 */
	num = 20;
	recs = (wifi_ap_record_t *)
				esp_shim_calloc(num, sizeof(wifi_ap_record_t));
	if (recs == NULL) {
		return;
	}
	*(volatile uint32_t *)0x50000028U = 2U;	/* [10]=reach 2: calloc後 */
	err = esp_wifi_scan_get_ap_records(&num, recs);
	*(volatile uint32_t *)0x50000020U = (uint32_t)num;	/* [8]=AP件数 */
	*(volatile uint32_t *)0x50000024U = (uint32_t)err;	/* [9]=scan_get err */
	*(volatile uint32_t *)0x50000028U = 3U;	/* [10]=reach 3: get後 */
	{
		/*  Wi-Fi割込み線(1〜15)の発火総数をRTC[11]へ．blobがset_intrで
		 *  ルーティングしたMAC/RX割込みが実際に配送されているかの実測．*/
		extern volatile uint32_t esp_shim_int_count[];
		uint32_t wsum = 0U;
		int wi;
		for (wi = 1; wi <= 15; wi++) {
			wsum += esp_shim_int_count[wi];
		}
		*(volatile uint32_t *)0x5000002CU = wsum;	/* [11]=Wi-Fi int総数 */
	}
#ifdef TOPPERS_ESP32C6_WIFI
	/*
	 *  DIAGNOSTIC（C6専用，実施(追記12)）：C6の固定ROM PHYFUNS表アドレス
	 *  (0x4087f954)のidx23=read_mask関数ポインタを直接呼ぶ．
	 *  ★C3では未guardのままこの分岐を素通しした結果，このアドレスの
	 *  idx23エントリが未設定(NULL)のままjalrされ，Illegal instruction
	 *  (pc=0)でクラッシュすることを確認した（docs/wifi-scan-c3-crash.md
	 *  実施1）．esp_coex_adapter.cで既に確立している同種のC6専用診断
	 *  （同じ0x4087f954直読み，そちらは#if defined(TOPPERS_ESP32C6_WIFI)
	 *  で正しくguard済み）と同じパターンで，本ファイルだけguard漏れして
	 *  いた．native(受信OK)と同じ読み出しをして比較＝RF較正の正否を
	 *  判定するためのC6専用計装なので，C3を含む他チップでは実行しない．
	 */
	{
		/*  追記12：RF較正regi2cブロックを読み戻してRTC[16..](0x50000040)へ． */
		uint32_t *romtbl = (uint32_t *)0x4087f954U;
		uint8_t (*rd)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) =
			(uint8_t (*)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t))
				(uintptr_t)romtbl[23];
		volatile uint8_t *out = (volatile uint8_t *)0x50000040U;
		static const uint8_t blk[4] = {0x6bU, 0x6aU, 0x66U, 0x6dU};
		static const uint8_t hst[4] = {1U, 1U, 0U, 1U};
		int bi, r, o = 0;
		if (rd != NULL) {
			for (bi = 0; bi < 4; bi++) {
				for (r = 0; r < 16; r++) {
					out[o++] = rd(blk[bi], hst[bi], (uint8_t)r, 7U, 0U);
				}
			}
		}
	}
#endif /* TOPPERS_ESP32C6_WIFI */
	syslog(LOG_NOTICE, "wifi_scan: %d APs found (err=%d)",
		   (int_t)num, (int_t)err);
	for (i = 0; i < num; i++) {
		syslog(LOG_NOTICE, "  [%d] %s (rssi=%d ch=%d)",
			   (int_t)i, (const char *)recs[i].ssid,
			   (int_t)recs[i].rssi, (int_t)recs[i].primary);
	}
	esp_shim_free(recs);
	syslog(LOG_NOTICE, "wifi_scan: done");

	/*  DIAGNOSTIC（追記19・一時）：因果検証用の再scanループ．
	 *  JTAGでRFシンセ(0x6b)のreg2/4/11/13/14をnative値に上書きした後，
	 *  後続scanでAPが出るかを見る．native側GT-REGDIFFループと同形． */
	{
		wifi_ap_record_t	rec1;
		uint16_t			n1;
		for (;;) {
			scan_done = false;
			err = esp_wifi_scan_start(NULL, false);
			while (!scan_done) {
				(void) tslp_tsk(500000);
			}
			{
				uint16_t total = 0;
				(void) esp_wifi_scan_get_ap_num(&total);
				n1 = 1;
				err = esp_wifi_scan_get_ap_records(&n1, &rec1);	/* 結果flush */
				syslog(LOG_NOTICE, "wifi_scan: RESCAN %d APs (err=%d)",
					   (int_t)total, (int_t)err);
			}
		}
	}
}
