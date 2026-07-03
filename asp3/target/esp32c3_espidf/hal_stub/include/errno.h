/*
 *  esp-hal／mbedtls／wpa_supplicant用のerrno.hスタブ
 *
 *  ツールチェーン（riscv64-unknown-elf-gcc）にnewlibヘッダが無い環境の
 *  ためのスタブ．参照されるマクロのみ定義する（不足分は必要時に追加）．
 *  errno実体はos_adapter shim（Phase B-2）で提供する。
 */
#ifndef TOPPERS_HAL_STUB_ERRNO_H
#define TOPPERS_HAL_STUB_ERRNO_H

extern int errno;

#define ENOSYS  38

#endif /* TOPPERS_HAL_STUB_ERRNO_H */
