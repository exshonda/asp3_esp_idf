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
#include "esp_rom_sys.h"	/* esp_rom_delay_us（esp_shim_pvt_init用） */
#include "target_syssvc.h"	/* target_fput_log（実施24：ocode_force機械確認の直接出力用） */

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
	/*
	 *  【C5実機で確定・実施02/実施04】INTMTXのMAP値は«CLIC内部線番号»
	 *  そのもの（＝CLIC_LINE(intr_num)＝intr_num+16）を書く．C6/C3のように
	 *  生のintr_numを書くと，CLIC側（下のCTL/ATTR）とカーネルのハンドラ登録は
	 *  line=CLIC_LINE(intr_num)で待っているのに，割込みは未許可の線intr_numへ
	 *  配送され一切届かない（WiFi MAC割込みが来ずesp_wifi_initがpp_create_task
	 *  のポーリング待ちでハングする実バグの原因．docs/c5-bringup.md実施04-05）．
	 */
	sil_wrw_mem((void *)(INTMTX_BASE_ADDR + intr_source * 4U), line);
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
 *
 *  【実施10】esp_event_post の宣言は IDF v6.1 の esp_event.h
 *  （esp_wifi.h 経由で本TUに取り込まれる）が供給する
 *  （esp_err_t esp_event_post(esp_event_base_t, int32_t, const void *,
 *   size_t, TickType_t)）。以前の hal では esp_event.h がこの宣言を
 *  持たず本ファイルでローカル extern していたが，IDF では宣言が衝突
 *  （const void * 対 void *）するため削除し，esp_event.h の宣言を使う。
 *  実体は esp_event_shim.c（C3・独立TU）が提供する。
 */
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

/*
 *  PVT自動dbias調整＋チャージポンプの初期化（【実施21】候補Bの加算移植）
 *
 *  stock ESP-IDF v6.1はC5でCONFIG_ESP_ENABLE_PVT=y（Kconfig既定）であり，
 *  CPUクロックをPLL(160M/240M)へ切り替える度に
 *  esp_hw_support/port/esp32c5/rtc_clk.c → pmu_pvt.c の
 *  pvt_auto_dbias_init()＋charge_pump_init()＋pvt_func_enable(true)＋
 *  charge_pump_enable(true)を実行する（eFuse blk_version>=2が前提．
 *  DUT実測=3で成立）．ASP3 Direct BootはROM設定のクロックのまま起動する
 *  ためこの経路を一切通らず，実施21のJTAG A/B実測で
 *  「stock=PCR_PVT_MONITOR_CONF(0x600960B8) bit0=1・PVTブロック
 *  (0x60019000)設定済み＋動作中／ASP3=クロックゲート・全ゼロ」という
 *  再現差分（各2ブート）を確認した．PVTはHP系dbias（コア電圧バイアス）を
 *  動的補正する機構で，アナログ測定系（トーン自己ループバックのSAR ADC）
 *  への影響が仮説候補のため加算移植する（docs/c5-bringup.md実施21）．
 *
 *  実装はIDF v6.1 pmu_pvt.cの実行順を忠実に再現し，レジスタ値は
 *  同一個体のstock実測ダンプ（実施21）と照合済み：
 *    - PVT_DBIAS_CMD0/1/2の上位ビットはeFuse dbias_vol_gap由来の
 *      チップ固有値を含むが，同一個体のstock実測値(0x13024/0x13005/
 *      0x13427)を転記するため一致する（rtc.hのPVT_CMD0=0x24等の
 *      定数成分も照合済み）．
 *    - DELAY_LIMIT系(154/147/143)・PUMP_BITMAP(1<<22)・
 *      PUMP_CHANNEL_CODE(1<<27)・PVT_TARGET(0xffff)・CLK_DIV(1)も
 *      rtc.h定数とstock実測の両方に一致．
 *
 *  なお実施21のJTAG注入（+2.6s＝txcap探索窓の冒頭で同内容を書込み，
 *  PVT実動作を読み戻しで確認）では30秒観測で症状不変（done=0・
 *  生ADC=0）だった．本移植はstockと同じ「PHY較正開始前に有効」という
 *  タイミングでの因果検証（負なら候補B棄却の確定側の証拠）を兼ねる．
 */
static void
esp_shim_pvt_init(void)
{
	/*  eFuse blk_version（EFUSE_RD_MAC_SYS2_REG 0x600B484C：
	 *  major=bit[12:11]・minor=bit[10:8]，blk_version=major*100+minor）  */
	uint32_t	sys2 = *(volatile uint32_t *)0x600B484CU;
	uint32_t	blk_version = ((sys2 >> 11) & 0x3U) * 100U + ((sys2 >> 8) & 0x7U);
	volatile uint32_t	*pcr_pvt_conf = (volatile uint32_t *)0x600960B8U;	/* PCR_PVT_MONITOR_CONF */
	volatile uint32_t	*pcr_pvt_func = (volatile uint32_t *)0x600960BCU;	/* PCR_PVT_MONITOR_FUNC_CLK_CONF */
	volatile uint32_t	*pmu_hp_reg0 = (volatile uint32_t *)0x600B0028U;	/* PMU_HP_ACTIVE_HP_REGULATOR0 */

	if (blk_version < 2U) {
		return;		/* stockと同条件：PVT未サポートeFuse */
	}

	/*  --- pvt_auto_dbias_init() 相当 ---  */
	*pcr_pvt_conf |= (1U << 1);					/* RST_EN（リセットパルス） */
	*pcr_pvt_conf &= ~(1U << 1);
	*pcr_pvt_conf |= (1U << 0);					/* CLK_EN */
	*pcr_pvt_func |= (1U << 22);				/* FUNC_CLK_EN */
	*(volatile uint32_t *)0x60019064U = 0x7fff8000U;	/* DBIAS_TIMER：EN=0・TARGET=0xffff */
	esp_rom_delay_us(1U);
	*(volatile uint32_t *)0x60019034U = 0x42960400U;	/* DBIAS_CHANNEL_SEL0（monitor cell選択） */
	*(volatile uint32_t *)0x60019038U = 0x80000000U;	/* DBIAS_CHANNEL_SEL1 */
	*(volatile uint32_t *)0x6001903CU = 0x00013e80U;	/* CHANNEL0_SEL（フィルタ閾値） */
	*(volatile uint32_t *)0x60019040U = 0x00013e80U;	/* CHANNEL1_SEL */
	*(volatile uint32_t *)0x60019044U = 0x00010000U;	/* CHANNEL2_SEL */
	*(volatile uint32_t *)0x60019050U = 0x00013024U;	/* DBIAS_CMD0（調整特性＋lp/hp gap） */
	*(volatile uint32_t *)0x60019054U = 0x00013005U;	/* DBIAS_CMD1 */
	*(volatile uint32_t *)0x60019058U = 0x00013427U;	/* DBIAS_CMD2 */
	*pcr_pvt_func = (*pcr_pvt_func & ~0xFU) | 0x1U;		/* FUNC_CLK_DIV_NUM=1 */
	*pcr_pvt_func |= (1U << 20);				/* FUNC_CLK_SEL */
	*(volatile uint32_t *)0x600190D8U = 154U << 2;	/* SITE2_UNIT0_VT1：電圧高判定閾値 */
	*(volatile uint32_t *)0x600190DCU = 147U << 2;	/* SITE2_UNIT1_VT1：電圧低判定閾値 */
	*(volatile uint32_t *)0x600190E0U = 143U << 2;	/* SITE2_UNIT2_VT1：チャージポンプ閾値 */

	/*  --- charge_pump_init() 相当 ---  */
	*(volatile uint32_t *)0x6001902CU = 0x08000000U;	/* PMUP_CHANNEL_CFG：code0=1 */
	*(volatile uint32_t *)0x60019014U = 1U << 22;		/* PMUP_BITMAP_LOW0 */
	/*  PMUP_DRV_CFG：PUMP_DRV0=0（フィールドS=27）＝書込み不要  */

	/*  --- pvt_func_enable(true) 相当 ---  */
	*pmu_hp_reg0 |= (1U << 3);					/* DIG_DBIAS_INIT（較正開始） */
	*pcr_pvt_func |= (1U << 22);
	*pcr_pvt_conf |= (1U << 0);
	*(volatile uint32_t *)0x60019030U |= (1U << 8);		/* CLK_CFG：MONITOR_CLK_PVT_EN */
	*(volatile uint32_t *)0x600190D8U |= (1U << 0);		/* SITE2_UNIT0_VT1：MONITOR_EN */
	esp_rom_delay_us(10U);
	*pmu_hp_reg0 &= ~(1U << 14);				/* DIG_REGULATOR0_DBIAS_SEL=0（dbias制御をPVTへ移譲） */
	*pmu_hp_reg0 &= ~(1U << 3);					/* DIG_DBIAS_INIT解除 */
	*(volatile uint32_t *)0x60019064U |= (1U << 31);	/* DBIAS_TIMER：EN（自動dbias開始） */
	esp_rom_delay_us(50U);

	/*  --- charge_pump_enable(true) 相当 ---  */
	*(volatile uint32_t *)0x60019028U |= (1U << 9);		/* PMUP_DRV_CFG：PUMP_EN */

	/*  pmu_init()はPVT有効化後に1msの安定待ちを置く  */
	esp_rom_delay_us(1000U);
}

/*
 *  PMU HP_ACTIVEバイアス生成器起動＋WIFI電源ドメインのforce→FSM委譲
 *  （【実施22】決定実験Cのbefore-PHY移植）
 *
 *  実施22のJTAG A/B実測（PMU HP_ACTIVEバンク5レジスタの4-way比較，
 *  stock×2・ASP3×2）で，PMU_HP_ACTIVE_DIG_POWER/HP_REGULATOR1は差分なし，
 *  HP_REGULATOR0の差分は既に実施21でPVT/dbias軸として因果棄却済みだが，
 *  残り2件が実施11〜21のどのラウンドでも一度も実測されていなかった
 *  新規差分と判明した：
 *    - PMU_HP_ACTIVE_BIAS（0x600B0018）：stock=0x02000000（XPD_BIAS=1，
 *      HP_ACTIVEモードのアナログバイアス生成器を明示起動）／
 *      ASP3=0x00000000（POR既定のまま＝未起動）。
 *      hal/components/esp_hw_support/port/esp32c5/pmu_param.c:212の
 *      PMU_HP_ACTIVE_ANALOG_CONFIG_DEFAULT()で`.bias.xpd_bias = 1`と
 *      確定．stockのpmu_hp_system_init(PMU_MODE_HP_ACTIVE, ...)が
 *      設定する．ASP3はこの呼出し自体がゼロ行．
 *    - PMU_POWER_PD_HPWIFI_CNTL（0x600B0108）：stock=0x00000000
 *      （FORCE_PU/FORCE_NO_RESET/FORCE_NO_ISO等の強制ビットを全解除し
 *      HW電源シーケンサFSMへ委譲）／ASP3=0x0000001C（POR既定のまま＝
 *      FORCE_PU=1・FORCE_NO_RESET=1・FORCE_NO_ISO=1で静的固定）。
 *      hal/.../pmu_init.c:145-152のpmu_power_domain_force_default()相当が
 *      4電源ドメイン（HPWIFI含む）を順に「force解除→FSM委譲」させる．
 *      ASP3はこの遷移を一度も経験していない＝WIFI電源ドメインが
 *      POR以来ずっと静的forceのまま（動的な電源シーケンス・内部リセット
 *      パルスを一度も経ていない可能性）．
 *
 *  両レジスタともプレーンなR/Wビットフィールドでトリガ/ラッチ機構は
 *  無い（pmu_ll.hのpmu_ll_hp_set_bias_xpd/pmu_ll_hp_set_power_force_*系に
 *  update系呼出しは存在しない）．
 *
 *  実施22はまずASP3がPHY較正の無限リトライループに入った後（mid-hang）
 *  にJTAGで同値を注入し，読み戻しで注入成立を確認した上で30秒観測したが
 *  症状不変だった（組合せ・単独とも）．ただしXPD_BIASはアナログバイアス
 *  基準点そのものであり，較正チェーンが起動時に一度だけ基準点を
 *  ラッチする設計であれば mid-hang注入は原理的に無力（実施21候補Aと
 *  同型の限界，advisorレビュー指摘）．本移植はstockと同じ
 *  「PHY較正開始前」タイミングでの因果検証を兼ねる．
 */
static void
esp_shim_hpactive_bias_init(void)
{
	volatile uint32_t	*pmu_bias = (volatile uint32_t *)0x600B0018U;	/* PMU_HP_ACTIVE_BIAS */
	volatile uint32_t	*pmu_pd_hpwifi = (volatile uint32_t *)0x600B0108U;	/* PMU_POWER_PD_HPWIFI_CNTL */

	*pmu_bias = 0x02000000U;		/* XPD_BIAS=1（bit25）：HP_ACTIVEアナログバイアス生成器起動 */
	*pmu_pd_hpwifi = 0x00000000U;	/* force全解除：WIFI電源ドメインをHW FSM委譲へ */
}

/*
 *  【実施23】残余(1)：PMU_HP_ACTIVE_HP_CK_POWER（BBPLL/BB-I2Cアナログ電源）
 *
 *  実施21/22でHP_ACTIVEバンクの一部（BIAS/HP_REGULATOR0/PD_HPWIFI_CNTL）を
 *  比較・移植済みだったが，同バンクの`HP_CK_POWER`（`0x600B0014`，
 *  `pmu_hp_system_init()`のclk_power.val書込み先，`pmu_hp_clk_power_reg_t`
 *  ＝`xpd_bb_i2c`(bit28)・`xpd_bbpll_i2c`(bit29)・`xpd_bbpll`(bit30)）は
 *  実施11〜22のどのラウンドでも実測されていなかった．
 *
 *  実施23のJTAG A/B実測（同一個体，stock×2・ASP3×2）：
 *    stock=0x70000000（xpd_bb_i2c=1・xpd_bbpll_i2c=1・xpd_bbpll=1，
 *    `pmu_param.c`の`PMU_HP_ACTIVE_POWER_CONFIG_DEFAULT().clk_power`と一致）／
 *    ASP3=0x00000000（POR既定のまま＝BBPLL・BB I2Cのアナログ電源が
 *    一度も明示起動されていない）。
 *
 *  BBPLL（ベースバンドPLL）とBB I2C（ベースバンド内部レジスタバス）は
 *  トーン自己ループバック測定が使うBB内部ADC（`MODEM0+0x81C..0x828`）と
 *  同じアナログ/ミックスシグナル領域に属し，実施11〜22で見つかった
 *  どの差分よりもRF/BBアナログ機序への近さが高い．HP_ACTIVEバンクは
 *  Direct Boot開始以来ずっとこのモードのまま（light-sleep等のモード遷移が
 *  一度もない）ため，このバンクの値は現在も生きて反映されている
 *  （HP_MODEM/HP_SLEEPバンクとは異なり「休止中の設定」ではない）。
 *
 *  プレーンなR/Wビットフィールドでトリガ/ラッチ機構は無い
 *  （`pmu_ll_hp_set_clk_power()`はimm-update呼出しを伴わない）。
 *  最優先候補として単体で先に因果検証する（他の残余レジスタ群は
 *  実施23の次段でまとめて移植・検証）．
 */
static void
esp_shim_hpactive_ckpower_init(void)
{
	volatile uint32_t	*pmu_ck_power = (volatile uint32_t *)0x600B0014U;	/* PMU_HP_ACTIVE_HP_CK_POWER */

	*pmu_ck_power = 0x70000000U;	/* xpd_bb_i2c=1(bit28)/xpd_bbpll_i2c=1(bit29)/xpd_bbpll=1(bit30) */
}

/*
 *  【実施23】残余(2)：pmu_hp_system_init()のHP_ACTIVEバンクに残る
 *  低確度候補群＋pmu_power_domain_force_default()の未移植3ドメインを
 *  まとめて移植する（実施23のJTAG全域ダンプで新規に見つかったが，
 *  いずれも「現在到達しないモード遷移専用の設定」または「実施22で
 *  棄却済みPD_HPWIFIと同型のforce解除（他ドメイン）」であり，
 *  個別に単体因果検証するほどの機序的優先度は無いと判断し1件として
 *  まとめる——残余(1)のCK_POWERのみ単体検証済み）：
 *
 *    - `PMU_HP_ACTIVE_SYSCLK`（`0x600B0024`）：stock=0x08000000
 *      （icg_sysclk_en=1のみ）／ASP3=0x00000000。システムクロックICG
 *      バイパスの有効化ビットで，実施13のicg_modemと同系統だが対象が
 *      sysclk全体のICGである点が異なる。
 *    - `PMU_HP_ACTIVE_BACKUP`/`BACKUP_CLK`（`0x600B001C`/`0x600B0020`）：
 *      HP_SLEEP→ACTIVE・HP_MODEM→ACTIVEのregdma retention設定。
 *      ASP3はHP_SLEEP/HP_MODEMへ一度も遷移しない（light-sleep等の
 *      電源管理を一切呼ばない）ため，この設定が現在参照される経路は
 *      無いと考えられる（機序的に最も疑わしくない）。
 *    - `PMU_POWER_PD_TOP_CNTL`/`PD_HPAON_CNTL`/`PD_HPCPU_CNTL`
 *      （`0xf8`/`0xfc`/`0x100`）：`pmu_power_domain_force_default()`が
 *      force解除する4ドメイン（実施22はWIFIのみ移植・棄却）のうち
 *      残り3ドメイン。stock=0x00000000（force全解除）／ASP3=0x0000001c
 *      （POR既定のまま静的force）。
 *    - `PMU_POWER_PD_LPPERI_CNTL`（`0x10c`）：同関数のLP側force解除。
 *      stock=0x00000000／ASP3=0x0000001c。
 *
 *  いずれもプレーンR/Wでトリガ/ラッチ機構は無い。値は同一個体の
 *  stock実測（実施23）をそのまま転記する。
 */
static void
esp_shim_hpactive_residual2_init(void)
{
	volatile uint32_t	*pmu_sysclk    = (volatile uint32_t *)0x600B0024U;	/* PMU_HP_ACTIVE_SYSCLK */
	volatile uint32_t	*pmu_backup    = (volatile uint32_t *)0x600B001CU;	/* PMU_HP_ACTIVE_BACKUP */
	volatile uint32_t	*pmu_backupclk = (volatile uint32_t *)0x600B0020U;	/* PMU_HP_ACTIVE_BACKUP_CLK */
#if 0	/* 【実施24】UART直接出力（logtask非依存，target_fput_log経由の
	 * wifi_diag_live）でこのブロックをA/B検証した：書込み成立を機械確認
	 * （pd_top/hpaon/hpcpu/lpperi=0x00000000，stockと一致）した上で
	 * ≥5独立ブート（RTSクリーンリセット×2＋同一セッション内WDTループ再現×3）
	 * とも症状不変（raw_adc=0・done16=0）・WDTリブート周期（約3.5秒）も
	 * 不変——UART経由では「悪化」は一切観測されなかった。
	 * しかし本ラウンドはJTAGが使用不能な環境だったため，実施23が実際に
	 * 検出した問題（**JTAG単発halt捕捉法がPHYハングループへ到達できず
	 * dispatcher_1近傍に着地する**という，JTAG介入時にのみ現れる現象）を
	 * 直接再検証することはできていない。advisorレビュー指摘のとおり，
	 * UART計測とJTAG halt捕捉は異なる観測条件であり，UART側が無事だからと
	 * いってJTAG側の問題が解消したとは言えない。次回JTAG環境での作業を
	 * 妨げない（実施23の主要な調査ツールである単発halt捕捉法を壊さない）
	 * ことを優先し，**因果棄却は確定したが，安全側に倒してrevertを維持する**。
	 * 【実施23 bisection】PD_TOP/HPAON/HPCPU/LPPERI force解除を有効にした
	 * ビルドでJTAG単発halt(+9.9/12/13s)が11連続でPHYハングループ
	 * （0x42026000-0x4202a000）に到達できず，毎回dispatcher_1近傍
	 * （0x420217xx-0x420218xx）に着地する新規の停滞パターンを確認した
	 * （それ以前のCK_POWER単体ビルドでは同方式で確実に到達できていた）。
	 * 「悪化したら即revert」の方針に従い，最も疑わしい（CPU/TOP電源
	 * ドメインという影響範囲の大きさから）本ブロックを一時的に無効化し，
	 * 単独のビセクションでこの停滞の原因かどうかを切り分ける
	 * （docs/c5-bringup.md 実施23参照）。因果確認までは残置し，コードは
	 * ツリーからは削除しない。 */
	volatile uint32_t	*pmu_pd_top    = (volatile uint32_t *)0x600B00F8U;	/* PMU_POWER_PD_TOP_CNTL */
	volatile uint32_t	*pmu_pd_hpaon  = (volatile uint32_t *)0x600B00FCU;	/* PMU_POWER_PD_HPAON_CNTL */
	volatile uint32_t	*pmu_pd_hpcpu  = (volatile uint32_t *)0x600B0100U;	/* PMU_POWER_PD_HPCPU_CNTL */
	volatile uint32_t	*pmu_pd_lpperi = (volatile uint32_t *)0x600B010CU;	/* PMU_POWER_PD_LPPERI_CNTL */
#endif

	*pmu_sysclk    = 0x08000000U;	/* icg_sysclk_en=1(bit27) */
	*pmu_backup    = 0x010200a0U;	/* stock実測値（実施23）をそのまま転記 */
	*pmu_backupclk = 0xffffffffU;	/* backup_clk：全ビットicgバイパス（stock/デフォルトと同一） */
#if 0	/* 実施24：上のブロックと合わせて無効化のまま維持（詳細は上記コメント） */
	*pmu_pd_top    = 0x00000000U;	/* force全解除 */
	*pmu_pd_hpaon  = 0x00000000U;	/* force全解除 */
	*pmu_pd_hpcpu  = 0x00000000U;	/* force全解除 */
	*pmu_pd_lpperi = 0x00000000U;	/* force全解除 */
#endif
}

/*
 *  【実施23】pmu_lp_system_init()の未移植分（LP_ACTIVE/LP_SLEEPバンク）
 *
 *  実施23のJTAG全域ダンプで新規に見つかった差分：
 *    - `PMU_HP_SLEEP_LP_REGULATOR0`（`0x600B009C`＝`lp_sys[LP_ACTIVE]
 *      .regulator0`，LPドメイン電圧レギュレータのdbias/xpd）：
 *      stock=0xe8400000／ASP3=0xc6600000。LP_ACTIVEは（ASP3がlight-sleep等の
 *      電源管理を一切呼ばないため）Direct Boot開始以来ずっと「現在有効な」
 *      バンクであり，実施21のHP側dbias（PVT）軸と同型だが対象がLPドメイン
 *      という点で新規（実施21候補Bはこの軸を棄却済みだがLP側は未検証）。
 *    - `PMU_LP_SLEEP_LP_REGULATOR0`/`XTAL`/`LP_CK_POWER`/`BIAS`
 *      （`0x600B00B4`/`0xBC`/`0xC4`/`0xC8`）：LP_SLEEPバンク（ASP3が
 *      一度も遷移しないモード専用の設定）。HP_MODEM/HP_SLEEPバンクと
 *      同種の理由で現在は不活性と考えられるが，`pmu_lp_system_init()`の
 *      パリティとして移植する。
 *
 *  ★注意：実施23のPD_TOP/HPAON/HPCPU force解除でJTAG単発halt法が
 *  ブートリーチャビリティを悪化させた前例があるため，本関数もCPU/TOP等の
 *  広域ドメイン制御を含まない（LPドメインのレギュレータ/クロック電源の
 *  プレーンR/Wのみ）ことを確認した上で適用する。
 */
static void
esp_shim_lpsystem_init(void)
{
	volatile uint32_t	*lp_active_reg0 = (volatile uint32_t *)0x600B009CU;	/* LP_ACTIVE.REGULATOR0 */
	volatile uint32_t	*lp_sleep_reg0  = (volatile uint32_t *)0x600B00B4U;	/* LP_SLEEP.REGULATOR0 */
	volatile uint32_t	*lp_sleep_xtal  = (volatile uint32_t *)0x600B00BCU;	/* LP_SLEEP.XTAL */
	volatile uint32_t	*lp_sleep_ck    = (volatile uint32_t *)0x600B00C4U;	/* LP_SLEEP.CK_POWER */
	volatile uint32_t	*lp_sleep_bias  = (volatile uint32_t *)0x600B00C8U;	/* LP_SLEEP.BIAS */

	*lp_active_reg0 = 0xe8400000U;	/* stock実測値（実施23）をそのまま転記 */
	*lp_sleep_reg0  = 0x60400000U;
	*lp_sleep_xtal  = 0x00000000U;
	*lp_sleep_ck    = 0x00000000U;
	*lp_sleep_bias  = 0xc0000000U;
}

/*
 *  【実施24】手動regi2cリプレイ（実施14で確立したI2C_ANA_MST直叩き手法）の
 *  shim関数化。hal/esp_rom_hp_regi2c_esp32c5.c（regi2c ROMパッチ，読取り専用で
 *  参照）と同一のプロトコルをASP3側で独立実装し，リンク構造を変えずに
 *  regi2cトランザクションを発行する。I2C_ULP(0x61=ULP_CAL)ブロック専用
 *  （regi2c_enable_block()のULP_CAL分岐のみを再現。他ブロックは未対応）。
 *
 *  プロトコル（I2C_ANA_MST base=0x600AF800，実施14/23で確認済み）：
 *    ANA_CONF2(+0x20) bit10 = ULP_CAL_MST_SEL → i2c_sel（0/1のどちらの
 *    I2C{0,1}_CTRL_REGを使うか）を決める。ANA_CONF1(+0x1C)に
 *    RD_MASK（~BIT(8)&0xFFFFFF）を書く。I2C{0,1}_CTRL_REG(+0x0/+0x4)：
 *    bit25=busy，bits15:8=reg_addr，bits7:0=block/slave_id，
 *    bit24=WR_CNTL（1=write），bits23:16=data。
 */
#define ESP_SHIM_I2C_ANA_MST_BASE	0x600AF800U
#define ESP_SHIM_I2C_ANA_MST_I2C0_CTRL	(ESP_SHIM_I2C_ANA_MST_BASE + 0x00U)
#define ESP_SHIM_I2C_ANA_MST_I2C1_CTRL	(ESP_SHIM_I2C_ANA_MST_BASE + 0x04U)
#define ESP_SHIM_I2C_ANA_MST_ANA_CONF1	(ESP_SHIM_I2C_ANA_MST_BASE + 0x1CU)
#define ESP_SHIM_I2C_ANA_MST_ANA_CONF2	(ESP_SHIM_I2C_ANA_MST_BASE + 0x20U)
#define ESP_SHIM_REGI2C_BUSY_BIT	(1UL << 25)
#define ESP_SHIM_REGI2C_ULP_CAL_MST_SEL	(1UL << 10)

static uint32_t
esp_shim_regi2c_ctrl_addr(uint8_t block)
{
	uint32_t	conf2 = *(volatile uint32_t *)ESP_SHIM_I2C_ANA_MST_ANA_CONF2;
	uint32_t	mst_sel_bit = (block == 0x61U) ? ESP_SHIM_REGI2C_ULP_CAL_MST_SEL : 0UL;
	uint32_t	i2c_sel_raw = (conf2 & mst_sel_bit) ? 1U : 0U;

	/*  RD_MASK書込み（regi2c_enable_block()相当，I2C_ULPのみ対応）  */
	*(volatile uint32_t *)ESP_SHIM_I2C_ANA_MST_ANA_CONF1 =
		(~(1UL << 8)) & 0x00FFFFFFU;

	return((i2c_sel_raw != 0U) ? ESP_SHIM_I2C_ANA_MST_I2C0_CTRL
								: ESP_SHIM_I2C_ANA_MST_I2C1_CTRL);
}

static uint8_t
esp_shim_regi2c_read(uint8_t block, uint8_t reg_add)
{
	uint32_t	ctrl = esp_shim_regi2c_ctrl_addr(block);
	uint32_t	temp;

	while ((*(volatile uint32_t *)ctrl & ESP_SHIM_REGI2C_BUSY_BIT) != 0U) {
		;
	}
	temp = ((uint32_t)block & 0xFFU) | (((uint32_t)reg_add & 0xFFU) << 8);
	*(volatile uint32_t *)ctrl = temp;
	while ((*(volatile uint32_t *)ctrl & ESP_SHIM_REGI2C_BUSY_BIT) != 0U) {
		;
	}
	return((uint8_t)((*(volatile uint32_t *)ctrl >> 16) & 0xFFU));
}

static void
esp_shim_regi2c_write_mask(uint8_t block, uint8_t reg_add, uint8_t msb,
							 uint8_t lsb, uint8_t data)
{
	uint32_t	ctrl = esp_shim_regi2c_ctrl_addr(block);
	uint32_t	temp;

	while ((*(volatile uint32_t *)ctrl & ESP_SHIM_REGI2C_BUSY_BIT) != 0U) {
		;
	}
	temp = ((uint32_t)block & 0xFFU) | (((uint32_t)reg_add & 0xFFU) << 8);
	*(volatile uint32_t *)ctrl = temp;
	while ((*(volatile uint32_t *)ctrl & ESP_SHIM_REGI2C_BUSY_BIT) != 0U) {
		;
	}
	temp = (*(volatile uint32_t *)ctrl >> 16) & 0xFFU;

	temp &= (uint32_t)((~(0xFFFFFFFFUL << lsb)) | (0xFFFFFFFFUL << ((uint32_t)msb + 1U)));
	temp |= (((uint32_t)data & (~(0xFFFFFFFFUL << ((uint32_t)msb - lsb + 1U)))) << lsb);

	temp = ((uint32_t)block & 0xFFU) | (((uint32_t)reg_add & 0xFFU) << 8)
			| (1UL << 24) | ((temp & 0xFFU) << 16);
	*(volatile uint32_t *)ctrl = temp;
	while ((*(volatile uint32_t *)ctrl & ESP_SHIM_REGI2C_BUSY_BIT) != 0U) {
		;
	}
}

/*
 *  【実施24】esp_ocode_calib_init()（bandgap o-codeトリム）のbefore-PHY移植。
 *
 *  実施23で差分自体を確定済み（stock=eFuse値でIR_FORCE_CODE=1を強制／
 *  ASP3=pmu_init()自体を呼ばないためIR_FORCE_CODE=0のままHW自動較正）。
 *  実施23はROM regi2cパッチ（hal/esp_rom_hp_regi2c_esp32c5.c）をASP3の
 *  CMakeへ追加リンクする構造変更のコストを理由に因果検証を見送っていたが，
 *  本ラウンドでは上記esp_shim_regi2c_*関数（実施14の手動リプレイの
 *  shim化）でリンク構造を変えずに同じレジスタ操作を行う。
 *
 *  stockの`set_ocode_by_efuse(1)`（hal/esp_hw_support/port/esp32c5/
 *  ocode_init.c，読取り専用で参照）を忠実に再現：
 *    1. eFuse RD_SYS_PART1_DATA4（0x600B486C）bit[16:9]からocode値を読む。
 *    2. block=I2C_ULP(0x61) reg=6(EXT_CODE，bits7:0)にocode値を書く。
 *    3. block=I2C_ULP reg=5(IR_FORCE_CODE，bit6)に1を書く。
 *  適用条件（stockと同じ，esp_ocode_calib_init()より）：
 *  chip_revision==1&&blk_version>=1，または chip_revision>=100&&
 *  blk_version>=2（本DUTはchip_revision=100・blk_version=3で成立，
 *  実施21/23のeFuse実測で確認済み）。不成立ならstockはcalibrate_ocode()
 *  （HW自己較正）を使うため何もしない（ASP3は元々この経路＝POR既定の
 *  ままであり，この分岐は現状維持が正しい）。
 *
 *  書込み成立の確認はregi2c読み戻しを直接ポーリング出力
 *  （`target_fput_log`）でUART越しに記録する（TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
 *  ガード）。★実施24の実測でsyslog()経由の周期出力（wifi_diag_cyclic_handler，
 *  wifi_scan.c）はPHY較正の無限リトライループに入ると出力が完全に止まる
 *  （logtaskがスケジューリングされなくなると推定）ことが判明したため，
 *  ここでもtarget_fput_log直呼び（カーネルバナー同様，タスク
 *  スケジューリングに非依存で確実に届く）を使う。本関数はesp_wifi_init()の
 *  ごく早い段階（PHYハングループへ到達する前）で1回だけ実行されるため
 *  タイミング的には元々syslogでも間に合っていたはずだが，念のため
 *  直接出力に統一する。
 */
static void
esp_shim_diag_fput_str(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s);
		s++;
	}
}

static void
esp_shim_diag_fput_hex8(uint8_t v)
{
	static const char	hexdig[] = "0123456789abcdef";

	target_fput_log(hexdig[(v >> 4) & 0xFU]);
	target_fput_log(hexdig[v & 0xFU]);
}

static void
esp_shim_diag_fput_dec(uint32_t v)
{
	char	buf[10];
	int_t	i = 0;

	if (v == 0U) {
		target_fput_log('0');
		return;
	}
	while (v != 0U && i < 10) {
		buf[i++] = (char)('0' + (v % 10U));
		v /= 10U;
	}
	while (i > 0) {
		target_fput_log(buf[--i]);
	}
}

static void
esp_shim_ocode_force_init(void)
{
	uint32_t	sys2 = *(volatile uint32_t *)0x600B484CU;	/* EFUSE_RD_MAC_SYS2_REG */
	uint32_t	wafer_major = (sys2 >> 4) & 0x3U;
	uint32_t	wafer_minor = sys2 & 0xFU;
	uint32_t	chip_revision = wafer_major * 100U + wafer_minor;
	uint32_t	blk_major = (sys2 >> 11) & 0x3U;
	uint32_t	blk_minor = (sys2 >> 8) & 0x7U;
	uint32_t	blk_version = blk_major * 100U + blk_minor;
	uint32_t	data4;
	uint32_t	ocode;
	uint8_t		rb_ext_code;
	uint8_t		rb_force;

	if (!((chip_revision == 1U && blk_version >= 1U) ||
		  (chip_revision >= 100U && blk_version >= 2U))) {
#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
		esp_shim_diag_fput_str("\r\nocode_force: skip chip_rev=");
		esp_shim_diag_fput_dec(chip_revision);
		esp_shim_diag_fput_str(" blk_ver=");
		esp_shim_diag_fput_dec(blk_version);
		esp_shim_diag_fput_str(" (calib_ocode branch)\r\n");
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */
		return;
	}

	data4 = *(volatile uint32_t *)0x600B486CU;	/* EFUSE_RD_SYS_PART1_DATA4_REG */
	ocode = (data4 >> 9) & 0xFFU;

	esp_shim_regi2c_write_mask(0x61U, 6U, 7U, 0U, (uint8_t)ocode);	/* I2C_ULP_EXT_CODE */
	esp_shim_regi2c_write_mask(0x61U, 5U, 6U, 6U, 1U);				/* I2C_ULP_IR_FORCE_CODE */

	rb_ext_code = esp_shim_regi2c_read(0x61U, 6U);
	rb_force = esp_shim_regi2c_read(0x61U, 5U);

#ifdef TOPPERS_ESP32C5_WIFI_REGI2C_TRACE
	esp_shim_diag_fput_str("\r\nocode_force: chip_rev=");
	esp_shim_diag_fput_dec(chip_revision);
	esp_shim_diag_fput_str(" blk_ver=");
	esp_shim_diag_fput_dec(blk_version);
	esp_shim_diag_fput_str(" ocode=0x");
	esp_shim_diag_fput_hex8((uint8_t)ocode);
	esp_shim_diag_fput_str(" readback ext_code_reg=0x");
	esp_shim_diag_fput_hex8(rb_ext_code);
	esp_shim_diag_fput_str(" force_reg=0x");
	esp_shim_diag_fput_hex8(rb_force);
	esp_shim_diag_fput_str(" force_bit=");
	target_fput_log((char)('0' + ((rb_force >> 6) & 1U)));
	esp_shim_diag_fput_str("\r\n");
#endif /* TOPPERS_ESP32C5_WIFI_REGI2C_TRACE */
}

static void
wifi_clock_enable_wrapper(void)
{
	static bool_t	lpclk_selected = false;

	/*  【実施13】C6ではesp_shim_modem_icg_init()はJTAG実測で冗長と判明
	 *  （clk_conf_power_st=0x66660000は既にnative一致＝modem_clockが
	 *  設定）していたため，C5でも暫定的に踏襲して無効化し，
	 *  「C5は未検証（【実機確認待ち】）」と明記していた．**C5実機の
	 *  JTAG実測でこの踏襲が誤りと判明したため有効化する**．
	 *
	 *  C5実測：PMU hp_sys[HP_ACTIVE].icg_modem.code（0x600B000C
	 *  bit31:30）がDirect Bootでは0のまま残る．一方，
	 *  MODEM_SYSCON_CLK_CONF_POWER_ST（0x600A9C0C）のCLK_WIFI_ST_MAP
	 *  ＝0x6＝BIT(1)|BIT(2)であり，ICGコード0はこのマップに含まれない．
	 *  そのためCLK_CONF1（0x600A9C14）のCLK_WIFIBB_*_EN群が全て1でも
	 *  **WIFIBBクロックはICGでゲートされたまま**になり，BBレジスタ
	 *  ブロック（MODEM0＝0x600A0000）への書込みが一切効かない．
	 *  結果，PHY較正のphy_iq_est_enable_new()が起動ビット（BB+0x450
	 *  bit1）を立てられず，完了ビット（BB+0x47C bit16）が永久に
	 *  立たない＝無限リトライループ（実施12で発見したハング）．
	 *
	 *  A/B/A/B反証実験（FORCE_ON=0・ST_MAP不変のまま，icg_modem.code
	 *  のみ0↔2をトグルし，都度BBレジスタへの書込み成否を確認）で
	 *  因果を確認済み：code=0→書込み無視，code=2→書込み成立．
	 *  なお適用にはPMUのimmediate updateパルス2本
	 *  （PMU_IMM_MODEM_ICG_REG=0x600B00DC bit31＝update_dig_icg_modem_en，
	 *  PMU_IMM_SLEEP_SYSCLK_REG=0x600B00D0 bit28＝update_dig_icg_switch）
	 *  の**両方**が必要（codeを書くだけでは反映されない．片方だけ
	 *  パルスした際は書込みが効かないことも実測で確認）．
	 *  詳細は docs/c5-bringup.md 実施13．
	 */
	esp_shim_modem_icg_init();

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
		/*  【実施21】候補B加算移植：stockがクロック切替時（＝PHY較正より
		 *  前）に行うPVT自動dbias＋チャージポンプ有効化を，Wi-Fi初期化の
		 *  一度きりのこの時点（regi2c有効化・phy_enableより前）で代替する  */
		esp_shim_pvt_init();

		/*  【実施22】決定実験Cのbefore-PHY移植：PMU HP_ACTIVEバイアス
		 *  生成器起動＋WIFI電源ドメインのforce→FSM委譲を同じ時点で代替する  */
		esp_shim_hpactive_bias_init();

		/*  【実施23】残余(1)：BBPLL/BB-I2Cアナログ電源起動を同じ時点で代替する  */
		esp_shim_hpactive_ckpower_init();

		/*  【実施23】残余(2)：SYSCLK ICG・retention・PD_TOP/HPAON/HPCPU/LPPERI
		 *  force解除を同じ時点で代替する  */
		esp_shim_hpactive_residual2_init();

		/*  【実施23】pmu_lp_system_init()の未移植分（LP_ACTIVE/LP_SLEEPバンク）
		 *  を同じ時点で代替する  */
		esp_shim_lpsystem_init();

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

		/*  【実施24】esp_ocode_calib_init()のbefore-PHY移植：regi2cマスタ
		 *  クロックが実際に有効化された直後（本関数のこの時点で初めて
		 *  I2C_ANA_MSTトランザクションが物理的に成立する）に置く。  */
		esp_shim_ocode_force_init();

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
 *  【実施10】ESP-IDF v6.1（os_adapter 0x09）で wifi_osi_funcs_t に
 *  追加された sleep-retention / HE(AX) 系フィールドの no-op スタブ。
 *  scan 経路（PM/sleep 無効）では呼ばれない想定のため，まず無害な
 *  スタブ（0返し／何もしない）で ABI を満たす。scan が要求した場合の
 *  実装は後続（docs/c5-bringup.md）。v8 では未設定＝NULL のままだった
 *  C6/C5-gated の regdma/sleep_retention_find もここで安全側に埋める。
 */
static void regdma_link_set_write_wait_content_wrapper(void *link, uint32_t value, uint32_t mask)
{
	(void)link; (void)value; (void)mask;
}
static void *sleep_retention_find_link_by_id_wrapper(int id)
{
	(void)id;
	return NULL;
}
static int32_t wifi_bb_sleep_retention_attach_wrapper(void)
{
	return 0;
}
static int32_t wifi_bb_sleep_retention_detach_wrapper(void)
{
	return 0;
}
static int32_t wifi_mac_sleep_retention_attach_wrapper(void)
{
	return 0;
}
static int32_t wifi_mac_sleep_retention_detach_wrapper(void)
{
	return 0;
}
/*
 *  【実施12】_wifi_pm_sleep_lock_acquire/_wifi_pm_sleep_lock_release：
 *  v9のwifi_os_adapter.hでは，v8で削除した_wifi_apb80m_request/releaseと
 *  同じ構造体スロット（_dport_access_stall_other_cpu_end_wrapの直後，
 *  _phy_disableの直前）に位置する別フィールドとして存在する（削除では
 *  なく置換）。実施10のv9移行時，apb80m側の削除は正しく行ったが，この
 *  置換後継フィールドの追加が漏れていたためNULL関数ポインタのまま
 *  だった。本リポジトリはESP-IDFのPM（動的クロック/スリープ）サブ
 *  システムを実装しないため，他のPM/sleep-retention系フィールドと
 *  同じ方針でno-opスタブとする。
 */
static void wifi_pm_sleep_lock_acquire_wrapper(void)
{
}
static void wifi_pm_sleep_lock_release_wrapper(void)
{
}
#if CONFIG_SOC_WIFI_HE_SUPPORT
static bool wifi_disable_ac_ax_wrapper(void)
{
	return false;
}
#endif

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
	/*
	 *  _wifi_apb80m_request/_wifi_apb80m_release：IDF v6.1のwifi_os_adapter.h
	 *  はversion 0x08（v6.1-dev）とversion 0x09（v6.1-beta1，本ビルドが
	 *  実際に使うblob世代）でフィールド構成が異なり，このAPB80M要求/解放
	 *  ペアはv8由来でv9では削除されている（wifi_osi_funcs_tにメンバが
	 *  存在しない）．v9移行（実施10）時の見落としと判明．wrapper関数
	 *  自体（wifi_apb80m_request/release_wrapper）は削除せず残置
	 *  （将来的な参照用．未使用関数警告のみで実害無し）。
	 *  同じ構造体スロットの後継フィールド（_wifi_pm_sleep_lock_acquire/
	 *  _wifi_pm_sleep_lock_release）は実施12で追加（下記）。
	 */
	._wifi_pm_sleep_lock_acquire = wifi_pm_sleep_lock_acquire_wrapper,
	._wifi_pm_sleep_lock_release = wifi_pm_sleep_lock_release_wrapper,
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
#if CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32C5 || CONFIG_IDF_TARGET_ESP32C61 || CONFIG_IDF_TARGET_ESP32S31
	/*  v8 でも定義済みだが未設定(NULL)だった C5/C6-gated 2フィールド  */
	._regdma_link_set_write_wait_content = regdma_link_set_write_wait_content_wrapper,
	._sleep_retention_find_link_by_id = sleep_retention_find_link_by_id_wrapper,
	/*  【実施10】v9 で追加された sleep-retention 4フィールド  */
	._wifi_bb_sleep_retention_attach = wifi_bb_sleep_retention_attach_wrapper,
	._wifi_bb_sleep_retention_detach = wifi_bb_sleep_retention_detach_wrapper,
	._wifi_mac_sleep_retention_attach = wifi_mac_sleep_retention_attach_wrapper,
	._wifi_mac_sleep_retention_detach = wifi_mac_sleep_retention_detach_wrapper,
#endif
#if CONFIG_SOC_WIFI_HE_SUPPORT
	/*  【実施10】v9 で追加された HE(AX) 無効化フィールド  */
	._wifi_disable_ac_ax = wifi_disable_ac_ax_wrapper,
#endif
	._magic = ESP_WIFI_OS_ADAPTER_MAGIC,
};
