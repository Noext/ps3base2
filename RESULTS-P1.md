# RESULTS-P1 -- reading the RDR host SIGSEGV at exe+0x17aa2b6

Branch explore/p1, worktree /root/Dev/wt-P1, base main 01c5e16.

## Task
1. addr2line exe+0x17aa2b6 and every crash frame; name the function (HLE handler
   vs lifted guest code).
2. Guest thread, guest lr, last 30 imports that thread made with returns.
3. Find '*NO NAME*' in the game ELF and in every module -- whoever contains it
   names the subsystem.
4. If the site is one of our handlers (cellSaveData* first suspect): arguments
   received, structure involved, compare against RPCS3's header.

## Log

### M1 -- addr2line of every crash frame (build-rdr/ps3base, main 01c5e16)

```
$ for a in 0x17aa182 0x17aa290 0x17aa2b6 0x17ac22c 0x17ac515 0x17ad1cc 0x17b0d23 \
       0x17b3eb3 0x17b7955 0x17bc0fc 0x1682b9d 0x815f599 0x75b8156 0x75ba3b4 0x75ba6f9; do
    addr2line -f -C -e build-rdr/ps3base $a; done
0x17aa182  f_3600026c_body   out-rdr/ppu/libfiber_000.cpp:206
0x17aa290  f_360002a0_body   out-rdr/ppu/libfiber_000.cpp:230
0x17aa2b6  ppu::ld64         runtime/include/ppu/ops.h:63   <-- FAULT
0x17ac22c  f_36000748_body   runtime/include/ppu/ops.h:104
0x17ac515  f_360008a0_body   out-rdr/ppu/libfiber_000.cpp
0x17ad1cc  f_3600093c_body   out-rdr/ppu/libfiber_000.cpp
0x17b0d23  f_360012a0_body   out-rdr/ppu/libfiber_000.cpp:1565
0x17b3eb3  f_36001b04_body   out-rdr/ppu/libfiber_000.cpp:2251
0x17b7955  f_3600254c_body   out-rdr/ppu/libfiber_000.cpp:3138
0x17bc0fc  f_360033dc_body   out-rdr/ppu/libfiber_000.cpp:4128
0x1682b9d  ppu::call_import_inner+0x85d (ppu_runtime.cpp)
0x815f599  f_00caf26c_body   out-rdr/ppu/rdr_129.cpp:7327
0x75b8156  f_00b57c10_body   out-rdr/ppu/rdr_115.cpp:17907
0x75ba3b4  f_00b58368_body   out-rdr/ppu/rdr_115.cpp:18374
0x75ba6f9  f_00b58430_body   out-rdr/ppu/rdr_115.cpp:18411
```

Plus `nm` (exact symbol bound, addr2line rounds to the whole inline chain):

```
0x17aa2b6 f_360002a0_body+0xe6
0x1682b9d ppu::call_import_inner+0x85d
```

**Conclusion:** every frame 0..22 is LIFTED GUEST CODE, not one of our HLE
handlers. The faulting site is the second instruction of the guest function
360002a0's epilogue, a plain `ld` through the frame pointer chain.

### M2 -- disassembly at the fault (objdump, build-rdr/ps3base)

```
17aa290: mov 0x8(%rbx),%rax          ; r9 = frame->+8   (guest r9)
17aa294: lea 0x28(%rax),%ecx         ; r9 + 0x28
17aa297: mov (%r14,%rcx,1),%rcx      ; ld  r9+0x28
17aa29b: movbe %rcx,0x10(%rbx)
17aa2a1: mov %eax,%eax
17aa2a3: mov (%r14,%rax,1),%rax      ; ld  r9       <- r9 == 0
17aa2ad: movbe %rax,0x48(%rbx)
17aa2b3: add $0x10,%ecx
17aa2b6: mov (%r14,%rcx,1),%rcx      ; ld  r9+0x10  <- FAULT, ea = 0x10
```

r14 is the guest base. `ecx` computed from rax = *(frame+8) = 0, so the load
reads guest 0x10. Matches `fault 0x...010 (guest 0x10)` exactly.

### M3 -- the lifted source of the faulting guest function

`out-rdr/ppu/libfiber_000.cpp`, f_360002a0_body:

```
    ctx.gpr[8] = ctx.gpr[1] | ctx.gpr[1];
    { uint64_t x = ~ctx.gpr[1]; uint64_t y = ctx.gpr[5]; ... ctx.gpr[9] = r; }  // r9 = r5 - sm1
    { uint32_t ea = uint32_t(ctx.gpr[1] + ctx.gpr[9]); ppu::st64(base, ea, uint64_t(ctx.gpr[1])); ctx.gpr[1] = ctx.gpr[1] + ctx.gpr[9]; } // stdu r1,r9(r1)
    ctx.gpr[0] = ctx.lr;
    ppu::st64(base, uint32_t(ctx.gpr[8] + 16), ctx.gpr[0]);   // std r0,16(r8)  save LR in caller frame
    ppu::st64(base, uint32_t(ctx.gpr[1] + 40), ctx.gpr[2]);   // std r2,40(r1)
    ctx.gpr[7] = ctx.gpr[3] & 0xffffffff;                     // r7 = func desc
    ctx.gpr[0] = ld32(base, r7+0); ctx.ctr = r0;              // lwz r0,0(r7)
    ctx.gpr[2] = ld32(base, r7+4);                            // lwz r2,4(r7)   TOC from the OPD
    ctx.gpr[3] = ctx.gpr[4];                                  // r3 = arg
    ctx.lr = 0x360002d0; ppu::call_indirect(ctx, base, uint32_t(ctx.ctr));
    ctx.gpr[2] = ld64(base, r1+40);                           // ld r2,40(r1)
    ctx.gpr[9] = ld64(base, r1+0);                            // ld r9,0(r1)    back-chain
    ctx.gpr[0] = ld64(base, r9+16);                           // ld r0,16(r9)   <-- FAULT
    ctx.lr = ctx.gpr[0];
    ctx.gpr[1] = ctx.gpr[9];
```

This is the standard PPC64 `-mminimal-toc`/ELFv1 **function-pointer thunk**:
save LR into the caller's frame at +16, load TOC from the descriptor,
indirect-call, then walk the back-chain to restore LR.

### M4 -- which module 360002a0 belongs to: libfiber (FULL LLE)

From the crash log (`/tmp/seg.log`, run 42.96 s):

```
I 0.667358 loader: loaded libfiber (out-rdr/gen/reloc/libfiber.elf) text 0x36000000-0x36013000 ...
```
And the faulting lifted file is `out-rdr/ppu/libfiber_000.cpp`. So the whole
crash stack 0..22 is **inside Sony's libfiber**, running as lifted LLE code.

### M5 -- `*NO NAME*`, the string on the faulting frame

Byte search (not text-mode grep, which misses these files) over the game ELFs,
the firmware PRX/SPRX set and the whole **dev_flash** tree:

```
$ python3 - search b'*NO NAME*' in bin/*/*.elf fw/4.91/*.sprx /root/Dev/devflash-out
FULL /root/Dev/ps3base2/fw/4.91/libfiber.prx
FULL /root/Dev/ps3base2/fw/4.91/libspurs_jq.prx
FULL /root/Dev/ps3base2/fw/4.91/libsre.prx
FULL /root/Dev/ps3base2/fw/4.91/reloc-rdr/libsre.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc-rdr/libspurs_jq.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc/libfiber.elf
FULL /root/Dev/ps3base2/fw/4.91/reloc/libsre.elf
0 bin/rdr/EBOOT.elf  bin/gta/EBOOT.elf  bin/mc/mc.elf  bin/01.12/GOWA.elf
0 every fw/4.91/*.sprx (the HLE-side PRX set)
```

**The game ELF does not contain `*NO NAME*` at all.** The string lives only in
libfiber + libsre + libspurs_jq. This is the SPU-task name that cellSpurs uses
for a task created without a name -- i.e. the crash is in the firmware's
fiber/SPURS support library, and the `*NO NAME*` string visible in r6/r7 is a
just-created SPURS task name struct, not a save-data name.

### M6 -- guest thread, guest lr, and the 24-frame guest stack

From `/tmp/real-flash.log` (the run that reaches the ALERT screen) and reproduced
identically in `/tmp/p1-crash.log` at 163.72 s (same site, same registers):

```
crash: signal 11 at exe+0x17aa2b6 fault ...(guest 0x10) thread main_thread 0x1000000
crash:   guest lr 0x0 r1 0xd0032d30 r2 0x0 r3 0x0 r4 0x0 r5 0xd0033360
crash:   guest regs: r0=0x0 r1=0xd0032d30 r2=0x0 r3=0x0 r4=0x0 r5=0xd0033360 r6=0xd0033370
         r7=0xd0033380 r8=0xd0033390 r9=0x0 r10=0xd0033310 r11=0xd0033290 r12=0x24000048
         r13=0x10007060 r19=0x1 r20=0xd0033480 r22=0x165d580 r25=0x10000464 r26=0xd0033180
         r27=0x1
```

Guest thread: **main_thread, id 0x1000000**. Guest stack range
`0xd0010000..0xd0050000` (main_thread's, spp 75735-88040). `r1 = 0xd0032d30` is
inside that range; `r13 = 0x10007060` is the TLS thread pointer.

Frame list decoded with `nm` (every frame is lifted guest code):

```
#0  0x17aa2b6  libfiber  f_360002a0_body+0xe6        <-- FAULT
#1  0x17ac22c  libfiber  f_36000748_body+0x57c   (= cellFiberPpuContextEnterScheduler, 0x36000884)
#2  0x17bc0fc  libfiber  f_360033dc_body+0x1bc   (= cellFiberPpuYield, 0x3600345c)
#3            ppu::call_indirect
#4            ppu::call_import_inner+0x85d
#5            ppu::call_import
#6            rdr      f_0053338c+0x82            (game's cellFiberPpuYield wrapper, import 334)
#7  0x815f599  rdr      f_00caf26c_body  rdr_129.cpp:7327
#8  0x75b8156  rdr      f_00b57c10_body  rdr_115.cpp:17907
#9  0x75ba3b4  rdr      f_00b58368_body  rdr_115.cpp:18374
#10 0x75ba6f9  rdr      f_00b58430_body  rdr_115.cpp:18411
#11           ppu::call_indirect
#12 0x17b3eb3  libfiber f_36001b04_body+0xf3   (= cellFiberPpuExit path, 0x36001a30+0xd3)
#13           ppu::call_indirect
#14 0x17aa182  libfiber f_3600026c_body+0xb2
#15 0x17ac515  libfiber f_360008a0_body+0x235
#16 0x17b0d23  libfiber f_360012a0_body+0x583   (fiber trampoline)
#17           ppu::call_indirect
#18 0x17aa290  libfiber f_360002a0_body+0xc0
#19 0x17ad1cc  libfiber f_3600093c_body+0xb6c
#20 0x17b7955  libfiber f_3600254c_body+0x7b5   (= cellFiberPpuRunFibers)
#21           ppu::call_indirect
#22 0x1682b9d  ppu::call_import_inner
#23           ppu::call_import
```

So the whole fault stack is ONE library: Sony's **libfiber**, lifted (LLE).
Not a single frame is an HLE handler. The libfiber load line:

```
I 1.164066 prx: load '/dev_flash/sys/external/libfiber.sprx' -> 0x23026500 (LLE, start 0x3600ea68)
```
and `lle_inventory.py` on the same table: `cellFiber imported 8, LLE 8, HLE 0,
NONE 0`; `grep cellFiber runtime/src/hle/` is empty.

### M7 -- the faulting instruction, in PPC and in lifted form

`fw/4.91/reloc/libfiber.elf` guest 0x360002a0 (capstone, BE PPC64):

```
360002a0  mr     r8,r1            ; frame base
360002a4  subf   r9,r1,r5         ; delta  (r5 = new stack top)
360002a8  stdux  r1,r1,r9         ; *(r5) = old r1   <- back-chain store
360002ac  mflr   r0
360002b0  std    r0,0x10(r8)      ; LR into the CALLER frame
360002b4  std    r2,0x28(r1)      ; TOC
360002b8  clrldi r7,r3,32
360002bc  lwz    r0,0(r7)         ; descriptor: entry
360002c0  mtctr  r0
360002c4  lwz    r2,4(r7)         ; descriptor: TOC
360002c8  mr     r3,r4
360002cc  bctrl                   ; <- enter the fiber
360002d0  ld     r2,0x28(r1)      ; TOC back      (lifted line 230) -- read 0
360002d4  ld     r9,0(r1)         ; back-chain    (lifted line 231) -- read 0
360002d8  ld     r0,0x10(r9)      ; LR from the caller frame  <-- FAULT ea=0x10
360002dc  mtlr   r0
360002e0  mr     r1,r9
360002e4  blr
```

`objdump` on our host binary agrees instruction for instruction
(`17aa2b6: mov (%r14,%rcx,1),%rcx` with ecx = 0 + 0x10), and the lifted source is
`out-rdr/ppu/libfiber_000.cpp:216-236`, fault at line 232.

**The two reloads are the proof of mechanism**: line 230 reads `[r1+0x28]` (the TOC
this same thunk stored at 360002b4) and gets 0, and line 231 reads `[r1]` (the
back-chain it stored at 360002a8) and gets 0. Both words the thunk wrote into its
own frame are absent at the read. A store that vanished into its own frame means
**r1 is not the frame the thunk built when it returns from `bctrl`: the callee
switched the guest stack and did not restore r1.** `r2 = 0x0` in the crash
registers is the same fact measured twice.

Every one of the four available crash logs (39.4 s, 42.9 s, 78.1 s, 163.7 s) has
the identical register file and the identical 24 frames: `r1=0xd0032d30,
r2=0, r9=0, guest 0x10`, `exe+0x17aa2b6`.

### M8 -- `*NO NAME*`: not in the game. It is libfiber's default fiber name

Byte search (grep -a misses these because of the NULs):

```
$ python3: find b'*NO NAME*' in bin/rdr/EBOOT.elf bin/gta/EBOOT.elf bin/mc/mc.elf \
           bin/01.12/GOWA.elf fw/4.91/*.sprx /root/Dev/devflash-out (whole tree)
FULL /root/Dev/ps3base2/fw/4.91/libfiber.prx
FULL /root/Dev/ps3base2/fw/4.91/libspurs_jq.prx
FULL /root/Dev/ps3base2/fw/4.91/libsre.prx
FULL .../reloc-rdr/libsre.elf  .../reloc-rdr/libspurs_jq.elf
FULL .../reloc/libfiber.elf    .../reloc/libsre.elf
0    bin/rdr/EBOOT.elf        <-- the game ELF does NOT contain it
0    bin/gta/EBOOT.elf  bin/mc/mc.elf  bin/01.12/GOWA.elf
```

In the very module that crashes, libfiber, the string sits in .rodata:

```
0x36006408  'libfiber'
0x36006418  'noOrderingLock.c'
0x36006450  'fiber_ppu_context.c'
0x36006478  '*NO NAME*'          <- also copied to 0x360064b8
0x36006498  'fiber_ppu.c'
0x360064e0  'trace.c'
```

The r6/r7/r8 dump in the crash report (`*NO NAME*` at r6+0x3c and r7+0x2c) is the
**fiber-name field of a libfiber ppu-context struct** (`_sys_strncpy(r3,
0x360064b8, 0x1f)` at lr 0x36000fe8 is literally the copy of that literal), not a
save-data name. It names the subsystem unambiguously: **libfiber / cellFiber**,
Sony's PPU fiber (cooperative user-level thread) library.

### M9 -- what the guest was doing: the last 30 imports of main_thread

Measured with `GOWA_EVTRACE=import` (binary event trace, `tools/trace.py` record
format) on a run that reproduced the crash at 163.72 s:

```
 163.720108  cellSpurs::cellSpursCreateTask        (0x704cde80,0xd004f200,0x1211780) -> 0x0        lr=0x0068fdcc
 163.720110  cellSpurs::cellSpursCreateTask        (0x704cde80,0xd004f200,0x1211780) -> 0x0        lr=0x0068fdcc
 163.720283  sysPrxForUser::_sys_memcpy            (0xd004f1f0,0x704d0970,0x10)      -> 0xd004f1f0 lr=0x32016f10
 163.720283  cellSpurs::cellSpursQueuePopBody      (0x70401b00,0xd004f1f0,0x0)        -> 0x0        lr=0x0068fe14
 163.720285  sysPrxForUser::_sys_memcpy            (0xd004f1f0,0x704d0980,0x10)      -> 0xd004f1f0 lr=0x32016f10
 163.720285  cellSpurs::cellSpursQueuePopBody      (0x70401b00,0xd004f1f0,0x0)        -> 0x0        lr=0x0068fe14
 ... (the pair repeats 6x, queue body draining) ...
 163.720300  sysPrxForUser::sys_lwmutex_lock        (0x71847e38,0x0,0x1)             -> 0x0        lr=0x006a9a14
 163.720302  sysPrxForUser::sys_lwmutex_unlock      (0x71847e38,...)                  -> 0x0        lr=0x006a9c74
 163.720306  sysPrxForUser::sys_lwmutex_lock        (0x175cd70,0x0,0x72b7b480)        -> 0x0        lr=0x00c92bc0
 163.720308  sysPrxForUser::sys_lwmutex_unlock      (0x175cd70,...)                   -> 0x0        lr=0x00c92c1c
 163.720311  sysPrxForUser::sys_lwmutex_lock        (0x71847e38,0x0,0x1)             -> 0x0        lr=0x006a9a14
 163.720312  sysPrxForUser::sys_lwmutex_unlock      (0x71847e38,...)                  -> 0x0        lr=0x006a9c74
 163.720647  sysPrxForUser::sys_lwmutex_lock        (0x1762dc0,0x0,0x32e20)           -> 0x0        lr=0x0078c29c
 163.720648  sysPrxForUser::sys_lwmutex_unlock      (0x1762dc0,0x0,0x32e20)           -> 0x0        lr=0x0078c360
 163.720657  sysPrxForUser::sys_lwmutex_lock        (0x16451b0,0x0,0x12ff600)         -> 0x0        lr=0x0052b7b0
 163.720659  sysPrxForUser::sys_lwmutex_unlock      (0x16451b0,0x16451b0,0x1640000)   -> 0x0        lr=0x0052b7c4
 163.720662  sysPrxForUser::sys_lwmutex_lock        (0x16451b0,0x0,0x16451b0)         -> 0x0        lr=0x0052bf18
 163.720664  cellSpurs::cellSpursRunJobChain       (0x1648b00,0x1,0x1648b00)         -> 0x0        lr=0x0052bc50
 163.720665  sysPrxForUser::sys_lwmutex_unlock      (0x16451b0,0x2,0x1648b00)         -> 0x0        lr=0x0052bf2c
 163.720677  sysPrxForUser::_sys_memset             (0xd00251b0,0x0,0x100)            -> 0xd00251b0 lr=0x36001c60
 163.720681  sysPrxForUser::_sys_strncpy            (0xd00251c0,0x360064b8,0x1f)      -> 0xd00251c0 lr=0x36001c94
 163.720681  cellFiber::_cellFiberPpuAttributeInitialize (0xd00251b0,0x300000,0xd0033480) -> 0x0   lr=0x005332f4
 163.720688  sysPrxForUser::_sys_strncpy            (0xd00333a8,0xd00251c0,0x1f)      -> 0xd00333a8 lr=0x36000fe8
 163.720689  cellFiber::cellFiberPpuCreateFiber    (0x165d580,0xd0033100,0x1385418)  -> 0x0        lr=0x0053334c
 163.722868  <CRASH>
```

The last two guest words before the crash: `_sys_memset(r3,0,0x100)` +
`_sys_strncpy(r3, 0x360064b8, 0x1f)` (that is libfiber copying the literal
`*NO NAME*`) + `cellFiberPpuCreateFiber` — then libfiber's own scheduler entry
crashes 2 ms later.

### M10 -- the fiber library is already failing before the crash, 426077 times

`cellFiberPpuYield` return values over the whole trace:

```
$ python3 (count rec.d of every cellFiber::cellFiberPpuYield import)
cellFiberPpuYield total calls 426077
   return 0x80760009  426077        <-- 100%
```

`0x80760009` is `CELL_FIBER_ERROR_PERM` (refs/dod3-recomp/ps3recomp/libs/spurs/
cellFiber.h:32, module 0x8076; RPCS3's cellFiber returns exactly this when there
is no current fiber). The game does not check it and spins:
`out-rdr/ppu/rdr_052.cpp:18552` `f_0053338c` calls import 334
(`cellFiberPpuYield`) then `cntlzw` on the result, and its caller at 0x52c9a8
(`L_52c9a8: ctx.lr = 0x52c9ac; f_0053338c(...)`) loops.

The setup the game performed, in order (same trace):

```
   1.164224  _cellFiberPpuInitialize                    (0x10000464, 0x0, -1) -> 0xffffffffffff9404
   1.164244  _cellFiberPpuSchedulerAttributeInitialize   (0xd004f950, 0x300000, -1) -> 0
   1.164262  cellFiberPpuInitializeScheduler            (0x165d580, 0xd004f950, 0xd004fa50) -> 0
  61.367575  cellFiberPpuYield                          (0x0, 0x1e, 0x1550000) -> 0x80760009   <-- first yield
 163.720681  _cellFiberPpuAttributeInitialize           (0xd00251b0, 0x300000, 0xd0033480) -> 0
 163.720689  cellFiberPpuCreateFiber                    (0x165d580, 0xd0033100, 0x1385418) -> 0
```

`cellFiberPpuYield` takes NO arguments in the SDK; the `(0x0,0x1e,...)` shown is
the caller's stale r3/r4, which is consistent with the error: the LLE libfiber
has no current fiber, so `Yield` returns PERM immediately.

### M11 -- the site is NOT one of our handlers; what the save subsystem really did

The task's first suspect was `cellSaveData*`, because the game had just shown the
save prompt. The crash stack contains **no** `cellSaveData` frame, and
`ppu::call_import` in the chain (#5 and #23) resolves to `cellFiberPpuYield`, not
to a save-data import:

```
out-rdr/ppu/table.cpp:77181  index 334 = {"cellFiber", 0x0C44F441, "cellFiberPpuYield"}
```

The save subsystem ran correctly and *finished* before the crash (run
`/tmp/p1-crash.log`):

```
   0.768  savedata: load 'BLES00680RDR2OPTIONS_SAV' (existing) ... progress 100
   1.769  savedata: load 'BLES00680RDR2MPSAVE_SAV'  (existing) ... progress 100
   2.770  savedata: load 'BLES00680RDR2SAVE0_SAV'   (existing) ... progress 100
  61.699  savedata: ListAutoLoad: 3 savedata match prefix 'BLES00680' ...
  61.700  savedata: GetListItem 'BLES00680RDR2SAVE0_SAV' -> title 'Red Dead Redemption' size 188 kB
  61.700  savedata: ListAutoLoad: funcFixed returned 1 with 3 savedata
 160.907  savedata: read BLES00680RDR2SAVE0_SAV/RAGE.SAV size 0x124c7 -> 0x124c7
 163.720  <fiber created>
 163.722  <SIGSEGV>
```

So there is no handler argument to report and nothing to compare with RPCS3's
`cellSaveData` header: the save calls completed with 3 saves listed and a
0x124c7-byte RAGE.SAV read. The 3 s gap between that read and the crash is the
game parsing the save **and then starting its script/system fibers** —
`cellSpursCreateTask` + `cellFiberPpuCreateFiber` at 163.72 s.

### M12 -- the conclusion

1. The faulting site `exe+0x17aa2b6` is **not our code**: it is
   `ppu::ld64` inlined into **libfiber's lifted `f_360002a0_body`**
   (guest 0x360002a0), the epilogue `ld r0,0x10(r9)` after the thunk's `bctrl`.
   Every frame 0..23 is lifted guest code; libfiber is loaded LLE (8/8 exports,
   0 HLE).
2. The guest thread is **main_thread 0x1000000**, guest lr 0x0, r1 0xd0032d30
   (inside main_thread's own 0xd0010000..0xd0050000 stack), fault guest **0x10**.
   The thunk's own frame words — `[r1+0x28]` (TOC) and `[r1]` (back-chain) — both
   read back as 0, so the frame it built was **replaced while the callee ran**:
   a fiber stack switch that did not restore r1.
3. `*NO NAME*` is **libfiber's default fiber name**, at guest 0x36006478/0x360064b8
   in `fw/4.91/libfiber.prx` (also in libsre and libspurs_jq). The game's EBOOT.elf
   does not contain it. Subsystem named: **cellFiber (libfiber)**.
4. The subsystem was already broken before the fault: **all 426077**
   `cellFiberPpuYield` calls in a 164 s run returned `CELL_FIBER_ERROR_PERM`
   (0x80760009), i.e. libfiber's scheduler never had a current fiber, and the game
   was spinning on the error. `cellFiberPpuCreateFiber` (0.7 ms before the crash)
   is the first fiber the title creates through its own scheduler.

### M13 -- the stack switch, measured: r1 is not the frame the thunk built

Disassembly of libfiber shows exactly ONE instruction in the whole library that
loads r1 (i.e. restores a stack pointer):

```
$ capstone over the whole libfiber text (0x36000000..0x36006558)
=== loads of r1:
  36000130  ld   r1, 0(r3)          <-- the fiber context switch
=== stores of r1:
  36000010  std  r1, 0(r3)          <-- save current stack into a context
  36000278  stdu r1, -0x70(r1)
```

`cellFiberPpuContextEnterScheduler` (0x36000748) enters the fiber at 0x36000840:

```
36000850  lwz  r3, -0x7fe4(r2)   ; r2 = 0x36016a80 -> 0x3600ea9c -> 0x3600e860 (OPD)
3600085c  lwz  r5, 0xc(r25)      ; r25 = 0x10000464 (fiber lib ctx)
36000860  addi r9, r5, -0x20     ; r9 = [0x10000470] - 0x20
36000864  addi r5, r5, -0x90     ; r5 = [0x10000470] - 0x90   <- NEW stack top
36000870  mr   r4, r9
36000874  std  r28, 0(r9)
36000878  std  r27, 8(r9)
36000880  bl   0x360002a0        ; the thunk, which does stdux r1,r1,r9
```

and the OPD resolves to the fiber trampoline:

```
0x3600e860 -> code 0x360012a0, toc 0x36016a80
```

`[0x10000470]` read out of the crash memory dump (blackbox/memory_0.bin) is
**0xd0024e90**, so the thunk switched r1 to **0xd0024e00**.

The register file at the fault says **r1 = 0xd0032d30**: neither the value before
the switch (0xd0032d30 is where the *previous* frame was) nor the switch target
0xd0024e00. r1 has been restored a further time inside the fiber, by the one
`ld r1,0(r3)` at 0x36000130. That is the fiber scheduler switching stacks while
the thunk is suspended in `bctrl`, and it is exactly why the thunk's two
self-written frame words read 0 on the way back.

Corroborating measurement from the runtime's own stack tracker:

```
stacks: 'main_thread' id 16777216 range 0xd0010000..0xd0050000
        r1_min 0xd0046460 margin 222304 bytes
crash:  r1 0xd0032d30
        r1_min - r1 = 0x13730 = 79664 bytes
```

main_thread's deepest legitimate r1 is 0xd0046460; the fault runs 79664 bytes
below it, on a stack main_thread itself never descended to.

