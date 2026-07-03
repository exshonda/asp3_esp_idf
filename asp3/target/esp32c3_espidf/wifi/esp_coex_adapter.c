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
 *  Coexistence os_adapter（coex_adapter_funcs_t）のASP3実装
 *
 *  NuttXのesp_coex_adapter.c（arch/risc-v/src/esp32c3/）を設計
 *  テンプレートに，coex OSアダプタをshim基盤（esp_shim.[ch]）で実装
 *  したもの．NuttXと同じく起動時に esp_coex_adapter_register() で
 *  登録する（wifi初期化前に呼ぶ＝esp_shim_wifi_pre_init）．
 *  これを登録しないと，libcoexist.a内のcoexist_funcsグローバルが
 *  未初期化のままとなり，WiFi PM（pm_disconnected_start）が
 *  coexist_funcsのNULLメソッドを呼んでクラッシュする（実機JTAGで特定）．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_shim.h"

#include "private/esp_coexist_adapter.h"

/*  esp_coex_adapter_register（libcoexist.a／esp_coex内） */
extern int esp_coex_adapter_register(coex_adapter_funcs_t *funcs);

/*
 *  coexist_funcs：ROM常駐グローバル（esp32c3.rom.ld・0x3fcdf83c）．
 *  ROMのcoexistラッパー群がこのポインタ経由でメソッドを呼ぶ．
 *  ESP-IDF/NuttXでは（BT併用時に）blob側が有効なテーブルを設定するが，
 *  WiFi単独のDirect Boot移植では設定されずNULLのままとなり，WiFi PM
 *  （pm_disconnected_start）がNULLメソッドを呼んでクラッシュする．
 *  coexistが不要なWiFi単独では，全メソッドをno-op（0を返す＝coex非
 *  アクティブ）にしたダミーテーブルを指させれば安全に通過できる．
 */
extern void *coexist_funcs;

static intptr_t
coex_noop(void)
{
	return(0);
}

static void *dummy_coexist_table[48];

static void
coex_task_yield_from_isr_wrapper(void)
{
	/* ASP3では割込み出口でディスパッチされるため何もしない */
}

static void *
coex_semphr_create_wrapper(uint32_t max, uint32_t init)
{
	return(esp_shim_sem_create(max, init));
}

static void
coex_semphr_delete_wrapper(void *semphr)
{
	esp_shim_sem_delete(semphr);
}

static int32_t
coex_semphr_take_from_isr_wrapper(void *semphr, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;
	}
	return(esp_shim_sem_take(semphr, 0U));		/* ポーリング */
}

static int32_t
coex_semphr_give_from_isr_wrapper(void *semphr, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;
	}
	return(esp_shim_sem_give(semphr));
}

static int32_t
coex_semphr_take_wrapper(void *semphr, uint32_t block_time_tick)
{
	return(esp_shim_sem_take(semphr, block_time_tick));
}

static int32_t
coex_semphr_give_wrapper(void *semphr)
{
	return(esp_shim_sem_give(semphr));
}

static int
coex_is_in_isr_wrapper(void)
{
	return((int) sns_ctx());
}

static void *
coex_malloc_internal_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void
coex_free_wrapper(void *p)
{
	esp_shim_free(p);
}

static int64_t
coex_esp_timer_get_time_wrapper(void)
{
	return(esp_shim_time_us());
}

static bool
coex_env_is_chip_wrapper(void)
{
	return(true);
}

static void
coex_timer_disarm_wrapper(void *timer)
{
	esp_shim_timer_disarm(timer);
}

static void
coex_timer_done_wrapper(void *ptimer)
{
	esp_shim_timer_done(ptimer);
}

static void
coex_timer_setfn_wrapper(void *ptimer, void *pfunction, void *parg)
{
	esp_shim_timer_setfn(ptimer, (void (*)(void *))pfunction, parg);
}

static void
coex_timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
	esp_shim_timer_arm_us(ptimer, us, repeat);
}

static int
coex_debug_matrix_init_wrapper(int event, int signal, bool rev)
{
	(void) event; (void) signal; (void) rev;
	return(0);
}

static int
coex_xtal_freq_get_wrapper(void)
{
	return(40);		/* ESP32-C3は40MHz固定 */
}

coex_adapter_funcs_t g_coex_adapter_funcs = {
	._version = COEX_ADAPTER_VERSION,
	._task_yield_from_isr = coex_task_yield_from_isr_wrapper,
	._semphr_create = coex_semphr_create_wrapper,
	._semphr_delete = coex_semphr_delete_wrapper,
	._semphr_take_from_isr = coex_semphr_take_from_isr_wrapper,
	._semphr_give_from_isr = coex_semphr_give_from_isr_wrapper,
	._semphr_take = coex_semphr_take_wrapper,
	._semphr_give = coex_semphr_give_wrapper,
	._is_in_isr = coex_is_in_isr_wrapper,
	._malloc_internal = coex_malloc_internal_wrapper,
	._free = coex_free_wrapper,
	._esp_timer_get_time = coex_esp_timer_get_time_wrapper,
	._env_is_chip = coex_env_is_chip_wrapper,
	._timer_disarm = coex_timer_disarm_wrapper,
	._timer_done = coex_timer_done_wrapper,
	._timer_setfn = coex_timer_setfn_wrapper,
	._timer_arm_us = coex_timer_arm_us_wrapper,
	._debug_matrix_init = coex_debug_matrix_init_wrapper,
	._xtal_freq_get = coex_xtal_freq_get_wrapper,
	._magic = COEX_ADAPTER_MAGIC,
};

/*
 *  coexアダプタの登録（WiFi初期化前に呼ぶ．NuttXのbringupに相当）
 */
void
esp_shim_coex_adapter_register(void)
{
	int		ret = esp_coex_adapter_register(&g_coex_adapter_funcs);
	uint_t	i;

	if (ret != 0) {
		syslog(LOG_ERROR, "esp_coex_adapter_register -> %d", (int_t)ret);
	}

	/*
	 *  ROMのcoexist_funcsポインタをダミーno-opテーブルへ向ける
	 *  （WiFi単独＝coexist非アクティブ．上記コメント参照）．
	 */
	for (i = 0U; i < 48U; i++) {
		dummy_coexist_table[i] = (void *)coex_noop;
	}
	coexist_funcs = dummy_coexist_table;
}
