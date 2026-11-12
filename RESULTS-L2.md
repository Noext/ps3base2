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


## Measurement 6 -- the sharpest case: dcbz, which is a plain memset on the line

runtime/include/ppu/ops.h:285 does `std::memset(b + (ea & ~127u), 0, 128)` and
calls notify_store() for a diagnostic, never rsv::. So it bumps no version. Only
the byte comparison of the full line can catch it, and it does:

    $ ./build-51/reservation_line
      lwarx  [0x10000090] = 0x11111111, then a bypass store to 0x100000a0 (same line)
      stwcx. -> failed (correct)
      stwcx. after a write to a different line -> success (correct)
      lwarx at 0x10000090 then stwcx. at 0x100000a0 (same line) -> success (correct)
      ldarx then a bypass store to the same line -> stdcx. failed (correct)
      lwarx then vm::write32 to the same line -> stwcx. failed (correct)
      clean lwarx/stwcx.: ok=1 counter=0x00000011
      lwarx then dcbz of the same line -> stwcx. failed (correct)
    7 checks, 0 failures

## Measurement 7 -- the whole suite, on the rebased branch

    $ cd /root/Dev/wt-L2/build-51 && ctest --output-on-failure -j 4
    100% tests passed, 0 tests failed out of 35
    Total Test time (real) =   3.61 sec

The new `reservation_line` is one of the 35. The four reservation tests:

    reservation_line       7 checks, 0 failures
    reservation_increment  8 checks, 0 failures
    reservation_xproc      4 checks, 0 failures
    lock_counter           3 checks, 0 failures


## Measurement 8 -- what the 541 -> 1921 actually means: the run survives longer, the frame rate does not change

Read honestly, the flip numbers are a duration difference, not a speed-up:

    baseline  541 flips / 21.88 s = 24.7 flips/s, aborted at 23.5 s
    fix      1921 flips / 85.98 s = 22.3 flips/s, aborted at 87.0 s

The frame rate is unchanged (slightly lower, 10%, well inside run-to-run noise on
a machine at load 20 -- the same binary gave 601 flips in 23.8 s on the Vulkan
presenter). What changed by 3.7x is how long the title runs before libsre aborts
SPURS: 23.5 s -> 87.0 s. The reservation fix does not make rendering faster; it
stops the guest losing its own atomic updates, so the taskset that was dying at
23 s keeps going to 87 s.

That is the correct claim and the one this task supports: the SPURS abort is a
separate defect (same `_sys_process_exit(0x8001000c)`, same liblv2+0xc7e8), and
the honest statement is that it arrives later. The strict criterion in
docs/regress-baseline.md (screen + flips >= 2000 + no abort before 60 s) is
still not met by this change alone: measured under tools/regress.sh,

    $ DISPLAY_NUM=:951 SECONDS_PER_GAME=140 tools/regress.sh rdr
    rdr          FAIL  SPURS abort     flips=601    crashes=0   screen=socialclub@13s abort=at 25s

-- the Social Club notice IS read back from the captured frame (screen_hit at
13 s, /tmp/regress-rdr-shots/hit.png), and the abort is at 25 s in that
invocation because regress.sh runs the Vulkan presenter (window present), which
is the other path. This change is not sufficient for RDR PASS on its own.



## Summary

The cause, verified rather than re-derived: rsv::reserve() in runtime/src/ppu_ops.cpp:80
took `blk = ea & ~7` and copied 8 bytes, so a reservation was one doubleword, and
`store_conditional()` refused any stwcx. whose target was not in that same
doubleword (line 120). Measured with tests/reservation_line.cpp against main
HEAD 22d2a0c: 3 of 6 checks fail -- a bypass store to another block of the
reserved line does NOT cancel the reservation, stwcx. to another word of the
reserved line fails when it should succeed, and ldarx behaves the same way.

The fix: the reservation is the 128-byte line, the snapshot is the full line,
lwarx/ldarx read out of it at `ea & 127`, store_conditional() accepts a target
anywhere inside the reserved line, and commit_if is called with the full line as
both expected and data. commit_if already compared the expected bytes against the
line's current contents, so with a full-line expected value it now sees writes
that never bumped the version -- the 81 direct vm::ptr writes (loader, HLE,
RSX uploader, and dcbz's memset). That is RPCS3's own test:
`old_data != data || rtime != (res & -128)`.

Measured effect, headless path held fixed on both sides:

    baseline (main)   541 flips, abort 23.5 s, worst frame 335.8 ms, 6 frames >50 ms
    with the fix     1921 flips, abort 87.0 s, worst frame  38.7 ms, 0 frames >50 ms
                     crash events 0 -> 0

The flip gain is duration, not frame rate (24.7 -> 22.3 flips/s, unchanged within
noise): what the fix buys is that the title runs 3.7x longer before libsre aborts
SPURS. The SPURS abort is a separate defect and is still there. Under
tools/regress.sh (Vulkan presenter) RDR reads the Social Club notice off the
captured frame at 13 s but still aborts at 25 s, so this change alone does not
meet the RDR PASS bar and the results say so.

Tests: tests/reservation_line.cpp, 7 checks, all pass on the fix and 3 fail on
main. Whole suite 35/35 ctest pass on the rebased branch. Diff touches 5 source
files and deletes nothing.


