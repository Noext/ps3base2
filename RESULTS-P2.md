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
