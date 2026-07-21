#!/usr/bin/env python3
"""c3_postmortem_decode.py — c3_postmortem.sh の生ダンプを人間可読へ

TCB(32B): +0 task_queue(8) +8 p_tinib +12 tstat/bpri/pri(3) +15 flags
          +16 p_winfo +20 p_lastmtx +24 sp +28 pc
tstat: 0x00 DORMANT / 0x01 RUNNABLE / 0x02 SUSPENDED / 待ちは (n<<2)
"""
import re, sys, subprocess

TSTAT = {0x00: "DORMANT", 0x01: "RUNNABLE", 0x02: "SUSPENDED"}
WAIT = {0x01: "SLP(起床待ち)", 0x02: "DLY(時間経過待ち)", 0x08: "RDTQ(dtq受信待ち)",
        0x09: "RPDQ", 0x10: "SEM(セマフォ待ち)", 0x11: "FLG", 0x12: "SDTQ(dtq送信待ち)",
        0x13: "SPDQ", 0x14: "MTX(mutex待ち)", 0x15: "MPF"}
TASKS = ["SHIM_TSK1","SHIM_TSK2","SHIM_TSK3","SHIM_TSK4","SHIM_TSK5","SHIM_TSK6",
         "SHIM_TSK7","SHIM_TSK8","SHIM_TIMER_TSK","BT_TIMER_TSK","LOGTASK","MAIN_TASK","PROBE_TASK"]
CNAMES = ["que_pend_used","que_pend_total","?","?","que_debt_conflict","sem_pend_total",
          "?","?","?","sem_take_ectx_total","sem_ectx_total","?","crit_nest"]

def decode_tstat(t):
    if t in TSTAT: return TSTAT[t]
    if t & 0x7c:
        w = (t >> 2) & 0x1f
        s = WAIT.get(w, f"WAIT?({w:#x})")
        return s + (" +SUSPENDED" if t & 0x02 else "")
    return f"?{t:#04x}"

def words(text, tag):
    """openocd の mdw 出力（'addr: w0 w1 ...'）から tag 以降の語を拾う"""
    out=[]; on=False
    for line in text.splitlines():
        if tag in line: on=True; continue
        if on and re.match(r'^=== ', line.strip()): break
        m=re.match(r'^0x[0-9a-fA-F]+:\s+(.*)$', line.strip())
        if on and m: out += [int(x,16) for x in m.group(1).split()]
    return out

def sym_of(elf, addr):
    if not addr or addr in (0xffffffff,): return ""
    try:
        r = subprocess.run(["riscv32-esp-elf-addr2line","-f","-e",elf,hex(addr)],
                           capture_output=True, text=True, timeout=10)
        f = r.stdout.strip().splitlines()
        return f[0] if f else ""
    except Exception:
        return ""

def main():
    raw = open(sys.argv[1]).read(); elf = sys.argv[2] if len(sys.argv)>2 else None
    print("=== レジスタ ===")
    for r in ("pc","mcause","mepc","mstatus","sp","ra"):
        m=re.search(rf'^{r} \(/\d+\): (0x[0-9a-fA-F]+)', raw, re.M)
        if m:
            v=int(m.group(1),16); extra=""
            if r=="mcause":
                extra = f"  [{'割込み' if v>>31 else '例外'} cause={v&0x7fffffff}]"
            if r=="mstatus":
                extra = f"  [MIE={'1' if v&0x8 else '0'} MPIE={'1' if v&0x80 else '0'}]"
            if r in ("pc","mepc","ra") and elf:
                extra = "  " + sym_of(elf, v)
            print(f"  {r:8s}= {m.group(1)}{extra}")
    sched = words(raw, "=== SCHED")
    if len(sched)>=4:
        print(f"\n=== スケジューラ ===\n  dspflg={sched[0]}  p_schedtsk={sched[2]:#010x}  p_runtsk={sched[3]:#010x}")
        if sched[3]==0: print("    ★ p_runtsk=0 ＝ どのタスクも走っていない（idle/割込み中）")
    tcbs = words(raw, "=== TCB_TABLE")
    if tcbs:
        print("\n=== タスク状態 ===")
        for i in range(min(13, len(tcbs)//8)):
            b=tcbs[i*8:(i+1)*8]
            tstat=b[3]&0xff; pri=(b[3]>>16)&0xff
            winfo=b[4]; sp=b[6]; pc=b[7]
            name=TASKS[i] if i<len(TASKS) else f"TCB{i}"
            st=decode_tstat(tstat)
            mark = " ★" if ("待ち" in st or "WAIT" in st) else ""
            loc = f"  pc={pc:#010x} {sym_of(elf,pc) if elf else ''}" if pc else ""
            print(f"  [{i+1:2d}] {name:15s} tstat={tstat:#04x} {st:22s} pri={pri} winfo={winfo:#010x}{loc}{mark}")
    cnt = words(raw, "=== SHIM COUNTERS")
    if cnt:
        print("\n=== shim カウンタ ===")
        for i,v in enumerate(cnt[:13]):
            n = CNAMES[i] if i<len(CNAMES) else "?"
            if n=="?": continue
            star = " ★★" if (n=="crit_nest" and v!=0) else ""
            print(f"  {n:22s}= {v}{star}")
    conn = words(raw, "=== g_conn_handle")
    if conn:
        v=conn[0]&0xffff
        print(f"\n=== 接続 ===\n  g_conn_handle={v:#06x}" + ("  ★ 接続保持中（wedge の可能性）" if v!=0xffff else "  (未接続)"))

main()
