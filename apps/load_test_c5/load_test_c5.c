/*
 *  C5負荷試験デモ（ASP3＋esp_wifi blob＋os_adapter shim＋lwIP）
 *
 *  apps/load_test_c3（実施1，docs/load-test-c3c5c6.md）をC5向けに複製
 *  （実施46）．apps/tcp_socket_echo・apps/udp_socket_echoのWi-Fi接続＋
 *  DHCP待ちボイラープレートをそのまま流用．TCP echo（port 8）とUDP
 *  echo（port 9）を同一バイナリで同時に起動し，monitor_taskが
 *  esp_shim_heap_free_size()と累積送受信バイト数を周期syslog出力する．
 *
 *  docs/s3-throughput-findings-for-c6.md が報告する2つのOSAシム潜在
 *  欠陥（(A)キューmalloc／(B)TXバッファ動的malloc）をC5で検証する
 *  ための持続負荷試験に使う．欠陥Bは -DLOAD_TEST_STATIC_TXBUF で
 *  opt-in（未指定時は動的＝修正前の挙動）．
 *
 *  fault_capture_handler（apps/wifi_dhcp実施45由来）を安全網として
 *  同梱：本ラウンドの主目的はC3で発見された「負荷誘発リンク完全停止」
 *  の再現有無の確認であり，停止がCPU例外由来である可能性は事前に
 *  排除できないため，DEF_EXCで捕捉できる構えにしておく（フォルトが
 *  起きなければ無害．load_test_c5.cfg参照）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "kernel_cfg.h"		/* STALL_SNAPSHOT用：SHIM_*/NET_*のID参照 */
#include "load_test_c5.h"
#include "esp_shim.h"

#include "esp_wifi.h"
#include "esp_event.h"

#include "netif_esp32c3.h"

#include "lwip/sockets.h"

static ID	main_tskid;
static volatile int32_t	conn_state;	/* 0=待機 1=接続 -1=切断 */

/*
 *  監視用の累積カウンタ（tcp_echo_task/udp_echo_task/mon_taskからのみ
 *  アクセス．32bit単純カウンタでオーバーフローは監視ログの相対値で
 *  判断できるため許容）
 */
static volatile uint32_t	g_tcp_bytes_echoed;
static volatile uint32_t	g_udp_bytes_echoed;
static volatile uint32_t	g_tcp_sessions;
static volatile uint32_t	g_udp_datagrams;
static volatile uint32_t	g_tcp_send_errors;
static volatile uint32_t	g_udp_send_errors;

/*
 *  ==========================================================
 *  停止時スナップショット診断（実施47．docs/c5-bringup.md）
 *  ==========================================================
 *
 *  負荷誘発リンク完全停止（C3実施1／C5実施46で確認済み，2〜10秒で
 *  発生・双方向死・STA_DISCONNECTEDなし・heap一定・カーネル健全）の
 *  分類（(i) WiFiタスク系がsem/queue待ちで恒久ブロック，(ii) タスク
 *  全部生存だがTXが黙って落ちる，(iii) esp_wifi_internal_txがエラー
 *  連発）を判別するため，デバイス発1Hz raw pingの連続失敗（net/
 *  netif_esp32c3.cのnet_ping_result→本ファイルのnetstall_trace_
 *  ping_result．TOPPERS_ESP32C5_NETSTALL_TRACEガード）をトリガに
 *  全関連タスクのref_tsk（状態／待ち要因／待ちオブジェクトID）と
 *  シムqueue（DTQ）の水位，esp_wifi_internal_txの直近戻り値をUARTへ
 *  ダンプする．1ブートにつき1回だけ発火（ラッチ）。
 */
#ifdef TOPPERS_ESP32C5_NETSTALL_TRACE

extern volatile uint32_t	g_netstall_tx_calls;
extern volatile uint32_t	g_netstall_tx_errs;
extern volatile int32_t	g_netstall_last_tx_ret;

#define STALL_PING_FAIL_THRESHOLD	3	/* 連続3回（約3秒）で発火 */

static volatile uint32_t	g_ping_fail_streak;
static volatile bool_t		g_stall_snapshot_done;

typedef struct {
	ID			id;
	const char	*name;
} ID_NAME;

/*  タスク（WiFiシムタスクプール＋NET_TSK＋本アプリのタスク一式） */
static const ID_NAME	s_tsk_names[] = {
	{ SHIM_TSK1,      "SHIM_TSK1"      },
	{ SHIM_TSK2,      "SHIM_TSK2"      },
	{ SHIM_TSK3,      "SHIM_TSK3"      },
	{ SHIM_TSK4,      "SHIM_TSK4"      },
	{ SHIM_TSK5,      "SHIM_TSK5"      },
	{ SHIM_TSK6,      "SHIM_TSK6"      },
	{ SHIM_TIMER_TSK, "SHIM_TIMER_TSK" },
	{ NET_TSK,        "NET_TSK"        },
	{ MAIN_TASK,      "MAIN_TASK"      },
	{ TCP_TASK,       "TCP_TASK"       },
	{ UDP_TASK,       "UDP_TASK"       },
	{ MON_TASK,       "MON_TASK"       },
};

/*  セマフォ（WiFiシムsemプール＋net/ sys_arch semプール） */
static const ID_NAME	s_sem_names[] = {
	{ SHIM_SEM1,  "SHIM_SEM1"  }, { SHIM_SEM2,  "SHIM_SEM2"  },
	{ SHIM_SEM3,  "SHIM_SEM3"  }, { SHIM_SEM4,  "SHIM_SEM4"  },
	{ SHIM_SEM5,  "SHIM_SEM5"  }, { SHIM_SEM6,  "SHIM_SEM6"  },
	{ SHIM_SEM7,  "SHIM_SEM7"  }, { SHIM_SEM8,  "SHIM_SEM8"  },
	{ SHIM_SEM9,  "SHIM_SEM9"  }, { SHIM_SEM10, "SHIM_SEM10" },
	{ SHIM_SEM11, "SHIM_SEM11" }, { SHIM_SEM12, "SHIM_SEM12" },
	{ SHIM_SEM13, "SHIM_SEM13" }, { SHIM_SEM14, "SHIM_SEM14" },
	{ SHIM_SEM15, "SHIM_SEM15" }, { SHIM_SEM16, "SHIM_SEM16" },
	{ SHIM_SEM17, "SHIM_SEM17" }, { SHIM_SEM18, "SHIM_SEM18" },
	{ SHIM_SEM19, "SHIM_SEM19" }, { SHIM_SEM20, "SHIM_SEM20" },
	{ SHIM_SEM21, "SHIM_SEM21" }, { SHIM_SEM22, "SHIM_SEM22" },
	{ SHIM_SEM23, "SHIM_SEM23" }, { SHIM_SEM24, "SHIM_SEM24" },
	{ SHIM_TIMER_SEM, "SHIM_TIMER_SEM" },
	{ NET_SEM1, "NET_SEM1" }, { NET_SEM2, "NET_SEM2" },
	{ NET_SEM3, "NET_SEM3" }, { NET_SEM4, "NET_SEM4" },
	{ NET_SEM5, "NET_SEM5" }, { NET_SEM6, "NET_SEM6" },
	{ NET_SEM7, "NET_SEM7" }, { NET_SEM8, "NET_SEM8" },
};

/*  ミューテックス（WiFiシムmtxプール） */
static const ID_NAME	s_mtx_names[] = {
	{ SHIM_MTX1, "SHIM_MTX1" }, { SHIM_MTX2, "SHIM_MTX2" },
	{ SHIM_MTX3, "SHIM_MTX3" }, { SHIM_MTX4, "SHIM_MTX4" },
	{ SHIM_MTX5, "SHIM_MTX5" }, { SHIM_MTX6, "SHIM_MTX6" },
	{ SHIM_MTX7, "SHIM_MTX7" }, { SHIM_MTX8, "SHIM_MTX8" },
};

/*  データキュー（WiFiシムDTQプール＋lwIP sys_arch mboxプール） */
static const ID_NAME	s_dtq_names[] = {
	{ SHIM_DTQ1, "SHIM_DTQ1" }, { SHIM_DTQ2, "SHIM_DTQ2" },
	{ SHIM_DTQ3, "SHIM_DTQ3" }, { SHIM_DTQ4, "SHIM_DTQ4" },
	{ NET_MBOX1, "NET_MBOX1" }, { NET_MBOX2, "NET_MBOX2" },
	{ NET_MBOX3, "NET_MBOX3" }, { NET_MBOX4, "NET_MBOX4" },
	{ NET_MBOX5, "NET_MBOX5" }, { NET_MBOX6, "NET_MBOX6" },
	{ NET_MBOX7, "NET_MBOX7" }, { NET_MBOX8, "NET_MBOX8" },
	{ NET_MBOX9, "NET_MBOX9" }, { NET_MBOX10, "NET_MBOX10" },
};

static const char *
find_id_name(const ID_NAME *tbl, size_t n, ID id)
{
	size_t	i;

	for (i = 0; i < n; i++) {
		if (tbl[i].id == id) {
			return(tbl[i].name);
		}
	}
	return("?");
}

#define NUM_ELEM(a)		(sizeof(a) / sizeof((a)[0]))

static void
stall_dump_task(ID tskid, const char *name)
{
	T_RTSK		rt;
	ER			er;
	const char	*wtype = "-";
	const char	*wname = "-";

	er = ref_tsk(tskid, &rt);
	if (er != E_OK) {
		syslog(LOG_NOTICE, "STALL: tsk=%s ref_tsk err=%d",
			   name, (int_t) er);
		return;
	}
	if ((rt.tskstat & TTS_WAI) != 0) {
		if ((rt.tskwait & TTW_SEM) != 0) {
			wtype = "SEM";
			wname = find_id_name(s_sem_names, NUM_ELEM(s_sem_names),
								  rt.wobjid);
		} else if ((rt.tskwait & TTW_MTX) != 0) {
			wtype = "MTX";
			wname = find_id_name(s_mtx_names, NUM_ELEM(s_mtx_names),
								  rt.wobjid);
		} else if ((rt.tskwait & (TTW_SDTQ | TTW_RDTQ)) != 0) {
			wtype = ((rt.tskwait & TTW_SDTQ) != 0) ? "SDTQ" : "RDTQ";
			wname = find_id_name(s_dtq_names, NUM_ELEM(s_dtq_names),
								  rt.wobjid);
		} else if ((rt.tskwait & TTW_SLP) != 0) {
			wtype = "SLP";
		} else if ((rt.tskwait & TTW_DLY) != 0) {
			wtype = "DLY";
		}
	}
	syslog(LOG_NOTICE, "STALL: tsk=%s stat=%#x wait=%#x tmo=%d",
		   name, (uint_t) rt.tskstat, (uint_t) rt.tskwait,
		   (int_t) rt.lefttmo);
	syslog(LOG_NOTICE, "STALL: tsk=%s wobjtype=%s wobjname=%s",
		   name, wtype, wname);
}

static void
stall_dump_dtq(ID dtqid, const char *name)
{
	T_RDTQ	rd;
	ER		er;

	er = ref_dtq(dtqid, &rd);
	if (er != E_OK) {
		return;
	}
	syslog(LOG_NOTICE, "STALL: dtq=%s cnt=%d stsk=%d rtsk=%d",
		   name, (int_t) rd.sdtqcnt, (int_t) rd.stskid, (int_t) rd.rtskid);
}

static void
stall_snapshot_dump(void)
{
	uint_t	i;

	syslog(LOG_NOTICE,
		   "STALL: ==== snapshot fired (ping fail streak=%u) ====",
		   (uint_t) g_ping_fail_streak);
	syslog(LOG_NOTICE,
		   "STALL: tx_calls=%u tx_errs=%u last_tx_ret=%d",
		   (uint_t) g_netstall_tx_calls, (uint_t) g_netstall_tx_errs,
		   (int_t) g_netstall_last_tx_ret);
	syslog(LOG_NOTICE,
		   "STALL: heap_free=%u tcp_bytes=%u udp_bytes=%u",
		   (uint_t) esp_shim_heap_free_size(),
		   (uint_t) g_tcp_bytes_echoed, (uint_t) g_udp_bytes_echoed);

	for (i = 0; i < NUM_ELEM(s_tsk_names); i++) {
		stall_dump_task(s_tsk_names[i].id, s_tsk_names[i].name);
	}
	for (i = 0; i < NUM_ELEM(s_dtq_names); i++) {
		stall_dump_dtq(s_dtq_names[i].id, s_dtq_names[i].name);
	}
	syslog(LOG_NOTICE, "STALL: ==== snapshot done ====");
}

void
netstall_trace_ping_result(int ok)
{
	if (ok) {
		g_ping_fail_streak = 0;
		return;
	}
	g_ping_fail_streak++;
	if (g_ping_fail_streak >= STALL_PING_FAIL_THRESHOLD
		&& !g_stall_snapshot_done) {
		g_stall_snapshot_done = true;
		stall_snapshot_dump();
	}
}

#endif /* TOPPERS_ESP32C5_NETSTALL_TRACE */

/*
 *  CPU例外フォルト捕捉ハンドラ（apps/wifi_dhcp実施45と同一機構の
 *  安全網移植．docs/c5-bringup.md実施06/08/45参照）．RTC-RAM
 *  0x50000000〜（magic 0xFA017C05）へmepc/mcause/mtval＋汎用レジスタ
 *  ＋callee-saved(s0-s3)/spを保存し，無限ループで凍結する．
 */
#define FAULTCAP_BASE		((volatile uint32_t *)0x50000000U)
#define FAULTCAP_MAGIC		0xFA017C05U

void
fault_capture_handler(void *p_excinf)
{
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
	FAULTCAP_BASE[4]  = (uint32_t) p->pc;
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
		/*  凍結．JTAG（非侵襲attach）またはesptool read_memで回収する。 */
	}
}

static void
wifi_event_handler(void *arg, const char *base, int32_t id, void *data)
{
	(void) arg; (void) base;

	switch (id) {
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

	netif_esp32c3_start();
	esp_shim_initialize();
	(void) esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
									  (void *)wifi_event_handler, NULL);
	esp_shim_coex_adapter_register();

#ifdef LOAD_TEST_STATIC_TXBUF
	/*
	 *  欠陥B対策（S3 commit dd7a76d移植．C3実施1と同一判断基準）：
	 *  TXバッファを静的プール化．既定（tx_buf_type=1・
	 *  static_tx_buf_num=0）は毎パケット~1.7KBをシムヒープから
	 *  malloc/freeし，持続高レート送信で断片化を招く。
	 */
	cfg.tx_buf_type = 0;
	cfg.static_tx_buf_num = 16;
	syslog(LOG_NOTICE,
		   "load_test_c5: LOAD_TEST_STATIC_TXBUF=ON "
		   "(tx_buf_type=0, static_tx_buf_num=16)");
#else
	syslog(LOG_NOTICE,
		   "load_test_c5: LOAD_TEST_STATIC_TXBUF=OFF "
		   "(default dynamic tx_buf)");
#endif

	syslog(LOG_NOTICE, "load_test_c5: esp_wifi_init");
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
	syslog(LOG_NOTICE, "load_test_c5: SSID='%s'", WIFI_SSID);

	err = esp_wifi_start();
	if (err != 0) {
		syslog(LOG_ERROR, "esp_wifi_start -> %d", (int_t)err);
		return;
	}

	/*
	 *  STA_START後にmain_task文脈からconnectする（イベントハンドラ＝
	 *  WiFiタスク文脈からのconnect呼出しを避ける．Phase B-2bの知見）
	 */
	(void) tslp_tsk(2000000);
	err = esp_wifi_connect();
	syslog(LOG_NOTICE, "load_test_c5: esp_wifi_connect -> %d", (int_t)err);

	/*
	 *  接続完了（またはリトライ上限）を待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		if (conn_state == 1) {
			syslog(LOG_NOTICE, "load_test_c5: CONNECTED, waiting for DHCP");
			break;
		}
		if (conn_state == -1) {
			syslog(LOG_NOTICE, "load_test_c5: retry connect (%d)", retry);
			conn_state = 0;
			(void) esp_wifi_connect();
		}
	}

	if (conn_state != 1) {
		syslog(LOG_NOTICE, "load_test_c5: FAILED (timeout)");
		return;
	}

	/*
	 *  接続先APの実測情報（チャンネル・RSSI）を記録する（帯域の証跡用．
	 *  wifi_dhcp実施45と同一のログ形式）。
	 */
	{
		wifi_ap_record_t	ap;

		if (esp_wifi_sta_get_ap_info(&ap) == 0) {
			syslog(LOG_NOTICE,
				   "load_test_c5: AP info: channel=%d rssi=%d",
				   (int_t) ap.primary, (int_t) ap.rssi);
		}
	}

	/*
	 *  DHCPでIPアドレスが割り当たるまで待つ
	 */
	for (retry = 0; retry < 20; retry++) {
		(void) tslp_tsk(1000000);	/* 1秒 */
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			syslog(LOG_NOTICE, "load_test_c5: IP acquired: %d.%d.%d.%d",
				   (int_t)(ip & 0xff), (int_t)((ip >> 8) & 0xff),
				   (int_t)((ip >> 16) & 0xff), (int_t)((ip >> 24) & 0xff));
			break;
		}
	}

	if (ip == 0) {
		syslog(LOG_NOTICE, "load_test_c5: DHCP FAILED (timeout)");
	}
	syslog(LOG_NOTICE, "load_test_c5: main_task done "
		   "(echo servers run by tcp_echo_task/udp_echo_task)");
}

/*
 *  1接続分のTCP echo処理（recv()した内容をそのままsend()で送り返す）
 */
static void
tcp_echo_session(int conn_sock)
{
	char	buf[1460];	/* TCP_MSS前後を1回のrecvで拾えるサイズ */
	ssize_t	rlen;
	ssize_t	wlen;
	uint32_t	total = 0;

	for (;;) {
		rlen = recv(conn_sock, buf, sizeof(buf), 0);
		if (rlen <= 0) {
			break;
		}
		wlen = send(conn_sock, buf, (size_t) rlen, 0);
		if (wlen != rlen) {
			syslog(LOG_NOTICE, "load_test_c5: tcp send() error (wlen=%d)",
				   (int_t) wlen);
			g_tcp_send_errors++;
			break;
		}
		total += (uint32_t) rlen;
		g_tcp_bytes_echoed += (uint32_t) rlen;
	}
	syslog(LOG_NOTICE, "load_test_c5: tcp client disconnected "
		   "(%d bytes echoed)", (int_t) total);
	(void) close(conn_sock);
}

void
tcp_echo_task(EXINF exinf)
{
	int					listen_sock;
	int					conn_sock;
	struct sockaddr_in	addr;
	struct sockaddr_in	peer;
	socklen_t			peerlen;
	uint32_t			ip;
	uint32_t			pip;

	(void) exinf;

	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE,
		   "load_test_c5: starting TCP echo server (port %d)",
		   (int_t) TCP_ECHO_PORT);

	listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp socket() failed (%d)",
			   (int_t) listen_sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(TCP_ECHO_PORT);

	if (bind(listen_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp bind() failed");
		(void) close(listen_sock);
		return;
	}

	if (listen(listen_sock, 1) < 0) {
		syslog(LOG_ERROR, "load_test_c5: tcp listen() failed");
		(void) close(listen_sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c5: tcp listening on 0.0.0.0:%d",
		   (int_t) TCP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		conn_sock = accept(listen_sock, (struct sockaddr *) &peer, &peerlen);
		if (conn_sock < 0) {
			syslog(LOG_NOTICE, "load_test_c5: tcp accept() failed");
			continue;
		}

		pip = peer.sin_addr.s_addr;
		g_tcp_sessions++;
		syslog(LOG_NOTICE,
			   "load_test_c5: tcp client connected %d.%d.%d.%d:%d",
			   (int_t)(pip & 0xff), (int_t)((pip >> 8) & 0xff),
			   (int_t)((pip >> 16) & 0xff), (int_t)((pip >> 24) & 0xff),
			   (int_t) ntohs(peer.sin_port));

		tcp_echo_session(conn_sock);
	}
}

void
udp_echo_task(EXINF exinf)
{
	int					sock;
	struct sockaddr_in	addr;
	struct sockaddr_in	peer;
	socklen_t			peerlen;
	char				buf[1460];
	ssize_t				rlen;
	ssize_t				wlen;
	uint32_t			ip;
	uint32_t			pip;

	(void) exinf;

	for (;;) {
		ip = netif_esp32c3_get_ipaddr();
		if (ip != 0) {
			break;
		}
		(void) tslp_tsk(1000000);	/* 1秒 */
	}
	(void) ip;

	syslog(LOG_NOTICE,
		   "load_test_c5: starting UDP echo server (port %d)",
		   (int_t) UDP_ECHO_PORT);

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		syslog(LOG_ERROR, "load_test_c5: udp socket() failed (%d)",
			   (int_t) sock);
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(UDP_ECHO_PORT);

	if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		syslog(LOG_ERROR, "load_test_c5: udp bind() failed");
		(void) close(sock);
		return;
	}

	syslog(LOG_NOTICE, "load_test_c5: udp listening on 0.0.0.0:%d (UDP)",
		   (int_t) UDP_ECHO_PORT);

	for (;;) {
		peerlen = sizeof(peer);
		rlen = recvfrom(sock, buf, sizeof(buf), 0,
						 (struct sockaddr *) &peer, &peerlen);
		if (rlen < 0) {
			syslog(LOG_NOTICE, "load_test_c5: udp recvfrom() failed");
			continue;
		}
		g_udp_datagrams++;

		pip = peer.sin_addr.s_addr;
		(void) pip;

		wlen = sendto(sock, buf, (size_t) rlen, 0,
					  (struct sockaddr *) &peer, peerlen);
		if (wlen != rlen) {
			syslog(LOG_NOTICE, "load_test_c5: udp sendto() error (wlen=%d)",
				   (int_t) wlen);
			g_udp_send_errors++;
		} else {
			g_udp_bytes_echoed += (uint32_t) rlen;
		}
	}
}

/*
 *  監視タスク：ヒープ残量・累積カウンタを周期syslog出力．
 *  S3の欠陥A（シムヒープ断片化→malloc失敗）・欠陥B（TXバッファ枯渇）
 *  が起きていれば，heap_free単調減少／send_errors増加として現れる．
 */
void
mon_task(EXINF exinf)
{
	uint32_t	uptime_s = 0;

	(void) exinf;

	for (;;) {
		(void) tslp_tsk((TMO) MON_PERIOD_US);
		uptime_s += (MON_PERIOD_US / 1000000U);
		/*
		 *  syslog()の可変引数はTNUM_LOGPAR=6（先頭はformat文字列自体）
		 *  制約により%変換指定子は最大5個までしか安全に運べない
		 *  （asp3/asp3_core/include/t_syslog.h参照）．8個の値を1行に
		 *  詰めると超過分が文字列として化けるため2行に分割する．
		 */
		syslog(LOG_NOTICE,
			   "load_test_c5: MON uptime=%us heap_free=%u "
			   "tcp_bytes=%u tcp_sessions=%u tcp_errs=%u",
			   (uint_t) uptime_s, (uint_t) esp_shim_heap_free_size(),
			   (uint_t) g_tcp_bytes_echoed, (uint_t) g_tcp_sessions,
			   (uint_t) g_tcp_send_errors);
		syslog(LOG_NOTICE,
			   "load_test_c5: MON uptime=%us "
			   "udp_bytes=%u udp_dgrams=%u udp_errs=%u",
			   (uint_t) uptime_s,
			   (uint_t) g_udp_bytes_echoed, (uint_t) g_udp_datagrams,
			   (uint_t) g_udp_send_errors);
	}
}
