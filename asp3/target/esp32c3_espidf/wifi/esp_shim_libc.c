/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  libcグローバルシンボルの提供（Wi-Fi統合用）
 *
 *  ツールチェーンにnewlibが無い環境のため，wpa_supplicant／mbedtls／
 *  blobが参照するlibc関数のうち，ROM（esp32c3.rom.newlib.ld等）に
 *  無いものをここで提供する：
 *    - malloc系：shimヒープへ委譲（ROMのmallocはROM専用ヒープ前提の
 *      ため使わない．こちらの定義が強シンボルとしてPROVIDEに勝つ）
 *    - ファイルI/O：ASP3にファイルシステムは無いため失敗を返すスタブ
 *      （mbedtlsのMBEDTLS_FS_IO経路＝実行時には使われない想定）
 */

#include <kernel.h>
#include <t_syslog.h>
#include <stddef.h>
#include <sys/lock.h>
#include "esp_shim.h"
#include "esp_heap_caps.h"

void *
malloc(size_t size)
{
	return(esp_shim_malloc(size));
}

void
free(void *ptr)
{
	esp_shim_free(ptr);
}

void *
calloc(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

void *
realloc(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

int
rand(void)
{
	return((int)(esp_shim_random() & 0x7FFFFFFFU));
}

void
abort(void)
{
	syslog(LOG_EMERG, "abort() called");
	while (1) ;
}

void
__assert_func(const char *file, int line, const char *func, const char *expr)
{
	syslog(LOG_EMERG, "assert failed: %s:%d %s", file, (int_t)line, expr);
	while (1) ;
}

/*
 *  ファイルI/O（スタブ）
 */
struct __FILE;

struct __FILE *
fopen(const char *path, const char *mode)
{
	(void) path; (void) mode;
	return(NULL);
}

int
fclose(struct __FILE *fp)
{
	(void) fp;
	return(-1);
}

size_t
fread(void *ptr, size_t size, size_t n, struct __FILE *fp)
{
	(void) ptr; (void) size; (void) n; (void) fp;
	return(0);
}

size_t
fwrite(const void *ptr, size_t size, size_t n, struct __FILE *fp)
{
	(void) ptr; (void) size; (void) n; (void) fp;
	return(0);
}

int
fseek(struct __FILE *fp, long offset, int whence)
{
	(void) fp; (void) offset; (void) whence;
	return(-1);
}

long
ftell(struct __FILE *fp)
{
	(void) fp;
	return(-1);
}

int
ferror(struct __FILE *fp)
{
	(void) fp;
	return(1);
}

/*
 *  esp_log系（blob／esp-halソースが参照．syslogへ折返し）
 */
#include <stdarg.h>
#include <stdio.h>

void
esp_log(unsigned int config, const char *tag, const char *format, ...)
{
	char	buf[128];
	va_list	args;

	(void) config; (void) tag;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
}

void
esp_log_level_set(const char *tag, int level)
{
	(void) tag; (void) level;
}

unsigned int
esp_log_timestamp(void)
{
	return((unsigned int)(esp_shim_time_us() / 1000));
}

/*
 *  vsnprintf／snprintf／sprintf／puts（ROM newlib.ldに実体が無い．
 *  ツールチェーンにnewlib実体を持たない環境向けの簡易printf実装）
 *
 *  対応：%d %i %u %x %X %o %c %s %p %%．フラグ'-'（左寄せ）'0'（0埋め）．
 *  幅指定（10進数）．長さ修飾子'l'/'ll'（32/64bit）．'h'/'hh'は
 *  va_argの昇格規則によりint/unsignedとして読めば十分なため無視する．
 *  精度指定（%.*f等）・浮動小数点変換は非対応（wpa_supplicant／
 *  esp-halソースのログ文字列は整数・文字列が主で浮動小数点は
 *  使っていないため実用上十分．必要になった時点で拡張する）．
 */
typedef struct {
	char	*buf;
	size_t	size;	/* バッファ容量（NUL込み） */
	size_t	total;	/* 書き込んだ（はずの）文字数．NUL含まず */
} VSN_CTX;

static void
vsn_putc(VSN_CTX *ctx, char c)
{
	if (ctx->size > 0U && ctx->total + 1U < ctx->size) {
		ctx->buf[ctx->total] = c;
	}
	ctx->total++;
}

static void
vsn_puts_raw(VSN_CTX *ctx, const char *s)
{
	while (*s != '\0') {
		vsn_putc(ctx, *s++);
	}
}

/*
 *  符号無し整数を指定基数で出力（幅・0埋め・符号（'-'固定文字）対応）
 */
static void
vsn_putnum(VSN_CTX *ctx, uint64_t val, unsigned int base, bool_t upper,
		   int width, bool_t zero_pad, bool_t left, bool_t neg)
{
	static const char digits_lo[] = "0123456789abcdef";
	static const char digits_up[] = "0123456789ABCDEF";
	const char	*digits = upper ? digits_up : digits_lo;
	char		tmp[24];
	int			ndig = 0;
	int			pad;
	int			i;

	if (val == 0U) {
		tmp[ndig++] = '0';
	}
	while (val != 0U && ndig < (int) sizeof(tmp)) {
		tmp[ndig++] = digits[val % base];
		val /= base;
	}

	pad = width - ndig - (neg ? 1 : 0);
	if (pad < 0) {
		pad = 0;
	}

	if (left) {
		if (neg) {
			vsn_putc(ctx, '-');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
		while (pad-- > 0) {
			vsn_putc(ctx, ' ');
		}
	} else if (zero_pad) {
		if (neg) {
			vsn_putc(ctx, '-');
		}
		while (pad-- > 0) {
			vsn_putc(ctx, '0');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
	} else {
		while (pad-- > 0) {
			vsn_putc(ctx, ' ');
		}
		if (neg) {
			vsn_putc(ctx, '-');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
	}
}

int
vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
	VSN_CTX	ctx;
	char	c;

	ctx.buf = buf;
	ctx.size = size;
	ctx.total = 0U;

	while ((c = *format++) != '\0') {
		bool_t	left, zero_pad, longlong, is_long;
		int		width;

		if (c != '%') {
			vsn_putc(&ctx, c);
			continue;
		}

		left = false;
		zero_pad = false;
		for (;;) {
			c = *format;
			if (c == '-') {
				left = true;
			} else if (c == '0') {
				zero_pad = true;
			} else if (c == '+' || c == ' ' || c == '#') {
				/*  未対応フラグ（符号常時表示等）は読み飛ばすのみ  */
			} else {
				break;
			}
			format++;
		}

		width = 0;
		while (*format >= '0' && *format <= '9') {
			width = width * 10 + (int) (*format - '0');
			format++;
		}

		is_long = false;
		longlong = false;
		if (*format == 'l') {
			is_long = true;
			format++;
			if (*format == 'l') {
				longlong = true;
				format++;
			}
		} else if (*format == 'h') {
			format++;
			if (*format == 'h') {
				format++;
			}
		} else if (*format == 'z' || *format == 't') {
			is_long = true;
			format++;
		}

		c = *format++;
		switch (c) {
		case 'd':
		case 'i':
			{
				int64_t	v = longlong ? va_arg(ap, long long)
							: is_long ? va_arg(ap, long) : va_arg(ap, int);
				bool_t	neg = (v < 0);
				uint64_t uv = neg ? (uint64_t) (-v) : (uint64_t) v;
				vsn_putnum(&ctx, uv, 10U, false, width, zero_pad, left, neg);
			}
			break;
		case 'u':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 10U, false, width, zero_pad, left, false);
			}
			break;
		case 'x':
		case 'X':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 16U, (c == 'X'), width, zero_pad, left,
						   false);
			}
			break;
		case 'o':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 8U, false, width, zero_pad, left, false);
			}
			break;
		case 'p':
			vsn_puts_raw(&ctx, "0x");
			vsn_putnum(&ctx, (uint64_t) (uintptr_t) va_arg(ap, void *),
					   16U, false, 0, false, false, false);
			break;
		case 'c':
			vsn_putc(&ctx, (char) va_arg(ap, int));
			break;
		case 's':
			{
				const char *s = va_arg(ap, const char *);

				if (s == NULL) {
					s = "(null)";
				}
				if (!left && width > 0) {
					int len = (int) strlen(s);
					int pad = width - len;

					while (pad-- > 0) {
						vsn_putc(&ctx, ' ');
					}
				}
				vsn_puts_raw(&ctx, s);
				if (left && width > 0) {
					int len = (int) strlen(s);
					int pad = width - len;

					while (pad-- > 0) {
						vsn_putc(&ctx, ' ');
					}
				}
			}
			break;
		case '%':
			vsn_putc(&ctx, '%');
			break;
		case '\0':
			format--;
			break;
		default:
			vsn_putc(&ctx, '%');
			vsn_putc(&ctx, c);
			break;
		}
	}

	if (ctx.size > 0U) {
		size_t	end = (ctx.total < ctx.size - 1U) ? ctx.total : ctx.size - 1U;

		ctx.buf[end] = '\0';
	}
	return((int) ctx.total);
}

int
snprintf(char *buf, size_t size, const char *format, ...)
{
	va_list	args;
	int		ret;

	va_start(args, format);
	ret = vsnprintf(buf, size, format, args);
	va_end(args);
	return(ret);
}

int
sprintf(char *buf, const char *format, ...)
{
	va_list	args;
	int		ret;

	/*
	 *  ASP3にはファイルシステム上限の概念が無く，呼び出し元
	 *  （wifi_set_default_ssid等）は十分なバッファを渡す前提のため，
	 *  実質無制限（size_t最大）としてvsnprintfへ委譲する．
	 */
	va_start(args, format);
	ret = vsnprintf(buf, (size_t) -1, format, args);
	va_end(args);
	return(ret);
}

int
puts(const char *s)
{
	syslog(LOG_NOTICE, "%s", s);
	return(0);
}

/*
 *  setbuf（バッファリング設定．ASP3にストリームI/Oは無いためno-op）
 */
struct __FILE;

void
setbuf(struct __FILE *fp, char *buf)
{
	(void) fp; (void) buf;
}

/*
 *  remove／rename（ファイルシステム無し．常に失敗を返すスタブ）
 */
int
remove(const char *path)
{
	(void) path;
	return(-1);
}

int
rename(const char *oldpath, const char *newpath)
{
	(void) oldpath; (void) newpath;
	return(-1);
}

/*
 *  usleep／sleep（dly_tskへ委譲．ASP3のdly_tskはμs単位のRELTIM）
 */
int
usleep(unsigned long usec)
{
	dly_tsk((RELTIM) usec);
	return(0);
}

unsigned int
sleep(unsigned int seconds)
{
	dly_tsk((RELTIM) seconds * 1000000U);
	return(0);
}

/*
 *  gettimeofday（SYSTIMER起点のμsをtimevalへ変換．起動時刻=エポック0
 *  扱い＝wpa_supplicant/port/os_xtensa.cのos_get_time()はNTP等の絶対
 *  時刻ではなく単調増加時刻として使うため，これで用が足りる）
 */
struct timeval {
	long	tv_sec;
	long	tv_usec;
};
struct timezone;

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int64_t	us;

	(void) tz;
	if (tv == NULL) {
		return(-1);
	}
	us = esp_shim_time_us();
	tv->tv_sec = (long) (us / 1000000);
	tv->tv_usec = (long) (us % 1000000);
	return(0);
}

/*
 *  esp_fill_random（乱数バイト列．HW RNG=esp_shim_randomを繰り返す）
 */
void
esp_fill_random(void *buf, size_t len)
{
	uint8_t	*p = (uint8_t *) buf;
	size_t	i = 0U;

	while (i < len) {
		uint32_t	r = esp_shim_random();
		size_t		n = (len - i < 4U) ? (len - i) : 4U;
		size_t		j;

		for (j = 0U; j < n; j++) {
			p[i + j] = (uint8_t) (r >> (8U * j));
		}
		i += n;
	}
}

/*
 *  heap_caps_free（esp_shim_freeへ委譲．malloc系はmalloc/free等の
 *  ラッパと同じくshimヒープに一本化＝ESP32-C3はDMA/internal区別不要
 *  というdocs/wifi-shim.mdの設計方針どおり）
 */
void
heap_caps_free(void *ptr)
{
	esp_shim_free(ptr);
}

/*
 *  heap_caps_malloc（esp_shim_mallocへ委譲．heap_caps_freeと同じく
 *  DMA/internal属性はESP32-C3では区別不要のためcapsは無視する．
 *  esp_phy/src/phy_init.cのesp_phy_modem_init()
 *  （SOC_PM_MODEM_RETENTION_BY_BACKUPDMA=1のためPHYディジタル
 *  レジスタ退避バッファ21*4バイトをここで確保．wifi_init.cの
 *  esp_wifi_init()から呼ばれる＝到達コード）が要求する．
 */
void *
heap_caps_malloc(size_t size, uint32_t caps)
{
	(void) caps;
	return(esp_shim_malloc(size));
}

/*
 *  esp_timer_get_time（hal_stub/include/esp_timer.h参照．
 *  esp_shim_time_us＝SYSTIMER起点のμsへ委譲）
 */
int64_t
esp_timer_get_time(void)
{
	return(esp_shim_time_us());
}

/*
 *  ------------------------------------------------------------------
 *  newlib retargetable locking（sys/lock.h．_lock_t＝void*）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.cのs_phy_access_lock（PHY enable/disable/
 *  modem_init/deinitの排他制御．register_chipv7_phy()によるフル較正
 *  は数百ms〜要することがあるため，loc_cpu/unl_cpuによる割込み禁止
 *  ではなくタスクブロッキング可能な本物のミューテックスが必要）が
 *  使う．実体はesp_shim_mutex_*（wifi_shim基盤．静的CRE_MTXプール）
 *  に委譲する．_lock_tは静的グローバル変数として0初期化される
 *  （newlib流儀）ため，_lock_acquire側で遅延生成する．遅延生成の
 *  競合はloc_cpu/unl_cpuで短時間保護する（生成自体はO(1)のプール
 *  取得のため割込み禁止でも問題ない．ロック取得後の本待ち合わせ
 *  （esp_shim_mutex_lock）は生成区間の外＝タスクブロッキング可能）．
 */
static void
esp_shim_lock_lazy_init(_lock_t *lock)
{
	ER	ercd;

	if (*lock == NULL) {
		ercd = loc_cpu();
		if (*lock == NULL) {
			*lock = esp_shim_mutex_create(true);	/* 再帰可 */
		}
		if (ercd == E_OK) {
			(void) unl_cpu();
		}
	}
}

void
_lock_init(_lock_t *lock)
{
	*lock = esp_shim_mutex_create(true);
}

void
_lock_init_recursive(_lock_t *lock)
{
	*lock = esp_shim_mutex_create(true);
}

void
_lock_close(_lock_t *lock)
{
	if (*lock != NULL) {
		esp_shim_mutex_delete(*lock);
		*lock = NULL;
	}
}

void
_lock_close_recursive(_lock_t *lock)
{
	_lock_close(lock);
}

void
_lock_acquire(_lock_t *lock)
{
	esp_shim_lock_lazy_init(lock);
	(void) esp_shim_mutex_lock(*lock);
}

void
_lock_acquire_recursive(_lock_t *lock)
{
	esp_shim_lock_lazy_init(lock);
	(void) esp_shim_mutex_lock(*lock);
}

int
_lock_try_acquire(_lock_t *lock)
{
	esp_shim_lock_lazy_init(lock);
	return((esp_shim_mutex_lock(*lock) != 0) ? 0 : -1);
}

int
_lock_try_acquire_recursive(_lock_t *lock)
{
	return(_lock_try_acquire(lock));
}

void
_lock_release(_lock_t *lock)
{
	if (*lock != NULL) {
		(void) esp_shim_mutex_unlock(*lock);
	}
}

void
_lock_release_recursive(_lock_t *lock)
{
	_lock_release(lock);
}

/*
 *  coexist_printf（libcoexist.aが参照．syslogへ折返し）
 */
int
coexist_printf(const char *format, ...)
{
	char	buf[128];
	va_list	args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
	return(0);
}
