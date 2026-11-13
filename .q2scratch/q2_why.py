
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param

e=Elf('/root/Dev/ps3base2/out-rdr/gen/reloc/libfiber.elf')
prx=prx_param(e)

# All code addresses the OPD names (the whole .opd run in the data LOAD)
lo,hi=0x36000000,0x36006558
codes=[]
for t,f,o,va,pa,fs,ms,al in e.phdrs:
    if t!=1 or (f&1) or not fs: continue
    blob=e.d[o:o+fs]
    for i in range(0,len(blob)-7,8):
        c,toc=struct.unpack_from('>II',blob,i)
        if lo<=c<hi and toc==0x36016a80:
            codes.append((va+i,c))
print('OPD descriptors naming libfiber code: %d' % len(codes))
uniq=sorted({c for _,c in codes})
print('unique code addresses: %d' % len(uniq))
print('lowest  0x%08x' % uniq[0])
print('highest 0x%08x  <-- the "stop"' % uniq[-1])

# lifted entries
t=open('/root/Dev/ps3base2/out-rdr/ppu/table.cpp',encoding='utf-8',errors='replace').read()
fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
funcs=sorted(int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk))
lib=[f for f in funcs if lo<=f<hi]
print('lifted entries in libfiber: %d, highest 0x%08x' % (len(lib), lib[-1]))
print('OPD code addresses NOT lifted: %s' % ([hex(c) for c in uniq if c not in set(funcs)] or 'none'))
print('lifted entries NOT in the OPD: %s' % ([hex(f) for f in lib if f not in set(uniq)] or 'none'))
print()
print('--- so the lift top equals the OPD top ---')
print('  max(OPD code)   = 0x%08x' % uniq[-1])
print('  max(lifted)     = 0x%08x' % lib[-1])
print('  equal: %s' % (uniq[-1]==lib[-1]))
print()
print('--- and 0x3600e930, the address the docs compared against it ---')
for t2,f2,o2,va2,pa2,fs2,ms2,al2 in e.phdrs:
    if t2==1 and va2<=0x3600e930<va2+ms2:
        seg='CODE (executable)' if f2&1 else 'DATA (.opd)'
        print('  0x3600e930 is in LOAD 0x%08x + 0x%x -> %s' % (va2,0x3600e930-va2,seg))
print('  its descriptor: code 0x%08x toc 0x%08x' % (e.u32(0x3600e930), e.u32(0x3600e934)))
print('  code 0x%08x lifted: %s' % (e.u32(0x3600e930), e.u32(0x3600e930) in set(funcs)))
