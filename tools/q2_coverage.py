
import sys, re, struct, json, collections
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param

def table(path):
    t=open(path,encoding='utf-8',errors='replace').read()
    imgs=[]
    blk=t[t.index('const ImageEntry images[]'):]
    blk=blk[:blk.index('};')]
    for m in re.finditer(r'\{"([^"]+)", "([^"]+)", (true|false), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk):
        imgs.append(dict(name=m.group(1), file=m.group(2), prx=m.group(3)=='true',
                         lo=int(m.group(4),16), hi=int(m.group(5),16),
                         toc=int(m.group(6),16), entry=int(m.group(7),16)))
    fblk=t[t.index('const FuncEntry functions[]'):]
    fblk=fblk[:fblk.index('};')]
    funcs={int(a,16) for a in re.findall(r'\{0x([0-9a-fA-F]+), f_', fblk)}
    eblk=t[t.index('const ExportEntry exports[]'):]
    eblk=eblk[:eblk.index('};')]
    exps=[]
    for m in re.finditer(r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', eblk):
        exps.append(dict(module=m.group(1), nid=int(m.group(2),16), name=m.group(3), image=int(m.group(4)), opd=int(m.group(5),16)))
    return imgs, funcs, exps

for game in ('rdr','mc','gta'):
    T='/root/Dev/ps3base2/out-%s/ppu/table.cpp'%game
    imgs, funcs, exps = table(T)
    print('='*110)
    print('%s: %d images, %d lifted functions, %d export rows' % (game, len(imgs), len(funcs), len(exps)))
    print('%-14s %5s %5s %5s %6s %6s %6s' % ('image','exp','lift','miss','opd','opdL','opdM'))
    for idx,im in enumerate(imgs):
        if not im['prx']: continue
        path='/root/Dev/ps3base2/'+im['file']
        try:
            e=Elf(path); prx=prx_param(e)
        except Exception as ex:
            print('%-14s  cannot read %s: %s' % (im['name'], path, ex)); continue
        rows=[x for x in exps if x['image']==idx]
        lift=0; miss=[]
        for x in rows:
            code=e.u32(x['opd']) if x['opd'] else 0
            if im['lo']<=code<im['hi'] and code in funcs: lift+=1
            else: miss.append((x,code))
        opd_lo=opd_hi=None
        for sh in e.shdrs:
            addr,size=sh[3],sh[5]
            if addr<=e.entry<addr+size and size%8==0 and size:
                opd_lo,opd_hi=addr,addr+size; break
        opdc=0; opdl=0; opdm=[]
        if opd_lo:
            for a in range(opd_lo,opd_hi,8):
                code=e.u32(a)
                if not (im['lo']<=code<im['hi']): continue
                opdc+=1
                if code in funcs: opdl+=1
                else: opdm.append((a,code))
        print('%-14s %5d %5d %5d %6d %6d %6d' % (im['name'], len(rows), lift, len(miss), opdc, opdl, len(opdm)))
        for x,code in miss[:8]:
            print('    EXPORT MISS %-8X %-42s opd 0x%08x code 0x%08x' % (x['nid'], x['name'] or '(unnamed)', x['opd'], code))
        for a,code in opdm[:10]:
            print('    OPD MISS opd 0x%08x -> code 0x%08x' % (a,code))
