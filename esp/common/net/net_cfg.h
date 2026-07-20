/*
 *  TCP/IP統合（lwIP，Phase C／BSDソケット互換化）の静的構成
 *  （net.cfgと一致させること）
 *
 *  NO_SYS=0．lwIP自身が生成する唯一のスレッド（tcpip_thread）を，
 *  cfgで静的生成した1個のASP3タスク（NET_TSK）に割り当てる
 *  （sys_thread_new()は生涯に一度しか呼ばれない前提．port/sys_arch.c
 *  参照）．sys_sem_t／sys_mbox_tはこのプールから割り当てる
 *  （mbox＝ASP3 CRE_DTQをそのまま利用．メッセージは常に1ポインタ分＝
 *  ボックス化不要）．設計・経緯はdocs/tcpip-integration.md．
 */
#ifndef NET_CFG_H
#define NET_CFG_H

/*
 *  sys_arch静的プール
 *
 *  セマフォ：netconn毎のop_completed用（MEMP_NUM_NETCONN分）＋予備．
 *  メールボックス：tcpip_thread自身の受信箱(1)＋netconn毎のrecv/accept
 *  （MEMP_NUM_NETCONN×2まで）＋予備．深さは全メールボックス共通
 *  （lwipopts.hのTCPIP_MBOX_SIZE／DEFAULT_*_MBOX_SIZE以上にすること）．
 */
#define SYS_ARCH_NUM_SEM    8
#define SYS_ARCH_NUM_MBOX   10
#define SYS_ARCH_MBOX_DEPTH 8

/*
 *  NET_TSK：lwIPが生成する唯一のスレッド（tcpip_thread）の実体．
 *  cfgでは静的に生成（TA_NULL＝休止状態）し，sys_thread_new()から
 *  act_tskで起動する．エントリはport/sys_arch.cのトランポリン
 *  （net_task_entry）で，起動時に渡された関数ポインタ（＝lwIPの
 *  tcpip_thread）を呼ぶだけ．
 */
#define NET_TASK_PRI     4
#define NET_TASK_STKSZ   4096

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void net_task_entry(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* NET_CFG_H */
