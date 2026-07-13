/*
 *  C3負荷試験デモ（TCP echo + UDP echo + ヒープ/カウンタ監視）
 *
 *  S3のスループット試験（docs/s3-throughput-findings-for-c6.md）が
 *  露見させた2つのOSAシム潜在欠陥（(A)キューのメッセージ毎malloc・
 *  ISR内malloc，(B)TXバッファの毎パケット動的malloc）をC3で検証する
 *  ための専用アプリ．apps/tcp_socket_echo・apps/udp_socket_echoの
 *  ボイラープレートをそのまま流用し（両アプリは他エージェントが使用中
 *  の可能性があるため直接改造せず新規ディレクトリとして複製），
 *  ・TCP echo（port 8）
 *  ・UDP echo（port 9）
 *  ・monitor_task：esp_shim_heap_free_size()・累積送受信バイト数を
 *    周期syslog出力
 *  を1バイナリにまとめている．
 *
 *  欠陥B（TXバッファ静的化）は -DLOAD_TEST_STATIC_TXBUF でopt-in．
 *  未指定時は WIFI_INIT_CONFIG_DEFAULT() の既定（動的）のままとし，
 *  修正前後のA/B比較を同一ソース・ビルドオプションの差分だけで行える
 *  ようにしている．
 */
#ifndef LOAD_TEST_C3_H
#define LOAD_TEST_C3_H

#include <kernel.h>

#define MAIN_PRIORITY		10
#define STACK_SIZE			8192

#define TCP_PRIORITY		10
#define TCP_STACK_SIZE		4096

#define UDP_PRIORITY		10
#define UDP_STACK_SIZE		4096

#define MON_PRIORITY		12		/* 監視は低優先度（負荷を妨げない） */
#define MON_STACK_SIZE		2048

/*
 *  echoポート（7=tcpecho_raw，8=tcp_socket_echo，9=udp_socket_echoと
 *  重複しないよう，本アプリは他アプリと同時起動しない前提で同じ番号
 *  8/9を再利用する（単体flash運用）．
 */
#define TCP_ECHO_PORT		8
#define UDP_ECHO_PORT		9

/*
 *  監視周期（us）
 */
#define MON_PERIOD_US		5000000U	/* 5秒 */

/*
 *  接続先（-DWIFI_SSID=... -DWIFI_PASSWORD=... で上書き）
 */
#ifndef WIFI_SSID
#define WIFI_SSID		"your-ssid"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD	"your-password"
#endif

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
extern void tcp_echo_task(EXINF exinf);
extern void udp_echo_task(EXINF exinf);
extern void mon_task(EXINF exinf);
#endif

#endif /* LOAD_TEST_C3_H */
