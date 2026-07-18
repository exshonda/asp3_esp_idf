import struct, sys, datetime
EPOCH_DELTA = 0x00dcddb30f2f8000  # us between 0000-01-01 and 1970-01-01
def ts(us): return datetime.datetime.utcfromtimestamp((us-EPOCH_DELTA)/1e6)
def parse(path):
    d=open(path,'rb').read()
    assert d[:8]==b'btsnoop\x00', 'not btsnoop'
    off=16; recs=[]
    while off+24<=len(d):
        olen,ilen,flags,drops,t = struct.unpack('>IIIIq', d[off:off+24]); off+=24
        pkt=d[off:off+ilen]; off+=ilen
        recs.append((t,flags,pkt))
    return recs
def le16(b,i): return b[i]|(b[i+1]<<8)
for path in sys.argv[1:]:
    print("="*70); print(path.split('/')[-1])
    recs=parse(path); t0=None
    conn_evts=[]; phy=[]; disc=[]; smp=[]; enc=[]; dle=[]; chmap=[]
    for t,flags,pkt in recs:
        if not pkt: continue
        typ=pkt[0]; body=pkt[1:]
        if typ==0x04 and len(body)>=2:  # HCI Event
            code=body[0]
            if code==0x3E and len(body)>=3:  # LE Meta
                sub=body[2]; p=body[3:]
                if sub in (0x01,0x0A) and len(p)>=15:
                    # status,handle,role,peer_addr_type,peer_addr(6)[,local_rpa,peer_rpa],itvl,lat,sup,ca
                    o = 9 if sub==0x01 else 21
                    if len(p)>=o+7:
                        itvl=le16(p,o); lat=le16(p,o+2); sup=le16(p,o+4)
                        conn_evts.append((t,'CONN_COMPLETE',itvl,lat,sup))
                elif sub==0x03 and len(p)>=9:  # LE Conn Update Complete
                    itvl=le16(p,3); lat=le16(p,5); sup=le16(p,7)
                    conn_evts.append((t,'CONN_UPDATE',itvl,lat,sup))
                elif sub==0x0C:
                    phy.append((t,'PHY_UPDATE_COMPLETE',p.hex()[:20]))
            elif code==0x05:
                disc.append((t,'DISCONNECT', body[2:].hex()))
            elif code==0x08:
                enc.append((t,'ENC_CHANGE', body[2:].hex()))
        elif typ==0x01 and len(body)>=2:  # HCI Command
            op=le16(body,0)
            if op==0x2032: phy.append((t,'CMD_LE_SET_PHY',''))
            if op==0x2022: dle.append((t,'CMD_LE_SET_DATA_LEN',''))
            if op==0x2014: chmap.append((t,'CMD_LE_SET_HOST_CH_CLASS',''))
        elif typ==0x02 and len(body)>=8:  # ACL
            cid=le16(body,6)
            if cid==0x0006 and len(body)>=9:
                smp.append((t,'SMP', body[8], 'H->C' if not (flags&1) else 'C->H'))
    allt=[r[0] for r in recs]; base=min(allt)
    def rel(t): return (t-base)/1e6
    print(" -- conn params --")
    for t,k,i,l,s in conn_evts:
        print(f"   +{rel(t):7.3f}s {k}: itvl={i}({i*1.25:.1f}ms) lat={l} suptmo={s}({s*10}ms)")
    print(f" -- PHY 関連イベント/コマンド: {len(phy)} 件 {[p[1] for p in phy][:5]}")
    print(f" -- DLE cmd: {len(dle)} / channel-map cmd: {len(chmap)}")
    print(" -- ENC_CHANGE --")
    for t,k,h in enc: print(f"   +{rel(t):7.3f}s {k} {h}")
    print(" -- SMP PDU (opcode) --")
    for t,k,op,dirn in smp[:14]: print(f"   +{rel(t):7.3f}s SMP op=0x{op:02x} {dirn}")
    print(" -- DISCONNECT --")
    for t,k,h in disc:
        print(f"   +{rel(t):7.3f}s {k} raw={h}")
        if conn_evts:
            sup=conn_evts[-1][4]*10/1000.0
            print(f"     ⇒ 沈黙開始推定 = 切断 - suptmo({sup}s) = +{rel(t)-sup:.3f}s")
