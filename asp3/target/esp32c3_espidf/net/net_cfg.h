/*
 *  TCP/IP統合（lwIP，Phase C）の静的構成（net.cfgと一致させること）
 */
#ifndef NET_CFG_H
#define NET_CFG_H

/*
 *  受信フレームキュー（wifi_rx_cb→net_taskへのボックス化ポインタ渡し．
 *  esp_wifi blob側のrx pool深さに合わせる．満杯時はドロップ＋即eb解放）
 */
#define NET_RXQ_CNT      16

/*
 *  net_task（lwIPコアの唯一の実行文脈．sys_check_timeouts含む全lwIP
 *  API呼出しをここに集約する．経緯はdocs/tcpip-integration.md）
 */
#define NET_TASK_PRI     4
#define NET_TASK_STKSZ   3072

/*
 *  net_taskの受信ポーリング周期（μs．trcv_dtqのタイムアウト．この間隔
 *  でsys_check_timeouts()を呼ぶ＝DHCP/ARP/pingタイマの粒度になる）
 */
#define NET_POLL_TMO     100000

#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void net_task(EXINF exinf);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* NET_CFG_H */
