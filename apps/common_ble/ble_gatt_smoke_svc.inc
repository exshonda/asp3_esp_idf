/*
 *  ble_gatt_smoke_svc.inc — ble_host_smoke の自前GATTサービス（共有）
 *
 *  dedup Tier2c（軽量版）：3チップの ble_host_smoke{,_c5,_c6}.c で «逐語同一»
 *  だった自前GATTサーバ（0xABF0 サービス＋0xABF1-4 特性＋3アクセスコールバック）
 *  を1本に集約する．各 app は «契約シンボル» を定義してから相対 include する：
 *    #include "../common_ble/ble_gatt_smoke_svc.inc"
 *
 *  ★app が include «前» に用意する契約：
 *    - グローバル（本 .inc が参照）：
 *        volatile uint32_t g_notify_counter;   （notify カウンタ）
 *        uint32_t          g_write_count;      （WRITE 受信回数）
 *        uint32_t          g_write_last;       （最終 WRITE の len<<8|first）
 *        uint16_t          g_notify_val_handle;（0xABF2 の val_handle）
 *    - マクロ：
 *        BLE_WRITE_MARK_ADDR   … WRITE 時に書く RTC マーカ番地（チップ固有）
 *        BLE_SMOKE_LOG_TAG     … syslog の接頭タグ文字列（例 "ble_host_smoke"）
 *        BLE_SMOKE_HAS_ENC_CHR … 定義時のみ 0xABF4（暗号必須READ）を公開
 *                                （＝チップの BT_SM が有効なとき app 側で定義）
 *
 *  D-2d：自前GATTサービス（S3 BT-5 を «正» として逐語移植）．
 *    service 0xABF0（PRIMARY）
 *      chr 0xABF1  READ            固定値 "BT4-OK"（S3と同一．42 54 34 2d 4f 4b）
 *      chr 0xABF2  READ | NOTIFY   32bit LE カウンタ（subscribe中のみ周期送出）
 *      chr 0xABF3  WRITE           受信値をログ＋RTCマーカ＋putカウンタ
 *      chr 0xABF4  READ(暗号必須)  BLE_SMOKE_HAS_ENC_CHR 定義時のみ（bond トリガ入口）
 */

static int
gatt_read_access(uint16_t conn_handle, uint16_t attr_handle,
				 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	static const char	val[] = "BT4-OK";

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		return (os_mbuf_append(ctxt->om, val, sizeof(val) - 1) == 0
				? 0 : BLE_ATT_ERR_INSUFFICIENT_RES);
	}
	return BLE_ATT_ERR_UNLIKELY;
}

/*  notify特性（0xABF2）のREAD：現在のカウンタ値（notifyペイロードと同形式）  */
static int
gatt_notify_access(uint16_t conn_handle, uint16_t attr_handle,
				   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	uint32_t	v = g_notify_counter;
	uint8_t		buf[4];

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
		buf[0] = (uint8_t) (v & 0xffU);
		buf[1] = (uint8_t) ((v >> 8) & 0xffU);
		buf[2] = (uint8_t) ((v >> 16) & 0xffU);
		buf[3] = (uint8_t) ((v >> 24) & 0xffU);
		return (os_mbuf_append(ctxt->om, buf, sizeof(buf)) == 0
				? 0 : BLE_ATT_ERR_INSUFFICIENT_RES);
	}
	return BLE_ATT_ERR_UNLIKELY;
}

/*  write特性（0xABF3）：受信データをログ＋RTCマーカ＋putカウンタ加算  */
static int
gatt_write_access(uint16_t conn_handle, uint16_t attr_handle,
				  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	uint16_t	len;
	uint8_t		first = 0U;

	(void) conn_handle;
	(void) attr_handle;
	(void) arg;
	if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
		len = OS_MBUF_PKTLEN(ctxt->om);
		if (len > 0U) {
			(void) os_mbuf_copydata(ctxt->om, 0, 1, &first);
		}
		g_write_count++;
		g_write_last = ((uint32_t) len << 8) | (uint32_t) first;
		sil_wrw_mem(BLE_WRITE_MARK_ADDR,
					0x77170000UL
					| ((g_write_count & 0xffUL) << 8)
					| ((uint32_t) first & 0xffUL));
		syslog(LOG_NOTICE,
			   BLE_SMOKE_LOG_TAG ": GATT WRITE len=%d first=0x%02x count=%d",
			   (int_t) len, (int_t) first, (int_t) g_write_count);
		return 0;
	}
	return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def custom_chrs[] = {
	{
		.uuid = BLE_UUID16_DECLARE(0xABF1),
		.access_cb = gatt_read_access,
		.flags = BLE_GATT_CHR_F_READ,
	},
	{
		.uuid = BLE_UUID16_DECLARE(0xABF2),
		.access_cb = gatt_notify_access,
		.val_handle = &g_notify_val_handle,
		.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
	},
	{
		.uuid = BLE_UUID16_DECLARE(0xABF3),
		.access_cb = gatt_write_access,
		.flags = BLE_GATT_CHR_F_WRITE,
	},
#ifdef BLE_SMOKE_HAS_ENC_CHR
	/*  D-2d(bond確認)：暗号必須 READ 特性（0xABF4）．未ペアの central が
	    これを READ すると NimBLE が insufficient-authentication を返し，
	    central が pairing/bonding を «決定論的に» 開始する＝スマホ/BlueZ
	    双方で bond を強制トリガして PAIRING_COMPLETE の status を採取する
	    ための入口（暗号確立後は "BT4-OK" を平文READと同じく返す）．
	    ★これが読めた事実が «bond/LTK 暗号が end-to-end で実効» の物証．  */
	{
		.uuid = BLE_UUID16_DECLARE(0xABF4),
		.access_cb = gatt_read_access,
		.flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
	},
#endif
	{ 0 }	/* 終端 */
};

static const struct ble_gatt_svc_def custom_svcs[] = {
	{
		.type = BLE_GATT_SVC_TYPE_PRIMARY,
		.uuid = BLE_UUID16_DECLARE(0xABF0),
		.characteristics = custom_chrs,
	},
	{ 0 }	/* 終端 */
};
