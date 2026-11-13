
import sys, struct
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf
e=Elf('/root/Dev/reg3/out-rdr/gen/reloc/libfiber.elf')
toc=0x36016a80
for off in (-32656, -32712):
    ea=toc+off
    print('r2%+d = 0x%08x -> 0x%08x' % (off, ea, e.u32(ea)))
print()
print('--- _cellFiberPpuInitialize (0x36003d18) disassembly ---')
import capstone
md=capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_64|capstone.CS_MODE_BIG_ENDIAN)
for ins in md.disasm(e.read(0x36003d18, 6*4), 0x36003d18):
    print('  %08x  %-10s %s' % (ins.address, ins.mnemonic, ins.op_str))
print()
print('--- what the exported _cellFiberPpuInitialize opd says ---')
print('  opd 0x3600e958 -> code 0x%08x' % e.u32(0x3600e958))
print()
print('--- Yield PERM path disassembly 0x360033f0..0x36003420 ---')
for ins in md.disasm(e.read(0x360033f0, 12*4), 0x360033f0):
    print('  %08x  %-10s %s' % (ins.address, ins.mnemonic, ins.op_str))
