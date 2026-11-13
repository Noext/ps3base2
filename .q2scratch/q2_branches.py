
import sys, struct, re, json
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports

G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
p=prx_param(e)
nids=json.load(open('/root/Dev/wt-Q2/ref/nid_db.json'))
stubs={int(f['stub_target'],16) for m in inv_imports(e,p,nids) for f in m['functions']}

t=open(G+'/ppu/table.cpp',encoding='utf-8',errors='replace').read()
fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
funcs={int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
lo,hi=0x36000000,0x36006558

# every direct branch target inside libfiber's text
base,blob=next(e.code())
missing=set(); total=0
for i in range(0,len(blob)-3,4):
    w=struct.unpack_from('>I',blob,i)[0]
    op=w>>26
    if op==18:  # b/bl/ba/bla
        li=w & 0x03FFFFFC
        if li & 0x02000000: li-=0x04000000
        tgt=(li if w&2 else base+i+li) & 0xFFFFFFFF
        if not (lo<=tgt<hi): continue
        total+=1
        if tgt not in funcs and tgt not in stubs: missing.add((base+i,tgt,w&1))
    elif op==16:  # bc
        bd=w & 0x0000FFFC
        if bd & 0x00008000: bd-=0x00010000
        tgt=(bd if w&2 else base+i+bd) & 0xFFFFFFFF
        if not (lo<=tgt<hi): continue
        total+=1
        if tgt not in funcs and tgt not in stubs: missing.add((base+i,tgt,w&1))
print('direct branch targets inside libfiber text: %d' % total)
print('targets that are neither a lifted entry nor an import stub: %d' % len(missing))
for a,tg,lk in sorted(missing)[:20]:
    print('   at 0x%08x -> 0x%08x (%s)' % (a,tg,'bl' if lk else 'b'))
print()
# bytes covered by the lifted functions: walk each generated body's labels
src=open(G+'/ppu/libfiber_000.cpp',encoding='utf-8',errors='replace').read()
labels=sorted({int(m,16) for m in re.findall(r'^L_([0-9a-f]{8}):', src, re.M)})
print('labels in the generated libfiber source: %d' % len(labels))
print('lowest 0x%08x highest 0x%08x' % (labels[0],labels[-1]))
print('labels that are lifted entries: %d' % len(set(labels)&funcs))
print('labels that are NOT entries (in-function blocks): %d' % len(set(labels)-funcs))
