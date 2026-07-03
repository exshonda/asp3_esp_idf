/*
 *  mbedtls（esp_timing.c）用のsys/time.hスタブ
 *
 *  struct timeval／time_tの定義自体は<time.h>スタブ側にあるため，
 *  ここでは<time.h>を取り込みgettimeofday()の宣言のみ追加する。
 *  実体（ASP3のfch_hrt等への接続）はos_adapter shimで提供する。
 */
#ifndef TOPPERS_HAL_STUB_SYS_TIME_H
#define TOPPERS_HAL_STUB_SYS_TIME_H

#include <time.h>

struct timezone {
	int	tz_minuteswest;
	int	tz_dsttime;
};

extern int gettimeofday(struct timeval *tv, struct timezone *tz);

#endif /* TOPPERS_HAL_STUB_SYS_TIME_H */
