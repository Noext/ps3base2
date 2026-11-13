
import sys, struct, re, json
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports
import capstone
from capstone import ppc as ppc_const

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
md.detail=True

def target(ins):
    for op in ins.operands:
        if op.type == ppc_const.PPC_OP_IMM:
            return op.imm & 0xFFFFFFFF
    return None

covered=set(); work=list(lib); seen=set()
while work:
    pc=work.pop()
    while True:
        if pc in seen or not (lo<=pc<hi) or pc in stubs: break
        b=e.read(pc,4)
        if not b: break
        ins=list(md.disasm(b, pc))
        if not ins: break
        ins=ins[0]; seen.add(pc); covered.add(pc)
        m=ins.mnemonic
        if m in ('b','ba','bl','bla'):
            tg=target(ins)
            if tg is not None and lo<=tg<hi: work.append(tg)
            if m in ('b','ba'): break
            pc+=4; continue
        if m in ('bclr','blr','bcctr','bctrl','bcctrl','blrl'):
            break
        if m.startswith('bc') or m.startswith('b') and 'l' in m and ins.operands and ins.operands[-1].type==ppc_const.PPC_OP_IMM:
            tg=target(ins)
            if tg is not None and lo<=tg<hi: work.append(tg)
            if m.startswith('bcl') or m.startswith('bclr'): break
            pc+=4; continue
        pc+=4

total=(hi-lo)//4
print('libfiber text: %d words (0x%x bytes), %d lifted entries' % (total, hi-lo, len(lib)))
print('decoded by walking from those entries: %d words (%.1f%%)' % (len(covered), 100.0*len(covered)/total))
unreached=sorted(a for a in range(lo,hi,4) if a not in covered)
runs=[]
for a in unreached:
    if runs and a==runs[-1][1]: runs[-1][1]=a+4
    else: runs.append([a,a+4])
print('unreached runs: %d' % len(runs))
for a,b in runs:
    ws=[struct.unpack('>I',e.read(x,4))[0] for x in range(a,b,4)]
    if all(x in stubs for x in range(a,b,4)): kind='import stub'
    elif all(x==0 for x in ws): kind='zero fill'
    elif all(x==0x60000000 for x in ws): kind='nop padding'
    else: kind='OTHER'
    print('   0x%08x..0x%08x %4d words  %s' % (a,b,(b-a)//4,kind))
