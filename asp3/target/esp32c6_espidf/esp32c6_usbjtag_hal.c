/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアを TOPPERS ライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソフ
 *  トウェアは無保証で提供される．
 *
 */

/*
 *		ESP32-C6 USB Serial/JTAGコントローラ用 簡易SIOドライバ
 *		（esp-hal LL層版・非TECS版専用）
 *
 *  asp3_core の arch/riscv_gcc/esp32c6/esp32c6_usbjtag.c（レジスタ直
 *  叩き版）を，esp-hal-3rdparty の LL 層（hal/usb_serial_jtag_ll.h＝
 *  static inline のレジスタ薄層・RTOS非依存）で置き換えたもの
 *  （Phase B-1：esp-hal統合の実証）．公開シンボル（esp32c6_usbjtag_*）
 *  は同一のため，chip_serial.c（asp3_core側）はそのままリンクできる．
 */

#include <sil.h>
#include "target_syssvc.h"
#include "esp32c6_usbjtag.h"
#include "hal/usb_serial_jtag_ll.h"

/*
 *  SIOポート管理ブロックの定義（LL層はデバイス単一のためbaseを持たない）
 */
struct sio_port_control_block {
	EXINF		exinf;			/* 拡張情報 */
	bool_t		opened;			/* オープン済み */
};

/*
 *  SIOポート管理ブロックのエリア
 */
SIOPCB	siopcb_table[TNUM_SIOP];

/*
 *  SIOポートIDから管理ブロックを取り出すためのマクロ
 */
#define INDEX_SIOP(siopid)	((uint_t)((siopid) - 1))
#define get_siopcb(siopid)	(&(siopcb_table[INDEX_SIOP(siopid)]))

/*
 *  SIOドライバの初期化
 */
void
esp32c6_usbjtag_initialize(void)
{
	SIOPCB	*p_siopcb;
	uint_t	i;

	for (p_siopcb = siopcb_table, i = 0; i < TNUM_SIOP; p_siopcb++, i++) {
		p_siopcb->opened = false;
	}
}

/*
 *  SIOドライバの終了処理
 */
void
esp32c6_usbjtag_terminate(void)
{
	uint_t	i;

	for (i = 0; i < TNUM_SIOP; i++) {
		esp32c6_usbjtag_cls_por(&(siopcb_table[i]));
	}
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
esp32c6_usbjtag_opn_por(ID siopid, EXINF exinf)
{
	SIOPCB	*p_siopcb;

	p_siopcb = get_siopcb(siopid);

	if (!(p_siopcb->opened)) {
		/*
		 *  全割込みの禁止とクリア（ハードウェアの動作設定は不要）
		 */
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);

		p_siopcb->opened = true;
	}
	p_siopcb->exinf = exinf;
	return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
esp32c6_usbjtag_cls_por(SIOPCB *p_siopcb)
{
	uint32_t	retry;

	if (p_siopcb->opened) {
		/*
		 *  送信FIFOが掃けるのを待つ（ホストが読み出さない場合は
		 *  空かないため，リトライ上限を設けて打ち切る）
		 */
		for (retry = 100000U; retry > 0U; retry--) {
			if (usb_serial_jtag_ll_txfifo_writable() != 0) {
				break;
			}
		}

		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);

		p_siopcb->opened = false;
	}
}

/*
 *  SIOポートへの文字送信（1文字毎にパケットとして送出する）
 */
bool_t
esp32c6_usbjtag_snd_chr(SIOPCB *p_siopcb, char c)
{
	if (usb_serial_jtag_ll_txfifo_writable() != 0) {
		usb_serial_jtag_ll_write_txfifo((const uint8_t *) &c, 1U);
		usb_serial_jtag_ll_txfifo_flush();
		return(true);
	}
	return(false);
}

/*
 *  SIOポートからの文字受信
 */
int_t
esp32c6_usbjtag_rcv_chr(SIOPCB *p_siopcb)
{
	uint8_t	c;

	if (usb_serial_jtag_ll_rxfifo_data_available() != 0) {
		(void) usb_serial_jtag_ll_read_rxfifo(&c, 1U);
		return((int_t) c);
	}
	return(-1);
}

/*
 *  SIOポートからのコールバックの許可
 */
void
esp32c6_usbjtag_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	switch (cbrtn) {
	case SIO_RDY_SND:
		usb_serial_jtag_ll_ena_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		usb_serial_jtag_ll_ena_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートからのコールバックの禁止
 */
void
esp32c6_usbjtag_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	switch (cbrtn) {
	case SIO_RDY_SND:
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートに対する割込み処理
 */
static void
esp32c6_usbjtag_isr_siop(SIOPCB *p_siopcb)
{
	uint32_t	stat;

	stat = usb_serial_jtag_ll_get_intsts_mask();

	if ((stat & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) != 0U) {
		/*
		 *  送信FIFOエンプティはイベント（送信完了時）として立つため，
		 *  クリアしてから送信可能コールバックルーチンを呼び出す．
		 */
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		esp32c6_usbjtag_irdy_snd(p_siopcb->exinf);
	}
	if ((stat & USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT) != 0U) {
		/*
		 *  受信パケット到着はパケット毎のイベントのため，クリアした
		 *  うえでFIFO内の全データを引き取るまで受信通知コールバック
		 *  ルーチンを呼び出す（1回の呼出しで1文字読み出される）．
		 */
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		while (usb_serial_jtag_ll_rxfifo_data_available() != 0) {
			esp32c6_usbjtag_irdy_rcv(p_siopcb->exinf);
		}
	}
}

/*
 *  SIOの割込みサービスルーチン
 */
void
esp32c6_usbjtag_isr(ID siopid)
{
	esp32c6_usbjtag_isr_siop(get_siopcb(siopid));
}
