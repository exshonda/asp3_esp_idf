# C6 Wi-Fi調査 セッションハンドオフ（2026-07-06時点・別PC作業用）

全経緯の正本は `tmp/c6_step0_findings.md`（追記1〜21）。本書は**別PCで
作業を再開するための環境・手順・現在地**のみをまとめる。

## 現在地（1行で）

RXブロッカーは**WiFi MAC/WDEV空間（0x600A4xxx等）の未設定レジスタ42個**まで
局在化済み。42個をnative値へJTAG移植するとMAC割込みが発火開始
（11固定→~170/秒）。ただしまだ0 AP（データパス未達）。
**次の一手＝42個の二分探索**（`tmp/c6_jtag_tools/poke_mac.tcl`を分割）。

## 必要な環境（別PCで揃えるもの）

1. **ESP-IDF v6.1**（native参照ビルド＋esptool＋python環境）
   - 本機では `/home/honda/tools/esp-idf-v6.1` ＋ `IDF_TOOLS_PATH=/home/honda/tools/espressif`
   - `source <idf>/export.sh` で esptool.py / riscv32-esp-elf-gcc が入る
2. **OpenOCD-esp32**（v0.12.0-esp32-20260424相当）
   - `OPENOCD_SCRIPTS=<openocd-esp32>/share/openocd/scripts`
   - 使用cfg: `board/esp32c6-builtin.cfg`（内蔵USB-JTAG）
3. **xpack riscv-none-elf-gcc**（objdump/nm用。riscv32-esp-elfでも代用可）
4. ハードウェア：ESP32-C6ボード（XIAO ESP32C6, rev v0.2）
   - **native USB → ttyACM0**（esptool書き込み・JTAG・リセット用）
   - **UART0(TX/RX) → FT232R → ttyUSB0**（コンソール観測用）
   - FT232Rは電源トグルで復帰しない＝**物理抜き差しのみ**。
     ボードのリセットは `esptool --before default-reset --after hard-reset flash-id` で行う（電源トグル不要）

## ビルド

```bash
# ASP3側（wifi_scan・Direct Boot）
export PATH=<riscv32-esp-elf>/bin:$PATH   # nm/objcopyが要る
cmake -B build/wifi_scan-c6-direct \
  -DASP3_TARGET_DIR=$PWD/asp3/target/esp32c6_espidf \
  -DASP3_APPLDIR=$PWD/apps/wifi_scan -DASP3_APPLNAME=wifi_scan \
  -DCMAKE_C_FLAGS="-Wno-error=implicit-function-declaration" \
  asp3/asp3_core   # （既存build/ディレクトリがあれば cmake --build のみでよい）
cmake --build build/wifi_scan-c6-direct
python3 -c "d=open('build/wifi_scan-c6-direct/asp_flash.bin','rb').read(0x100000); open('build/wifi_scan-c6-direct/asp_flash_trunc1M.bin','wb').write(d)"

# native側（参照・受信できる版。scan後ジャンプせず再scanループに改造済み）
cd tmp/c6_handoff_source && idf.py set-target esp32c6 && idf.py build
```

## フラッシュ＆観測（確立手順）

```bash
# ASP3（Direct Boot＝0x0にtrunc1M）
esptool.py --chip esp32c6 --port /dev/ttyACM0 --before default-reset --after hard-reset \
  write-flash 0x0 build/wifi_scan-c6-direct/asp_flash_trunc1M.bin
# native（2段ブート）
esptool.py --chip esp32c6 -b 460800 --port /dev/ttyACM0 --before default-reset --after hard-reset \
  write-flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin \
  0x10000 build/c6_handoff_source.bin
# コンソール: ttyUSB0 115200bps（ASP3は"RESCAN N APs"を約2.5秒毎に出力）
# JTAG読み:
openocd -f board/esp32c6-builtin.cfg -c init -c halt -c "mdw <addr> <n>" -c resume -c exit
```

## 次の一手：42レジスタの二分探索

1. ASP3を焼いてRESCANループ確認（0 APs）
2. `tmp/c6_jtag_tools/poke_mac.tcl` の mww 行を半分に割った版を作って実行
3. **判定＝esp_shim_int_count[1]**（MAC線ディスパッチ数）が増加し始めるか
   - アドレスは**ビルド毎にnmで確認**：
     `nm build/wifi_scan-c6-direct/asp.elf | grep esp_shim_int_count`
     配列[1]=ベース+4。移植前は11程度で固定、効くと数百/秒で増加
4. 効いたレジスタを特定したら：
   - libpp逆アセンブル（`objdump -d libpp.a`でそのアドレスのlui/store箇所）
     とNuttX（apache/nuttx arch/risc-v/src/esp32c6/）から設定経路を逆引き
   - 全42移植状態で sta_rx_cb / promisc_rx_count / RESCAN APs を確認
     （フレームがデータパスに乗るか）

## リポジトリに入っている資産

- `tmp/c6_jtag_tools/`：JTAGツール（read6b/write6b/check6b/poke_mac.tcl）
  ＋MAC空間dump生データ＋42差分リスト＋README
- `tmp/c6_handoff_source/`：native参照ビルド（再scanループ・regi2c読み戻し付き）
- `apps/wifi_scan/wifi_scan.c`：RESCANループ・RTC-RAM計測
  （0x50000080=phy_enable入場/88=register_chipv7_phy入場等）
- `tmp/c6_step0_findings.md`：全調査記録（追記1〜21）

## リポジトリに入っていないもの（別PCで再現が必要なら）

- **kernel `asp3/asp3_core/kernel/time_event.c` のログ無効化**（submodule内
  ローカル変更・未コミット＝禁則領域の一時診断）。
  再適用する場合：line 625 の `syslog_0(LOG_NOTICE, "no time event...")` を
  コメントアウト（観測性向上のみ・無くても調査は可能）。
- NuttX/Zephyrのクローン（公開GitHubから取得可）：
  `apache/nuttx`（arch/risc-v/src/esp32c6/esp_wifi_adapter.c）、
  `zephyrproject-rtos/hal_espressif`（zephyr/esp32c6/src/soc_init.c）
- ~~Codex CLI~~（任意）：`npm install -g @openai/codex`（ユーザ領域）＋
  ChatGPTログイン。CodexはローカルFS読めずGitHub経由なのでpush必須。

## 既知の罠（本機で踏んだもの）

- nm由来のRAMアドレスは**ビルド毎に変わる**（wifi_tr_count誤読の教訓）
  → 計測はRTC-RAM固定番地（0x50000040〜）が確実
- regi2cのRFブロック(0x6b)は**scan中のみ応答**（scan後は全0xff）
- native(IDF)ではROMのSRAM上部テーブルがheap回収で壊れる・regi2cバスは
  トランザクション毎gate → nativeでのregi2c読みは
  `regi2c_ctrl_read_reg_mask`（ELFシンボル）か生JTAGトランザクションで
- pyserialでttyACM0を読むとハングすることがある → ttyUSB0のみ読む
