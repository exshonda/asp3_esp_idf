/*
 *  PMU_instance() の ASP3 版（stock の weak 定義を上書きする）
 *  ── target.cmake の ASP3_C5_PMU_INIT=ON のときだけコンパイルされる ──
 *
 *  ★なぜ要るか（実機クラッシュで判明した機序．evidence-c5-04 §4）
 *
 *  stock の `pmu_init.c` は PMU_instance() を**初期化子つきの static 変数**で
 *  持っている：
 *
 *      pmu_context_t * __attribute__((weak)) IRAM_ATTR PMU_instance(void)
 *      {
 *          static DRAM_ATTR pmu_hal_context_t pmu_hal = { .dev = &PMU };
 *          static DRAM_ATTR pmu_sleep_machine_constant_t pmu_mc = PMU_SLEEP_MC_DEFAULT();
 *          static DRAM_ATTR pmu_context_t pmu_context = { .hal = &pmu_hal, .mc = (void *)&pmu_mc };
 *          return &pmu_context;
 *      }
 *
 *  `DRAM_ATTR` は `.dram1.N` セクション＝ASP3のldでは `.data` に集約され
 *  （esp32c5.ld:109 `*(.dram1 .dram1.*)`），**フラッシュからRAMへコピー
 *  されて初めて有効**になる。
 *
 *  ところが ASP3 の `start.S`（arch/riscv_gcc/common/start.S:120）は
 *
 *      jal  hardware_init_hook      <-- ここで pmu_init() を呼びたい
 *      ... bssセクションのクリア ...      (start.S:127〜)
 *      ... dataセクションの初期化（ROM化対応） ...  (start.S:143〜)
 *
 *  という順序であり、**`hardware_init_hook()` は .data 初期化より前に走る**
 *  （SDRAMコントローラ等「RAM初期化より前に立てるべきHW」のためのフック
 *  という TOPPERS の設計上そうなっている）。
 *  ⇒ `pmu_context.hal` が未初期化のゴミのままとなり、`pmu_hp_system_init()`
 *  の `ctx->hal->dev` で Load access fault になる。
 *
 *  実機実測（本ラウンド，cold boot）：
 *      Guru Meditation Error: Core 0 panic'ed (Load access fault)
 *      PC : 0x420027c6  = pmu_hp_system_init at pmu_init.c:62
 *      RA : 0x42002cfe  = pmu_init at pmu_init.c:217
 *      MTVAL : 0xa59b20b8 （＝未初期化RAMのゴミをポインタとしてload）
 *
 *  ★対処：`PMU_instance()` は stock 側で **`__attribute__((weak))`** と
 *  明示されている＝上書き前提の拡張点なので、ここで strong 定義を与える。
 *  **stock ソース（pmu_init.c）には一切手を入れない**（CLAUDE.md の禁則
 *  「hal/ を直接編集しない」と同じ精神で、供給元ツリーは無改変に保つ）。
 *
 *  静的初期化子に頼らず**呼ばれる度に代入する**ことで、.data/.bss 初期化の
 *  前後どちらから呼ばれても正しく動く（.bss は本フックの直後にクリアされる
 *  が、次回呼出しでも再代入されるため問題にならない）。
 *
 *  ※`.mc`（pmu_sleep_machine_constant_t）は `pmu_sleep.c` 専用で、本
 *  ターゲットは pmu_sleep.c をコンパイルしていない（ASP3はスリープ遷移を
 *  しない）ため NULL でよい。将来 pmu_sleep.c を積むなら、ここで
 *  PMU_SLEEP_MC_DEFAULT() 相当を実行時に構築する必要がある。
 */
#include "hal/pmu_hal.h"
#include "esp_private/esp_pmu.h"

pmu_context_t *
PMU_instance(void)
{
	static pmu_hal_context_t	s_pmu_hal;
	static pmu_context_t		s_pmu_context;

	s_pmu_hal.dev = &PMU;
	s_pmu_context.hal = &s_pmu_hal;
	s_pmu_context.mc = NULL;

	return(&s_pmu_context);
}
