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
 *  Wi-Fi os_adapter shimの基盤プリミティブ（ASP3用）
 *
 *  Wi-Fiバイナリblobが要求するFreeRTOS流の動的生成（task/queue/
 *  semaphore/mutex/timer/malloc）を，ASP3の静的生成オブジェクトの
 *  プール＋shim実装で提供する（設計はdocs/wifi-shim.md）．
 *  ここはカーネル外（アプリ/ライブラリ層）＝AGENTS.md禁則②の対象外
 *  だが，ヒープ自体は静的配列上に実装する．
 *
 *  時間の単位：blobとのやりとりの「tick」は1ms（_task_ms_to_tick等で
 *  blobへそう申告する）．ASP3のタイムアウトはμs（TMO）へ変換する．
 */

#ifndef ESP_SHIM_H
#define ESP_SHIM_H

#include <kernel.h>
#include <t_syslog.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ブロック指定（blob側の定義と一致：OSI_FUNCS_TIME_BLOCKING）
 */
#define ESP_SHIM_BLOCK_FOREVER  0xffffffffU

/*
 *  tick（1ms）→ASP3タイムアウト（μs）変換
 */
extern TMO esp_shim_tick_to_tmo(uint32_t tick);

/*
 *  ヒープ（静的配列上のfirst-fit・カーネル外）
 */
extern void *esp_shim_malloc(size_t size);
extern void *esp_shim_calloc(size_t n, size_t size);
extern void *esp_shim_realloc(void *ptr, size_t size);
extern void esp_shim_free(void *ptr);
extern size_t esp_shim_heap_free_size(void);

/*
 *  セマフォ（CRE_SEMプール．counting／binary両対応）
 */
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);

/*
 *  ミューテックス（CRE_MTXプール．再帰対応はshimでラップ）
 */
extern void *esp_shim_mutex_create(bool_t recursive);
extern void esp_shim_mutex_delete(void *mtx);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_unlock(void *mtx);

/*
 *  キュー（ヒープ上リングバッファ＋セマフォ．任意item長・ISR送信対応）
 */
extern void *esp_shim_queue_create(uint32_t len, uint32_t item_size);
extern void esp_shim_queue_delete(void *que);
extern int32_t esp_shim_queue_send(void *que, void *item,
								   uint32_t block_time_tick, bool_t to_front);
extern int32_t esp_shim_queue_send_from_isr(void *que, void *item);
extern int32_t esp_shim_queue_recv(void *que, void *item,
								   uint32_t block_time_tick);
extern uint32_t esp_shim_queue_msg_waiting(void *que);
extern void esp_shim_queue_reset(void *que);
/*  E_CTX（CPUロック）文脈で保留リングへ退避した送信をDTQへ流し込む
 *  （esp_shim_exit_critical最外解除・queue_send/recv冒頭から呼ぶ．D-2c）．  */
extern void esp_shim_queue_flush_pending(void);
/*  ★D-2d bond修正：E_CTX文脈で保留した «セマフォgive» を sig_sem で精算する
 *  （キューの pend_ring と同型．exit_critical/機会的flushから呼ぶ）．  */
extern void esp_shim_sem_flush_pending(void);

/*
 *  タスク（CRE_TSKプール．共通エントリ＋関数ポインタ渡し）
 */
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									uint32_t stack_size, void *param,
									uint32_t freertos_prio, void **task_handle);
extern void esp_shim_task_delete(void *task_handle);  /* NULL=自タスク */
extern void esp_shim_task_delay(uint32_t tick);
extern void *esp_shim_task_get_current(void);
extern void esp_shim_task_yield(void);

/*
 *  ets_timer（shim専用タイマタスク＋リスト．コールバックはタスク文脈）
 */
struct ets_timer;   /* blob側定義（rom/ets_sys.h）と互換のopaque扱い */
extern void esp_shim_timer_setfn(void *ptimer, void (*pfunc)(void *),
								 void *parg);
extern void esp_shim_timer_arm_us(void *ptimer, uint32_t us, bool_t repeat);
extern void esp_shim_timer_disarm(void *ptimer);
extern void esp_shim_timer_done(void *ptimer);

/*
 *  クリティカルセクション（loc_cpu／unl_cpuのネスト対応ラッパ）
 */
extern uint32_t esp_shim_int_disable(void);
extern void esp_shim_int_restore(uint32_t state);

/*
 *  ネスト対応クリティカルセクション（FreeRTOS portENTER/EXIT_CRITICAL用．
 *  割込み状態を大域ネストカウンタで退避／復元＝同一muxの入れ子でも
 *  MIEを取りこぼさない．esp_shim.c参照）
 */
extern void esp_shim_enter_critical(void);
extern void esp_shim_exit_critical(void);

/*
 *  割込みディスパッチ（Wi-Fi系のCPU割込み線の動的ハンドラ登録）
 *
 *  cfgでDEF_INHした共通入口（esp_shim_wifi_int_handler）から，
 *  set_isrで登録された関数を呼び出す．
 */
extern void esp_shim_set_isr(int32_t cpu_intno, void *handler, void *arg);
extern void esp_shim_wifi_mac_inthdr(void);
extern void esp_shim_wifi_pwr_inthdr(void);

/*
 *  時刻・乱数
 */
extern int64_t esp_shim_time_us(void);   /* 起動からのμs（SYSTIMER） */
extern uint32_t esp_shim_random(void);

/*
 *  shim全体の初期化（ヒープ・プール管理の初期化．Wi-Fi使用前に呼ぶ）
 */
extern void esp_shim_initialize(void);

/*
 *  coexアダプタの登録（WiFi初期化前に呼ぶ）
 */
extern void esp_shim_coex_adapter_register(void);

/*
 *  ログ（blobの_log_write系の折返し先）
 */
extern void esp_shim_log_write(const char *format, ...);

/*
 *  （D-2b(1) ISRストーム診断）0でOFF（既定）．appが1でBLE ISR（CPU線1）の
 *  ストーム率／level割込みclear残存をRTC STORE4-7へ計装記録する．
 */
extern volatile uint32_t esp_shim_isr_storm_probe;

#ifdef __cplusplus
}
#endif

#endif /* ESP_SHIM_H */
