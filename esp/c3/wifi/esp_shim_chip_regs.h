/*
 *  esp_shim_chip_regs.h（ESP32-C3）
 *
 *  dedup Tier2c：共有コア（common_espidf/wifi/esp_shim_core.c）が使う
 *  «チップ固有アドレス／レジスタ» を symbolic 名で per-chip に集約する
 *  ヘッダ（esp32_s3 precedent：番地ハードコードでなく soc symbolic 名で
 *  参照すると共有コアがチップ非依存になる）．各チップの wifi dir が
 *  include path の最優先（target.cmake の ${TARGETDIR}/wifi）なので，
 *  同名ヘッダを各チップ dir に置けば core が per-chip に解決する．
 *
 *  ★共有ファイルに #ifdef TOPPERS_ESP32Cx は入れない（Codex 設計方針）．
 *  チップ差はこのヘッダの «値» だけで表す．
 */

#ifndef ESP_SHIM_CHIP_REGS_H
#define ESP_SHIM_CHIP_REGS_H

#include "target_timer.h"		/* esp32c3_systimer_read / ESP32C3_SYSTIMER_TICKS_PER_US */

/*
 *  HW乱数生成器（WDEV_RND_REG）．無線が有効になるとRFノイズ由来の真性
 *  乱数になる（無効時はエントロピー低）．
 *
 *  C3 は SYSCON_RND_DATA_REG = DR_REG_SYSCON_BASE(0x60026000)+0x0B0
 *  = 0x600260B0（esp-hal-3rdparty: soc/esp32c3/register/soc/syscon_reg.h
 *  の SYSCON_RND_DATA_REG／RNG_DATA_REG）．旧実装は 0x6002607C
 *  （-0x34 のオフセット違い）を読んでおり常に0を返す別レジスタだった＝
 *  WPA2 4-way ハンドシェイクの SNonce が常時全ゼロ→AP が nonce 再利用と
 *  みなし msg1 を再送し続ける（reason=15）原因だった．実機JTAGで確認済み．
 */
#define ESP_SHIM_WDEV_RND_REG			0x600260B0U		/* SYSCON_RND_DATA_REG */

/*
 *  起動からのμs（SYSTIMER）．チップ中立な別名で共有コアから参照する．
 */
#define ESP_SHIM_SYSTIMER_READ()		esp32c3_systimer_read()
#define ESP_SHIM_SYSTIMER_TICKS_PER_US	ESP32C3_SYSTIMER_TICKS_PER_US

#endif /* ESP_SHIM_CHIP_REGS_H */
