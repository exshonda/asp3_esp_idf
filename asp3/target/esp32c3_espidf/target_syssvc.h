/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  システムサービスのターゲット依存部（ESP32-C3用）
 *
 *  pico2_riscv_gcc版からの流用。rpi_pico.h/RP2350.hはesp32c3.hに置換．
 *  INTNO_SIOは+1ずらしなし（ESP32-C3のCPU割込み線番号そのまま）．
 */

#ifndef TOPPERS_TARGET_SYSSVC_H
#define TOPPERS_TARGET_SYSSVC_H

#ifdef TOPPERS_OMIT_TECS

#include "esp32c3.h"
#include "chip_serial.h"

/*
 *  ターゲットシステムのハードウェア資源の定義
 */
#define TARGET_NAME "ESP32-C3"

/*
 *  低レベル出力
 */
extern void target_fput_log(char c);

/*
 *  使用するUART（UART0）
 */
#define SIO_UART_BASE		ESP32C3_UART0_BASE			/* UARTのベース番地 */

/*
 *  UART割込み（ASP3の割込み番号INTNO = ESP32-C3のCPU割込み線番号そのまま）
 */
#define INTNO_SIO		17							/* UART割込み番号 */
#define ISRPRI_SIO		1							/* UART ISR優先度 */
#define INTPRI_SIO		(-2)						/* UART割込み優先度 */
#define INTATR_SIO		TA_NULL						/* UART割込み属性 */

/*
 *  シリアルポート数の定義
 */
#define TNUM_PORT	1

/*
 *  低レベル出力用のSIOポートID
 */
#define SIOPID_FPUT 1

#endif /* TOPPERS_OMIT_TECS */

/*
 *  コアで共通な定義
 */
#include "core_syssvc.h"

#endif /* TOPPERS_TARGET_SYSSVC_H */
