/*
 *  esp-hal用のsys/cdefs.hスタブ（esp_types.hが要求する最小限）
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_SYS_CDEFS_H
#define TOPPERS_HAL_STUB_SYS_CDEFS_H

#define __BEGIN_DECLS
#define __END_DECLS

#endif /* TOPPERS_HAL_STUB_SYS_CDEFS_H */
