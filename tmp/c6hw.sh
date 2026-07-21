#!/usr/bin/env bash
#
#  C6ハンドオフ実験のための実機操作ヘルパ（このマシン固有）
#
#  このボード（XIAO ESP32-C6）は複数USB経路から給電されるため，
#  確実なリセットは Acroname USBHub3c で全ポートOFF→port4,5 ON．
#  esptoolの同期窓は電源ON後〜約5秒（c6_handoff_sourceが起動から
#  scan+2秒でジャンプ→ハングするため）．全OFF→ON直後にttyACM0出現を
#  高速ポーリングして即esptoolを撃つと窓に入る．
#
#  使い方:
#    c6hw.sh off                 全ポートOFF（真の電源断）
#    c6hw.sh on                  port4,5 ON（ボード復帰）
#    c6hw.sh cycle               全OFF→(5s)→port4,5 ON（＝リセット）
#    c6hw.sh flash <bin> [off]   電源レースでダウンロードモード同期し
#                                <bin>を[off]（既定0x200000）へ書込み(no-reset)
#    c6hw.sh flashfull <dir>     <dir>のbootloader/partition/app(0x0系)＋
#                                asp_flash_trunc1M.bin(0x200000)を一括書込み
#    c6hw.sh run                 リセットして実行（cycleと同じ）
#
#  観測は Monitor ツールで /dev/ttyUSB0（CP2102, 115200）を読む．
#
set -u
HUB=$HOME/tools/acroname/usbhub3c_ctl.py
ESPTOOL_ENV=$HOME/tools/espressif/python_env/idf6.1_py3.12_env/bin
export PATH="$ESPTOOL_ENV:$PATH"
PORT=/dev/ttyACM0

off_all() { for p in 1 2 3 4 5; do python3 "$HUB" off $p >/dev/null 2>&1; done; }
on_board() { python3 "$HUB" on 4 >/dev/null 2>&1; python3 "$HUB" on 5 >/dev/null 2>&1; }
wait_acm() { for i in $(seq 1 40); do [ -e "$PORT" ] && return 0; sleep 0.05; done; return 1; }

case "${1:-}" in
  off)   off_all; echo "all ports OFF" ;;
  on)    on_board; echo "port4,5 ON" ;;
  cycle|run) off_all; sleep 5; on_board; echo "power-cycled (reset)" ;;
  flash)
    bin="$2"; offset="${3:-0x200000}"
    off_all; sleep 5; on_board; wait_acm
    timeout 25 esptool.py --chip esp32c6 --port "$PORT" --after no-reset \
      write-flash "$offset" "$bin" 2>&1 | tail -4
    ;;
  flashfull)
    d="$2"
    off_all; sleep 5; on_board; wait_acm
    timeout 40 esptool.py --chip esp32c6 --port "$PORT" --after no-reset write-flash \
      0x0 "$d/bootloader/bootloader.bin" \
      0x8000 "$d/partition_table/partition-table.bin" \
      0x10000 "$d/c6_handoff_source.bin" 2>&1 | tail -4
    ;;
  *) echo "usage: c6hw.sh {off|on|cycle|run|flash <bin> [offset]|flashfull <idfbuilddir>}" ;;
esac
