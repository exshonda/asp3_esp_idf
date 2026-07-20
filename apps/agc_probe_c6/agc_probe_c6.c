/*
 *  AGCプローブ（実施29：NuttX->ASP3クロスカーネル・ハンドオフ実験用）
 *
 *  esp_wifi_init/startを一切呼ばない最小アプリ．AGCスポットレジスタ
 *  （0x600a7128．実施19由来）とPHY/AGC 4KB全域チェックサム
 *  （0x600a7000-0x600a7fff．実施19の`phy_agc_sum`と同じ技法）を
 *  定期的に読み，syslogへ出力するだけ．
 *
 *  目的：NuttX実行中（PHY/AGCが生きている状態）からこのASP3
 *  イメージへジャンプし，Wi-Fi初期化を再実行せずに，ASP3の
 *  ランタイム（スケジューラ・タイマ割込み）稼働下でAGCが
 *  生き続けるか，それとも即座に凍結するかを観測する．
 *  docs/wifi-shim-c6.md「実施29」参照．
 */
#include <kernel.h>
#include <t_syslog.h>
#include "agc_probe_c6.h"

#define AGC_SPOT_REG		0x600a7128UL
#define AGC_REGION_BASE		0x600a7000UL
#define AGC_REGION_WORDS	1024U		/* 4KB / 4 */

#define SAMPLE_COUNT		120U
#define SAMPLE_INTERVAL_US	50000U		/* 50ms */

static uint32_t
agc_spot_read(void)
{
	return(*(volatile uint32_t *)AGC_SPOT_REG);
}

static uint32_t
agc_region_sum(void)
{
	volatile uint32_t	*p = (volatile uint32_t *)AGC_REGION_BASE;
	uint32_t	sum = 0U;
	uint32_t	i;

	for (i = 0U; i < AGC_REGION_WORDS; i++) {
		sum += p[i];
	}
	return(sum);
}

void
main_task(EXINF exinf)
{
	uint32_t	i;
	uint32_t	spot, sum;

	/*
	 *  タスク起動直後（＝ASP3がCPU制御を得た直後に最も近いタイミング）
	 *  に即座に1回読む．ここが「ジャンプ直後」に一番近い観測点．
	 */
	spot = agc_spot_read();
	sum = agc_region_sum();
	syslog(LOG_NOTICE, "agc_probe: alive, entry sample spot=%08x sum=%08x",
		   (uint_t) spot, (uint_t) sum);

	for (i = 0U; i < SAMPLE_COUNT; i++) {
		spot = agc_spot_read();
		sum = agc_region_sum();
		syslog(LOG_NOTICE, "agc_probe: [%d] spot=%08x sum=%08x",
			   (int_t) i, (uint_t) spot, (uint_t) sum);
		(void) tslp_tsk(SAMPLE_INTERVAL_US);
	}

	syslog(LOG_NOTICE, "agc_probe: done (%d samples over %dms)",
		   (int_t) SAMPLE_COUNT,
		   (int_t) (SAMPLE_COUNT * SAMPLE_INTERVAL_US / 1000U));

	while (true) {
		(void) tslp_tsk(1000000);
	}
}
