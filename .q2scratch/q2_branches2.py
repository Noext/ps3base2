
import sys, struct, re, json
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports

G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
p=prx_param(e)
nids=json.load(open('/root/Dev/wt-Q2/ref/nid_db.json'))
stubs={int(f['stub_target'],16) for m in inv_imports(e,p,nids) for f in m['functions']}
src=open(G+'/ppu/libfiber_000.cpp',encoding='utf-8',errors='replace').read()
labels={int(m,16) for m in re.findall(r'^L_([0-9a-f]{8}):', src, re.M)}
lo,hi=0x36000000,0x36006558
base,blob=next(e.code())
missing=set(); total=0
for i in range(0,len(blob)-3,4):
    w=struct.unpack_from('>I',blob,i)[0]
    op=w>>26
    if op==18:
        li=w & 0x03FFFFFC
        if li & 0x02000000: li-=0x04000000
        tgt=(li if w&2 else base+i+li) & 0xFFFFFFFF
    elif op==16:
        bd=w & 0x0000FFFC
        if bd & 0x00008000: bd-=0x00010000
        tgt=(bd if w&2 else base+i+bd) & 0xFFFFFFFF
    else:
        continue
    if not (lo<=tgt<hi): continue
    total+=1
    if tgt not in labels and tgt not in stubs: missing.add((base+i,tgt))
print('direct branch targets inside libfiber text: %d' % total)
print('targets with no label and no stub (i.e. not in the lifted source): %d' % len(missing))
for a,tg in sorted(missing)[:20]: print('   at 0x%08x -> 0x%08x' % (a,tg))
print()
print('labels: %d (89 entries + %d in-function blocks)' % (len(labels), len(labels)-89))
