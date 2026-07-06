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

## ESP32-C6横展開（Phase B-0/B-1．2026-07-04）

asp3_core側のESP32-C6 Phase A完了（`feat/esp32c6`ブランチ．test_porting
6/6・testexec 35/36）を受け，本リポジトリの`asp3/asp3_core`submoduleを
`feat/esp32c6`へ切替え（`feat/esp32c3`からの差分はC6専用ファイルの
純追加のみと確認済み＝C3側への影響なし．submodule切替え後にC3の
既存ビルド（wifi_dhcp・tcp_socket_echo・bt_smoke等）を再ビルドし
回帰なしを確認）。新規に`asp3/target/esp32c6_espidf/`をC3版と並列の
外部ターゲットとして追加：

- `hal_stub/include`はC3版をそのまま再利用（チップ非依存のlibc互換
  ヘッダのため複製しない）。
- USB Serial/JTAGコンソール・SYSTIMER HRTのLL層置換はC3と同じ設計
  （`esp32c6_usbjtag_hal.c`・`target_timer.[ch]`）。SYSTIMERの
  52bitカウンタ読出しはC3同様`_high`/`_low`ペア関数を組み合わせる
  （`systimer_ll_get_counter_value`という単一関数は存在しない）。
- リンカスクリプトはC6のIROM/DROM非分離構造（Phase A版）に
  `esp32c6.peripherals.ld`のINCLUDEを追加しただけ（C3のような
  複数ORIGIN分割は元々不要）。
- QEMU非対応（Espressif版QEMU forkにesp32c6マシンが無いとPhase Aで
  確認済み）のため実機書込みのみ（`ESP32C6_PORT`既定`/dev/ttyACM1`）。
- test_porting実行には`tap.c`を`ASP3_EXTRA_APP_C_FILES`で明示的に
  追加する必要がある（`ASP3_APPLNAME`は`test_porting.c`のみを自動採用
  するため）。

**検証**：実機ESP32-C6（rev v0.2）で`sample1`起動・
**test_porting 6/6 PASS**（外部ターゲット`esp32c6_espidf`経由）。
Wi-Fi/BT統合（Phase B-2相当）は未着手。

## 実機ボード個体差メモ（2026-07-06）

C6のAGC調査（実施20〜38）後のC3リグレッション確認の一環で，
複数の実機ボードで`esp32c3_espidf`ターゲットを検証した際に判明した
落とし穴。

### ソフトリセットのタイミング感度（M5Stamp C3U Mate・rev v0.3）

XIAO ESP32-C3（rev v0.4）でtest_porting 6/6・testexec 36/36 PASSを
確認済みの同一手順（`scripts/ci/run_board_esp32c3.py`：
`dtr=False`→`rts`を0.2秒アサート→解除）を，M5Stamp C3U Mate
（**rev v0.3**）にそのまま適用すると，**ダウンロードモード
（`boot:0x4 DOWNLOAD(USB/UART0/1)`）に落ちて起動失敗**する
（ファームウェアは書き込めているが実行されない）。

物理的な電源断→再接続（真の抜き差し）では正常起動
（`boot:0x8`/`0xc SPI_FAST_FLASH_BOOT`）することから，ハードウェア
自体（Direct Boot・USB Serial/JTAG）は正常。原因はこのボードの
USB Serial/JTAG自動リセット回路のRC時定数がXIAO/DevKitC-02より
遅く，0.2秒のリセットアサート保持では十分に安定しないためと推測。

**対処**：`dtr=False`後に0.1秒の整定待ちを追加し，`rts`アサート
保持を0.2秒→0.5秒に延長することで，無人の書込み→リセット→
キャプチャが安定して再現する（testexecフルセット36件を無人で
完走できた）。ボード固有のリセット回路の余裕が個体・ロットで
異なりうるため，新しいC3ボードで`run_board_esp32c3.py`が無出力・
ダウンロードモード固着になる場合は，まずこの遅延延長を疑うこと。

**検証結果**：M5Stamp C3U Mate（rev v0.3）実機で
**test_porting 6/6 PASS**・**testexec 36/36 PASS**（cpuexc10=SKIP
扱いPASS・dlynse含む）。`apps/wifi_scan`でも実機Wi-Fiスキャン成功
（周辺AP 20個のSSID/RSSI/ch受信）。XIAO（rev v0.4）と合わせ，
チップリビジョンに依らずC3依存部（カーネル・arch/target層）は
同一動作であることを確認。

### apps/wifi_scan.c のC6専用診断コードによるC3ビルド破壊（既存バグ・本セッションで修正）

C6のAGC調査で`apps/wifi_scan/wifi_scan.c`（C3/C6共用アプリ）に
`wifi_trace.h`（`asp3/target/esp32c6_espidf/wifi/`のみに存在する
C6専用診断ヘッダ）のincludeと，`wifi_trace_dump()`等の呼出しが
ガード無しで追加されており，**esp32c3_espidfターゲットで
`apps/wifi_scan`がビルドできなくなっていた**（`wifi_trace.h: No
such file or directory`）。実際には`wifi_regsnap_dump()`等の一部は
これより前のC6専用コミット（`c0e7134`）から未ガードで，かなり
以前からC3側は壊れていた。

`#ifdef TOPPERS_ESP32C6_WIFI`で該当箇所（include・
`wifi_trace_reset`/`wifi_regi2c_*`/`wifi_taskdelay_*`/
`wifi_regsnap_*`/`wifi_phyinit_dump`/`wifi_trace_dump*`の全呼出し）
を囲み，C3側はこれらのC6専用診断を完全にスキップするよう修正した。
kernel/arch側のtest_porting・testexecはこのアプリを経由しないため
このリグレッションを検出できず，`apps/wifi_scan`のような実アプリの
ビルドまで確認しないと見つからない種類の不具合だった点は今後の
教訓とする。

### ビルドに使うツールチェーンによる挙動差

`asp3/target/esp32c3_espidf/wifi/esp_shim_libc.c`等のWi-Fi統合コード
は「ツールチェーンにnewlibが無い環境」を前提に，`strlen`等の
libc関数をROM側解決に委ねる設計（ヘッダでの前方宣言を意図的に
省略している箇所がある）。GCC 14以降はC言語で暗黙関数宣言を既定で
エラー化するため，新しいGCCではこれが**ビルドエラー**になる
（`implicit declaration of function 'strlen'`等）。

`-DCMAKE_C_FLAGS="-Wno-error=implicit-function-declaration"`を
configure時に渡すことでエラーを警告に戻せる（GCC 14.2.0
`riscv32-esp-elf-gcc`で`apps/wifi_scan`のビルド・実機スキャン成功を
確認）。より古いGCC（8.3.0系）はこの点では問題ないが，`zicsr_zifencei`
ISA拡張表記に対応していないため`-march=rv32imc_zicsr_zifencei`が
使えず，逆に不可（本リポジトリのtoolchain-riscv64.cmakeが要求する
march文字列とは非互換）。
