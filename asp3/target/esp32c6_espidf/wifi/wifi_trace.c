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

#define WIFI_TRACE_SIZE  512

static wifi_trace_t	wifi_tr[WIFI_TRACE_SIZE];
static volatile uint32_t wifi_tr_pos;

/*
 *  DIAGNOSTIC（実施20）：syslogのバースト・ロス（キュー溢れで
 *  wifi_trace_dump()自体の行が欠落する事象を実機で確認．
 *  「44 messages are lost」）を避けるため，呼出し回数だけを
 *  カウントする軽量版．IDあたり1ワードのみ，ダンプ時に
 *  非ゼロのものだけ数行で出力する．
 */
#define WIFI_TRACE_MAXID 44
static volatile uint32_t wifi_tr_count[WIFI_TRACE_MAXID];
static const char *wifi_trace_name(uint16_t id);

void
wifi_trace_reset(void)
{
	wifi_tr_pos = 0U;
	memset(wifi_tr, 0, sizeof(wifi_tr));
	memset((void *)wifi_tr_count, 0, sizeof(wifi_tr_count));
}

void
wifi_trace_push(uint16_t id, uint16_t ctx,
				uintptr_t a0, uintptr_t a1, uintptr_t ret)
{
	uint32_t	pos = wifi_tr_pos++;
	wifi_trace_t *ent = &wifi_tr[pos % WIFI_TRACE_SIZE];

	if (id < WIFI_TRACE_MAXID) {
		wifi_tr_count[id]++;
	}
	ent->t_us_low = (uint32_t)esp_shim_time_us();
	ent->id = id;
	ent->ctx = ctx;
	ent->a0 = a0;
	ent->a1 = a1;
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
		syslog(LOG_NOTICE, "wifi_trace:   a1=%08x ret=%08x",
			   (unsigned int)wifi_tr[idx].a1,
			   (unsigned int)wifi_tr[idx].ret);
	}
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
		wifi_trace_push((id), 0U, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)ret); \
		return(ret); \
	}

WIFI_TRACE_WRAP4(wifi_hw_start, 1)
WIFI_TRACE_WRAP4(wifi_hmac_init, 2)
WIFI_TRACE_WRAP4(wifi_lmac_init, 3)
WIFI_TRACE_WRAP4(wDev_Rxbuf_Init, 4)
WIFI_TRACE_WRAP4(esf_buf_setup, 5)
WIFI_TRACE_WRAP4(esf_buf_setup_static, 6)
WIFI_TRACE_WRAP4(wdev_set_promis, 7)
WIFI_TRACE_WRAP4(sta_rx_cb, 8)
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
WIFI_TRACE_WRAP4(register_chipv7_phy, 21)
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
WIFI_TRACE_WRAP4(coex_init, 40)
WIFI_TRACE_WRAP4(coex_schm_process_restart, 41)

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

	wifi_trace_push(42U, 0U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr, 0xEEEEEEEEUL);
	{
		long ret = __real_coex_schm_lock(a0, a1, a2, a3);
		wifi_trace_push(42U, 1U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr, (uintptr_t)ret);
		return(ret);
	}
}

extern long __real_coex_schm_interval_get(long a0, long a1, long a2, long a3);
long
__wrap_coex_schm_interval_get(long a0, long a1, long a2, long a3)
{
	extern void *coex_schm_env_ptr;

	wifi_trace_push(43U, 0U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr, 0xEEEEEEEEUL);
	{
		long ret = __real_coex_schm_interval_get(a0, a1, a2, a3);
		wifi_trace_push(43U, 1U, (uintptr_t)a0, (uintptr_t)coex_schm_env_ptr, (uintptr_t)ret);
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
	wifi_trace_push(10U, 0U, (uintptr_t)a0, (uintptr_t)a1, (uintptr_t)ret);
	if (a1 == 2) {
		wifi_regsnap_capture();
	}
	return(ret);
}
