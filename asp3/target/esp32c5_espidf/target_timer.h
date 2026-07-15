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
 *  タイマドライバのターゲット依存部（ESP32-C5 esp-hal LL層版）
 *
 *  esp32c6版（asp3_core/target/esp32c6_espidf/target_timer.h相当）から
 *  のコピー・C5対応。C6ブリングアップ専用の一時計装（DIAGNOSTIC計測
 *  カウンタ・g_hrt_last_target等）はdocs/c5-port-design.md §8.3の方針
 *  （「C6資産の非移植方針」）に従い持ち込まず，クリーンな実装とした。
 *    - HRTCNT（μs）はカウント値の1/16（ESP32C5_SYSTIMER_TICKS_PER_US．
 *      esp32c5.h参照．【未確認・暫定値】XTAL実測後の較正が必要）。
 *    - 割込みの強制（過去時刻設定時のペンディング・raise_event）は，
 *      同じCPU割込み線に多重マップしたFROM_CPU_0ソース（INTPRI
 *      ペリフェラル．CLIC移行の影響を受けない）で行う。
 *  SYSTIMERデバイス構造体インスタンスは esp-hal の
 *  esp32c5.peripherals.ld（リンカスクリプトからINCLUDE）が提供する。
 */

#ifndef TOPPERS_TARGET_TIMER_H
#define TOPPERS_TARGET_TIMER_H

#include <sil.h>
#include "esp32c5.h"

/*
 *  タイマ割込みハンドラ登録のための定数
 *  （C6と同じ割込み線番号を使う．線1-15はWi-Fi shim退避のため線16へ）
 */
#define INTNO_TIMER  16                           /* 割込み番号（Wi-Fi shimの線1-15予約により線16へ退避） */
#define INHNO_TIMER  16                           /* 割込みハンドラ番号（INTNO_TIMERと一致させる） */
#define INTPRI_TIMER (TMAX_INTPRI - 1)             /* 割込み優先度 */
#define INTATR_TIMER TA_NULL                       /* 割込み属性 */

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
esp32c5_systimer_read(void)
{
	uint32_t hi, lo;

	systimer_ll_counter_snapshot(&SYSTIMER, 0U);
	while (!systimer_ll_is_counter_value_valid(&SYSTIMER, 0U)) ;
	hi = systimer_ll_get_counter_value_high(&SYSTIMER, 0U);
	lo = systimer_ll_get_counter_value_low(&SYSTIMER, 0U);
	return(((uint64_t)hi << 32) | (uint64_t)lo);
}

/*
 *  現在時刻の読出し（HRTCNT＝μs）
 */
Inline HRTCNT
target_hrt_get_current(void)
{
	return((HRTCNT)(esp32c5_systimer_read()
					/ ESP32C5_SYSTIMER_TICKS_PER_US));
}

/*
 *  タイマ割込みの強制（FROM_CPU_0ソースのアサート．levelソースの
 *  ためクリアはtarget_hrt_handler／target_hrt_terminateで行う）
 */
Inline void
target_timer_force_int(void)
{
	sil_wrw_mem((void *)ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0, 1U);
}

/*
 *  次に割込みを発生させる時刻の設定
 */
Inline void
target_hrt_set_event(HRTCNT hrtcnt)
{
	uint64_t	current = esp32c5_systimer_read();
	uint64_t	target = current
					+ (uint64_t)hrtcnt * ESP32C5_SYSTIMER_TICKS_PER_US;

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
	if (esp32c5_systimer_read() >= target) {
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
