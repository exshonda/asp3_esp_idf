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
 *  タイマドライバのターゲット依存部（ESP32-C3 esp-hal LL層版）
 *
 *  asp3_core の target/esp32c3_gcc/target_timer.h（レジスタ直叩き版）
 *  のSYSTIMERアクセスを，esp-hal-3rdparty の LL 層
 *  （hal/systimer_ll.h＝static inline・RTOS非依存）で置き換えたもの
 *  （Phase B-1：esp-hal統合の実証）：
 *    - HRTCNT（μs）はカウント値の1/16（16MHz固定）．
 *    - 割込みの強制（過去時刻設定時のペンディング・raise_event）は，
 *      同じCPU割込み線に多重マップしたFROM_CPU_0ソースで行う（ASP3
 *      固有の機構のためLL化せずレジスタ直書きのまま）．
 *  SYSTIMERデバイス構造体インスタンスは esp-hal の
 *  esp32c3.peripherals.ld（リンカスクリプトからINCLUDE）が提供する．
 */

#ifndef TOPPERS_TARGET_TIMER_H
#define TOPPERS_TARGET_TIMER_H

#include <sil.h>
#include "esp32c3.h"

/*
 *  タイマ割込みハンドラ登録のための定数
 *  （SYSTIMER_TARGET0とFROM_CPU_0の両ソースをCPU割込み線16に多重マップ（線1〜15はWi-Fi blob用に開放））
 */
#define INTNO_TIMER  16                           /* 割込み番号 */
#define INHNO_TIMER  16                           /* 割込みハンドラ番号 */
#define INTPRI_TIMER (TMAX_INTPRI - 1)            /* 割込み優先度 */
#define INTATR_TIMER TA_NULL                      /* 割込み属性 */

#ifndef TOPPERS_MACRO_ONLY

#include "hal/systimer_ll.h"

/*
 *  高分解能タイマの起動処理
 */
extern void target_hrt_initialize(intptr_t exinf);

/*
 *  高分解能タイマの停止処理
 */
extern void target_hrt_terminate(intptr_t exinf);

/*
 *  SYSTIMERの52bitカウンタの読出し（unit0．スナップショット方式）
 */
Inline uint64_t
esp32c3_systimer_read(void)
{
	uint32_t hi, lo;

	systimer_ll_counter_snapshot(&SYSTIMER, 0U);
	while (!systimer_ll_is_counter_value_valid(&SYSTIMER, 0U)) ;
	hi = systimer_ll_get_counter_value_high(&SYSTIMER, 0U);
	lo = systimer_ll_get_counter_value_low(&SYSTIMER, 0U);
	return(((uint64_t)hi << 32) | (uint64_t)lo);
}

/*
 *  高分解能タイマの現在のカウント値の読出し（μs単位・下位32bit）
 */
Inline HRTCNT
target_hrt_get_current(void)
{
	return((HRTCNT)(esp32c3_systimer_read()
					/ ESP32C3_SYSTIMER_TICKS_PER_US));
}

/*
 *  タイマ割込みの強制（FROM_CPU_0ソースのアサート．levelソースの
 *  ため，タイマ割込みハンドラでクリア（0書込み）するまで保持される）
 */
Inline void
target_timer_force_int(void)
{
	sil_wrw_mem((void *)ESP32C3_SYSTEM_CPU_INTR_FROM_CPU_0, 1U);
}

/*
 *  高分解能タイマへの割込みタイミングの設定
 *
 *  高分解能タイマを，hrtcntで指定した値カウントアップしたら割込みを発
 *  生させるように設定する．
 */
Inline void
target_hrt_set_event(HRTCNT hrtcnt)
{
	uint64_t	current = esp32c3_systimer_read();
	uint64_t	target = current
					+ (uint64_t)hrtcnt * ESP32C3_SYSTIMER_TICKS_PER_US;

	/*
	 *  target0コンパレータへ比較値を設定する
	 *
	 *  ★「no time event」氾濫の根治（C6実施04の統一移植）：oneshotアラームは
	 *  発火後もWORK_ENが残り，comparator targetが過去になるとlevel再ラッチ
	 *  してスプリアス再発火を繰り返す（handler→signal_time→time event無し→
	 *  syslog氾濫）．再arm毎にdisable→set→apply→enableの«クリーン再arm»で
	 *  古いlevel-latchをクリアしてから未来targetを武装する．
	 */
	systimer_ll_enable_alarm(&SYSTIMER, 0U, false);
	systimer_ll_set_alarm_target(&SYSTIMER, 0U, target);
	systimer_ll_apply_alarm_value(&SYSTIMER, 0U);
	systimer_ll_enable_alarm(&SYSTIMER, 0U, true);

	/*
	 *  設定完了時点で比較値を過ぎていたら割込みを強制する
	 *  （oneshotのコンパレータは過去時刻に対して発火しないため）
	 */
	if (esp32c3_systimer_read() >= target) {
		target_timer_force_int();
	}
}

/*
 *  高分解能タイマ割込みの要求
 */
Inline void
target_hrt_raise_event(void)
{
	target_timer_force_int();
}

/*
 *  割込みタイミングに指定する最大値
 */
#define HRTCNT_BOUND 4000000002U

/*
 *  高分解能タイマ割込みハンドラ
 */
extern void target_hrt_handler(void);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_TARGET_TIMER_H */
