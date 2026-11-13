
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param, imports as inv_imports
import json

G='/root/Dev/reg3/out-rdr'
e=Elf(G+'/gen/reloc/libfiber.elf')
p=prx_param(e)
nids=json.load(open('/root/Dev/wt-Q2/ref/nid_db.json'))
stubs=sorted({int(f['stub_target'],16) for m in inv_imports(e,p,nids) for f in m['functions']})
src=open(G+'/ppu/libfiber_000.cpp',encoding='utf-8',errors='replace').read()
labels=sorted({int(m,16) for m in re.findall(r'^L_([0-9a-f]{8}):', src, re.M)})
lo,hi=0x36000000,0x36006558
lib=[x for x in labels if lo<=x<hi]
print('libfiber text 0x%08x..0x%08x (%d bytes)' % (lo,hi,hi-lo))
print('labels in that range: %d, lowest 0x%08x, highest 0x%08x' % (len(lib), lib[0], lib[-1]))
print('first import stub: 0x%08x' % stubs[0])
print('last import stub : 0x%08x' % stubs[-1])
print()
# bytes from lo to the first stub are code; the labels tile it
gap = stubs[0] - lib[-1]
print('highest label to first stub: 0x%08x..0x%08x = %d bytes' % (lib[-1], stubs[0], gap))
print('  that gap is: %s' % ' '.join('%08x'%e.u32(a) for a in range(lib[-1], stubs[0], 4)))
print()
print('--- so the lifted code region is 0x%08x..0x%08x, and the only unlabelled' % (lo, stubs[0]))
print('    bytes above it are the nine import trampolines and the data that follows. ---')
print('stub region: 0x%08x..0x%08x = %d bytes = 9 trampolines x 32 bytes' % (stubs[0], stubs[0]+9*32, 9*32))
