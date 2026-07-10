/*
 *  sdkconfig.h スタブ（ESP32-C5用．ASP3側で用意）
 *
 *  esp-hal-3rdparty（hal submodule）はNuttX統合向けの
 *  nuttx/esp32c5/include/sdkconfig.h を提供していない（本移植時点で
 *  hal submoduleにESP32-C5用のNuttX board支援ファイルが同梱されて
 *  いないため．C6にはhal/nuttx/esp32c6/include/sdkconfig.hが存在する
 *  が，同ファイルは`#include <nuttx/config.h>`を含みASP3（NuttX非
 *  依存）ではそのままでは使えない）。
 *
 *  CLAUDE.mdの禁則によりhal/（submodule）を直接編集できないため，本
 *  スタブをASP3側（asp3/target/esp32c5_espidf/sdkconfig_stub/）に
 *  用意し，ASP3_INCLUDE_DIRSでこちらを優先解決させる（target.cmake
 *  参照）。
 *
 *  B-0/B-1スコープ（UART0コンソール・SYSTIMER HRT・test_porting）で
 *  実際に#include "sdkconfig.h"されるヘッダは無い見込みだが（C6の
 *  ビルドでも同様にnuttx/esp32c6/include自体はinclude dirsに積まれる
 *  だけで実際には参照されていない），保険的にインクルードパス解決の
 *  受け皿として置く。フェーズ2b（Wi-Fi統合）でCONFIG_SOC_*等の具体的な
 *  定義が必要になった場合はここに追記すること（esp32c6版のnuttx
 *  sdkconfig.hを参考にできるが，値はC5のsoc_caps.h等から個別に確認
 *  すること．CLAUDE.mdの教訓＝値を転記せず確認する，に従う）。
 */

#pragma once
