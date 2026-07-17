/*
 *  esp-hal／mbedtls用のstdlib.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．malloc系の
 *  実体はWi-Fi shimのヒープ（esp_shim_heap＝wifi/esp_shim.c）が
 *  提供するグローバルシンボル（esp_wifi_adapter.c参照）．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
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
