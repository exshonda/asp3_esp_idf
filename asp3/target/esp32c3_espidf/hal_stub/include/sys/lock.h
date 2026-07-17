/*
 *  newlib sys/lock.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  esp_phy/include/esp_private/phy.h が _lock_t を要求する。
 *  実体（ロックの中身）はos_adapter shim（Phase B-2）で
 *  ASP3のミューテックス/CPUロックへ接続する。
 *
 *  ★BLE実施02（GCC14.2.0での再ビルドで発覚）：本スタブは型（_lock_t）
 *  のみを供給し，_lock_acquire/_lock_release等の関数プロトタイプを
 *  持たない．実体はwifi/esp_shim_libc.cが定義済み（WiFi/BT共有）だが，
 *  esp_phy/src/phy_init.cはこのスタブ経由で`<sys/lock.h>`を解決する
 *  （ツールチェーン付属の本物のnewlib sys/lock.hより本スタブがinclude
 *  パス上で先に見つかるため——esp_timer.hスタブと同種の「意図的簡略化
 *  ヘッダが先勝ちする」構造．esp_bt.cmake/npl_os_bridge.h参照）ため，
 *  プロトタイプが無いと呼出し側（phy_init.c）でimplicit-function-
 *  declarationエラーになる．GCC14.2.0は同エラーを既定でハードエラー
 *  にする（従来ツールチェーンでは警告どまりで実害が無かった）．
 *  esp_shim_libc.cの定義に合わせてプロトタイプを追加する
 *  （strictly additive．WiFi/BT両ビルドに共通で効く）．
 *
 *  【重要】本ヘッダは3チップ共有＝C3専用ではない．esp32c3_espidf/
 *  配下にあるが，C5／C6のtarget.cmakeも${C3_TARGETDIR}/hal_stub/include
 *  として同じ実体を参照する（コピーは存在しない）．⇒変更は3チップの
 *  ビルドに波及する．C3固有の内容を入れてはならない．
 *  詳細はesp32c3_espidf/target.cmakeのhal_stub節（実測値つき）．
 */
#ifndef TOPPERS_HAL_STUB_SYS_LOCK_H
#define TOPPERS_HAL_STUB_SYS_LOCK_H

typedef void *_lock_t;

#ifndef TOPPERS_MACRO_ONLY
extern void _lock_init(_lock_t *lock);
extern void _lock_init_recursive(_lock_t *lock);
extern void _lock_close(_lock_t *lock);
extern void _lock_close_recursive(_lock_t *lock);
extern void _lock_acquire(_lock_t *lock);
extern void _lock_acquire_recursive(_lock_t *lock);
extern int  _lock_try_acquire(_lock_t *lock);
extern int  _lock_try_acquire_recursive(_lock_t *lock);
extern void _lock_release(_lock_t *lock);
extern void _lock_release_recursive(_lock_t *lock);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_HAL_STUB_SYS_LOCK_H */
