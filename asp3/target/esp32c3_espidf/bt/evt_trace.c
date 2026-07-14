/*
 *  （D-2d bond診断：HCI EVT 経路計装＝暗号有効化の «決定的» 局在化）
 *
 *  C3 の BLE bond が «暗号有効化タイムアウト»（ENC_CHANGE status=13=
 *  BLE_HS_ETIMEOUT）で失敗する真因を，LL/コントローラ層か shim/host 層かで
 *  確定させる計装。ペリフェラルの暗号有効化は次の HCI イベント授受で進む：
 *    central LL_ENC_REQ → controller が host へ «LE Long Term Key Request»
 *    (LE Meta 0x3E / subevent 0x05) → host が LTK Reply → LL_ENC_RSP/
 *    START_ENC → controller が host へ «Encryption Change»(event 0x08)。
 *
 *  host が処理する «全» HCI イベントは NimBLE host の
 *  `ble_hs_hci_evt_process(struct ble_hci_ev *ev)` を通る（ble_hs.c:655 から
 *  «クロスTU» 呼出し＝`--wrap` が確実に効く。同一TU内の
 *  ble_hci_trans_ll_evt_tx は --wrap 不可のため採らない）。ここを --wrap で
 *  横取りし，LTK Request と Encryption Change の到着数・status を RTC scratch
 *  STORE0(0x50) へ記録する（EVT_TRACE 時は SYNC マーカを転用＝0x50 は
 *  接続→pair→切断→再adv の間 app からは書かれないため保持される）。
 *
 *    LTK req == 0           → controller が LTK を要求していない＝LL/コント
 *                             ローラ層で暗号開始が完遂しない（shim を排除。
 *                             ※EVT 経路は非クリティカルで shim は落とさない
 *                             ことが別途反証済み＝«host 未到達»＝«controller
 *                             未生成» と読める）
 *    LTK req > 0, encchg==0 → LTK は要求されたが Encryption Change が返らない
 *                             （LTK Reply 送出後 LL 暗号完了で停止）
 *    encchg > 0             → status で成否（0=成功／非0=失敗）
 *
 *  0x50 レイアウト（各バイト飽和255）:
 *    [31:24] host 処理 EVT 総数（wrapper 稼働の sanity。0x5ADE51C0 のままなら
 *            wrapper 未動作＝--wrap 不発）
 *    [23:16] 直近 Encryption Change の status バイト
 *    [15: 8] Encryption Change(0x08) 到着数
 *    [ 7: 0] LE LTK Request(0x3E/0x05) 到着数
 *
 *  ESP32C3_BT_EVT_TRACE=ON のみコンパイル＆--wrap。既定OFF＝非回帰・passive
 *  （挙動不変＝生イベントを覗いて数えるだけで素通しする）。
 */
#include <stdint.h>

#define EVT_TRACE_RTC	((volatile uint32_t *) 0x60008050UL)

volatile uint32_t	g_evt_total;		/* host 処理 HCI EVT 総数        */
volatile uint32_t	g_evt_ltk_req;		/* LE LTK Request(0x3E/0x05) 数  */
volatile uint32_t	g_evt_enc_chg;		/* Encryption Change(0x08) 数    */
volatile uint32_t	g_evt_enc_status;	/* 直近 Encryption Change status */

static void
evt_trace_pack(void)
{
	uint32_t t = g_evt_total   > 255U ? 255U : g_evt_total;
	uint32_t l = g_evt_ltk_req > 255U ? 255U : g_evt_ltk_req;
	uint32_t c = g_evt_enc_chg > 255U ? 255U : g_evt_enc_chg;
	uint32_t s = g_evt_enc_status & 0xffU;

	*EVT_TRACE_RTC = (t << 24) | (s << 16) | (c << 8) | l;
}

/*  app が起動時に一度呼ぶ（.bss は0初期化だが SYNC マーカ転用を明示化）  */
void
esp_evt_trace_reset(void)
{
	g_evt_total = 0U;
	g_evt_ltk_req = 0U;
	g_evt_enc_chg = 0U;
	g_evt_enc_status = 0U;
	evt_trace_pack();
}

/*  --wrap 対象。struct ble_hci_ev* は先頭 uint8_t opcode / uint8_t length /
    uint8_t data[]（packed）なので void*→uint8_t* で opcode=[0]・param=[2]．  */
extern int __real_ble_hs_hci_evt_process(void *ev);

int
__wrap_ble_hs_hci_evt_process(void *ev)
{
	if (ev != (void *) 0) {
		const uint8_t	*p = (const uint8_t *) ev;
		uint8_t			code = p[0];

		g_evt_total++;
		if (code == 0x08U) {			/* Encryption Change */
			g_evt_enc_chg++;
			g_evt_enc_status = p[2];	/* status バイト */
		}
		else if (code == 0x3EU && p[2] == 0x05U) {	/* LE Meta / LTK Request */
			g_evt_ltk_req++;
		}
		evt_trace_pack();
	}
	return __real_ble_hs_hci_evt_process(ev);
}
