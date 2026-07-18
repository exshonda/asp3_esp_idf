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
 *  Wi-Fi/BT os_adapter shim ― ESP32-C5 «チップ固有» 部（dedup Tier2．
 *  docs/dedup-tier2-plan.md）．
 *
 *  3チップ 0-diff の共有コア（基盤プリミティブ）は
 *  common_espidf/wifi/esp_shim_core.c へ集約した．本ファイルには C5 固有の
 *  実装だけを残す：クリティカルセクション・時刻/乱数・modem ICG・セマフォ
 *  take/give/get_count・タスク生成/遅延・タイマ arm/ディスパッチ・割込み
 *  入口・初期化・保留診断アクセサ．跨ぐ file-scope オブジェクトは
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
#include "target_timer.h"		/* esp32c5_systimer_read */
#ifdef TOPPERS_ESP32C5_WIFI
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP32C5_WIFI */
#ifdef TOPPERS_ESP32C5_BT
/*  ★C5 BT用：esp_shim_modem_icg_init（modem ICG覚醒）に必要な hal LL ヘッダ．
    BT単体ビルドでは esp_wifi_adapter.c（この関数のもう1つの static 定義を持つ）が
    コンパイルされないため，ここに非static版を用意する（BLE実施05／c5-port-design.md §8.3．
    v9削除の回帰修正でC3版esp_shim.c再生成に伴い C5固有の本関数を再追加）．
    ★build hygiene（bt_smoke_c5リンク不可の修正）：bt_shim.cのesp_shim_bt_clock_init()は
    NimBLEの有無に関わらずesp_shim_modem_icg_init()を呼ぶため，ガードは
    TOPPERS_ESP32C5_BT_NIMBLE（NimBLEホストON時のみ）ではなくTOPPERS_ESP32C5_BT
    （BTコントローラ自体ON＝bt_smoke_c5のD-1も含む）でなければならない．
    NIMBLE限定ガードのままだとbt_smoke_c5（BT ON・NIMBLE OFF）でesp_shim_bt_clock_init
    がesp_shim_modem_icg_init未定義参照でリンク不可になる．  */
#include "hal/pmu_ll.h"
#include "hal/modem_syscon_ll.h"
#include "hal/modem_lpcon_ll.h"
#endif /* TOPPERS_ESP32C5_BT */

/*
 *  サービスコールのエラーのログ出力（sample1 の SVC_PERROR 相当）．
 *  E_CTX／E_TMOUT は本シムの «想定内»（CPUロック時のフォールバック・受信
 *  タイムアウト）なので除外し，«想定外» のエラーだけを file:line 付きで
 *  記録する．ESP32C5_BT_APIERR_TRACE=ON のときのみ有効（既定OFF＝非回帰）．
 */
#ifdef TOPPERS_ESP32C5_BT_APIERR_TRACE
/*  ★C5 SVC_PERROR：想定外エラー（非E_OK かつ 非E_CTX/E_TMOUT/E_QOVR）を
    «グローバルにも記録»＝コンソール不安定な C5 で app が RTC STORE へミラー
    して esptool で回収できるようにする（g_svc_err_last=直近ercd／
    g_svc_err_count=累計／g_svc_err_line=直近行）．ercd はそのまま返す（挙動不変）．  */
volatile int32_t	g_svc_err_last;		/* 直近の想定外エラーコード（負） */
volatile uint32_t	g_svc_err_count;	/* 想定外エラー累計 */
volatile int32_t	g_svc_err_line;		/* 直近エラーの行番号 */

ER
esp_shim_svc_perror(const char *file, int_t line, const char *expr, ER ercd)
{
	if (ercd < 0 && ercd != E_CTX && ercd != E_TMOUT && ercd != E_QOVR) {
		/*  ★順序は元実装どおり «g_svc_err_* 更新→t_perror»（Tier2 review で
		    順序反転が指摘されたので復元）．コンソール不安定な C5 で app が
		    RTC STORE へミラー回収するため，回収用グローバルを先に確定する．  */
		g_svc_err_last = (int32_t) ercd;
		g_svc_err_line = (int32_t) line;
		g_svc_err_count++;
		t_perror(LOG_ERROR, file, line, expr, ercd);
	}
	return(ercd);
}
#define SVC_PERROR(expr)	esp_shim_svc_perror(__FILE__, __LINE__, #expr, (expr))
#else
#define SVC_PERROR(expr)	(expr)
#endif

/*
 *  ネスト対応クリティカルセクション（FreeRTOS portENTER/EXIT_CRITICAL用）
 *
 *  esp_shim_int_disable()の退避値を「muxに格納」する方式は，同一muxを
 *  入れ子で取得する（BTコントローラのGLOBAL_INT_DISABLE系＝osiの
 *  _global_intr_disable/restoreはRW/LLDスタックが深くネストする）と，
 *  内側の取得が外側の退避値（MIE=1）をMIE=0で上書きし，最外の解放でも
 *  MIEが復元されず割込み禁止（CPUロック）のまま残る．その文脈で
 *  block-foreverのtwai_semを呼ぶとブロック不可でE_CTX→take失敗→
 *  btdm_controller_taskがexit→RTC_SW_SYS_RESET（docs/bt-shim.md
 *  「リセット点の局所化」）．
 *  ESP-IDF FreeRTOSと同じく，割込み状態はコア単位（単一コアなので大域）に
 *  ネストカウンタで退避し，最外(0→1)で退避・最外(1→0)でのみ復元する．
 *  muxは単一コアでは無意味なため参照しない．
 */
static volatile uint32_t	esp_shim_crit_nest = 0U;
static volatile uint32_t	esp_shim_crit_saved = 0U;

void
esp_shim_enter_critical(void)
{
	uint32_t state;

	/* mstatus.MIEを読みつつクリア（この時点以降はISRが入らない＝以降の
	   カウンタ/退避値更新はアトミック） */
	Asm("csrrci %0, mstatus, 8" : "=r"(state));
	if (esp_shim_crit_nest == 0U) {
		esp_shim_crit_saved = state & 8U;
	}
	esp_shim_crit_nest++;
}

void
esp_shim_exit_critical(void)
{
	if (esp_shim_crit_nest > 0U) {
		esp_shim_crit_nest--;
		if ((esp_shim_crit_nest == 0U) && (esp_shim_crit_saved != 0U)) {
			Asm("csrsi mstatus, 8");
			/*
			 *  最外解除でMIEを復帰した＝サービスコール発行可能になった．
			 *  BTクリティカルセクション内でE_CTXのため保留リングへ退避
			 *  された送信（D-2c ACL経路等）をここでDTQへflushし，待機中の
			 *  受信タスク（NimBLEホスト等）を起床させる．保留が無ければ
			 *  高速パスで即return（非回帰）．
			 *  ★D-2d bond修正：セマフォ側の保留give（E_CTXで消えた
			 *  controller の give）も同時に精算する．  */
			esp_shim_queue_flush_pending();
			esp_shim_sem_flush_pending();
			esp_shim_wakeup_flush_pending();	/* #5：タイマ起床の保留精算 */
		}
	}
}

/*
 *  時刻（esp_shim_time_us）・乱数（esp_shim_random）は dedup Tier2c で共有コア
 *  （common_espidf/wifi/esp_shim_core.c）へ移動．チップ固有の番地は
 *  esp_shim_chip_regs.h の ESP_SHIM_WDEV_RND_REG／ESP_SHIM_SYSTIMER_* で吸収．
 */

#ifdef TOPPERS_ESP32C5_BT
/*
 *  C5 modem ICG（Internal Clock Gating）初期化＝MODEMデジタルドメインを
 *  覚醒させる（docs/c5-bringup.md C5実施13・esp_wifi_adapter.c の static 版と
 *  同一実装）．BT clock init（bt_shim.c の esp_shim_bt_clock_init）から呼ぶ．
 *  ★C5レジスタ：pmu=0x600B0000・modem_lpcon=0x600AF000・modem_syscon=
 *  0x600A9C00（C6の0x600A9800から+0x400移動．c5-port-design.md §9）．
 *  v9削除の回帰修正でC3版esp_shim.c再生成に伴い C5固有の本関数を再追加．
 *  ★build hygiene修正：ガードをTOPPERS_ESP32C5_BT_NIMBLEから
 *  TOPPERS_ESP32C5_BTへ変更（bt_shim.cはNimBLEの有無に関わらず本関数を
 *  呼ぶため．上記インクルードガードと同じ理由）．
 */
void
esp_shim_modem_icg_init(void)
{
	pmu_dev_t			*pmu = (pmu_dev_t *)0x600B0000U;
	modem_lpcon_dev_t	*lpcon = (modem_lpcon_dev_t *)0x600AF000U;
	modem_syscon_dev_t	*syscon = (modem_syscon_dev_t *)0x600A9C00U;
	uint32_t			code_bit = 1U << 2;	/* BIT(PMU_HP_ICG_MODEM_CODE_ACTIVE=2) */

	pmu_ll_hp_set_icg_modem(pmu, PMU_MODE_HP_ACTIVE, 2U);
	modem_syscon_ll_set_modem_apb_icg_bitmap(syscon, code_bit);
	modem_lpcon_ll_set_i2c_master_icg_bitmap(lpcon, code_bit);
	modem_lpcon_ll_set_lp_apb_icg_bitmap(lpcon, code_bit);
	pmu_ll_imm_update_dig_icg_modem_code(pmu, true);
	pmu_ll_imm_update_dig_icg_switch(pmu, true);
}
#endif /* TOPPERS_ESP32C5_BT */

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
		 *  ★D-2d bond修正：mstatus.MIE==0（BTクリティカルセクション/ISR）
		 *  文脈では sig_sem が E_CTX＝give が消える．保留カウントへ退避し，
		 *  MIE復帰(exit_critical)や機会的flush(esp_shim_sem_flush_pending)で
		 *  sig_sem を精算する（キュー pend_ring と同型）．これが無いと
		 *  controller が ISR/クリティカルから出す give（暗号後の2個目ACL
		 *  処理の起床等）が失われる（docs/bt-shim.md「D-2d bond診断」）．
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
 *  現在のセマフォ資源数（FreeRTOS uxSemaphoreGetCount相当．NimBLE NPL用）
 */
uint32_t
esp_shim_sem_get_count(void *sem)
{
	T_RSEM		rsem;

	if (ref_sem((ID)(intptr_t)sem, &rsem) != E_OK) {
		return(0U);
	}
	return((uint32_t)rsem.semcnt);
}

#ifdef TOPPERS_C5_PEND_DIAG
/*
 *  ★E1計装（診断専用・既定OFF＝非回帰）：保留リングの «滞留» を app から
 *  観測するための読み出し専用アクセサ．跨ぐ保留カウンタ（shim_que_pend_total／
 *  shim_que_pend_used）は共有コアが所有し esp_shim_core.h で extern 宣言している．
 *
 *  設計上の要点（evidence-c5-09 §4）：
 *    - ★push/flush の hot path には «1命令も» 足さない．本関数は「静的変数を
 *      2つ読むだけ」で，呼ぶのは app の storm_monitor_task（200ms周期）のみ．
 *    - *cur ＝ shim_que_pend_total の «現在値»．呼び側が 200ms 周期でサンプル
 *      して最大値を取ることで «滞留（居座り）» を測る．
 *    - *used ＝ shim_que_pend_used ＝ 保留経路の «累積利用回数»．これが «較正»：
 *      hw==0 が «滞留が無い» のか «経路が使われていない» のかを分ける．
 *    - ロック不要：どちらも volatile な単一ワードで，0/非0 と大小の判定に
 *      しか使わない．
 */
void
esp_shim_pend_stats(uint32_t *cur, uint32_t *used)
{
	if (cur != NULL) {
		*cur = shim_que_pend_total;
	}
	if (used != NULL) {
		*used = shim_que_pend_used;
	}
}
#endif /* TOPPERS_C5_PEND_DIAG */

/*
 *		タスク生成（チップ固有＝優先度・プールサイズ差のため per-chip）
 */
int32_t
esp_shim_task_create(void (*entry)(void *), const char *name,
					 uint32_t stack_size, void *param,
					 uint32_t freertos_prio, void **task_handle)
{
	static const ID tskid_tbl[ESP_SHIM_NUM_TSK] = {
		SHIM_TSK1, SHIM_TSK2, SHIM_TSK3, SHIM_TSK4,
		SHIM_TSK5, SHIM_TSK6
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

/*
 *		Wi-Fi割込みディスパッチ（チップ固有＝番地・ストーム診断が per-chip）
 *
 *  ハンドラ登録表（shim_isr_tbl）は共有コア set_isr が管理し esp_shim_core.h で
 *  extern 宣言している．
 */
volatile uint32_t esp_shim_int_count[ESP_SHIM_MAX_WIFI_INTNO + 1];

/*  ストーム診断フラグ（既定0＝不活性）．C3版と互換のため宣言のみ残置． */
volatile uint32_t esp_shim_isr_storm_probe;

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
	/*  ★Low#3：C5 の storm 診断（旧 C3 コピー）は C3 番地 0x6001108C/0x600C2110/
	    0x60008058 等を叩いていた＝C5 では無意味な MMIO のため除去．C5 で storm
	    調査が要る場合は C5 の INTMTX/BB 番地で作り直すこと（esp_shim_isr_storm_probe
	    フラグ宣言は互換のため残置）．  */
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
		 *  Bluetooth単体ビルド（ESP32C5_WIFI=OFF）はmbedtls/PSA Cryptoを
		 *  リンクしないため，この初期化自体が不要（WPA2固有の問題）．
		 */
#ifdef TOPPERS_ESP32C5_WIFI
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP32C5_WIFI */
	}
}
