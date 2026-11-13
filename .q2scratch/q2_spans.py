
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf

G='/root/Dev/reg3/out-rdr'
t=open(G+'/ppu/table.cpp',encoding='utf-8',errors='replace').read()
blk=t[t.index('const ImageEntry images[]'):]; blk=blk[:blk.index('};')]
imgs=[]
for m in re.finditer(r'\{"([^"]+)", "([^"]+)", (true|false), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+)\}', blk):
    imgs.append(dict(name=m.group(1), file=m.group(2), prx=m.group(3)=='true',
                     lo=int(m.group(4),16), hi=int(m.group(5),16)))
sblk=t[t.index('const FunctionSpan function_spans[]'):]; sblk=sblk[:sblk.index('};')]
spans=[]
for m in re.finditer(r'\{0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), 0x([0-9a-fA-F]+), (\d+)\}', sblk):
    spans.append((int(m.group(1),16), int(m.group(2),16), int(m.group(3),16), int(m.group(4))))
print('spans in table: %d' % len(spans))

for idx,im in enumerate(imgs):
    if not im['prx']: continue
    mine=[s for s in spans if s[3]==idx]
    covered=sum(s[1]-s[0]+4 for s in mine)
    total=im['hi']-im['lo']
    print('%-13s spans %4d  covered 0x%06x of 0x%06x text bytes = %5.1f%%' %
          (im['name'], len(mine), covered, total, 100.0*covered/total))

print()
print('--- libfiber: the bytes NOT inside any lifted span ---')
im=[i for i in imgs if i['name']=='libfiber'][0]
idx=imgs.index(im)
mine=sorted((s[0],s[1]) for s in spans if s[3]==idx)
holes=[]
cur=im['lo']
for lo,hi in mine:
    if lo>cur: holes.append((cur,lo))
    cur=max(cur,hi+4)
if cur<im['hi']: holes.append((cur,im['hi']))
for lo,hi in holes:
    print('  hole 0x%08x..0x%08x (%d bytes)' % (lo,hi,hi-lo))
