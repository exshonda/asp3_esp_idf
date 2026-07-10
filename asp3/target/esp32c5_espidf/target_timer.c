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
 *  タイマドライバ（ESP32-C5 esp-hal LL層版）
 *  SYSTIMER（unit0＋target0コンパレータ）を使用して高分解能タイマを
 *  実現する．esp32c6版（asp3_core/target/esp32c6_espidf/target_timer.c
 *  相当）からのコピー・C5対応。C6ブリングアップ専用のRTC RAM診断計装
 *  （docs/c5-port-design.md §8.3「C6資産の非移植方針」）は持ち込まず，
 *  クリーンな実装とした。
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
	while (esp32c5_systimer_read() < 1U) ;
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
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0, 0U);
}

/*
 *  タイマ割込みハンドラ
 */
void
target_hrt_handler(void)
{
	/*
	 *  SYSTIMER側とFROM_CPU_0側（強制割込み）の両方をクリアする
	 *  （どちらも同じCPU割込み線にマップされたlevelソース）
	 */
	systimer_ll_clear_alarm_int(&SYSTIMER, 0U);
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0, 0U);
	signal_time();
}
