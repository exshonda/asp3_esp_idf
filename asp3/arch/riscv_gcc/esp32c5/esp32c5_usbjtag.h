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
 *  esp32c5_uart.h と同じAPI形（プレフィックスをesp32c5_usbjtag_に変更）
 *  のUSB CDC-ACMコンソール．UARTブリッジを持たないネイティブUSB接続の
 *  ボード（チップ内蔵のUSB Serial/JTAGでホストに303a:1001として見える）
 *  で，ホスト側の/dev/ttyACM*をそのままコンソールにする．
 *  ハードウェアはリセット後デフォルトで動作しており初期化不要．
 *
 *  ■ 送信（IN方向＝デバイス→ホスト）
 *  EP1レジスタへ書き込み（FIFO 64バイト），EP1_CONFのWR_DONEでパケット
 *  送出（フラッシュ）．WR_DONE後はホストが読み出すまでSERIAL_IN_EP_
 *  DATA_FREEが0になる．本ドライバは1文字毎にフラッシュする（コンソール
 *  用途では十分な速度）．
 *
 *  ■ ホスト未接続時の注意
 *  ホスト（端末プログラム）がパケットを読み出さないとDATA_FREEが0の
 *  まま戻らない．ポーリング出力（target_fput_log）はリトライ上限を
 *  設けて出力を捨てる（chip_serial.c参照）．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#ifndef TOPPERS_ESP32C5_USBJTAG_H
#define TOPPERS_ESP32C5_USBJTAG_H

#ifdef TOPPERS_OMIT_TECS

#include <sil.h>

/*
 *  USB Serial/JTAGレジスタの番地の定義（ベースアドレスからのオフセット）
 */
#define ESP32C5_USBJTAG_EP1(base)		((uint32_t *)((base) + 0x00U))	/* 送受信FIFO */
#define ESP32C5_USBJTAG_EP1_CONF(base)	((uint32_t *)((base) + 0x04U))	/* FIFO制御・状態 */
#define ESP32C5_USBJTAG_INT_RAW(base)	((uint32_t *)((base) + 0x08U))	/* 割込み生ステータス */
#define ESP32C5_USBJTAG_INT_ST(base)	((uint32_t *)((base) + 0x0CU))	/* 割込みステータス（マスク後） */
#define ESP32C5_USBJTAG_INT_ENA(base)	((uint32_t *)((base) + 0x10U))	/* 割込みイネーブル */
#define ESP32C5_USBJTAG_INT_CLR(base)	((uint32_t *)((base) + 0x14U))	/* 割込みクリア */

/*
 *  ESP32C5_USBJTAG_EP1_CONFのフィールド
 */
#define ESP32C5_USBJTAG_EP1_CONF_WR_DONE		UINT_C(0x00000001)	/* パケット送出（WT） */
#define ESP32C5_USBJTAG_EP1_CONF_IN_DATA_FREE	UINT_C(0x00000002)	/* 送信FIFOに空きあり（RO） */
#define ESP32C5_USBJTAG_EP1_CONF_OUT_DATA_AVAIL	UINT_C(0x00000004)	/* 受信データあり（RO） */

/*
 *  ESP32C5_USBJTAG_INT_{RAW,ST,ENA,CLR}の設定値
 */
#define ESP32C5_USBJTAG_INT_OUT_RECV_PKT	UINT_C(0x00000004)	/* 受信パケット到着 */
#define ESP32C5_USBJTAG_INT_IN_EMPTY		UINT_C(0x00000008)	/* 送信FIFOエンプティ */

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
esp32c5_usbjtag_getready(uintptr_t base)
{
	return((sil_rew_mem(ESP32C5_USBJTAG_EP1_CONF(base))
					& ESP32C5_USBJTAG_EP1_CONF_OUT_DATA_AVAIL) != 0U);
}

/*
 *  送信バッファに空きがあるか？
 */
Inline bool_t
esp32c5_usbjtag_putready(uintptr_t base)
{
	return((sil_rew_mem(ESP32C5_USBJTAG_EP1_CONF(base))
					& ESP32C5_USBJTAG_EP1_CONF_IN_DATA_FREE) != 0U);
}

/*
 *  受信した文字の取出し
 */
Inline char
esp32c5_usbjtag_getchar(uintptr_t base)
{
	return((char) sil_rew_mem(ESP32C5_USBJTAG_EP1(base)));
}

/*
 *  送信する文字の書込み（1文字毎にパケットとして送出する）
 */
Inline void
esp32c5_usbjtag_putchar(uintptr_t base, char c)
{
	sil_wrw_mem(ESP32C5_USBJTAG_EP1(base), (uint32_t) c);
	sil_wrw_mem(ESP32C5_USBJTAG_EP1_CONF(base),
				ESP32C5_USBJTAG_EP1_CONF_WR_DONE);
}

/*
 *  シリアルインタフェースドライバに提供する機能
 */
extern void		esp32c5_usbjtag_initialize(void);
extern void		esp32c5_usbjtag_terminate(void);
extern void		esp32c5_usbjtag_isr(ID siopid);
extern SIOPCB	*esp32c5_usbjtag_opn_por(ID siopid, EXINF exinf);
extern void		esp32c5_usbjtag_cls_por(SIOPCB *siopcb);
extern bool_t	esp32c5_usbjtag_snd_chr(SIOPCB *siopcb, char c);
extern int_t	esp32c5_usbjtag_rcv_chr(SIOPCB *siopcb);
extern void		esp32c5_usbjtag_ena_cbr(SIOPCB *siopcb, uint_t cbrtn);
extern void		esp32c5_usbjtag_dis_cbr(SIOPCB *siopcb, uint_t cbrtn);
extern void		esp32c5_usbjtag_irdy_snd(EXINF exinf);
extern void		esp32c5_usbjtag_irdy_rcv(EXINF exinf);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_OMIT_TECS */
#endif /* TOPPERS_ESP32C5_USBJTAG_H */
