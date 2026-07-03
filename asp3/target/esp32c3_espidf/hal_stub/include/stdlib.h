/*
 *  esp-hal／mbedtls用のstdlib.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．malloc系の
 *  実体はWi-Fi shimのヒープ（esp_shim_heap＝wifi/esp_shim.c）が
 *  提供するグローバルシンボル（esp_wifi_adapter.c参照）．
 */
#ifndef TOPPERS_HAL_STUB_STDLIB_H
#define TOPPERS_HAL_STUB_STDLIB_H

#include <stddef.h>

extern void *malloc(size_t size);
extern void *calloc(size_t n, size_t size);
extern void *realloc(void *ptr, size_t size);
extern void free(void *ptr);
extern int atoi(const char *s);
extern long strtol(const char *nptr, char **endptr, int base);
extern int abs(int n);
extern int rand(void);
extern void abort(void);

#endif /* TOPPERS_HAL_STUB_STDLIB_H */
