/*
 *  （D-2b(1)(j) RF-cal regi2c トレース計装）
 *
 *  ASP3 Direct Boot下でRF/AGC calのSA（successive-approximation）ループが
 *  反復せず固定trimを返す（(1)(a)-(h)で局所化）真因を掴むための計装。
 *  register_chipv7_phy が cal 直前に呼ぶ ROM `phy_get_romfuncs`（絶対シンボル
 *  0x400018fc）を `--wrap` で捕捉し、返ってきた g_phyFuns テーブル（RAM常駐・
 *  可書換）内の regi2c read/write/read_mask/write_mask エントリ（ROM関数）を
 *  自前トレースラッパへ差替える。ラッパは元関数を呼びつつ op/block/reg/msb/lsb/
 *  value を RAM リングバッファへ記録する（passive＝JTAG halt不要＝RF中も安全）。
 *  cal後にJTAG mdwで `pct_buf`/`pct_count` を一括読みし、RF/AGCブロック
 *  (0x6b/0x6d/0x6a) の write_mask→read_mask 反復で read_mask が同一値を返し
 *  続ける（stuck＝比較器無応答）箇所を特定する。
 *
 *  本ファイルは CMake オプション ESP32C3_BT_PHY_CAL_TRACE=ON のときのみ
 *  コンパイル＆リンク（`--wrap=phy_get_romfuncs` 付与）される。既定OFFゆえ
 *  bt_smoke_hw / wifi_dhcp_hw / 通常 ble_host_smoke ビルドは非回帰。
 */
#include <stdint.h>

/*  トレース対象 ROM regi2c 関数（esp32c3 rev3；g_phyFunsテーブル内で実測確認）  */
#define PCT_READREG        0x40039162UL	/* rom_i2c_readReg(blk,host,reg)          */
#define PCT_WRITEREG       0x400391e6UL	/* rom_i2c_writeReg(blk,host,reg,data)    */
#define PCT_READREG_MASK   0x400391f4UL	/* rom_i2c_readReg_Mask(blk,host,reg,m,l) */
#define PCT_WRITEREG_MASK  0x4003922aUL	/* rom_i2c_writeReg_Mask(blk,host,reg,m,l,d) */
#define PCT_SARREAD        0x4003a2ccUL	/* g_phyFuns[82] SAR読み(buf), 測定値=*(u16*)(buf+2) */

typedef int  (*pct_read_t)(int blk, int host, int reg);
typedef void (*pct_write_t)(int blk, int host, int reg, int data);
typedef int  (*pct_readm_t)(int blk, int host, int reg, int msb, int lsb);
typedef void (*pct_writem_t)(int blk, int host, int reg, int msb, int lsb, int data);
typedef void (*pct_sarread_t)(void *buf);

#define PCT_N	1200	/* 線形バッファ長（cal先頭から。溢れは pct_count で判る） */

/*  JTAG mdw で読む対象（.bss RAM 常駐）  */
volatile uint32_t pct_magic;			/* 0x50435431("PCT1")＝差替え成功 */
volatile uint32_t pct_count;			/* 記録試行総数（>PCT_Nなら溢れ）  */
volatile uint32_t pct_swapped;			/* 差替えたエントリ数（4期待）      */
/*  各イベント3語: w0=op/blk/reg/msb/lsb, w1=host/val, w2=caller PC
 *  （w2＝regi2c呼出し元＝0x6b sweepを回すcal関数の局所化用）  */
volatile uint32_t pct_buf[PCT_N * 3];

static pct_read_t	pct_orig_read;
static pct_write_t	pct_orig_write;
static pct_readm_t	pct_orig_readm;
static pct_writem_t	pct_orig_writem;
static pct_sarread_t	pct_orig_sarread;

static void
pct_log(uint32_t op, uint32_t blk, uint32_t reg,
		uint32_t msb, uint32_t lsb, uint32_t host, uint32_t val, uint32_t caller)
{
	uint32_t i = pct_count++;
	if (i < PCT_N) {
		pct_buf[i * 3] = (op << 24) | ((blk & 0xffU) << 16)
					   | ((reg & 0xffU) << 8)
					   | (((msb & 0xfU) << 4) | (lsb & 0xfU));
		pct_buf[i * 3 + 1] = ((host & 0xffU) << 16) | (val & 0xffffU);
		pct_buf[i * 3 + 2] = caller;
	}
}

static int
pct_wrap_read(int blk, int host, int reg)
{
	int v = pct_orig_read(blk, host, reg);
	pct_log(0U, (uint32_t) blk, (uint32_t) reg, 0U, 0U, (uint32_t) host, (uint32_t) v,
			(uint32_t) __builtin_return_address(0));
	return v;
}
static void
pct_wrap_write(int blk, int host, int reg, int data)
{
	pct_orig_write(blk, host, reg, data);
	pct_log(1U, (uint32_t) blk, (uint32_t) reg, 0U, 0U, (uint32_t) host, (uint32_t) data,
			(uint32_t) __builtin_return_address(0));
}
static int
pct_wrap_readm(int blk, int host, int reg, int msb, int lsb)
{
	int v = pct_orig_readm(blk, host, reg, msb, lsb);
	pct_log(2U, (uint32_t) blk, (uint32_t) reg, (uint32_t) msb, (uint32_t) lsb,
			(uint32_t) host, (uint32_t) v, (uint32_t) __builtin_return_address(0));
	return v;
}
static void
pct_wrap_writem(int blk, int host, int reg, int msb, int lsb, int data)
{
	pct_orig_writem(blk, host, reg, msb, lsb, data);
	pct_log(3U, (uint32_t) blk, (uint32_t) reg, (uint32_t) msb, (uint32_t) lsb,
			(uint32_t) host, (uint32_t) data, (uint32_t) __builtin_return_address(0));
}

static void
pct_wrap_sarread(void *buf)
{
	pct_orig_sarread(buf);
	/*  測定値＝*(u16*)(buf+2)（get_tone_sar_dout の lhu 2(sp) と同じ）  */
	uint32_t v = *(volatile uint16_t *)((char *) buf + 2);
	pct_log(4U, 0U, 0U, 0U, 0U, 0U, v, (uint32_t) __builtin_return_address(0));
}

extern void **__real_phy_get_romfuncs(void);

void **
__wrap_phy_get_romfuncs(void)
{
	void **tbl = __real_phy_get_romfuncs();
	int i;

	/*  テーブルを走査し regi2c ROM 関数エントリをラッパへ差替え（既に差替え済み
	    なら一致せずスキップ＝orig保持）．範囲は実測offset(107-111語)を含む余裕分．  */
	for (i = 0; i < 220; i++) {
		uint32_t e = (uint32_t) tbl[i];
		if (e == PCT_READREG) {
			pct_orig_read = (pct_read_t) tbl[i];
			tbl[i] = (void *) pct_wrap_read;
			pct_swapped++;
		} else if (e == PCT_WRITEREG) {
			pct_orig_write = (pct_write_t) tbl[i];
			tbl[i] = (void *) pct_wrap_write;
			pct_swapped++;
		} else if (e == PCT_READREG_MASK) {
			pct_orig_readm = (pct_readm_t) tbl[i];
			tbl[i] = (void *) pct_wrap_readm;
			pct_swapped++;
		} else if (e == PCT_WRITEREG_MASK) {
			pct_orig_writem = (pct_writem_t) tbl[i];
			tbl[i] = (void *) pct_wrap_writem;
			pct_swapped++;
		} else if (e == PCT_SARREAD) {
			pct_orig_sarread = (pct_sarread_t) tbl[i];
			tbl[i] = (void *) pct_wrap_sarread;
			pct_swapped++;
		}
	}
	pct_magic = 0x50435431UL;
	return tbl;
}
