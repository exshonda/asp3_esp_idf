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
 *		ESP32-C5 UART用 簡易SIOドライバ（非TECS版専用）
 *
 *  arch/arm_m_gcc/rp2350/rp2350_uart.c からの流用．UART実体をRP2350
 *  UART(PL011系)からESP32-C5 UARTに置換．ESP32-C5のUART0はROMブート
 *  ローダによって115200bps・データ長8ビット等の設定済みで起動するため，
 *  クロックゲート選択・UARTのリセット・ボーレート設定は行わない（RP2350
 *  版にあったこれらの処理は削除）．オープン時にはFIFO閾値の設定と割込
 *  みのマスク・クリアのみを行う．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#include <sil.h>
#include "target_syssvc.h"
#include "esp32c5_uart.h"

/*
 *  SIOポート初期化ブロックの定義
 */
typedef struct sio_port_initialization_block {
	uintptr_t	base;			/* UARTレジスタのベースアドレス */
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
	{ SIO_UART_BASE }
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
esp32c5_uart_initialize(void)
{
	SIOPCB	*p_siopcb;
	uint_t	i;

	/*
	 *  SIOポート管理ブロックの初期化
	 */
	for (p_siopcb = siopcb_table, i = 0; i < TNUM_SIOP; p_siopcb++, i++) {
		p_siopcb->siopinib = &(siopinib_table[i]);
		p_siopcb->opened = false;
	}
}

/*
 *  SIOドライバの終了処理
 */
void
esp32c5_uart_terminate(void)
{
	uint_t	i;

	/*
	 *  オープンされているSIOポートのクローズ
	 */
	for (i = 0; i < TNUM_SIOP; i++) {
		esp32c5_uart_cls_por(&(siopcb_table[i]));
	}
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
esp32c5_uart_opn_por(ID siopid, EXINF exinf)
{
	SIOPCB		*p_siopcb;
	uintptr_t	base;
	uint32_t	conf1;

	p_siopcb = get_siopcb(siopid);

	if (!(p_siopcb->opened)) {
		/*
		 *  既にオープンしている場合は，二重にオープンしない．
		 */
		base = p_siopcb->siopinib->base;

		/*
		 *  全割込みの禁止とクリア
		 *  （ボーレート等の動作設定はROMが済ませているため行わない）
		 */
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base), 0U);
		sil_wrw_mem(ESP32C5_UART_INT_CLR(base),
					ESP32C5_UART_INT_RXFIFO_FULL
						| ESP32C5_UART_INT_TXFIFO_EMPTY);

		/*
		 *  受信FIFOフル閾値・送信FIFOエンプティ閾値の設定
		 *  （他のビットを壊さないようにread-modify-writeする）
		 */
		conf1 = sil_rew_mem(ESP32C5_UART_CONF1(base));
		conf1 &= ~(ESP32C5_UART_CONF1_RXFIFO_FULL_THRHD_MASK
						| ESP32C5_UART_CONF1_TXFIFO_EMPTY_THRHD_MASK);
		conf1 |= (ESP32C5_UART_RXFIFO_FULL_THRHD_INIVAL
						& ESP32C5_UART_CONF1_RXFIFO_FULL_THRHD_MASK);
		conf1 |= ((ESP32C5_UART_TXFIFO_EMPTY_THRHD_INIVAL
						<< ESP32C5_UART_CONF1_TXFIFO_EMPTY_THRHD_SHIFT)
						& ESP32C5_UART_CONF1_TXFIFO_EMPTY_THRHD_MASK);
		sil_wrw_mem(ESP32C5_UART_CONF1(base), conf1);

		p_siopcb->opened = true;
	}
	p_siopcb->exinf = exinf;
	return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
esp32c5_uart_cls_por(SIOPCB *p_siopcb)
{
	uintptr_t	base;
	uint32_t	txcnt;

	if (p_siopcb->opened) {
		base = p_siopcb->siopinib->base;

		/*
		 *  送信FIFOが掃けるのを待つ（待たずに割込みをディスエーブル
		 *  するとカーネル終了時の最後の出力（TAPサマリ行等）が失わ
		 *  れる）
		 */
		do {
			txcnt = (sil_rew_mem(ESP32C5_UART_STATUS(base))
							& ESP32C5_UART_STATUS_TXFIFO_CNT_MASK)
								>> ESP32C5_UART_STATUS_TXFIFO_CNT_SHIFT;
		} while (txcnt != 0U);

		/*
		 *  全割込みの禁止
		 */
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base), 0U);

		p_siopcb->opened = false;
	}
}

/*
 *  SIOポートへの文字送信
 */
bool_t
esp32c5_uart_snd_chr(SIOPCB *p_siopcb, char c)
{
	if (esp32c5_uart_putready(p_siopcb->siopinib->base)) {
		esp32c5_uart_putchar(p_siopcb->siopinib->base, c);
		return(true);
	}
	return(false);
}

/*
 *  SIOポートからの文字受信
 */
int_t
esp32c5_uart_rcv_chr(SIOPCB *p_siopcb)
{
	if (esp32c5_uart_getready(p_siopcb->siopinib->base)) {
		return((int_t)(uint8_t) esp32c5_uart_getchar(p_siopcb->siopinib->base));
	}
	return(-1);
}

/*
 *  SIOポートからのコールバックの許可
 *
 *  ESP32-C5にはRP2350のようなアトミックアクセスエイリアス（SET/CLR）
 *  が確認できていないため，sil_orw/sil_clrwは使わず，read-modify-write
 *  で明示的にビット操作する．
 */
void
esp32c5_uart_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	uintptr_t	base = p_siopcb->siopinib->base;

	switch (cbrtn) {
	case SIO_RDY_SND:
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base),
					sil_rew_mem(ESP32C5_UART_INT_ENA(base))
							| ESP32C5_UART_INT_TXFIFO_EMPTY);
		break;
	case SIO_RDY_RCV:
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base),
					sil_rew_mem(ESP32C5_UART_INT_ENA(base))
							| ESP32C5_UART_INT_RXFIFO_FULL);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートからのコールバックの禁止
 */
void
esp32c5_uart_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	uintptr_t	base = p_siopcb->siopinib->base;

	switch (cbrtn) {
	case SIO_RDY_SND:
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base),
					sil_rew_mem(ESP32C5_UART_INT_ENA(base))
							& ~ESP32C5_UART_INT_TXFIFO_EMPTY);
		break;
	case SIO_RDY_RCV:
		sil_wrw_mem(ESP32C5_UART_INT_ENA(base),
					sil_rew_mem(ESP32C5_UART_INT_ENA(base))
							& ~ESP32C5_UART_INT_RXFIFO_FULL);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートに対する割込み処理
 */
static void
esp32c5_uart_isr_siop(SIOPCB *p_siopcb)
{
	uintptr_t	base = p_siopcb->siopinib->base;
	uint32_t	stat;

	stat = sil_rew_mem(ESP32C5_UART_INT_ST(base));

	if ((stat & ESP32C5_UART_INT_TXFIFO_EMPTY) != 0U) {
		/*
		 *  送信可能コールバックルーチンを呼び出す．TXFIFO_EMPTYは
		 *  送信データが無い限り条件が続く（level型）ため，これ以上
		 *  送信するデータが無い場合はコールバック側（sio_dis_cbr経
		 *  由）でINT_ENAのビットを落とす必要がある．
		 */
		esp32c5_uart_irdy_snd(p_siopcb->exinf);
	}
	if ((stat & ESP32C5_UART_INT_RXFIFO_FULL) != 0U) {
		/*
		 *  受信通知コールバックルーチンを呼び出す．RXFIFO_FULLは
		 *  FIFOから読み出せば条件が消えるが，念のためINT_CLRへの
		 *  書込みも行っておく．
		 */
		sil_wrw_mem(ESP32C5_UART_INT_CLR(base), ESP32C5_UART_INT_RXFIFO_FULL);
		esp32c5_uart_irdy_rcv(p_siopcb->exinf);
	}
}

/*
 *  SIOの割込みサービスルーチン
 */
void
esp32c5_uart_isr(ID siopid)
{
	esp32c5_uart_isr_siop(get_siopcb(siopid));
}
