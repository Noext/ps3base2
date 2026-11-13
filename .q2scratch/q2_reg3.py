
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf
G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
t=open(G+'/ppu/table.cpp',encoding='utf-8',errors='replace').read()
blk=t[t.index('const ImageEntry images[]'):]; blk=blk[:blk.index('};')]
m=re.search(r'\{"libfiber", "([^"]+)", true, 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk)
print('libfiber image row:', m.group(0))
lo,hi,toc=int(m.group(2),16),int(m.group(3),16),int(m.group(4),16)
fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
funcs={int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
lib=sorted(f for f in funcs if lo<=f<hi)
print('lifted libfiber entries: %d, min 0x%08x max 0x%08x' % (len(lib),lib[0],lib[-1]))
print('0x360033dc lifted:', 0x360033dc in funcs)
print('0x36005c80 lifted:', 0x36005c80 in funcs)
# OPD run
codes=[]
for tt,f,o,va,pa,fs,ms,al in e.phdrs:
    if tt!=1 or (f&1) or not fs: continue
    blob=e.d[o:o+fs]
    for i in range(0,len(blob)-7,8):
        c,tc=struct.unpack_from('>II',blob,i)
        if lo<=c<hi and tc==toc: codes.append((va+i,c))
uniq=sorted({c for _,c in codes})
print('OPD descriptors naming code: %d, unique %d, min 0x%08x max 0x%08x' % (len(codes),len(uniq),uniq[0],uniq[-1]))
print('OPD code not lifted:', [hex(c) for c in uniq if c not in funcs] or 'none')
print('lifted not in OPD:', [hex(f) for f in lib if f not in set(uniq)] or 'none')
print('0x3600e930 descriptor: code 0x%08x toc 0x%08x' % (e.u32(0x3600e930), e.u32(0x3600e934)))
print('0x3600e930 in exec LOAD:', any(tt==1 and (f&1) and va<=0x3600e930<va+fs for tt,f,o,va,pa,fs,ms,al in e.phdrs))
print('0x3600e930 in data LOAD :', any(tt==1 and not (f&1) and va<=0x3600e930<va+fs for tt,f,o,va,pa,fs,ms,al in e.phdrs))
print('0x36005c88 trampoline words:', ' '.join('%08x'%e.u32(0x36005c88+i*4) for i in range(8)))
print('0x36006560 pointer table:', ' '.join('%08x'%e.u32(0x36006560+i*4) for i in range(9)))
