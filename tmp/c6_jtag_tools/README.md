# C6 Wi-Fi調査用 OpenOCD JTAGツール（追記16〜21）

使い方（共通）：
```bash
OCD=/home/honda/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32
export OPENOCD_SCRIPTS=$OCD/share/openocd/scripts
$OCD/bin/openocd -f board/esp32c6-builtin.cfg -f <script>.tcl
```

- `read6b.tcl` — RFシンセ(regi2c block 0x6b)のreg0-15を生トランザクションで
  読む（I2C1_CTRL 0x600af804へ cmd=(reg<<8)|0x6b、data=CTRL[23:16]）。
  **RFデバイスはscan中のみ応答**（scan後は全0xff）。
- `write6b.tcl` — 同block のreg2/4/11/13/14へnative値を書く
  （cmd=0x05000000|(data<<16)|(reg<<8)|0x6b）＋読み戻し検証。
- `check6b.tcl` — 上記5regの現在値確認。
- `poke_mac.tcl` — **追記21ブレークスルー**：MAC/WDEV空間の42安定差分
  （native vs ASP3）をnative値へ一括書き込み。これでMAC割込みが
  11固定→~170/秒で発火開始した。次段=これを分割して二分探索。

観測ポイント：
- MAC割込みディスパッチ数: esp_shim_int_count[1]（アドレスはビルド毎に
  nmで確認。例 0x4081bd2c）
- INTMTX生ステータス: mdw 0x60010134（bit0=WIFI_MAC）
- RTC-RAM計測: 0x50000080=phy_enable入場 / 88=register_chipv7_phy入場
