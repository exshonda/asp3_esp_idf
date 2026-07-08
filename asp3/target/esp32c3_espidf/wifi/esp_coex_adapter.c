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

/*  esp_coex_adapter_register／coex_pre_init（libcoexist.a／esp_coex内） */
extern int esp_coex_adapter_register(coex_adapter_funcs_t *funcs);
extern int coex_pre_init(void);

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
/*
 *  DIAGNOSTIC（実施53，一時的）：coex_pre_init()自体が返ってくるかを
 *  BSSグローバル（毎回のDirect Bootで確実にゼロ初期化される）で
 *  確認する．entered=1で「呼んだ」，done=1で「戻ってきた」．
 */
volatile uint32_t	esp_shim_coex_pre_init_entered;
volatile uint32_t	esp_shim_coex_pre_init_done;
volatile int32_t	esp_shim_coex_pre_init_ret;
volatile uint32_t	esp_shim_coex_pre_regi2c_63 = 0xFFFFFFFFU;
volatile uint32_t	esp_shim_coex_post_regi2c_63 = 0xFFFFFFFFU;

void
esp_shim_coex_adapter_register(void)
{
	static bool	done = false;
	int		ret;
	int		pre_ret;
	uint_t	i;

	/*
	 *  実施54：hardware_init_hook（起動ごく初期）とアプリのmain_task
	 *  （wifi初期化直前）の両方から呼ばれても1回しか実行しない
	 *  （NuttXのボード起動タイミングへ近付けるためhardware_init_hook
	 *  からも呼ぶが，既存アプリ側の呼出しは互換のため残す）．
	 */
	if (done) {
		return;
	}
	done = true;

	ret = esp_coex_adapter_register(&g_coex_adapter_funcs);

	if (ret != 0) {
		syslog(LOG_ERROR, "esp_coex_adapter_register -> %d", (int_t)ret);
	}

	/*
	 *  実施52（docs/wifi-shim-c6.md）：coexist_funcsをダミーno-opテーブル
	 *  で上書きしていたのがC6 RX不能（PTI=0でMAC RX割込み不発）の根本
	 *  原因と判明．NuttXのボード起動（esp32c6_bringup.c）は
	 *  esp_coex_adapter_register()の直後に必ずcoex_pre_init()（libcoexist.a
	 *  内，esp_coexist_internal.h宣言）を呼んでおり，これがcoexist_funcs
	 *  を正しい実体（PTI設定を含む）で初期化する．ESP-IDF本体の
	 *  startup_funcs.cではこの2つを起動シーケンスの中で自動的に呼ぶが，
	 *  「#ifndef __NuttX__」でガードされ**NuttXでは呼ばれない**ため，
	 *  NuttXは同じ2行をボード側（bringup）で明示的に呼んでいる．ASP3の
	 *  Direct Bootも同様にESP-IDFの起動シーケンスを経由しないため，
	 *  NuttXと同じ位置（wifi初期化前のcoexアダプタ登録時）で明示的に
	 *  呼ぶ必要がある．
	 */
	esp_shim_coex_pre_init_entered = 1U;
	/*
	 *  実施54続き：coex_pre_init()がregi2c block=0x63（実施53で
	 *  wait_i2c_sdm_stableが待つブロック，実施44でも既出）に
	 *  何らかの影響を与えていないか，直前直後でROM常駐i2c_read
	 *  （固定ROMテーブルWIFI_ROM_PHYFUNS_TABLE_ADDR，wifi_trace.cと
	 *  同一の手法．blob初期化を待たずROM起動直後から有効）で
	 *  直接読み比べる．
	 */
	{
		typedef uint8_t (*regi2c_read_fn_t)(uint8_t, uint8_t, uint8_t);
		uint32_t			*tbl = (uint32_t *)0x4087f954UL;
		regi2c_read_fn_t	read_fn =
			(regi2c_read_fn_t)(uintptr_t)tbl[20U];

		/*
		 *  read_fn（ROM PHYFUNS表エントリtbl[20]）はブート直後は間欠的に
		 *  NULLのことがあり，そのまま呼ぶとpc=0のIllegal instructionで
		 *  クラッシュする（BTのbt_smoke経由で顕在化＝本セッションで
		 *  観測したboot varianceの真因．docs/bt-shim.md参照）．診断値
		 *  取得は非機能なのでNULL時はスキップする（read_fnが有効な
		 *  場合の従来動作は不変＝WiFi C6診断への影響なし）．coex_pre_init
		 *  本体は常に実行する．
		 */
		if (read_fn != NULL) {
			esp_shim_coex_pre_regi2c_63 = read_fn(0x63U, 1U, 0U);
		}
		pre_ret = coex_pre_init();
		if (read_fn != NULL) {
			esp_shim_coex_post_regi2c_63 = read_fn(0x63U, 1U, 0U);
		}
	}
	esp_shim_coex_pre_init_done = 1U;
	esp_shim_coex_pre_init_ret = (int32_t)pre_ret;
	if (pre_ret != 0) {
		/*
		 *  coex_pre_init失敗時のみ，従来のダミーno-opテーブルへ
		 *  フォールバックする（NULLメソッド呼出しによるクラッシュを
		 *  避けるための保険．正常経路では通らないはず）．
		 */
		syslog(LOG_ERROR, "coex_pre_init -> %d (falling back to no-op)",
			   (int_t)pre_ret);
		for (i = 0U; i < 48U; i++) {
			dummy_coexist_table[i] = (void *)coex_noop;
		}
		coexist_funcs = dummy_coexist_table;
	}
}
