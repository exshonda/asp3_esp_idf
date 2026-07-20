/*
 *  esp_shim_chip_regs.h（ESP32-C5）
 *
 *  dedup Tier2c：共有コア（common_espidf/wifi/esp_shim_core.c）が使う
 *  «チップ固有アドレス／レジスタ» を symbolic 名で per-chip に集約する
 *  ヘッダ．設計は C3 版ヘッダ冒頭を参照．共有ファイルに
 *  #ifdef TOPPERS_ESP32Cx は入れず，チップ差はここの «値» だけで表す．
 */

#ifndef ESP_SHIM_CHIP_REGS_H
#define ESP_SHIM_CHIP_REGS_H

#include "target_timer.h"		/* esp32c5_systimer_read / ESP32C5_SYSTIMER_TICKS_PER_US */

/*
 *  HW乱数生成器（WDEV_RND_REG）．C5 は C3 の SYSCON_RND_DATA_REG
 *  (0x600260B0) ではなく LPPERI_RNG_DATA_SYNC_REG (WDEV_RND_REG)
 *  = 0x600B2828（docs/c5-port-design.md §8.3．C5 は LP_PERI ブロックへ移動）．
 */
#define ESP_SHIM_WDEV_RND_REG			0x600B2828U		/* LPPERI_RNG_DATA_SYNC_REG (WDEV_RND_REG) */

/*
 *  起動からのμs（SYSTIMER）．チップ中立な別名で共有コアから参照する．
 */
#define ESP_SHIM_SYSTIMER_READ()		esp32c5_systimer_read()
#define ESP_SHIM_SYSTIMER_TICKS_PER_US	ESP32C5_SYSTIMER_TICKS_PER_US

#endif /* ESP_SHIM_CHIP_REGS_H */
