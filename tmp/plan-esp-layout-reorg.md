# 計画：`./asp3` を純ASP3化し、ESP統合を `./esp/{common,c3,c5,c6}` へ分離

2026-07-20。レビュー用に計画を文書化したもの（**未実施・着手前**）。

## 0. 動機（ユーザー方針）

**`./asp3` 以下には純粋にASP3に関連するファイルのみを置きたい。**
現状は `asp3/target/esp32cN_espidf/` にASP3カーネルポートとESP-IDF統合が同居している。

加えて本セッションで判明した実害：**3チップ共有の資産が `esp32c3_espidf/` の中に隠れている**。
C5/C6 の `target.cmake` が `${C3_TARGETDIR}` 経由で以下を参照している（実測）：

```
${C3_TARGETDIR}/hal_stub/include                    (160K・3チップ共有)
${C3_TARGETDIR}/net/{net.cfg,netif_esp32c3.c,port/sys_arch.c,port/include}
${C3_TARGETDIR}/wifi/{esp_shim_libc.c,esp_event_shim.c,esp_coex_adapter.c,esp_shim.cfg}
```

ファイル数の非対称（C3=87 / C5=41 / C6=49）はこれが主因。
`net/netif_esp32c3.c` のヘッダコメント自身が「**C3専用ではない・3チップ共有・改名しないのは
ビルド波及が大きいため**」と告白している＝**名前と実態のズレ**。

（同型の問題を本日 `apps/` で修正済み：`bt_smoke`→`bt_smoke_c3`・`agc_probe`→`agc_probe_c6` 等。
その際 `esp_bt.cmake` の `if(ASP3_APPLNAME STREQUAL "ble_host_smoke")` が黙って壊れる寸前だった＝
**文字列一致に依存した隠れ結合**が実在した。）

## 1. 先行事例（esp32_s3 リポジトリ・実測）

S3 は**カーネルポートとESP統合を既に分離**している：
```
fmp3_core/target/esp32s3_devkitc_gcc/  … カーネルポート（esp32s3_xip.ld・target_*.c）
esp/shim/  esp/wifi/  esp/bt/  esp/config/<chip>/  esp/ld/  esp/lib/  esp/boot/
```
ただしS3の `esp/` は**機能メジャー**（チップはファイル名サフィックスか leaf dir）で、
**手書きshellスクリプトが `.a` を個別にビルドする方式**に最適化されている
（`esp/lib/*.a`＝ビルド成果物・gitignore、`esp/ld/`＝esp-idfへのsymlink、
`esp/boot/build_*_<chip>.sh`、`esp/build_incflags_<chip>_espidf.txt`）。

**asp3_esp_idf は asp3_core の CMake が全ソースを一括コンパイルする方式**で、
`.a` 中間成果物も incflags ファイルも存在しない。**ディレクトリ名だけ真似ても意味がない**。

## 2. 実測：分割線はきれいに引ける（着手判断の根拠）

カーネルポート側の esp-idf 依存を実測（C3）：

| ファイル | ESP依存 |
|---|---|
| `target_timer.c`(90行) / `flash_header.S`(37行) / `target_kernel_impl.h` / `target_sil.h` / `target_serial.h` | **0** |
| `target_kernel_impl.c`(390行) | `extern void esp_rom_set_cpu_ticks_per_us(uint32_t);` の**1宣言のみ**（`#include` すら無い） |
| `esp32c3_usbjtag_hal.c`(240行) | `#include "hal/usb_serial_jtag_ll.h"` **1本** |

ファイル数も3チップ揃って **ASP3側26 / ESP側5** と対称（実測）。

## 3. 目標レイアウト

```
asp3/
  asp3_core/                      … submodule（純カーネル）
  arch/riscv_gcc/esp32c5/         … C5チップarch（純ASP3・将来asp3_coreへupstream予定）
  cmake/                          … toolchain-esp32-riscv32.cmake 等
  target/esp32c{3,5,6}_espidf/    … 26ファイル（純ASP3カーネルポート）
      target_*.{c,h,cfg,py,inc,def}  esp32cN.ld  flash_header.S
      esp32cN_usbjtag_hal.c  run.cmake  target.cmake

esp/
  common/                         … 3チップ共有
      hal_stub/  net/  esp_shim_core.{c,h}
      esp_shim_libc.c  esp_event_shim.c  esp_coex_adapter.c  esp_shim.cfg
  c3/  c5/  c6/                   … チップ固有ESP統合
      wifi/  bt/  sdkconfig_stub/  esp_wifi.cmake  esp_bt.cmake
```

## 4. 段階（各段：可逆・実機GREEN確認後に次段）

- **段階A**：3チップ共有資産を `asp3/target/common_espidf/` へ寄せる
  （`hal_stub/`・`net/`・共有 `wifi/esp_shim_*`・`esp_shim.cfg`）。
  `common_espidf/` は既に存在し3チップとも参照済み＝規約内・低リスク。
  これだけで「C3なりすまし」は解消する。
- **段階B**：`common_espidf/` → `esp/common/`、各チップのESP統合部 → `esp/cN/` へ移動。
  パス書き換えのみで論理変更なし。

## 5. 未決定の論点（レビューで意見が欲しい）

### 5-1. `target.cmake`(554行) の扱い

`ASP3_TARGET_DIR` の入口なので**移動できない**が、中身の大半はESP側：
`IDF_V554` 解決(83-96行)・`ASP3_ESPIDF_SUPPLY`(161行)・`ESP32C3_{WIFI,BT,LWIP}` option・
`components/{hal,soc}` の include パス(244-249行)。

- **(a) 薄いtarget.cmake**：純ASP3部分だけ残し、ESP部分を `esp/cN/esp.cmake` へ出して `include()`
- **(b) target.cmake は現状維持**：`esp/` 配下のファイルを参照するだけ

現時点の傾向は **(b)**。理由：目的（`asp3/`にESPの«ファイル»を置かない）は(b)で満たされ、
`ESP32C3_QEMU`（カーネルの `csrw mie` 経路を切り替える＝ASP3側の関心事）のような
**境界オプション**の置き場を決める必要が(a)では生じるため。

### 5-2. 不可分な依存は残る

素カーネル（WiFi/BT両OFF）でも `components/hal`・`components/soc` の include と
ROM ld（`esp_rom_set_cpu_ticks_per_us`）が要る。∴「`asp3/` が esp-idf を参照しない」状態には
**ならない**。達成できるのは「**ESPの«ファイル»を `asp3/` に置かない**」まで。
この理解でユーザー方針を満たすか。

### 5-3. asp3系他repoとの分岐

pico/fsp/stm32/nxp は全て `target/<name>/` にSDK統合を置く（asp3_core の
`PORTING_GUIDE.md` §外部ターゲット規約）。本提案はこの一家から分岐し、
代わりにESP系2repo（asp3_esp_idf・esp32_s3）で揃う。この trade-off は妥当か。

## 6. 規約上の可否（確認済み）

`asp3_core/docs/porting/PORTING_GUIDE.md` §外部（SDK）ターゲットの置き方：
- `ASP3_TARGET_DIR` が指すdirの `target.cmake` が include される（∴target.cmakeは移動不可）
- **ファイル供給（`ASP3_*_FILES`）は絶対パスで積む**のが既存規約
- 骨格例自体が `set(CHIPDIR ${CMAKE_CURRENT_LIST_DIR}/../../arch/<arch_dir>/<chip>)` と
  **target dir の外**を参照している

⇒ `esp/` 配下からファイルを積むことは**規約違反ではない**。

## 7. リスクと検証

- 検証コスト：3チップ×(素カーネル/WiFi+lwIP/BLE)=**9構成**の再ビルド＋実機GREEN。
  本日この9構成での `ninja -t deps` 実測とC3/C5/C6実機GOT-IP+pingを実施済み＝手順は確立。
- 論理変更ゼロ（パス移動のみ）なのでバイナリ同一性で検証可能なはず。
  ただし `__TIME__` 埋め込みがあるため md5 は不正な計器（過去に踏んだ）。
  per-object 逆アセンブル比較かサイズ一致で見る。
- **本日の教訓**：`apps/` 改名時に `ASP3_APPLNAME STREQUAL` の文字列一致が黙って壊れる寸前だった。
  **同種の«パス/名前の文字列一致に依存した隠れ結合»が他にもある可能性**。要探索。
