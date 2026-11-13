
import sys, struct, json, re, glob
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports

nids=json.load(open('/root/Dev/wt-Q2/ref/nid_db.json'))
T='/root/Dev/ps3base2/out-rdr/ppu/table.cpp'
t=open(T,encoding='utf-8',errors='replace').read()
eblk=t[t.index('const ExportEntry exports[]'):]; eblk=eblk[:eblk.index('};')]
exps=set()
for m in re.finditer(r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', eblk):
    exps.add((m.group(1), int(m.group(2),16)))

hle=set()
for p in glob.glob('/root/Dev/wt-Q2/runtime/src/hle/*.cpp'):
    s=open(p,encoding='utf-8',errors='replace').read()
    for m in re.finditer(r'HLE_FUNC\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)', s):
        hle.add((m.group(1), m.group(2)))

for mod in ('libfiber','libfs','libnet','libsre','libgcm_sys','librtc','libsysmodule','liblv2'):
    path='/root/Dev/ps3base2/out-rdr/gen/reloc/%s.elf'%mod
    e=Elf(path); prx=prx_param(e)
    rows=[]
    for m in inv_imports(e, prx, nids):
        for f in m['functions']:
            rows.append((m['module'], int(f['nid'],16), f['name']))
    lle=sum(1 for (mm,nid,nm) in rows if (mm,nid) in exps)
    hh=sum(1 for (mm,nid,nm) in rows if (mm,nm) and (mm,nm) in hle)
    none=[(mm,nid,nm) for (mm,nid,nm) in rows if (mm,nid) not in exps and not ((mm,nm) and (mm,nm) in hle)]
    print('%-14s imports %3d  LLE %3d  HLE %3d  NONE %3d' % (mod, len(rows), lle, hh, len(none)))
    for mm,nid,nm in none:
        print('      NONE %-14s %08X %s' % (mm, nid, nm))
