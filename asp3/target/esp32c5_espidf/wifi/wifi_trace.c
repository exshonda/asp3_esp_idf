/*
 *  DIAGNOSTIC (temporary，実施16)：C5 PHY校正ハング調査専用の
 *  regi2cトランザクション（write/write_mask/read/read_mask）トレース。
 *
 *  机上調査（実施16）で，C5のlibphy.a（IDF v6.1／v9世代）は
 *  `phy_i2c_writeReg`/`phy_i2c_writeReg_Mask`/`phy_i2c_readReg`/
 *  `phy_i2c_readReg_Mask`が**libphy.a内の通常の大域リンケージ関数**
 *  として実在し，`phy_rx_cal.o`・`phy_analog_cal.o`・`phy_rfpll.o`等
 *  他の.oから`U`（外部参照）で呼ばれていることを`nm`・`objdump -dr`
 *  （relocation `R_RISCV_CALL`）で確認した——C6のようなROM常駐関数
 *  ポインタテーブル（`g_phyFuns`）経由の間接呼出しではない。よって
 *  C6実施23のようなテーブルパッチは不要で，`-Wl,--wrap`が直接効く。
 *
 *  呼出し規約（`objdump -dr`で実測確認済み）：
 *    phy_i2c_writeReg(block, host_id, reg_add, data)            : a0-a3
 *    phy_i2c_writeReg_Mask(block, host_id, reg_add, msb, lsb, data) : a0-a5
 *    phy_i2c_readReg(block, host_id, reg_add) -> uint8_t          : a0-a2
 *    phy_i2c_readReg_Mask(block, host_id, reg_add, msb, lsb) -> uint8_t : a0-a4
 *
 *  ゲート＝`ESP32C5_WIFI_REGI2C_TRACE`（esp_wifi.cmake，既定OFF）。
 *  Not for permanent use．docs/c5-bringup.md 実施16参照。
 */
#include <t_syslog.h>
#include <string.h>
#include "wifi_trace.h"
#include "esp_shim.h"
#include "target_syssvc.h"	/* target_fput_log（実施24：PD_TOP A/B用の直接出力） */

#define WIFI_REGI2C_SIZE 2048

wifi_regi2c_t	wifi_regi2c[WIFI_REGI2C_SIZE];
volatile uint32_t wifi_regi2c_pos;

void
wifi_regi2c_reset(void)
{
	wifi_regi2c_pos = 0U;
	memset(wifi_regi2c, 0, sizeof(wifi_regi2c));
}

void
wifi_regi2c_dump_count(void)
{
	syslog(LOG_NOTICE, "wifi_regi2c_cnt: total=%d", (int_t)wifi_regi2c_pos);
}

void
wifi_regi2c_dump_addr(void)
{
	syslog(LOG_NOTICE, "wifi_regi2c_addr: arr=%08x pos=%08x entsz=%d cap=%d",
		   (unsigned int)(uintptr_t)wifi_regi2c,
		   (unsigned int)(uintptr_t)&wifi_regi2c_pos,
		   (int_t)sizeof(wifi_regi2c_t), (int_t)WIFI_REGI2C_SIZE);
}

static void
wifi_regi2c_push(uint8_t op, uint8_t block, uint8_t host_id, uint8_t reg_add,
				  uint8_t msb, uint8_t lsb, uint8_t data, uint32_t ra)
{
	uint32_t		pos = wifi_regi2c_pos++;
	wifi_regi2c_t	*e = &wifi_regi2c[pos % WIFI_REGI2C_SIZE];

	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->block = block;
	e->host_id = host_id;
	e->reg_add = reg_add;
	e->data = data;
	e->msb = msb;
	e->lsb = lsb;
	e->op = op;
	e->_pad = 0U;
	e->ra = ra;
}

/*
 *  素通し＋記録（`--wrap`標準パターン）。`__real_*`はリンカが元シンボルへ
 *  差し替える。捕捉対象がblob内部の較正ループから高頻度に呼ばれるため，
 *  記録は書込み/読出しの実行前後どちらでも良いが，読み出し系は実測値を
 *  記録するため呼出し後に記録する（write系は書いた値をそのまま記録する
 *  ため呼出し前後どちらでも同じ）。
 */
extern void __real_phy_i2c_writeReg(uint8_t block, uint8_t host_id,
									 uint8_t reg_add, uint8_t data);
void
__wrap_phy_i2c_writeReg(uint8_t block, uint8_t host_id, uint8_t reg_add,
						 uint8_t data)
{
	uint32_t	ra = (uint32_t)(uintptr_t)__builtin_return_address(0);

	__real_phy_i2c_writeReg(block, host_id, reg_add, data);
	wifi_regi2c_push(0U, block, host_id, reg_add, 0xFFU, 0xFFU, data, ra);
}

extern void __real_phy_i2c_writeReg_Mask(uint8_t block, uint8_t host_id,
										  uint8_t reg_add, uint8_t msb,
										  uint8_t lsb, uint8_t data);
void
__wrap_phy_i2c_writeReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							  uint8_t msb, uint8_t lsb, uint8_t data)
{
	uint32_t	ra = (uint32_t)(uintptr_t)__builtin_return_address(0);

	__real_phy_i2c_writeReg_Mask(block, host_id, reg_add, msb, lsb, data);
	wifi_regi2c_push(1U, block, host_id, reg_add, msb, lsb, data, ra);
}

extern uint8_t __real_phy_i2c_readReg(uint8_t block, uint8_t host_id,
									   uint8_t reg_add);
uint8_t
__wrap_phy_i2c_readReg(uint8_t block, uint8_t host_id, uint8_t reg_add)
{
	uint32_t	ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
	uint8_t	ret = __real_phy_i2c_readReg(block, host_id, reg_add);
	wifi_regi2c_push(2U, block, host_id, reg_add, 0xFFU, 0xFFU, ret, ra);
	return(ret);
}

extern uint8_t __real_phy_i2c_readReg_Mask(uint8_t block, uint8_t host_id,
											uint8_t reg_add, uint8_t msb,
											uint8_t lsb);
uint8_t
__wrap_phy_i2c_readReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							 uint8_t msb, uint8_t lsb)
{
	uint32_t	ra = (uint32_t)(uintptr_t)__builtin_return_address(0);
	uint8_t	ret = __real_phy_i2c_readReg_Mask(block, host_id, reg_add, msb, lsb);
	wifi_regi2c_push(3U, block, host_id, reg_add, msb, lsb, ret, ra);
	return(ret);
}

/*
 *  実施18: phy_set_txcap_reg(channel/freq)の引数トレース。wifi_trace.hの
 *  コメント参照。regi2cリングバッファとは別の小さな専用バッファ
 *  （32エントリで十分——実測でこの関数の呼出しは1ブートあたり10回未満）。
 */
#define WIFI_TXCAP_SIZE 32

wifi_txcap_call_t	wifi_txcap_calls[WIFI_TXCAP_SIZE];
volatile uint32_t	wifi_txcap_pos;

void
wifi_txcap_reset(void)
{
	wifi_txcap_pos = 0U;
	memset(wifi_txcap_calls, 0, sizeof(wifi_txcap_calls));
}

void
wifi_txcap_dump_addr(void)
{
	syslog(LOG_NOTICE, "wifi_txcap_addr: arr=%08x pos=%08x entsz=%d cap=%d",
		   (unsigned int)(uintptr_t)wifi_txcap_calls,
		   (unsigned int)(uintptr_t)&wifi_txcap_pos,
		   (int_t)sizeof(wifi_txcap_call_t), (int_t)WIFI_TXCAP_SIZE);
}

extern void __real_phy_set_txcap_reg(uint32_t arg0);
void
__wrap_phy_set_txcap_reg(uint32_t arg0)
{
	uint32_t			pos = wifi_txcap_pos++;
	wifi_txcap_call_t	*e = &wifi_txcap_calls[pos % WIFI_TXCAP_SIZE];

	e->t_us_low = (uint32_t)esp_shim_time_us();
	e->arg0 = arg0;
	__real_phy_set_txcap_reg(arg0);
}

/*
 *  【実施24】PD_TOP/HPAON/HPCPU/LPPERI force解除shimのA/B判定用，
 *  logtask（タスクスケジューリング）に依存しない直接ポーリング出力。
 *
 *  本ラウンドの実測で判明：wifi_scan.cfgのCRE_CYC経由のsyslog()による
 *  周期出力（wifi_diag_cyclic_handler，wifi_scan.c）は，PHY較正の無限
 *  リトライループに入った後は出力が完全に止まる（1回目の出力のみ届き，
 *  以後は次のSUPER_WDTリセットまで無音）。原因はlogtask（優先度3）が
 *  スケジューリングされなくなるためと推定される（未確定，本ラウンドの
 *  範囲では確定できず）。回避として，`target_fput_log`（syssvc/logtask.cの
 *  下請け＝低レベルポーリング文字出力．カーネルバナー同様，タスク
 *  スケジューリングに非依存で常に届く）を直接呼ぶ。
 *
 *  フック先＝`phy_get_pkdet_data`（引数無し・`0x600a0c50`を読み符号拡張して
 *  返すだけの関数．実施16で逆アセンブル確認済み）。実施21〜23で
 *  「ASP3のPHY較正無限リトライループはこの関数を経由的に呼び続ける」と
 *  確認済みの唯一の関数のため，これをフック先に選ぶ（regi2c越しの
 *  phy_i2c_*系は実施20の逆アセンブルにより，トーン測定チェーンが
 *  MODEM0内部MMIOを直接読むだけでregi2cを経由しないと判明しているため，
 *  ループ中に呼ばれる保証がない）。1秒に1回だけ出力するようソフトウェア
 *  タイマ（esp_shim_time_us()差分）でレート制限する。
 */
static uint32_t	wifi_diag_last_print_us;

static void
wifi_diag_fput_str(const char *s)
{
	while (*s != '\0') {
		target_fput_log(*s);
		s++;
	}
}

static void
wifi_diag_fput_hex32(uint32_t v)
{
	static const char	hexdig[] = "0123456789abcdef";
	int_t				i;

	for (i = 28; i >= 0; i -= 4) {
		target_fput_log(hexdig[(v >> i) & 0xFU]);
	}
}

extern int32_t __real_phy_get_pkdet_data(void);
int32_t
__wrap_phy_get_pkdet_data(void)
{
	int32_t		ret = __real_phy_get_pkdet_data();
	uint32_t	now = (uint32_t)esp_shim_time_us();

	if ((now - wifi_diag_last_print_us) >= 1000000U) {
		uint32_t	raw_adc   = *(volatile uint32_t *)0x600A081CU;
		uint32_t	done      = *(volatile uint32_t *)0x600A047CU;
		uint32_t	pd_top    = *(volatile uint32_t *)0x600B00F8U;
		uint32_t	pd_hpaon  = *(volatile uint32_t *)0x600B00FCU;
		uint32_t	pd_hpcpu  = *(volatile uint32_t *)0x600B0100U;
		uint32_t	pd_lpperi = *(volatile uint32_t *)0x600B010CU;

		wifi_diag_last_print_us = now;
		wifi_diag_fput_str("\r\nwifi_diag_live: raw_adc=0x");
		wifi_diag_fput_hex32(raw_adc);
		wifi_diag_fput_str(" done16=");
		target_fput_log((char)('0' + ((done >> 16) & 1U)));
		wifi_diag_fput_str(" pd_top=0x");
		wifi_diag_fput_hex32(pd_top);
		wifi_diag_fput_str(" pd_hpaon=0x");
		wifi_diag_fput_hex32(pd_hpaon);
		wifi_diag_fput_str(" pd_hpcpu=0x");
		wifi_diag_fput_hex32(pd_hpcpu);
		wifi_diag_fput_str(" pd_lpperi=0x");
		wifi_diag_fput_hex32(pd_lpperi);
		wifi_diag_fput_str("\r\n");
	}
	return(ret);
}
