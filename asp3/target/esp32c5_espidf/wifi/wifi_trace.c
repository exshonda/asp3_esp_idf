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
				  uint8_t msb, uint8_t lsb, uint8_t data)
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
	__real_phy_i2c_writeReg(block, host_id, reg_add, data);
	wifi_regi2c_push(0U, block, host_id, reg_add, 0xFFU, 0xFFU, data);
}

extern void __real_phy_i2c_writeReg_Mask(uint8_t block, uint8_t host_id,
										  uint8_t reg_add, uint8_t msb,
										  uint8_t lsb, uint8_t data);
void
__wrap_phy_i2c_writeReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							  uint8_t msb, uint8_t lsb, uint8_t data)
{
	__real_phy_i2c_writeReg_Mask(block, host_id, reg_add, msb, lsb, data);
	wifi_regi2c_push(1U, block, host_id, reg_add, msb, lsb, data);
}

extern uint8_t __real_phy_i2c_readReg(uint8_t block, uint8_t host_id,
									   uint8_t reg_add);
uint8_t
__wrap_phy_i2c_readReg(uint8_t block, uint8_t host_id, uint8_t reg_add)
{
	uint8_t	ret = __real_phy_i2c_readReg(block, host_id, reg_add);
	wifi_regi2c_push(2U, block, host_id, reg_add, 0xFFU, 0xFFU, ret);
	return(ret);
}

extern uint8_t __real_phy_i2c_readReg_Mask(uint8_t block, uint8_t host_id,
											uint8_t reg_add, uint8_t msb,
											uint8_t lsb);
uint8_t
__wrap_phy_i2c_readReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
							 uint8_t msb, uint8_t lsb)
{
	uint8_t	ret = __real_phy_i2c_readReg_Mask(block, host_id, reg_add, msb, lsb);
	wifi_regi2c_push(3U, block, host_id, reg_add, msb, lsb, ret);
	return(ret);
}
