/*
 *  AGCプローブ（実施29：NuttX->ASP3クロスカーネル・ハンドオフ実験用）
 */
#ifndef AGC_PROBE_H
#define AGC_PROBE_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
#endif

#endif /* AGC_PROBE_H */
