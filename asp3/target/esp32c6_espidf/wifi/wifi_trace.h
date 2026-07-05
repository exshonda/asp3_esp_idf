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

#endif /* WIFI_TRACE_H */
