
import sys, struct
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf
e=Elf('/root/Dev/ps3base2/out-rdr/gen/reloc/libfiber.elf')
print('--- import trampoline at 0x36005c88 ---')
for a in range(0x36005c88,0x36005ca8,4):
    print('  0x%08x: %08x' % (a,e.u32(a)))
print()
print('--- the pointer table the trampolines read: 0x36006560 ---')
for i in range(9):
    a=0x36006560+i*4
    print('  0x%08x: %08x' % (a,e.u32(a)))
print()
print('--- what each pointer points at (opd -> code) ---')
for i in range(9):
    a=0x36006560+i*4
    p=e.u32(a)
    print('  slot %d -> 0x%08x : code 0x%08x toc 0x%08x' % (i,p,e.u32(p),e.u32(p+4)))
print()
print('--- last 16 bytes of the executable segment (text_hi 0x36006558) ---')
for a in range(0x36006540,0x36006558,4):
    print('  0x%08x: %08x' % (a,e.u32(a)))
