# Chip (ESP32-C5 RISC-V) awareness helpers for gdb OS-awareness (ASP3).
#
# esp32c6版（asp3_core/arch/riscv_gcc/esp32c6/chip_os_awareness.py）から
# のコピー・C5対応。ESP32-C6はCPU割込み線制御が独自"PLIC"命名のメモリ
# マップトレジスタ（ENABLE/EIP配列）だったが，ESP32-C5は標準RISC-V CLIC
# のCLIC_INT_CTRL_REG(i)（1ワードにIP/IE/ATTR/CTLを格納）を使うため
# 読み出し方を全面的に書き換えた。
#
# 役割: チップ（SoC）依存の知識。ESP32-C5の割込みはINTMTX
#       （ソースルーティング）＋CLIC（CPU側制御）で管理される。「指定
#       INTNO の割込み許可/禁止・ペンディング状態」をCLIC_INT_CTRL_REG
#       から読んで返す。
#
# ■ INTNO の対応
#   ASP3 の INTNO（1〜31）は，CLIC内部番号（外部割込みは16〜47．
#   RV_EXTERNAL_INT_OFFSET=16）へ +16 した番号に対応する
#   （clic_kernel_impl.hのCLIC_LINEマクロと同じ変換．本ファイルでも
#   同じ変換を独自に行う＝gdbスクリプトはC側ヘッダをincludeできない
#   ため，値を直接埋め込む）。
#
# ■ レジスタ（clic_kernel_impl.h と一致させること）
#   CLIC_INT_CTRL_REG(i) = 0x20801000 + i*4（ESP32C5_CLIC_CTRL_BASE）
#   バイト0(bit0)=IP（pending）／バイト1(bit0)=IE（enable）
#   （hal: soc/esp32c5/include/soc/clic_reg.hのBYTE_CLIC_INT_IP_REG(i)=
#   BASE+4i+0・BYTE_CLIC_INT_IE_REG(i)=BASE+4i+1と同じレイアウト）。
#
# core_os_awareness.py は arch/riscv_gcc/common にあるため，本ファイルからの
# 相対パスで sys.path に追加してから import する。

import os
import sys

# 下位層(core)の import で .pyc を生成させない（ソースツリーに __pycache__ を残さない）。
sys.dont_write_bytecode = True

sys.path.insert(0, os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "../common")))

import core_os_awareness

import gdb

_CLIC_CTRL_BASE = 0x20801000
_CLIC_EXT_INT_OFFSET = 16


def _clic_line(intno):
    """ASP3のINTNO(1〜31)をCLIC内部番号(17〜47)へ変換する。"""
    return int(intno) + _CLIC_EXT_INT_OFFSET


def _read_reg8(addr):
    """8ビットMMIOレジスタの読出し。"""
    return int(gdb.parse_and_eval("*(unsigned char *)0x%08x" % addr)) & 0xFF


def int_enabled(intno):
    """指定 INTNO の割込み許可状態（True=許可 / False=禁止）。

    CLIC_INT_CTRL_REG(line)のIEバイト（オフセット+1）のbit0を見る。
    """
    line = _clic_line(intno)
    ie_addr = _CLIC_CTRL_BASE + line * 4 + 1
    return bool(_read_reg8(ie_addr) & 0x1)


def int_pending(intno):
    """指定 INTNO のペンディング状態（True=ペンディング）。

    CLIC_INT_CTRL_REG(line)のIPバイト（オフセット+0）のbit0を見る。
    """
    line = _clic_line(intno)
    ip_addr = _CLIC_CTRL_BASE + line * 4 + 0
    return bool(_read_reg8(ip_addr) & 0x1)


# 割込みハンドラ番地の取得（_kernel_inh_table[INTNO]．INTNOで直接添字付け）と
# レディキュービットマップのビット方向は riscv 共通部の知識なので core 層の
# 実装をそのまま公開する。
inh_handler = core_os_awareness.inh_handler
primap_bit = core_os_awareness.primap_bit
