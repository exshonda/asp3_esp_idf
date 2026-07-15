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
 *  タイマドライバのターゲット依存部（ESP32-C6 esp-hal LL層版）
 *
 *  asp3_core の target/esp32c6_gcc/target_timer.h（レジスタ直叩き版）
 *  のSYSTIMERアクセスを，esp-hal-3rdparty の LL 層
 *  （hal/systimer_ll.h＝static inline・RTOS非依存．C3と同一API）で
 *  置き換えたもの（Phase B-1：esp-hal統合）．
 *    - HRTCNT（μs）はカウント値の1/16（較正済み．asp3_core側
 *      esp32c6.h参照．16.024 ticks/us実測＝16と0.15%以内で一致）．
 *    - 割込みの強制（過去時刻設定時のペンディング・raise_event）は，
 *      同じCPU割込み線に多重マップしたFROM_CPU_0ソース（INTPRI
 *      ペリフェラル．C3のSYSTEMレジスタとは異なるブロック）で行う
 *      （ASP3固有の機構のためLL化せずレジスタ直書きのまま）．
 *  SYSTIMERデバイス構造体インスタンスは esp-hal の
 *  esp32c6.peripherals.ld（リンカスクリプトからINCLUDE）が提供する．
 */

#ifndef TOPPERS_TARGET_TIMER_H
#define TOPPERS_TARGET_TIMER_H

#include <sil.h>
#include "esp32c6.h"

/*
 *  タイマ割込みハンドラ登録のための定数
 *  （asp3_core側target_timer.hと同じ割込み線番号を使う．Phase Aでの
 *  実機検証と合わせるため）
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
esp32c6_systimer_read(void)
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
	return((HRTCNT)(esp32c6_systimer_read()
					/ ESP32C6_SYSTIMER_TICKS_PER_US));
}

/*
 *  タイマ割込みの強制（FROM_CPU_0ソースのアサート．levelソースの
 *  ためクリアはtarget_hrt_handler／target_hrt_terminateで行う）
 */
Inline void
target_timer_force_int(void)
{
	sil_wrw_mem((void *)ESP32C6_INTPRI_CPU_INTR_FROM_CPU_0, 1U);
}

/*
 *  DIAGNOSTIC（実施50，一時的）：target_hrt_set_eventの呼出し頻度と
 *  「設定完了時点で既に過去」を検出した回数・最小hrtcntを軽量カウンタ
 *  で観測する．JTAGでのライブhalt採取は関数内部の2回のsystimer読出し
 *  の間に割込んでしまうと見かけ上「常に過去」を作り出す計測アーチ
 *  ファクトになり得るため，計測はカウンタの蓄積のみで行い，JTAGは
 *  ダンプ後にまとめて読む．
 */
extern volatile uint32_t	esp32c6_hrt_set_event_count;
extern volatile uint32_t	esp32c6_hrt_set_event_forceint_count;
extern volatile uint32_t	esp32c6_hrt_set_event_hrtcnt_min;

/*
 *  次に割込みを発生させる時刻の設定
 */
/*  DIAGNOSTIC（HRTスプリアス割込み調査・一時的）：最後に武装したtarget
 *  をハンドラが参照して「alarm発火主張だが実はcounter<target＝古い
 *  targetのレベル再ラッチ」を検出する． */
extern volatile uint64_t g_hrt_last_target;

Inline void
target_hrt_set_event(HRTCNT hrtcnt)
{
	uint64_t	current = esp32c6_systimer_read();
	uint64_t	target = current
					+ (uint64_t)hrtcnt * ESP32C6_SYSTIMER_TICKS_PER_US;

	g_hrt_last_target = target;	/* DIAGNOSTIC */

	esp32c6_hrt_set_event_count++;
	if (hrtcnt < esp32c6_hrt_set_event_hrtcnt_min) {
		esp32c6_hrt_set_event_hrtcnt_min = hrtcnt;
	}

	/*
	 *  target0コンパレータへ比較値を設定する
	 *
	 *  ★「no time event」氾濫の根治（実施04）：oneshotアラームは発火後も
	 *  WORK_EN が残り，comparator target が過去になると level 再ラッチして
	 *  スプリアス再発火を繰り返す（handler→signal_time→time event 無し→
	 *  syslog 氾濫）．再arm 毎に disable→set→apply→enable の «クリーン再arm»
	 *  で古い level-latch をクリアしてから未来 target を武装する．
	 */
	systimer_ll_enable_alarm(&SYSTIMER, 0U, false);
	systimer_ll_set_alarm_target(&SYSTIMER, 0U, target);
	systimer_ll_apply_alarm_value(&SYSTIMER, 0U);
	systimer_ll_enable_alarm(&SYSTIMER, 0U, true);

	/*
	 *  設定完了時点で比較値を過ぎていたら割込みを強制する
	 *  （oneshotのコンパレータは過去時刻に対して発火しないため）
	 */
	if (esp32c6_systimer_read() >= target) {
		esp32c6_hrt_set_event_forceint_count++;
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
