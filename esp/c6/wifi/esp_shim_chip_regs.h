/*
 *  esp_shim_chip_regs.h（ESP32-C6）
 *
 *  dedup Tier2c：共有コア（common_espidf/wifi/esp_shim_core.c）が使う
 *  «チップ固有アドレス／レジスタ» を symbolic 名で per-chip に集約する
 *  ヘッダ．設計は C3 版ヘッダ冒頭を参照．共有ファイルに
 *  #ifdef TOPPERS_ESP32Cx は入れず，チップ差はここの «値» だけで表す．
 */

#ifndef ESP_SHIM_CHIP_REGS_H
#define ESP_SHIM_CHIP_REGS_H

#include "target_timer.h"		/* esp32c6_systimer_read / ESP32C6_SYSTIMER_TICKS_PER_US */

/*
 *  HW乱数生成器（WDEV_RND_REG）．C6 の真の HW RNG 読出しレジスタは
 *  WDEV_RND_REG＝LPPERI_RNG_DATA_REG＝DR_REG_LPPERI_BASE(0x600B2800)+0x8
 *  = 0x600B2808（esp-hal-3rdparty: soc/esp32c6/register/soc/lpperi_reg.h
 *  および soc/wdev_reg.h の WDEV_RND_REG）．C3 の SYSCON_RND_DATA_REG とは
 *  別ペリフェラル（C6 は LP_PERI ブロックへ移動）．C3 B-2b の «WDEV_RND_REG
 *  の実体を正しく引く» 教訓を踏まえ最初から wdev_reg.h 同値を採用．
 */
#define ESP_SHIM_WDEV_RND_REG			0x600B2808U		/* LPPERI_RNG_DATA_REG (WDEV_RND_REG) */

/*
 *  起動からのμs（SYSTIMER）．チップ中立な別名で共有コアから参照する．
 */
#define ESP_SHIM_SYSTIMER_READ()		esp32c6_systimer_read()
#define ESP_SHIM_SYSTIMER_TICKS_PER_US	ESP32C6_SYSTIMER_TICKS_PER_US

#endif /* ESP_SHIM_CHIP_REGS_H */
