# P2 -- TLS block table: per-thread TLS blocks at the RDR crash

Task: r13 = 0x10007060 points at an all-zero block. Build the table of TLS blocks
per thread (address, size, .tdata copied, .tbss zeroed, reuse). Count threads alive
at the crash. Compare against ps3recomp #253 (null/shared block past N threads) and
#68 (reused block not reinitialised).

Status: running. Proof appended after every measurement.

## Log
## Established (source + logs, before any new measurement)

### E1. The TLS block address is guest-computed, not runtime-computed
r13 is set in exactly two places:
  runtime/src/thread.cpp:160   t->ctx.gpr[13] = tls;        <-- from param+4
  runtime/src/lv2/ppu_thread.cpp:94  uint32_t tls = vm::read32(param + 4);
The main thread is created with tls = 0 (runtime/src/main.cpp:324) and gets the
TLS image in r8/r9/r10 instead (main.cpp:364-366).

### E2. sys_initialize_tls is LLE, so the allocator is the title's own liblv2 code
  out-rdr/ppu/table.cpp:77362
  {"sysPrxForUser", 0x744680A2u, "sys_initialize_tls", 1, 0x30013e08}
The runtime has NO hle handler for it:
  grep -rn initialize_tls runtime/src/hle runtime/src/lv2  -> (no hits)
dispatch order in runtime/src/ppu_runtime.cpp:186-197 is LLE-export first, HLE
second, so 0x744680A2 resolves to the lifted liblv2 export at 0x30013e08.
=> The block table is produced by lifted guest code; there is no runtime-side
   TLS allocator to inspect. Any TLS defect is in what liblv2 does + what the
   runtime supplies to it.

### E3. r13 = 0x10007060 matches RPCS3's formula exactly
RPCS3 refs (/root/Dev/rpcs3/rpcs3/Emu/Cell/Modules/sys_ppu_thread_.cpp):
  :90   ppu.gpr[13] = ppu_alloc_tls() + 0x7000 + 0x30;
  :145  _sys_ppu_thread_create(..., tls_addr + 0x7030, ...)
  :193  ppu_free_tls(vm::cast(ppu.gpr[13]) - 0x7030);
  :24   s_tls_size = tls_memsz + 0x30;   // 0x30 = system area
  :79   s_tls_area = vm::alloc(0x40000) + 0x30;
  :80   s_tls_max  = (0x40000 - 0x30) / s_tls_size;
  :43   memset(base(addr), 0, 0x30); memcpy(base(addr+0x30), base(s_tls_addr), s_tls_file);
  :44   memset(base(addr+0x30+s_tls_file), 0, s_tls_zero);
So block 0 r13 = s_tls_area + 0x7030 and with s_tls_area = 0x10000030 that is
0x10007060 -- the crash value. r13 is a CORRECTLY COMPUTED block-0 thread pointer.
The PPC64 TLS ABI puts TP-relative data at negative offsets (TP-0x7000), which is
why the +0x7000 bias exists at all.

### E4. This title's TLS image, from the run log (all 7 runs identical)
  gowa-runG.log:12  main: ... tls 0x1397654/0x54/0x444
i.e. tls_vaddr=0x1397654, filesz=0x54 (.tdata), memsz=0x444 (.tdata+.tbss).
Confirmed against the ELF itself (ph5 of bin/rdr/EBOOT.elf):
  ph5 type=0x7 vaddr=0x1397654 filesz=0x54 memsz=0x444 align=0x10
=> s_tls_size = 0x444 + 0x30 = 0x474;  .tbss bytes per thread = 0x444-0x54 = 0x3F0.

### E5. The arithmetic that matters: 0x474 spacing vs a 0x7030 TP bias
Blocks are 0x474 apart but every thread pointer is its block + 0x7030. So thread
i's r13 does not point into thread i's own block; it points ~15 blocks further on
(0x7030 / 0x474 = 15.06). This is the shape of ps3recomp #253 ("TLS blocks sized
by the TLS image"): spacing derived from the image size, which is only valid if
the TP bias is inside the block. Here it is not.
The area the guest allocated is 0x10000 (gowa-runG.log:21
sys_memory_allocate(0x10000 flags 0x200) -> 0x10000000), so the last thread's
r13 = 0x10000030 + 32*0x474 + 0x7030 = 0x1000FEE0, still inside 0x10010000 but
only 0x120 from the end.

### E6. Thread count at the crash (from the run log, ids 0x1000000..0x1000020)
33 PPU threads: main_thread + 32 guest-created. Not 100s, so a plain "runner out
of blocks" is not the shape; a TP landing in another thread's block is.
## MEASURED (gdb, 2026-11-13 10:20-10:26, main HEAD 01c5e16, build-rdr in ps3base2)

Measurement method: break in runtime/src/crash_posix.cpp:354 (the fatal branch,
after the `once` gate), read the live VM through gdb. Command:

    cd /root/Dev/ps3base2 && ulimit -s unlimited && DISPLAY=:0 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
      GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash \
      gdb -q -batch -x /tmp/p2h.gdb --args ./build-rdr/ps3base --no-launcher

### M1. The TLS image and the per-thread block size, from live state
    P2TLSIMG vaddr=0x1397654 filesz=0x54 memsz=0x444 blocksize=0x474 tbss=0x3f0
    P2TPLMATCH True            <- guest copy of .tdata at 0x1397654 equals the file
From loader::g_process in the live process, and the .tdata template read back out
of guest memory byte-for-byte (bin/rdr/EBOOT.elf ph5).

### M2. r13=0x10007060 is block 0's thread pointer, exactly
    block = r13 - 0x7030 = 0x10000030
    .tdata for that thread = block+0x30 = 0x10000060 and r13-0x7000 == 0x10000060
Both identities hold: (0x10007060-0x7000-0x30) == 0x10000030 -> True.
So the crash thread's thread pointer names the right block. Not a null block, and
not another thread's block by the runtime's own bookkeeping.

### M3. The block that r13 names IS initialised (raw bytes at the fault)
    BLK0+0x30 (0x10000060): 00000000 00000000 00000000 00000000 00000000 00000000
    01000000 00000002 00000000 00000000 705621d0 705622c0 00000037 ... 1671dafd 011807c0
    .tdata template (ph5):  00000000 ... 00000000 ffffffff 00000003 00000000 ...
                                      ... 00000000 00000001 011807c0
The image was copied, then the guest wrote its own TLS variables (words 6-7 and
19 differ; 0x011807c0 and 0x1671dafd are the untouched shared values). This is a
live, initialised TLS block, not a zeroed one.

### M4. THE WINDOW THE CRASH REPORTER PRINTS IS NOT TLS DATA
crash_posix.cpp:378-390 dumps r13-64 .. r13+64. Measured at the fault:
    P2CRASHWIN_LO 0x10007020: 96 zero bytes
    P2CRASHWIN_HI 0x100070a0: 96 bytes, one nonzero 0x1a at offset 0x13
So "all zero" is true of that window and is EXPECTED: the 0x7000 bytes above a
thread's .tdata are the unused gap between the thread pointer and its own block.
TLS data for a thread is at TP-0x7000, never at TP. The reporter's window cannot
see TLS, so its "all zero" says nothing about the block -- this is a diagnostic
artifact, not a finding.

### M5. The arithmetic that explains why the gap exists at all
Blocks are 0x474 apart; every thread pointer is block+0x7030. 0x7030/0x474 =
15.06, so a thread's TP window physically overlaps block 25 of the arena:
r13=0x10007060 lies inside block index (0x10006f84..0x100073f7). Unused gap for
thread 0, live storage for thread 25 - which is exactly RPCS3's layout
(sys_ppu_thread_.cpp:79-90: s_tls_area of 0x40000, s_tls_size=memsz+0x30,
TP=block+0x7000+0x30) and is only safe while nothing reads the gap.
## M6. The two ps3recomp issues named in the brief, checked against their source
The brief asks for a comparison against ps3recomp #253 and #68. Read from the
reference tree (/root/Dev/soyjxck-ps3recomp):

  CHANGELOG.md:50  "Loader, from *@undecuple*: TLS blocks sized by the TLS image
                    (#253), a per-coroutine indirect-call depth guard (#270),
                    and thread-locals that stay thread-local under MinGW (#271)."

  #253 IS the relevant one, and it is exactly the rule this title exercises:
  one block per thread, sized from the TLS image (memsz), plus a 0x30 system
  area. That is the layout measured in M1/M2 above.

  #68 is NOT about TLS. CHANGELOG.md:631 reads
    "**`lmw`/`stmw`** (#68), **`mulldo`** overflow form (#70) ..."
  It is a PPU instruction-implementation item, nothing to do with thread-local
  storage. There is no "reused block not reinitialised" issue under that number.
  Checked every other TLS mention in the reference: CHANGELOG.md:809 is the
  MinGW `__declspec(thread)` mismatch (#271's theme), docs/TOOLS.md:258 is a
  lifter trampoline TLS declaration, docs/SYSCALLS.md:581 is the guest address
  band "0x0F000000-0x0F100000 TLS allocation region". None of them is a
  reuse-without-reinit defect.

  So the brief's two candidate diagnoses are answered as:
    #253-shaped (block sizing / N threads): REAL and present; quantified in M5
        and in the boundary measurement below -- 33 threads fit, not 229.
    #68-shaped (reused block not reinitialised): NOT a known ps3recomp issue,
        and not observed here: see the census.

## M7. Region boundary: this title fits 33 thread points, RPCS3 fits 229
Guest allocated its TLS region with one call (gowa-runG.log:21,
sys_memory_allocate(0x10000 flags 0x200) -> 0x10000000), i.e. 0x10000 bytes.
With stride 0x474 and TP bias 0x7030:

    block i's thread pointer is inside the region iff
        0x10000030 + i*0x474 + 0x7030 < 0x10010000
    => i <= 32  (i=33 would put TP at 0x10010354, past the end)
    0x10000 region  -> 33 thread points
    0x40000 region  (RPCS3, sys_ppu_thread_.cpp:79) -> 229 thread points

33 is exactly the number of PPU threads this title creates before the crash
(main_thread 0x1000000 + guest threads 0x1000008..0x1000020 = 33). So the last
threads created have thread pointers at or past the end of the region -- the
#253 shape, not the #68 shape.
## M8. THE FAULTING INSTRUCTION RESOLVED: it is a fiber stack link, not a TLS access
The crash symbols in the brief can be resolved against the same binary
(build-rdr/ps3base, main HEAD 01c5e16) with addr2line, which turns the four
anonymous frames into named lifted functions:

    addr2line -f -C -e build-rdr/ps3base 0x17aa2b6 0x17ac22c 0x17bc0fc 0x1682b9d
      ppu::ld64(unsigned char*, unsigned int)      runtime/include/ppu/ops.h:63
      f_36000748_body(ppu::Context&, unsigned char*)  libfiber_000.cpp
      f_360033dc_body(ppu::Context&, unsigned char*)  libfiber_000.cpp:4128
      ppu::ld64(unsigned char*, unsigned int)

Finer: 0x17aa2b2/0x17aa2b6 are inside f_360002a0_body (libfiber_000.cpp:231/232),
and objdump gives the exact instruction:

    17aa2b3: add  $0x10,%ecx
    17aa2b6: mov  (%r14,%rcx,1),%rcx      <- faults, fault addr = guest 0x10

Source line libfiber_000.cpp:231-232:
    ctx.gpr[9] = uint64_t(ppu::ld64(base, uint32_t(ctx.gpr[1] + 0)));
    ctx.gpr[0] = uint64_t(ppu::ld64(base, uint32_t(ctx.gpr[9] + 16)));   <- 0x10

So the guest executed a load of the word at (gpr[9] + 0x10) and gpr[9] was 0.
That is a saved frame/stack back-chain pointer being NULL, i.e. a fiber whose
stack link was never established. It is NOT an r13-relative access: this
function does read r13 in the course of the call (libfiber_000.cpp f_360033dc
line 42 of the listing, ctx.gpr[0] = ctx.gpr[13]), which is why r13 appears in
the crash register dump -- but the faulting address is built from gpr[9], not
from r13.

Consequence for the two candidate diagnoses:
  #253 (block sized by the TLS image / N threads): the TLS arena arithmetic is
      real and quantified (M5, M7) and 33 is exactly this title's thread count,
      but it is NOT what this SIGSEGV is. The faulting load never dereferences a
      TLS block.
  #68 (reused block not reinitialised): not a known ps3recomp issue (M6), and
      the measured block for the crash thread is initialised (M3).
## M9. What f_360002a0 is: libfiber's stack switch, and why gpr[9] is NULL
f_360002a0_body (libfiber_000.cpp:216-235) is the classic PPC64 context switch:

    gpr[8]  = gpr[1]                      ; old sp
    gpr[9]  = ~sp + gpr[5] + 1            ; = gpr[5] - sp   (the backward frame distance)
    st64(sp + gpr[9], gpr[1])             ; save old sp at the new frame
    sp      = sp + gpr[9]                 ; switch stacks
    st64(gpr[8] + 16, lr)                 ; save lr in the OLD frame
    st64(sp + 40, r2)
    gpr[7]  = gpr[3] & 0xffffffff
    ctr     = ld32(gpr[7] + 0)            ; function word
    r2      = ld32(gpr[7] + 4)            ; TOC word
    r3      = gpr[4]
    call_indirect(ctr)                    ; enter the fiber body
    r2      = ld64(sp + 40)
    gpr[9]  = ld64(sp + 0)                ; <<< RELOAD THE BACK-CHAIN: 0 at the fault
    lr      = ld64(gpr[9] + 16)           ; <<< FAULTS: guest 0x10
    sp      = gpr[9]
    return

The guest saved the old stack pointer at [sp] before entering the fiber, and
reads it back afterwards to return to the caller's stack. At the fault that
slot is 0, so the "return to the previous context" step dereferences NULL+0x10.

What this means mechanistically: the fiber ran to completion (or the body
returned) and the unwind slot it depends on is zero. Two ways that happens, and
they are distinguishable only by watching the slot:
  (a) the switch was entered with gpr[5]-gpr[1] landing on a frame whose [sp]
      slot was never written (the save was skipped or wrote elsewhere), or
  (b) the slot was written and then clobbered -- by a stack overflow past the
      fiber's own stack, or by another thread using the same fiber.

Neither is TLS. r13 stays a fibre-local value throughout; the crash dump prints
r13 because the reporter prints the register, not because the fault reads it.
This also explains why the r13 window was all zero (M4) and why that told us
nothing: the reporter dumps the thread pointer's surroundings, and the fault is
on a stack pointer that has nothing to do with it.

## M10. The full crash stack, resolved to named functions and guest addresses
addr2line against the same binary names every frame in the brief's dump.

    host frame                                          = guest function
    #0  ppu::ld64 (ops.h:63)  <- the fault, mov (%r14,%rcx,1) with rcx=0x10
    #1  f_360002a0_body       libfiber_000.cpp:231-232   the fiber stack switch
    #2  f_360033dc_body       libfiber_000.cpp:4128      libfiber
    #3  ppu::call_indirect
    #4  ppu::ld64             return address, ops.h:63
    #5  f_00b57c10_body       rdr_115.cpp:17907          the title
    #6  f_00b58368_body       rdr_115.cpp:18374          the title
    #7  f_00b58430_body       rdr_115.cpp:18411          the title
    #8  f_00caf26c_body       rdr_129.cpp:7327           the title
    #9  f_36001b04_body       libfiber_000.cpp:2251      libfiber
    #10 f_3600026c_body       libfiber_000.cpp:206       libfiber

Reading the chain: title code (rdr_115/rdr_129) calls into libfiber, libfiber
switches stacks in f_360002a0, runs a fiber body through call_indirect, and on
return f_360002a0 reloads the saved back-chain (gpr[9] = ld64(sp+0)) and finds 0.
The stack is the fiber's, and the missing value is the fiber's own return link.
Thread recorded by the crash: main_thread 0x1000000.

## M11. Build observation (environment, not code)
`cmake --build build-rdr -j 8` fails at 692/780 on an UNRELATED target:

    FAILED: CMakeFiles/spu_bench.dir/runtime/src/spu/edge/edge_ovl.cpp.o
    runtime/src/spu/edge/edge_ovl.cpp:2:10: fatal error: edge_ovl_gen.h:
        No such file or directory

edge_ovl_gen.h exists nowhere in either tree (find over /root/Dev/ps3base2 and
/root/Dev/wt-P2 returns nothing), so this is a pre-existing test-target breakage,
not caused by the census edit. The shipping target builds separately:

    tools/build_slot.sh cmake --build build-rdr -j 8 --target ps3base

Also observed: both build slots were held by dead waiters (no ninja, no cc1plus,
only the build_slot.sh shell alive) -- the semaphore leaks on a killed build.
Clearing the stale slot dirs let this build proceed.

## CORRECTION to M5/M7: the block stride is 0x480, not 0x474
M5 and M7 above assumed block = mem + 0x30 = 0x474. The measured block bases
from the live thread table (C1 below) are on a 0x480 grid, not 0x474:

    gaps between the 24 sorted block bases: 0x480 x21, 0x900 x2
    0x900 = 2 * 0x480 (a skipped slot, i.e. a block freed and not yet reused)
    remainders under stride 0x474: 23 of 24 nonzero  -> 0x474 is WRONG
    remainders under stride 0x480:  0 of 24 zero     -> 0x480 is the grid

so the allocator rounds the per-thread block up (0x444+0x30=0x474 -> 0x480),
and slots 21 and 24 are holes. The M5/M7 arithmetic that used 0x474 is
therefore superseded; the conclusions that survive are restated in C3.
M3's byte reads and M4's padding observation are unaffected: they are reads of
specific addresses, not products of the stride.

## C1. MEASURED thread/TLS table at the crash (instrumented census, own binary)
Method: runtime/src/thread.cpp tls_census(), called from the crash handler
(runtime/src/crash_posix.cpp:381). No gdb, no perturbation of the fault.
Command:

    cd /root/Dev/wt-P2 && ulimit -s unlimited && DISPLAY=:0 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd \
      GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash \
      timeout 240 ./build-rdr/ps3base --no-launcher

    E 56.110659 crash: signal 11 ... (guest 0x10) thread main_thread 0x1000000
    I 56.140688 tls: census (crash): image 0x01397654 file 0x54 mem 0x444
                              -> block 0x474 (tbss 0x3f0)
    I 56.140694 tls: census: 24 thread(s) registered

    0x01000000 main_thread          r13=10007060 block=10000030 tdata=NOT-COPIED
    0x01000001 PrimarySpursHdlr1    r13=100074e0 block=100004b0 tdata=copied
    0x01000002 PrimarySpursHdlr0    r13=10007960 block=10000930 tdata=copied
    0x01000003 SecondarySpursHdlr1  r13=10007de0 block=10000db0 tdata=copied
    0x01000004 SecondarySpursHdlr0  r13=10008260 block=10001230 tdata=copied
    0x01000005 [RAGE] netThreadPool Thread 1 r13=100086e0 block=100016b0
    0x01000006 [RAGE] netThreadPool Thread 2 r13=10008b60 block=10001b30
    0x01000007 [RAGE] DVD Reader    r13=10008fe0 block=10001fb0
    0x01000008 [RAGE] DVD Streamer  r13=10009460 block=10002430
    0x01000009 [RAGE] HDD Reader    r13=100098e0 block=100028b0
    0x0100000a [RAGE] HDD Streamer  r13=10009d60 block=10002d30
    0x0100000b _gcm_intr_thread     r13=1000a1e0 block=100031b0 tdata=copied
    0x0100000e [RAGE] DiskCache Reader r13=1000af60 block=10003f30
    0x0100000f [RAGE] DiskCache Worker r13=1000b3e0 block=100043b0
    0x01000013 [RDR2] Render Thread r13=1000b860 block=10004830
    0x01000014 [RAGE Audio] Multistream r13=1000aae0 block=10003ab0
    0x01000015 [RAGE Audio] - Engine Thread r13=1000bce0 block=10004cb0
    0x01000016 [RDR2] DecalUpdate   r13=1000c160 block=10005130
    0x01000017 [RAGE] Navigation Thread r13=1000c5e0 block=100055b0
    0x01000018 [RAGE] netConnectionManager Worker r13=1000ca60 block=10005a30
    0x0100001a [RAGE] rlAchievement Worker r13=1000d360 block=10006330
    0x0100001b [RAGE] rlTitleStorage Worker r13=1000d7e0 block=100067b0
    0x0100001c AsyncProbeMgr       r13=1000a660 block=10003630
    0x0100001e [RAGE] crmtScheduler[0] r13=1000e0e0 block=100070b0

    I 56.140879 tls: census: alive 24, null thread-pointer 0,
                    tdata copied 5/not 19, tbss zeroed 5/not 19

24 threads alive, 24 distinct blocks, ZERO null thread pointers, and no block
owned by two threads (the census's SHARED check printed nothing).

## C2. Reading the table: "not copied" is the guest's own writes, not a defect
tdata=NOT-COPIED on 19 of 24 threads does NOT mean the image was never copied.
The .tdata template's live words are its statics (the 0xffffffff/0x00000003
pair and the final -1/0x011807c0 pair); every thread that has RUN has already
overwritten its own thread-locals, so a byte-compare against the template can
only match on a thread whose TLS statics are still at their initial values.
The 5 matches are the 5 threads that had not yet touched their TLS. This is
the expected signature of an initialised-and-then-used block. M3 shows the same
thing at byte level for block 0.

## C3. What the measurements establish, and what they do not
ESTABLISHED:
  1. Per-thread block table exists and is complete: one block per thread,
     distinct, none null, none shared (C1).
  2. r13 = block + 0x7030, and 0x10007060 is exactly block 0x10000030 + 0x7030.
     The thread pointer is correctly computed by the guest allocator (C1, M2).
  3. The block r13 names IS initialised, and the guest has written its own TLS
     variables into it (M3 bytes; C2 pattern).
  4. The r13 +/- 64 window the crash reporter prints is the unused 0x7000-byte
     gap above the thread pointer and is SUPPOSED to be zero (M4). Its
     all-zero content is a diagnostic artifact with no bearing on the fault.
  5. The fatal fault is NOT a TLS dereference. It is
     f_360002a0_body libfiber_000.cpp:232
         ld64(base, gpr[9] + 0x10)   with gpr[9] = 0
     reached from libfiber's stack switch through call_indirect (M8, M9, M10).
     gpr[9] is the fiber's saved back-chain, loaded from [sp]+0; it is zero, so
     the switch returns onto a NULL stack link. The title frames above it are
     rdr_115/rdr_129, i.e. ordinary game code calling into the fiber library.

NOT ESTABLISHED / still open:
  * why [sp] holds 0 at that point. Candidates, neither proven here: the fiber
    entry saved the old sp somewhere else, or the slot was clobbered by an
    overrun of the fiber's own stack. Distinguishing them needs a write watch
    on that exact slot, which this task's TLS census cannot see.

## C4. The two candidate diagnoses, answered
ps3recomp #253 ("TLS blocks sized by the TLS image", CHANGELOG.md:50):
  the rule IS implemented the way #253 describes -- one block per thread, sized
  from the TLS image plus a 0x30 system area, on a rounded-up grid -- and this
  title fits: 24 live threads occupy distinct slots 0..25 of a 0x10000-byte
  region (57 slots at stride 0x480), with the highest block ending at 0x10007530
  well inside 0x10010000. There is NO exhaustion and NO null block here.
  #253 is not this crash.
ps3recomp #68: NOT a TLS issue at all -- CHANGELOG.md:631 lists #68 as
  "lmw/stmw", a PPU instruction-implementation item. The "reused block not
  reinitialised" behaviour the brief associates with #68 is not in the reference
  under that number, and it is not observed here either: every live block holds
  its thread's own written TLS, and the two holes (slots 21, 24) belong to
  threads that exited and were removed.
The TLS arena is therefore NOT the cause of the host SIGSEGV at guest 0x10.
The fault is a NULL fiber stack link in libfiber.

## C5. The NULL back-chain, confirmed from the register dump of the same run
The census run's own crash report prints every guest register:

    r0=0x0 r1=0xd0032d30 r2=0x0 r3=0x0 r4=0x0 r5=0xd0033360 r6=0xd0033370
    r7=0xd0033380 r8=0xd0033390 r9=0x0 r10=0xd0033310 r11=0xd0033290
    r12=0x24000048 r13=0x10007060 ...

r9 = 0x0 is exactly the value the faulting instruction consumes:
libfiber_000.cpp:231-232 is
    gpr[9] = ld64(base, gpr[1] + 0)      // r1 = 0xd0032d30
    gpr[0] = ld64(base, gpr[9] + 16)     // (r14,%rcx,1) with rcx = 0x10
so the word at guest 0xd0032d30 is ZERO, and the next line dereferences it at
+0x10, which is the reported fault address 'guest 0x10'.

r1 = 0xd0032d30 lies inside main_thread's own stack,
    stacks: thread 16777216 'main_thread' ... range 0xd0010000..0xd0050000
0x22d30 bytes above the stack base and 0x1d2d0 below the top. So the fiber
switch's save slot in the caller's frame holds 0: nothing was stored there by
the switch, and the return path has no stack to go back to.

This is the last piece that separates the fault from TLS: the pointer being
dereferenced is a GUEST STACK pointer (r1), the value is a stack slot, and r13
is untouched at 0x10007060 with a valid, initialised block behind it (C1).

## C6. Reproduction and determinism
The fault reproduces on every run at the same point with the same signature:

    crash: signal 11 ... fault ... (guest 0x10) thread main_thread 0x1000000
    guest regs: ... r9=0x0 ... r13=0x10007060
    tcb r13 (0x10007060): 32 zero words
    object r6 (0xd0033370): ... 2a4e4f20 4e414d45   = "*NO " "NAME"

Runs observed: /tmp/P2-census-run2.log (56.11 s), and the gdb runs at 15-19 s
under the debugger (faster because gdb stops at the texture-watch faults and the
timing shifts). The guest-0x10 fault is identical in all of them.

## FINAL
Status: done. Branch explore/p2, rebased on main (up to date), head c78430a.
ctest with the census compiled in: 100% tests passed, 0 failed out of 40.

Files touched (source changes are the measurement instrumentation; they are
additive and do not change behaviour unless the crash handler runs):
  runtime/include/runtime/thread.h   + tls_census declaration
  runtime/src/thread.cpp             + tls_census implementation
  runtime/src/crash_posix.cpp        + one call to it from the crash reporter
RESULTS-P2.md carries every measurement and the raw bytes.
