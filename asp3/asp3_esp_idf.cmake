#
#  TOPPERS/ASP3 Core ＋ Espressif esp-hal 協調動作ヘルパ
#
#  役割（asp3_mcuxsdk/asp3_stm32cube と同型）：
#   - ASP3_TARGET → ASP3_TARGET_DIR（本リポジトリ側ターゲット依存部）の解決
#   - ASP3_CORE_DIR（純カーネル submodule）／ASP3_ROOT_DIR の設定
#   - ESP_HAL_DIR（esp-hal-3rdparty submodule）の解決
#
#  ESP32-C3はASP3自前のDirect Bootで起動する（ESP-IDFのスタートアップ・
#  FreeRTOSは使用しない）ため，ビルドはasp3_core本体のCMakeを
#  ASP3_TARGET_DIRで駆動する方式（pico-sdk型）を基本とする：
#
#    cmake -S asp3/asp3_core -B build \
#      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-riscv64.cmake \
#      -DASP3_TARGET=esp32c3_espidf \
#      -DASP3_TARGET_DIR=<本リポジトリ>/asp3/target/esp32c3_espidf
#
#  アプリプロジェクト側からincludeして使う場合（Wi-Fiデモ等・
#  ASP3_LIBRARY_ONLY方式）にも対応できるよう，パス解決はここに集約する．
#  経緯：asp3/asp3_core/docs/dev/esp-idf-integration.md
#

#  このファイルの場所＝本リポジトリの asp3/ ディレクトリ
set(ASP3_ESPIDF_DIR ${CMAKE_CURRENT_LIST_DIR})

#  純カーネル（submodule）
set(ASP3_CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/asp3_core)

#  ASP3カーネルソースのルート＝submodule
set(ASP3_ROOT_DIR ${ASP3_CORE_DIR})

#  esp-hal-3rdparty（submodule．Phase B-1以降で使用）

#  ターゲット依存部（本リポジトリ側）を asp3_core へ供給
if(NOT DEFINED ASP3_TARGET)
    set(ASP3_TARGET esp32c3_espidf)
endif()
set(ASP3_TARGET_DIR ${CMAKE_CURRENT_LIST_DIR}/target/${ASP3_TARGET})
