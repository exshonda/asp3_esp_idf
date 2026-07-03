# CLAUDE.md — asp3_esp_idf

TOPPERS/ASP3 Core と Espressif esp-hal（ESP32-C3）の統合リポジトリ。
全体像・ビルド手順は `README.md`、統合の経緯・設計判断は
`asp3/asp3_core/docs/dev/esp-idf-integration.md` を正本とする。

## 禁則（asp3_core と共通）

1. **`asp3/asp3_core/`（submodule）を直接編集しない**。カーネル・共通arch・
   チップ依存部（`arch/riscv_gcc/esp32c3`）の変更は asp3_core リポジトリ側で
   行い、submodule を bump する。
2. **`hal/`（esp-hal-3rdparty submodule）を直接編集しない**。差分が必要な
   場合はラッパ・シムを本リポジトリ側（`asp3/target/` 等）に置く。
3. **カーネル内で動的メモリ確保を使わない**。Wi-Fi blob が要求する
   ヒープはカーネル外（アプリ/ライブラリ層）として実装する。

## 構成の要点

- ビルドは **asp3_core 本体の CMake を `ASP3_TARGET_DIR` で駆動**する
  （pico-sdk型）。ブートは ASP3 自前の Direct Boot（ESP-IDFの
  スタートアップ・FreeRTOSは不使用）。
- ターゲット依存部 `asp3/target/esp32c3_espidf/` は asp3_core の
  `target/esp32c3_gcc` のコピーが起点（外部ターゲット規約＝
  `CMAKE_CURRENT_LIST_DIR` 相対。PORTING_GUIDE.md §6）。
- テストは asp3_core 側の資産を使う：QEMU＝`test/porting`・testexec、
  実機＝`scripts/ci/run_board_esp32c3.py`（ESP32C3_TTY/ESPTOOL 環境変数）。

## 検証の鉄則

- 変更したら必ずビルドが通ることを確認してから報告する。
- テストはTAP（`ok`/`not ok`・`# 6/6 passed`）で機械判定する。
- QEMUと実機で割込み配送の仕組みが異なる（mie必須／mie非実装）等の
  既知の罠は esp-idf-integration.md「Phase A（実機）結果」を参照。
