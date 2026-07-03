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
 *  タイマドライバ（ESP32-C3 ASP3用）
 *  SYSTIMER（unit0＋target0コンパレータ）を使用して高分解能タイマを
 *  実現する．pico2_riscv（TIMER0 ALARM0）からの流用．
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
	 *  unit0のカウント開始とtarget0コンパレータの動作開始
	 *  （クロックゲート・リセットはリセット後デフォルトで解除済み）
	 */
	sil_orw((void *)ESP32C3_SYSTIMER_CONF,
			ESP32C3_SYSTIMER_CONF_UNIT0_WORK_EN
				| ESP32C3_SYSTIMER_CONF_TARGET0_WORK_EN);

	/*
	 *  target0をoneshotモード（unit0と比較）に設定
	 */
	sil_wrw_mem((void *)ESP32C3_SYSTIMER_TARGET0_CONF, 0U);

	/*
	 *  target0割込みのクリアと許可
	 */
	sil_wrw_mem((void *)ESP32C3_SYSTIMER_INT_CLR,
				ESP32C3_SYSTIMER_INT_TARGET0);
	sil_orw((void *)ESP32C3_SYSTIMER_INT_ENA,
			ESP32C3_SYSTIMER_INT_TARGET0);

	/*
	 *  カウンタの歩進を確認する
	 */
	while (esp32c3_systimer_read() < 1U) ;
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
	sil_clrw((void *)ESP32C3_SYSTIMER_INT_ENA,
			 ESP32C3_SYSTIMER_INT_TARGET0);
	sil_wrw_mem((void *)ESP32C3_SYSTIMER_INT_CLR,
				ESP32C3_SYSTIMER_INT_TARGET0);
	sil_wrw_mem((void *)ESP32C3_SYSTEM_CPU_INTR_FROM_CPU_0, 0U);
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
	sil_wrw_mem((void *)ESP32C3_SYSTIMER_INT_CLR,
				ESP32C3_SYSTIMER_INT_TARGET0);
	sil_wrw_mem((void *)ESP32C3_SYSTEM_CPU_INTR_FROM_CPU_0, 0U);
	signal_time();
}
