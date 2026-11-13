
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf

def table(path):
    t=open(path,encoding='utf-8',errors='replace').read()
    imgs=[]
    blk=t[t.index('const ImageEntry images[]'):]; blk=blk[:blk.index('};')]
    for m in re.finditer(r'\{"([^"]+)", "([^"]+)", (true|false), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk):
        imgs.append(dict(name=m.group(1), file=m.group(2), prx=m.group(3)=='true',
                         lo=int(m.group(4),16), hi=int(m.group(5),16), toc=int(m.group(6),16)))
    fblk=t[t.index('const FuncEntry functions[]'):]; fblk=fblk[:fblk.index('};')]
    funcs={int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
    eblk=t[t.index('const ExportEntry exports[]'):]; eblk=eblk[:eblk.index('};')]
    exps=[]
    for m in re.finditer(r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', eblk):
        exps.append(dict(module=m.group(1), nid=int(m.group(2),16), name=m.group(3), image=int(m.group(4)), opd=int(m.group(5),16)))
    return imgs, funcs, exps

def find_opd(e, im):
    best=None
    for t,f,o,va,pa,fs,ms,al in e.phdrs:
        if t!=1 or (f & 1) or not fs: continue
        blob=e.d[o:o+fs]
        run=[]; start=None; i=0
        while i+8<=len(blob):
            code,toc=struct.unpack_from('>II',blob,i)
            if im['lo']<=code<im['hi'] and toc==im['toc']:
                if start is None: start=i
                run.append((va+i,code)); i+=8
            else:
                if start is not None and len(run)>=4:
                    if best is None or len(run)>len(best[1]): best=(va+start,run)
                run=[]; start=None; i+=4
        if start is not None and len(run)>=4:
            if best is None or len(run)>len(best[1]): best=(va+start,run)
    return best

HDR='%-13s %5s %5s %5s %5s %5s' % ('image','expN','expU','opdAll','opdLift','opdGap')
for game in ('rdr','mc','gta'):
    T='/root/Dev/ps3base2/out-%s/ppu/table.cpp'%game
    imgs, funcs, exps = table(T)
    print('='*104)
    print('%s  (%d export rows, %d lifted funcs)' % (game, len(exps), len(funcs)))
    print(HDR)
    tot=[0,0,0,0,0]
    for idx,im in enumerate(imgs):
        if not im['prx']: continue
        path='/root/Dev/ps3base2/'+im['file']
        e=Elf(path)
        rows=[x for x in exps if x['image']==idx]
        named=[x for x in rows if x['module']]; unnamed=[x for x in rows if not x['module']]
        o=find_opd(e,im)
        opdAll=len(o[1]) if o else 0
        opdLift=sum(1 for a,c in o[1] if c in funcs) if o else 0
        gap=opdAll-opdLift
        print('%-13s %5d %5d %5d %5d %5d' % (im['name'], len(named), len(unnamed), opdAll, opdLift, gap))
        if o and gap:
            for a,c in o[1]:
                if c not in funcs: print('      GAP opd 0x%08x -> code 0x%08x' % (a,c))
        tot=[tot[0]+len(named), tot[1]+len(unnamed), tot[2]+opdAll, tot[3]+opdLift, tot[4]+gap]
    print('%-13s %5d %5d %5d %5d %5d' % ('TOTAL', *tot))
