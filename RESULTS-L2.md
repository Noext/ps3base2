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


## Measurement 1 -- today's behaviour, measured: the reservation is 8 bytes, not the line

Baseline binary at main HEAD 22d2a0c (build-51), RDR run, 120 s:

    $ nohup setsid env DISPLAY=:951 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info timeout 120 /root/Dev/wt-L2/build-51/ps3base --no-launcher > /tmp/L2-base.log 2>&1 < /dev/null

    $ grep -a 'rsx: flip' /tmp/L2-base.log | tail -1
    I  21.875769 [3210514] rsx: flip 541 (buffer 0) worst 335.8 ms, >50ms 6, >100ms 3
    $ grep -a 'aborted' /tmp/L2-base.log | tail -2
    I  23.493121 [3210480] tty: [0] The SPURS is aborted.
    I  23.496001 [3210480] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 (no lifted code)

## Measurement 2 -- tests/reservation_line.cpp fails 3 of 6 on that binary

The test is written to the shape the failure has: lwarx, then a store by another
agent into the SAME 128-byte line at a different 8-byte block, then stwcx. The
store is a plain memcpy into vm::ptr -- the shape of the 81 write sites that never
reach rsv:: (runtime/src/loader.cpp:80, runtime/src/hle/*.cpp, ...).

    $ ./build-51/reservation_line; echo EXIT=$?
      lwarx  [0x10000090] = 0x11111111, then a bypass store to 0x100000a0 (same line)
      stwcx. -> SUCCESS (reservation survived a write to its own line)
    [FAIL] a store to another 8-byte block of the reserved line must cancel the reservation
      stwcx. after a write to a different line -> success (correct)
      lwarx at 0x10000090 then stwcx. at 0x100000a0 (same line) -> FAILED
    [FAIL] the reservation covers the whole 128-byte line, so stwcx. to another word in it must succeed
      ldarx then a bypass store to the same line -> stdcx. SUCCESS (wrong)
    [FAIL] a store to another block of the line reserved by ldarx must cancel the reservation
      lwarx then vm::write32 to the same line -> stwcx. failed (correct)
      clean lwarx/stwcx.: ok=1 counter=0x00000011
    6 checks, 3 failures
    EXIT=1

Three separate defects, all from the same 8-byte granule:

1. reserve() copies 8 bytes (runtime/src/ppu_ops.cpp:80-95), so a write to another
   block of the line leaves the snapshot identical and commit_if's memcmp passes.
2. store_conditional() requires `(ctx.raddr & ~7) == (ea & ~7)` (line 120), so a
   stwcx. to another word of the reserved line fails even when nothing wrote.
3. The version bump is only in rsv::, so the 81 direct vm::ptr writes are invisible
   to the version -- which is why the snapshot has to be the whole line and the
   comparison has to be against the line's actual bytes, not only the version.


