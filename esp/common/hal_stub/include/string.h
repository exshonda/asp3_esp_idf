/*
 *  esp-halヘッダ用のstring.hスタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．実体は
 *  asp3_coreのlibc_stub.c（コンパイラ生成のmemcpy/memset等）が提供
 *  する．esp-halのhal/misc.h等が<string.h>を要求する．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
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
/*
 *  BLE実施02で発覚：strcpy/strcatはROM（esp32c6.rom.libc.ld／
 *  esp32c6.rom.libc-suboptimal_for_misaligned_mem.ld，D-1で既にリンク
 *  済み）が実体を提供するが，本スタブにプロトタイプが無くGCC14.2.0で
 *  implicit-function-declarationエラーになっていた（ble_gatts_lcl.c，
 *  GATT_SERVER=1化で新規に到達）．
 */
extern char *strcpy(char *dst, const char *src);
extern char *strcat(char *dst, const char *src);
/*
 *  bzero（BSD互換．本来<strings.h>だが本ビルドに同ヘッダは無く，
 *  wpa_supplicant側は<string.h>経由の暗黙宣言を期待している．実体は
 *  esp_shim_libc.c＝memsetへの委譲）
 */
extern void bzero(void *s, size_t n);
extern char *strchr(const char *s, int c);
/*
 *  ROM（esp32c6.rom.libc.ld／esp32c6.rom.libc-suboptimal_for_misaligned_
 *  mem.ld，D-1／esp_wifi.cmakeで既にリンク済み）が実体を提供する残りの
 *  string.h関数群．strchr/strcpy/strcatと同じ理由で先読みして追加
 *  （BLE実施02，wifi_scan再ビルドで発覚）．
 */
extern char *strncpy(char *dst, const char *src, size_t n);
extern char *strncat(char *dst, const char *src, size_t n);
extern char *strrchr(const char *s, int c);
extern size_t strspn(const char *s, const char *accept);
extern size_t strcspn(const char *s, const char *reject);
extern void qsort(void *base, size_t nmemb, size_t size,
				   int (*compar)(const void *, const void *));
extern char *strdup(const char *s);	/* ROM: esp32c6.rom.newlib.ld */
extern size_t strlcpy(char *dst, const char *src, size_t size);	/* ROM: esp32c6.rom.libc.ld */

#endif /* TOPPERS_HAL_STUB_STRING_H */
