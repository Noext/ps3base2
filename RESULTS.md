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

Confirmed on the checked-out ELF (note: it is a BIG-ENDIAN ELF64, so the
little-endian read of e_phoff yields the nonsense 0x4000000000000000 --
that is why the first parse failed).

$ readelf -lW bin/rdr/EBOOT.elf

Elf file type is EXEC (Executable file)
Entry point 0x1332080
There are 8 program headers, starting at offset 64

Program Headers:
  Type           Offset   VirtAddr           PhysAddr           FileSiz  MemSiz   Flg Align
  LOAD           0x000000 0x0000000000010000 0x0000000000010000 0x1316860 0x1316860 R E 0x10000
  LOAD           0x1320000 0x0000000001330000 0x0000000001330000 0x1bc014 0x48ff98 RW  0x10000
  LOAD           0x14dc014 0x0000000000000000 0x0000000000000000 0x000000 0x000000 R   0x10000
  LOAD           0x14dc014 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x10000
  LOAD           0x14dc014 0x0000000000000000 0x0000000000000000 0x000000 0x000000 RW  0x10000
  TLS            0x1387654 0x0000000001397654 0x0000000001397654 0x000054 0x000444 R   0x10
  LOOS+0x1       0x1316800 0x0000000001326800 0x0000000001326800 0x000020 0x000020     0x8
  LOOS+0x2       0x1316820 0x0000000001326820 0x0000000001326820 0x000040 0x000040     0x4

$ python3 (parse >=Q program headers)
GUARD 0x0177caa8 -> PT_LOAD[1] seg_offset=0x44caa8 filesz=0x1bc014 memsz=0x48ff98 memsz_end=0x17bff98 infile=False
BEYOND FILESZ: file holds NOTHING for this address; loader zero-fills.
Segment file range 0x1320000..0x14dc014, requested 0x176caa8..0x176cab8
  0x0177caa0 LOAD[1] seg+0x44caa0 infile=False bytes=00000000(BSS)
  0x0177caa4 LOAD[1] seg+0x44caa4 infile=False bytes=00000000(BSS)
  0x0177caa8 LOAD[1] seg+0x44caa8 infile=False bytes=00000000(BSS)
  0x0177caac LOAD[1] seg+0x44caac infile=False bytes=00000000(BSS)
  0x0177cab0 LOAD[1] seg+0x44cab0 infile=False bytes=00000000(BSS)
  0x0177cc60 LOAD[1] seg+0x44cc60 infile=False bytes=00000000(BSS)

CONCLUSION M1: the initial value of 0x0177caa8 is 0 by loader definition. The
file cannot supply 1. So a 1 observed at 2.308 s is either a guest/HLE write
after start or a watcher defect. M2 decides which.


## Measurement 2 -- one run, both instruments, coverage proved, and a watcher defect found and fixed

### What was instrumented

Two instruments on the same run, on the same word.

1. **A READ+WRITE mprotect trap** (runtime/src/crash_posix.cpp), armed from
   main.cpp:364 "if (!watch_delay) crash::arm_watch();" which is before
   guest::start(main_thread) on line 365. It logs prev -> new, the guest lr, the
   guest thread and the host stack. The log proves the ordering:

       I   0.049839 [3293832] trap: trap 0: read/write 0x0177caa8+0x4 arm-time words: 00000000
       I   0.050165 [3293933] ppu: thread 0x1000000 'main_thread' start entry opd 0x30013bd8

   The arm-time word is 0: the loader had already zero-filled LOAD[1]'s BSS
   (loader.cpp:80-82, memset(g_base+vaddr+filesz, 0, memsz-filesz)) and the watch,
   armed after the loaders and before the first guest instruction, read 0. That is
   an independent second measurement of the initial value.

2. **A software block watch** (PS3BASE_BLOCKWATCH), a call inside every write
   funnel rather than a page trap, so it reports a store at the instruction that
   performs it.

### Coverage: measured, not argued

Every guest write funnel calls cw::block_store before the store retires:

    runtime/src/reservation.cpp:226  "rsv::commit_if putllc" / "rsv::commit_if stcx"
    runtime/src/reservation.cpp:248  "rsv::store_unconditional"   <- putlluc
    runtime/src/reservation.cpp:261  "rsv::store_locked"          <- SPURS locked store
    runtime/src/reservation.cpp:272  "rsv::store_range"           <- every lifted st*, vm::write*, MFC put
    runtime/src/reservation.cpp:321  "rsv::merge_bits"            <- SPU PUT bit merge
    runtime/src/ppu_ops.cpp:152      "ppu::dcbz"                  <- dcbz (ops.h:285)

That table is an argument, so it is not what this result relies on.
tests/write_paths.cpp drives a store through each of the seven paths with the
watch armed, then reads gowa.log back and requires every path's label:

    $ ./build-40/write_paths
    write_paths: 8 checks passed (0 failed)
    exit=0

    WRITE +0xaa0 (0x0177caa0) w4   prev 0x00000000 -> 0x00000001 by rsv::store_range
    WRITE +0xae4 (0x0177cae4) w4   prev 0x00000000 -> 0x00000000 by rsv::commit_if stcx
    WRITE +0xb20 (0x0177cb20) w128 prev 0x00000000 -> 0x00000000 by rsv::commit_if putllc
    WRITE +0xba0 (0x0177cba0) w128 prev 0x00000000 -> 0x11000000 by rsv::store_unconditional
    WRITE +0xc20 (0x0177cc20) w4   prev 0x00000000 -> 0x44332211 by rsv::store_locked
    WRITE +0xca0 (0x0177cca0) w4   prev 0xdeadbeef -> 0xdeadbeef by rsv::merge_bits
    WRITE +0xd00 (0x0177cd00) w128 prev 0x00000000 -> 0x00000000 by ppu::dcbz

All seven labels are also in the shipped binary: rsv::store_range 1,
rsv::commit_if putllc 1, rsv::commit_if stcx 1, rsv::store_unconditional 1,
rsv::store_locked 1, rsv::merge_bits 1, ppu::dcbz 1.

The uncovered path is a direct memcpy(vm::ptr(...)) in an HLE handler or the
loader (15 such call sites). The loader's is the ELF load itself, before either
watch; the rest write guest buffers handed to a syscall, and no HLE handler in
this title is handed a pointer into this structure. The cross-check that nothing
slipped is that in run E the block watch reported the same six value-relevant
writes the trap watch did on the same word, in the same order, from the same
guest lr -- two instruments with different mechanisms agreeing event for event.

### A watcher defect this run found, and its fix

The first version of the trap watch reported, as guest reads, its own reads. The
block watch computes prev with vm::read32, and when the watched word sits on a
trap page that read faults inside the SIGSEGV handler and is published as a guest
READ carrying the guest lr of whatever store was interrupted. In the unfixed run B,
6 of 575 events on 0x0177caa8 were this -- every one at a moment that made the
store it interrupted look preceded by a read, i.e. it fabricated exactly the event
the investigation was looking for.

The fix: codewatch.h adds "thread_local int t_self_read_depth" and a SelfReadScope
RAII guard, entered around both prev reads; trap_fault records
"self_read = !is_write && cw::t_self_read_depth > 0" and process_hit counts those
in g_self_reads instead of reporting them. The count is printed, so "0 self reads"
is a number rather than an absence.

Run E, the run this result is based on, prints it:

    trap: watch totals: 1 guest events, 1 self reads dropped ...
    trap: watch totals: 201 guest events, 15 self reads dropped ...
    trap: watch totals: 1801 guest events, 66 self reads dropped ...

and the independent check over the published events finds 0 of 1956 whose host
stack is cw::blockwatch_hit:

    guest events: 1956 ; self among them: 0

### Run E -- the decisive run

    DISPLAY=:941 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
    PS3BASE_LOG=info GOWA_TRAP=r0177caa8 GOWA_TRAP_MAX=50000 \
    PS3BASE_BLOCKWATCH=0x0177caa0-0x0177cab0 \
    timeout 100 ./build-40/ps3base --no-launcher

    1956 guest trap events, 0 self reads among them
    576 events on 0x0177caa8 (564 reads, 12 writes), 2 threads
    0 CHAIN GAP in the whole run
    0 crashes; exit is _sys_process_exit(0x8001000c) at 35.7 s

The first 16 events on the word, in log order:

    2.293473 write prev=0 -> 0 lr=0x00609a40 main_thread
    2.312218 write prev=0 -> 0 lr=0x00609a40 main_thread
    2.366340 read  prev=0 -> 0 lr=0x00609a40 main_thread
    2.393590 write prev=0 -> 1 lr=0x00609a40 main_thread     <== the 1 appears
    2.407247 write prev=1 -> 1 lr=0x00609a40 main_thread
    2.497101 write prev=1 -> 0 lr=0x0091fd98 main_thread
    2.510946 write prev=0 -> 0 lr=0x0091fd98 main_thread
    2.559227 read  prev=0 -> 0 lr=0x0091fd98 main_thread
    2.587907 write prev=0 -> 1 lr=0x0091fd98 main_thread
    2.598780 write prev=1 -> 1 lr=0x0091fd98 main_thread
    2.682091 write prev=1 -> 0 lr=0x0091fdb0 main_thread
    2.692400 write prev=0 -> 0 lr=0x0091fdb0 main_thread
    2.742894 read  prev=0 -> 0 lr=0x0091fdb0 main_thread
    2.766011 write prev=0 -> 1 lr=0x0091fdb0 main_thread
    2.779481 write prev=1 -> 1 lr=0x0091fdb0 main_thread
    2.840156 read  prev=1 -> 1 lr=0x0091fdb0 main_thread     <== first read that sees 1

Every write that changes the value, whole run:

    2.393590  0 -> 1  lr=0x00609a40
    2.497101  1 -> 0  lr=0x0091fd98
    2.587907  0 -> 1  lr=0x0091fd98
    2.682091  1 -> 0  lr=0x0091fdb0
    2.766011  0 -> 1  lr=0x0091fdb0

Mechanically checked, not read off by eye:

    guest events: 1956 ; self among them: 0
    on word: 576 (r 564 w 12)
    first 0->1 write 2.393590 lr=0x00609a40 ; first read==1 2.840156 lr=0x0091fdb0
    reads==1 before write: 0

The same run's block watch agrees write for write:

    0x00000000 -> 0x00000000 by rsv::store_range  lr=0x00609a40
    0x00000000 -> 0x00000001 by rsv::store_range  lr=0x00609a40
    0x00000001 -> 0x00000000 by rsv::store_range  lr=0x0091fd98
    0x00000000 -> 0x00000001 by rsv::store_range  lr=0x0091fd98
    0x00000001 -> 0x00000000 by rsv::store_range  lr=0x0091fdb0
    0x00000000 -> 0x00000001 by rsv::store_range  lr=0x0091fdb0

Reproduced in three independent runs at different start offsets: run B (before the
self-read fix) at 3.348297 -> 3.531398, run D at 2.409792 -> 3.074423, run E at
2.393590 -> 2.840156. The 0->1 always precedes every read of 1.

## The answer

**The write that puts 1 at 0x0177caa8 comes before any read of it, in every run
measured. It is an stw inside f_0091f364, reached by the call at 0x00609a3c from
f_006099e8. The watcher is not lying and the reader is not wrong.** The 1 is not
in BSS: the ELF cannot supply it (Measurement 1) and the arm-time word is 0.

The write, named to the instruction:

- guest lr 0x00609a40 is the return address of "bl" at 0x00609a3c
  (48315929, decoded -> 0x0091f364), inside f_006099e8_body at generated source
  out-rdr/ppu/rdr_061.cpp:5357-5358:
      ctx.lr = 0x609a40; f_0091f364(ctx, base);
- f_0091f364 is a counter setter: it loads the addresses of two counters from the
  TOC and writes them,
      0x0091f3ac: 81628224  lwz  r11, -32220(r2)
      0x0091f3b4: 81228220  lwz  r9,  -32224(r2)
  and the TOC really holds the watched words:
      TOC (r2) = 0x0139ebb0 (loader log)
      [0x01396dd4] (toc-32220) = 0x0177caa8
      [0x01396dd0] (toc-32224) = 0x0177caa4
- the store that puts the 1 there is stw #3 of the block, "914b0000 stw r10,0(r11)"
  at 0x0091f3d4, with r10 set to 1 by "addi r10,r0,1" at 0x0091f3a8 on the
  "counter is zero" arm of cmpi cr7,r0,0 / bc BO=12 BI=30:
      0x0091f3b8: 2f800000  cmpi  cr7, r0, 0
      0x0091f3bc: 38000400  addi  r0, r0, 1024
      0x0091f3c0: 914b0000  stw   r10, 0(r11)      <- 0x0177caa8 = 0
      0x0091f3c4: 90090000  stw   r0,  0(r9)       <- 0x0177caa4 = 0x400
      0x0091f3c8: 419e0014  bc    BO=12 BI=30 -> 0x0091f3dc
  and in run E the first two writes are exactly that pair, the 0->0 at 2.293473
  and the 0->1 at 2.393590.

So 0x0177caa8/0x0177caa4 are a live counter pair: 0x0177caa8 counts executions
and 0x0177caa4 accumulates a time slice (0x400, 0x200, 0x100, ...); f_0091f364
increments the count and adds the slice. The 0->1 is the count going from 0 to 1 --
the first execution of the block -- not an uninitialised word and not a watcher
artifact.

### The f_0091fd18 read

f_0091fd18 calls f_0091f364 at "ctx.lr = 0x91fd98" (out-rdr/ppu/rdr_092.cpp:26004),
and the event with lr 0x0091fd98 at 2.559227 is the return of that call, so the
increment it observes is the earliest increment any caller can see. The read that
first sees 1 (2.840156, lr 0x0091fdb0) is f_00921138 from rdr_092.cpp:27433.

### W1's two runs, settled

W1's "2.308099 peek 0x0177caa8 = 0x00000001" was taken inside a blockwatch hit for
0x0177cc60: a peek at a different word, at a moment when the counter was already 1.
Correct value, correct read, and no evidence of an unwritten initial 1. W1's other
run (first write 2.412857 0 -> 0, then 0->1 at 2.442203, then 1->0, 0->1, 1->0,
0->1) is the same sequence as run E at a different start offset: a redundant 0->0
write, the 0->1, then the counter oscillating while the RAGE audio thread reads it.
Nothing in either run contradicts this one.

The claim that the 1 was in BSS before any write is refuted on three independent
grounds:

1. the ELF: 0x0177caa8 is segment offset 0x44caa8 of PT_LOAD[1], beyond p_filesz
   0x1bc014, so zero-fill BSS -- the file holds no initial value (Measurement 1);
2. the loader: loader.cpp memsets the BSS tail to 0, and the watch's arm-time read
   logged 0x00000000 before the first guest instruction;
3. the run: the first event observing 1 is a WRITE with prev 0, and there are 0
   reads observing 1 before it, over 576 events on that word, with 0 chain gaps and
   0 self-read contamination.

### Why W1's earlier reading looked like a contradiction

W1 could not see this because its watch reported only writes, and its "peek" was a
read of one word taken inside a report about another. A write-only watch plus a
peek gives you two facts about two words at one instant; it cannot order a write
against a read. The READ watch in the same run can, and with the self-read guard
the ordering is unambiguous: 0 -> 1 at 2.393590, first read of 1 at 2.840156.

## Confirmation after the rebase on main 22d2a0c

Run F, same tree and binary as the merged branch, same command as run E:

    2025 guest trap events, 0 self reads among them
    571 events on 0x0177caa8 (559 reads, 12 writes)
    0 CHAIN GAP
    first 0->1 write: 4.346518 lr=0x00609a40
    first read==1:    5.473972 lr=0x0091fdb0
    reads==1 before write: 0
    value changes: 4.346518 0->1, 4.619699 1->0, 4.850968 0->1, 5.085010 1->0, 5.294042 0->1
    watch totals: 2001 guest events, 49 self reads dropped

Four runs, four start offsets, the same ordering every time:

    run  first 0->1 write        first read of 1       gap
    B    3.348297 (lr 0x609a40)  3.531398*             0.183 s   (*the watcher's own read; see the defect section)
    D    2.409792 (lr 0x609a40)  3.074423              0.665 s
    E    2.393590 (lr 0x609a40)  2.840156              0.447 s
    F    4.346518 (lr 0x609a40)  5.473972              1.127 s

The write that carries the 1 has guest lr 0x00609a40 in every run, which is the
return address of the call to f_0091f364. It is never a read.

## Tests

Full suite after the rebase, clean tree, all targets built:

    $ ctest --test-dir build-40 -j 4
    100% tests passed, 0 tests failed out of 35

Including the new test this measurement added:

    Test #14: write_paths ...........................   Passed    0.01 sec

and the neighbouring reservation tests unchanged:

    Test #13: reservation_xproc ............. Passed
    Test #15: lock_counter .................. Passed
    Test #16: guest_spinlock ................ Passed
    Test #17: reservation_increment ......... Passed
    Test #18: lwmutex_contention ............ Passed
    Test #19: lwmutex_hle ................... Passed

## Final binary, run G (after the last code change)

Run G, on the binary that ships with this branch:

    2011 guest trap events, 0 self reads among them
    569 events on 0x0177caa8 (557 reads, 12 writes)
    0 CHAIN GAP
    first 0->1 write 4.872751 lr=0x00609a40
    first read==1    5.752610 lr=0x0091fdb0
    reads==1 before write: 0
    watch totals: 2001 guest events, 42 self reads dropped

Five runs, five start offsets, the same ordering every time, always with the
0->1 write carrying guest lr 0x00609a40:

    run  first 0->1 write        first read of 1       gap
    B    3.348297 (lr 0x609a40)  3.531398*             0.183 s
    D    2.409792 (lr 0x609a40)  3.074423              0.665 s
    E    2.393590 (lr 0x609a40)  2.840156              0.447 s
    F    4.346518 (lr 0x609a40)  5.473972              1.127 s
    G    4.872751 (lr 0x609a40)  5.752610              0.880 s

(*run B's numbers are from before the self-read fix; the fix did not change the
write times, it removed a fabricated read.)

## Final test run on the shipped binary

    $ ctest --test-dir build-40 -j 4
    100% tests passed, 0 tests failed out of 35
