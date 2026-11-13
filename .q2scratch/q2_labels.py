
import sys, struct, re
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf

G='/root/Dev/reg3/out-rdr'
src=open(G+'/ppu/libfiber_000.cpp',encoding='utf-8',errors='replace').read()
labels={int(m,16) for m in re.findall(r'^L_([0-9a-f]{8}):', src, re.M)}
e=Elf(G+'/gen/reloc/libfiber.elf')

# every guest address the generated source mentions as a label
print('labels: %d' % len(labels))
# the "unreached" runs from the walk -- do they carry labels?
runs=[(0x360003a4,0x360003d4),(0x36000418,0x36000514),(0x36000644,0x3600070c),
      (0x36000c6c,0x36000e0c),(0x36001d14,0x36002000),(0x36002b9c,0x360033dc),
      (0x36005c88,0x36006558)]
for a,b in runs:
    ls=[hex(x) for x in range(a,b,4) if x in labels]
    print('run 0x%08x..0x%08x: %d labels %s' % (a,b,len(ls),ls[:6]))
print()
# how many labels lie inside [0x36000000,0x36006558)?
lib=[x for x in labels if 0x36000000<=x<0x36006558]
print('labels in libfiber text: %d' % len(lib))
print('highest label 0x%08x' % max(lib))
# the highest 12 labels
print('highest 12:', [hex(x) for x in sorted(lib)[-12:]])
