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
| Espressif riscv32-esp-elf 14.2.0 | `riscv32-esp-elf-` | 未検証（xpack15で両方PASSのため未実施） | 未検証（同左） |
| Espressif riscv32-esp-elf 15.2.0 | `riscv32-esp-elf-` | 未検証（同左） | 未検証（同左） |

xpack15 で WiFi・BLE とも成功したため，タスク計画上「両方動くコンパイラを
探す」ための esp-14.2/esp-15.2 への切替検証は不要と判断（計画の条件分岐＝
xpack15 が失敗した場合のみ次を試す）。他2系統の可否は未確定（「動かない」
ではなく「未検証」）。

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
- 注意：C5 は `ESP32C5_BT AND ESP32C5_WIFI` 同時 ON が
  `target.cmake` で `FATAL_ERROR`（RAM予算，C3/C6と同じ制約）。∴「WiFi+BLE
  両方動く」は「同一コンパイラで両方の“別バイナリ”がそれぞれ動く」の意味
  （coexist 1バイナリは対象外・別テーマ）。

## 関連

- `docs/c5-bringup.md` 実施53（本ラウンドの詳細ログ）。
- `docs/wifi-shim.md`・`docs/c5-bringup.md` 実施48-52（hal(v8) WiFi 実証の来歴）。
- `docs/bt-shim.md`「D-2d bond真因確定」（C5 BLE bond 実証の来歴）。
- memory: `c5-wifi-hal-v8-scan-works`・`c5-canonical-compiler`（本ラウンドで新規追加）。
