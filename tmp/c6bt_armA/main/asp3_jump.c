/*
 *  ESP32-C6：稼働中のネイティブESP-IDF（shim無し・Arduino代替の
 *  ground truth側）からASP3(wifi_scan)へジャンプする（実施33の
 *  asp3_jump_now()移植版．NuttXカーネルスレッドではなくESP-IDFの
 *  タスクから呼ぶ点のみ異なる．docs/wifi-shim-c6.md 実施33参照）．
 *
 *  ASP3イメージ（実体先頭1MB＝asp_flash_trunc1M.bin）はフラッシュ
 *  オフセット0x00200000（2MB地点．ESP-IDF側の bootloader+partition
 *  +appイメージの実占有量より十分離れている）に事前にesptoolで
 *  個別書込み済みであることを前提とする．
 */
#include <stdint.h>
#include "hal/mmu_hal.h"
#include "hal/mmu_types.h"
#include "hal/cache_hal.h"
#include "soc/lp_wdt_reg.h"
#include "hal/lpwdt_ll.h"
#include "asp3_jump.h"

#define ASP3_JUMP_VADDR   0x42000000UL
#define ASP3_JUMP_PADDR   0x00200000UL
#define ASP3_JUMP_LEN     0x00100000UL
#define ASP3_ENTRY_VADDR  0x42000008UL

void __attribute__((section(".iram1"), noinline))
asp3_jump_now(void)
{
	uint32_t	out_len;

	/*  LP_SWDT_SYS（LP Super Watchdog）が数秒後に自動リブートさせて
	 *  しまうため，ジャンプ前にAUTO_FEEDへ切替えて無効化する
	 *  （esp_task_wdt_deinitだけでは止まらなかった＝別系統の
	 *  ウォッチドッグ．bootloader_esp32c6.cのbootloader_super_wdt_
	 *  auto_feed()と同じ手順）．*/
	REG_WRITE(LP_WDT_SWD_WPROTECT_REG, LP_WDT_SWD_WKEY_VALUE);
	REG_SET_BIT(LP_WDT_SWD_CONFIG_REG, LP_WDT_SWD_DISABLE);
	REG_WRITE(LP_WDT_SWD_WPROTECT_REG, 0);

	/*  割込みを全マスク（実施33と同じ形状） */
	__asm__ volatile ("csrw mie, zero");
	__asm__ volatile ("csrci mstatus, 0x8");

	mmu_hal_unmap_all();
	mmu_hal_map_region(0, MMU_TARGET_FLASH0,
						ASP3_JUMP_VADDR, ASP3_JUMP_PADDR,
						ASP3_JUMP_LEN, &out_len);
	(void) cache_hal_invalidate_addr(ASP3_JUMP_VADDR, ASP3_JUMP_LEN);

	((void (*)(void)) ASP3_ENTRY_VADDR)();

	/*  到達しない */
	for (;;) {
		;
	}
}
