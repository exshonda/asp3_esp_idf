/*
 *  esp-hal／wpa_supplicant用のunistd.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  wpa_supplicant/port/os_xtensa.c が sleep()/usleep() を要求する。
 *  実体はos_adapter shim（Phase B-2）で提供する。
 */
#ifndef TOPPERS_HAL_STUB_UNISTD_H
#define TOPPERS_HAL_STUB_UNISTD_H

extern unsigned int sleep(unsigned int seconds);
extern int usleep(unsigned long usec);

#endif /* TOPPERS_HAL_STUB_UNISTD_H */
