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
 *		ESP32-C5 USB Serial/JTAGコントローラ用 簡易SIOドライバ
 *		（非TECS版専用）
 *
 *  esp32c5_uart.c と同構造（UART実体をUSB Serial/JTAGに置換）．
 *  ハードウェアはリセット後デフォルトで動作しており，ボーレート等の
 *  設定は存在しない（USB CDC-ACM）．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#include <sil.h>
#include "target_syssvc.h"
#include "esp32c5_usbjtag.h"

/*
 *  SIOポート初期化ブロックの定義
 */
typedef struct sio_port_initialization_block {
	uintptr_t	base;			/* USB Serial/JTAGレジスタのベースアドレス */
} SIOPINIB;

/*
 *  SIOポート管理ブロックの定義
 */
struct sio_port_control_block {
	const SIOPINIB *siopinib;	/* SIOポート初期化ブロック */
	EXINF		exinf;			/* 拡張情報 */
	bool_t		opened;			/* オープン済み */
};

/*
 *  SIOポート初期化ブロック
 */
const SIOPINIB siopinib_table[TNUM_SIOP] = {
	{ ESP32C5_USBJTAG_BASE }
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
esp32c5_usbjtag_initialize(void)
{
	SIOPCB	*p_siopcb;
	uint_t	i;

	for (p_siopcb = siopcb_table, i = 0; i < TNUM_SIOP; p_siopcb++, i++) {
		p_siopcb->siopinib = &(siopinib_table[i]);
		p_siopcb->opened = false;
	}
}

/*
 *  SIOドライバの終了処理
 */
void
esp32c5_usbjtag_terminate(void)
{
	uint_t	i;

	for (i = 0; i < TNUM_SIOP; i++) {
		esp32c5_usbjtag_cls_por(&(siopcb_table[i]));
	}
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
esp32c5_usbjtag_opn_por(ID siopid, EXINF exinf)
{
	SIOPCB		*p_siopcb;
	uintptr_t	base;

	p_siopcb = get_siopcb(siopid);

	if (!(p_siopcb->opened)) {
		base = p_siopcb->siopinib->base;

		/*
		 *  全割込みの禁止とクリア（ハードウェアの動作設定は不要）
		 */
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base), 0U);
		sil_wrw_mem(ESP32C5_USBJTAG_INT_CLR(base),
					ESP32C5_USBJTAG_INT_OUT_RECV_PKT
						| ESP32C5_USBJTAG_INT_IN_EMPTY);

		p_siopcb->opened = true;
	}
	p_siopcb->exinf = exinf;
	return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
esp32c5_usbjtag_cls_por(SIOPCB *p_siopcb)
{
	uintptr_t	base;
	uint32_t	retry;

	if (p_siopcb->opened) {
		base = p_siopcb->siopinib->base;

		/*
		 *  送信FIFOが掃けるのを待つ（カーネル終了時の最後の出力を
		 *  失わないため）．ホストが読み出さない場合は空かないため，
		 *  リトライ上限を設けて打ち切る．
		 */
		for (retry = 100000U; retry > 0U; retry--) {
			if (esp32c5_usbjtag_putready(base)) {
				break;
			}
		}

		/*
		 *  全割込みの禁止
		 */
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base), 0U);

		p_siopcb->opened = false;
	}
}

/*
 *  SIOポートへの文字送信
 */
bool_t
esp32c5_usbjtag_snd_chr(SIOPCB *p_siopcb, char c)
{
	if (esp32c5_usbjtag_putready(p_siopcb->siopinib->base)) {
		esp32c5_usbjtag_putchar(p_siopcb->siopinib->base, c);
		return(true);
	}
	return(false);
}

/*
 *  SIOポートからの文字受信
 */
int_t
esp32c5_usbjtag_rcv_chr(SIOPCB *p_siopcb)
{
	if (esp32c5_usbjtag_getready(p_siopcb->siopinib->base)) {
		return((int_t)(uint8_t)
					esp32c5_usbjtag_getchar(p_siopcb->siopinib->base));
	}
	return(-1);
}

/*
 *  SIOポートからのコールバックの許可
 */
void
esp32c5_usbjtag_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	uintptr_t	base = p_siopcb->siopinib->base;

	switch (cbrtn) {
	case SIO_RDY_SND:
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base),
					sil_rew_mem(ESP32C5_USBJTAG_INT_ENA(base))
							| ESP32C5_USBJTAG_INT_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base),
					sil_rew_mem(ESP32C5_USBJTAG_INT_ENA(base))
							| ESP32C5_USBJTAG_INT_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートからのコールバックの禁止
 */
void
esp32c5_usbjtag_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	uintptr_t	base = p_siopcb->siopinib->base;

	switch (cbrtn) {
	case SIO_RDY_SND:
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base),
					sil_rew_mem(ESP32C5_USBJTAG_INT_ENA(base))
							& ~ESP32C5_USBJTAG_INT_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		sil_wrw_mem(ESP32C5_USBJTAG_INT_ENA(base),
					sil_rew_mem(ESP32C5_USBJTAG_INT_ENA(base))
							& ~ESP32C5_USBJTAG_INT_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートに対する割込み処理
 */
static void
esp32c5_usbjtag_isr_siop(SIOPCB *p_siopcb)
{
	uintptr_t	base = p_siopcb->siopinib->base;
	uint32_t	stat;

	stat = sil_rew_mem(ESP32C5_USBJTAG_INT_ST(base));

	if ((stat & ESP32C5_USBJTAG_INT_IN_EMPTY) != 0U) {
		/*
		 *  送信FIFOエンプティはイベント（送信完了時）として立つため，
		 *  クリアしてから送信可能コールバックルーチンを呼び出す．
		 */
		sil_wrw_mem(ESP32C5_USBJTAG_INT_CLR(base),
					ESP32C5_USBJTAG_INT_IN_EMPTY);
		esp32c5_usbjtag_irdy_snd(p_siopcb->exinf);
	}
	if ((stat & ESP32C5_USBJTAG_INT_OUT_RECV_PKT) != 0U) {
		/*
		 *  受信パケット到着はパケット毎のイベントのため，クリアした
		 *  うえでFIFO内の全データを引き取るまで受信通知コールバック
		 *  ルーチンを呼び出す（1回の呼出しで1文字読み出される）．
		 */
		sil_wrw_mem(ESP32C5_USBJTAG_INT_CLR(base),
					ESP32C5_USBJTAG_INT_OUT_RECV_PKT);
		while (esp32c5_usbjtag_getready(base)) {
			esp32c5_usbjtag_irdy_rcv(p_siopcb->exinf);
		}
	}
}

/*
 *  SIOの割込みサービスルーチン
 */
void
esp32c5_usbjtag_isr(ID siopid)
{
	esp32c5_usbjtag_isr_siop(get_siopcb(siopid));
}
