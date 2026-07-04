/*
 *  lwIP sys_arch型定義（ASP3用．NO_SYS=0）
 *
 *  sys_sem_t／sys_mbox_t／sys_thread_tはASP3のID（int_t）と同じ表現を
 *  持つ素のintとして定義する（<kernel.h>はここではincludeしない．
 *  lwip/sys.h経由でほぼ全lwIP翻訳単位に波及するため，実体（kernel.h
 *  依存の関数群）はport/sys_arch.c側にのみ閉じ込める）．
 *  0はASP3の有効ID範囲外（TMIN_xxxID=1以上）のため無効値として使う．
 *
 *  LWIP_COMPAT_MUTEX=1（lwipopts.h）のため，sys_mutex_tはlwip/sys.hが
 *  sys_sem_tへ自動的にエイリアスする（本ヘッダでの定義は不要）．
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

typedef int sys_sem_t;
typedef int sys_mbox_t;
typedef int sys_thread_t;

#define SYS_SEM_NULL   ((sys_sem_t) 0)
#define SYS_MBOX_NULL  ((sys_mbox_t) 0)

#endif /* LWIP_ARCH_SYS_ARCH_H */
