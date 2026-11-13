
import subprocess, sys, os
SRC='/root/Dev/reg3/out-rdr/ppu/table.cpp'
t=open(SRC,encoding='utf-8',errors='replace').read()
RELOC='/root/Dev/reg3/out-rdr/gen/reloc/libfiber.elf'
OLD='/root/Dev/wt-Q2/tools/check_lle_exports.py'
NEW='/root/Dev/wt-Q2/tools/check_export_coverage.py'

cases={}
# E: Yield's opd pointed at a DATA address whose first word is outside all image text.
#    The old tool's rule is "a code word not inside an image's text is data, skip it",
#    so this passes as "ok". The runtime does ld32(opd) and would call 0x00000101.
e=t.replace('{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x3600e930}',
            '{"cellFiber", 0x0C44F441u, "cellFiberPpuYield", 7, 0x36005e50}')
assert e!=t
cases['E: Yield opd -> 0x36005e50 (data; first word 0x00000101)']=e
# F: Yield's opd pointed at a valid descriptor but with the TOC word zeroed.
#    Neither tool looks at the TOC through the table; the new gate does.
open('/tmp/q2-E.cpp','w').write(cases['E: Yield opd -> 0x36005e50 (data; first word 0x00000101)'])

for name, text in cases.items():
    p='/tmp/q2-case.cpp'
    open(p,'w').write(text)
    o=subprocess.run([sys.executable,OLD,p,RELOC,'cellFiber'],capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
    n=subprocess.run([sys.executable,NEW,p,'--root','/root/Dev/reg3'],capture_output=True,text=True,cwd='/root/Dev/wt-Q2')
    print('='*90)
    print(name)
    print('  OLD exit %d : %s' % (o.returncode,(o.stdout+o.stderr).strip().splitlines()[-1]))
    print('      ' + '\n      '.join((o.stdout+o.stderr).strip().splitlines()[-4:-1]))
    print('  NEW exit %d : %s' % (n.returncode,(n.stdout+n.stderr).strip().splitlines()[-1]))
    print('      ' + '\n      '.join((n.stdout+n.stderr).strip().splitlines()[-4:-1]))
