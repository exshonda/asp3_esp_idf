/*
 *  esp-halヘッダ用のstring.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．実体は
 *  asp3_coreのlibc_stub.c（コンパイラ生成のmemcpy/memset等）が提供
 *  する．esp-halのhal/misc.h等が<string.h>を要求する．
 */
#ifndef TOPPERS_HAL_STUB_STRING_H
#define TOPPERS_HAL_STUB_STRING_H

#include <stddef.h>

extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);
extern void *memmove(void *dst, const void *src, size_t n);
extern int memcmp(const void *s1, const void *s2, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strstr(const char *haystack, const char *needle);
extern char *strerror(int errnum);

#endif /* TOPPERS_HAL_STUB_STRING_H */
