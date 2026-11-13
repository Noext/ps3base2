
import re, collections
for game in ('rdr','mc','gta'):
    T='/root/Dev/ps3base2/out-%s/ppu/table.cpp'%game
    t=open(T,encoding='utf-8',errors='replace').read()
    i=t.index('const ExportEntry exports[]')
    eblk=t[i:]
    j=eblk.index('};')
    eblk=eblk[:j]
    rows=re.findall(r'\{"([^"]+)", 0x([0-9a-fA-F]+)u, "([^"]*)", (\d+), 0x([0-9a-fA-F]+)\}', eblk)
    c=collections.Counter((m,int(n,16)) for m,n,_,_,_ in rows)
    dups={k:v for k,v in c.items() if v>1}
    print(game, len(rows),'rows', len(c),'keys', len(dups),'dup keys')
    for k,v in list(dups.items())[:8]:
        print('   ', k, v, [(r[3],r[4]) for r in rows if (r[0],int(r[1],16))==k])
