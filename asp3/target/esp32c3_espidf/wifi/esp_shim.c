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
 *  Wi-Fi os_adapter shim ― ESP32-C3 «チップ固有» 部（dedup Tier2．
 *  docs/dedup-tier2-plan.md）．
 *
 *  3チップ 0-diff の共有コア（基盤プリミティブ）は
 *  common_espidf/wifi/esp_shim_core.c へ集約した．本ファイルには C3 固有の
 *  実装だけを残す：クリティカルセクション・時刻/乱数・セマフォ take/give/
 *  get_count・タスク生成/遅延・タイマ arm/ディスパッチ・割込み入口・初期化．
 *  跨ぐ file-scope オブジェクト（プール配列・タイマリスト・ISR表・保留会計）
 *  は esp_shim_core.h で extern 宣言され，実体は共有コアが持つ．
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
#include "target_timer.h"		/* esp32c3_systimer_read */
#ifdef TOPPERS_ESP32C3_WIFI
#include "psa/crypto.h"			/* psa_crypto_init（後述．Wi-Fi固有＝
								   WPA2ハンドシェイクのPTK/MIC導出に必要．
								   Bluetooth単体ビルドではmbedtlsを
								   リンクしないため未定義時は除外する） */
#endif /* TOPPERS_ESP32C3_WIFI */

/*
 *  サービスコールのエラーのログ出力（sample1 の SVC_PERROR 相当）．
 *  E_CTX／E_TMOUT は本シムの «想定内»（CPUロック時のフォールバック・受信
 *  タイムアウト）なので除外し，«想定外» のエラーだけを file:line 付きで
 *  記録する．ESP32C3_BT_APIERR_TRACE=ON のときのみ有効（既定OFF＝非回帰）．
 */
#ifdef TOPPERS_ESP32C3_BT_APIERR_TRACE
/*  ★ercd を «そのまま返す»＝ er = SVC_PERROR(sig_sem(...)) で er にエラー
    コードが入り，かつ想定外エラーだけログする（挙動は不変・ログ追加のみ）．  */
ER
esp_shim_svc_perror(const char *file, int_t line, const char *expr, ER ercd)
{
	if (ercd < 0 && ercd != E_CTX && ercd != E_TMOUT && ercd != E_QOVR) {
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
		 *  ★#4：非タスク文脈（真のISR/CPUロック中）からの take．
		 *  ASP3 には «ISRから使えるセマフォ非ブロック取得» が存在しない
		 *  ——twai_sem も pol_sem も CHECK_TSKCTX_UNL（= sense_context()/
		 *  sense_lock() で弾かれるタスク文脈専用．kernel/semaphore.c:196,270）．
		 *  したがってISRからの取得は «失敗» を返すしかない（coex/BT の
		 *  _semphr_take_from_isr はこの経路に落ちる）．give 側と異なり «取得» は
		 *  延期不能なので保留救済もできない（保留できるのは «give» のみ）．
		 *  ★安直に pol_sem へ置換しても直らない（pol_sem も同じ CHECK）．
		 *  真の解決は blob の take-from-ISR 経路を絶つか，ISR安全な影カウンタを
		 *  持つ再設計だが，blob が実際にISRから take するかは未確認．
		 *  ここでは発生を計数し «本経路が実機で踏まれるか» を観測可能にする
		 *  （0 のままなら死経路＝無害＝再設計不要．非0 なら要再設計）．
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
 *  extern 宣言している．本ファイルは cfg でDEF_INHした共通入口とその
 *  ディスパッチ（C3固有の番地を叩く診断計装を含む）を持つ．
 */
volatile uint32_t esp_shim_int_count[ESP_SHIM_MAX_WIFI_INTNO + 1];

/*
 *  （D-2b(1) ISRストーム診断）実行時フラグ．既定0=無効なので通常ビルド
 *  （WiFi/bt_smoke_hw）は完全に非回帰．appが1を書くとBLEコントローラ
 *  ISR（CPU線1）発火ごとにRTC STORE4-7へストーム率とlevel割込みの
 *  clear残存を記録し，esptool read-mem（JTAG不要）で事後読みする．
 *    STORE4 0x600080B8 = esp_shim_int_count[1]（ストーム累積回数）
 *    STORE5 0x600080BC = INTMTX CPU_INT_EIP_STATUS(0x600C2110) 入口
 *    STORE6 0x600080C0 = INTMTX CPU_INT_TYPE(0x600C2108, bit1=線1のedge/level)
 *    STORE7 0x600080C4 = INTMTX CPU_INT_EIP_STATUS 出口（blob ISR実行後）
 *  出口でbit1が残る＝ソース未clear＝再発火＝ストーム（(i)クロックゲート
 *  or (ii)シムISR-clear欠落の判別材料）．詳細＝docs/bt-shim.md Phase D-2b．
 */
volatile uint32_t esp_shim_isr_storm_probe;

static void
shim_int_dispatch(int intno)
{
	esp_shim_int_count[intno]++;
	if (esp_shim_isr_storm_probe != 0U && intno == 2) {
		/*  （D-2b再開ラウンド）source多重登録修正（bt_shim.c）で2個目の
		    BTソース（予測=source5:BT_BB）を線2へ分離した．線2の発火数を
		    STORE2(0x60008058)へミラー＝esptool read-memで事後読み．
		    線2が実際にディスパッチされる＝INTMTXルーティング着弾の
		    機能的証拠（INTMTXレジスタ自体はusb-resetで初期化されるため
		    事後読み不可）．  */
		sil_wrw_mem((uint32_t *) 0x60008058UL, esp_shim_int_count[2]);
	}
	if (esp_shim_isr_storm_probe != 0U && intno == 1) {
		/*  （D-2b(1)(a) BBステータス計装）BT_BB(source5)割込みの原因を判定
		    する blob ISR（r_bt_bb_isr_hack）が読むBB割込みステータス
		    レジスタ 0x6001108c を，ISR実行**前**（再アサートされた生の原因）に
		    読んでRTCへ記録．どのビットが立ち続けるか＝ストームの原因．
		      STORE4(0xB8)=ストーム累積回数
		      STORE6(0xC0)=BB status 0x6001108c（ISR入口の生値）
		      STORE7(0xC4)=これまで観測した全statusのsticky OR（取りこぼし防止）
		    ※0xBC(STORE5)はROMがusb-reset時に上書きするため使用不可．  */
		/*  ストームは99.997%がBB status(0x6001108c)=0のspurious＝真の源は
		    blobの0x6001108cハンドラ外の低レベルBB線．blob ISR実行**後**に
		    INTMTX EIP_STATUS(0x600C2110)のbit1(線1)が残るか＝BT_BB線が
		    ack不能で立ちっぱなしか(=ハード/クロック要因)を判定する．
		      0xB8=総ディスパッチ数
		      0xC0=blob ISR実行後EIP_STATUSのsticky OR（bit1残存＝ack不能）
		      0xC4=入口BB status 0x6001108c のsticky OR（原因bit）  */
		uint32_t bbst = sil_rew_mem((const uint32_t *) 0x6001108CUL);
		sil_wrw_mem((uint32_t *) 0x600080B8UL, esp_shim_int_count[1]);
		sil_wrw_mem((uint32_t *) 0x600080C4UL,
					sil_rew_mem((const uint32_t *) 0x600080C4UL) | bbst);
		if (shim_isr_tbl[intno].fn != NULL) {
			shim_isr_tbl[intno].fn(shim_isr_tbl[intno].arg);
		}
		sil_wrw_mem((uint32_t *) 0x600080C0UL,
					sil_rew_mem((const uint32_t *) 0x600080C0UL)
					| sil_rew_mem((const uint32_t *) 0x600C2110UL));
		return;
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
#ifdef TOPPERS_ESP32C3_WIFI
		{
			psa_status_t st = psa_crypto_init();
			if (st != PSA_SUCCESS) {
				syslog(LOG_ERROR,
					   "esp_shim: psa_crypto_init failed (%d)",
					   (int_t)st);
			}
		}
#endif /* TOPPERS_ESP32C3_WIFI */
	}
}
