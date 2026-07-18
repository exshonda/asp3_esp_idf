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
 *    [31:24] LTK Request 到着〜Encryption Change 到着の «秒» 差（★遅延実測。
 *            〜30 なら «遅いハンドシェイク»＝手遅れで SM タイムアウト／〜0-2 なら
 *            «速いのに SM がタイムアウト»＝NimBLE SM proc バグ。当初の total は
 *            wrapper 稼働実証済みのため delta 秒へ転用）
 *    [23:16] 直近 Encryption Change の status バイト
 *    [15: 8] Encryption Change(0x08) 到着数
 *    [ 7: 0] LE LTK Request(0x3E/0x05) 到着数
 *
 *  ESP32C3_BT_EVT_TRACE=ON のみコンパイル＆--wrap。既定OFF＝非回帰・passive
 *  （挙動不変＝生イベントを覗いて数えるだけで素通しする）。
 */
#include <stdint.h>

/*  fch_hrt()＝高分解能タイマ（μs 単位の HRTCNT＝uint32_t）．LTK Request〜
    Encryption Change の実時間差を測るために使う（タスク文脈で呼出し可）．  */
extern uint32_t fch_hrt(void);

#define EVT_TRACE_RTC	((volatile uint32_t *) 0x60008050UL)

volatile uint32_t	g_evt_total;		/* host 処理 HCI EVT 総数        */
volatile uint32_t	g_evt_ltk_req;		/* LE LTK Request(0x3E/0x05) 数  */
volatile uint32_t	g_evt_enc_chg;		/* Encryption Change(0x08) 数    */
volatile uint32_t	g_evt_enc_status;	/* 直近 Encryption Change status */
volatile uint32_t	g_evt_ltk_hrt;		/* 初回 LTK Request の HRT(μs)   */
volatile uint32_t	g_evt_delta_us;		/* LTK Req〜Enc Change の差(μs)  */
volatile uint32_t	g_evt_enc_hrt;		/* Encryption Change(0x08) の HRT(μs)．
										   ★app が ETIMEOUT を受けた時刻との差＝
										   «実30秒待ち»(PDU欠落) vs «早発火»(NPL)の判別 */
volatile uint32_t	g_evt_acl_put;		/* ble_mqueue_put 数＝ACL が host rx_q へ */
volatile uint32_t	g_evt_acl_get;		/* ble_mqueue_get 非NULL数＝host が drain */
volatile uint32_t	g_evt_acl_l2cap;	/* ble_l2cap_rx 数＝L2CAP 到達 */
volatile uint32_t	g_evt_acl_at_enc;	/* Enc Change 時点の put スナップショット */
volatile uint32_t	g_evt_get_at_enc;	/* 同 get スナップショット */
volatile uint32_t	g_evt_l2cap_at_enc;	/* 同 l2cap スナップショット */

/*
 *  ------------------------------------------------------------------
 *  ★2026-07-17 追加：bond «機構» の A/B 局在化（evidence-c3-04）
 *  ------------------------------------------------------------------
 *
 *  背景（evidence-c3-03 §5）：真cold・同一 central・同一手順で
 *    A＝esp-idf 供給（真の v5.5.4 タグ）… bond **失敗 2/2**（AuthenticationCanceled）
 *    B＝hal 供給                        … bond **成功 2/2**
 *  ＝**帰属は «供給»** まで確定。**しかし «どこで死ぬか»（機構）は未特定**。
 *
 *  本追加は既存の `evt_trace_pack()`（RTC STORE0 への ACL パイプライン用パッキング）
 *  を**壊さずに**，**RTC FAST RAM（0x50000000 系）へ «HCI ステージ地図» を出す**。
 *
 *  ★なぜ RTC FAST か（実測）：`esptool --before usb-reset ... read-mem` は
 *  **usb-reset を伴う**が，**RTC FAST は usb-reset を跨いで保持される**
 *  （本ラウンドで `0xCAFEBABE` を書いて read-back で実証）。かつ **POR では消える**
 *  ので cleared-boot-read がそのまま効く。RTC STORE は 7 語しか無く，
 *  app（SYNC/ADV/CONN/DISC/PAIR）と食い合うため，**語数の要る地図は FAST 側へ置く**。
 *  （app は既に 0x50000080 を DHCPDIAG に使っており，スクラッチとして前例がある。）
 *
 *  ★測るのは «HCI イベントの種別と順序» だけ＝**passive**（挙動不変）。
 *  ★★**「blob だけ差し替える第3アーム」はやらない**——それ自体が «混成» であり
 *  ABI 不整合と機能差を分離できない（私自身の警告．C6 evidence-c6-05 と同型）。
 *  本計装は **A/B の «同一コード» に対して同じ窓から覗く**ので混成を作らない。
 */
#define EVT_FAST_BASE	((volatile uint32_t *) 0x50000100UL)
/*  index: 0=magic 1=total 2=conn_complete 3=disc 4=disc_reason 5=ltk_req
    6=enc_chg 7=enc_status 8=acl_put 9=acl_get 10=acl_l2cap 11=cmd_complete
    12=cmd_status 13=le_meta_other 14..19=最初に見た EVT code 24個（4個/語）  */
volatile uint32_t	g_evt_conn_cmpl;	/* LE Connection Complete (0x3E/0x01) */
volatile uint32_t	g_evt_disc;			/* Disconnection Complete (0x05)      */
volatile uint32_t	g_evt_disc_reason;	/* 直近 Disconnection の reason        */
volatile uint32_t	g_evt_cmd_cmpl;		/* Command Complete (0x0E)            */
volatile uint32_t	g_evt_cmd_status;	/* Command Status (0x0F)              */
volatile uint32_t	g_evt_le_other;		/* 上記以外の LE Meta subevent         */
volatile uint32_t	g_evt_seq_n;		/* 記録済み EVT code 数（最大24）      */
volatile uint8_t	g_evt_seq[24];		/* 到着順の EVT code（0x3E は subevent を上位に） */

#ifdef TOPPERS_C3_EVT_FAST_MAP
static void
evt_trace_fast_dump(void)
{
	volatile uint32_t	*f = EVT_FAST_BASE;
	uint32_t			i;

	f[0]  = 0x5C3E0001U;			/* magic＝この地図が書かれた証拠 */
	f[1]  = g_evt_total;
	f[2]  = g_evt_conn_cmpl;
	f[3]  = g_evt_disc;
	f[4]  = g_evt_disc_reason;
	f[5]  = g_evt_ltk_req;
	f[6]  = g_evt_enc_chg;
	f[7]  = g_evt_enc_status;
	f[8]  = g_evt_acl_put;
	f[9]  = g_evt_acl_get;
	f[10] = g_evt_acl_l2cap;
	f[11] = g_evt_cmd_cmpl;
	f[12] = g_evt_cmd_status;
	f[13] = g_evt_le_other;
	/*  到着順の EVT code（4個/語×6語＝24個）  */
	for (i = 0U; i < 6U; i++) {
		f[14U + i] = ((uint32_t) g_evt_seq[i * 4U + 0U])
				   | ((uint32_t) g_evt_seq[i * 4U + 1U] << 8)
				   | ((uint32_t) g_evt_seq[i * 4U + 2U] << 16)
				   | ((uint32_t) g_evt_seq[i * 4U + 3U] << 24);
	}
}
#endif /* TOPPERS_C3_EVT_FAST_MAP */

#ifdef TOPPERS_C3_EVT_FAST_MAP
/*
 *  ------------------------------------------------------------------
 *  ★TX 側の計装（evidence-c3-05）＝「沈黙」か「誤答」かを分ける
 *  ------------------------------------------------------------------
 *
 *  evidence-c3-04 の残＝**本計装は RX しか数えていない**ので
 *  「ペリフェラルが応答しない」のか「誤った応答を返す」のか区別できなかった。
 *
 *  ★wrap 対象＝`ble_transport_to_ll_acl_impl(struct os_mbuf *om)`。
 *  ★★**`ble_transport_to_ll_acl` を wrap しても «無言で効かない»**（本ラウンドで実測）：
 *  そちらは transport.h の薄いシムで，`ble_hs_tx_data` は逆アセンブル上
 *  **直接 `ble_transport_to_ll_acl_impl` へ j している**＝当該名の未定義参照が
 *  存在せず `--wrap` が噛まない。**wrap が効かないと TX=0 になり «ペリフェラルは沈黙»
 *  と読めてしまう＝捏造**。∴ **wrap は必ず «実際に噛んだか» を逆アセンブルで確認する**。
 *  **選定理由（実測）**：host→controller の ACL 送出の chokepoint で，
 *  **両ツリーで «同一シグネチャ»**（`ble_hs.c:880` の `ble_hs_tx_data()` から
 *  クロスTU 呼出し＝`--wrap` が効く．両ツリーとも同じ行）。
 *
 *  ★★他の候補を «実測で» 却下した：
 *   - `ble_sm_rx`：**関数ポインタ（`chan->rx_fn`）経由で呼ばれる**⇒`--wrap` 不可。
 *   - `ble_l2cap_rx`：**両ツリーでシグネチャが違う**
 *       hal    ＝ `(struct ble_hs_conn *conn, struct hci_data_hdr *hci_hdr, struct os_mbuf **om, …)`
 *       esp-idf＝ `(uint16_t conn_handle, uint8_t pb, struct os_mbuf *om)`
 *     ⇒ 引数から om を取り出す位置が違う（片方は二重ポインタ）＝**共通の parser を書けない**。
 *     （既存 wrap は 5 引数＝hal 用に書かれている。RISC-V では a0-a2 がそのまま
 *      素通しされるので **«数える» 用途では両ツリーで正しく動く**が，
 *      **引数の «解釈» は esp-idf 側で無意味**＝だから RX の opcode 解析には使わない。）
 *
 *  ★om のレイアウト（両ツリー共通．`ble_hs_tx_data` は完全な ACL パケットを渡す
 *    ＝同関数内の HCI ログが `data[0]=0x02`(ACL) を付けて om 全体をコピーしている）：
 *      [0:1] handle+flags ／ [2:3] ACL len ／ [4:5] L2CAP len ／ [6:7] **CID** ／ [8] **payload 先頭**
 *    ⇒ CID==0x0006 が SMP。`om_data` は **両ツリーとも os_mbuf の第1フィールド**（実測）。
 */
#define TX_FAST_BASE	((volatile uint32_t *) 0x50000160UL)

volatile uint32_t	g_tx_total;		/* ble_transport_to_ll_acl 呼出し総数 */
volatile uint32_t	g_tx_smp;		/* うち CID==6（SMP）             */
volatile uint32_t	g_tx_att;		/* うち CID==4（ATT）             */
volatile uint32_t	g_tx_seq_n;		/* 記録済み SMP opcode 数（最大12） */
volatile uint8_t	g_tx_seq[12];	/* 送出した SMP opcode の順        */

static void
tx_trace_fast_dump(void)
{
	volatile uint32_t	*f = TX_FAST_BASE;
	uint32_t			i;

	f[0] = 0x5C3E0002U;
	f[1] = g_tx_total;
	f[2] = g_tx_smp;
	f[3] = g_tx_att;
	for (i = 0U; i < 3U; i++) {
		f[4U + i] = ((uint32_t) g_tx_seq[i * 4U + 0U])
				  | ((uint32_t) g_tx_seq[i * 4U + 1U] << 8)
				  | ((uint32_t) g_tx_seq[i * 4U + 2U] << 16)
				  | ((uint32_t) g_tx_seq[i * 4U + 3U] << 24);
	}
}

extern int __real_ble_transport_to_ll_acl_impl(void *om);

int
__wrap_ble_transport_to_ll_acl_impl(void *om)
{
	if (om != (void *) 0) {
		/*  om_data＝os_mbuf の第1フィールド（両ツリー実測）  */
		const uint8_t	*d = *(const uint8_t **) om;

		g_tx_total++;
		if (d != (const uint8_t *) 0) {
			uint16_t	cid = (uint16_t) (d[6] | ((uint16_t) d[7] << 8));

			if (cid == 0x0006U) {			/* SMP */
				g_tx_smp++;
				if (g_tx_seq_n < 12U) {
					g_tx_seq[g_tx_seq_n] = d[8];	/* SMP opcode */
					g_tx_seq_n++;
				}
			}
			else if (cid == 0x0004U) {		/* ATT */
				g_tx_att++;
			}
		}
		/*  ★ACL TX は «毎パケット» だが pairing 中は十数回なので dump してよい。
		    それでも hot path 化を避けるため SMP/ATT のときだけ dump する
		    （evidence-c3-04 §5.1＝hot path で 20 語 dump したら hal でも bond が
		     落ちた＝計装が侵襲的になった，の再発防止）。  */
		if (g_tx_smp != 0U || g_tx_att != 0U) {
			tx_trace_fast_dump();
		}
	}
	return __real_ble_transport_to_ll_acl_impl(om);
}
#endif /* TOPPERS_C3_EVT_FAST_MAP */

static void
evt_trace_pack(void)
{
	/*  ★暗号確立«後» の ACL RX パイプラインを可視化（各バイト飽和255）:
	 *    [ 7: 0] put   ＝ ACL が host rx_q へ届いた数（ble_mqueue_put）
	 *    [15: 8] get   ＝ host が drain した数（ble_mqueue_get 非NULL）
	 *    [23:16] l2cap ＝ L2CAP へ回った数（ble_l2cap_rx）
	 *    [31:24] enc_chg ＝ Encryption Change 到着数（暗号到達の sanity）
	 *  判定：put=0→controller未配送(blob)／put>get→rx_q滞留(host未drain=eventq
	 *  signal欠落)／get>l2cap→L2CAP前drop(conn_find NULL)／put=get=l2cap>0→
	 *  SMP層まで到達＝鍵配布PDU数不足 or SMP処理の問題．  */
	/*  byte3 は «E_CTX セマフォgive累計»（shim_sem_ectx_total）へ転用＝
	    controller が MIE==0 文脈から give を出したか（sem修正が該当する機序が
	    実在するか）の判別．0=機序なし(sem説誤り→blob疑い)／>0=機序実在．  */
	extern volatile uint32_t	shim_sem_ectx_total;
	uint32_t base = (g_evt_enc_hrt != 0U) ? 1U : 0U;
	uint32_t p = base ? (g_evt_acl_put   - g_evt_acl_at_enc)   : 0U;
	uint32_t g = base ? (g_evt_acl_get   - g_evt_get_at_enc)   : 0U;
	uint32_t x = base ? (g_evt_acl_l2cap - g_evt_l2cap_at_enc) : 0U;
	uint32_t e = shim_sem_ectx_total;

	if (p > 255U) { p = 255U; }
	if (g > 255U) { g = 255U; }
	if (x > 255U) { x = 255U; }
	if (e > 255U) { e = 255U; }
	*EVT_TRACE_RTC = (e << 24) | (x << 16) | (g << 8) | p;
}

/*  app が起動時に一度呼ぶ（.bss は0初期化だが SYNC マーカ転用を明示化）  */
void
esp_evt_trace_reset(void)
{
	g_evt_total = 0U;
	g_evt_ltk_req = 0U;
	g_evt_enc_chg = 0U;
	g_evt_enc_status = 0U;
	g_evt_ltk_hrt = 0U;
	g_evt_delta_us = 0U;
	g_evt_enc_hrt = 0U;
	g_evt_acl_put = 0U;
	g_evt_acl_get = 0U;
	g_evt_acl_l2cap = 0U;
	g_evt_acl_at_enc = 0U;
	g_evt_get_at_enc = 0U;
	g_evt_l2cap_at_enc = 0U;
	g_evt_conn_cmpl = 0U;
	g_evt_disc = 0U;
	g_evt_disc_reason = 0U;
	g_evt_cmd_cmpl = 0U;
	g_evt_cmd_status = 0U;
	g_evt_le_other = 0U;
	g_evt_seq_n = 0U;
#ifdef TOPPERS_C3_EVT_FAST_MAP
	g_tx_total = 0U;
	g_tx_smp = 0U;
	g_tx_att = 0U;
	g_tx_seq_n = 0U;
	tx_trace_fast_dump();
#endif
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
		/*
		 *  ★bond 機構の A/B 局在化（evidence-c3-04）：到着順と種別を記録。
		 *  0x3E（LE Meta）は subevent を上位ニブルへ畳んで 1 バイトに収める
		 *  （0xE1＝LE Conn Complete / 0xE5＝LTK Req / 0xEx＝その他 LE Meta）。
		 */
		{
			uint8_t	tag = code;

			if (code == 0x3EU) {
				tag = (uint8_t) (0xE0U | (p[2] & 0x0FU));
			}
			if (g_evt_seq_n < 24U) {
				g_evt_seq[g_evt_seq_n] = tag;
				g_evt_seq_n++;
			}
		}
		if (code == 0x05U) {			/* Disconnection Complete */
			g_evt_disc++;
			g_evt_disc_reason = p[5];	/* status,handle(2),reason */
		}
		else if (code == 0x0EU) {		/* Command Complete */
			g_evt_cmd_cmpl++;
		}
		else if (code == 0x0FU) {		/* Command Status */
			g_evt_cmd_status++;
		}
		else if (code == 0x3EU && (p[2] == 0x01U || p[2] == 0x0AU)) {
			/*  LE Connection Complete (0x01) *or* LE Enhanced Connection
			    Complete (0x0A).  ★実測(rc-c3 C0/2026-07-18)：esp-idf v5.5.4 の
			    C3 controller は «Enhanced»(0x0A) を出す＝旧条件 0x01 のみでは
			    over-determined 0 になる（seq に 0xea を観測・app CONN=1 と乖離）。
			    C5-10 §7.6 の «Enc Change v2» と同型の «新コード盲»．  */
			g_evt_conn_cmpl++;
		}
		else if (code == 0x3EU && p[2] != 0x05U) {	/* その他 LE Meta */
			g_evt_le_other++;
		}
		if (code == 0x08U || code == 0x59U) {	/* Encryption Change (0x08) or v2 (0x59) */
			g_evt_enc_chg++;
			g_evt_enc_status = p[2];	/* status バイト */
			g_evt_enc_hrt = fch_hrt();	/* Enc Change 到着時刻 */
			g_evt_acl_at_enc = g_evt_acl_put;	/* 暗号後 ACL 基点(put) */
			g_evt_get_at_enc = g_evt_acl_get;	/* 同(get) */
			g_evt_l2cap_at_enc = g_evt_acl_l2cap;	/* 同(l2cap) */
			if (g_evt_ltk_hrt != 0U) {
				g_evt_delta_us = g_evt_enc_hrt - g_evt_ltk_hrt;
			}
		}
		else if (code == 0x3EU && p[2] == 0x05U) {	/* LE Meta / LTK Request */
			g_evt_ltk_req++;
			if (g_evt_ltk_hrt == 0U) {
				g_evt_ltk_hrt = fch_hrt();	/* 初回 LTK Request 時刻 */
			}
		}
		evt_trace_pack();
#ifdef TOPPERS_C3_EVT_FAST_MAP
		/*  ★ここだけで地図を書く．ACL wrap（ble_mqueue_put/get・ble_l2cap_rx）は
		    «毎パケット» の hot path なので絶対に呼ばない——実測（evidence-c3-04 §5）：
		    そこから 20 語 dump を呼んだら **hal でも bond が落ちた**（C0a）＝計装が侵襲的になる。
		    HCI EVT は pairing 中せいぜい十数回なので 20 語 dump でも無視できる。  */
		evt_trace_fast_dump();
#endif
	}
	return __real_ble_hs_hci_evt_process(ev);
}

/*  --wrap 対象＝ACL RX を host rx_q(ble_hs_rx_q)へ積む点．呼出し数を数えて
    «暗号確立後» に ACL RX が届くかを局在化する（引数はポインタのみ＝void*）．
    acl_trace.c と同一シグネチャ（ESP32C3_BT_ACL_TRACE と同時ONは二重定義に
    なるため排他．本計装は EVT_TRACE 側でのみ有効）．  */
extern int __real_ble_mqueue_put(void *mq, void *evq, void *om);

int
__wrap_ble_mqueue_put(void *mq, void *evq, void *om)
{
	g_evt_acl_put++;
	evt_trace_pack();
	return __real_ble_mqueue_put(mq, evq, om);
}

/*  host が rx_q を drain した点（非NULL＝実際に1件取り出せた）．  */
extern void *__real_ble_mqueue_get(void *mq);

void *
__wrap_ble_mqueue_get(void *mq)
{
	void	*om = __real_ble_mqueue_get(mq);

	if (om != (void *) 0) {
		g_evt_acl_get++;
		evt_trace_pack();
	}
	return om;
}

/*  ACL が L2CAP へ回った点．  */
extern int __real_ble_l2cap_rx(void *conn, void *hdr, void *om,
							   void *out_rx_cb, void *out_reject_cid);

int
__wrap_ble_l2cap_rx(void *conn, void *hdr, void *om,
					void *out_rx_cb, void *out_reject_cid)
{
	g_evt_acl_l2cap++;
	evt_trace_pack();
	return __real_ble_l2cap_rx(conn, hdr, om, out_rx_cb, out_reject_cid);
}
