/*
 *  （D-2c RX-data dispatch 局在化計装）
 *
 *  接続はできるが ServicesResolved が解決しない（ATT/GATT 探索が完了しない）
 *  ブロッカーの真の所在を，仮説 (a)/(b) で切り分けるための計装。
 *
 *    (a) host task が ble_hs_rx_q の RX-data イベントを dispatch しない
 *        （＝ble_hs_process_rx_data_queue が走らず ble_mqueue_get で drain
 *          されない）。
 *    (b) drain はされるが ble_hs_hci_evt_acl_process 内の
 *        ble_hs_conn_find(handle)==NULL で ble_l2cap_rx 前に drop。
 *
 *  ble_hs.c は ble_mqueue_put/get（ble_hs_mqueue.c）・ble_l2cap_rx
 *  （ble_l2cap.c）・ble_hs_conn_find（ble_hs_conn.c）をいずれも別TUへ呼ぶ
 *  ので，hal を一切編集せず `--wrap` で捕捉できる。各カウンタを RTC scratch
 *  STORE2（0x60008058．D-2c では storm probe OFF で未使用）へパックし，
 *  ライブ接続の後に **1回の esptool dump-mem** で読む（リセットは RTC
 *  ドメインを保持する）。.bss グローバルへもミラー（JTAG 読み）。
 *
 *    RTC 0x60008058 レイアウト（各バイト飽和 255）:
 *      [ 7: 0] ble_mqueue_put   呼出し数 = ACL が host rx_q へ届いた回数
 *      [15: 8] ble_mqueue_get   非NULL数 = host が rx_q を drain した回数
 *      [23:16] ble_l2cap_rx     呼出し数 = ACL が L2CAP へ回った回数
 *      [31:24] ble_hs_conn_find NULL数   = 接続未検出 drop の corroboration
 *
 *  判定:
 *    put>0 かつ get==0            → (a) host が RX-data を dispatch しない
 *    put>0, get>0, l2cap_rx==0    → drain するが l2cap 前 drop = (b) 濃厚
 *                                    （conn_find NULL 数が同時に増えれば確定）
 *    put>0, get>0, l2cap_rx>0     → ACL は L2CAP 到達＝(a)(b)いずれも反証，
 *                                    詰まりは ATT/GATT 層（新知見）
 *
 *  CMake オプション ESP32C3_BT_ACL_TRACE=ON のときのみコンパイル＆リンク
 *  （4関数を --wrap）。既定 OFF ゆえ通常ビルドは非回帰・passive（挙動不変）。
 */
#include <stdint.h>

#define ACL_TRACE_RTC	((volatile uint32_t *) 0x60008058UL)
/*  TX 側（host→controller）カウンタ．STORE5(0x600080BC)．default_reset の
    dump-mem では保持される（usb-reset時のみROM破壊）．
      [ 7: 0] ble_hci_trans_hs_acl_tx 呼出し数（host が ATT 応答を送ろうと
              した回数）
      [15: 8] 同 戻り値!=0（送信失敗）数
      [23:16] esp_vhci_host_send_packet 呼出し数（controller への実送出）  */
#define ACL_TXTRACE_RTC	((volatile uint32_t *) 0x600080BCUL)

volatile uint32_t	g_acl_put;		/* ble_mqueue_put 呼出し数      */
volatile uint32_t	g_acl_get;		/* ble_mqueue_get 非NULL数      */
volatile uint32_t	g_acl_l2cap_rx;	/* ble_l2cap_rx 呼出し数        */
volatile uint32_t	g_acl_cf_null;	/* ble_hs_conn_find NULL 返却数 */
volatile uint32_t	g_acl_tx;		/* ble_hci_trans_hs_acl_tx 数   */
volatile uint32_t	g_acl_tx_fail;	/* 同 戻り値!=0 数              */
volatile uint32_t	g_acl_vhci_tx;	/* esp_vhci_host_send_packet 数 */

static void
acl_trace_pack(void)
{
	uint32_t p = g_acl_put      > 255U ? 255U : g_acl_put;
	uint32_t g = g_acl_get      > 255U ? 255U : g_acl_get;
	uint32_t l = g_acl_l2cap_rx > 255U ? 255U : g_acl_l2cap_rx;
	uint32_t c = g_acl_cf_null  > 255U ? 255U : g_acl_cf_null;

	*ACL_TRACE_RTC = (c << 24) | (l << 16) | (g << 8) | p;
}

static void
acl_txtrace_pack(void)
{
	uint32_t t  = g_acl_tx      > 255U ? 255U : g_acl_tx;
	uint32_t tf = g_acl_tx_fail > 255U ? 255U : g_acl_tx_fail;
	uint32_t v  = g_acl_vhci_tx > 255U ? 255U : g_acl_vhci_tx;

	*ACL_TXTRACE_RTC = (v << 16) | (tf << 8) | t;
}

/*  app が起動時に一度呼ぶ（RTC の前 boot 残値との混同回避）  */
void
esp_acl_trace_reset(void)
{
	g_acl_put = 0U;
	g_acl_get = 0U;
	g_acl_l2cap_rx = 0U;
	g_acl_cf_null = 0U;
	g_acl_tx = 0U;
	g_acl_tx_fail = 0U;
	g_acl_vhci_tx = 0U;
	*ACL_TRACE_RTC = 0U;
	*ACL_TXTRACE_RTC = 0U;
}

/*  --wrap 対象。ポインタ引数/戻りのみなので不透明型 void* で十分
    （リンカは型を検査しない．呼出し規約は同一）。  */
extern int   __real_ble_mqueue_put(void *mq, void *evq, void *om);
extern void *__real_ble_mqueue_get(void *mq);
extern void *__real_ble_hs_conn_find(unsigned conn_handle);
extern int   __real_ble_l2cap_rx(void *conn, void *hdr, void *om,
								 void *out_rx_cb, void *out_reject_cid);

int
__wrap_ble_mqueue_put(void *mq, void *evq, void *om)
{
	g_acl_put++;
	acl_trace_pack();
	return __real_ble_mqueue_put(mq, evq, om);
}

void *
__wrap_ble_mqueue_get(void *mq)
{
	void *om = __real_ble_mqueue_get(mq);
	if (om != (void *) 0) {
		g_acl_get++;
		acl_trace_pack();
	}
	return om;
}

void *
__wrap_ble_hs_conn_find(unsigned conn_handle)
{
	void *conn = __real_ble_hs_conn_find(conn_handle);
	if (conn == (void *) 0) {
		g_acl_cf_null++;
		acl_trace_pack();
	}
	return conn;
}

int
__wrap_ble_l2cap_rx(void *conn, void *hdr, void *om,
					void *out_rx_cb, void *out_reject_cid)
{
	g_acl_l2cap_rx++;
	acl_trace_pack();
	return __real_ble_l2cap_rx(conn, hdr, om, out_rx_cb, out_reject_cid);
}

/*  TX 側（host→controller）  */
extern int __real_ble_hci_trans_hs_acl_tx(void *om);
extern void __real_esp_vhci_host_send_packet(unsigned char *data, unsigned short len);

int
__wrap_ble_hci_trans_hs_acl_tx(void *om)
{
	int rc = __real_ble_hci_trans_hs_acl_tx(om);
	g_acl_tx++;
	if (rc != 0) {
		g_acl_tx_fail++;
	}
	acl_txtrace_pack();
	return rc;
}

void
__wrap_esp_vhci_host_send_packet(unsigned char *data, unsigned short len)
{
	g_acl_vhci_tx++;
	acl_txtrace_pack();
	__real_esp_vhci_host_send_packet(data, len);
}
