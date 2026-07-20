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

---

# 8. ★レビュー結果と訂正（2026-07-20・Codex + fable 並走、全指摘を独立検証済み）

**結論：この計画は書かれたままでは実行できない。両レビュアが独立に「段階Aのみ・段階Bは見送り」で一致した。**

## 8-1. 上記 §2・§3 の実測値は誤り（訂正）

私（Claude）の測定に5件の誤りがあった。全て `file:line` で再現確認済み：

| 誤り | 実際 |
|---|---|
| 「`target_*.h` のESP依存 0」 | **`target_timer.h:46` が `#include "hal/systimer_ll.h"`**。私は `target_timer.c` は測ったが **`.h` を測っていなかった** |
| 「`.ld` のESP依存 0」 | **`esp32c3.ld:27` が `INCLUDE esp32c3.peripherals.ld`**（esp-idf の soc component 由来） |
| 「ASP3側26/ESP側5 で3チップ対称」 | **26/28/27 で非対称**。私の grep パターン `^target|\.ld$|^flash_header|usbjtag|^run\.cmake` が **C5 `pmu_instance.c`・`seam_appdesc.c`・`esp32c5_seam.ld`、C6 `cold_clk_init_c6.c` を機械的に漏らした**＝対称性は測定の人工物。しかもこの超過分は**esp-idf 依存が最も濃い**ファイル群 |
| 「C3で測ったので3チップ同じ」 | **外挿が崩れる**。C5 `target_kernel_impl.c` は **1794行**（C3の4.6倍）で `#include "esp_rom_sys.h"`。C6 `cold_clk_init_c6.c` は esp-idf ヘッダ4本 |
| §0 の共有資産リスト | **4件漏れ**（8-2） |

## 8-2. ★C6 には «逆向き» の依存がある（新発見・分割線が一方向でない）

`asp3/target/esp32c6_espidf/target_kernel_impl.c` が **ESP shim のシンボルを7箇所で `extern` 宣言して呼ぶ**
（:268 `esp_shim_coex_adapter_register` / :292,:374 `esp_shim_bt_pmu_diag` / :322,:437 `esp_shim_bt_pmu_init` /
:390 `esp_shim_cold_cpu_clk_init` / :405 `esp_shim_cold_recalib_bbpll`）。
実体は `bt/bt_pmu_init_c6.c`・`wifi/esp_shim.c`＝**ESP側へ移すディレクトリ**。
∴「カーネルポート → ESP統合」の一方向ではなく、**カーネルポートが ESP統合を呼び返す**。

## 8-3. §3 のレイアウトどおりに移すと C5/C6 がビルド不能になる

- **`esp_shim.h` / `esp_shim_cfg.h` は C3 の `wifi/` にしか存在しない**（実測：C5 `wifi_v8/`・C6 `wifi/` には
  `esp_shim_chip_regs.h` のみ）。C5 `bt/bt_shim.c:89`・C6 `wifi/esp_shim.c:36`・
  `common_espidf/wifi/esp_shim_core.c:42` が C3 側の実体を include している。
  **§3 の `esp/common` 列挙にこの2ヘッダが無い**＝列挙どおり移すと C5/C6 が壊れる。
- **`${C3_TARGETDIR}/bt/stub/include` も3チップ共有**（C5 `esp_bt.cmake:224`・C6 `esp_bt.cmake:284` ほか）。
  §0 のリストに無い。C5/C6 の自前 stub は `bt_nimble_config.h`＋`FreeRTOSConfig.h` のみで、
  **`freertos/*.h` 8本と `esp_partition.h` は C3 にしか無い**。

## 8-4. 「パス書き換えのみで論理変更なし」（§4）は誤り

1. **`TARGETDIR` が «ESP資産の在処» として使われている**（`esp_bt.cmake:39` `set(BT_TARGETDIR ${TARGETDIR}/bt)`、
   `${TARGETDIR}/wifi` 等）。段階B後も `TARGETDIR` は ASP3 target dir を指すので、
   **`ESP_CHIP_DIR` / `ESP_COMMON_DIR` のような新変数の導入が必須**。
2. **ディレクトリ名がソース/cfg に焼き込まれている**：
   - `wifi/esp_shim.cfg:4` → `#include "wifi/esp_shim_cfg.h"`（`wifi/` が焼き込み）
   - `bt/bt.cfg:4`・`bt/bt_shim.c:37` → `#include "bt/bt_cfg.h"`（`bt/` が焼き込み）
   ⇒ **`esp/common` 直下に平坦に置くと解決不能**。`esp/common/wifi/`・`esp/cN/bt/` と
   **サブディレクトリ構造を保存**すれば書き換え不要（fable の提案・妥当）。
3. **移動対象ファイル自身が階層依存の相対パスを持つ（4箇所）**：
   `esp32c5_espidf/esp_bt.cmake:40`・`esp32c6_espidf/esp_bt.cmake:36`（`../esp32c3_espidf`）、
   `esp32c6_espidf/esp_wifi.cmake:323`・`esp32c5_espidf/esp_wifi_v8.cmake:333`（`../../../esp-idf`）。
   前者は移動後に不存在パスを指し**ビルドエラー（大きな音で壊れる＝まだ良い）**、
   後者は `if(NOT DEFINED IDF_V554)` ガード内なので**黙って死ぬ潜在バグ**になる。

## 8-5. §5 の未決定論点への回答（両レビュア一致）

- **5-1**：**(b)** で一致。ただし「現状維持」ではなく **ESP資産の所在を `ESP_COMMON_DIR`/`ESP_CHIP_DIR` で明示**すべき。
- **5-2**：「target port が esp-idf に依存しない」は**現実と合わない**（8-1・8-2 で実証）。
  達成可能な定義は「**`asp3/` 配下に ESP統合ソース・shim・blob adapter を置かない**」まで。
- **5-3**：段階Aの分岐は許容。段階Bの分岐はコストが便益を上回る。

## 8-6. 検証戦略（§7）の不足

- fresh build dir での configure 検証、`ASP3_APPLNAME` による NimBLE 自動有効化の確認、
  旧パス残存の監査、`ninja -t deps` で旧パスが依存に残っていないことの確認を追加する。
- バイナリ同一性は md5 でなく **debug情報を除いた `.text/.rodata/.data` 比較**。
  ★**C5 seam 構成は `seam_appdesc.c:110-111` が `__TIME__`/`__DATE__` を埋めるため
  同一性判定から除外**する（さもなくば偽の差分を追うことになる）。

## 8-7. 改訂方針

- **段階A のみ実行**する。ただし §0 の共有資産リストを 8-3 の漏れ2件で補正し、
  `common` 配下は **`wifi/`・`bt/stub/` のサブディレクトリ構造を保存**する。
- **段階B は見送り**。C6 の逆向き依存（8-2）と焼き込みディレクトリ名（8-4）を先に解消しない限り、
  「物理配置を整える」便益に対してリスクが見合わない。
- C5 の `pmu_instance.c`・`seam_appdesc.c`・`esp32c5_seam.ld`、C6 の `cold_clk_init_c6.c` を
  ASP3側/ESP側どちらに置くかは**未決**。ここを決めないと §0 の動機は C5/C6 で達成されない。
