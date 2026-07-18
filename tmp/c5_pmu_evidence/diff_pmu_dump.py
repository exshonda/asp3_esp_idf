import re,sys
def parse(p):
    d={}
    for ln in open(p,'rb').read().decode('ascii','replace').splitlines():
        m=re.match(r'(PMU|MDMSYS)\+([0-9a-f]{3}): ([0-9a-f ]+)',ln.strip())
        if m:
            base={'PMU':0x600B0000,'MDMSYS':0x600A9C00}[m.group(1)]
            off=int(m.group(2),16)
            for i,w in enumerate(m.group(3).split()):
                d[base+off+4*i]=w
        m=re.match(r'PHYDIG: 044c=([0-9a-f]+) 0450=([0-9a-f]+) 047c=([0-9a-f]+)',ln.strip())
        if m:
            d[0x600A044C],d[0x600A0450],d[0x600A047C]=m.groups()
    return d
a,b=parse(sys.argv[1]),parse(sys.argv[2])
names={0x600B0000:'HP_ACTIVE_DIG_POWER',0x600B0004:'HP_ACTIVE_ICG_HP_FUNC',0x600B0008:'HP_ACTIVE_ICG_HP_APB',
0x600B000C:'HP_ACTIVE_ICG_MODEM',0x600B0010:'HP_ACTIVE_HP_SYS_CNTL',0x600B0014:'HP_ACTIVE_HP_CK_POWER  <== RF/analog',
0x600B0018:'HP_ACTIVE_BIAS',0x600B001C:'HP_ACTIVE_BACKUP',0x600B0020:'HP_ACTIVE_BACKUP_CLK',0x600B0024:'HP_ACTIVE_SYSCLK',
0x600B0028:'HP_ACTIVE_HP_REGULATOR0',0x600B002C:'HP_ACTIVE_HP_REGULATOR1',0x600B0030:'HP_ACTIVE_XTAL',
0x600B0034:'HP_MODEM_DIG_POWER',0x600B0038:'HP_MODEM_ICG_HP_FUNC',0x600B003C:'HP_MODEM_ICG_HP_APB',
0x600B0040:'HP_MODEM_ICG_MODEM',0x600B0044:'HP_MODEM_HP_SYS_CNTL',0x600B0048:'HP_MODEM_HP_CK_POWER',
0x600B004C:'HP_MODEM_BIAS',0x600B0050:'HP_MODEM_BACKUP',0x600B0054:'HP_MODEM_BACKUP_CLK',0x600B0058:'HP_MODEM_SYSCLK',
0x600B005C:'HP_MODEM_HP_REGULATOR0',0x600B0068:'HP_SLEEP_DIG_POWER',0x600B006C:'HP_SLEEP_ICG_HP_FUNC',
0x600B0070:'HP_SLEEP_ICG_HP_APB',0x600B0074:'HP_SLEEP_ICG_MODEM',0x600B0078:'HP_SLEEP_HP_SYS_CNTL',
0x600B007C:'HP_SLEEP_HP_CK_POWER',0x600B0080:'HP_SLEEP_BIAS',0x600B0084:'HP_SLEEP_BACKUP',0x600B0088:'HP_SLEEP_BACKUP_CLK',
0x600B008C:'HP_SLEEP_SYSCLK',0x600B0090:'HP_SLEEP_HP_REGULATOR0',0x600B0098:'HP_SLEEP_XTAL',
0x600A044C:'PHYDIG_044C',0x600A0450:'PHYDIG_0450',0x600A047C:'PHYDIG_047C'}
n=0
print(f"{'addr':<12}{'register':<32}{'OFF(before)':<14}{'ON(after)'}")
for k in sorted(a):
    if k in b and a[k]!=b[k]:
        n+=1
        nm=names.get(k, 'LP/other bank +0x%x'%(k-0x600B0000) if k>=0x600B0000 else '?')
        print(f"0x{k:08x}  {nm:<32}{a[k]:<14}{b[k]}")
print(f"\n changed words = {n} / {len(a)} compared")
same=[k for k in a if k in b and a[k]==b[k]]
print(f" unchanged     = {len(same)}")
