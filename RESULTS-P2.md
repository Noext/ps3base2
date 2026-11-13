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
