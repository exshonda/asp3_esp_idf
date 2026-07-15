/*
 *  （§18 RF-cal regi2c トレース計装：C6 BLE synth-lock ハング調査）
 *
 *  C6-BT は esp_bt_controller_enable(BLE)→esp_phy_enable(PHY_MODEM_BT)→
 *  register_chipv7_phy の synth 位相（ram_set_chan_freq_sw_start が
 *  0x600a00cc bit8 を polling）で永久にハングする（docs/ble-c5c6-plan.md
 *  §16/§17）．本計装は register_chipv7_phy が regi2c アクセスに使う
 *  ROM 関数テーブル（g_phyFuns，C6 では固定アドレス 0x4087f954＝実施23 で
 *  実測特定・本ラウンドでも rev v0.2 実機で再確認）の write/write_mask 枠
 *  （idx22/24）を自前トレースラッパへ差し替え，(op,block,reg,msb,lsb,host,
 *  data,caller-PC) を .bss リングバッファへ記録する．ラッパは必ず元関数を
 *  素通しする（passive＝JTAG halt 不要＝実行を乱さない）．
 *
 *  C6 は register_chipv7_phy と phy_get_romfunc_addr が同一 .o のため
 *  --wrap が原理的に効かない（実施23）．よってテーブルを直接パッチする
 *  （wifi/wifi_trace.c の wifi_regi2c_patch_install と同一手法・同一
 *  アドレス）．C3 の bt/phy_cal_trace.c（--wrap 版）とは差し替え手段のみ
 *  異なる．
 *
 *  ハング時，JTAG mdw で btr_magic/btr_count/btr_buf を生読みして
 *  synth 位相直前の regi2c write 列を得る（syslog ダンプはハングで
 *  走らないため使わない）．
 *
 *  CMake オプション ESP32C6_BT_REGI2C_TRACE=ON のときのみコンパイル・
 *  リンク（既定 OFF＝非回帰）．
 */
#include <stdint.h>

/*  C6 の g_phyFuns 固定テーブル（実施23＝0x4087f954．本ラウンド rev
 *  v0.2 実機で idx20/22/23/24 に ROM regi2c 関数ポインタが載ることを
 *  再確認済み：idx20=rom_i2c_readReg(0x400040a6)/idx22=rom_i2c_writeReg
 *  (0x4000411e)/idx23=readReg_Mask(0x4000412c)/idx24=writeReg_Mask
 *  (0x40004160)）  */
#define BTR_PHYFUNS_TABLE_ADDR   0x4087f954UL
#define BTR_IDX_I2C_WRITE        22U
#define BTR_IDX_I2C_WRITE_MASK   24U

/*  ROM regi2c write 関数のシグネチャ（wifi/wifi_trace.c で C6 実証済み）  */
typedef void (*btr_write_fn_t)(uint8_t block, uint8_t host_id,
							   uint8_t reg_add, uint8_t data);
typedef void (*btr_write_mask_fn_t)(uint8_t block, uint8_t host_id,
									uint8_t reg_add, uint8_t msb,
									uint8_t lsb, uint8_t data);

#define BTR_N	1024	/* 線形バッファ長（cal 先頭から．溢れは btr_count で判る） */

/*  JTAG mdw で読む対象（.bss RAM 常駐）  */
volatile uint32_t btr_magic;			/* 0x42545231("BTR1")＝差替え成功  */
volatile uint32_t btr_count;			/* 記録試行総数（>BTR_N なら溢れ）  */
volatile uint32_t btr_swapped;			/* 差替えたエントリ数（2 期待）      */
/*  各イベント 3 語：
 *    w0 = (op<<24)|(block<<16)|(reg<<8)|((msb<<4)|lsb)   op:1=write,3=write_mask
 *    w1 = (host<<16)|(data&0xffff)
 *    w2 = caller PC（regi2c 呼出し元＝synth 位相の局在化用）  */
volatile uint32_t btr_buf[BTR_N * 3];

static btr_write_fn_t		btr_orig_write;
static btr_write_mask_fn_t	btr_orig_write_mask;

static void
btr_log(uint32_t op, uint32_t block, uint32_t reg, uint32_t msb, uint32_t lsb,
		uint32_t host, uint32_t data, uint32_t caller)
{
	uint32_t i = btr_count++;
	if (i < BTR_N) {
		btr_buf[i * 3] = (op << 24) | ((block & 0xffU) << 16)
					   | ((reg & 0xffU) << 8) | (((msb & 0xfU) << 4) | (lsb & 0xfU));
		btr_buf[i * 3 + 1] = ((host & 0xffU) << 16) | (data & 0xffffU);
		btr_buf[i * 3 + 2] = caller;
	}
}

static void
btr_wrap_write(uint8_t block, uint8_t host_id, uint8_t reg_add, uint8_t data)
{
	if (btr_orig_write != (btr_write_fn_t)0) {
		btr_orig_write(block, host_id, reg_add, data);
	}
	btr_log(1U, block, reg_add, 0U, 0U, host_id, data,
			(uint32_t) __builtin_return_address(0));
}

static void
btr_wrap_write_mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
					uint8_t msb, uint8_t lsb, uint8_t data)
{
	if (btr_orig_write_mask != (btr_write_mask_fn_t)0) {
		btr_orig_write_mask(block, host_id, reg_add, msb, lsb, data);
	}
	btr_log(3U, block, reg_add, msb, lsb, host_id, data,
			(uint32_t) __builtin_return_address(0));
}

/*
 *  esp_bt_controller_init() より前（可能な限り早く．esp_shim_bt_clock_init
 *  の直後）に呼ぶこと．テーブルの write/write_mask 枠を保存してラッパへ
 *  差し替える．冪等（既に差替え済みならスキップ）．
 */
void
esp_bt_regi2c_trace_install(void)
{
	volatile uint32_t	*tbl = (volatile uint32_t *) BTR_PHYFUNS_TABLE_ADDR;
	uint32_t			w, wm;

	btr_count = 0U;
	btr_swapped = 0U;

	w  = tbl[BTR_IDX_I2C_WRITE];
	wm = tbl[BTR_IDX_I2C_WRITE_MASK];

	/*  既にラッパへ差替え済みなら二重差替えしない（orig を潰さない）  */
	if (w != (uint32_t)(uintptr_t) btr_wrap_write) {
		btr_orig_write = (btr_write_fn_t)(uintptr_t) w;
		tbl[BTR_IDX_I2C_WRITE] = (uint32_t)(uintptr_t) btr_wrap_write;
		btr_swapped++;
	}
	if (wm != (uint32_t)(uintptr_t) btr_wrap_write_mask) {
		btr_orig_write_mask = (btr_write_mask_fn_t)(uintptr_t) wm;
		tbl[BTR_IDX_I2C_WRITE_MASK] = (uint32_t)(uintptr_t) btr_wrap_write_mask;
		btr_swapped++;
	}

	btr_magic = 0x42545231UL;	/* "BTR1" */
}
