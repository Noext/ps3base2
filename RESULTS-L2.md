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


## Measurement 3 -- the fix: the reservation is the line, and the comparison is the line's bytes

runtime/src/ppu_ops.cpp: reserve() now snapshots the whole 128-byte line at
`ea & ~127`, lwarx/ldarx read their value out of that snapshot at `ea & 127`,
store_conditional() accepts a target anywhere inside the reserved line (refusing
only an unaligned reservation, a different line, or a store that would cross the
line), and commits with `commit_if(line, rtime, rdata, data, 0, 128)` -- expected
and data both the full line.

commit_if already had the property that matters: it re-reads the version AND
compares `expected` byte for byte against what the line holds now
(`memcmp(vm::ptr(line + offset), expected, size)`). With expected = the full line
that comparison is now a full-line comparison, so a write that never bumped the
version -- the 81 direct vm::ptr writers -- is visible. That is the same test
RPCS3's ppu_store_reservation makes (`old_data != data || rtime != (res & -128)`).

    $ ./build-51/reservation_line; echo EXIT=$?
      lwarx  [0x10000090] = 0x11111111, then a bypass store to 0x100000a0 (same line)
      stwcx. -> failed (correct)
      stwcx. after a write to a different line -> success (correct)
      lwarx at 0x10000090 then stwcx. at 0x100000a0 (same line) -> success (correct)
      ldarx then a bypass store to the same line -> stdcx. failed (correct)
      lwarx then vm::write32 to the same line -> stwcx. failed (correct)
      clean lwarx/stwcx.: ok=1 counter=0x00000011
    6 checks, 0 failures
    EXIT=0

## Measurement 4 -- the existing reservation tests still pass

    $ for t in reservation_line reservation_increment reservation_xproc lock_counter guest_spinlock; do printf '%-22s ' $t; ./build-51/$t 2>/dev/null | tail -1; done
    reservation_line       6 checks, 0 failures
    reservation_increment  8 checks, 0 failures
    reservation_xproc      4 checks, 0 failures
    lock_counter           3 checks, 0 failures
    guest_spinlock         3 checks, 0 failures

reservation_increment is the one that would catch a fix that lost updates: 4
threads x 1e6 lock-free lwarx/stwcx increments, successes and counter both
4,000,000.


## Measurement 5 -- RDR against the fix, matched headless baseline

Two runs, same command, same headless configuration (both logs say
`gpu: SDL window creation failed: No available video device`, i.e. the
buffer-present path, not the Vulkan presenter -- the two are NOT comparable
across that boundary and the runs below are compared only within it):

baseline, main HEAD 22d2a0c, before the fix:

    I  21.875769 [3210514] rsx: flip 541 (buffer 0) worst 335.8 ms, >50ms 6, >100ms 3
    I  23.493121 [3210480] tty: [0] The SPURS is aborted.
    I  23.496001 [3210480] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 (no lifted code)

fix, same tree + the line reservation:

    I  85.975564 [3287010] rsx: flip 1921 (buffer 0) worst 38.7 ms, >50ms 0, >100ms 0
    I  87.025167 [3286990] tty: [0] The SPURS is aborted.
    I  87.025841 [3286990] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 (no lifted code)

    flips 541 -> 1921 (3.55x)
    abort 23.5 s -> 87.0 s (3.70x)
    worst frame 335.8 ms -> 38.7 ms; >50 ms frames 6 -> 0; >100 ms frames 3 -> 0
    crash events 0 -> 0

The SPURS abort itself is not this fix's problem (the same
`_sys_process_exit(0x8001000c)` / liblv2+0xc7e8 stop, and the same SPURS
handler path); it simply arrives three and a half times later, and the flips
are three and a half times higher, because the guest's own spin loops now stop
losing their reservations to writes it could not see.

### A note on the presenter path, because it changes the number

The same binary presented through Vulkan instead of the headless buffer gives

    I  23.793906 [3308404] rsx: flip 601 (buffer 0) worst 299.9 ms, >50ms 8, >100ms 4
    I  25.421933 [3308394] tty: [0] The SPURS is aborted.

so the two paths are not interchangeable and a comparison must hold it fixed.
Both baseline and fix numbers above are from the headless path.


