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
 *  arch/arm_m_gcc/rp2350/rp2350_uart.h からの流用．UART実体をRP2350
 *  UART(PL011系)からESP32-C5 UARTに置換．esp32c5.h がまだ無いため，
 *  UART関連のレジスタ定義はこのファイル内に自前定義する（プレフィッ
 *  クス ESP32C5_UART_）．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#ifndef TOPPERS_ESP32C5_UART_H
#define TOPPERS_ESP32C5_UART_H

#ifdef TOPPERS_OMIT_TECS

#include <sil.h>

/*
 *  ESP32-C5 UARTレジスタの番地の定義（UART0のベースアドレスからのオフ
 *  セット）．レジスタの詳細はHW_NOTES.md §3を参照．
 *  sil_rew_mem/sil_wrw_memに渡せるよう(uint32_t *)にキャストする
 *  （RP2350.hのレジスタマクロと同じ流儀）．
 */
#define ESP32C5_UART_FIFO(base)		((uint32_t *)((base) + 0x00U))	/* 送受信FIFO */
#define ESP32C5_UART_INT_RAW(base)		((uint32_t *)((base) + 0x04U))	/* 割込み生ステータス */
#define ESP32C5_UART_INT_ST(base)		((uint32_t *)((base) + 0x08U))	/* 割込みステータス（マスク後） */
#define ESP32C5_UART_INT_ENA(base)		((uint32_t *)((base) + 0x0CU))	/* 割込みイネーブル */
#define ESP32C5_UART_INT_CLR(base)		((uint32_t *)((base) + 0x10U))	/* 割込みクリア */
#define ESP32C5_UART_CLKDIV(base)		((uint32_t *)((base) + 0x14U))	/* ボーレート分周（未使用） */
#define ESP32C5_UART_STATUS(base)		((uint32_t *)((base) + 0x1CU))	/* FIFOカウント等のステータス */
#define ESP32C5_UART_CONF1(base)		((uint32_t *)((base) + 0x24U))	/* FIFO閾値等の設定 */

/*
 *  ESP32C5_UART_INT_{RAW,ST,ENA,CLR}の設定値
 */
#define ESP32C5_UART_INT_RXFIFO_FULL	UINT_C(0x00000001)	/* 受信FIFOフル */
#define ESP32C5_UART_INT_TXFIFO_EMPTY	UINT_C(0x00000002)	/* 送信FIFOエンプティ */

/*
 *  ESP32C5_UART_STATUSのフィールド（C3と異なりRX/TXとも8bit幅．
 *  esp-hal-3rdparty uart_reg.hで確認：UART_RXFIFO_CNT bit[7:0]・
 *  UART_TXFIFO_CNT bit[23:16]）
 */
#define ESP32C5_UART_STATUS_RXFIFO_CNT_MASK	UINT_C(0x000000ff)	/* bit[7:0] */
#define ESP32C5_UART_STATUS_TXFIFO_CNT_SHIFT	16U
#define ESP32C5_UART_STATUS_TXFIFO_CNT_MASK	UINT_C(0x00ff0000)	/* bit[23:16] */

/*
 *  ESP32C5_UART_CONF1のフィールド（C3と異なり各8bit幅．
 *  UART_RXFIFO_FULL_THRHD bit[7:0]・UART_TXFIFO_EMPTY_THRHD bit[15:8]）
 */
#define ESP32C5_UART_CONF1_RXFIFO_FULL_THRHD_MASK	UINT_C(0x000000ff)	/* bit[7:0] */
#define ESP32C5_UART_CONF1_TXFIFO_EMPTY_THRHD_SHIFT	8U
#define ESP32C5_UART_CONF1_TXFIFO_EMPTY_THRHD_MASK	UINT_C(0x0000ff00)	/* bit[15:8] */

/*
 *  送受信FIFOの深さ
 */
#define ESP32C5_UART_FIFO_DEPTH		128U

/*
 *  初期化時に設定するFIFO閾値
 *  （RXFIFO_FULL_THRHD=1：1バイト以上で受信割込み，
 *    TXFIFO_EMPTY_THRHD=2：送信FIFOが2バイト未満で送信割込み）
 */
#define ESP32C5_UART_RXFIFO_FULL_THRHD_INIVAL	1U
#define ESP32C5_UART_TXFIFO_EMPTY_THRHD_INIVAL	2U

/*
 *  SIOポート数の定義
 */
#define TNUM_SIOP		1		/* サポートするSIOポートの数 */

/*
 *  コールバックルーチンの識別番号
 */
#define SIO_RDY_SND		1U		/* 送信可能コールバック */
#define SIO_RDY_RCV		2U		/* 受信通知コールバック */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  SIOポート管理ブロックの定義
 */
typedef struct sio_port_control_block	SIOPCB;

/*
 *  プリミティブな送信／受信関数
 */

/*
 *  受信バッファに文字があるか？
 */
Inline bool_t
esp32c5_uart_getready(uintptr_t base)
{
	return((sil_rew_mem(ESP32C5_UART_STATUS(base))
					& ESP32C5_UART_STATUS_RXFIFO_CNT_MASK) != 0U);
}

/*
 *  送信バッファに空きがあるか？
 */
Inline bool_t
esp32c5_uart_putready(uintptr_t base)
{
	uint32_t	txcnt;

	txcnt = (sil_rew_mem(ESP32C5_UART_STATUS(base))
					& ESP32C5_UART_STATUS_TXFIFO_CNT_MASK)
						>> ESP32C5_UART_STATUS_TXFIFO_CNT_SHIFT;
	return(txcnt < ESP32C5_UART_FIFO_DEPTH);
}

/*
 *  受信した文字の取出し
 */
Inline char
esp32c5_uart_getchar(uintptr_t base)
{
	return((char) sil_rew_mem(ESP32C5_UART_FIFO(base)));
}

/*
 *  送信する文字の書込み
 */
Inline void
esp32c5_uart_putchar(uintptr_t base, char c)
{
	sil_wrw_mem(ESP32C5_UART_FIFO(base), (uint32_t) c);
}

/*
 *  シリアルインタフェースドライバに提供する機能
 */

/*
 *  SIOドライバの初期化
 */
extern void		esp32c5_uart_initialize(void);

/*
 *  SIOドライバの終了処理
 */
extern void		esp32c5_uart_terminate(void);

/*
 *  SIOの割込みサービスルーチン
 */
extern void		esp32c5_uart_isr(ID siopid);

/*
 *  SIOポートのオープン
 */
extern SIOPCB	*esp32c5_uart_opn_por(ID siopid, EXINF exinf);

/*
 *  SIOポートのクローズ
 */
extern void		esp32c5_uart_cls_por(SIOPCB *siopcb);

/*
 *  SIOポートへの文字送信
 */
extern bool_t	esp32c5_uart_snd_chr(SIOPCB *siopcb, char c);

/*
 *  SIOポートからの文字受信
 */
extern int_t	esp32c5_uart_rcv_chr(SIOPCB *siopcb);

/*
 *  SIOポートからのコールバックの許可
 */
extern void		esp32c5_uart_ena_cbr(SIOPCB *siopcb, uint_t cbrtn);

/*
 *  SIOポートからのコールバックの禁止
 */
extern void		esp32c5_uart_dis_cbr(SIOPCB *siopcb, uint_t cbrtn);

/*
 *  SIOポートからの送信可能コールバック
 */
extern void		esp32c5_uart_irdy_snd(EXINF exinf);

/*
 *  SIOポートからの受信通知コールバック
 */
extern void		esp32c5_uart_irdy_rcv(EXINF exinf);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_OMIT_TECS */
#endif /* TOPPERS_ESP32C5_UART_H */
