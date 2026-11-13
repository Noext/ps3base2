
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param

G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
p=prx_param(e)

# all function exports (named + unnamed), and all .opd-named code addresses
func_exports=[]
addr,end=p['libent']
while addr<end:
    b=e.read(addr,0x1C); size=b[0]
    nf,nv,nt=struct.unpack('>HHH',b[6:12])
    name_p,nid_p,addr_p=struct.unpack('>3I',b[0x10:0x1C])
    nm=e.cstr(name_p) if name_p else ''
    for i in range(nf):
        nid=e.u32(nid_p+i*4); opd=e.u32(addr_p+i*4)
        func_exports.append((nm,nid,opd,e.u32(opd)))
    addr+=size
named=[x for x in func_exports if x[0]]
unnamed=[x for x in func_exports if not x[0]]
print('function exports: %d total = %d named + %d unnamed' % (len(func_exports),len(named),len(unnamed)))
print('  unnamed:', ['%08X->0x%08x'%(nid,code) for _,nid,_,code in unnamed])

t=open(G+'/ppu/table.cpp',encoding='utf-8',errors='replace').read()
fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
funcs={int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
codes=[c for _,_,_,c in func_exports]
print('  of those, lifted: %d of %d' % (sum(1 for c in codes if c in funcs), len(codes)))

# .opd-named code addresses
opdcodes=set()
for tt,f,o,va,pa,fs,ms,al in e.phdrs:
    if tt!=1 or (f&1) or not fs: continue
    blob=e.d[o:o+fs]
    for i in range(0,len(blob)-7,8):
        c,tc=struct.unpack_from('>II',blob,i)
        if 0x36000000<=c<0x36006558 and tc==0x36016a80: opdcodes.add(c)
print()
print('OPD-named code addresses: %d' % len(opdcodes))
print('  of which are function exports: %d' % len(opdcodes & set(codes)))
print('  of which are NOT exports (internal, .opd-only): %d' % len(opdcodes - set(codes)))
print('  of the internal ones, lifted: %d of %d' % (len((opdcodes - set(codes)) & funcs), len(opdcodes - set(codes))))
print()
print('So "47/47 exports verified" checked %d of the %d functions this module\'s own .opd names.'
      % (len(named), len(opdcodes)))
print('The other %d are internal functions the export table does not name, and the check never looked at them.'
      % (len(opdcodes)-len(named)))
