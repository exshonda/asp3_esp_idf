# Target (ESP32-C5) awareness helpers for gdb OS-awareness (ASP3).
#
# pico2_riscv_gcc版からの流用。
#
# 役割: ターゲット（ボード）依存の知識。本ボードはチップ(ESP32-C5/INTMTX)の
#       機能をそのまま使い，現時点でボード固有の追加項目は無いため，
#       chip_os_awareness の API を再エクスポートする。
#
# chip_os_awareness.py は arch/riscv_gcc/esp32c5 にあるため，本ファイルからの
# 相対パスで sys.path に追加してから import する。os_awareness.py
# （scripts/gdb_os_aware/）はこの target_os_awareness を（発見できれば）import
# して，割込み状態の表示等に用いる。

import os
import sys

# 下位層(chip/core)の import で .pyc を生成させない（ソースツリーに __pycache__ を残さない）。
sys.dont_write_bytecode = True

sys.path.insert(0, os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../arch/riscv_gcc/esp32c5")))

import chip_os_awareness

# ボード固有の追加は今回なし。チップ層の API をそのまま公開する。
int_enabled = chip_os_awareness.int_enabled
int_pending = chip_os_awareness.int_pending
inh_handler = chip_os_awareness.inh_handler
primap_bit = chip_os_awareness.primap_bit
