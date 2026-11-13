
import re, shutil, subprocess, os, sys
SRC='/root/Dev/ps3base2/out-rdr/ppu/table.cpp'
t=open(SRC,encoding='utf-8',errors='replace').read()

def run(tool, table):
    p=subprocess.run([sys.executable, tool, table], capture_output=True, text=True, cwd='/root/Dev/wt-Q2')
    return p.returncode, (p.stdout+p.stderr).strip().splitlines()

cases={}

# A: remove f_360033dc (the Yield body) from functions[] -- simulates "Yield not lifted"
a=t.replace('    {0x360033dc, f_360033dc},\n','')
assert a!=t
open('/tmp/tbl-A.cpp','w').write(a)
cases['A: Yield entry removed from functions[]']='/tmp/tbl-A.cpp'

# B: point the Yield opd at 0 (descriptor with no code) -- simulates "descriptor never lifted"
b=t.replace('{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e930}',
            '{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x0}')
assert b!=t
open('/tmp/tbl-B.cpp','w').write(b)
cases['B: Yield opd replaced by 0']='/tmp/tbl-B.cpp'

# C: point the Yield opd at a code address that is inside text but not an entry
c=t.replace('{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e930}',
            '{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e938}')
assert c!=t
open('/tmp/tbl-C.cpp','w').write(c)
cases['C: Yield opd -> 0x3600e938 (code 0x360034a4, not lifted)']='/tmp/tbl-C.cpp'

# D: delete an unnamed export row (the PRX entry) -- the class check_lle_exports skips
d=t.replace('{"", 0xBC9A0086u, "", 7, 0x3600ea68},\n','')
assert d!=t
open('/tmp/tbl-D.cpp','w').write(d)
cases['D: unnamed PRX-entry export removed']='/tmp/tbl-D.cpp'

for name,path in cases.items():
    rc1,out1=run('tools/check_lle_exports.py', path)
    # check_lle_exports takes pairs; feed it the libfiber pair
    p=subprocess.run([sys.executable,'tools/check_lle_exports.py',path,
                      '/root/Dev/ps3base2/out-rdr/gen/reloc/libfiber.elf','cellFiber'],
                     capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
    old_rc=p.returncode; old=(p.stdout+p.stderr).strip().splitlines()
    p2=subprocess.run([sys.executable,'tools/check_export_coverage.py',path,'--root','/root/Dev/ps3base2'],
                      capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
    new_rc=p2.returncode; new=(p2.stdout+p2.stderr).strip().splitlines()
    print('='*100)
    print(name)
    print('  OLD check_lle_exports.py : exit %d  %s' % (old_rc, old[-1] if old else ''))
    for l in old[-6:-1]: print('      '+l)
    print('  NEW check_export_coverage.py: exit %d  %s' % (new_rc, new[-1] if new else ''))
    for l in new[-6:-1]: print('      '+l)
