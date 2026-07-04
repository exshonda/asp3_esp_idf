/*
 *  lwIP プラットフォーム適合層（ASP3用．NO_SYS=1）
 *
 *  型・PACK_STRUCT・エンディアンはlwip/arch.hのGCC既定に委ねる
 *  （<stdint.h>/<stddef.h>はツールチェーン同梱のfreestandingヘッダで
 *  充足）．ここではlwip/arch.hが「ポート固有」として要求する項目
 *  （診断出力・assert・乱数・エンディアン）とNO_SYS=1構成で不足する
 *  ヘッダ回避（ctype.h／unistd.h＝本ツールチェーンに実体が無い）のみ
 *  上書きする．esp_shim.h／kernel.hはここではincludeしない（全lwIP
 *  翻訳単位に波及するため，2関数の宣言のみ切り出す）．
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>

#define BYTE_ORDER LITTLE_ENDIAN

#define LWIP_NO_CTYPE_H   1
#define LWIP_NO_UNISTD_H  1

/*
 *  SYS_ARCH_PROTECT/UNPROTECT用（sys_arch.cで実装．NO_SYS=1でも
 *  arch/sys_arch.hを介さず本ヘッダで型を提供する必要がある）
 */
typedef int sys_prot_t;

#ifdef __cplusplus
extern "C" {
#endif

extern void esp_shim_log_write(const char *format, ...);
extern uint32_t esp_shim_random(void);

#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x)  do { esp_shim_log_write x; } while (0)

#define LWIP_PLATFORM_ASSERT(x) \
	do { \
		esp_shim_log_write("lwip: assertion \"%s\" failed at line %d in %s\n", \
						   (x), (int) __LINE__, __FILE__); \
		for (;;) { } \
	} while (0)

#define LWIP_RAND() ((u32_t) esp_shim_random())

#endif /* LWIP_ARCH_CC_H */
