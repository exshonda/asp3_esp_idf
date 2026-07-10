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
 *		シリアルインタフェースドライバのチップ依存部（ESP32-C5用）
 *		（非TECS版専用）
 *
 *  arch/arm_m_gcc/rp2350/chip_serial.h からの流用．UART実体をRP2350
 *  UART(PL011系)からESP32-C5 UARTに置換．
 *
 *  コンソールの実体はコンパイル時に選択する（ESP32C5_CONSOLE＝
 *  chip.cmake参照）：
 *    - UART0（既定・QEMU）
 *    - USB Serial/JTAG（TOPPERS_ESP32C5_CONSOLE_USBJTAG定義時．
 *      UARTブリッジを持たないネイティブUSBボード用）
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#ifndef TOPPERS_CHIP_SERIAL_H
#define TOPPERS_CHIP_SERIAL_H

#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG
#include "esp32c5_usbjtag.h"
#else /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
#include "esp32c5_uart.h"
#endif /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  SIOドライバの初期化
 */
extern void sio_initialize(EXINF exinf);

/*
 *  SIOドライバの終了処理
 */
extern void sio_terminate(EXINF exinf);

/*
 *  SIOの割込みサービスルーチン
 */
extern void sio_isr(EXINF exinf);

/*
 *  SIOポートのオープン
 */
extern SIOPCB *sio_opn_por(ID siopid, EXINF exinf);

/*
 *  SIOポートのクローズ
 */
extern void sio_cls_por(SIOPCB *p_siopcb);

/*
 *  SIOポートへの文字送信
 */
extern bool_t sio_snd_chr(SIOPCB *p_siopcb, char c);

/*
 *  SIOポートからの文字受信
 */
extern int_t sio_rcv_chr(SIOPCB *p_siopcb);

/*
 *  SIOポートからのコールバックの許可
 */
extern void sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn);

/*
 *  SIOポートからのコールバックの禁止
 */
extern void sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn);

/*
 *  SIOポートからの送信可能コールバック
 */
extern void sio_irdy_snd(EXINF exinf);

/*
 *  SIOポートからの受信通知コールバック
 */
extern void sio_irdy_rcv(EXINF exinf);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CHIP_SERIAL_H */
