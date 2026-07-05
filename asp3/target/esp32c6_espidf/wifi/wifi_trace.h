/*
 *  DIAGNOSTIC (temporary): lightweight ISR-safe ring-buffer trace for
 *  --wrap-based blob call tracing (C6 Wi-Fi RX-enable investigation).
 *  Not for permanent use -- to be removed once the investigation
 *  concludes.  See docs/wifi-shim-c6.md.
 */
#ifndef WIFI_TRACE_H
#define WIFI_TRACE_H

#include <stdint.h>

typedef struct {
	uint32_t	t_us_low;
	uint16_t	id;
	uint16_t	ctx;
	uintptr_t	a0, a1, ret;
} wifi_trace_t;

extern void wifi_trace_push(uint16_t id, uint16_t ctx,
							 uintptr_t a0, uintptr_t a1, uintptr_t ret);
extern void wifi_trace_dump(void);
extern void wifi_trace_dump_counts(void);
extern void wifi_trace_reset(void);

/*
 *  DIAGNOSTIC (temporary, Priority 2)：チャネルホップ毎の
 *  INTMTX/PHY-AGC領域レジスタスナップショット．
 */
extern void wifi_regsnap_reset(void);
extern void wifi_regsnap_dump(void);
extern void wifi_regsnap_capture(void);

/*
 *  DIAGNOSTIC (temporary，実施23／Priority 2)：g_phyFuns（PHYブロブが
 *  ROM regi2cアクセサを呼ぶための関数ポインタテーブル）の
 *  write/write_mask枠を差し替え，実際のregi2c書込み
 *  (block,host_id,reg_add[,msb,lsb],data)を記録する．
 */
extern void wifi_regi2c_reset(void);
extern void wifi_regi2c_dump(void);
extern void wifi_regi2c_dump_count(void);
extern void wifi_regi2c_patch_install(void);

/*
 *  DIAGNOSTIC（実施26／タイミング感度調査）：`_task_delay`実呼出しの
 *  tick引数・実経過時間（us）の記録．
 */
extern void wifi_taskdelay_reset(void);
extern void wifi_taskdelay_capture(uint32_t tick, uint32_t t0, uint32_t elapsed_us);
extern void wifi_taskdelay_dump(void);

/*
 *  DIAGNOSTIC（実施36／コーディネータ指示：phy_init呼出し境界の一点
 *  スナップショット）：`register_chipv7_phy()`呼出し**直前**（1回限り）
 *  に，AGC/PHY領域・MODEM_LPCON（I2C_ANA_MST／WIFIPWR）・MODEM_SYSCON
 *  ・PMU（ICG_MODEM／HP_REGULATOR0）・BBPLLロック/較正ステータス
 *  （regi2c経由，OR_LOCK/OR_CAL_END/OR_CAL_OVF）を1回だけ採取する．
 *  ASP3・NuttX双方で同一関数を同一箇所（呼出し直前）から呼ぶことで，
 *  完全に同期した1点比較を可能にする．
 */
typedef struct {
	uint32_t	t_us_low;
	uint32_t	agc_spot;
	uint32_t	phy_agc_sum;
	uint32_t	modem_lpcon_clk_conf;
	uint32_t	modem_syscon_clk_conf;
	uint32_t	modem_syscon_rst_conf;
	uint32_t	modem_syscon_wifi_bb_cfg;
	uint32_t	pmu_icg_modem;
	uint32_t	pmu_hp_regulator0;
	uint8_t		bbpll_or_lock;
	uint8_t		bbpll_or_cal_end;
	uint8_t		bbpll_or_cal_ovf;
	uint8_t		captured;
} wifi_phyinit_snap_t;

extern void wifi_phyinit_capture_entry(void);
extern void wifi_phyinit_dump(void);

#endif /* WIFI_TRACE_H */
