import sys, struct, os
sys.path.insert(0,'/root/Dev/wt-Q2/tools')
from inventory import Elf, prx_param
for tree in ('/root/Dev/reg3/out-rdr','/root/Dev/reg3/out-mc','/root/Dev/reg3/out-gta'):
    print('==', tree)
    d = os.path.join(tree, 'gen', 'reloc')
    for fn in sorted(os.listdir(d)):
        if not fn.endswith('.elf'): continue
        e = Elf(os.path.join(d, fn))
        p = prx_param(e)
        if not p or 'libent' not in p:
            print('   %-18s no libent' % fn); continue
        addr, end = p['libent']
        n = 0; zero = 0; steps = 0
        while addr < end:
            steps += 1
            if steps > 1000: break
            b = e.read(addr, 0x1C)
            if not b or len(b) < 0x1C: break
            size = b[0]
            if size == 0:
                zero += 1
                print('   %-18s libent 0x%08x size=0  <-- INFINITE LOOP in any walker' % (fn, addr))
                break
            n += 1; addr += size
        print('   %-18s libent 0x%08x..0x%08x  %d entries  zero-size %d' % (fn, p['libent'][0], p['libent'][1], n, zero))