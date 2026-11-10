# Stack inventory and guard pages: what the hypothesis needed

## The inventory, measured on a live boot

18 guest threads. Requested sizes are honoured exactly, aligned to 4 KB with a
0x4000 floor -- the same rule as RPCS3 (`sys_ppu_thread.cpp:503`: aligned to
4096, 4096 by default).

| thread | requested | allocated | range |
|---|---|---|---|
| main_thread | 0x10000 | 0x10000 | 0xd0010000..0xd0020000 |
| MinecraftSpursHdlr1 | 0x8000 | 0x8000 | 0xd0030000..0xd0038000 |
| MinecraftSpursHdlr0 | 0x4000 | 0x4000 | 0xd0050000..0xd0054000 |
| _gcm_intr_thread | 0x4000 | 0x4000 | 0xd0070000..0xd0074000 |
| (4J) Tile update | 0x20000 | 0x20000 | 0xd00c0000..0xd00e0000 |
| (4J) Chunk update | 0x20000 | 0x20000 | 0xd0140000..0xd0160000 |

All guest stacks live in the `Stack` region at `0xd0000000`, which is a
**separate** reservation from `Main` (heap), `User64K`, `User1M`, `RsxContext`,
`Video` and `Spu`. The regions are disjoint by construction in
`memory.cpp:kRegions`, so a stack cannot statically overlap the heap, sys_memory,
sys_mmapper or sys_vm. **No overlap was found.**

## A real bug the guards exposed in my own code

The first version reserved the guard with a separate `vm::alloc`, and measured

    stack d0020000..d0028000    guard d0030000..d0040000

0x8000 of nothing in between: `vm::alloc` skips past the previous block, so the
guard landed ABOVE the stack and an overflow would have walked through the hole
without touching it. The guard is now part of **one contiguous reservation**, cut
off before being protected, and every entry logs `(adjacent)` or
`(NOT ADJACENT -- bug)`. Re-measured: 18 threads, **0 non-adjacent**.

## What contains 0x0048f640

**`f_0048f628`** (`out/ppu/mc_046.cpp:1457`), 24 bytes past its entry. The
instruction is not a vtable slot: the function loads a global at `+0`, loads
another at `0x490000-3600`, multiplies them, adds 11, stores the result back to
`gpr[3]+0`, and returns a value derived from `countl_zero`. A word from that body
landing in a heap node is what a **return address** looks like -- exactly the
shape a stack overflow writes.

## The guard and the status line

64 KB of `PROT_NONE` sits immediately below every guest stack, and
`status_dump` prints per thread: range, allocation, requested size, guard, the
lowest `r1` seen (sampled at every syscall entry) and the **margin** in bytes
between that lowest `r1` and the top of the guard. That margin is the number that
says whether a thread ever came close to overrunning.

## Result of the long session

Generation, save, and the scripted hour: see the run log. No guard fired. If no
guard fires and the crash returns, the next step is `rr` with software rendering,
`watch -l` on the corrupted word and `reverse-continue`.