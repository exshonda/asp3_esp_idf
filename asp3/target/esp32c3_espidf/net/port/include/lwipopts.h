/*
 *  lwIP コンフィギュレーション（ASP3／ESP32-C3．Phase C＝TCP/IP統合）
 *
 *  NO_SYS=1（RTOS非依存のraw API）＋専用タスク（net_task）で全ての
 *  lwIPコア呼出しを単一の実行文脈に集約する設計．マルチスレッド用の
 *  tcpip_thread／sockets／netconnは使わない（api配下の.cファイルはビルド対象外）．
 *  設計・経緯は docs/tcpip-integration.md．
 *
 *  個別ファイルは全て自己ガード（#if LWIP_XXX）されているため，
 *  Filelists.cmakeの lwipcore_SRCS／lwipcore4_SRCS をまるごと採用し，
 *  ここでの機能フラグでコンパイル内容を絞る（IPv6・PPP・6LoWPAN・
 *  apps一式・sockets/netconnは非採用＝ソースリスト自体に含めない）．
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/*
 *  RTOS非依存（raw API）．sys_now()／sys_arch_protect/unprotectのみ
 *  port/sys_arch.cで実装する．
 */
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        1

/*
 *  ヒープ・プール
 *
 *  MEM_SIZEはlwIP内部の可変長確保（raw pcb・dhcp構造体等）用．
 *  PBUF_POOL_BUFSIZEは1イーサネットフレーム（MTU 1500＋ヘッダ）を
 *  1個のpoolバッファに収める値（デフォルト値はTCP_MSS基準で小さすぎる
 *  ため明示指定．収まらない場合はpbufチェーンで自動対応するが，
 *  受信の主経路を単純化するため十分な大きさを確保する）．
 *  ESP32-C3のRAMはWi-Fi blob側の静的ヒープ（192KB．esp_shim_cfg.h）
 *  と共存するため小さめに抑える（DHCP＋raw ping程度のスコープ）．
 */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (4 * 1024)
#define PBUF_POOL_SIZE              4
#define PBUF_POOL_BUFSIZE           1600
#define MEMP_NUM_PBUF               4

#define MEMP_NUM_RAW_PCB            2
#define MEMP_NUM_UDP_PCB            2
#define MEMP_NUM_SYS_TIMEOUT        8

/*
 *  プロトコル（ソケット／netconn層＝api配下の.cファイルはビルド対象外のため0固定）
 */
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1

/*
 *  TCPは本フェーズ（DHCP＋raw ICMP ping）のスコープ外＝OFF．
 *  tcp*.cは自己ガードのためソースリストに含めたままでも空になる．
 *  将来TCPを使う場合はここを1にすれば足りる（追加ソースは不要）．
 */
#define LWIP_TCP                    0

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
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_HOSTNAME         0

/*
 *  contrib/apps/ping（raw API版．PING_USE_SOCKETS既定=LWIP_SOCKET=0で
 *  raw API経路が選ばれる）の結果通知先．実体はnetif_esp32c3.c．
 */
#ifdef __cplusplus
extern "C" {
#endif
extern void net_ping_result(int ok);
#ifdef __cplusplus
}
#endif
#define PING_RESULT(ping_ok)  net_ping_result(ping_ok)

#endif /* LWIP_LWIPOPTS_H */
