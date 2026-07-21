#!/usr/bin/env bash
#
#  c3_pool_watch.sh — msys mbuf プールの残数を JTAG で読む（★halt 後に必ず resume する）
#
#  P1-A（サイクル毎リークの確定）用。post-mortem 版と違い «実行を止めない»。
#  ★接続«中»には撃たないこと：halt 中は controller が回らないため、長引くと
#    supervision timeout(phone=5000ms) を自分で誘発してしまう（＝人工の死）。
#    測定は «切断中» に行う。
#
#  使い方: ./c3_pool_watch.sh <asp.elf> [ラベル]
set -u
ELF="${1:?usage: c3_pool_watch.sh <asp.elf> [label]}"
LABEL="${2:-}"
OOCD_DIR=$HOME/.espressif/tools/openocd-esp32/v0.12.0-esp32-20250422/openocd-esp32
SERIAL="<MAC-19>"

sym() { riscv32-esp-elf-nm "$ELF" | awk -v n="$1" '$3==n{print "0x"$1}' | head -1; }
MP1=$(sym os_msys_init_1_mempool)
MP2=$(sym os_msys_init_2_mempool)
CONN=$(sym g_conn_handle)
NSENT=$(sym g_notify_sent)     # +0 sent, 隣接: fail は別番地なので個別に
NFAIL=$(sym g_notify_fail)
GCONN=$(sym g_gap_conn_count)
GDISC=$(sym g_gap_disc_count)

# P1-3b watchdog のカウンタ（WD=ON ビルドのときだけ存在）
WDP=$(sym g_wd_probe_count); WDRC=$(sym g_wd_probe_last_rc)
WDT=$(sym g_wd_term_count);  WDTRC=$(sym g_wd_term_last_rc); WDRSSI=$(sym g_wd_last_rssi); WDRST=$(sym g_wd_reset_count)
WDARGS=()
if [ -n "$WDP" ]; then
  WDARGS=( -c "mdw $WDP 1" -c "mdw $WDRC 1" -c "mdw $WDT 1" -c "mdw $WDTRC 1" -c "mdw $WDRSSI 1" -c "mdw $WDRST 1" )
fi

OUT=$("$OOCD_DIR/bin/openocd" -s "$OOCD_DIR/share/openocd/scripts" \
  -c "adapter serial $SERIAL" -f board/esp32c3-builtin.cfg \
  -c init -c halt \
  -c "mdw $MP1 3" -c "mdw $MP2 3" -c "mdw $CONN 1" \
  -c "mdw $NSENT 1" -c "mdw $NFAIL 1" -c "mdw $GCONN 1" -c "mdw $GDISC 1" \
  "${WDARGS[@]}" \
  -c resume -c shutdown 2>&1)

python3 - "$LABEL" <<PY
import re,sys
raw = """$OUT"""
lab = sys.argv[1] if len(sys.argv)>1 else ""
w=[]
for line in raw.splitlines():
    m=re.match(r'^0x[0-9a-fA-F]+:\s+(.*)$', line.strip())
    if m: w += [int(x,16) for x in m.group(1).split()]
# mp1: w0=block_size w1=(num_free<<16)|num_blocks w2=min_free
if len(w) >= 11:
    nb1, nf1 = w[1] & 0xffff, (w[1] >> 16) & 0xffff
    mf1 = w[2] & 0xffff
    nb2, nf2 = w[4] & 0xffff, (w[4] >> 16) & 0xffff
    conn = w[6] & 0xffff
    line = (f"[{lab}] msys_1 free={nf1}/{nb1} (min={mf1})   msys_2 free={nf2}/{nb2}   "
            f"conn_handle={conn:#06x}  notify_sent={w[7]} fail={w[8]}  gap_conn={w[9]} gap_disc={w[10]}")
    if len(w) >= 16:
        def s32(v): return v - (1 << 32) if v >> 31 else v
        line += (f"\n        WD: probe={w[11]} last_rc={s32(w[12])} term={w[13]} "
                 f"term_rc={s32(w[14])} rssi={s32(w[15])} reset={w[16] if len(w)>16 else 0}")
    print(line)
else:
    print(f"[{lab}] 読み出し失敗 (words={len(w)})")
PY
