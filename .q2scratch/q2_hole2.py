
import struct, shutil, subprocess, sys, os
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf

SRC='/root/Dev/reg3/out-rdr/gen/reloc/libfiber.elf'
DST='/tmp/libfiber-badcode.elf'
shutil.copy(SRC,DST)
e=Elf(SRC)
# find the file offset of the .opd slot 0x3600e930
d=bytearray(open(DST,'rb').read())
entry,phoff,shoff=struct.unpack('>QQQ', d[0x18:0x30])
phes,phn,shes,shn=struct.unpack('>HHHH', d[0x36:0x3E])
off=None
for i in range(phn):
    t,f,o,va,pa,fs,ms,al=struct.unpack('>IIQQQQQQ', d[phoff+i*phes:phoff+i*phes+56])
    if t==1 and va<=0x3600e930<va+fs and not (f&1):
        off=o+(0x3600e930-va)
print('opd 0x3600e930 at file offset 0x%x, before %s' % (off, d[off:off+8].hex()))
# code word -> 0x00010001 (a DATA value: this is the classic "descriptor never lifted")
d[off:off+4]=b'\x00\x01\x00\x01'
open(DST,'wb').write(d)
print('after  %s' % d[off:off+8].hex())

# table row unchanged: the opd still matches the PRX, so the old tool's
# opd-equality check passes; only the descriptor content differs.
T='/tmp/tbl-badcode.cpp'
t=open('/root/Dev/reg3/out-rdr/ppu/table.cpp',encoding='utf-8',errors='replace').read()
t=t.replace('"out-rdr/gen/reloc/libfiber.elf"','"%s"'%DST)
open(T,'w').write(t)

OLD='/root/Dev/wt-Q2/tools/check_lle_exports.py'
NEW='/root/Dev/wt-Q2/tools/check_export_coverage.py'
o=subprocess.run([sys.executable,OLD,T,DST,'cellFiber'],capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
n=subprocess.run([sys.executable,NEW,T,'--root','/root/Dev/reg3'],capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
print()
print('='*92)
print('G: the Yield descriptor code word set to 0x00010001 (outside every image text)')
print('  OLD check_lle_exports.py exit %d' % o.returncode)
for l in (o.stdout+o.stderr).strip().splitlines()[-3:]: print('     '+l)
print('  NEW check_export_coverage.py exit %d' % n.returncode)
for l in (n.stdout+n.stderr).strip().splitlines()[-3:]: print('     '+l)
