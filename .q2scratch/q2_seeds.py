import sys, struct
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param
e = Elf('/root/Dev/reg3/out-rdr/gen/reloc/libfiber.elf')
print('section headers present: %d' % len(e.shdrs))
print('  -> analysis.Image takes the %s branch' % ('shdrs' if e.shdrs else 'else (PRX)'))
# reproduce analysis.Image's PRX opd scan exactly
prx = prx_param(e)
toc = int(prx['toc'], 16)
seg = next(p for p in e.phdrs if p[0]==1 and p[1]&1)
text_lo, text_hi = seg[3], seg[3]+seg[5]
print('text_lo 0x%08x text_hi 0x%08x  toc 0x%08x' % (text_lo, text_hi, toc))
opd = []
for p in e.phdrs:
    if p[0] != 1 or p[1] & 1 or not p[5]: continue
    blob = e.d[p[2]:p[2]+p[5]]
    for k in range(0, len(blob)-7, 4):
        code, t = struct.unpack_from('>II', blob, k)
        if t == toc and text_lo <= code < text_hi:
            opd.append(code)
print('seeds the lifter gets from the PRX .opd scan: %d' % len(opd))
print('  unique: %d' % len(set(opd)))
print('  highest seed: 0x%08x' % max(opd))
print()
print('And that highest seed is exactly the number the finding called the stop:')
print('  max(opd) = 0x%08x' % max(opd))