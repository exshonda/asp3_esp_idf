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
 *  Wi-Fi os_adapter（wifi_osi_funcs_t）のASP3実装（ESP32-C5版）
 *
 *  esp32c6版（asp3_target/esp32c6_espidf/wifi/esp_wifi_adapter.c）から
 *  のコピー・C5対応（docs/c5-port-design.md §5.4）。設計テンプレートは
 *  NuttXのesp_wifi_adapter.c（apache/nuttx
 *  arch/risc-v/src/esp32c3/esp_wifi_adapter.c）で，osi関数をshim基盤
 *  （esp_shim.[ch]）で実装したもの．NuttXと同じくevent group・NVSは
 *  未実装（スタブ）．設計はdocs/wifi-shim.md．
 *
 *  ■ C6からの変更点（本ファイルの書き換え量が最大．docs/c5-port-design.md
 *    §5.4・§9参照）
 *   1. 割込み関連（下記「割込み関連」節）：C6独自"PLIC_MX"方式
 *      （メモリマップトレジスタ配列＝ENABLE/TYPE/PRI）を，標準RISC-V
 *      CLIC方式（CLIC_INT_CTRL_REG(i)のIE/ATTR/CTLバイト操作．
 *      arch/riscv_gcc/esp32c5/clic_kernel_impl.hと同じ規約）へ全面
 *      書き換えた。同ヘッダはカーネル内部専用（他ファイルからinclude
 *      禁止と明記）のため，本ファイルでは同じ規約を独立に定義している
 *      （二重定義ではなく意図的な並行定義）。
 *   2. MODEM_SYSCON：C6の0x600A9800から**0x600A9C00へ移動**
 *      （esp_shim_modem_icg_init参照．+0x400移動．C6の値を転記せず
 *      soc/esp32c5/include/modem/reg_base.hで個別に確認済み）。
 *      MODEM_LPCON（0x600AF000）・PMU（0x600B0000）・LP_AON（0x600B1000）
 *      はC6と同一（soc/esp32c5/include/modem/reg_base.h・
 *      soc/esp32c5/register/soc/reg_base.h・
 *      soc/esp32c5/register/soc/lp_aon_reg.hで確認済み）。
 *   3. wifi_clock_enable_wrapper/phy_enable_wrapperのWIFIPWRクロック
 *      ドメイン有効化（MODEM_LPCON_CLK_CONF_REG＝0x600af018のbit0-2）は
 *      C6の根本原因修正のうちレジスタ名・オフセットともC5でも完全に
 *      同一と確認できたためそのまま移植した（soc/esp32c5/include/
 *      modem/modem_lpcon_reg.hで確認）。一方，C6版が広域JTAG diffで
 *      追加していた0x600af008／0x600af048への書込みは，前者は「検証の
 *      ため」と明記された未確定の補助措置，後者はC5では
 *      MODEM_LPCON_DCMEM_VALID_3_REG（DCMEM有効性ビットマップ．LP
 *      クロックとは無関係な別レジスタ）に相当し，C6の値をそのまま
 *      書き込むのは意味的に誤りうるため，本ポートでは移植せず削除した
 *      （下記phy_enable_wrapper・wifi_clock_enable_wrapperのコメント
 *      参照）。
 *   4. C6専用のGROUND-TRUTH比較用RTCカウンタ（GT_SEMTAKE等）・
 *      phy_enable_wrapper内のRTC診断マーカ（0x50000080/0x50000084）は
 *      C6のAGC/deaf-RX調査専用の一時計装のため移植しない
 *      （docs/c5-port-design.md §8.3「C6資産の非移植方針」）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include <stdio.h>
#include <sil.h>
#include "esp_shim.h"
#include "esp_shim_cfg.h"
#include "esp32c5.h"		/* ESP32C5_INTMTX_BASE／ESP32C5_CLIC_CTRL_BASE等
							   （CHIPDIRがASP3_INCLUDE_DIRSに含まれるため
							   到達可能．arch/riscv_gcc/esp32c5/chip.cmake参照） */

#include "esp_attr.h"
#include "esp_private/wifi_os_adapter.h"
#include "esp_private/wifi.h"
#include "private/esp_coexist_adapter.h"
#include "soc/periph_defs.h"
#include "esp_private/esp_modem_clock.h"
#include "hal/regi2c_ctrl_ll.h"
#include "hal/modem_lpcon_ll.h"
#include "hal/modem_syscon_ll.h"
#include "hal/pmu_ll.h"

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
 *  ESP32-C5は標準RISC-V CLICを採用する（C6独自の"PLIC_MX"方式とは
 *  アーキテクチャレベルで異なる．docs/c5-port-design.md §3・§4・§8.2，
 *  arch/riscv_gcc/esp32c5/clic_kernel_impl.h冒頭コメント参照）。
 *
 *  ソースルーティング（INTMTX．ペリフェラル割込みソース→CPU割込み線）
 *  自体はC6・C3と同一アドレス・同一方式のまま存続する
 *  （soc/esp32c5/register/soc/reg_base.hでDR_REG_INTMTX_BASE=0x60010000
 *  がC6と同一値であることを確認済み）。
 *
 *  CPU割込み線側の制御（C6はENABLE/TYPE/PRIの独立したメモリマップト
 *  レジスタ配列＝PLIC_MX）は，標準RISC-V CLICのCLIC_INT_CTRL_REG(i)
 *  （1ワードにIP/IE/ATTR(TRIG/SHV/MODE)/CTL(優先度)をバイト単位で
 *  格納．soc/esp32c5/include/soc/clic_reg.h）へ全面書き換えた。
 *  ここで使う規約（CLIC内部番号=ASP3 INTNO+16・NLBITS=3・ATTRバイトの
 *  MODE/TRIG/SHVビット位置）はarch/riscv_gcc/esp32c5/clic_kernel_impl.h
 *  （フェーズ2aでCLIC対応の基盤として実装済み）と完全に同じ規約だが，
 *  同ヘッダはカーネル内部専用（ファイル冒頭に「他のファイルから直接
 *  インクルードしてはならない」と明記）のため，本ファイルでは同じ
 *  規約を独立に（ローカルマクロとして）定義する。
 */
#define INTMTX_BASE_ADDR       ESP32C5_INTMTX_BASE      /* 0x60010000．ソースルーティング（C6・C3と同一．変更なし） */
#define CLIC_CTRL_BASE_ADDR    ESP32C5_CLIC_CTRL_BASE    /* 0x20801000．CPU割込み線制御（CLIC_INT_CTRL_REG(i)=BASE+4i） */
#define CLIC_EXT_OFFSET        16U                       /* ASP3 INTNO(1〜31)→CLIC内部番号(17〜47)のオフセット */
#define CLIC_LINE(intno)       ((uint32_t)(intno) + CLIC_EXT_OFFSET)
#define CLIC_IE_OFF(line)      (CLIC_CTRL_BASE_ADDR + (line) * 4U + 1U)  /* 許可（1byte） */
#define CLIC_ATTR_OFF(line)    (CLIC_CTRL_BASE_ADDR + (line) * 4U + 2U)  /* SHV/TRIG/MODE（1byte） */
#define CLIC_CTL_OFF(line)     (CLIC_CTRL_BASE_ADDR + (line) * 4U + 3U)  /* 優先度（1byte，上位3bit=NLBITSが実効） */

/*
 *  ATTRバイト＝MODE(bit[7:6])<<6 | TRIG(bit[2:1])<<1 | SHV(bit0)．
 *  MODE=3(machine)・TRIG=0(level)・SHV=0(非ベクタド)＝0xC0
 *  （soc/esp32c5/include/soc/clic_reg.hのBYTE_CLIC_INT_ATTR_*ビット
 *  位置で確認．clic_kernel_impl.hのCLIC_ATTR_MODE_MACHINEと同じ値）。
 */
#define CLIC_ATTR_LEVEL_MACHINE   UINT_C(0xC0)

/*
 *  優先度→CTLバイト変換（NLBITS=3．clic_kernel_impl.hのCLIC_NLBITS_TO_BYTE
 *  と同じ規約．上位3bitが実効優先度，下位5bitは1で埋める）。
 */
#define CLIC_NLBITS_SHIFT      5U
#define CLIC_PRIO_TO_CTL(pri) \
	(((uint32_t)(pri) << CLIC_NLBITS_SHIFT) | ((1U << CLIC_NLBITS_SHIFT) - 1U))

static void
set_intr_wrapper(int32_t cpu_no, uint32_t intr_source, uint32_t intr_num,
				 int32_t intr_prio)
{
	uint32_t	line = CLIC_LINE(intr_num);

	syslog(LOG_NOTICE, "wifi_adapter: set_intr src=%d intno=%d prio=%d",
		   (int_t)intr_source, (int_t)intr_num, (int_t)intr_prio);
	/*
	 *  ソースルーティング（INTMTX＝ソース→CPU割込み線）と優先度
	 *  （CLIC_INT_CTRL_REG(i)のCTLバイト）．blobが使う線はカーネル
	 *  管理外扱い（cfgのDEF_INHは共通ディスパッチャ＝esp_shim.cfg参照）
	 *  のため直接レジスタを操作する．優先度はblobの指定に関わらず
	 *  内部表現2（外部-2）に固定する（C6・C3のset_intr_wrapperと
	 *  同じ方針）．
	 *
	 *  ATTRバイトを明示的にLEVEL型・machineモード・非ベクタドへ設定
	 *  する。chip_initialize()のclic_initialize()（arch/riscv_gcc/
	 *  esp32c5/chip_kernel_impl.c）が起動時に全CLIC外部割込み線へ同じ
	 *  値を設定済みだが，C6の実機調査で「WiFi MAC割込み線のLEVEL型
	 *  未設定」が実バグだったという教訓（docs/wifi-shim-c6.md）を
	 *  踏まえ，blobが動的にset_intrを呼ぶたびにここでも不変条件として
	 *  明示的に保証する（起動時設定と同値のため冪等・無害）。WiFi MAC
	 *  割込みはinterrupts.hで"level"型と明記されており，CPU側がEDGE型
	 *  のままだと，レベルで張り続ける信号を取りこぼす（最初の一度も
	 *  含めて）おそれがある。
	 */
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), intr_num);
	sil_wrb_mem((void *)CLIC_CTL_OFF(line), (uint8_t)CLIC_PRIO_TO_CTL(2U));
	sil_wrb_mem((void *)CLIC_ATTR_OFF(line), (uint8_t)CLIC_ATTR_LEVEL_MACHINE);
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

/*
 *  C6のPLICMX_ENABLE_REGは1本のビットマスクレジスタ（bit n＝CPU割込み
 *  線nの許可）だったが，CLICはCLIC_INT_CTRL_REG(i)のIEバイトが線ごとに
 *  独立しているため単一のビットマスクレジスタが存在しない．blobから
 *  渡される mask のビットnを「CPU割込み線n（＝ASP3のASP3擬似INTNO）の
 *  許可/禁止」として1本ずつCLICのIEバイトへ反映する（意味的には
 *  C6版と同じ効果）．
 */
static void
ints_on_wrapper(uint32_t mask)
{
	uint32_t	lock = esp_shim_int_disable();
	uint32_t	n;

	for (n = 0U; n < 32U; n++) {
		if ((mask & (1UL << n)) != 0U) {
			sil_wrb_mem((void *)CLIC_IE_OFF(CLIC_LINE(n)), 1U);
		}
	}
	esp_shim_int_restore(lock);
}

static void
ints_off_wrapper(uint32_t mask)
{
	uint32_t	lock = esp_shim_int_disable();
	uint32_t	n;

	for (n = 0U; n < 32U; n++) {
		if ((mask & (1UL << n)) != 0U) {
			sil_wrb_mem((void *)CLIC_IE_OFF(CLIC_LINE(n)), 0U);
		}
	}
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
	/*  ★根本原因修正（C6実施6/追記10からの移植）：MODEM_LPCON_CLK_CONF
	 *  (0x600af018)のWIFIPWR(bit0)/COEX(bit1)/I2C_MST(bit2)クロックを，
	 *  PHY較正（esp_phy_enable→register_chipv7_phyがregi2c経由でRFを
	 *  較正）の直前に確実に有効化する．C6ではJTAGでnative(受信OK)=0x7
	 *  vs ASP3=0x0の差分として特定・実機検証済みの根本原因修正．
	 *  MODEM_LPCON_CLK_CONF_REGはC5でもベースアドレス（0x600AF000）・
	 *  オフセット（+0x18）・ビット位置（bit0=WIFIPWR/bit1=COEX/
	 *  bit2=I2C_MST）ともC6と完全に同一であることをsoc/esp32c5/include/
	 *  modem/modem_lpcon_reg.hで確認済みのため，そのまま移植する
	 *  （docs/c5-port-design.md §9）．I2C_MSTが立っていないとregi2c
	 *  越しのRF較正が無応答で受信不能＝AP 0個になる．
	 *
	 *  【C6からの差分・非移植】C6版はさらに0x600af008
	 *  （MODEM_LPCON_COEX_LP_CLK_CONF_REG．C5でも同名・同オフセットで
	 *  実在するが「検証のため」の未確定の補助措置であり根本原因修正
	 *  ではない）と0x600af048（C6のヘッダには対応する名前付きレジスタ
	 *  が存在せず，実機JTAGの広域diffで見つかった未公開アドレスへの
	 *  書込み）を行っていた．C5では0x600af048はMODEM_LPCON_DCMEM_
	 *  VALID_3_REG（DCMEM有効性ビットマップ．LPクロックとは無関係な
	 *  別レジスタ．soc/esp32c5/include/modem/modem_lpcon_reg.hで確認）
	 *  に相当するため，C6の値0x314をそのまま書き込むのは意味的に誤り
	 *  うる．未検証の判別指標を転記しない方針（memory/
	 *  feedback_hardware_investigation_rigor.md）に従い，本ポートでは
	 *  この2行を移植しない．C5で受信不成立が再現した場合の実機investigation
	 *  候補としてのみ記録する（【実機確認待ち】）．
	 */
	*(volatile uint32_t *)0x600af018U = 0x7U;
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

/*
 *  Zephyr soc_hw_init()のmodem ICG設定に相当する初期化（C6側で
 *  「根本原因候補」として実装後，C6実機JTAG実測でRX不成立とは無関係な
 *  冗長設定と判明し無効化された経緯がある．下記wifi_clock_enable_wrapper
 *  参照）．C5へはレジスタアドレスのみ更新して移植する（実装自体は
 *  C6同様，呼び出さない＝無効化した状態を維持．C5固有の実機検証で
 *  必要性を再確認すること）．
 *
 *  【C5でのレジスタ差替え】pmu（0x600B0000）・modem_lpcon（0x600AF000）
 *  はC6と同一アドレス．**modem_sysconのみ0x600A9800→0x600A9C00へ
 *  変更**（C6から+0x400移動．soc/esp32c5/include/modem/reg_base.hの
 *  DR_REG_MODEM_SYSCON_BASEで確認済み．docs/c5-port-design.md §9）．
 *  C6の値をそのまま転記しないという方針に従い個別に確認した．
 */
static void
esp_shim_modem_icg_init(void)
{
	pmu_dev_t			*pmu = (pmu_dev_t *)0x600B0000U;
	modem_lpcon_dev_t	*lpcon = (modem_lpcon_dev_t *)0x600AF000U;
	modem_syscon_dev_t	*syscon = (modem_syscon_dev_t *)0x600A9C00U;	/* C6の0x600A9800から+0x400移動 */
	uint32_t			code_bit = 1U << 2;	/* BIT(PMU_HP_ICG_MODEM_CODE_ACTIVE=2) */

	pmu_ll_hp_set_icg_modem(pmu, PMU_MODE_HP_ACTIVE, 2U);
	modem_syscon_ll_set_modem_apb_icg_bitmap(syscon, code_bit);
	modem_lpcon_ll_set_i2c_master_icg_bitmap(lpcon, code_bit);
	modem_lpcon_ll_set_lp_apb_icg_bitmap(lpcon, code_bit);
	pmu_ll_imm_update_dig_icg_modem_code(pmu, true);
	pmu_ll_imm_update_dig_icg_switch(pmu, true);
}

static void
wifi_clock_enable_wrapper(void)
{
	static bool_t	lpclk_selected = false;

	/*  C6ではesp_shim_modem_icg_init()はJTAG実測で冗長と判明
	 *  （clk_conf_power_st=0x66660000は既にnative一致＝modem_clockが
	 *  設定）．RXブロッカーはICGでもクロックでもなくRF/regi2c較正層
	 *  だった．C5でも同じ判断を暫定的に踏襲し無効化するが，C5は
	 *  未検証（【実機確認待ち】）のため，deaf-RX相当の症状が出た場合は
	 *  真っ先に疑うべき候補としてここに残す． */
	(void) esp_shim_modem_icg_init;

	/*
	 *  esp_perip_clk_init()（esp-hal-3rdpartyのesp_system/port/soc/
	 *  <chip>/clk.c．C6ではesp32c6/clk.c，C5では対応するesp32c5/clk.c
	 *  相当）が通常起動（第2段ブートローダ経由）で行うWi-Fi低電力
	 *  クロックソースの選択をここで代替する．Direct Bootではこの関数
	 *  自体が一切呼ばれないため，modem_clock_module_enable(PERIPH_WIFI_
	 *  MODULE)（wifi_module_enable経由）だけではWIFIPWRクロック
	 *  ドメイン（modem_lpcon.clk_conf.clk_wifipwr_en＝
	 *  DR_REG_MODEM_LPCON_BASE(0x600af000)+0x18のbit0．C5でも同一
	 *  アドレス／ビット位置）が有効化されない．これは
	 *  `MODEM_CLOCK_DOMAIN_WIFI`とは別系統のクロックゲートである．
	 *  C6はJTAGで実機比較して確認済みの根本原因修正（本関数を無効化
	 *  するとclk_wifipwr_en=0に留まり，有効化すると1へ一致．ただし
	 *  この修正単独ではC6の「AP 0個」問題は解消しなかった＝別要因が
	 *  残っていた．docs/wifi-shim-c6.md「実施6」参照）。C5でも
	 *  Direct Bootが同じ経路（esp_perip_clk_init相当を一切呼ばない）を
	 *  取ることは設計上確実なため同じ理由で必要と判断し移植するが，
	 *  clk_wifipwr_enが実際に0のまま残ることをJTAGで実測してはいない
	 *  （【実機確認待ち】）。
	 */
	if (!lpclk_selected) {
		modem_clock_deselect_all_module_lp_clock_source();
		modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE,
											MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0U);

		/*
		 *  bootloader_hardware_init()（bootloader_support/src/<chip>/
		 *  bootloader_<chip>.c．C6ではesp32c6版）が第2段ブートローダ
		 *  内で行う「アナログI2Cマスタクロックを常時有効化」をここで
		 *  代替する．Direct Bootではブートローダ自体が動かないため，
		 *  modem_lpcon.clk_conf.clk_i2c_mst_en（DR_REG_MODEM_LPCON_BASE
		 *  (0x600af000)+0x18のbit2．clk_wifipwr_en=bit0とは別ビット．
		 *  C5でも同一アドレス／ビット位置）が一度も立たない．この
		 *  ビットはregi2c（RFシンセサイザ/PA/LNA/バイアスの内部
		 *  アナログ較正バス）の前提クロックで，PHYブロブは
		 *  esp_rom_regi2c_read/write（ROM関数）をこの前提の下で直接
		 *  呼ぶ．未有効化のままだとregi2c越しの較正（BBPLL/TXRF/BIAS
		 *  等）が無応答または不定値のまま進み，TX/RXとも電波が出ない
		 *  （C6でのsniffer実機観測で確認済み．docs/wifi-shim-c6.md
		 *  参照．C5では同種の症状が起きるかを含めて実機確認待ち）。
		 */
		_regi2c_ctrl_ll_master_enable_clock(true);
		regi2c_ctrl_ll_master_configure_clock();

		lpclk_selected = true;
	}

	wifi_module_enable();

	/*  ★根本原因修正（C6実施6/追記10からの移植）：wifi_module_enable()
	 *  後もMODEM_LPCON_CLK_CONF(0x600af018)のWIFIPWR/COEX/I2C_MSTクロック
	 *  (bit0/1/2)を明示的に有効化する．C6は_regi2c_ctrl_ll_master_enable_
	 *  clock等だけでは実機で0x0のまま（JTAG実測）＝受信不能だったため，
	 *  この明示的な再アサートが必要だった．レジスタアドレス・ビット
	 *  位置がC5でも同一であることを確認済みのためそのまま移植する
	 *  （phy_enable_wrapper側のコメントも参照）。 */
	*(volatile uint32_t *)0x600af018U = 0x7U;
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
	 *  (0x600B1000)+0x4）．C6はRTC_CNTL→LP_AON等へ分割・改称された．
	 *  C5もLP_AONベース（0x600B1000）・STORE1オフセット（+0x4）とも
	 *  C6と同一であることをsoc/esp32c5/register/soc/lp_aon_reg.hで
	 *  確認済み（docs/c5-port-design.md §9）．  */
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
