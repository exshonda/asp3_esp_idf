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
 *  システムサービスのターゲット依存部（ESP32-C5用）
 *
 *  esp32c6版（asp3_core/target/esp32c6_espidf/target_syssvc.h相当）
 *  からのコピー・C5対応。INTNO_SIOは+1ずらしなし（ASP32-C5のCPU割込み線
 *  番号そのまま．CLIC内部番号への変換はclic_kernel_impl.hに閉じ込めて
 *  おり，本ファイル・target層からは今まで通りINTNO=1〜31のみが見える）。
 */

#ifndef TOPPERS_TARGET_SYSSVC_H
#define TOPPERS_TARGET_SYSSVC_H

#ifdef TOPPERS_OMIT_TECS

#include "esp32c5.h"
#include "chip_serial.h"

/*
 *  ターゲットシステムのハードウェア資源の定義
 */
#define TARGET_NAME "ESP32-C5"

/*
 *  低レベル出力
 */
extern void target_fput_log(char c);

/*
 *  使用するUART（UART0）
 */
#define SIO_UART_BASE		ESP32C5_UART0_BASE			/* UARTのベース番地 */

/*
 *  UART割込み（ASP3の割込み番号INTNO = ESP32-C5のCPU割込み線番号そのまま）
 *
 *  線1〜15は将来のWi-Fi shim（フェーズ2b）がesp_wifi_adapter.cの
 *  set_intr_wrapper等で動的に使う予定のため予約する（C6と同じ退避
 *  パターン．docs/c5-port-design.md §5.2参照）。線16〜18はtarget_timer.h
 *  ・target_test.hが使用する。
 */
#define INTNO_SIO		17							/* UART割込み番号（Wi-Fi shim退避のためのオフセット） */
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
