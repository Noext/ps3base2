
import sys, struct, re, json
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports
import capstone

G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
p=prx_param(e)
nids=json.load(open('/root/Dev/wt-Q2/ref/nid_db.json'))
stubs={int(f['stub_target'],16) for m in inv_imports(e,p,nids) for f in m['functions']}
t=open(G+'/ppu/table.cpp',encoding='utf-8',errors='replace').read()
fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
funcs=sorted(int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk))
lo,hi=0x36000000,0x36006558
lib=[f for f in funcs if lo<=f<hi]

md=capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
md.detail=False
words={}
def w(a):
    if a in words: return words[a]
    b=e.read(a,4)
    words[a]=struct.unpack('>I',b)[0] if b else None
    return words[a]

# Reachability from every lifted entry, following direct branches, exactly the
# walk the lifter does. A word is "covered" when the walk decoded it.
covered=set()
work=list(lib)
seen=set()
while work:
    pc=work.pop()
    while True:
        if pc in seen or not (lo<=pc<hi) or pc in stubs: break
        if w(pc) is None: break
        seen.add(pc)
        ins=list(md.disasm(e.read(pc,4), pc))
        if not ins: break
        ins=ins[0]
        covered.add(pc)
        m=ins.mnemonic
        if m in ('b','ba'):
            try: work.append(int(ins.op_str,16))
            except ValueError: pass
            break
        if m in ('bl','bla'):
            try: work.append(int(ins.op_str,16))
            except ValueError: pass
            pc+=4; continue
        if m.startswith('bc') or m in ('bcl','bcla','bclr','bcctr','bcctrl','bctrl','blrl','blr'):
            if m in ('bclr','blr','bcctr','bctrl','bcctrl','blrl'):
                break
            try: work.append(int(ins.op_str.split(',')[-1].strip(),16))
            except ValueError: pass
            pc+=4; continue
        pc+=4

total=(hi-lo)//4
print('libfiber text: %d instructions (0x%x bytes)' % (total, hi-lo))
print('reachable from the %d lifted entries: %d instructions (%.1f%%)' % (len(lib), len(covered), 100.0*len(covered)/total))
unreached=sorted(a for a in range(lo,hi,4) if a not in covered)
# group into runs
runs=[]
for a in unreached:
    if runs and a==runs[-1][1]: runs[-1][1]=a+4
    else: runs.append([a,a+4])
print('unreached runs: %d' % len(runs))
for a,b in runs:
    kind='import stubs' if all(x in stubs for x in range(a,b,4)) else ('zero fill' if all(w(x)==0 for x in range(a,b,4)) else 'OTHER')
    print('   0x%08x..0x%08x  %3d words  %s' % (a,b,(b-a)//4,kind))
