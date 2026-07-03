/*
 *  newlib sys/lock.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  esp_phy/include/esp_private/phy.h が _lock_t を要求する。
 *  実体（ロックの中身）はos_adapter shim（Phase B-2）で
 *  ASP3のミューテックス/CPUロックへ接続する。
 */
#ifndef TOPPERS_HAL_STUB_SYS_LOCK_H
#define TOPPERS_HAL_STUB_SYS_LOCK_H

typedef void *_lock_t;

#endif /* TOPPERS_HAL_STUB_SYS_LOCK_H */
