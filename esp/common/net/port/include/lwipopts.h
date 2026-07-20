/*
 *  lwIP コンフィギュレーション（ASP3／ESP32-C3．BSDソケット互換化）
 *
 *  NO_SYS=0．lwIP自身が生成する唯一のスレッド（tcpip_thread）を
 *  cfg生成のNET_TSK（port/sys_arch.c参照）に割り当てる．
 *  LWIP_TCPIP_CORE_LOCKING=0＝メッセージパッシングモデル（ソケット呼出し
 *  は各アプリタスク文脈でmboxにメッセージを積みop_completedセマフォで
 *  待つ．core全体を保護する巨大ミューテックスは使わない．必要な
 *  セマフォ／ミューテックスの数が少なく済む）．
 *  設計・経緯は docs/tcpip-integration.md．
 *
 *  個別ファイルは全て自己ガード（#if LWIP_XXX）されているため，
 *  Filelists.cmakeの lwipcore_SRCS／lwipcore4_SRCS／lwipapi_SRCS を
 *  まるごと採用し，ここでの機能フラグでコンパイル内容を絞る
 *  （IPv6・PPP・6LoWPANは非採用＝ソースリスト自体に含めない）．
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/*
 *  NO_SYS=0（BSDソケット／netconn API．tcpip_thread経由のメッセージ
 *  パッシング）．sys_now()／sys_arch_protect/unprotect／セマフォ・
 *  メールボックス・スレッド生成はport/sys_arch.cで実装する．
 */
#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_TCPIP_CORE_LOCKING     0
#define LWIP_COMPAT_MUTEX           1

/*
 *  tcpip_thread自身の受信箱および各netconnのrecv/acceptメールボックス
 *  の深さ．port/net_cfg.hのSYS_ARCH_MBOX_DEPTH（プール共通深さ）以下
 *  にすること．
 */
#define TCPIP_MBOX_SIZE             6
#define DEFAULT_RAW_RECVMBOX_SIZE   6
#define DEFAULT_UDP_RECVMBOX_SIZE   6
#define DEFAULT_TCP_RECVMBOX_SIZE   6
#define DEFAULT_ACCEPTMBOX_SIZE     6

/*
 *  ソケット／netconn（BSD互換．LWIP_COMPAT_SOCKETSは既定1のため
 *  socket()/connect()/send()/recv()等の標準名がそのまま使える）
 */
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 1
#define MEMP_NUM_NETCONN            4

/*
 *  SO_RCVTIMEO（tcp_socket_clientがrecv()をブロックしっぱなしに
 *  しないため）．NONSTANDARD=1でint（ミリ秒）指定にし，struct
 *  timeval／sys/time.hへの依存を避ける．
 */
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD  1

/*
 *  errno（hal_stub/include/errno.hへ委譲．lwip/src/include/lwip/
 *  errno.hがLWIP_ERRNO_STDINCLUDE経由で<errno.h>にフォールバックする
 *  仕組みを利用．詳細はerrno.h先頭コメント参照）
 */
#define LWIP_ERRNO_STDINCLUDE       1

/*
 *  ヒープ・プール
 *
 *  MEM_SIZEはlwIP内部の可変長確保（raw pcb・dhcp構造体・ソケット層の
 *  一時バッファ等）用．PBUF_POOL_BUFSIZEは1イーサネットフレーム
 *  （MTU 1500＋ヘッダ）を1個のpoolバッファに収める値．
 *  ESP32-C3のRAMはWi-Fi blob側の静的ヒープ（192KB．esp_shim_cfg.h）
 *  と共存するため小さめに抑える．
 */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (4 * 1024)
#define PBUF_POOL_SIZE              4
#define PBUF_POOL_BUFSIZE           1600
#define MEMP_NUM_PBUF               4

#define MEMP_NUM_RAW_PCB            2
#define MEMP_NUM_UDP_PCB            2
#define MEMP_NUM_SYS_TIMEOUT        8

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1

/*
 *  TCP（tcpecho_raw＝ポート7のraw APIエコーサーバ＋tcp_socket_echo＝
 *  ポート8のBSDソケットAPIエコーサーバ．両方が同時にlistenするため
 *  MEMP_NUM_TCP_PCB_LISTENは最低2必要（1だと2個目のlisten()が
 *  ERR_MEMで失敗する．実機で確認済）．プールは小さめに抑える
 *  （RAM予算．docs/tcpip-integration.md参照）．
 */
#define LWIP_TCP                    1
#define MEMP_NUM_TCP_PCB            3
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            8

#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_DNS                    0

/*
 *  ソフトウェアチェックサム（esp_wifiはホストスタック向けの
 *  チェックサムオフロードを行わない）
 */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

#define LWIP_STATS                  0

/*
 *  netifステータス変化（DHCP完了等）のコールバック．ポーリング不要で
 *  IPアドレス取得を検出するために使用（netif_esp32c3.c参照）．
 */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_HOSTNAME         0

/*
 *  contrib/apps/ping．PING_USE_SOCKETSは既定でLWIP_SOCKET追従だが，
 *  ここでは明示的に0固定＝raw API経路に留める．sys_thread_new()は
 *  port/sys_arch.cが「生涯に一度だけ（tcpip_thread用）」しか想定して
 *  いないため，ping_thread()（ソケット版．独自にsys_thread_newする）
 *  を使うと壊れる．
 */
#define PING_USE_SOCKETS            0

#ifdef __cplusplus
extern "C" {
#endif
extern void net_ping_result(int ok);
#ifdef __cplusplus
}
#endif
#define PING_RESULT(ping_ok)  net_ping_result(ping_ok)

#endif /* LWIP_LWIPOPTS_H */
