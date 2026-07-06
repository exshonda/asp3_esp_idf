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
 *  タイマドライバ（ESP32-C6 esp-hal LL層版）
 *  SYSTIMER（unit0＋target0コンパレータ）を使用して高分解能タイマを
 *  実現する．asp3_core の target/esp32c6_gcc/target_timer.c（レジスタ
 *  直叩き版）を hal/systimer_ll.h で置き換えたもの（Phase B-1）．
 */

#include "kernel_impl.h"
#include "time_event.h"
#include "target_timer.h"
#include <sil.h>

/*
 *  タイマの起動処理
 */
void
target_hrt_initialize(intptr_t exinf)
{
	/*
	 *  unit0のカウント開始とtarget0コンパレータの設定
	 *  （クロックゲート・リセットはリセット後デフォルトで解除済み）
	 */
	systimer_ll_enable_counter(&SYSTIMER, 0U, true);
	systimer_ll_connect_alarm_counter(&SYSTIMER, 0U, 0U);
	systimer_ll_enable_alarm_oneshot(&SYSTIMER, 0U);
	systimer_ll_enable_alarm(&SYSTIMER, 0U, true);

	/*
	 *  target0割込みのクリアと許可
	 */
	systimer_ll_clear_alarm_int(&SYSTIMER, 0U);
	systimer_ll_enable_alarm_int(&SYSTIMER, 0U, true);

	/*
	 *  カウンタの歩進を確認する
	 */
	while (esp32c6_systimer_read() < 1U) ;
}

/*
 *  タイマの停止処理
 */
void
target_hrt_terminate(intptr_t exinf)
{
	/*
	 *  target0割込みの禁止とクリア
	 */
	systimer_ll_enable_alarm_int(&SYSTIMER, 0U, false);
	systimer_ll_clear_alarm_int(&SYSTIMER, 0U);
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_0, 0U);
}

/*
 *  タイマ割込みハンドラ
 */
/*
 *  DIAGNOSTIC（Step0 HRTスプリアス割込み調査・一時的）：
 *  RTC RAM(0x50000000)に計測を蓄積．[12]handler総入場 [13]入場時に
 *  systimer alarm int_stがセット済み（＝alarm発火主張） [14]うち
 *  counter<last_target（＝古いtargetのレベル再ラッチ＝スプリアスの証拠）．
 *  last_targetはtarget_hrt_set_eventが g_hrt_last_target へ保存する．
 */
volatile uint64_t g_hrt_last_target;

void
target_hrt_handler(void)
{
	volatile uint32_t *rc = (volatile uint32_t *)0x50000000U;
	bool_t fired = systimer_ll_is_alarm_int_fired(&SYSTIMER, 0U);
	uint64_t now = esp32c6_systimer_read();

	rc[12] += 1U;
	if (fired) {
		rc[13] += 1U;
		if (now < g_hrt_last_target) {
			rc[14] += 1U;	/* alarm発火主張だが武装targetは未来＝スプリアス */
		}
	}

	/*
	 *  SYSTIMER側とFROM_CPU_0側（強制割込み）の両方をクリアする
	 *  （どちらも同じCPU割込み線にマップされたlevelソース）
	 */
	systimer_ll_clear_alarm_int(&SYSTIMER, 0U);
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_0, 0U);
	signal_time();
}
