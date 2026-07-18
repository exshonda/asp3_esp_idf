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
 *  Wi-Fi/BT os_adapter shim ― 共有コア（common_espidf/wifi/esp_shim_core.c）
 *  の «内部» 共有宣言（dedup Tier2．docs/dedup-tier2-plan.md）．
 *
 *  esp_shim.h が «公開 API»（blob/アプリが叩く関数）を宣言するのに対し，
 *  本ヘッダは共有コアとチップ固有 esp_shim.c が跨いで共有する内部オブジェ
 *  クト（プール配列・タイマリスト・ISR表・ロックマクロ）を宣言する．
 *
 *  ★方針：本ヘッダにも共有コア（.c）にも #ifdef TOPPERS_ESP32Cx を持ち込
 *  まない．NimBLE 拡張分は esp_shim_cfg.h が定義する中立マクロ
 *  ESP_SHIM_BT_NIMBLE で分岐する．
 */

#ifndef ESP_SHIM_CORE_H
#define ESP_SHIM_CORE_H

#include <kernel.h>
#include "esp_shim.h"
#include "esp_shim_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  クリティカルセクション（mstatus.MIE の退避・復元）ロックマクロ．
 *  共有コアとチップ固有 esp_shim.c の双方が使う．
 */
#define SHIM_LOCK()		uint32_t shim_lock_ = esp_shim_int_disable()
#define SHIM_UNLOCK()	esp_shim_int_restore(shim_lock_)

/*
 *		タスクプール（共有コアが所有．チップ側 esp_shim_task_create が確保）
 */
typedef struct {
	ID			tskid;
	void		(*fn)(void *);
	void		*arg;
	bool_t		used;
	void		*thread_sem;	/* _wifi_thread_semphr_get用（遅延生成） */
} SHIM_TSK;

extern SHIM_TSK shim_tsk[ESP_SHIM_NUM_TSK];

/*
 *		ets_timer リスト（共有コアが所有．チップ側 timer_arm_us/timer_task が参照）
 */
typedef struct shim_timer {
	struct shim_timer	*next;
	void				*key;			/* blob側のETSTimer* */
	void				(*fn)(void *);
	void				*arg;
	int64_t				deadline_us;	/* 0なら停止中 */
	uint32_t			period_us;		/* 0ならワンショット */
} SHIM_TIMER;

extern SHIM_TIMER *shim_timer_list;		/* 全タイマ（生成順） */
extern SHIM_TIMER *esp_shim_timer_find(void *key, bool_t create);

/*
 *		Wi-Fi割込みディスパッチ表（共有コア set_isr が登録・チップ dispatch が読む）
 */
typedef struct {
	void	(*fn)(void *);
	void	*arg;
} SHIM_ISR;

extern SHIM_ISR shim_isr_tbl[ESP_SHIM_MAX_WIFI_INTNO + 1];

/*
 *		セマフォプール（共有コアが所有．チップ側 sem_give が保留会計に触る）
 */
extern const ID shim_sem_id[ESP_SHIM_NUM_SEM];
extern volatile uint32_t shim_sem_pend[ESP_SHIM_NUM_SEM];
extern volatile uint32_t shim_sem_pend_total;

/*
 *		キュー保留リングの累計カウンタ（共有コアが所有．チップ診断が読む）
 */
extern volatile uint32_t shim_que_pend_total;
extern volatile uint32_t shim_que_pend_used;

/*
 *		ヒープ初期化（共有コアが所有．チップ側 esp_shim_initialize が呼ぶ）
 */
extern void esp_shim_heap_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_SHIM_CORE_H */
