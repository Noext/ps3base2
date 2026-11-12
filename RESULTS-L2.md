# L2 -- reservations on the whole line

Branch explore/l2 from main HEAD 22d2a0c.

Task: lwarx/ldarx must reserve the 128-byte line, and any write into that line by
another agent (PPU store, SPU put, host write) must cancel the reservation, as the
Cell does. Today runtime/src/ppu_ops.cpp:80 reserve() sets blk = ea & ~7, copies 8
bytes, and commit_if compares a 128-byte line version -- so only writes that go
through rsv:: bump the version, while the ~80 memcpy/memset(vm::ptr) sites bypass
it entirely.

## Status: started

Proof follows, measurement by measurement, appended with the raw output.

