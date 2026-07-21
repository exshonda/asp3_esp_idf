#!/usr/bin/env bash
#
#  c3_postmortem.sh — C3 の «沈黙死» 後に JTAG で halt して状態を丸ごと吸い出す
#
#  P1-1（計画 docs/ble-c3-smp-death-plan.md）の計器。
#  ★出荷ビルドに計装を «足さず»、JTAG 読み出しのみで完結する（layout 摂動ゼロ）。
#     必要なカウンタ（shim_*_pend_total 等）は出荷ビルドに既に存在する。
#
#  使い方:  ./c3_postmortem.sh <asp.elf> [出力先.txt]
#  前提  :  C3 = USB-JTAG adapter serial 60:55:F9:57:BA:BC（ベンチ台帳）
#
#  ★halt したままにする（resume しない）＝死んだ瞬間の状態を保持して追加読み出しできる。
#    再開したい場合は openocd を再度起動して `resume` を打つ。
set -u
ELF="${1:?usage: c3_postmortem.sh <asp.elf> [out.txt]}"
OUT="${2:-c3_postmortem_$(date +%H%M%S).txt}"
OOCD_DIR=/home/honda/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250422/openocd-esp32
OOCD="$OOCD_DIR/bin/openocd"
SCRIPTS="$OOCD_DIR/share/openocd/scripts"
SERIAL="60:55:F9:57:BA:BC"

# シンボル位置は ELF から引く（ビルドが変わっても追従する）
sym() { riscv32-esp-elf-nm "$ELF" | awk -v n="$2" '$3==n{print "0x"$1}' | head -1; }
TCB=$(sym x _kernel_tcb_table)
RUNTSK=$(sym x _kernel_p_runtsk)
SCHEDTSK=$(sym x _kernel_p_schedtsk)
DSPFLG=$(sym x _kernel_dspflg)
CRIT=$(sym x esp_shim_crit_nest)
CONN=$(sym x g_conn_handle)
QUSED=$(sym x shim_que_pend_used)
SEMPEND=$(sym x shim_sem_pend_total)
: "${TCB:?_kernel_tcb_table が ELF に無い}"

{
  echo "### c3_postmortem $(date -Is)  ELF=$ELF"
  echo "### syms: tcb=$TCB runtsk=$RUNTSK schedtsk=$SCHEDTSK dspflg=$DSPFLG crit=$CRIT conn=$CONN"
} > "$OUT"

"$OOCD" -s "$SCRIPTS" \
  -c "adapter serial $SERIAL" \
  -f board/esp32c3-builtin.cfg \
  -c "init" -c "halt" \
  -c "echo {=== REGS ===}" \
  -c "reg pc" -c "reg mcause" -c "reg mepc" -c "reg mstatus" -c "reg mtvec" -c "reg sp" -c "reg ra" \
  -c "echo {=== SCHED (dspflg, ?, schedtsk, runtsk) ===}" \
  -c "mdw $DSPFLG 4" \
  -c "echo {=== TCB_TABLE (13 tasks x 32B) ===}" \
  -c "mdw $TCB 104" \
  -c "echo {=== SHIM COUNTERS (que_pend_used .. crit_nest) ===}" \
  -c "mdw $QUSED 13" \
  -c "echo {=== g_conn_handle ===}" \
  -c "mdw $CONN 1" \
  -c "echo {=== HALT のまま終了（状態保持）===}" \
  -c "shutdown" >> "$OUT" 2>&1

echo "wrote: $OUT"
echo "--- decode ---"
python3 "$(dirname "$0")/c3_postmortem_decode.py" "$OUT" "$ELF"
