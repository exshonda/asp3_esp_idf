/*
 *  lwIP sys_arch（ASP3用．NO_SYS=1）
 *
 *  NO_SYS=1構成でlwIPコアが要求するのは sys_now() と
 *  SYS_ARCH_PROTECT/UNPROTECT（sys_arch_protect/unprotect）のみ
 *  （mbox/sem/threadのRTOS抽象化＝sys_arch.hは不要．全lwIP呼出しは
 *  net_task 1タスクに集約するため，このprotectは保険＝実質無競合）．
 */
#include <kernel.h>
#include "lwip/sys.h"
#include "esp_shim.h"

u32_t
sys_now(void)
{
	return (u32_t) (esp_shim_time_us() / 1000);
}

sys_prot_t
sys_arch_protect(void)
{
	return (sys_prot_t) esp_shim_int_disable();
}

void
sys_arch_unprotect(sys_prot_t pval)
{
	esp_shim_int_restore((uint32_t) pval);
}
