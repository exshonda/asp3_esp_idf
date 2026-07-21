#!/usr/bin/env bash
#
#  c3_sec_state.sh — 接続中の C3 を halt して **暗号化状態と SM proc** を読む
#
#  目的（evidence-06 §5 の「次の測定」）:
#    「**ペアリングは空中で成功しているのに `ENC_CHANGE status=13`（ETIMEOUT）が出る**」
#    の判別。ETIMEOUT は SM proc の 30 秒期限で発火するので、
#    **成功直後（ETIMEOUT が出る «前»）に既に暗号化されているか**を見れば
#      encrypted=1 → **bond は実際に成功しており、30秒後の ETIMEOUT は
#                     «遅れて届く偽の失敗報告»**（SM proc が回収されず残存）
#      encrypted=0 → 暗号化が本当に成立しておらず、空中の PDU 交換とホスト状態が乖離
#    が分かれる。
#
#  ★アプリを一切変更しない（計装ゼロ）。GDB の型情報でシンボリックに読むので
#    構造体オフセットの手計算ミスが起きない
#    （`--wrap` 計装が最適化で inert になった evidence-06 §4 の反省）。
#
#  ★halt → 読み → **必ず resume**（接続を殺さない）。
#
#  ★実測で確定した型（推測しない）:
#      `ble_hs_conns`  = SLIST  → 先頭は `.slh_first`   （ble_hs_conn.c:32）
#      `ble_sm_procs`  = STAILQ → 先頭は `.stqh_first`  （ble_sm.c:73,171）
#    GDB の batch では `if/else` が使えないため、**null でも安全に評価できる式**
#    （`? :` で 0 を返す）だけで書く。
#
#  使い方:
#     C3_SERIAL=<USB-JTAG シリアル> c3_sec_state.sh <asp.elf>
#  前提:
#     判別したい状態（接続中／ETIMEOUT 前）で実行すること。
#
set -u
ELF="${1:?usage: C3_SERIAL=<serial> c3_sec_state.sh <asp.elf>}"
SERIAL="${C3_SERIAL:?C3_SERIAL に対象ボードの USB-JTAG シリアル(MAC)を指定してください}"
OOCD_DIR="${OOCD_DIR:-/home/honda/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260703/openocd-esp32}"
GDB="${GDB:-/home/honda/tools/espressif/tools/riscv32-esp-elf-gdb/16.3_20250913/riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb}"

"$OOCD_DIR/bin/openocd" \
    -s "$OOCD_DIR/share/openocd/scripts" \
    -f board/esp32c3-builtin.cfg \
    -c "adapter usb location any" \
    -c "esp usb_serial $SERIAL" \
    -c "init" -c "halt" \
    > /tmp/c3_sec_oocd.log 2>&1 &
OOCD_PID=$!
sleep 4

C="ble_hs_conns.slh_first"
P="ble_sm_procs.stqh_first"

"$GDB" -q -batch "$ELF" \
    -ex "target extended-remote :3333" \
    -ex "set confirm off" \
    -ex "set pagination off" \
    -ex "echo === 接続の暗号化状態 ===\n" \
    -ex "printf \"  g_conn_handle = 0x%04x\\n\", g_conn_handle" \
    -ex "printf \"  conn_ptr      = %p\\n\", $C" \
    -ex "printf \"  handle        = 0x%04x\\n\", $C ? $C->bhc_handle : 0xffff" \
    -ex "printf \"  ★encrypted    = %d\\n\", $C ? $C->bhc_sec_state.encrypted : -1" \
    -ex "printf \"  authenticated = %d\\n\", $C ? $C->bhc_sec_state.authenticated : -1" \
    -ex "printf \"  ★bonded       = %d\\n\", $C ? $C->bhc_sec_state.bonded : -1" \
    -ex "printf \"  key_size      = %d\\n\", $C ? $C->bhc_sec_state.key_size : -1" \
    -ex "echo === SM proc（残存していれば 30s 後に ETIMEOUT を撃つ）===\n" \
    -ex "printf \"  ★proc_ptr     = %p\\n\", $P" \
    -ex "printf \"  conn_handle   = 0x%04x\\n\", $P ? $P->conn_handle : 0xffff" \
    -ex "printf \"  state         = %d\\n\", $P ? $P->state : -1" \
    -ex "printf \"  flags         = 0x%08x\\n\", $P ? $P->flags : 0" \
    -ex "echo === アプリ側カウンタ ===\n" \
    -ex "printf \"  gap_conn=%u gap_disc=%u\\n\", g_gap_conn_count, g_gap_disc_count" \
    -ex "continue &" \
    -ex "detach" \
    2>&1 | grep -avE "^\[|Reading symbols|warning:|^$|could not connect"

kill $OOCD_PID 2>/dev/null
wait $OOCD_PID 2>/dev/null
