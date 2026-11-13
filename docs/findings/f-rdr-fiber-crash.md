# The RDR SIGSEGV is a fiber stack switch, not a handler and not save data

Measured 2026-11-13 on main `01c5e16`, all four available crash logs
(`/tmp/notimeout.log` 39.4 s, `/tmp/seg.log` 42.9 s, `/tmp/real-flash.log` 78.1 s,
a fresh traced run 163.7 s). Every one has the identical register file and the
identical 24 frames, so this is one bug, not a family.

## The site is lifted libfiber, not our code

```
$ addr2line -f -C -e build-rdr/ps3base 0x17aa2b6
ppu::ld64(unsigned char*, unsigned int)
  build-rdr/../runtime/include/ppu/ops.h:63
```

`nm` gives the inlining host: `f_360002a0_body+0xe6`, from
`out-rdr/ppu/libfiber_000.cpp` (guest 0x360002a0). Every frame 0..23 is lifted
guest code in **libfiber**; libfiber is loaded LLE --
`prx: load '/dev_flash/sys/external/libfiber.sprx' -> 0x23026500 (LLE)`, and
`tools/lle_inventory.py --table out-rdr/ppu/table.cpp --fw fw/4.91 cellFiber`
reports `imported 8, LLE 8, HLE 0, NONE 0`. `grep cellFiber runtime/src/hle/`
is empty: **there is no cellFiber handler to blame and no save-data frame in the
chain.** `ppu::call_import` at frames #5/#23 resolves to import 334 =
`cellFiber::cellFiberPpuYield` (`out-rdr/ppu/table.cpp:77181`).

## The faulting instruction

```
360002a0  mr     r8,r1
360002a4  subf   r9,r1,r5
360002a8  stdux  r1,r1,r9      ; *(new_top) = old r1   -- back-chain
360002ac  mflr   r0
360002b0  std    r0,0x10(r8)   ; LR into the caller frame
360002b4  std    r2,0x28(r1)   ; TOC
360002b8  clrldi r7,r3,32
360002bc  lwz    r0,0(r7)      ; descriptor entry
360002c0  mtctr  r0
360002c4  lwz    r2,4(r7)      ; descriptor TOC
360002cc  bctrl                ; enter the fiber  <-- stack is switched here
360002d0  ld     r2,0x28(r1)   ; read back the TOC   -> 0
360002d4  ld     r9,0(r1)      ; read back the chain -> 0
360002d8  ld     r0,0x10(r9)   ; <-- FAULT, ea = 0 + 0x10
```

This is the ELFv1 function-pointer thunk around a fiber entry. The two reloads
are the evidence: both words the thunk wrote into its *own* frame at
0x360002b4/0x360002a8 read back as **0**, and r1 at the fault is
**0xd0032d30**, which is neither the stack the thunk established
(`[0x10000470]-0x90 = 0xd0024e00`, read out of `blackbox/memory_0.bin`) nor
main_thread's deepest legitimate frame (`r1_min 0xd0046460`, i.e. 79664 bytes
above the fault). The guest stack was moved while the thunk was suspended in
`bctrl`.

`ld r1,0(r3)` at 0x36000130 is the **only** instruction in all of libfiber that
loads r1 -- the fiber-context restore. A `cellFiberPpuYield` from inside a fiber
resumes on that path.

## `*NO NAME*` names the subsystem, and it is not the game

Byte search (grep -a misses it because of the NULs) over `bin/rdr/EBOOT.elf`,
`bin/gta/EBOOT.elf`, `bin/mc/mc.elf`, `bin/01.12/GOWA.elf`, `fw/4.91/*.sprx` and
the whole `/root/Dev/devflash-out` tree:

```
FULL  fw/4.91/libfiber.prx        fw/4.91/reloc/libfiber.elf
FULL  fw/4.91/libsre.prx          fw/4.91/reloc/libsre.elf
FULL  fw/4.91/libspurs_jq.prx     fw/4.91/reloc-rdr/libspurs_jq.elf
0     bin/rdr/EBOOT.elf           <-- the game ELF does not contain it
```

In libfiber it is .rodata next to the library's own file names:

```
0x36006408  libfiber
0x36006450  fiber_ppu_context.c
0x36006478  *NO NAME*   (also copied to 0x360064b8)
0x36006498  fiber_ppu.c
```

The r6/r7/r8 words in the crash dump are a **libfiber ppu-context**, its name
field filled from that literal by `_sys_strncpy(r3, 0x360064b8, 0x1f)`
(lr 0x36000fe8) immediately before the crash.

## What main_thread was doing: the last imports

`GOWA_EVTRACE=import` on a run that reproduced the crash at 163.72 s:

```
163.720664  cellSpurs::cellSpursRunJobChain              (0x1648b00,0x1,...)     -> 0
163.720677  sysPrxForUser::_sys_memset                   (0xd00251b0,0x0,0x100)  -> 0xd00251b0
163.720681  sysPrxForUser::_sys_strncpy                  (0xd00251c0,0x360064b8,0x1f)
163.720681  cellFiber::_cellFiberPpuAttributeInitialize  (0xd00251b0,0x300000,0xd0033480) -> 0
163.720688  sysPrxForUser::_sys_strncpy                  (0xd00333a8,0xd00251c0,0x1f)
163.720689  cellFiber::cellFiberPpuCreateFiber           (0x165d580,0xd0033100,0x1385418) -> 0
163.722868  <CRASH>
```

The earlier setup, in order: `_cellFiberPpuInitialize(0x10000464,0,-1)` at
1.164 s, `_cellFiberPpuSchedulerAttributeInitialize`, then
`cellFiberPpuInitializeScheduler(0x165d580,0xd004f950,0xd004fa50)` -> 0.

## The fiber library was already failing, 426077 times

Every `cellFiberPpuYield` in a 164 s trace returned the same value:

```
cellFiberPpuYield total calls 426077
   return 0x80760009  426077
```

`0x80760009` = `CELL_FIBER_ERROR_PERM` (`refs/dod3-recomp/ps3recomp/libs/spurs/
cellFiber.h:32`), returned when there is no current fiber. The game does not
check it: `out-rdr/ppu/rdr_052.cpp:18552` `f_0053338c` calls import 334 and its
caller loops at `L_52c9a8`. libfiber's own scheduler was therefore never entered
until `cellFiberPpuCreateFiber` at 163.72 s, 2 ms before the fault.

## The save subsystem is not involved

It ran to completion and finished 3 s before the crash: 3 saves listed
(`ListAutoLoad: 3 savedata match prefix 'BLES00680'`, funcFixed returned 1), and
`read BLES00680RDR2SAVE0_SAV/RAGE.SAV size 0x124c7 -> 0x124c7` at 160.9 s. No
`cellSaveData` frame appears anywhere in the crash stack.

## What to look at next

Not a handler fix: the guest stack discipline across `cellFiberPpuYield`. The
question the evidence leaves is whether r1 is being restored to the right fiber
context (0x36000130 with r3 = `(fiber_ctx+0xf) & ~7`) or whether the scheduler
resumes a context that was never set up, given `Yield` returned PERM 426077
times beforehand.
