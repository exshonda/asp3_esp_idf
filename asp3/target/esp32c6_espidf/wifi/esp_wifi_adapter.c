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
 *  Wi-Fi os_adapter（wifi_osi_funcs_t）のASP3実装
 *
 *  NuttXのesp_wifi_adapter.c（apache/nuttx
 *  arch/risc-v/src/esp32c3/esp_wifi_adapter.c）を設計テンプレートに，
 *  osi関数をshim基盤（esp_shim.[ch]）で実装したもの．
 *  NuttXと同じくevent group・NVSは未実装（スタブ）．
 *  設計はdocs/wifi-shim.md．
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <stdio.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_shim_cfg.h"

#include "esp_attr.h"
#include "esp_private/wifi_os_adapter.h"
#include "esp_private/wifi.h"
#include "private/esp_coexist_adapter.h"
#include "soc/periph_defs.h"
#include "esp_private/esp_modem_clock.h"

/*
 *  リンク閉包で解決するesp-hal／blob側の関数（宣言のみ）
 *
 *  C6はmodem_clockサブシステムを持ち（SOC_MODEM_CLOCK_IS_INDEPENDENT），
 *  WIFI/BT等モジュールのreset/enableはmodem_clock_module_*系を使う．
 *  periph_module_reset()（clk_gate_ll.hのTIMG0/TIMG1/UHCI0/SYSTIMER
 *  4種のみを扱う旧shared_periph_module_tテーブル）はWIFI(=4)に対して
 *  範囲外アクセスとなり，GCCが到達不能と判断してebreakを生成する
 *  （実機JTAG調査で確定．periph_ctrl.cのperiph_ll_reset参照）．
 *  C3のesp_wifi_adapter.cはこの区別がないチップ向けの実装で，本ファイルは
 *  そこからの流用時にこの差を見落としていた．
 */
extern void esp_phy_enable(int modem);
extern void esp_phy_disable(int modem);
extern void phy_wifi_enable_set(uint8_t enable);
extern int esp_phy_update_country_info(const char *country);
extern void wifi_module_enable(void);
extern void wifi_module_disable(void);
extern int esp_read_mac(uint8_t *mac, int type);

#ifndef PHY_MODEM_WIFI
#define PHY_MODEM_WIFI      1
#endif

/*
 *		割込み関連
 */
/*
 *  ESP32-C6はソースルーティング（INTMTX）とCPU割込み線制御（Espressif
 *  呼称"PLIC"＝PLIC_MX）が別ブロックに分離している（arch/riscv_gcc/
 *  esp32c6/intmtx_kernel_impl.hのPhase A実機確認済みレジスタ配置と
 *  同一．C3は両方が単一のINTMTX_BASE_ADDRに同居）．
 */
#define INTMTX_BASE_ADDR      0x60010000U   /* ソースルーティング */
#define PLICMX_BASE_ADDR      0x20001000U   /* CPU割込み線制御 */
#define PLICMX_ENABLE_REG     (PLICMX_BASE_ADDR + 0x000U)
#define PLICMX_PRI_REG(n)     (PLICMX_BASE_ADDR + 0x010U + (n) * 4U)

static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	syslog(LOG_NOTICE, "wifi_adapter: set_intr src=%d intno=%d prio=%d",
		   (int_t)intr_source, (int_t)intr_num, (int_t)intr_prio);
	/*
	 *  ソースルーティング（INTMTX＝ソース→CPU割込み線）と優先度
	 *  （PLIC_MX）．blobが使う線はカーネル管理外扱い（cfgのDEF_INHは
	 *  共通ディスパッチャ＝esp_shim.cfg参照）のため直接レジスタを
	 *  操作する．優先度はblobの指定に関わらず内部表現2（外部-2）に
	 *  固定する（C3のset_intr_wrapperと同じ方針）．
	 */
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), intr_num);
	sil_wrw_mem((void *)(uintptr_t)PLICMX_PRI_REG(intr_num), 2U);
	(void) cpu_no;
	(void) intr_prio;
}

static void
clear_intr_wrapper(uint32_t intr_source, uint32_t intr_num)
{
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), 0U);
	(void) intr_num;
}

static void
set_isr_wrapper(int32_t n, void *f, void *arg)
{
	esp_shim_set_isr(n, f, arg);
}

static void
ints_on_wrapper(uint32_t mask)
{
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)PLICMX_ENABLE_REG,
				sil_rew_mem((void *)PLICMX_ENABLE_REG) | mask);
	esp_shim_int_restore(lock);
}

static void
ints_off_wrapper(uint32_t mask)
{
	uint32_t	lock = esp_shim_int_disable();
	sil_wrw_mem((void *)PLICMX_ENABLE_REG,
				sil_rew_mem((void *)PLICMX_ENABLE_REG) & ~mask);
	esp_shim_int_restore(lock);
}

static bool
is_from_isr_wrapper(void)
{
	return(sns_ctx());
}

/*
 *		環境・スピンロック（シングルコアのため割込み禁止で代用）
 */
static bool
env_is_chip_wrapper(void)
{
	return(true);	/* 実チップ（QEMUでもWi-Fiは動かないためtrue固定） */
}

static void *
spin_lock_create_wrapper(void)
{
	return((void *)1);	/* シングルコア：実体不要（非NULLを返す） */
}

static void
spin_lock_delete_wrapper(void *lock)
{
	(void) lock;
}

static uint32_t IRAM_ATTR
wifi_int_disable_wrapper(void *wifi_int_mux)
{
	(void) wifi_int_mux;
	return(esp_shim_int_disable());
}

static void IRAM_ATTR
wifi_int_restore_wrapper(void *wifi_int_mux, uint32_t tmp)
{
	(void) wifi_int_mux;
	esp_shim_int_restore(tmp);
}

static void IRAM_ATTR
task_yield_from_isr_wrapper(void)
{
	/* ASP3では割込み出口でディスパッチされるため何もしない */
}

/*
 *		セマフォ・ミューテックス
 */
static void *
semphr_create_wrapper(uint32_t max, uint32_t init)
{
	return(esp_shim_sem_create(max, init));
}

static void
semphr_delete_wrapper(void *semphr)
{
	esp_shim_sem_delete(semphr);
}

static int32_t
semphr_take_wrapper(void *semphr, uint32_t block_time_tick)
{
	return(esp_shim_sem_take(semphr, block_time_tick));
}

static int32_t
semphr_give_wrapper(void *semphr)
{
	return(esp_shim_sem_give(semphr));
}

extern void *esp_shim_thread_semphr_get(void);

static void *
mutex_create_wrapper(void)
{
	return(esp_shim_mutex_create(false));
}

static void *
recursive_mutex_create_wrapper(void)
{
	return(esp_shim_mutex_create(true));
}

static void
mutex_delete_wrapper(void *mutex)
{
	esp_shim_mutex_delete(mutex);
}

static int32_t
mutex_lock_wrapper(void *mutex)
{
	return(esp_shim_mutex_lock(mutex));
}

static int32_t
mutex_unlock_wrapper(void *mutex)
{
	return(esp_shim_mutex_unlock(mutex));
}

/*
 *		キュー
 */
static void *
queue_create_wrapper(uint32_t queue_len, uint32_t item_size)
{
	return(esp_shim_queue_create(queue_len, item_size));
}

static void
queue_delete_wrapper(void *queue)
{
	esp_shim_queue_delete(queue);
}

static int32_t
queue_send_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, false));
}

static int32_t IRAM_ATTR
queue_send_from_isr_wrapper(void *queue, void *item, void *hptw)
{
	if (hptw != NULL) {
		*(int *)hptw = 0;	/* higher priority task woken：ASP3では不要 */
	}
	return(esp_shim_queue_send_from_isr(queue, item));
}

static int32_t
queue_send_to_back_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, false));
}

static int32_t
queue_send_to_front_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_send(queue, item, block_time_tick, true));
}

static int32_t
queue_recv_wrapper(void *queue, void *item, uint32_t block_time_tick)
{
	return(esp_shim_queue_recv(queue, item, block_time_tick));
}

static uint32_t
queue_msg_waiting_wrapper(void *queue)
{
	return(esp_shim_queue_msg_waiting(queue));
}

static void *
wifi_create_queue_wrapper(int queue_len, int item_size)
{
	/*
	 *  blobはwifi_static_queue_t（{handle,storage}）形式を期待する
	 *  （NuttX実装と同じ）
	 */
	wifi_static_queue_t	*wq;

	wq = (wifi_static_queue_t *)esp_shim_calloc(1U,
												sizeof(wifi_static_queue_t));
	if (wq == NULL) {
		return(NULL);
	}
	wq->handle = esp_shim_queue_create((uint32_t)queue_len,
									   (uint32_t)item_size);
	if (wq->handle == NULL) {
		esp_shim_free(wq);
		return(NULL);
	}
	return(wq);
}

static void
wifi_delete_queue_wrapper(void *queue)
{
	wifi_static_queue_t	*wq = (wifi_static_queue_t *)queue;

	if (wq != NULL) {
		esp_shim_queue_delete(wq->handle);
		esp_shim_free(wq);
	}
}

/*
 *		event group（NuttXと同じく未実装：blobは通常経路では使わない）
 */
static void *
event_group_create_wrapper(void)
{
	syslog(LOG_ERROR, "wifi_adapter: event_group not supported");
	return(NULL);
}

static void
event_group_delete_wrapper(void *event)
{
	(void) event;
}

static uint32_t
event_group_set_bits_wrapper(void *event, uint32_t bits)
{
	(void) event;
	return(bits);
}

static uint32_t
event_group_clear_bits_wrapper(void *event, uint32_t bits)
{
	(void) event;
	return(bits);
}

static uint32_t
event_group_wait_bits_wrapper(void *event, uint32_t bits_to_wait_for,
							  int clear_on_exit, int wait_for_all_bits,
							  uint32_t block_time_tick)
{
	(void) event; (void) bits_to_wait_for; (void) clear_on_exit;
	(void) wait_for_all_bits; (void) block_time_tick;
	return(0U);
}

/*
 *		タスク
 */
static int32_t
task_create_wrapper(void *task_func, const char *name, uint32_t stack_depth,
					void *param, uint32_t prio, void *task_handle)
{
	return(esp_shim_task_create((void (*)(void *))task_func, name,
								stack_depth, param, prio,
								(void **)task_handle));
}

static int32_t
task_create_pinned_to_core_wrapper(void *task_func, const char *name,
								   uint32_t stack_depth, void *param,
								   uint32_t prio, void *task_handle,
								   uint32_t core_id)
{
	(void) core_id;		/* シングルコア */
	return(esp_shim_task_create((void (*)(void *))task_func, name,
								stack_depth, param, prio,
								(void **)task_handle));
}

static void
task_delete_wrapper(void *task_handle)
{
	esp_shim_task_delete(task_handle);
}

static void
task_delay_wrapper(uint32_t tick)
{
	esp_shim_task_delay(tick);
}

static int32_t
task_ms_to_tick_wrapper(uint32_t ms)
{
	return((int32_t)ms);	/* tick＝1ms */
}

static void *
task_get_current_task_wrapper(void)
{
	return(esp_shim_task_get_current());
}

static int32_t
task_get_max_priority_wrapper(void)
{
	return(25);		/* FreeRTOS互換の見かけの値（実際の写像はshim内） */
}

/*
 *		メモリ（全系統をshimヒープへ一本化）
 */
static void *
malloc_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void
free_wrapper(void *p)
{
	esp_shim_free(p);
}

static void *
malloc_internal_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void *
realloc_internal_wrapper(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

static void *
calloc_internal_wrapper(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

static void *
zalloc_internal_wrapper(size_t size)
{
	return(esp_shim_calloc(1U, size));
}

static void *
wifi_malloc_wrapper(size_t size)
{
	return(esp_shim_malloc(size));
}

static void *
wifi_realloc_wrapper(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

static void *
wifi_calloc_wrapper(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

static void *
wifi_zalloc_wrapper(size_t size)
{
	return(esp_shim_calloc(1U, size));
}

static uint32_t
get_free_heap_size_wrapper(void)
{
	return((uint32_t)esp_shim_heap_free_size());
}

/*
 *		イベント（esp_event_shim.cの最小実装へ）
 */
extern int esp_event_post(const char *event_base, int32_t event_id,
						  void *event_data, size_t event_data_size,
						  uint32_t ticks_to_wait);

static int32_t
event_post_wrapper(const char *event_base, int32_t event_id,
				   void *event_data, size_t event_data_size,
				   uint32_t ticks_to_wait)
{
	return(esp_event_post(event_base, event_id, event_data,
						  event_data_size, ticks_to_wait));
}

/*
 *		電源・クロック・PHY
 */
static void
dport_access_stall_other_cpu_start_wrapper(void)
{
	/* シングルコア：不要 */
}

static void
dport_access_stall_other_cpu_end_wrapper(void)
{
	/* シングルコア：不要 */
}

static void
wifi_apb80m_request_wrapper(void)
{
	/* 省電力（auto sleep）非対応：不要 */
}

static void
wifi_apb80m_release_wrapper(void)
{
}

static void
phy_enable_wrapper(void)
{
	esp_phy_enable(PHY_MODEM_WIFI);
	phy_wifi_enable_set(1U);
}

static void
phy_disable_wrapper(void)
{
	phy_wifi_enable_set(0U);
	esp_phy_disable(PHY_MODEM_WIFI);
}

static int
read_mac_wrapper(uint8_t *mac, unsigned int type)
{
	return(esp_read_mac(mac, (int)type));
}

static void
wifi_reset_mac_wrapper(void)
{
	modem_clock_module_mac_reset(PERIPH_WIFI_MODULE);
}

static void
wifi_clock_enable_wrapper(void)
{
	wifi_module_enable();
}

static void
wifi_clock_disable_wrapper(void)
{
	wifi_module_disable();
}

static void
wifi_rtc_enable_iso_wrapper(void)
{
	/* MAC/BBパワーダウン非対応：不要 */
}

static void
wifi_rtc_disable_iso_wrapper(void)
{
}

static uint32_t
slowclk_cal_get_wrapper(void)
{
	/*
	 *  RTCスローклックの較正値（Q13固定小数点）．
	 *  RTC_CNTL_STORE1に格納された値を返す（ROM/ブート時の設定を流用）．
	 *  未設定（0）の場合は150kHz RCの公称値を返す．
	 */
	/*  LP_AON_STORE1_REG（C3のRTC_CNTL_STORE1相当．DR_REG_LP_AON_BASE
	 *  (0x600B1000)+0x4）．C6はRTC_CNTL→LP_AON等へ分割・改称された．  */
	uint32_t cal = sil_rew_mem((void *)0x600B1004U);	/* LP_AON_STORE1 */
	if (cal == 0U) {
		cal = (uint32_t)((1000000ULL << 13) / 150000U);
	}
	return(cal);
}

/*
 *		タイマ
 */
static void
timer_arm_wrapper(void *timer, uint32_t tmout, bool repeat)
{
	esp_shim_timer_arm_us(timer, tmout * 1000U, repeat);
}

static void
timer_arm_us_wrapper(void *ptimer, uint32_t us, bool repeat)
{
	esp_shim_timer_arm_us(ptimer, us, repeat);
}

static void
timer_disarm_wrapper(void *timer)
{
	esp_shim_timer_disarm(timer);
}

static void
timer_done_wrapper(void *ptimer)
{
	esp_shim_timer_done(ptimer);
}

static void
timer_setfn_wrapper(void *ptimer, void *pfunction, void *parg)
{
	esp_shim_timer_setfn(ptimer, (void (*)(void *))pfunction, parg);
}

static int64_t
esp_timer_get_time_wrapper(void)
{
	return(esp_shim_time_us());
}

/*
 *		NVS（NuttXと同じく未実装）
 */
static int
nvs_set_i8_wrapper(uint32_t handle, const char *key, int8_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_i8_wrapper(uint32_t handle, const char *key, int8_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_set_u8_wrapper(uint32_t handle, const char *key, uint8_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_u8_wrapper(uint32_t handle, const char *key, uint8_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_set_u16_wrapper(uint32_t handle, const char *key, uint16_t value)
{
	(void) handle; (void) key; (void) value;
	return(-1);
}

static int
nvs_get_u16_wrapper(uint32_t handle, const char *key, uint16_t *out_value)
{
	(void) handle; (void) key; (void) out_value;
	return(-1);
}

static int
nvs_open_wrapper(const char *name, unsigned int open_mode,
				 uint32_t *out_handle)
{
	(void) name; (void) open_mode; (void) out_handle;
	return(-1);
}

static void
nvs_close_wrapper(uint32_t handle)
{
	(void) handle;
}

static int
nvs_commit_wrapper(uint32_t handle)
{
	(void) handle;
	return(-1);
}

static int
nvs_set_blob_wrapper(uint32_t handle, const char *key, const void *value,
					 size_t length)
{
	(void) handle; (void) key; (void) value; (void) length;
	return(-1);
}

static int
nvs_get_blob_wrapper(uint32_t handle, const char *key, void *out_value,
					 size_t *length)
{
	(void) handle; (void) key; (void) out_value; (void) length;
	return(-1);
}

static int
nvs_erase_key_wrapper(uint32_t handle, const char *key)
{
	(void) handle; (void) key;
	return(-1);
}

/*
 *		乱数・時刻
 */
static uint32_t
rand_wrapper(void)
{
	return(esp_shim_random());
}

static int
get_random_wrapper(uint8_t *buf, size_t len)
{
	size_t	i;

	for (i = 0U; i < len; i++) {
		buf[i] = (uint8_t)(esp_shim_random() & 0xFFU);
	}
	return(0);
}

static int
get_time_wrapper(void *t)
{
	struct {
		long	tv_sec;
		long	tv_usec;
	} *tv = t;
	int64_t	us = esp_shim_time_us();

	tv->tv_sec = (long)(us / 1000000);
	tv->tv_usec = (long)(us % 1000000);
	return(0);
}

static unsigned long
random_wrapper(void)
{
	return((unsigned long)esp_shim_random());
}

/*
 *		ログ
 */
static void
log_writev_wrapper(unsigned int level, const char *tag, const char *format,
				   va_list args)
{
	char	buf[128];

	(void) level;
	(void) tag;
	vsnprintf(buf, sizeof(buf), format, args);
	syslog(LOG_NOTICE, "%s", buf);
}

static void
log_write_wrapper(unsigned int level, const char *tag, const char *format, ...)
{
	va_list	args;

	va_start(args, format);
	log_writev_wrapper(level, tag, format, args);
	va_end(args);
}

static uint32_t
log_timestamp_wrapper(void)
{
	return((uint32_t)(esp_shim_time_us() / 1000));
}

/*
 *		coexistence（libcoexist.aへのパススルー）
 */
extern int coex_init(void);
extern void coex_deinit(void);
extern int coex_enable(void);
extern void coex_disable(void);
extern uint32_t coex_status_get(void);
extern void coex_condition_set(uint32_t type, bool dissatisfy);
extern int coex_wifi_request(uint32_t event, uint32_t latency,
							 uint32_t duration);
extern int coex_wifi_release(uint32_t event);
extern int coex_wifi_channel_set(uint8_t primary, uint8_t secondary);
extern int coex_event_duration_get(uint32_t event, uint32_t *duration);
extern int coex_pti_get(uint32_t event, uint8_t *pti);
extern void coex_schm_status_bit_clear(uint32_t type, uint32_t status);
extern void coex_schm_status_bit_set(uint32_t type, uint32_t status);
extern int coex_schm_interval_set(uint32_t interval);
extern uint32_t coex_schm_interval_get(void);
extern uint8_t coex_schm_curr_period_get(void);
extern void *coex_schm_curr_phase_get(void);
extern int coex_schm_process_restart(void);
extern int coex_schm_register_callback(int type, int (*cb)(int));
/*
 *  blob側の実シンボル名は coex_register_start_cb（末尾_callbackでは
 *  ない）．nm確認済み（hal/components/esp_coex/lib/esp32c3/
 *  libcoexist.a）．
 */
extern int coex_register_start_cb(int (*cb)(void));
extern int coex_schm_flexible_period_set(uint8_t period);
extern uint8_t coex_schm_flexible_period_get(void);
extern void *coex_schm_get_phase_by_idx(int idx);

/*
 *		osiテーブル本体
 */
wifi_osi_funcs_t g_wifi_osi_funcs = {
	._version = ESP_WIFI_OS_ADAPTER_VERSION,
	._env_is_chip = env_is_chip_wrapper,
	._set_intr = set_intr_wrapper,
	._clear_intr = clear_intr_wrapper,
	._set_isr = set_isr_wrapper,
	._ints_on = ints_on_wrapper,
	._ints_off = ints_off_wrapper,
	._is_from_isr = is_from_isr_wrapper,
	._spin_lock_create = spin_lock_create_wrapper,
	._spin_lock_delete = spin_lock_delete_wrapper,
	._wifi_int_disable = wifi_int_disable_wrapper,
	._wifi_int_restore = wifi_int_restore_wrapper,
	._task_yield_from_isr = task_yield_from_isr_wrapper,
	._semphr_create = semphr_create_wrapper,
	._semphr_delete = semphr_delete_wrapper,
	._semphr_take = semphr_take_wrapper,
	._semphr_give = semphr_give_wrapper,
	._wifi_thread_semphr_get = esp_shim_thread_semphr_get,
	._mutex_create = mutex_create_wrapper,
	._recursive_mutex_create = recursive_mutex_create_wrapper,
	._mutex_delete = mutex_delete_wrapper,
	._mutex_lock = mutex_lock_wrapper,
	._mutex_unlock = mutex_unlock_wrapper,
	._queue_create = queue_create_wrapper,
	._queue_delete = queue_delete_wrapper,
	._queue_send = queue_send_wrapper,
	._queue_send_from_isr = queue_send_from_isr_wrapper,
	._queue_send_to_back = queue_send_to_back_wrapper,
	._queue_send_to_front = queue_send_to_front_wrapper,
	._queue_recv = queue_recv_wrapper,
	._queue_msg_waiting = queue_msg_waiting_wrapper,
	._event_group_create = event_group_create_wrapper,
	._event_group_delete = event_group_delete_wrapper,
	._event_group_set_bits = event_group_set_bits_wrapper,
	._event_group_clear_bits = event_group_clear_bits_wrapper,
	._event_group_wait_bits = event_group_wait_bits_wrapper,
	._task_create_pinned_to_core = task_create_pinned_to_core_wrapper,
	._task_create = task_create_wrapper,
	._task_delete = task_delete_wrapper,
	._task_delay = task_delay_wrapper,
	._task_ms_to_tick = task_ms_to_tick_wrapper,
	._task_get_current_task = task_get_current_task_wrapper,
	._task_get_max_priority = task_get_max_priority_wrapper,
	._malloc = malloc_wrapper,
	._free = free_wrapper,
	._event_post = event_post_wrapper,
	._get_free_heap_size = get_free_heap_size_wrapper,
	._rand = rand_wrapper,
	._dport_access_stall_other_cpu_start_wrap =
		dport_access_stall_other_cpu_start_wrapper,
	._dport_access_stall_other_cpu_end_wrap =
		dport_access_stall_other_cpu_end_wrapper,
	._wifi_apb80m_request = wifi_apb80m_request_wrapper,
	._wifi_apb80m_release = wifi_apb80m_release_wrapper,
	._phy_disable = phy_disable_wrapper,
	._phy_enable = phy_enable_wrapper,
	._phy_update_country_info = esp_phy_update_country_info,
	._read_mac = read_mac_wrapper,
	._timer_arm = timer_arm_wrapper,
	._timer_disarm = timer_disarm_wrapper,
	._timer_done = timer_done_wrapper,
	._timer_setfn = timer_setfn_wrapper,
	._timer_arm_us = timer_arm_us_wrapper,
	._wifi_reset_mac = wifi_reset_mac_wrapper,
	._wifi_clock_enable = wifi_clock_enable_wrapper,
	._wifi_clock_disable = wifi_clock_disable_wrapper,
	._wifi_rtc_enable_iso = wifi_rtc_enable_iso_wrapper,
	._wifi_rtc_disable_iso = wifi_rtc_disable_iso_wrapper,
	._esp_timer_get_time = esp_timer_get_time_wrapper,
	._nvs_set_i8 = nvs_set_i8_wrapper,
	._nvs_get_i8 = nvs_get_i8_wrapper,
	._nvs_set_u8 = nvs_set_u8_wrapper,
	._nvs_get_u8 = nvs_get_u8_wrapper,
	._nvs_set_u16 = nvs_set_u16_wrapper,
	._nvs_get_u16 = nvs_get_u16_wrapper,
	._nvs_open = nvs_open_wrapper,
	._nvs_close = nvs_close_wrapper,
	._nvs_commit = nvs_commit_wrapper,
	._nvs_set_blob = nvs_set_blob_wrapper,
	._nvs_get_blob = nvs_get_blob_wrapper,
	._nvs_erase_key = nvs_erase_key_wrapper,
	._get_random = get_random_wrapper,
	._get_time = get_time_wrapper,
	._random = random_wrapper,
	._slowclk_cal_get = slowclk_cal_get_wrapper,
	._log_write = log_write_wrapper,
	._log_writev = log_writev_wrapper,
	._log_timestamp = log_timestamp_wrapper,
	._malloc_internal = malloc_internal_wrapper,
	._realloc_internal = realloc_internal_wrapper,
	._calloc_internal = calloc_internal_wrapper,
	._zalloc_internal = zalloc_internal_wrapper,
	._wifi_malloc = wifi_malloc_wrapper,
	._wifi_realloc = wifi_realloc_wrapper,
	._wifi_calloc = wifi_calloc_wrapper,
	._wifi_zalloc = wifi_zalloc_wrapper,
	._wifi_create_queue = wifi_create_queue_wrapper,
	._wifi_delete_queue = wifi_delete_queue_wrapper,
	._coex_init = coex_init,
	._coex_deinit = coex_deinit,
	._coex_enable = coex_enable,
	._coex_disable = coex_disable,
	._coex_status_get = coex_status_get,
	._coex_condition_set = coex_condition_set,
	._coex_wifi_request = coex_wifi_request,
	._coex_wifi_release = coex_wifi_release,
	._coex_wifi_channel_set = coex_wifi_channel_set,
	._coex_event_duration_get = coex_event_duration_get,
	._coex_pti_get = coex_pti_get,
	._coex_schm_status_bit_clear = coex_schm_status_bit_clear,
	._coex_schm_status_bit_set = coex_schm_status_bit_set,
	._coex_schm_interval_set = coex_schm_interval_set,
	._coex_schm_interval_get = coex_schm_interval_get,
	._coex_schm_curr_period_get = coex_schm_curr_period_get,
	._coex_schm_curr_phase_get = coex_schm_curr_phase_get,
	._coex_schm_process_restart = coex_schm_process_restart,
	._coex_schm_register_cb = coex_schm_register_callback,
	._coex_register_start_cb = coex_register_start_cb,
	._coex_schm_flexible_period_set = coex_schm_flexible_period_set,
	._coex_schm_flexible_period_get = coex_schm_flexible_period_get,
	._coex_schm_get_phase_by_idx = coex_schm_get_phase_by_idx,
	._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};
