import json, subprocess, sys, os
TMP=os.path.dirname(__file__)
OCD="$HOME/tools/espressif/tools/openocd-esp32/v0.12.0-esp32-20260424/openocd-esp32"
os.environ["OPENOCD_SCRIPTS"]=OCD+"/share/openocd/scripts"
CNT="0x4081bd2c"
pairs=json.load(open(TMP+"/diffs.json"))

def run_ocd(cmds):
    args=[OCD+"/bin/openocd","-f","board/esp32c6-builtin.cfg"]
    for c in cmds: args+=["-c",c]
    r=subprocess.run(args,capture_output=True,text=True,timeout=60,cwd="$HOME/TOPPERS/asp3_esp_idf")
    return r.stdout+r.stderr

def rd(addr):
    out=run_ocd(["init","halt","mdw "+addr+" 1","resume","exit"])
    for l in out.splitlines():
        if addr[2:].lower() in l.lower() and ':' in l:
            return int(l.split(':')[1].split()[0],16)
    return -1

def poke(idxs):
    cmds=["init","halt"]
    for i in idxs:
        a,n,_=pairs[i]
        cmds.append("mww 0x%s 0x%s"%(a,n))
    cmds+=["resume","exit"]
    run_ocd(cmds)

def measure_rate():
    # halt→read→resume→(2.5s run)→halt→read : within one session
    import time
    out=run_ocd(["init","halt","mdw "+CNT+" 1","resume","sleep 2500","halt","mdw "+CNT+" 1","resume","exit"])
    vals=[]
    for l in out.splitlines():
        if CNT[2:].lower() in l.lower() and ':' in l:
            vals.append(int(l.split(':')[1].split()[0],16))
    if len(vals)>=2:
        return (vals[1]-vals[0])/2.5, vals
    return None, vals

if __name__=="__main__":
    cmd=sys.argv[1]
    if cmd=="rate":
        r,v=measure_rate(); print("rate=%.1f/s  vals=%s"%(r if r else -1, v))
    elif cmd=="poke":
        idxs=[int(x) for x in sys.argv[2].split(",")] if sys.argv[2] else list(range(len(pairs)))
        poke(idxs); print("poked",len(idxs),"regs:",idxs)
