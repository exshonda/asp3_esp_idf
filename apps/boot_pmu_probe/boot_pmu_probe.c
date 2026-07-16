/*
 *  ブート方式 A/B プローブ — Direct Boot vs seam で PMU/MODEM ドメインの
 *  レジスタ状態が変わるかを実機で測る（evidence-c5-03 実験4）
 *
 *  ── 何を検証するか ──
 *
 *  記録済みの真因（[[c5-wifi-modem-domain-unpowered]]／
 *  [[c6-bt-handoff-success]]）は「ASP3の Direct Boot が stock の pmu_init()
 *  を飛ばすため MODEM 電源/クロックドメインが立たない」であった。
 *  「seam（実bootloader経由）ならその初期化が landing するのでは」という
 *  期待に対する **直接プローブ**。
 *
 *  ── ★予測（測定前に固定．ソース実測に基づく）★ ──
 *
 *  `pmu_init()` の唯一の呼出し元は
 *      esp_system/port/soc/esp32c5/clk.c:82  esp_rtc_init() { pmu_init(); }
 *      esp_system/port/cpu_start.c:566       esp_rtc_init();
 *  ＝**アプリ側の起動コード**（call_start_cpu0）であり，2nd-stage
 *  bootloader ではない（bootloader_support/src/esp32c5/bootloader_esp32c5.c の
 *  bootloader_init() に pmu 参照は皆無）。
 *
 *  seam は「bootloader → ASP3自前エントリ」であり IDF の cpu_start.c を
 *  一切通らない。∴ **seam でも pmu_init は実行されない**。
 *
 *  ⇒ 予測：本プローブの出力は Direct Boot と seam で **PMUレジスタが同一**
 *     （両方ともPOR値）になる。もし差が出たら上記の構造分析が誤り＝
 *     重要な反証情報。
 *
 *  ── 測り方 ──
 *
 *  Wi-Fi初期化を一切呼ばない（esp_shim_modem_icg_init も走らない）。
 *  ブート直後のPMU/MODEMドメインの生値だけを出す。両ビルドは
 *  **ASP3側のコードが完全に同一**で，違いはブート方式（ld＋イメージ形式）
 *  のみ＝差分が出ればブート方式に帰属できる。
 */
#include <kernel.h>
#include <t_syslog.h>
#include "boot_pmu_probe.h"

/*
 *  PMU（常時給電ドメイン．読みは常に安全）
 *  DR_REG_PMU_BASE = 0x600B0000（soc/esp32c5/register/soc/reg_base.h:92）
 */
#define PMU_BASE			0x600B0000UL
#define PMU_WORDS			80U		/* 0x000〜0x13C */

/*
 *  MODEM_SYSCON（C5のベースは 0x600A9C00．C6の0x600A9800とは別．
 *  [[c5-wifi-modem-domain-unpowered]]で確認済み）
 */
#define MODEM_SYSCON_BASE	0x600A9C00UL
#define MODEM_SYSCON_WORDS	16U

/*
 *  PHYデジタル（0x600A0000ブロック）．[[c5-wifi-modem-domain-unpowered]]の
 *  観測点：無給電なら書込みが保持されず読みは0になる。
 */
#define PHY_DIG_044C		0x600A044CUL
#define PHY_DIG_0450		0x600A0450UL
#define PHY_DIG_047C		0x600A047CUL

static uint32_t
rd(uint32_t addr)
{
	return(*(volatile uint32_t *)addr);
}

void
main_task(EXINF exinf)
{
	uint32_t	i;

	syslog_0(LOG_NOTICE, "boot_pmu_probe: START");
#ifdef ASP3_SEAM_BOOT_MARKER
	syslog_0(LOG_NOTICE, "boot_pmu_probe: BOOTMODE=seam");
#else
	syslog_0(LOG_NOTICE, "boot_pmu_probe: BOOTMODE=directboot");
#endif

	/*
	 *  PMU 0x600B0000〜0x600B013C
	 */
	for (i = 0U; i < PMU_WORDS; i += 4U) {
		syslog_5(LOG_NOTICE, "PMU+%03x: %08x %08x %08x %08x",
				 i * 4U,
				 rd(PMU_BASE + (i + 0U) * 4U),
				 rd(PMU_BASE + (i + 1U) * 4U),
				 rd(PMU_BASE + (i + 2U) * 4U),
				 rd(PMU_BASE + (i + 3U) * 4U));
		dly_tsk(20000U);	/* logtaskを詰まらせない */
	}

	/*
	 *  MODEM_SYSCON
	 */
	for (i = 0U; i < MODEM_SYSCON_WORDS; i += 4U) {
		syslog_5(LOG_NOTICE, "MDMSYS+%03x: %08x %08x %08x %08x",
				 i * 4U,
				 rd(MODEM_SYSCON_BASE + (i + 0U) * 4U),
				 rd(MODEM_SYSCON_BASE + (i + 1U) * 4U),
				 rd(MODEM_SYSCON_BASE + (i + 2U) * 4U),
				 rd(MODEM_SYSCON_BASE + (i + 3U) * 4U));
		dly_tsk(20000U);
	}

	/*
	 *  PHYデジタル（無給電なら0のはず）
	 */
	syslog_3(LOG_NOTICE, "PHYDIG: 044c=%08x 0450=%08x 047c=%08x",
			 rd(PHY_DIG_044C), rd(PHY_DIG_0450), rd(PHY_DIG_047C));

	syslog_0(LOG_NOTICE, "boot_pmu_probe: DONE");

	for (;;) {
		dly_tsk(1000000U);
	}
}
