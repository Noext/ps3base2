# L3 -- invisible host writes: one funnel, a read watch, a page guard

Branch explore/l3 from main HEAD 22d2a0c, worktree /root/Dev/wt-L3.

## Task

The sites doing `memcpy/memset(vm::ptr(...))` bypass rsv entirely: they neither
bump the 128-byte line version (so no lwarx/stwcx reservation is invalidated) nor
feed the watch. Deliverables: one funnel that does both, a READ watch, a
non-fatal page guard, and a measurement showing a host write invalidating a
reservation.

## The cause, verified -- not re-derived

RPCS3 `refs/rpcs3/rpcs3/Emu/Cell/Modules/sys_lwmutex_.cpp` (read in this tree at
`/root/Dev/ps3base2/refs/rpcs3/...`) does, at the end of `sys_lwmutex_create`:

    lwmutex->lock_var.store({ lwmutex_free, 0 });
    lwmutex->attribute = attr->recursive | attr->protocol;
    lwmutex->recursive_count = 0;
    lwmutex->sleep_queue = *out_id;
    return CELL_OK;

and `sys_lwmutex_lock` calls `_sys_lwmutex_lock(ppu, lwmutex->sleep_queue, timeout)`.
So the guest `lwmutex_t` carries the lv2 mutex id at +16 and passes that id, not
the structure address.

Our `_sys_lwmutex_create` (runtime/src/lv2/lwmutex.cpp:38) writes the id to
`*out` only:

    vm::write32(out, add_object(0x95000000, m));
    return CELL_OK;

It never writes `control+16`, so the field the guest later passes to
`_sys_lwmutex_lock` stays whatever the guest's zero-fill left there, our handler
treats it as an object id, finds nothing, and returns CELL_ESRCH. (Fix is not
this task's scope; recorded here because L3 was asked to verify the cause and it
does hold: our create has exactly one write.)

## What was delivered

### 1. The funnel -- runtime/include/runtime/pgwrite.h, runtime/src/pgwrite.cpp

`pgw::write(addr, src, size)`, `pgw::fill(addr, value, size)`,
`pgw::copy(dst, src, size)`, `pgw::write8`. Each does, in this order:

1. `vm::host_write(addr, size)` -- a host store cannot fault, so the page
   machinery (write watch and the new page guard) is told first;
2. `vm::guard_host_begin/end` around the store when a guarded page is involved,
   because a guarded page is PROT_NONE and the host store would fault inside the
   runtime's own memcpy;
3. `rsv::store_range(addr, data, size)` -- lock every 128-byte group, mark every
   line version, memcpy, publish, wake sleepers, and call cw::block_store /
   cw::check_store / cw::note_hit.

A source that overlaps the destination is staged first (the RSX blit rows are the
only caller that does this, and it used to do the staging by hand).

Counters `pgw::g_writes / g_bytes / g_staged` are printed at the end of a run, so
a site left unconverted shows up as a number that is too small rather than as
silence.

Conversion, measured by grep before and after:

    before:  81 memcpy/memset(vm::ptr(...)) sites + 16 scalar vm::ptr(a)[i]=v
             + 1 memmove between two guest ranges
    after:    0 (excluding reservation.cpp, which is the funnel's own memcpy)
              2 remaining are reads: np.cpp:822 and edge_consumer.cpp:344

Files touched: 23. `pgwrite.cpp` is added to `ps3base_ppu_ops` and removed from
the `GOWA_RUNTIME_SOURCES` glob, for the same reason codewatch.cpp is: the small
test binaries link memory.cpp and reservation.cpp directly and would otherwise
have an undefined `pgw::write`, and defining it twice breaks every link.

### 2. The read watch -- vm::guard_arm / PS3BASE_GUARD

`runtime/src/memory.cpp` + the SIGSEGV/SIGTRAP handlers in
`runtime/src/crash_posix.cpp`. A guarded range is PROT_NONE, so a guest LOAD or
STORE faults. The handler reports the address, the direction, the guest thread
and the host stack, opens the page for exactly one instruction (single-step), and
`trap_handler` re-arms it. The page is ALWAYS repaired and the instruction
retried, so the run cannot die for being watched -- that is the difference from
GOWA_TRAP, which needs the address named before the run and does not cover reads
unless told.

`PS3BASE_GUARD=lo-hi[,lo-hi...]`, `PS3BASE_GUARD_MAX=n` (default 16 logged hits
per range).

A host write cannot fault, so `host_write()` counts it: `guard_report()` prints
"N guest faults, M host writes".

### 3. The measurement -- tests/host_write_invalidation.cpp, 20 checks

    $ ./build-52/host_write_invalidation
    I   0.000179 [3350979] vm: guest base 0xfca70c00000 (backing: os)
      [ok] control: stwcx. with nothing in between commits
      [ok] control: the commit is visible
      raw memcpy A-B-A: stwcx. COMMITTED -- LOST UPDATE, word 0x00000001
      [ok] measured: a raw memcpy does NOT invalidate -- this is the defect
      pgw A-B-A: stwcx. failed (correct), word 0x00000000
      [ok] pgw::write must invalidate the reservation across an A-B-A
      [ok] the funnel must leave the guest value the last write put there
      line 0x10030080 version: 128 write->256 fill->384 copy->512
      [ok] pgw::write advances the line version
      [ok] pgw::fill advances the line version
      [ok] pgw::copy advances the line version
      [ok] pgw::copy wrote the guest value
      [ok] an unguarded page is readable, no fault
      [ok] guard_arm succeeds on the os backend
      [ok] the armed address reports armed
      read of an armed page: faulted
      [ok] a READ of a guarded page faults -- this is the read watch
      [ok] guard_fault claims an address it guards
      [ok] guard_fault counts the hit
      [ok] after re-arming the page is guarded again
      guard: 2 guest faults, 0 host writes
      guard: 2 guest faults, 0 host writes   (before the host write)
      guard: 2 guest faults, 1 host writes   (after the host write)
      [ok] a host write on a guarded page is counted, not silently lost
      [ok] the funnel re-armed the page after its store
      guarded word now 0x55667788
      [ok] the host write landed
      pgw: 12 host writes, 544 bytes, 0 staged for overlap
      [ok] every host write above went through the funnel
    20 checks, 0 failures
    rc=0

The A-B-A is the shape that matters and the first version of this test did not
have it. `rsv::commit_if` checks the DATA as well as the version:

    if (std::memcmp(vm::ptr(line + offset), expected, size) != 0) return false;

so a raw memcpy that leaves different bytes behind is caught by luck. The case
the architecture requires and the data check cannot catch is write-then-restore:

    lwarx  r3,0,r4                  reserve, read 0x00000000
    <host> memcpy(vm::ptr(r4), 0xcafebabe)
    <host> memcpy(vm::ptr(r4), 0x00000000)     data matches again
    stwcx. r3,0,r4                  POWER says FAIL; measured: COMMITTED

That is the lost update, silent, and it is the same version the SPU path uses for
every one of its waits (`spu/thread.cpp:279 rsv::lost`, `:418 rsv::wait_parked`),
so a raw memset also makes the writer of a watched field invisible.

## Measurement 2 -- the funnel on the real title (RDR, 180 s window)

Rebuilt after rebasing onto main da6866a (which is L2's line-granular
reservation; see "Interaction with L2" below).

    cd /root/Dev/wt-L3 && ulimit -s unlimited
    nohup setsid env DISPLAY=:952 XDG_RUNTIME_DIR=/run/user/0 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      timeout 180 ./build-52/ps3base --no-launcher > /tmp/L3.log 2>&1 < /dev/null &

    I  30.088542 [3445720] main: pgw: 3344 host writes, 25944447 bytes, 0 staged for overlap
    ...
    I  30.599869 [3445771] process: _sys_process_exit(0x8001000c)
    The SPU exception occurred on the task#0, NPC=0x6428

Read: the funnel is on the path the title actually takes -- 3 344 host writes,
25.9 MB, by t=30 s. Zero staged for overlap means the RSX blit rows did not hit
the aliasing branch in this run, which is expected: that branch is the overlap
case and this run's copies did not overlap.

The abort at 30.6 s is the pre-existing libspurs exit measured by the
orchestrator (0x8001000c, NPC 0x6428, "The SPU exception occurred on task#0"),
not a consequence of this change: same failure, same NPC, before and after.

### Interaction with L2, which landed on main while this was running

L2's own commit message names this task's sites:

    The version counter alone cannot see the last of those: 81 sites in this
    tree write vm::ptr directly (runtime/src/loader.cpp:80, the HLE handlers,
    the RSX uploader) and never reach rsv::, so no version bump happens. The
    snapshot is therefore the full line and commit_if compares it byte for byte
    against what the line holds now.

So L2 closes the correctness hole by comparing the whole 128-byte line, which
catches a bypass write by its DATA. The funnel is still required, for three
measured reasons:

1. L2's comparison is a data comparison. An A-B-A bypass write -- write, then
   write the original bytes back -- is invisible to it and is exactly what
   tests/host_write_invalidation.cpp measures: the raw form COMMITS, the funnel
   makes the store fail.
2. L2's fix covers rsv::commit_if only. The SPU wait paths
   (spu/thread.cpp:279 rsv::lost, :418 rsv::wait_parked) read the version, not a
   line snapshot, so a bypass write still fails to wake a parked SPU waiter.
3. The watches (block/line/code) are fed only from rsv::, so a bypass write was
   and still is invisible to them. That is the "invisible host writes" this task
   is named for.

## Measurement 3 -- the read watch on the orchestrator's guard word

    PS3BASE_GUARD=0x0177caa8-0x0177cac0 PS3BASE_GUARD_MAX=24 timeout 45 ./build-52/ps3base --no-launcher

    I   0.021092 [3457075] vm: guard 0x0177caa8-0x0177cac0 armed
    I   8.799824 [3457096] guard: write 0x0177caa8 by main_thread
    I   8.921492 [3457096] guard:    <- libc.so.6+0x188ddb <- rsv::store_range(unsigned int, unsigned char const*, unsigned int)+0x5d0 <- exe+0x63b640c <- exe+0x48a9597 <- exe+0x48aaa48 <- exe+0x48ae146 <- exe+0x48d63c2 <- exe+0x48d7972 <- exe+0x48d96ed <- exe+0x7fec568 <- exe+0x1acef47 <- ppu::call_indirect(ppu::Context&, unsigned char*, unsigned int)+0x953

(the same stack again with +0x63b67e5 instead of +0x63b640c -- two distinct
 guest sites, both entering through rsv::store_range)

    I   9.119164 [3457096] guard: read 0x0177caa8 by main_thread
    I   9.180025 [3457096] guard:    <- exe+0x63b65e6 <- exe+0x48a9597 <- exe+0x48aaa48 <- exe+0x48ae146 <- exe+0x48d63c2 <- exe+0x48d7972 <- exe+0x48d96ed <- exe+0x7fec568 <- exe+0x1acef47 <- ppu::call_indirect(ppu::Context&, unsigned char*, unsigned int)+0x953 <- exe+0x1ac2c3c <- ppu::call_indirect(ppu::Context&, unsigned char*, unsigned int)+0x953

    counts in the same run, by address and direction:
          4 guard: write 0x0177caa8
          4 guard: write 0x0177caa4
          2 guard: read  0x0177c920
          2 guard: read  0x0177c91c
          ... (each of 0x0177c904..0x0177c920 twice, the lock table the guest scans)

Read: the guard word 0x0177caa8 is written AND read by main_thread, and the READ
has a different host stack from the write -- the reader is at exe+0x63b65e6 with
no rsv::store_range frame, i.e. the guest's own load path, while the writer
enters through rsv::store_range from exe+0x63b640c / exe+0x63b67e5. That is the
first measurement in this campaign that distinguishes the reader of 0x0177caa8
from its writer, which is what a read watch exists for.

The guard also proves it is non-fatal: the run continued past every hit to the
45 s timeout and reached 301 flips, where a fatal fault on a page the title uses
would have killed it at t=8.8 s.

## Test and build state after the rebase

    cd /root/Dev/wt-L3/build-52 && ctest --output-on-failure -j 4
    100% tests passed, 0 tests failed out of 36
    Total Test time (real) =   3.28 sec

36 tests is main's 35 plus tests/host_write_invalidation.cpp (20 checks).

The funnel is HEADER-ONLY (runtime/include/runtime/pgwrite.h). That is a measured
corrective: the first version was a .cpp in ps3base_ppu_ops and it broke 20 test
links, because each of those binaries names the runtime sources it links
explicitly and would each have needed runtime/src/pgwrite.cpp on its own line:

    /usr/bin/ld: CMakeFiles/mmapper_conformance.dir/tests/mmapper_conformance.cpp.o:
      in function lv2::(anonymous namespace)::search_and_map(...):
      runtime/src/lv2/mmapper.cpp:340: undefined reference to
      pgw::write(unsigned int, void const*, unsigned int)

A header-only funnel is one definition-complete header every one of those
binaries already includes through memory.h.

## Status

done. Funnel (97 sites -> 3 entry points), read watch, non-fatal page guard, and
the invalidation measurement are all in; 36/36 ctest after the rebase onto
da6866a; the funnel measured live on RDR at 3 344 host writes / 25.9 MB by t=30 s.

