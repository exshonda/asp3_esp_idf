/*
 *  （C5 D-2d bond診断：暗号後の «RX/TX 双方向» パイプライン局在化）
 *
 *  C5 BLE bond が «暗号有効化タイムアウト»（ENC_CHANGE status=13=BLE_HS_ETIMEOUT）
 *  で失敗する真因を，«暗号確立後» の SMP 鍵配布フェーズで «どちら向きの ACL が
 *  止まるか» で局在化する。C3 の evt_trace.c は RX 側（controller→host の
 *  ble_mqueue_put）が put=1 で止まることまでを示したが，「controller が2個目を
 *  渡さない(A)」と「我々の SM が自分の鍵を送れず central が待って停止(TX側)」を
 *  区別できていなかった。RX 入口(host_rcv_pkt/ble_hci_rx_acl/alloc)は static＋
 *  inline で --wrap 不能のため，代わりに «別TU の global» な次の4点を横取りする：
 *
 *    ble_hs_hci_evt_process  … host が処理する全 HCI EVT（Encryption Change 0x08
 *                              検出で post_enc ゲートを立て，各カウンタの基点を
 *                              スナップショット）。クロスTU 呼出し(ble_hs.c)＝確実。
 *    ble_mqueue_put          … RX：controller→host ACL が host rx_q へ届いた数。
 *    ble_sm_tx               … TX：我々の SM が SMP PDU を送出しようとした数
 *                              （鍵配布 Identity Info/Addr 等）。0 なら «我々が鍵配布を
 *                              始めていない»＝host SM が暗号後に進んでいない。
 *    ble_transport_to_ll_acl_impl … TX：host→controller ACL が controller へ渡った数。
 *                              sm_tx>0 かつ to_ll=0 なら SMP PDU が controller へ
 *                              届いていない（TX 経路/queue の問題）。
 *
 *  RTC 記録＝LP_AON STORE3(0x600B100C)。app の APIERR ミラー(STORE3)とは排他
 *  （ESP32C5_BT_APIERR_TRACE=OFF で本トレースを使う）。SM マーカ STORE6 は温存。
 *
 *    STORE3 レイアウト（各バイト飽和255。enc_chg>=1 で «wrap 稼働＋暗号検出» の
 *    sanity）:
 *      [31:24] Encryption Change(0x08) 到着数
 *      [23:16] 暗号後 RX：ble_mqueue_put 増分（controller→host ACL）
 *      [15: 8] 暗号後 TX：ble_sm_tx 増分（我々の SMP 送出試行）
 *      [ 7: 0] 暗号後 TX：ble_transport_to_ll_acl_impl 増分（host→controller ACL）
 *
 *    判定：
 *      put>=1, sm_tx=0            → 我々の SM が暗号後に鍵配布を始めていない＝host
 *                                   SM が «暗号後の proc 進行» で停止（受信待ちで
 *                                   止まっている）。
 *      put=1, sm_tx>0, to_ll>0    → 我々は鍵を controller へ送出済み・central の鍵
 *                                   （RX put）が1個で止まる＝controller RX/LL or
 *                                   central 側。
 *      sm_tx>0, to_ll=0           → SMP PDU が controller へ届かない＝host→controller
 *                                   TX 経路の問題（esp_shim queue 等）。
 *
 *  ESP32C5_BT_RXTRACE=ON のみコンパイル＆--wrap。既定OFF＝非回帰・passive
 *  （素通しして数えるだけ）。
 */
#include <stdint.h>

#define RX_TRACE_RTC	((volatile uint32_t *) 0x600B100CUL)	/* C5 LP_AON STORE3 */

volatile uint32_t	g_rx_enc_chg;		/* Encryption Change(0x08) 到着数(HCI evt入口) */
volatile uint32_t	g_rx_sm_enc_rx;		/* ble_sm_enc_change_rx 到達数(SM dispatch)    */
volatile uint32_t	g_rx_put;			/* RX：ble_mqueue_put 累計         */
volatile uint32_t	g_rx_smtx;			/* TX：ble_sm_tx 累計              */
volatile uint32_t	g_rx_toll;			/* TX：to_ll_acl_impl 累計         */
volatile uint32_t	g_rx_put_at_enc;	/* Enc Change 時点の put 基点      */
volatile uint32_t	g_rx_smtx_at_enc;	/* 同 sm_tx 基点                  */
volatile uint32_t	g_rx_toll_at_enc;	/* 同 to_ll 基点                  */
/*  ★app が 1s 毎に g_rx_tick++（fch_hrt 非依存の粗タイマ）．enc 到着時に
    g_rx_enc_tick へスナップショット＝app が ETIMEOUT を受けた時刻との tick 差で
    «enc は早く来たか(SM が進まず30s待ち) / 30s で遅れて来たか(配送遅延)» を判別．  */
volatile uint32_t	g_rx_tick;			/* app 1s カウンタ                */
volatile uint32_t	g_rx_enc_tick;		/* Enc Change 到着時の tick        */

static void
rx_trace_pack(void)
{
	uint32_t base = (g_rx_enc_chg != 0U) ? 1U : 0U;
	uint32_t e = g_rx_enc_chg;		/* nibble */
	uint32_t s = g_rx_sm_enc_rx;	/* nibble */
	uint32_t p = base ? (g_rx_put  - g_rx_put_at_enc)  : 0U;
	uint32_t t = base ? (g_rx_smtx - g_rx_smtx_at_enc) : 0U;
	uint32_t l = base ? (g_rx_toll - g_rx_toll_at_enc) : 0U;

	if (e > 15U) { e = 15U; }
	if (s > 15U) { s = 15U; }
	if (p > 255U) { p = 255U; }
	if (t > 255U) { t = 255U; }
	if (l > 255U) { l = 255U; }
	/*  [31:28]enc_chg [27:24]sm_enc_rx [23:16]put [15:8]sm_tx [7:0]to_ll  */
	*RX_TRACE_RTC = (e << 28) | (s << 24) | (p << 16) | (t << 8) | l;
}

/*  --wrap 対象。struct ble_hci_ev* は先頭 uint8_t opcode / uint8_t length /
    uint8_t data[]（packed）＝void*→uint8_t* で opcode=[0]・param=[2]．  */
extern int __real_ble_hs_hci_evt_process(void *ev);

int
__wrap_ble_hs_hci_evt_process(void *ev)
{
	if (ev != (void *) 0) {
		const uint8_t	*p = (const uint8_t *) ev;

		if (p[0] == 0x08U) {			/* Encryption Change */
			g_rx_enc_chg++;
			g_rx_enc_tick    = g_rx_tick;	/* enc 到着時刻(tick) */
			g_rx_put_at_enc  = g_rx_put;
			g_rx_smtx_at_enc = g_rx_smtx;
			g_rx_toll_at_enc = g_rx_toll;
		}
		rx_trace_pack();
	}
	return __real_ble_hs_hci_evt_process(ev);
}

/*  --wrap 対象＝HCI Encryption Change を SM 層へ dispatch する入口．ble_hs_hci_evt.c
    のテーブルから «クロスTU» 呼出し＝--wrap 有効．enc_chg(HCI入口)は数えたが SM
    handler まで到達したかを分離＝enc_chg>0 かつ sm_enc_rx=0 なら dispatch/proc 不整合．  */
extern void __real_ble_sm_enc_change_rx(const void *ev);

void
__wrap_ble_sm_enc_change_rx(const void *ev)
{
	g_rx_sm_enc_rx++;
	rx_trace_pack();
	__real_ble_sm_enc_change_rx(ev);
}

/*  RX：controller→host ACL が host rx_q(ble_hs_rx_q)へ積まれた点．  */
extern int __real_ble_mqueue_put(void *mq, void *evq, void *om);

int
__wrap_ble_mqueue_put(void *mq, void *evq, void *om)
{
	g_rx_put++;
	rx_trace_pack();
	return __real_ble_mqueue_put(mq, evq, om);
}

/*  TX：我々の SM が SMP PDU を L2CAP へ送出しようとした点．  */
extern int __real_ble_sm_tx(uint16_t conn_handle, void *txom);

int
__wrap_ble_sm_tx(uint16_t conn_handle, void *txom)
{
	g_rx_smtx++;
	rx_trace_pack();
	return __real_ble_sm_tx(conn_handle, txom);
}

/*  TX：host→controller ACL が controller へ渡った点．  */
extern int __real_ble_transport_to_ll_acl_impl(void *om);

int
__wrap_ble_transport_to_ll_acl_impl(void *om)
{
	g_rx_toll++;
	rx_trace_pack();
	return __real_ble_transport_to_ll_acl_impl(om);
}
