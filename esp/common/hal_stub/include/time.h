/*
 *  mbedtls用のtime.hスタブ（MBEDTLS_HAVE_TIME無効構成での型参照用）
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_TIME_H
#define TOPPERS_HAL_STUB_TIME_H

#include <sys/types.h>

#ifndef TOPPERS_HAL_STUB_TIME_T_DEFINED
#define TOPPERS_HAL_STUB_TIME_T_DEFINED
typedef long time_t;
typedef long suseconds_t;
#endif

struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
};

extern time_t time(time_t *tloc);

/*
 *  struct tm／gmtime／mktime：wpa_supplicant/src/utils/common.c
 *  （os_gmtime／os_mktime）が要求する．
 */
struct tm {
	int	tm_sec;
	int	tm_min;
	int	tm_hour;
	int	tm_mday;
	int	tm_mon;
	int	tm_year;
	int	tm_wday;
	int	tm_yday;
	int	tm_isdst;
};

extern struct tm *gmtime(const time_t *timep);
extern time_t mktime(struct tm *tm);

#endif /* TOPPERS_HAL_STUB_TIME_H */
