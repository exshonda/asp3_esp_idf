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
 *  Wi-Fi/BT os_adapter shim ― ESP32-C6 «チップ固有» 部（dedup Tier2．
 *  docs/dedup-tier2-plan.md）．
 *
 *  3チップ 0-diff の共有コア（基盤プリミティブ）は
 *  common_espidf/wifi/esp_shim_core.c へ集約した．本ファイルには C6 固有の
 *  実装だけを残す：クリティカルセクション・時刻/乱数・modem ICG・セマフォ
 *  take/give/get_count・タスク生成/遅延・タイマ arm/ディスパッチ・割込み
 *  入口・初期化（各種 DIAGNOSTIC 計装を含む）．跨ぐ file-scope オブジェクトは
 *  esp_shim_core.h で extern 宣言され，実体は共有コアが持つ．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <sil.h>
#include <stdarg.h>
#include <stdio.h>
#include "kernel_cfg.h"
#include "esp_shim.h"
#include "esp_shim_cfg.h"
#include "esp_shim_core.h"
#include "target_timer.h"		/* esp32c6_systimer_read */
#include "wifi_trace.h"			/* DIAGNOSTIC（実施26）：_task_delay計測用 */
#if defined(TOPPERS_ESP32C3_WIFI) || defined(TOPPERS_ESP32C6_WIFI)
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP32C3_WIFI || TOPPERS_ESP32C6_WIFI */
#include "hal/regi2c_ctrl_ll.h"
#include "hal/modem_lpcon_ll.h"
#include "hal/modem_syscon_ll.h"
#include "hal/pmu_ll.h"

/*
 *  サービスコールのエラーのログ出力（sample1 の SVC_PERROR 相当．C3の
 *  f9dae7d／1b8e028のC6移植）．E_CTX／E_TMOUT／E_QOVR は本シムの
 *  «想定内»（CPUロック時のフォールバック・受信タイムアウト・上限到達）
 *  なので除外し，«想定外» のエラーだけを file:line 付きで記録する．
 *  ESP32C6_BT_APIERR_TRACE 相当の診断オプションは未導入のため既定で
 *  常時パススルー（挙動不変）．
 */
#define SVC_PERROR(expr)	(expr)

/*
 *  ------------------------------------------------------------------
 *  esp_shim_enter_critical／esp_shim_exit_critical（BLE実施01．
 *  bt/stub/include/freertos/FreeRTOS.hのportENTER/EXIT_CRITICALが
 *  委譲する．esp_shim_int_disable/restoreと異なりネストカウンタを
 *  持つ——C3のBT/BLE統合（docs/bt-shim.md）で確立した教訓を最初から
 *  適用する：npl_os_freertos.c（NimBLE NPL）はportENTER_CRITICALの
 *  再入（同一タスク内で既にクリティカルセクション中にさらに入る）を
 *  行う箇所があり，単純なsave/restoreだと外側の区間で先に割込みが
 *  再度有効化されてしまう．ASP3は単一コアのためmuxは無視．
 *  ------------------------------------------------------------------
 */
static volatile uint32_t	shim_crit_nest;
static volatile uint32_t	shim_crit_saved;

void
esp_shim_enter_critical(void)
{
	uint32_t	state;

	Asm("csrrci %0, mstatus, 8" : "=r"(state));
	if (shim_crit_nest == 0U) {
		shim_crit_saved = state & 8U;
	}
	shim_crit_nest++;
}

void
esp_shim_exit_critical(void)
{
	if (shim_crit_nest > 0U) {
		shim_crit_nest--;
		if (shim_crit_nest == 0U && shim_crit_saved != 0U) {
			Asm("csrsi mstatus, 8");
			/*
			 *  最外解除でMIEを復帰した＝サービスコール発行可能になった．
			 *  ★C6 E_CTX修正（C3 f9dae7dのC6移植．docs/ble-c5c6-plan.md
			 *  「D-2c/D-2d本体のC6移植」参照）：BTクリティカルセクション内
			 *  でE_CTXのため保留リングへ退避された送信（ACL経路等）を
			 *  ここでDTQへflushし，待機中の受信タスク（NimBLEホスト等）を
			 *  起床させる．保留が無ければ高速パスで即return（非回帰）．
			 *  セマフォ側の保留give（E_CTXで消えたcontrollerのgive）も
			 *  同時に精算する（C3 1b8e028のC6移植）．
			 */
			esp_shim_queue_flush_pending();
			esp_shim_sem_flush_pending();
			esp_shim_wakeup_flush_pending();	/* #5：タイマ起床の保留精算 */
		}
	}
}

/*
 *  時刻・乱数
 */
int64_t
esp_shim_time_us(void)
{
	return((int64_t)(esp32c6_systimer_read()
					 / ESP32C6_SYSTIMER_TICKS_PER_US));
}

uint32_t
esp_shim_random(void)
{
	/*
	 *  HW乱数生成器（RNG_DATA_REG）．無線が有効になるとRFノイズ由来の
	 *  真性乱数になる（無効時はエントロピー低）．
	 *
	 *  C6の真のHW RNG読出しレジスタはWDEV_RND_REG＝LPPERI_RNG_DATA_REG
	 *  （DR_REG_LPPERI_BASE(0x600B2800)+0x8＝0x600B2808．esp-hal-3rdparty:
	 *  soc/esp32c6/register/soc/lpperi_reg.hおよびsoc/wdev_reg.hの
	 *  WDEV_RND_REG定義で確認．C3のSYSCON_RND_DATA_REGとは全く別の
	 *  ペリフェラル（C6はLP_PERIブロックへ移動）．C3のB-2bで
	 *  「WDEV_RND_REGの実体を正しく引く」ことがWPA2 SNonce不具合の
	 *  修正だった教訓を踏まえ，C6でも最初からwdev_reg.h同値を採用する．
	 */
	return(sil_rew_mem((void *)0x600B2808U));	/* LPPERI_RNG_DATA_REG (WDEV_RND_REG) */
}

/*
 *		セマフォ take/give/get_count（チップ固有＝診断カウンタ差のため per-chip）
 *
 *  跨ぐ file-scope の保留会計（shim_sem_id／shim_sem_pend／shim_sem_pend_total）は
 *  共有コアが所有し esp_shim_core.h で extern 宣言している．診断用の累計
 *  カウンタ（*_ectx_total）は本ファイル（チップ側）で定義する．
 */
volatile uint32_t	shim_sem_ectx_total;	/* 累計E_CTX give数（診断・非static） */
volatile uint32_t	shim_sem_take_ectx_total;	/* 累計E_CTX take数（#4診断・非static） */

int32_t
esp_shim_sem_take(void *sem, uint32_t block_time_tick)
{
	ER	er = twai_sem((ID)(intptr_t)sem, esp_shim_tick_to_tmo(block_time_tick));

	if (er == E_CTX) {
		/*
		 *  ★#4：非タスク文脈（真のISR/CPUロック中）からの take．ASP3 には
		 *  «ISRから使えるセマフォ非ブロック取得» が無い（twai_sem/pol_sem とも
		 *  CHECK_TSKCTX_UNL＝タスク文脈専用）．ISRからは «失敗» を返すしかなく，
		 *  «取得» は give と違い延期不能で保留救済もできない．pol_sem 置換でも
		 *  直らない．発生を計数し実機で本経路が踏まれるかを観測可能にする
		 *  （0のまま＝死経路・無害／非0＝要再設計）．詳細はC3版コメント参照．
		 */
		shim_sem_take_ectx_total++;
		return(0);
	}
	return(er == E_OK ? 1 : 0);
}

int32_t
esp_shim_sem_give(void *sem)
{
	ID	semid = (ID)(intptr_t)sem;
	ER	er = SVC_PERROR(sig_sem(semid));

	if (er == E_OK || er == E_QOVR) {
		return(1);	/* 成立（E_QOVR=既に上限＝実質signaled） */
	}
	if (er == E_CTX) {
		/*
		 *  ★C6 E_CTX修正：mstatus.MIE==0（BTクリティカルセクション/ISR）
		 *  文脈ではsig_semがE_CTX＝giveが消える．保留カウントへ退避し，
		 *  MIE復帰(exit_critical)や機会的flush(esp_shim_sem_flush_pending)で
		 *  sig_semを精算する（キューpend_ringと同型）．これが無いと
		 *  controllerがISR/クリティカルから出すgive（接続後の2個目ACL
		 *  処理の起床等）が失われる（C3 docs/bt-shim.md「D-2d bond診断」）．
		 */
		uint_t	i;

		SHIM_LOCK();
		for (i = 0U; i < ESP_SHIM_NUM_SEM; i++) {
			if (shim_sem_id[i] == semid) {
				shim_sem_pend[i]++;
				shim_sem_pend_total++;
				shim_sem_ectx_total++;
				break;
			}
		}
		SHIM_UNLOCK();
		return(1);	/* 保留＝«成立» 扱い（呼出し元は戻り値を捨てる） */
	}
	return(0);
}

/*
 *  現在のセマフォ資源数（FreeRTOS uxSemaphoreGetCount相当．BLE実施01
 *  ＝NimBLE NPL用．bt/stub/include/freertos/semphr.hが要求）．
 */
uint32_t
esp_shim_sem_get_count(void *sem)
{
	T_RSEM	rsem;

	if (ref_sem((ID)(intptr_t)sem, &rsem) != E_OK) {
		return(0U);
	}
	return((uint32_t)rsem.semcnt);
}

/*
 *		タスク生成（チップ固有＝優先度・プールサイズ差のため per-chip）
 */
int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	static const ID tskid_tbl[ESP_SHIM_NUM_TSK] = {
		SHIM_TSK1, SHIM_TSK2, SHIM_TSK3, SHIM_TSK4, SHIM_TSK5, SHIM_TSK6
#ifdef ESP_SHIM_BT_NIMBLE
		, SHIM_TSK7, SHIM_TSK8
#endif
	};
	uint_t		i;
	SHIM_TSK	*t = NULL;

	if (stack_size > ESP_SHIM_TSK_STKSZ) {
		syslog(LOG_NOTICE, "esp_shim: task '%s' stack %u > pool %u",
			   name, (uint_t)stack_size, (uint_t)ESP_SHIM_TSK_STKSZ);
	}
	SHIM_LOCK();
	for (i = 0U; i < ESP_SHIM_NUM_TSK; i++) {
		if (!shim_tsk[i].used) {
			shim_tsk[i].used = true;
			shim_tsk[i].tskid = tskid_tbl[i];
			shim_tsk[i].fn = entry;
			shim_tsk[i].arg = param;
			t = &shim_tsk[i];
			break;
		}
	}
	SHIM_UNLOCK();

	if (t == NULL) {
		syslog(LOG_ERROR, "esp_shim: task pool exhausted ('%s')", name);
		return(0);
	}
	syslog(LOG_NOTICE, "esp_shim: task '%s' -> tskid %d (prio %u)",
		   name, (int_t)t->tskid, (uint_t)freertos_prio);
	(void) freertos_prio;	/* 優先度は一律（ESP_SHIM_WIFI_TASK_PRI） */
	if (act_tsk(t->tskid) != E_OK) {
		t->used = false;
		return(0);
	}
	if (task_handle != NULL) {
		*task_handle = (void *)t;
	}
	return(1);
}

void
esp_shim_task_delay(uint32_t tick)
{
	/*
	 *  DIAGNOSTIC（実施26／タイミング感度調査）：blobが要求した
	 *  tick引数と，実際にdly_tskで経過した実時間（us）を記録する．
	 *  NuttX側は`_task_ms_to_tick`＝`MSEC2TICK(ms)`（10ms/tick）・
	 *  `_task_delay`＝`TICK2USEC(tick)`のため，blobが
	 *  `_task_ms_to_tick`を経由せず直接`_task_delay(N)`を
	 *  「N＝ms」のつもりで呼ぶ箇所があると，NuttXは実時間で
	 *  約10倍待つことになる（ASP3は`tick＝1ms`で正確に変換）．
	 *  実際にどちらの呼び方をしているか，実測して確認する．
	 */
	uint32_t	t0 = (uint32_t)esp_shim_time_us();
	uint32_t	t1;

	(void) dly_tsk((RELTIM)(tick * 1000U));
	t1 = (uint32_t)esp_shim_time_us();
#if defined(TOPPERS_ESP32C6_WIFI)
	/*  wifi_trace.cはWiFi専用の一時計装ファイル（--wrap前提）のため
	 *  BT単体ビルドではリンクしない．BLE実施01で本ファイル
	 *  （esp_shim.c）がWiFi/BT共有になったため呼出しをガードする．  */
	wifi_taskdelay_capture(tick, t0, t1 - t0);
#else
	(void) t0; (void) t1;
#endif
}

/*
 *		ets_timer arm／タイマタスク本体（チップ固有＝診断計装差のため per-chip）
 *
 *  タイマリスト（shim_timer_list）と検索（esp_shim_timer_find）は共有コアが
 *  所有し esp_shim_core.h で extern 宣言している．
 *
 *  DIAGNOSTIC（実施50，一時的）：esp_shim_timer_arm_usの呼出し頻度・
 *  要求us値を軽量カウンタで観測する（syslogバースト・ロス回避のため
 *  実施20と同じ「カウントのみ・まとめてダンプ」方式）．
 */
volatile uint32_t	esp_shim_timer_arm_count;
volatile uint32_t	esp_shim_timer_arm_us_min = 0xffffffffU;
volatile uint32_t	esp_shim_timer_arm_us_last;

void
esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat)
{
	SHIM_TIMER	*t = esp_shim_timer_find(ptimer, true);

	esp_shim_timer_arm_count++;
	esp_shim_timer_arm_us_last = us;
	if (us < esp_shim_timer_arm_us_min) {
		esp_shim_timer_arm_us_min = us;
	}

	if (t != NULL) {
		SHIM_LOCK();
		t->deadline_us = esp_shim_time_us() + (int64_t)us;
		t->period_us = repeat ? us : 0U;
		SHIM_UNLOCK();
		esp_shim_signal_or_pend(SHIM_TIMER_SEM);	/* #5：起床（critical内E_CTXは保留精算） */
	}
}

/*
 *  タイマタスク本体（esp_shim.cfgのCRE_TSKで生成・起動）
 */
volatile uint32_t	esp_shim_timer_task_loops;
volatile uint32_t	esp_shim_timer_task_fn_calls;
volatile uint32_t	esp_shim_timer_task_wait_min = 0xffffffffU;
volatile uint32_t	esp_shim_timer_task_wait_last;

void
esp_shim_timer_task(EXINF exinf)
{
	for (;;) {
		SHIM_TIMER	*t;
		int64_t		now = esp_shim_time_us();
		int64_t		next = 0;
		void		(*fn)(void *) = NULL;
		void		*arg = NULL;

		esp_shim_timer_task_loops++;

		/*
		 *  期限到来タイマを1つ選ぶ（コールバックはロック外で実行）
		 */
		SHIM_LOCK();
		for (t = shim_timer_list; t != NULL; t = t->next) {
			if (t->deadline_us == 0) {
				continue;
			}
			if (t->deadline_us <= now) {
				fn = t->fn;
				arg = t->arg;
				if (t->period_us != 0U) {
					t->deadline_us = now + (int64_t)t->period_us;
				}
				else {
					t->deadline_us = 0;
				}
				break;
			}
			if (next == 0 || t->deadline_us < next) {
				next = t->deadline_us;
			}
		}
		SHIM_UNLOCK();

		if (fn != NULL) {
			esp_shim_timer_task_fn_calls++;
			fn(arg);
			continue;			/* 他の期限到来タイマを続けて処理 */
		}

		if (next == 0) {
			(void) twai_sem(SHIM_TIMER_SEM, TMO_FEVR);
		}
		else {
			int64_t wait = next - now;
			if (wait < 1000) {
				wait = 1000;
			}
			else if (wait > (int64_t) TMAX_RELTIM) {
				wait = (int64_t) TMAX_RELTIM;	/* ★Low#6：>TMAX_RELTIM の busy-spin 回避 */
			}
			esp_shim_timer_task_wait_last = (uint32_t)wait;
			if ((uint32_t)wait < esp_shim_timer_task_wait_min) {
				esp_shim_timer_task_wait_min = (uint32_t)wait;
			}
			(void) twai_sem(SHIM_TIMER_SEM, (TMO)wait);
		}
	}
}

/*
 *		Wi-Fi割込みディスパッチ（チップ固有＝番地・診断計装が per-chip）
 *
 *  ハンドラ登録表（shim_isr_tbl）は共有コア set_isr が管理し esp_shim_core.h で
 *  extern 宣言している．
 */
volatile uint32_t esp_shim_int_count[ESP_SHIM_MAX_WIFI_INTNO + 1];

/*
 *  ストーム診断フラグ（既定0＝不活性）．C3版（esp32c3_espidf/wifi/esp_shim.c）
 *  と同じく，通常ビルドでは割込みホットパスの計装を走らせない．JTAG/debugger
 *  から非0を書いた時だけ下記の実施59計装（MAC割込みイベント採取）を有効化する．
 */
volatile uint32_t esp_shim_isr_storm_probe;

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
	/*
	 *  DIAGNOSTIC（実施59）：MAC割込み線（intno==1）が上がった瞬間に，
	 *  blobのMAC ISRが読み出す前のMAC割込みイベント／ステータスレジスタ
	 *  （0x600a4c48＝hal_mac_interrupt_get_eventが読む先）を採取し，どの
	 *  ビットで割込みが上がっているのかを特定する．RTC固定番地（0x500000B0〜）
	 *  にOR蓄積・最新値・非零回数・総数を残す（Direct BootはRTC RAMをゼロ
	 *  クリアしないため，JTAGでゼロクリア後にフリーランさせて差分／蓄積を
	 *  読む）．あわせてRX制御（0x600a4080）・RX最終ディスクリプタ（0x600a408c）
	 *  も最新値を残し，RX DMAがディスクリプタを進めているかを見る．
	 *  ★esp_shim_isr_storm_probeガード（既定0）：BTビルドでは線1がBT
	 *  コントローラISRであり，クロックゲート状態のWiFi MACレジスタを毎割込み
	 *  読むのを避ける．C3版と同じく既定不活性．
	 */
	if (esp_shim_isr_storm_probe != 0U && intno == 1) {
		volatile uint32_t *rtc = (volatile uint32_t *)0x500000B0U;
		uint32_t ev = *(volatile uint32_t *)0x600A4C48U;	/* MAC int event */
		rtc[0] |= ev;						/* [B0] OR蓄積 */
		rtc[1]  = ev;						/* [B4] 最新値 */
		if (ev != 0U) {
			rtc[2]++;						/* [B8] 非零回数 */
		}
		rtc[3]++;						/* [BC] intno==1総数 */
		rtc[4]  = *(volatile uint32_t *)0x600A4080U;	/* [C0] RX制御 */
		rtc[5]  = *(volatile uint32_t *)0x600A408CU;	/* [C4] RX最終dscr */
		rtc[6] |= *(volatile uint32_t *)0x600A408CU;	/* [C8] RX最終dscr OR */
	}
	if (shim_isr_tbl[intno].fn != NULL) {
		shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
	}
}

/*
 *  cfg（esp_shim.cfg）でDEF_INHする入口（blobが使う線の分だけ用意）
 */
void esp_shim_inthdr_1(void) { shim_int_dispatch(1); }
void esp_shim_inthdr_2(void) { shim_int_dispatch(2); }
void esp_shim_inthdr_3(void) { shim_int_dispatch(3); }

/*
 *		modem ICG（Internal Clock Gating）初期化
 *
 *  ★実施91（docs/wifi-shim-c6.md）：PMU HP_ACTIVE ICG_MODEM_REG
 *  （0x600B000C，dig_icg_modem_code，bits[31:30]）がPOR既定code=0の
 *  ままだとregi2cマスタ/modem APBクロックが永久ゲートされ，cold boot
 *  時にwait_i2c_sdm_stableが無限ループする（=phy_initハング）．
 *  元々はWi-Fi専用esp_wifi_adapter.cのwifi_clock_enable_wrapper()内
 *  static関数だったが，BLE実施01でBT単体ビルド（ESP32C6_WIFI=OFF）
 *  でも同じ根治が要るとわかったため，WiFi/BT共有ファイルの本ファイル
 *  （ESP32C6_WIFI・ESP32C6_BTどちらでもリンクされる）へ移設した．
 *  呼出し元＝WiFi: esp_wifi_adapter.cのwifi_clock_enable_wrapper()，
 *  BT: bt/bt_shim.cのesp_shim_bt_clock_init()．
 */
void
esp_shim_modem_icg_init(void)
{
	pmu_dev_t			*pmu = (pmu_dev_t *)0x600B0000U;
	modem_lpcon_dev_t	*lpcon = (modem_lpcon_dev_t *)0x600AF000U;
	modem_syscon_dev_t	*syscon = (modem_syscon_dev_t *)0x600A9800U;
	uint32_t			code_bit = 1U << 2;	/* BIT(PMU_HP_ICG_MODEM_CODE_ACTIVE=2) */

	pmu_ll_hp_set_icg_modem(pmu, PMU_MODE_HP_ACTIVE, 2U);
	modem_syscon_ll_set_modem_apb_icg_bitmap(syscon, code_bit);
	modem_lpcon_ll_set_i2c_master_icg_bitmap(lpcon, code_bit);
	modem_lpcon_ll_set_lp_apb_icg_bitmap(lpcon, code_bit);
	pmu_ll_imm_update_dig_icg_modem_code(pmu, true);
	pmu_ll_imm_update_dig_icg_switch(pmu, true);
}

/*
 *		初期化（チップ固有＝PSA/PMU等の初期化差のため per-chip）
 */
void
esp_shim_initialize(void)
{
	static bool_t initialized = false;

	if (!initialized) {
		initialized = true;
		esp_shim_heap_initialize();
		(void) act_tsk(SHIM_TIMER_TSK);

		/*
		 *  PSA Crypto初期化．
		 *
		 *  esp_supplicant/crypto_mbedtls.cのhmac_vector()（PTK/MIC
		 *  導出のHMAC-SHA1等で使用）はPSA Crypto API（psa_import_key
		 *  /psa_mac_sign_setup等）を直接呼ぶ．本来はESP-IDF起動シーケ
		 *  ンス（esp_system_startup.cのSECONDARY初期化，優先度104＝
		 *  mbedtls/port/esp_psa_crypto_init.cのESP_SYSTEM_INIT_FN経由）
		 *  でpsa_crypto_init()が自動的に呼ばれるが，本ポートはDirect
		 *  Boot（ESP-IDF起動シーケンス非経由）のためこの初期化が走ら
		 *  ない．未初期化のままPSA API群を呼ぶと全て失敗し（PBKDF2は
		 *  レガシーmbedtls_md経路のため無関係で正常動作するが，PTK
		 *  導出のsha1_prf→hmac_sha1_vector→hmac_vectorはPSA経由のため
		 *  全滅），呼び出し元（sha1_prf等）は戻り値未チェックのため
		 *  ptk->kck/kek/tkに未初期化のスタック内容（ポインタ値等）が
		 *  そのまま書き込まれる．結果，STAが送るmsg2のMICが常に不正
		 *  となりAPがmsg1を再送し続ける（4-wayハンドシェイクタイム
		 *  アウト，reason=15）．実機JTAGでptk->kck/kek/tkの中身が
		 *  ポインタらしき値（sm->snonceやsrc_addr等のアドレス）である
		 *  ことを確認して特定．
		 *
		 *  WiFi初期化前（esp_wifi_init呼び出し前）に一度だけ呼ぶ．
		 *
		 *  Bluetooth単体ビルド（ESP32C3_WIFI=OFF）はmbedtls/PSA Cryptoを
		 *  リンクしないため，この初期化自体が不要（WPA2固有の問題）．
		 */
#if defined(TOPPERS_ESP32C3_WIFI) || defined(TOPPERS_ESP32C6_WIFI)
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP32C3_WIFI || TOPPERS_ESP32C6_WIFI */
	}
}
