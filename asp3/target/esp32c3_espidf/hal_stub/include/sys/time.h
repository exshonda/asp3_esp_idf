/*
 *  mbedtls（esp_timing.c）用のsys/time.hスタブ
 *
 *  struct timeval／time_tの定義自体は<time.h>スタブ側にあるため，
 *  ここでは<time.h>を取り込みgettimeofday()の宣言のみ追加する。
 *  実体（ASP3のfch_hrt等への接続）はos_adapter shimで提供する。
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
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
