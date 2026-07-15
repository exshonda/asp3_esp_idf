# ESP32-C5 正典（canonical）コンパイラ

C5 の WiFi と BLE を「実際に動く」状態で確定するために実測したツールチェーン
基準。C6 の BLE PHY-init（`register_chipv7_phy`）ツールチェーン依存議論の
基準として使う（C6 board 自体には不接触・別タスクが担当）。詳細な実施記録は
`docs/c5-bringup.md` 実施53。

## 結論

**C5 の正典コンパイラ＝xpack `riscv-none-elf-gcc` 15.2.0**

```
/home/honda/opt/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin
CMake: -DRISCV64_TOOLCHAIN_PREFIX=riscv-none-elf-
```

2026-07-15 実測（tree HEAD `ae21e7a`，board=C5#1 `d0:cf:13:f0:a7:44`）で
**WiFi(scan) と BLE(adv) の両方**が同一セッション・同一ツリーで実機動作を
実証済み。判定は必ず受信側／コンソールの実測（BLE＝ホスト側 BlueZ
可視性，WiFi＝scan で実 AP 検出）で行い，LP_AON 等のマーカ単独では判定
しない（マーカは過去ブートの到達しか示さない）。

## 動作表（実測）

| コンパイラ | prefix | BLE（`ble_host_smoke_c5`，BlueZ可視） | WiFi（`wifi_scan`，AP検出） |
|---|---|---|---|
| **xpack riscv-none-elf-gcc 15.2.0** | `riscv-none-elf-` | **PASS**（実測 2026-07-15：`ASP3-C5-BLE` BlueZ可視） | **PASS**（実測 2026-07-15：`20 APs found (err=0)`，実SSID確認） |
| **Espressif riscv32-esp-elf 15.2.0**（crosstool-NG） | `riscv32-esp-elf-` | **PASS**（実測 2026-07-15：ビルド壁ゼロ・BlueZキャッシュ判別で放射実証） | **PASS**（実測 2026-07-15：`20 APs found (err=0)`，実SSID確認） |
| Espressif riscv32-esp-elf 14.2.0 | `riscv32-esp-elf-` | 未検証（esp-15.2で両方PASSのため未実施） | 未検証（同左） |

xpack15・esp-15.2 とも WiFi・BLE両方成功。esp-14.2 は計画上の条件分岐
（正典xpack15が失敗した場合のみ次を試す）に該当せず未実施（「動かない」
ではなく「未検証」）。

## Espressif esp-15.2.0 実測（本ラウンド）

**目的**＝姉妹プロジェクトが ESP 側 toolchain をメインにしている
（P4＝`riscv32-esp-elf` esp-14.2.0 で Ethernet 動作実証済み・S31＝
`riscv32-esp-elf` esp-15.2.0 使用、他リポジトリもESP側toolchainメイン）
ため、C5 も esp toolchain に揃えられるかを実測で確認した
（`memory/c5-canonical-compiler.md` 参照）。

- toolchain: `/home/honda/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin`
  （`riscv32-esp-elf-gcc` ほか。crosstool-NG ビルド，ESPパッチ入り。同じ
  GCC 15.2.0 だが xpack とは別配布）。
- ヘルパ：`tmp/c5ble_esp15.sh`・`tmp/c5wifi_esp15.sh`（正典 `tmp/c5ble.sh`・
  `tmp/c5wifi.sh` からの差分は3点のみ＝GCC_BIN／
  `-DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`／別 BUILD dir。
  xpack ビルドと衝突させず A/B 比較可能）。
- **ビルド壁＝ゼロ**。C5 chip.cmake の固定フラグ
  `-march=rv32imc_zicsr_zifencei -mabi=ilp32` が esp-15.2 の multilib
  （`riscv32-esp-elf-gcc -print-multi-lib` で確認）に厳密一致で存在した
  ため，march/mabi 起因のリンク壁は発生せず。newlib/sys-lock/
  implicit-function-declaration 等の対処も不要（xpack15と同じ
  `-Wno-error=implicit-function-declaration` のみで両方ビルド完走）。
  asp3/target・hal・kernel への差分追加は **無し**（ヘルパのcmake引数変更
  のみで両立）。
- BLE 判定：BlueZ キャッシュの偽陽性を避けるため
  `bluetoothctl remove D0:CF:13:F0:A7:44` で既知エントリを消してから
  esp-15.2 ビルドを flash→boot→再scan し，`ASP3-C5-BLE` が **再出現**する
  ことで実放射を確認（マーカ単独判定ではなく受信側の実測。実施53と同じ
  A7:44 で再現性も確認：2回目の remove→scan でも同様に再出現）。
- WiFi 判定：`tmp/c5wifi_esp15.sh console 20` で
  `wifi_scan: 20 APs found (err=0)` と実SSID（`<SSID-INST-1X>`／`<SSID-INST-G>`／
  `<SSID-INST-1XG>`）を確認。以降のコンソール行が化けるのは
  `memory/c5-wifi-hal-v8-scan-works.md` に既知のUART氾濫アーティファクト
  （xpack15ビルドでも同様）であり，本ツールチェーン固有の不具合ではない。
  WDT/panic なし＝board latch 無し。
- **結論：esp-15.2（crosstool-NG，ESP公式配布）は C5 の WiFi(scan)・
  BLE(adv) いずれも xpack15 と同一の対処（`-Wno-error=implicit-function-declaration`
  のみ）でビルド・実機動作する。march/mabi 完全一致のため，march起因の
  移植コストはゼロ**。

### エコシステム整合（姉妹プロジェクトのtoolchain）

| プロジェクト | ターゲット | メインtoolchain | 実測状況 |
|---|---|---|---|
| （本プロジェクト自身） | ESP32-C5 | xpack riscv-none-elf-gcc 15.2.0（正典） | WiFi/BLE両方PASS（実施53） |
| P4 | （Ethernet実装、姉妹プロジェクト） | Espressif riscv32-esp-elf esp-14.2.0 | Ethernet動作実証済み |
| S31 | （姉妹プロジェクト） | Espressif riscv32-esp-elf esp-15.2.0 | （姉妹プロジェクト側で使用中） |

本ラウンドの実測で，C5 の xpack正典と esp-15.2 の間に動作差は見つからな
かった（両方PASS・ビルド壁ゼロ）。よって **C5 は esp toolchain へ切替
可能**（xpack15を正典として維持しつつ，エコシステム整合が必要になった
場合は esp-15.2 へ切替えても WiFi/BLE の実機動作は損なわれない見込み）。
ただし本ラウンドは C5#1 board・open scan・adv smokeのみの検証であり，
5GHz connect・BT bond 等の全機能面での esp-15.2 再検証は未実施（範囲外）。

## 実測ログ（要旨）

### BLE

```
$ tmp/c5ble.sh build ble_host_smoke_c5     # xpack15, 0 link errors, RAM 77.80%
$ tmp/c5ble.sh flash                       # write-flash 0x0 -> RTS boot
$ bluetoothctl devices | grep A7:44
Device D0:CF:13:F0:A7:44 ASP3-C5-BLE
```
（近隣43台が同時に見えており受信側の健全性も確認。43台中の1台として
`ASP3-C5-BLE` を確認＝機器固有ではなく実放射の証拠）

### WiFi

```
$ tmp/c5wifi.sh build wifi_scan            # xpack15, -DESP32C5_WIFI=ON(既定hal v8)
                                            # 0 link errors, RAM 76.05%
$ tmp/c5wifi.sh flash
$ tmp/c5wifi.sh console 20                 # pyserial dtr=False,rts=False で UART0(ttyUSB3)読取
wifi_scan: esp_wifi_init
wifi_scan: esp_wifi_start -> 0
wifi_scan: esp_wifi_scan_start -> 0
wifi_scan: 20 APs found (err=0)
  [0] <SSID-INST-1XG> (rssi=-48 ch=11)
  [1] <SSID-INST-1X> (rssi=-48 ch=11)
  [2] <SSID-INST> (rssi=-49 ch=11)
  ...
```
WDT/panic なし＝board latch（`memory/c5-latched-board-state.md`）無し。

## 固定化した箇所

- `tmp/c5ble.sh`：`GCC_BIN=.../xpack-riscv-none-elf-gcc-15.2.0-1/bin` 固定（既存。
  本ラウンドで実測により正典と確認）。
- `tmp/c5wifi.sh`（本ラウンド新規）：同じ xpack15 を固定。`c5ble.sh` の WiFi 版
  （build/flash/boot/console）。console サブコマンドは pyserial
  `dtr=False,rts=False` 読取りを実装（`cat`/`stty` は DTR/RTS assert で
  0 バイトになる既知の罠＝`memory/c5-wifi-hal-v8-scan-works.md`）。
- `tmp/c5ble_esp15.sh`・`tmp/c5wifi_esp15.sh`（esp-15.2ラウンドで新規）：
  上記2本のesp toolchain版。GCC_BIN／`RISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`／
  BUILD dir（`build/c5ble_esp15`・`build/c5wifi_esp15`）の3点のみが差分。
- 注意：C5 は `ESP32C5_BT AND ESP32C5_WIFI` 同時 ON が
  `target.cmake` で `FATAL_ERROR`（RAM予算，C3/C6と同じ制約）。∴「WiFi+BLE
  両方動く」は「同一コンパイラで両方の“別バイナリ”がそれぞれ動く」の意味
  （coexist 1バイナリは対象外・別テーマ）。

## 関連

- `docs/c5-bringup.md` 実施53（本ラウンドの詳細ログ）。
- `docs/wifi-shim.md`・`docs/c5-bringup.md` 実施48-52（hal(v8) WiFi 実証の来歴）。
- `docs/bt-shim.md`「D-2d bond真因確定」（C5 BLE bond 実証の来歴）。
- memory: `c5-wifi-hal-v8-scan-works`・`c5-canonical-compiler`（実施53で新規追加、
  本ラウンド〔esp-15.2実測〕で `c5-canonical-compiler` に追記）。

## C3 Espressif esp-15.2.0 実測（2026-07-15，追加ラウンド）

**目的**＝C5 で確認したエコシステム整合パターン（xpack正典 vs esp toolchain）
を C3 でも実測確認する。C3 の正典（現行）＝xpack `riscv-none-elf-gcc` 15.2.0
（`tmp/c3ble.sh`）。BLE は D-2c/D-2d 達成済み（`docs/bt-shim.md`）。

**board**：C3 実機 `60:55:F9:57:BA:BC`（by-id `usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:57:BA:BC-if00`）。C6（`14:C1:9F` port1・BT ハンドオフ調査中）・C5#1（`A7:44`）・
C5#2（`C8:94`）には不接触。

### 動作表（C3・実測追加）

| コンパイラ | prefix | BLE（`ble_host_smoke`，BlueZ可視） | kernel QEMU（`test_porting`） |
|---|---|---|---|
| **xpack riscv-none-elf-gcc 15.2.0**（正典） | `riscv-none-elf-` | PASS（既存・D-2c/D-2d） | PASS（既存 `build/c3_test_porting_qemu`） |
| **Espressif riscv32-esp-elf 15.2.0** | `riscv32-esp-elf-` | **PASS**（実測：ビルド壁ゼロ・BlueZで`ASP3-C3-BLE`放射を`remove`後の再scanで確認） | **PASS**（実測：`# 6/6 passed`，xpackと同一tree/同一結果） |

### 詳細

- toolchain: `/home/honda/tools/espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin`
  （`riscv32-esp-elf-gcc` crosstool-NG，ESPパッチ入り。C5 実測と同一配布）。
- ヘルパ：`tmp/c3ble_esp15.sh`（正典 `tmp/c3ble.sh` からの差分は3点のみ＝
  GCC_BIN／`-DRISCV64_TOOLCHAIN_PREFIX=riscv32-esp-elf-`／別 BUILD dir
  `build/c3ble_esp15`。`-DESP32C3_QEMU=OFF`（csrw mie 罠回避）・csrw mie
  自己検査は `c3ble.sh` から継承）。
- **march/mabi**：C3 chip.cmake 固定 `-march=rv32imc_zicsr_zifencei -mabi=ilp32`
  が esp-15.2 の multilib（`riscv32-esp-elf-gcc -print-multi-lib`）に厳密
  一致で存在（C5 と同一パターン）。march/mabi起因のビルド壁なし。
- **BLE ビルド壁＝ゼロ**：`ble_host_smoke`（`ESP32C3_BT=ON ESP32C3_BT_SM=ON`）
  が `-Wno-error=implicit-function-declaration` のみ（xpackと同一対処）で
  リンク完走。RAM 93.53%（xpack実測と近似・非回帰）。
  `CMakeFiles/3.28.3/CMakeCCompiler.cmake` の `CMAKE_C_COMPILER` が
  esp-15.2 のフルパスであることを機械的に確認（PATH推測ではない実証）。
- **BLE 実機動作**：flash→RTS boot 後，RTC マーカ
  （`0x50 SYNC`=host sync／`0x5C ADV`=advertising開始／`0xC4 adv-rc`=0）
  で到達を確認。ただしマーカ単独では判定しない
  （`memory/feedback_hardware_investigation_rigor.md`）ため，以下2点で
  裏取り：
  1. **clean console 実測**（`tmp/clean_boot_capture.py`，RTSパルスで
     単発クリーンリセット→開いたfdでそのまま12秒受信）：バナー
     `TOPPERS/ASP3 Kernel Release 3.7.2 for ESP32-C3 (Jul 15 2026,
     13:38:34)` を確認。このタイムスタンプ文字列は
     `build/c3ble_esp15/asp.elf` に埋め込まれた文字列と完全一致（`strings`
     で確認）＝**esp-15.2ビルドそのものが今まさに起動している**ことの
     直接証拠（xpackビルド由来の残存マーカではない）。続けて
     `ble_host_smoke: advertising started as 'ASP3-C3-BLE'`・
     `Phase D-2 milestone reached` まで到達を確認。
  2. **BlueZ 実放射（reproducibility 2回）**：
     `bluetoothctl remove 60:55:F9:57:BA:BC` で既知エントリを消してから
     再scanし，`ASP3-C3-BLE` が**再出現**することを2回連続で確認
     （初回は1回目のscan(15s)では未検出，2回目のより長いscan(20s)で
     検出＝BLE advの間欠性，デバイス側の問題ではない。C5実測と同じく
     remove→再scanのサイクルを2回実施して再現性を確保）。
- **kernel QEMU 非回帰**：`test/porting`（`ASP3_APPLDIR=test/porting`）を
  esp-15.2 で configure/build。**注意**：C3 の `target_kernel_impl.c` は
  `esp_rom_set_cpu_ticks_per_us()`（ROM関数．esp32c3.rom.ld由来の絶対
  アドレスシンボル）を無条件で呼ぶが，このROMリンカスクリプトは
  `esp_wifi.cmake`／`esp_bt.cmake` が `ESP32C3_WIFI` または `ESP32C3_BT`
  ON時にのみ `-Wl,-T` で注入する（`target.cmake` 単体では注入されない）。
  よって test_porting を素の設定でconfigureすると
  **xpackでもesp-15.2でも同様に** `undefined reference to
  esp_rom_set_cpu_ticks_per_us` でリンク失敗する（既存の
  `build/c3_test_porting_qemu` を確認したところ実際に `-DESP32C3_WIFI=ON`
  が付与されていた＝ツールチェーン非依存の既知の前提条件であり，
  esp-15.2固有の壁ではない）。同じく `-DESP32C3_WIFI=ON` を付与して
  configureしたところ0リンクエラーで完走（RAM 82.25%）。
  QEMU実行結果は `# 6/6 passed`（xpackビルドの`build/c3_test_porting_qemu`
  を同一QEMUで実行した結果とバイト単位で同一の出力構造・"no time event"
  1回のみで非フラッド＝両ビルド共通の既知アーティファクト，
  esp-15.2固有の回帰ではない）。
- **結論**：C3 も esp-15.2（crosstool-NG，ESP公式配布）で
  BLE(adv, BlueZ可視)・kernel(QEMU test_porting 6/6) いずれも xpack15 と
  同一の対処（`-Wno-error=implicit-function-declaration`・
  `-DESP32C3_WIFI=ON` for test_porting のみ，これはツールチェーン非依存）
  でビルド・動作する。march/mabi完全一致のためmarch起因の移植コストは
  ゼロ。**C3もエコシステム整合のためesp toolchainへ切替可能**（xpack15を
  正典として維持しつつ）。
- **C6 は本ラウンド対象外**：BT ハンドオフ調査中の別サブエージェントが
  board（port1）を占有・`ble_host_smoke_c6.c` をWIP編集中のため，
  esp-15.2 検証は**保留**（C6 board には不接触）。

### エコシステム整合（更新）

| プロジェクト/ターゲット | メインtoolchain | 実測状況 |
|---|---|---|
| 本プロジェクト C5 | xpack riscv-none-elf-gcc 15.2.0（正典） | WiFi/BLE両方PASS（esp-15.2もPASS，上記） |
| 本プロジェクト C3 | xpack riscv-none-elf-gcc 15.2.0（正典） | BLE PASS（esp-15.2もPASS・kernel QEMU 6/6も同一，本ラウンド） |
| 本プロジェクト C6 | xpack riscv-none-elf-gcc 15.2.0（正典） | esp-15.2検証は保留（BTハンドオフ調査中） |
| P4 | Espressif riscv32-esp-elf esp-14.2.0 | Ethernet動作実証済み |
| S31 | Espressif riscv32-esp-elf esp-15.2.0 | （姉妹プロジェクト側で使用中） |

C5・C3 の2チップで esp-15.2 との動作差が見つからなかったことから，
**asp3_esp_idf 全体としてもesp toolchainへの切替は技術的には可能**という
所見が補強された（C6 は BT ハンドオフ決着待ちで未実測）。
