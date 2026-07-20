/*
 *  DIAGNOSTIC (temporary): lightweight ISR-safe ring-buffer trace for
 *  --wrap-based blob call tracing (C6 Wi-Fi RX-enable investigation).
 *
 *  Wraps a handful of libpp.a/libnet80211.a internal symbols (found via
 *  nm; not part of any public API) around the RX-enable path, to
 *  observe whether NuttX and ASP3 diverge in which of these are
 *  actually reached and what they return.  syslog is intentionally NOT
 *  used from the wrap functions themselves (deferred-formatting bug,
 *  see docs/wifi-shim-c6.md) -- only wifi_trace_dump() (called once,
 *  after capture, from task context) uses syslog.
 *
 *  Not for permanent use -- remove once the investigation concludes.
 */
#include <t_syslog.h>
#include <string.h>
#include "wifi_trace.h"
#include "esp_shim.h"
#include "esp_rom_sys.h"	/* esp_rom_delay_us：実施39切り分け実験用 */

#define WIFI_TRACE_SIZE  1024

wifi_trace_t	wifi_tr[WIFI_TRACE_SIZE];
volatile uint32_t wifi_tr_pos;
volatile uint8_t wifi_trace_frozen;

/*
 *  DIAGNOSTIC（実施20）：syslogのバースト・ロス（キュー溢れで
 *  wifi_trace_dump()自体の行が欠落する事象を実機で確認．
 *  「44 messages are lost」）を避けるため，呼出し回数だけを
 *  カウントする軽量版．IDあたり1ワードのみ，ダンプ時に
 *  非ゼロのものだけ数行で出力する．
 */
#define WIFI_TRACE_MAXID 48
static volatile uint32_t wifi_tr_count[WIFI_TRACE_MAXID];
static const char *wifi_trace_name(uint16_t id);

void
wifi_trace_reset(void)
{
	wifi_tr_pos = 0U;
	wifi_trace_frozen = 0U;
	memset(wifi_tr, 0, sizeof(wifi_tr));
	memset((void *)wifi_tr_count, 0, sizeof(wifi_tr_count));
}

void
wifi_trace_push(uint16_t id, uint16_t ctx,
				uintptr_t a0, uintptr_t a1,
				uintptr_t a2, uintptr_t a3, uintptr_t ret)
{
	uint32_t	pos;
	wifi_trace_t *ent;

	if (wifi_trace_frozen != 0U) {
		return;
	}

	pos = wifi_tr_pos++;
	ent = &wifi_tr[pos % WIFI_TRACE_SIZE];

	if (id < WIFI_TRACE_MAXID) {
		wifi_tr_count[id]++;
	}
	ent->t_us_low = (uint32_t)esp_shim_time_us();
	ent->id = id;
	ent->ctx = ctx;
	ent->a0 = a0;
	ent->a1 = a1;
	ent->a2 = a2;
	ent->a3 = a3;
	ent->ret = ret;
}

void
wifi_trace_dump_counts(void)
{
	uint16_t	id;

	for (id = 1U; id < WIFI_TRACE_MAXID; id++) {
		if (wifi_tr_count[id] != 0U) {
			syslog(LOG_NOTICE, "wifi_trace_cnt: id=%d name=%s count=%d",
				   (int_t)id, wifi_trace_name(id),
				   (int_t)wifi_tr_count[id]);
		}
	}
}

/*  ID→シンボル名の対応（wifi_trace_dump()のログ出力専用） */
static const char *
wifi_trace_name(uint16_t id)
{
	switch (id) {
	case 1:  return("wifi_hw_start");
	case 2:  return("wifi_hmac_init");
	case 3:  return("wifi_lmac_init");
	case 4:  return("wDev_Rxbuf_Init");
	case 5:  return("esf_buf_setup");
	case 6:  return("esf_buf_setup_static");
	case 7:  return("wdev_set_promis");
	case 8:  return("sta_rx_cb");
	case 9:  return("wifi_recycle_rx_pkt");
	case 10: return("esf_buf_alloc");
	case 11: return("esf_buf_alloc_dynamic");
	case 12: return("wdev_data_init");
	case 13: return("wifi_set_rx_policy");
	case 14: return("adc2_wifi_acquire");
	case 15: return("ieee80211_set_hmac_stop");
	case 16: return("wifi_mode_set");
	case 17: return("_do_wifi_start");
	case 18: return("ieee80211_update_phy_country");
	case 19: return("wifi_start_process");
	case 20: return("wifi_set_promis_process");
	case 21: return("register_chipv7_phy");
	case 22: return("chip_v7_set_chan_ana");
	case 23: return("set_channel_rfpll_freq");
	case 24: return("set_rfpll_freq");
	case 25: return("write_rfpll_sdm");
	case 26: return("wait_rfpll_cal_end");
	case 27: return("enable_agc");
	case 28: return("disable_agc");
	case 29: return("mac_enable_bb");
	case 30: return("fe_reg_init");
	case 31: return("fe_txrx_reset");
	case 32: return("phy_bbpll_cal");
	case 33: return("set_rxclk_en");
	case 34: return("set_txclk_en");
	case 35: return("write_chan_freq");
	case 36: return("restart_cal");
	case 37: return("i2cmst_reg_init");
	case 38: return("rxiq_cal_init");
	case 39: return("set_rx_gain_cal_dc_new");
	case 40: return("coex_init");
	case 41: return("coex_schm_process_restart");
	case 42: return("coex_schm_lock_ENTER");
	case 43: return("coex_schm_interval_get_ENTER");
	case 44: return("pbus_rx_dco_cal_1step_new");
	case 45: return("ram_pbus_force_mode");
	case 46: return("rx_pbus_reset");
	case 50: return("lmacProcessRxSucData");
	case 51: return("ppRxPkt");
	case 52: return("wdevProcessRxSucDataAll");
	case 53: return("wDev_ProcessRxSucData");
	case 54: return("wDev_IndicateFrame");
	default: return("?");
	}
}

void
wifi_trace_dump(void)
{
	uint32_t	total = wifi_tr_pos;
	uint32_t	n = (total < WIFI_TRACE_SIZE) ? total : WIFI_TRACE_SIZE;
	uint32_t	start = (total < WIFI_TRACE_SIZE) ? 0U : (total % WIFI_TRACE_SIZE);
	uint32_t	i, idx;

	syslog(LOG_NOTICE, "wifi_trace: total=%d (showing %d, ring=%d)",
		   (int_t)total, (int_t)n, (int_t)WIFI_TRACE_SIZE);
	for (i = 0U; i < n; i++) {
		idx = (start + i) % WIFI_TRACE_SIZE;
		/*
		 *  TOPPERS syslogは1呼出あたりの引数上限がTNUM_LOGPAR=6
		 *  （t_syslog.h）で，6個ちょうどだと末尾が展開されない
		 *  （実施12で判明）ため2回に分けて出力する．
		 */
		syslog(LOG_NOTICE, "wifi_trace: [%d] t=%d id=%s a0=%08x",
			   (int_t)i, (int_t)wifi_tr[idx].t_us_low,
			   wifi_trace_name(wifi_tr[idx].id),
			   (unsigned int)wifi_tr[idx].a0);
		syslog(LOG_NOTICE, "wifi_trace:   a1=%08x a2=%08x a3=%08x ret=%08x",
			   (unsigned int)wifi_tr[idx].a1,
			   (unsigned int)wifi_tr[idx].a2,
			   (unsigned int)wifi_tr[idx].a3,
			   (unsigned int)wifi_tr[idx].ret);
	}
}

/*
 *  実施37：JTAGでの生メモリ直読み用に，`wifi_tr`配列本体・
 *  `wifi_tr_pos`・エントリサイズを一度だけsyslogへ出力する
 *  （アドレス確認の裏取り用．配列自体は`nm`でも確認可能だが，
 *  実行時の実際の値との突合せのため）．
 */
void
wifi_trace_dump_addr(void)
{
	syslog(LOG_NOTICE, "wifi_trace_addr: wifi_tr=%08x wifi_tr_pos=%08x",
		   (unsigned int)(uintptr_t)wifi_tr,
		   (unsigned int)(uintptr_t)&wifi_tr_pos);
	syslog(LOG_NOTICE, "wifi_trace_addr: sizeof(entry)=%d pos=%d frozen=%d",
		   (int_t)sizeof(wifi_trace_t), (int_t)wifi_tr_pos,
		   (int_t)wifi_trace_frozen);
}

/*
 *  --wrap対象（blob内部シンボル．公開ヘッダなし＝nmで発見した実名．
 *  実引数の型は不明のため，RISC-V呼出規約のレジスタ渡し（a0-a3）を
 *  そのまま素通しする汎用プロトタイプで代用する．
 */
#define WIFI_TRACE_WRAP4(name, id) \
	extern long __real_##name(long a0, long a1, long a2, long a3); \
	long __wrap_##name(long a0, long a1, long a2, long a3) \
	{ \
		long ret = __real_##name(a0, a1, a2, a3); \
		wifi_trace_push((id), 0U, (uintptr_t)a0, (uintptr_t)a1, \
						 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret); \
		return(ret); \
	}

WIFI_TRACE_WRAP4(wifi_hw_start, 1)
WIFI_TRACE_WRAP4(wifi_hmac_init, 2)
WIFI_TRACE_WRAP4(wifi_lmac_init, 3)
WIFI_TRACE_WRAP4(wDev_Rxbuf_Init, 4)
WIFI_TRACE_WRAP4(esf_buf_setup, 5)
WIFI_TRACE_WRAP4(esf_buf_setup_static, 6)
WIFI_TRACE_WRAP4(wdev_set_promis, 7)
/*  追記23：sta_rx_cb（RXフレームがドライバへ）にRTC計測を追加．
 *  frozen trace非依存でscan中も数える．RTC[36]=0x50000090． */
extern long __real_sta_rx_cb(long a0, long a1, long a2, long a3);
long __wrap_sta_rx_cb(long a0, long a1, long a2, long a3)
{
	(*(volatile uint32_t *)0x50000090U)++;
	long ret = __real_sta_rx_cb(a0, a1, a2, a3);
	wifi_trace_push(8U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	return(ret);
}

/*
 *  DIAGNOSTIC（実施58，一時的）：RXデータパスの各段にRTC固定番地
 *  カウンタを仕込み，「MAC割込みは約160/秒発火するのにsta_rx_cbが0」
 *  という現象が，RX成功処理チェーンのどこで途切れているかを特定する．
 *  チェーン（典型）：MAC RX ISR → lmacProcessRxSucData → ppRxPkt
 *  → wdevProcessRxSucDataAll → wDev_ProcessRxSucData → wDev_IndicateFrame
 *  → sta_rx_cb．いずれもlibpp.aに定義され，ppRxPkt/lmacProcessRxSucData/
 *  wdevProcessRxSucDataAllは外部参照（U）を持つため--wrapが確実に効く
 *  （wDev_ProcessRxSucData/wDev_IndicateFrameは局所コールのみの可能性が
 *  あり，発火すれば到達確定・0は両義的）．RTCはDirect Bootでゼロクリア
 *  されないため計測前にJTAGでゼロ書込みして差分を取る（実施57で確立）．
 *    RTC[37]=0x50000094 lmacProcessRxSucData
 *    RTC[38]=0x50000098 ppRxPkt
 *    RTC[39]=0x5000009C wdevProcessRxSucDataAll
 *    RTC[40]=0x500000A0 wDev_ProcessRxSucData
 *    RTC[41]=0x500000A4 wDev_IndicateFrame
 */
#define WIFI_TRACE_WRAP4_RTC(name, id, rtcaddr) \
	extern long __real_##name(long a0, long a1, long a2, long a3); \
	long __wrap_##name(long a0, long a1, long a2, long a3) \
	{ \
		(*(volatile uint32_t *)(rtcaddr))++; \
		long ret = __real_##name(a0, a1, a2, a3); \
		wifi_trace_push((id), 0U, (uintptr_t)a0, (uintptr_t)a1, \
						 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret); \
		return(ret); \
	}

WIFI_TRACE_WRAP4_RTC(lmacProcessRxSucData, 50, 0x50000094U)
WIFI_TRACE_WRAP4_RTC(ppRxPkt, 51, 0x50000098U)
WIFI_TRACE_WRAP4_RTC(wdevProcessRxSucDataAll, 52, 0x5000009CU)
WIFI_TRACE_WRAP4_RTC(wDev_ProcessRxSucData, 53, 0x500000A0U)
WIFI_TRACE_WRAP4_RTC(wDev_IndicateFrame, 54, 0x500000A4U)

WIFI_TRACE_WRAP4(wifi_recycle_rx_pkt, 9)
WIFI_TRACE_WRAP4(esf_buf_alloc_dynamic, 11)
WIFI_TRACE_WRAP4(wdev_data_init, 12)
WIFI_TRACE_WRAP4(wifi_set_rx_policy, 13)
WIFI_TRACE_WRAP4(adc2_wifi_acquire, 14)
WIFI_TRACE_WRAP4(ieee80211_set_hmac_stop, 15)
WIFI_TRACE_WRAP4(wifi_mode_set, 16)
WIFI_TRACE_WRAP4(_do_wifi_start, 17)
WIFI_TRACE_WRAP4(ieee80211_update_phy_country, 18)
WIFI_TRACE_WRAP4(wifi_start_process, 19)
WIFI_TRACE_WRAP4(wifi_set_promis_process, 20)
/*
 *  register_chipv7_phyは呼出し**直前**（実施36スナップショット）に
 *  加え，通常どおり戻り値もトレースする（実施36の下でカスタム実装に
 *  差し替え．generic WIFI_TRACE_WRAP4は使わない）．
 */
extern long __real_register_chipv7_phy(long a0, long a1, long a2, long a3);
long
__wrap_register_chipv7_phy(long a0, long a1, long a2, long a3)
{
	long	ret;

	/*  追記19：RF FULL較正の実行実測（RTC[34]=0x50000088が入場数，
	 *  RTC[35]=0x5000008Cが復帰時のret）．nm/ログ非依存の確実な計測． */
	(*(volatile uint32_t *)0x50000088U)++;
	wifi_phyinit_capture_entry();
	ret = __real_register_chipv7_phy(a0, a1, a2, a3);
	*(volatile uint32_t *)0x5000008CU = (uint32_t)ret ^ 0xA5A5A5A5U;
	wifi_trace_push(21U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	/*
	 *  実施37（コーディネータ指示）：ここでリングバッファを凍結する．
	 *  以降（esp_wifi_start()・チャネルホップスキャン等）の呼出しは
	 *  一切記録しない．これにより，`wifi_tr`はesp_wifi_init()開始から
	 *  phy_init完了までの呼出し列**だけ**を保持し続ける（後続の
	 *  スキャン活動で上書きされない）——JTAGでいつ読みに行っても
	 *  この境界内の完全な列がロスレスに残っている．
	 */
	wifi_trace_frozen = 1U;
	return(ret);
}
/*
 *  DIAGNOSTIC (temporary，Priority 2)：register_chipv7_phy()内部
 *  （closed-source）が実際に呼ぶROM常駐PHY関数（esp32c6.rom.phy.ld
 *  由来．libphy.aの未解決参照として`nm`で実在確認済み）を関数粒度で
 *  トレースする．JTAG単一命令ステップの代替．
 */
WIFI_TRACE_WRAP4(chip_v7_set_chan_ana, 22)
WIFI_TRACE_WRAP4(set_channel_rfpll_freq, 23)
WIFI_TRACE_WRAP4(set_rfpll_freq, 24)
WIFI_TRACE_WRAP4(write_rfpll_sdm, 25)
WIFI_TRACE_WRAP4(wait_rfpll_cal_end, 26)
WIFI_TRACE_WRAP4(enable_agc, 27)
WIFI_TRACE_WRAP4(disable_agc, 28)
WIFI_TRACE_WRAP4(mac_enable_bb, 29)
WIFI_TRACE_WRAP4(fe_reg_init, 30)
WIFI_TRACE_WRAP4(fe_txrx_reset, 31)
WIFI_TRACE_WRAP4(phy_bbpll_cal, 32)
WIFI_TRACE_WRAP4(set_rxclk_en, 33)
WIFI_TRACE_WRAP4(set_txclk_en, 34)
WIFI_TRACE_WRAP4(write_chan_freq, 35)
WIFI_TRACE_WRAP4(restart_cal, 36)
WIFI_TRACE_WRAP4(i2cmst_reg_init, 37)
WIFI_TRACE_WRAP4(rxiq_cal_init, 38)
WIFI_TRACE_WRAP4(set_rx_gain_cal_dc_new, 39)
/*
 *  実施53：coex_init/coex_schm_process_restartは実施37でring bufferを
 *  凍結するregister_chipv7_phyのはるか後（wifi_start以降）に呼ばれる
 *  ため，generic WIFI_TRACE_WRAP4のwifi_trace_push経由の記録は
 *  凍結済みで無条件に捨てられ，実際に呼ばれたかどうかを確認できない．
 *  frozen非依存の通常BSSグローバルカウンタで補完する（RTC-RAMは
 *  真の電源断でしかクリアされずwarm resetでは前回値が残るため，
 *  今回はBSS＝毎回のDirect Bootで確実にゼロ初期化される領域を使う）．
 */
volatile uint32_t	wifi_coex_init_count;
volatile uint32_t	wifi_coex_init_ret;
volatile uint32_t	wifi_coex_schm_process_restart_count;

extern long __real_coex_init(long a0, long a1, long a2, long a3);
long
__wrap_coex_init(long a0, long a1, long a2, long a3)
{
	long	ret;

	wifi_coex_init_count++;
	ret = __real_coex_init(a0, a1, a2, a3);
	wifi_coex_init_ret = (uint32_t)ret;
	wifi_trace_push(40U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	return(ret);
}

extern long __real_coex_schm_process_restart(long a0, long a1, long a2, long a3);
long
__wrap_coex_schm_process_restart(long a0, long a1, long a2, long a3)
{
	long	ret;

	wifi_coex_schm_process_restart_count++;
	ret = __real_coex_schm_process_restart(a0, a1, a2, a3);
	wifi_trace_push(41U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	return(ret);
}
/*
 *  実施38（コーディネータ指示）：set_rx_gain_cal_dc_new()内部で実際に
 *  呼ばれるROM常駐PBUSヘルパを直接--wrapする．手動逆アセンブルで
 *  一度誤り（別イメージのシンボルテーブルで突合せ・エピローグの
 *  読み違い）を犯したため，確実なリングバッファトレースに切り替える．
 *  pbus_rx_dco_cal_1step_newは実引数5個（a0-a4）——呼出し箇所の逆
 *  アセンブルで確認済み．generic WIFI_TRACE_WRAP4（4引数固定）では
 *  a4が転送されず壊れるため，専用の5引数版を用意する．a4はポインタ
 *  （出力先）のため，トレースにはa3までを記録する．
 */
extern long __real_pbus_rx_dco_cal_1step_new(long a0, long a1, long a2,
											  long a3, long a4);
long
__wrap_pbus_rx_dco_cal_1step_new(long a0, long a1, long a2, long a3, long a4)
{
	long	ret = __real_pbus_rx_dco_cal_1step_new(a0, a1, a2, a3, a4);
	wifi_trace_push(44U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	return(ret);
}
WIFI_TRACE_WRAP4(ram_pbus_force_mode, 45)
WIFI_TRACE_WRAP4(rx_pbus_reset, 46)

/*
 *  DIAGNOSTIC（実施21続き）：`coex_schm_lock`はROM内部で
 *  `coex_schm_env_ptr`（NULLだとクラッシュ）を無条件に参照するため，
 *  呼出し**前**にもpushする（内部でクラッシュしても「呼ばれたこと」
 *  自体はカウンタに残る．通常のWIFI_TRACE_WRAP4は戻り値を待って
 *  からpushするためクラッシュ時は記録されない）．
 */
extern long __real_coex_schm_lock(long a0, long a1, long a2, long a3);
long
__wrap_coex_schm_lock(long a0, long a1, long a2, long a3)
{
	extern void *coex_schm_env_ptr;

	wifi_trace_push(42U, 0U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr,
					 (uintptr_t)a2, (uintptr_t)a3, 0xEEEEEEEEUL);
	{
		long ret = __real_coex_schm_lock(a0, a1, a2, a3);
		wifi_trace_push(42U, 1U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr,
						 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
		return(ret);
	}
}

extern long __real_coex_schm_interval_get(long a0, long a1, long a2, long a3);
long
__wrap_coex_schm_interval_get(long a0, long a1, long a2, long a3)
{
	extern void *coex_schm_env_ptr;

	wifi_trace_push(43U, 0U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr,
					 (uintptr_t)a2, (uintptr_t)a3, 0xEEEEEEEEUL);
	{
		long ret = __real_coex_schm_interval_get(a0, a1, a2, a3);
		wifi_trace_push(43U, 1U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr,
						 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
		return(ret);
	}
}

/*
 *  DIAGNOSTIC (temporary，Priority 2)：チャネルホップ毎のレジスタ
 *  スナップショット．INTMTX_STATUS0/1/2（実施6由来）＋PHY/AGC領域
 *  （0x600a7000〜0x600a7fff．実施2で単発スポット比較したAGC値
 *  0x600a7128を含む）を，チャネルホップ確定関数
 *  `scan_inter_channel_timeout_process`（libnet80211.a．
 *  scan_next_channelへ内部でtail callする）の呼出し毎に採取する．
 */
#define WIFI_REGSNAP_SIZE 32
#define WIFI_PHY_AGC_BASE 0x600a7000UL
#define WIFI_PHY_AGC_WORDS 1024U	/* 4KB / 4 */
#define WIFI_PHY_AGC_SPOT_OFF 0x128UL	/* 実施2のAGC値 */
#define WIFI_INTMTX_S0 0x60010134UL
#define WIFI_INTMTX_S1 0x60010138UL
#define WIFI_INTMTX_S2 0x6001013cUL

/*
 *  DIAGNOSTIC（実施24／Priority 2続き）：FE/BBのenable/reset/
 *  clock-gate系レジスタ．コーディネータの指示により，AGCスポット
 *  レジスタだけでなくこの種のビットを追加でスナップショットする．
 *
 *  ROM ELF（esp32c6_rev0_rom.elf）の`enable_agc`/`disable_agc`/
 *  `fe_txrx_reset`/`mac_enable_bb`を逆アセンブルして特定：
 *
 *  - 0x600a7030（AGC領域内．公開ヘッダ未収録）：bit29が
 *    enable_agc/disable_agcが直接トグルするAGC本体のenableビット
 *    （disable_agcはセット＝無効化，enable_agcはクリア＝有効化）．
 *  - 0x600a0460（FE領域内．公開ヘッダ未収録）：bit25-26を
 *    fe_txrx_resetがクリア→セットの順で叩く．典型的な
 *    reset assert/deassertパルスパターン．
 *  - MODEM_SYSCON_CLK_CONF_REG（0x600a9804，公開ヘッダで確認済み）：
 *    bit0-8=WIFIBBの各クロック速度enable，bit9=WIFIMAC_EN，
 *    bit10=WIFI_APB_EN．PCR_MODEM_APB_CLK_EN（既に反証済み・別
 *    モジュール）とは異なる，MODEM_SYSCONモジュール側のクロック
 *    ゲートで，本ラウンドまで未確認だった経路．
 *  - MODEM_SYSCON_MODEM_RST_CONF_REG（0x600a9810，公開ヘッダで
 *    確認済み）：bit8=RST_WIFIBB，bit10=RST_WIFIMAC，bit14=RST_FE．
 *    いずれかがASP3側だけ1（reset assert状態）に固定されていれば，
 *    デジタルBB/MAC/FEが完全に沈黙する説明として筋が通る．
 *  - MODEM_SYSCON_WIFI_BB_CFG_REG（0x600a981c，公開ヘッダで
 *    確認済み．`mac_enable_bb`のbit16/1，`bb_bss_cbw40_dig`の
 *    bit2が書き込む対象）．
 */
#define WIFI_AGC_ENABLE_REG 0x600a7030UL	/* enable_agc/disable_agc: bit29 */
#define WIFI_FE_TXRX_RESET_REG 0x600a0460UL	/* fe_txrx_reset: bit25-26 */
#define WIFI_MODEM_SYSCON_CLK_CONF_REG 0x600a9804UL
#define WIFI_MODEM_SYSCON_RST_CONF_REG 0x600a9810UL
#define WIFI_MODEM_SYSCON_WIFI_BB_CFG_REG 0x600a981cUL

/*
 *  実施25で，FE領域（0x600a0000〜0x600a3000）・MODEM_SYSCON領域
 *  （0x600a9800〜0x600af000）の全域チェックサムを試したが，(1)未文書化
 *  領域の一部読出しが1ワードあたり数百usという異常な遅延を示し
 *  （120ms/チャネルだったチェックポイント間隔が1.8秒/チャネルまで
 *  悪化），(2)チェックサム自体はASP3・NuttX双方で同一パターン
 *  （約4MHzのフリーランカウンタらしき値が支配的．frozen-vs-varying
 *  ではない＝陰性）だったため，コストに見合わず本体には残さず
 *  revertした．詳細はdocs/wifi-shim-c6.md 実施25参照．
 */

/*
 *  DIAGNOSTIC（実施20）：phy_param（libphy.a）+164のビットマスク．
 *  bit10（0x400）がセットされているとrxiq_cal_init()が
 *  chip_v7_set_chan_ana()呼出しを含む全処理をスキップすることを
 *  逆アセンブルで確認済み．esp_wifi_start()直後の1回読みでは
 *  ASP3・NuttX間で完全に一致（0x0190d6a8）したため，
 *  チャンネル毎（regsnap採取毎）に読んで時間変化を追う．
 */
extern uint8_t	phy_param[];
#define WIFI_PHY_PARAM_FLAGS_OFF 164UL

typedef struct {
	uint32_t	t_us_low;
	uint32_t	intmtx0, intmtx1, intmtx2;
	uint32_t	agc_spot;
	uint32_t	phy_agc_sum;
	uint32_t	phy_param_flags;
	uint32_t	agc_enable_reg;
	uint32_t	fe_txrx_reset_reg;
	uint32_t	modem_clk_conf;
	uint32_t	modem_rst_conf;
	uint32_t	modem_wifi_bb_cfg;
} wifi_regsnap_t;

static wifi_regsnap_t	regsnap[WIFI_REGSNAP_SIZE];
static uint32_t			regsnap_pos;

void
wifi_regsnap_reset(void)
{
	regsnap_pos = 0U;
	memset(regsnap, 0, sizeof(regsnap));
}

void
wifi_regsnap_capture(void)
{
	uint32_t		pos = regsnap_pos++;
	wifi_regsnap_t	*e = &regsnap[pos % WIFI_REGSNAP_SIZE];
	volatile uint32_t *base = (volatile uint32_t *)WIFI_PHY_AGC_BASE;
	uint32_t		sum = 0U;
	uint32_t		i;

	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->intmtx0 = *(volatile uint32_t *)WIFI_INTMTX_S0;
	e->intmtx1 = *(volatile uint32_t *)WIFI_INTMTX_S1;
	e->intmtx2 = *(volatile uint32_t *)WIFI_INTMTX_S2;
	e->agc_spot = *(volatile uint32_t *)(WIFI_PHY_AGC_BASE + WIFI_PHY_AGC_SPOT_OFF);
	for (i = 0U; i < WIFI_PHY_AGC_WORDS; i++) {
		sum += base[i];
	}
	e->phy_agc_sum = sum;
	e->phy_param_flags = *(volatile uint32_t *)(phy_param + WIFI_PHY_PARAM_FLAGS_OFF);
	e->agc_enable_reg = *(volatile uint32_t *)WIFI_AGC_ENABLE_REG;
	e->fe_txrx_reset_reg = *(volatile uint32_t *)WIFI_FE_TXRX_RESET_REG;
	e->modem_clk_conf = *(volatile uint32_t *)WIFI_MODEM_SYSCON_CLK_CONF_REG;
	e->modem_rst_conf = *(volatile uint32_t *)WIFI_MODEM_SYSCON_RST_CONF_REG;
	e->modem_wifi_bb_cfg = *(volatile uint32_t *)WIFI_MODEM_SYSCON_WIFI_BB_CFG_REG;
}

void
wifi_regsnap_dump(void)
{
	uint32_t	total = regsnap_pos;
	uint32_t	n = (total < WIFI_REGSNAP_SIZE) ? total : WIFI_REGSNAP_SIZE;
	uint32_t	start = (total < WIFI_REGSNAP_SIZE) ? 0U : (total % WIFI_REGSNAP_SIZE);
	uint32_t	i, idx;

	syslog(LOG_NOTICE, "wifi_regsnap: total=%d (showing %d)",
		   (int_t)total, (int_t)n);
	for (i = 0U; i < n; i++) {
		idx = (start + i) % WIFI_REGSNAP_SIZE;
		syslog(LOG_NOTICE, "wifi_regsnap: [%d] t=%d intmtx=%08x/%08x/%08x",
			   (int_t)i, (int_t)regsnap[idx].t_us_low,
			   (unsigned int)regsnap[idx].intmtx0,
			   (unsigned int)regsnap[idx].intmtx1,
			   (unsigned int)regsnap[idx].intmtx2);
		syslog(LOG_NOTICE, "wifi_regsnap:   agc_spot=%08x phy_agc_sum=%08x",
			   (unsigned int)regsnap[idx].agc_spot,
			   (unsigned int)regsnap[idx].phy_agc_sum);
		syslog(LOG_NOTICE, "wifi_regsnap:   phy_param_flags=%08x",
			   (unsigned int)regsnap[idx].phy_param_flags);
		syslog(LOG_NOTICE, "wifi_regsnap:   agc_en=%08x fe_txrx_rst=%08x",
			   (unsigned int)regsnap[idx].agc_enable_reg,
			   (unsigned int)regsnap[idx].fe_txrx_reset_reg);
		syslog(LOG_NOTICE, "wifi_regsnap:   clk_conf=%08x rst_conf=%08x bb_cfg=%08x",
			   (unsigned int)regsnap[idx].modem_clk_conf,
			   (unsigned int)regsnap[idx].modem_rst_conf,
			   (unsigned int)regsnap[idx].modem_wifi_bb_cfg);
	}
}

/*
 *  DIAGNOSTIC（実施26／タイミング感度調査）：`esp_shim_task_delay()`
 *  （osi `_task_delay`の実体）が呼ばれる度に，要求されたtick引数と
 *  実際にdly_tskで経過した実時間（us）を記録する専用リングバッファ．
 *  wifi_trace_push()の512エントリ共有バッファに混ぜると他のIDに
 *  埋もれてロスする恐れがあるため独立させる．
 */
#define WIFI_TASKDELAY_SIZE 128
typedef struct {
	uint32_t	tick;
	uint32_t	t0;
	uint32_t	elapsed_us;
} wifi_taskdelay_t;
static wifi_taskdelay_t	wifi_taskdelay[WIFI_TASKDELAY_SIZE];
static volatile uint32_t	wifi_taskdelay_pos;

void
wifi_taskdelay_reset(void)
{
	wifi_taskdelay_pos = 0U;
	memset(wifi_taskdelay, 0, sizeof(wifi_taskdelay));
}

void
wifi_taskdelay_capture(uint32_t tick, uint32_t t0, uint32_t elapsed_us)
{
	uint32_t			pos = wifi_taskdelay_pos++;
	wifi_taskdelay_t	*e = &wifi_taskdelay[pos % WIFI_TASKDELAY_SIZE];

	e->tick = tick;
	e->t0 = t0;
	e->elapsed_us = elapsed_us;
}

void
wifi_taskdelay_dump(void)
{
	uint32_t	total = wifi_taskdelay_pos;
	uint32_t	n = (total < WIFI_TASKDELAY_SIZE) ? total : WIFI_TASKDELAY_SIZE;
	uint32_t	start = (total < WIFI_TASKDELAY_SIZE) ? 0U : (total % WIFI_TASKDELAY_SIZE);
	uint32_t	i, idx;

	syslog(LOG_NOTICE, "wifi_taskdelay: total=%d (showing %d)",
		   (int_t)total, (int_t)n);
	for (i = 0U; i < n; i++) {
		idx = (start + i) % WIFI_TASKDELAY_SIZE;
		syslog(LOG_NOTICE, "wifi_taskdelay: [%d] t0=%d tick=%d elapsed_us=%d",
			   (int_t)i, (int_t)wifi_taskdelay[idx].t0,
			   (int_t)wifi_taskdelay[idx].tick,
			   (int_t)wifi_taskdelay[idx].elapsed_us);
	}
}

extern long __real_scan_inter_channel_timeout_process(long a0, long a1, long a2, long a3);
long
__wrap_scan_inter_channel_timeout_process(long a0, long a1, long a2, long a3)
{
	long	ret = __real_scan_inter_channel_timeout_process(a0, a1, a2, a3);
	wifi_regsnap_capture();
	return(ret);
}

/*
 *  DIAGNOSTIC (temporary，Priority 2)：esf_buf_alloc（プールID a1=2＝
 *  スキャン中のチャネル毎バッファ）呼出しの度にレジスタスナップ
 *  ショットを採取する．実測でscan_inter_channel_timeout_processは
 *  一度も呼ばれない（実施19参照）ため，代わりにこちらを起点にする．
 */
extern long __real_esf_buf_alloc(long a0, long a1, long a2, long a3);
long
__wrap_esf_buf_alloc(long a0, long a1, long a2, long a3)
{
	long	ret = __real_esf_buf_alloc(a0, a1, a2, a3);
	wifi_trace_push(10U, 0U, (uintptr_t)a0, (uintptr_t)a1,
					 (uintptr_t)a2, (uintptr_t)a3, (uintptr_t)ret);
	if (a1 == 2) {
		wifi_regsnap_capture();
	}
	return(ret);
}

/*
 *  DIAGNOSTIC（実施23／Priority 2）：regi2c書込みの実測ログ．
 *
 *  `rom_i2c_writeReg`/`rom_i2c_writeReg_Mask`は`--wrap`では捕捉
 *  できない（`coex_schm_lock`と同様，blob内部の関数ポインタ
 *  テーブル`g_phyFuns`経由の間接呼出しのため．リンク時シンボル
 *  解決に依存する`--wrap`は原理的に無力——実施22で確認した手法と
 *  同じ制約）．
 *
 *  `g_phyFuns`はJTAGで実測して構造を特定済み（`register_chipv7_phy`
 *  →`phy_get_romfunc_addr()`の戻り値がそのままg_phyFuns代入元）：
 *    offset  76 (idx19): rom_chip_i2c_readReg
 *    offset  80 (idx20): rom_i2c_readReg       ← phy_i2c_init1/2が実際に使用
 *    offset  84 (idx21): rom_chip_i2c_writeReg
 *    offset  88 (idx22): rom_i2c_writeReg      ← phy_i2c_init1/2が実際に使用
 *    offset  92 (idx23): rom_i2c_readReg_Mask  ← 較正ループの比較読出しに使用
 *    offset  96 (idx24): rom_i2c_writeReg_Mask
 *
 *  `phy_get_romfunc_addr`はROM常駐だが通常のcall命令で参照される
 *  （function-pointer間接ではない）ため`--wrap`可能．戻り値
 *  （=テーブル本体へのポインタ）を横取りし，read/read_mask/write/
 *  write_mask枠を自前のトレース関数へ差し替えてから返す．
 *
 *  DIAGNOSTIC（コーディネータ指示：regi2c読み戻し比較）：実施23は
 *  write/write_mask枠のみを計装し「書込みシーケンスはほぼ完全一致」を
 *  確認したが，read/read_mask枠は無計装のまま（実施7で1回だけ手動で
 *  読み出した記録があるのみ）だった．較正ループ内部でblobが読み戻す
 *  実際の値（regi2cは`0x600a7xxx`のようなMMIO直読みでは観測できない
 *  唯一のアナログ経路）をASP3・NuttX間で直接突き合わせるため，
 *  write/write_maskと対称にread/read_maskも計装する．
 */
#define WIFI_REGI2C_SIZE 4096

typedef struct {
	uint32_t	t_us_low;
	uint8_t		block, host_id, reg_add, data;
	uint8_t		msb, lsb;	/* *_mask以外は0xFFを格納 */
	uint8_t		op;		/* 0=write,1=write_mask,2=read,3=read_mask */
} wifi_regi2c_t;

static wifi_regi2c_t	wifi_regi2c[WIFI_REGI2C_SIZE];
static volatile uint32_t wifi_regi2c_pos;

typedef void (*wifi_regi2c_write_fn_t)(uint8_t, uint8_t, uint8_t, uint8_t);
typedef void (*wifi_regi2c_write_mask_fn_t)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
typedef uint8_t (*wifi_regi2c_read_fn_t)(uint8_t, uint8_t, uint8_t);
typedef uint8_t (*wifi_regi2c_read_mask_fn_t2)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);

static wifi_regi2c_write_fn_t		wifi_regi2c_orig_write;
static wifi_regi2c_write_mask_fn_t	wifi_regi2c_orig_write_mask;
#ifdef WIFI_REGI2C_TRACE_READS
static wifi_regi2c_read_fn_t		wifi_regi2c_orig_read;
static wifi_regi2c_read_mask_fn_t2	wifi_regi2c_orig_read_mask;
#endif /* WIFI_REGI2C_TRACE_READS */

void
wifi_regi2c_reset(void)
{
	wifi_regi2c_pos = 0U;
	memset(wifi_regi2c, 0, sizeof(wifi_regi2c));
}

static void
wifi_regi2c_traced_write(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t data)
{
	uint32_t		pos = wifi_regi2c_pos++;
	wifi_regi2c_t	*e = &wifi_regi2c[pos % WIFI_REGI2C_SIZE];

	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->block = block;
	e->host_id = host_id;
	e->reg_add = reg_add;
	e->data = data;
	e->msb = 0xFFU;
	e->lsb = 0xFFU;
	e->op = 0U;
	if (wifi_regi2c_orig_write != NULL) {
		wifi_regi2c_orig_write(block, host_id, reg_add, data);
	}
}

static void
wifi_regi2c_traced_write_mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							   uint8_t msb, uint8_t lsb, uint8_t data)
{
	uint32_t		pos = wifi_regi2c_pos++;
	wifi_regi2c_t	*e = &wifi_regi2c[pos % WIFI_REGI2C_SIZE];

	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->block = block;
	e->host_id = host_id;
	e->reg_add = reg_add;
	e->data = data;
	e->msb = msb;
	e->lsb = lsb;
	e->op = 1U;
	if (wifi_regi2c_orig_write_mask != NULL) {
		wifi_regi2c_orig_write_mask(block, host_id, reg_add, msb, lsb, data);
	}
#ifdef WIFI_REGI2C_DELAY_DIV_ADC_WRITE
	/*
	 *  DIAGNOSTIC（実施40切り分け実験．結論確定済み・実施42参照）：
	 *  block=0x66,reg=0x04（I2C_BBPLL_DIV_ADC）へのwrite_mask直後の
	 *  読み戻しがASP3・NuttXで食い違う現象（実施39）について，
	 *  計装オーバーヘッド差によるアーティファクトかどうかをROM常駐
	 *  `esp_rom_delay_us`（両機種で同一機械語）による対称遅延で
	 *  切り分けた．結果は不変（アーティファクト説は反証．実施40／
	 *  42で調査完了・結論確定済み）．再検証が必要な場合のみ有効化
	 *  すること．
	 */
	if ((block == 0x66U) && (host_id == 0U) && (reg_add == 4U)) {
		esp_rom_delay_us(5U);
	}
#endif /* WIFI_REGI2C_DELAY_DIV_ADC_WRITE */
}

#ifdef WIFI_REGI2C_TRACE_READS
static uint8_t
wifi_regi2c_traced_read(uint8_t block, uint8_t host_id, uint8_t reg_add)
{
	uint32_t		pos;
	wifi_regi2c_t	*e;
	uint8_t			ret = 0U;

	if (wifi_regi2c_orig_read != NULL) {
		ret = wifi_regi2c_orig_read(block, host_id, reg_add);
	}
	pos = wifi_regi2c_pos++;
	e = &wifi_regi2c[pos % WIFI_REGI2C_SIZE];
	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->block = block;
	e->host_id = host_id;
	e->reg_add = reg_add;
	e->data = ret;
	e->msb = 0xFFU;
	e->lsb = 0xFFU;
	e->op = 2U;
	return(ret);
}

static uint8_t
wifi_regi2c_traced_read_mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							  uint8_t msb, uint8_t lsb)
{
	uint32_t		pos;
	wifi_regi2c_t	*e;
	uint8_t			ret = 0U;

	if (wifi_regi2c_orig_read_mask != NULL) {
		ret = wifi_regi2c_orig_read_mask(block, host_id, reg_add, msb, lsb);
	}
	pos = wifi_regi2c_pos++;
	e = &wifi_regi2c[pos % WIFI_REGI2C_SIZE];
	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->block = block;
	e->host_id = host_id;
	e->reg_add = reg_add;
	e->data = ret;
	e->msb = msb;
	e->lsb = lsb;
	e->op = 3U;
	return(ret);
}
#endif /* WIFI_REGI2C_TRACE_READS */

/*
 *  当初`phy_get_romfunc_addr`（`register_chipv7_phy`が呼ぶ，ROM常駐
 *  テーブルへのポインタを返す関数）を`--wrap`する案だったが，
 *  リンク後`__wrap_phy_get_romfunc_addr`が結線されないことが判明．
 *  原因：呼出し元`register_chipv7_phy`と`phy_get_romfunc_addr`の
 *  定義が**同一オブジェクトファイル（libphy.aの同一.o）内**にあり，
 *  この呼出しはコンパイラが直接のローカル参照として解決してしまう
 *  ため，リンカのグローバルシンボル解決を経由せず`--wrap`が原理的に
 *  効かない（`coex_schm_lock`とは別種の制約）．
 *
 *  代替案：呼出し先テーブル自体はROM起動時（Direct Bootでの
 *  `_flash_entry`到達前）に既に固定アドレスへ格納済みであることを
 *  実機JTAGで確認済み（`esp_wifi_init()`を呼ぶ前，起動直後でも
 *  同じROM関数アドレス列が読める＝実行時にphy_get_romfunc_addr()が
 *  都度書き込んでいるのではなく，ROM側が起動時に用意する固定
 *  テーブル）．よってアドレスは既知・固定であり，起動の可能な限り
 *  早い時点で直接パッチしてよい．
 *  `g_phyFuns`（ASP3側リンクの大域ポインタ変数．そのVALUEがこの
 *  固定テーブルを指す）とは無関係にテーブル自体へ直接書き込む．
 */
#define WIFI_ROM_PHYFUNS_TABLE_ADDR  0x4087f954UL
#define WIFI_PHYFUNS_IDX_I2C_READ        20U
#define WIFI_PHYFUNS_IDX_I2C_WRITE       22U
#define WIFI_PHYFUNS_IDX_I2C_READ_MASK   23U
#define WIFI_PHYFUNS_IDX_I2C_WRITE_MASK  24U

void
wifi_regi2c_patch_install(void)
{
	uint32_t	*tbl = (uint32_t *)WIFI_ROM_PHYFUNS_TABLE_ADDR;

	wifi_regi2c_orig_write =
		(wifi_regi2c_write_fn_t)(uintptr_t)tbl[WIFI_PHYFUNS_IDX_I2C_WRITE];
	wifi_regi2c_orig_write_mask =
		(wifi_regi2c_write_mask_fn_t)(uintptr_t)tbl[WIFI_PHYFUNS_IDX_I2C_WRITE_MASK];
	tbl[WIFI_PHYFUNS_IDX_I2C_WRITE] = (uint32_t)(uintptr_t)wifi_regi2c_traced_write;
	tbl[WIFI_PHYFUNS_IDX_I2C_WRITE_MASK] = (uint32_t)(uintptr_t)wifi_regi2c_traced_write_mask;
#ifdef WIFI_REGI2C_TRACE_READS
	/*
	 *  実施39〜42：read/read_mask枠のトレースは目的（regi2c読み戻し
	 *  比較）を達成し完了済み．常時有効のままだと，このwrapper1回
	 *  あたりのオーバーヘッド（関数呼出し1段＋esp_shim_time_us()＋
	 *  リングバッファ書込み）が，phy_init内部のタイトな逐次比較／
	 *  ポーリングループ（block=0x62等）の反復回数に掛け算されて
	 *  効いてしまい，通常のスキャン所要時間を数秒から分単位まで
	 *  悪化させることを実機で確認した（ボード再接続後の実行で発覚．
	 *  真のハングではなくROM常駐regi2c読み出し関数群を無限に近い
	 *  回数再訪しているだけとJTAGで確認済み）．再度read側の比較が
	 *  必要になった場合のみ，このマクロを定義してビルドすること．
	 */
	wifi_regi2c_orig_read =
		(wifi_regi2c_read_fn_t)(uintptr_t)tbl[WIFI_PHYFUNS_IDX_I2C_READ];
	wifi_regi2c_orig_read_mask =
		(wifi_regi2c_read_mask_fn_t2)(uintptr_t)tbl[WIFI_PHYFUNS_IDX_I2C_READ_MASK];
	tbl[WIFI_PHYFUNS_IDX_I2C_READ] = (uint32_t)(uintptr_t)wifi_regi2c_traced_read;
	tbl[WIFI_PHYFUNS_IDX_I2C_READ_MASK] = (uint32_t)(uintptr_t)wifi_regi2c_traced_read_mask;
#endif /* WIFI_REGI2C_TRACE_READS */
}

void
wifi_regi2c_dump(void)
{
	uint32_t	total = wifi_regi2c_pos;
	uint32_t	n = (total < WIFI_REGI2C_SIZE) ? total : WIFI_REGI2C_SIZE;
	uint32_t	start = (total < WIFI_REGI2C_SIZE) ? 0U : (total % WIFI_REGI2C_SIZE);
	uint32_t	i, idx;

	syslog(LOG_NOTICE, "wifi_regi2c: total=%d (showing %d)", (int_t)total, (int_t)n);
	for (i = 0U; i < n; i++) {
		idx = (start + i) % WIFI_REGI2C_SIZE;
		/*  syslog()はt_syslog.hのsyslog_5までしか無く6引数は無言で
		 *  破損する（実際に発生・確認済み）ため，op引数は2行目へ移す．  */
		syslog(LOG_NOTICE, "wifi_regi2c: [%d] t=%d block=%02x host=%d reg=%02x",
			   (int_t)i, (int_t)wifi_regi2c[idx].t_us_low,
			   (unsigned int)wifi_regi2c[idx].block,
			   (int_t)wifi_regi2c[idx].host_id,
			   (unsigned int)wifi_regi2c[idx].reg_add);
		syslog(LOG_NOTICE, "wifi_regi2c:   op=%d data=%02x msb=%d lsb=%d",
			   (int_t)wifi_regi2c[idx].op,
			   (unsigned int)wifi_regi2c[idx].data,
			   (int_t)wifi_regi2c[idx].msb,
			   (int_t)wifi_regi2c[idx].lsb);
	}
}

void
wifi_regi2c_dump_count(void)
{
	syslog(LOG_NOTICE, "wifi_regi2c_cnt: total=%d", (int_t)wifi_regi2c_pos);
}

/*
 *  DIAGNOSTIC（実施36／コーディネータ指示：phy_init呼出し境界の
 *  一点スナップショット）．BBPLLロック/較正ステータス
 *  （I2C_BBPLL block=0x66,hostid=0,reg_add=8: bit7=OR_LOCK,
 *  bit6=OR_CAL_END,bit5=OR_CAL_OVF．soc/regi2c_bbpll.h）はregi2c経由
 *  でしか読めないため，wifi_regi2c_patch_install()と同じROM常駐
 *  関数ポインタテーブル（起動時に固定済み．実施23で実証済み）から
 *  read_mask枠（idx23=rom_i2c_readReg_Mask）を取得して直接呼ぶ．
 *  write枠のパッチ（差し替え）とは異なり，読み出しは横取りせず
 *  素通しで良いため，パッチインストール不要．
 *
 *  （regi2c読み戻し比較の追加により，`wifi_regi2c_patch_install()`が
 *  read_mask枠も`wifi_regi2c_traced_read_mask`へ差し替え済みとなった
 *  ため，本関数がここで`romtbl[]`から取得するのは実際にはトレース
 *  関数経由になる＝二重にはならず，単にこの一点読出しもリングバッファ
 *  へ記録される副次効果が生じるだけで，動作・戻り値とも変化しない）．
 */

typedef uint8_t (*wifi_regi2c_read_mask_fn_t)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);

static wifi_phyinit_snap_t	wifi_phyinit_snap;

void
wifi_phyinit_capture_entry(void)
{
	volatile uint32_t	*agc_base = (volatile uint32_t *)WIFI_PHY_AGC_BASE;
	uint32_t			sum = 0U;
	uint32_t			i;
	uint32_t			*romtbl = (uint32_t *)WIFI_ROM_PHYFUNS_TABLE_ADDR;
	wifi_regi2c_read_mask_fn_t read_mask_fn =
		(wifi_regi2c_read_mask_fn_t)(uintptr_t)romtbl[WIFI_PHYFUNS_IDX_I2C_READ_MASK];

	if (wifi_phyinit_snap.captured != 0U) {
		/* 既に採取済み（2回目以降の呼出しは無視．最初の1回のみ記録） */
		return;
	}

	wifi_phyinit_snap.t_us_low = (uint32_t)esp_shim_time_us();

	wifi_phyinit_snap.agc_spot =
		*(volatile uint32_t *)(WIFI_PHY_AGC_BASE + WIFI_PHY_AGC_SPOT_OFF);
	for (i = 0U; i < WIFI_PHY_AGC_WORDS; i++) {
		sum += agc_base[i];
	}
	wifi_phyinit_snap.phy_agc_sum = sum;

	wifi_phyinit_snap.modem_lpcon_clk_conf =
		*(volatile uint32_t *)0x600af018UL;	/* MODEM_LPCON_CLK_CONF_REG */
	wifi_phyinit_snap.modem_syscon_clk_conf =
		*(volatile uint32_t *)WIFI_MODEM_SYSCON_CLK_CONF_REG;
	wifi_phyinit_snap.modem_syscon_rst_conf =
		*(volatile uint32_t *)WIFI_MODEM_SYSCON_RST_CONF_REG;
	wifi_phyinit_snap.modem_syscon_wifi_bb_cfg =
		*(volatile uint32_t *)WIFI_MODEM_SYSCON_WIFI_BB_CFG_REG;

	wifi_phyinit_snap.pmu_icg_modem =
		*(volatile uint32_t *)0x6009600cUL;	/* PMU_HP_ACTIVE_ICG_MODEM_REG */
	wifi_phyinit_snap.pmu_hp_regulator0 =
		*(volatile uint32_t *)0x60096028UL;	/* PMU_HP_ACTIVE_HP_REGULATOR0_REG */

	/* I2C_BBPLL=0x66, hostid=0, reg_add=8, bit7/6/5 */
	wifi_phyinit_snap.bbpll_or_lock    = read_mask_fn(0x66U, 0U, 8U, 7U, 7U);
	wifi_phyinit_snap.bbpll_or_cal_end = read_mask_fn(0x66U, 0U, 8U, 6U, 6U);
	wifi_phyinit_snap.bbpll_or_cal_ovf = read_mask_fn(0x66U, 0U, 8U, 5U, 5U);

	wifi_phyinit_snap.captured = 1U;
}

void
wifi_phyinit_dump(void)
{
	if (wifi_phyinit_snap.captured == 0U) {
		syslog(LOG_NOTICE, "wifi_phyinit: NOT CAPTURED (register_chipv7_phy never entered)");
		return;
	}
	syslog(LOG_NOTICE, "wifi_phyinit: t=%d agc_spot=%08x phy_agc_sum=%08x",
		   (int_t)wifi_phyinit_snap.t_us_low,
		   (unsigned int)wifi_phyinit_snap.agc_spot,
		   (unsigned int)wifi_phyinit_snap.phy_agc_sum);
	syslog(LOG_NOTICE, "wifi_phyinit: lpcon_clk_conf=%08x syscon_clk_conf=%08x",
		   (unsigned int)wifi_phyinit_snap.modem_lpcon_clk_conf,
		   (unsigned int)wifi_phyinit_snap.modem_syscon_clk_conf);
	syslog(LOG_NOTICE, "wifi_phyinit: syscon_rst_conf=%08x syscon_wifi_bb_cfg=%08x",
		   (unsigned int)wifi_phyinit_snap.modem_syscon_rst_conf,
		   (unsigned int)wifi_phyinit_snap.modem_syscon_wifi_bb_cfg);
	syslog(LOG_NOTICE, "wifi_phyinit: pmu_icg_modem=%08x pmu_hp_regulator0=%08x",
		   (unsigned int)wifi_phyinit_snap.pmu_icg_modem,
		   (unsigned int)wifi_phyinit_snap.pmu_hp_regulator0);
	syslog(LOG_NOTICE, "wifi_phyinit: bbpll_or_lock=%d or_cal_end=%d or_cal_ovf=%d",
		   (int_t)wifi_phyinit_snap.bbpll_or_lock,
		   (int_t)wifi_phyinit_snap.bbpll_or_cal_end,
		   (int_t)wifi_phyinit_snap.bbpll_or_cal_ovf);
}

/*
 *  DIAGNOSTIC（追記19・一時）：IDF v6.1 blob差し替え実験用のABIシム．
 *  新blob世代とhal側コンパイル済みソースのシンボル差を埋める．
 *  - esp_wifi_skip_supp_pmkcaching：旧libnet80211が提供・新は無し．
 *    wpa_supplicant(wpa.c)が参照．false（PMKキャッシュ使用）を返す．
 *  - printf：新libcoexistが直接参照．lib_printf相当へ（簡易に破棄）．
 */
/*  weak：旧blob(libnet80211)は本物を提供＝多重定義を回避．新blob使用時
 *  のみこのweak実装が使われる．falseはデフォルト挙動（PMKキャッシュ使用）． */
int __attribute__((weak)) esp_wifi_skip_supp_pmkcaching(void)
{
	return(0);	/* false: PMKキャッシュを使用 */
}

int __attribute__((weak)) printf(const char *fmt, ...)
{
	(void)fmt;
	return(0);
}
