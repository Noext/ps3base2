
import sys, struct
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf
e=Elf('/root/Dev/ps3base2/out-rdr/gen/reloc/libfiber.elf')
def w(a):
    b=e.read(a,4)
    return struct.unpack('>I',b)[0] if b else None
print('toc = 0x36016a80')
print('*(toc-32712) = *(0x%08x) = 0x%08x' % (0x36016a80-32712, w(0x36016a80-32712)))
p=w(0x36016a80-32712)
print('*(0x%08x) = 0x%08x' % (p, w(p)))
print()
print('--- the PERM null check in f_360033dc ---')
for a in range(0x3600342c,0x36003460,4):
    print('  0x%08x: %08x' % (a,w(a)))
print()
print('--- f_36000748 (called from the PERM path) is at opd 0x3600e928 -> code 0x36002b10 ---')
print('  lifted? ', 'f_36002b10' in open('/root/Dev/ps3base2/out-rdr/ppu/funcs.h').read())
print()
print('--- what is at 0x360066f0 (the global the entry reads) ---')
for a in range(0x360066f0, 0x36006720, 4):
    print('  0x%08x: %08x' % (a,w(a)))
print()
print('--- opd 0x3600e928 descriptor ---')
print('  code 0x%08x toc 0x%08x' % (w(0x3600e928), w(0x3600e92c)))
