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
