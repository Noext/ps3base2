
import sys, struct
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, PT_NAMES
import capstone
e=Elf('/root/Dev/reg3/out-rdr/gen/reloc/libfiber.elf')
print('--- libfiber program headers ---')
for t,f,o,va,pa,fs,ms,al in e.phdrs:
    print('  %-12s flags=0x%x vaddr=0x%08x filesz=0x%-6x memsz=0x%-6x' % (PT_NAMES.get(t,hex(t)),f,va,fs,ms))
print()
md=capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
md.detail=False
def dis(a,n):
    blob=e.read(a,n*4)
    print('--- 0x%08x ---' % a)
    for ins in md.disasm(blob, a):
        print('  %08x  %-10s %s' % (ins.address, ins.mnemonic, ins.op_str))
dis(0x360033dc, 44)
print()
print('--- the TOC-relative globals Yield reads (r2 = 0x36016a80) ---')
for off in (-32712,-32708,-32704,-32700,-32696,-32692,-32688):
    ea=0x36016a80+off
    print('  r2%+d = 0x%08x -> 0x%08x' % (off, ea, e.u32(ea)))
