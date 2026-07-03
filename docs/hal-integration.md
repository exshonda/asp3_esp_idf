# esp-hal-3rdparty統合（Phase B-1）

## 方針

- **submodule**：`hal/` ＝ [espressif/esp-hal-3rdparty](https://github.com/espressif/esp-hal-3rdparty)
  を**NuttXが検証済みのコミットSHAに固定**（`release/master.c` 系。
  NuttXの `arch/risc-v/src/common/espressif/Make.defs` の
  `ESP_HAL_3RDPARTY_VERSION` と同一SHA）。Zephyr方式（フォーク維持）
  ではなくNuttX方式（本家＋SHA固定）を採る（フォーク同期の保守コスト
  回避。ASP3固有のスタブは本リポジトリ側に置き，`hal/` は無改変）。
- **Phase B-1で使うのはRTOS非依存の下層のみ**：
  `components/hal`（LL層＝static inlineのレジスタ薄層）・
  `components/soc`（レジスタ定義・構造体・peripherals.ld）・
  `components/esp_common`（esp_attr.h）。
  `esp_wifi`／`esp_phy`のバイナリblob submodule（esp32-wifi-lib≈2GB）は
  Phase B-2まで `submodule update --init` しない。
- **Kconfig非依存**：`sdkconfig.h` はesp-hal同梱のNuttX用静的スタブ
  （`hal/nuttx/esp32c3/include/sdkconfig.h`＝SOC機能フラグ）を流用し，
  それが要求する `nuttx/config.h` と，ツールチェーンに無いlibcヘッダ
  （assert.h／string.h）を `asp3/target/esp32c3_espidf/hal_stub/include/`
  の最小スタブで供給する。

## LL層で置き換えたドライバ（統合の実証）

| ドライバ | 置換元（asp3_core・レジスタ直叩き） | 置換先（本リポジトリ・LL層） |
|---|---|---|
| USB Serial/JTAGコンソール | `arch/riscv_gcc/esp32c3/esp32c3_usbjtag.c` | `asp3/target/esp32c3_espidf/esp32c3_usbjtag_hal.c`（`hal/usb_serial_jtag_ll.h`） |
| HRTタイマ（SYSTIMER） | `target/esp32c3_gcc/target_timer.[ch]` | `asp3/target/esp32c3_espidf/target_timer.[ch]`（`hal/systimer_ll.h`） |

- 公開シンボル（`esp32c3_usbjtag_*`・HRT契約関数）は同一のため，
  asp3_core側（chip_serial.c・カーネル）は無改変。差し替えは
  `target.cmake` の `list(REMOVE_ITEM/APPEND ASP3_SYSSVC_TARGET_C_FILES)`。
- ペリフェラル構造体インスタンス（`SYSTIMER` 等）はesp-halの
  `esp32c3.peripherals.ld` をリンカスクリプトから `INCLUDE`（検索パスは
  `-L${ESP_HAL_DIR}/components/soc/esp32c3/ld`）。
- 割込みマトリクス（INTMTX）・Direct Bootブート・割込みの強制
  （FROM_CPU多重マップ）はASP3固有機構のためLL化対象外
  （asp3_core側のまま）。

## esp-hal-3rdparty（release/master.c系）のレイアウト注意

- LLヘッダの配置が従来のESP-IDF（`components/hal/<chip>/include/hal/`）
  から**ペリフェラル別コンポーネント**（`components/esp_hal_usb/` 等）へ
  移行途中。systimer_llは`components/hal/esp32c3/include`，
  usb_serial_jtag_llは`components/esp_hal_usb/esp32c3/include`にある。
- `hal/assert.h`・`hal/misc.h` は `components/hal/platform_port/include`。

## 検証

QEMU（Espressif fork）・実機ESP32-C3とも test_porting 6/6 passed，
実機testexec全件は本リポジトリのコミットログ・asp3_coreの
`docs/dev/esp-idf-integration.md` を参照。
