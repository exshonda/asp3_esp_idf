# -*- coding: utf-8 -*-
#
#		パス2の生成スクリプトのチップ依存部（ESP32-C5用）
#
#  pico2_riscv/rp2350版からの流用。INTNO_VALIDをESP32-C5のCPU割込み線
#  番号（1〜31）に変更。
#
#  （esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/）からのコピー・C5対応．
#  本ファイルはチップ非依存の内容のため識別子置換のみで移植。

#
#  使用できる割込み番号とそれに対応する割込みハンドラ番号
#  （ASP3の割込み番号INTNO = ESP32-C5のCPU割込み線番号（1〜31）そのまま．
#  +1ずらしなし）
#
INTNO_VALID = list(range(1, 32))
INHNO_VALID = INTNO_VALID

#
#  生成スクリプトのコア依存部
#
IncludeTrb("core_kernel.py")
