
import sys, struct, re, json
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param

for g in ('rdr','mc','gta'):
    T='/root/Dev/reg3/out-%s/ppu/table.cpp'%g
    t=open(T,encoding='utf-8',errors='replace').read()
    m=re.search(r'const uint32_t export_count = (\d+);', t)
    total_rows=int(m.group(1))
    blk=t[t.index('const ImageEntry images[]'):]; blk=blk[:blk.index('};')]
    imgs=[]
    for mm in re.finditer(r'\{"([^"]+)", "([^"]+)", (true|false), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk):
        imgs.append((mm.group(1), mm.group(2), mm.group(3)=='true'))
    named=0; unnamed=0; funcs_named=0; funcs_unnamed=0
    for name,rel,prx in imgs:
        if not prx: continue
        e=Elf('/root/Dev/reg3/'+rel); p=prx_param(e)
        addr,end=p['libent']
        while addr<end:
            b=e.read(addr,0x1C); size=b[0]
            nf,nv,nt=struct.unpack('>HHH',b[6:12])
            name_p=b[0x10:0x14]
            nm=e.cstr(struct.unpack('>I',name_p)[0]) if struct.unpack('>I',name_p)[0] else ''
            if nm: funcs_named+=nf; named+=nf+nv+nt
            else: funcs_unnamed+=nf; unnamed+=nf+nv+nt
            addr+=size
    print('%-4s table export_count=%4d | PRX named exports=%4d (funcs %4d) | PRX unnamed exports=%3d (funcs %3d)'
          % (g, total_rows, named, funcs_named, unnamed, funcs_unnamed))
