/*
 *  esp-hal／wpa_supplicant用のunistd.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  wpa_supplicant/port/os_xtensa.c が sleep()/usleep() を要求する。
 *  実体はos_adapter shim（Phase B-2）で提供する。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_UNISTD_H
#define TOPPERS_HAL_STUB_UNISTD_H

extern unsigned int sleep(unsigned int seconds);
extern int usleep(unsigned long usec);

#endif /* TOPPERS_HAL_STUB_UNISTD_H */
