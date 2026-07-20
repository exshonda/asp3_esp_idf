/*
 *  ブート方式A/B用 PMUプローブ（evidence-c5-03 実験4）
 */
#ifndef BOOT_PMU_PROBE_H
#define BOOT_PMU_PROBE_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
#endif

#endif /* BOOT_PMU_PROBE_H */
