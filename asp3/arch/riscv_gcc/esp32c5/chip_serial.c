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
 *  arch/arm_m_gcc/rp2350/chip_serial.c からの流用．UART実体をRP2350
 *  UART(PL011系)からESP32-C5 UARTに置換．
 *  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
 *  本ファイルはチップ非依存の内容のため識別子置換のみで移植．）
 */

#include <kernel.h>
#include <t_syslog.h>
#include "target_syssvc.h"
#include "chip_serial.h"

/*
 *  コンソール実体（UART0／USB Serial/JTAG）の選択（chip_serial.h参照）
 */
#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG
#define ESP32C5_SIO(name)	esp32c5_usbjtag_##name
#else /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
#define ESP32C5_SIO(name)	esp32c5_uart_##name
#endif /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */

/*
 *  低レベル出力用のSIOポート管理ブロック
 */
static SIOPCB	*p_siopcb_target_fput;

/*
 *  SIOドライバの初期化
 */
void
sio_initialize(EXINF exinf)
{
	ESP32C5_SIO(initialize)();
}

/*
 *  SIOドライバの終了処理
 */
void
sio_terminate(EXINF exinf)
{
	ESP32C5_SIO(terminate)();
}

/*
 *  SIOの割込みサービスルーチン
 */
void
sio_isr(EXINF exinf)
{
	ESP32C5_SIO(isr)((ID) exinf);
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
sio_opn_por(ID siopid, EXINF exinf)
{
	SIOPCB	*p_siopcb;

	/*
	 *  デバイス依存のオープン処理
	 */
	p_siopcb = ESP32C5_SIO(opn_por)(siopid, exinf);

	/*
	 *  低レベル出力用のSIOポートを記録する．
	 */
	if (siopid == SIOPID_FPUT) {
		p_siopcb_target_fput = p_siopcb;
	}

	/*
	 *  SIOの割込みマスクを解除する．
	 */
	(void) ena_int(INTNO_SIO);
	return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
sio_cls_por(SIOPCB *p_siopcb)
{
	/*
	 *  デバイス依存のクローズ処理
	 */
	ESP32C5_SIO(cls_por)(p_siopcb);

	/*
	 *  SIOの割込みをマスクする．
	 */
	(void) dis_int(INTNO_SIO);
}

/*
 *  SIOポートへの文字送信
 */
bool_t
sio_snd_chr(SIOPCB *p_siopcb, char c)
{
	return(ESP32C5_SIO(snd_chr)(p_siopcb, c));
}

/*
 *  SIOポートからの文字受信
 */
int_t
sio_rcv_chr(SIOPCB *p_siopcb)
{
	return(ESP32C5_SIO(rcv_chr)(p_siopcb));
}

/*
 *  SIOポートからのコールバックの許可
 */
void
sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	ESP32C5_SIO(ena_cbr)(p_siopcb, cbrtn);
}

/*
 *  SIOポートからのコールバックの禁止
 */
void
sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	ESP32C5_SIO(dis_cbr)(p_siopcb, cbrtn);
}

/*
 *  SIOポートからの送信可能コールバック
 */
void
ESP32C5_SIO(irdy_snd)(EXINF exinf)
{
	sio_irdy_snd(exinf);
}

/*
 *  SIOポートからの受信通知コールバック
 */
void
ESP32C5_SIO(irdy_rcv)(EXINF exinf)
{
	sio_irdy_rcv(exinf);
}

/*
 *		システムログの低レベル出力
 */

/*
 *  SIOポートへのポーリング出力
 *
 *  USB Serial/JTAGコンソールでは，ホスト（端末プログラム）がパケットを
 *  読み出さない限り送信FIFOが空かないため，リトライ上限を設けて出力を
 *  捨てる（ホスト未接続でシステムが固まるのを防ぐ）．
 */
static void
esp32c5_sio_fput(char c)
{
#ifdef TOPPERS_ESP32C5_CONSOLE_USBJTAG
	uint_t retry;

	for (retry = 5000U; retry > 0U; retry--) {
		if (ESP32C5_SIO(snd_chr)(p_siopcb_target_fput, c)) {
			break;
		}
		sil_dly_nse(100);
	}
#else /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
	/*
	 *  送信できるまでポーリング
	 */
	while (!(ESP32C5_SIO(snd_chr)(p_siopcb_target_fput, c))) {
		sil_dly_nse(100);
	}
#endif /* TOPPERS_ESP32C5_CONSOLE_USBJTAG */
}

/*
 *  SIOポートへの文字出力
 */
void
target_fput_log(char c)
{
	if (c == '\n') {
		esp32c5_sio_fput('\r');
	}
	esp32c5_sio_fput(c);
}
