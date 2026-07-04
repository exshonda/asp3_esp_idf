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
 *  lwIP sys_arch実装（ASP3用．NO_SYS=0）
 *
 *  sys_sem_t／sys_mbox_tはcfg（net.cfg）で静的生成したASP3セマフォ／
 *  データキューのプールから割り当てる（Wi-Fi shim＝wifi/esp_shim.cの
 *  静的プール方式を踏襲．設計はdocs/tcpip-integration.md）．
 *  ただしメールボックスはWi-Fi shimのキューと異なりヒープ確保での
 *  ボックス化が不要：lwIPのsys_mbox_*は常に「ポインタ1個」しか運ば
 *  ないため，ASP3のCRE_DTQ（item＝intptr_t）にそのまま1:1で対応する．
 *
 *  sys_thread_new()はlwIPの生涯で一度だけ（tcpip_init()内部から
 *  tcpip_thread生成のために）呼ばれる前提．cfgで休止状態生成した
 *  NET_TSKを起動するだけの単発実装とする（複数スレッド生成が必要に
 *  なった場合は別途プール化が要る）．
 */
#include <kernel.h>
#include <t_syslog.h>
#include "kernel_cfg.h"

#include "lwip/sys.h"
#include "lwip/err.h"

#include "net_cfg.h"
#include "esp_shim.h"		/* esp_shim_int_disable/restore・esp_shim_time_us */

#define NET_LOCK()		uint32_t net_lock_ = esp_shim_int_disable()
#define NET_UNLOCK()	esp_shim_int_restore(net_lock_)

/*
 *  ミリ秒タイムアウト→ASP3 TMO（μs．0＝永久待ち）変換
 */
static TMO
ms_to_tmo(u32_t timeout_ms)
{
	if (timeout_ms == 0U) {
		return(TMO_FEVR);
	}
	return((TMO)((uint64_t) timeout_ms * 1000U));
}

/*
 *  ポート初期化（lwip_init()の先頭で一度だけ呼ばれる．cfgの静的
 *  プールはBSS初期化済みのため特にすることはない）
 */
void
sys_init(void)
{
}

/*
 *  時刻（起動からのms．NO_SYS=0でも常時必要）
 */
u32_t
sys_now(void)
{
	return((u32_t) (esp_shim_time_us() / 1000));
}

/*
 *  SYS_ARCH_PROTECT/UNPROTECT（pbuf/memp等の保護．net_task
 *  （tcpip_thread）と他タスク／Wi-Fi rxコールバックとの排他に使う）
 */
sys_prot_t
sys_arch_protect(void)
{
	return((sys_prot_t) esp_shim_int_disable());
}

void
sys_arch_unprotect(sys_prot_t pval)
{
	esp_shim_int_restore((uint32_t) pval);
}

/*
 *  ------------------------------------------------------------------
 *  セマフォプール（LWIP_COMPAT_MUTEX=1のためミューテックスもこれを
 *  介する．sys_mutex_new(mutex)→sys_sem_new(mutex,1)等，lwip/sys.hが
 *  マクロで読み替える）
 *  ------------------------------------------------------------------
 */
static const ID net_sem_id[SYS_ARCH_NUM_SEM] = {
	NET_SEM1, NET_SEM2, NET_SEM3, NET_SEM4,
	NET_SEM5, NET_SEM6, NET_SEM7, NET_SEM8,
};
static bool_t net_sem_used[SYS_ARCH_NUM_SEM];

err_t
sys_sem_new(sys_sem_t *sem, u8_t count)
{
	uint_t	i;
	ID		semid = 0;

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_SEM; i++) {
		if (!net_sem_used[i]) {
			net_sem_used[i] = true;
			semid = net_sem_id[i];
			break;
		}
	}
	NET_UNLOCK();

	if (semid == 0) {
		syslog(LOG_ERROR, "net: sem pool exhausted");
		*sem = SYS_SEM_NULL;
		return(ERR_MEM);
	}

	/*  再利用時のカウントクリア（cfg初期値も0）  */
	while (pol_sem(semid) == E_OK) {
		;
	}
	while (count-- > 0U) {
		(void) sig_sem(semid);
	}
	*sem = (sys_sem_t) semid;
	return(ERR_OK);
}

void
sys_sem_free(sys_sem_t *sem)
{
	ID		semid = (ID) *sem;
	uint_t	i;

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_SEM; i++) {
		if (net_sem_id[i] == semid) {
			net_sem_used[i] = false;
			break;
		}
	}
	NET_UNLOCK();
	*sem = SYS_SEM_NULL;
}

void
sys_sem_signal(sys_sem_t *sem)
{
	(void) sig_sem((ID) *sem);
}

u32_t
sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
	ER	er;

	er = twai_sem((ID) *sem, ms_to_tmo(timeout_ms));
	return((er == E_OK) ? 1U : SYS_ARCH_TIMEOUT);
}

int
sys_sem_valid(sys_sem_t *sem)
{
	return(*sem != SYS_SEM_NULL);
}

void
sys_sem_set_invalid(sys_sem_t *sem)
{
	*sem = SYS_SEM_NULL;
}

/*
 *  ------------------------------------------------------------------
 *  メールボックスプール（ASP3 CRE_DTQに1:1対応．メッセージはポインタ
 *  1個＝ボックス化不要）
 *  ------------------------------------------------------------------
 */
static const ID net_mbox_id[SYS_ARCH_NUM_MBOX] = {
	NET_MBOX1, NET_MBOX2, NET_MBOX3, NET_MBOX4,  NET_MBOX5,
	NET_MBOX6, NET_MBOX7, NET_MBOX8, NET_MBOX9,  NET_MBOX10,
};
static bool_t net_mbox_used[SYS_ARCH_NUM_MBOX];

err_t
sys_mbox_new(sys_mbox_t *mbox, int size)
{
	uint_t	i;
	ID		dtqid = 0;

	(void) size;	/* 深さは固定（SYS_ARCH_MBOX_DEPTH．net_cfg.h参照） */

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_MBOX; i++) {
		if (!net_mbox_used[i]) {
			net_mbox_used[i] = true;
			dtqid = net_mbox_id[i];
			break;
		}
	}
	NET_UNLOCK();

	if (dtqid == 0) {
		syslog(LOG_ERROR, "net: mbox pool exhausted");
		*mbox = SYS_MBOX_NULL;
		return(ERR_MEM);
	}
	(void) ini_dtq(dtqid);		/* 再利用時の残留メッセージクリア */
	*mbox = (sys_mbox_t) dtqid;
	return(ERR_OK);
}

void
sys_mbox_free(sys_mbox_t *mbox)
{
	ID		dtqid = (ID) *mbox;
	uint_t	i;

	(void) ini_dtq(dtqid);

	NET_LOCK();
	for (i = 0U; i < SYS_ARCH_NUM_MBOX; i++) {
		if (net_mbox_id[i] == dtqid) {
			net_mbox_used[i] = false;
			break;
		}
	}
	NET_UNLOCK();
	*mbox = SYS_MBOX_NULL;
}

void
sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
	(void) tsnd_dtq((ID) *mbox, (intptr_t) msg, TMO_FEVR);
}

err_t
sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	return((psnd_dtq((ID) *mbox, (intptr_t) msg) == E_OK) ? ERR_OK : ERR_MEM);
}

err_t
sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
	return(sys_mbox_trypost(mbox, msg));
}

u32_t
sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
	intptr_t	data;
	ER			er;

	er = trcv_dtq((ID) *mbox, &data, ms_to_tmo(timeout_ms));
	if (er != E_OK) {
		return(SYS_ARCH_TIMEOUT);
	}
	if (msg != NULL) {
		*msg = (void *) data;
	}
	return(1U);
}

u32_t
sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
	intptr_t	data;
	ER			er;

	er = prcv_dtq((ID) *mbox, &data);
	if (er != E_OK) {
		return(SYS_MBOX_EMPTY);
	}
	if (msg != NULL) {
		*msg = (void *) data;
	}
	return(0U);
}

int
sys_mbox_valid(sys_mbox_t *mbox)
{
	return(*mbox != SYS_MBOX_NULL);
}

void
sys_mbox_set_invalid(sys_mbox_t *mbox)
{
	*mbox = SYS_MBOX_NULL;
}

/*
 *  ------------------------------------------------------------------
 *  スレッド（単発実装．lwIPはtcpip_init()の中でtcpip_thread生成の
 *  ために一度だけsys_thread_new()を呼ぶ．cfgで休止生成したNET_TSKを
 *  起動するだけで足りる．複数回呼ばれた場合は静的アサート的に
 *  ログを出して黙って上書きする（"MUST NOT FAIL"契約のため戻り値で
 *  失敗を伝える手段が無い）
 *  ------------------------------------------------------------------
 */
static lwip_thread_fn	net_thread_fn;
static void				*net_thread_arg;
static bool_t			net_thread_started;

void
net_task_entry(EXINF exinf)
{
	(void) exinf;
	net_thread_fn(net_thread_arg);		/* 通常は戻らない（tcpip_thread） */
}

sys_thread_t
sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
			   int stacksize, int prio)
{
	(void) name; (void) stacksize; (void) prio;

	if (net_thread_started) {
		syslog(LOG_ERROR,
			   "net: sys_thread_new() called more than once (unsupported)");
	}
	net_thread_fn = thread;
	net_thread_arg = arg;
	net_thread_started = true;
	(void) act_tsk(NET_TSK);
	return((sys_thread_t) NET_TSK);
}
