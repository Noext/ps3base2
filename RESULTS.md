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

## Status

running. Next: the same measurement in the full RDR run, and the fleet's tests
after the rebase.
