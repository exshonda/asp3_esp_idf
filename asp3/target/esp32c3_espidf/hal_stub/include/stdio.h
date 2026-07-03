/*
 *  esp-hal／mbedtls／wpa_supplicant用のstdio.hスタブ
 *  （printf系の実体はESP32-C3のROM内newlib＝esp32c3.rom.newlib.ldが提供）
 */
#ifndef TOPPERS_HAL_STUB_STDIO_H
#define TOPPERS_HAL_STUB_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct __FILE FILE;

extern int printf(const char *format, ...);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int sprintf(char *str, const char *format, ...);
extern int vprintf(const char *format, va_list ap);
extern int puts(const char *s);
extern int putchar(int c);

/*
 *  ファイルI/O（mbedtlsのMBEDTLS_FS_IO経路が参照する．ASP3にファイル
 *  システムは無いため，実体は失敗を返すスタブ（esp_shim_libc.c））
 */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

extern FILE *fopen(const char *path, const char *mode);
extern int fclose(FILE *fp);
extern size_t fread(void *ptr, size_t size, size_t n, FILE *fp);
extern size_t fwrite(const void *ptr, size_t size, size_t n, FILE *fp);
extern int fseek(FILE *fp, long offset, int whence);
extern long ftell(FILE *fp);
extern int ferror(FILE *fp);

#endif /* TOPPERS_HAL_STUB_STDIO_H */
