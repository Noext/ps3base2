# f_0064c044: the list walk, and what the evidence actually supports

Status: mechanism established, root cause **not** proven. Two defects were found
and fixed on the way, one of them real and reproducible; neither is shown to
cause this crash. Written so the next session does not redo the dead ends.

## What the crash is

```
crash: signal 11 ... thread (4J) Server
  #0 ppu::ld32             (ops.h:43)
  #3 f_0064c044_body       (mc_063.cpp:23079)
  #5 f_004e2ac0_body       (mc_049.cpp:10084)
guest lr 0x64c0fc
```

Observed on 3 runs out of ~16 (t = 302 s, 519 s, 662 s), never on the others.
The reported fault address differs per run (0x907a000c, 0x912b0010,
0x4bff926d), which is what a wild pointer produces, not a fixed null.

`f_0064c044` walks a chain:

```
r30 = obj[+36]                    count/index
idx = r30*4 + obj[+52]            table of pointers
r30 = ld32[idx] - 32              first node
loop:
    f_004e2ac0(ld32[r30+28])      visit
    r30 = ld32[r30+32] - 32       next
    until r30 == 0
```

## What the memory dump settles

`blackbox/memory_0.bin` from the 302 s crash, walked with a small parser
(`/tmp/walk3.py` in that session; the format is repeated `{u32 addr, u32 len}`
+ bytes, little-endian headers):

- The chain from `obj = 0x35558670` is healthy for **29 nodes**; node 29's
  `+32` is 0, which is a clean end of list.
- Of the 53 table heads, exactly **one** has a `+28` outside the heap: the one
  the crash was on. Its `+28` was `0x00721f30` and `0x00000730100` in the
  other dump.
- Both of those values are inside the title's own text segment
  (`0x10000..0xba3688`, from `bin/mc/mc.elf`'s first LOAD). `0x721f30`
  disassembles to `stw r3, 0(r26)`; `0x730100` to `stw r9, 4(r11)`. These are
  **instruction words**, and `f_004e2ac0` then does `ld32[0x721f30]` ->
  `0x907a0000`, then `ld32[0x907a000c]` -> SIGSEGV, which matches the reported
  guest fault exactly.

So: one 4-byte field of one node was overwritten with a word from the code
stream. Every other field of that node and of its neighbours is intact.

## Hypothese refutede : la reservation PPU (lwarx/stwcx)

The obvious suspect was our reservation implementation, and it is **wrong**.

`tests/reservation_increment.cpp` runs the title's own lock-free retry loop
through the very `ppu::lwarx`/`ppu::stwcx` the lifted code calls:

```
1: lwarx  r3,0,r4 ; addi r3,r3,1 ; stwcx. r3,0,r4 ; bne- 1b
```

2 threads x 1e6, 3 rounds, then 4 threads x 1e6:

```
single thread: 1000000 successes, counter 1000000
round 0: 2000000 successful stwcx, counter 2000000
round 1: 2000000 successful stwcx, counter 2000000
round 2: 2000000 successful stwcx, counter 2000000
4 threads: 4000000 successes, counter 4000000
```

Every successful store replaced the value it loaded. `tests/guest_spinlock.cpp`
adds the canonical acquire loop (2 000 000 of 2 000 000) and a deterministic
invalidation check: a write between `lwarx` and `stwcx` makes the store fail,
with no write in between it succeeds. Both hold.

## Hypothese refutede : le lwmutex HLE (mais un vrai bug y a ete trouve)

`runtime/src/hle/sysPrxForUser.cpp` kept one `std::mutex` per guest lwmutex
address with no ownership. Two consequences, both reproduced deterministically
by `tests/lwmutex_hle.cpp` driving the shared header:

```
before:  destroy-while-held: second thread entered ...: YES
         foreign unlock returned 0x0 (refused: NO)
         non-owner unlock let another thread in: YES
after:   destroy-while-held: ... no
         foreign unlock returned 0xffffffff80010009 (refused: yes)
         non-owner unlock let another thread in: no
         mutual exclusion: counter 200000 of 200000
```

The fix (commit `d63574f`) tracks the owner in the entry, refuses a non-owner
unlock with `EPERM` instead of performing undefined behaviour on a
`std::mutex`, and marks a held mutex dead on destroy instead of erasing it (the
erase let the next lock build a fresh, unlocked mutex at the same address).

**It is not the cause of this crash.** With `PS3BASE_LWMUX_TRACE=1` on a full
run, the HLE entry point is never reached: the title uses the LV2 **syscalls**
95-99 instead. Measured syscall mix over a 200 s window:

```
4649 sys_semaphore_wait
1271 _sys_lwmutex_create
1155 _sys_lwmutex_destroy
  37 _sys_lwmutex_lock
  32 _sys_lwmutex_unlock
```

So the fix stands on its own (it is a correct-behaviour fix for a path another
title will take), but it cannot explain 0x64c044. Saying otherwise would be
inventing a result.

## Ce qui reste, et par ou continuer

The corruption is a single 4-byte store of a code word into a heap field. Two
candidate shapes, neither measured yet:

1. **A stale node reused after free.** The address `0x721f30` is a code
   address; a freed node whose memory was recycled by the CRT allocator can
   come back holding whatever the previous owner wrote. `docs/forcings.md`
   already records a separate server-side null-deref on the same thread.
2. **A DMA write out of range.** The SPU writes into main memory through
   `rsv::store_range` with an address the guest computed; a bad effective
   address is not checked against the object's bounds anywhere.

`PS3BASE_LIST_GUARD=1` (commit `c047f24`) now names the node and both field
values at the moment the walk is about to pass it, instead of dying two frames
later. Run with it set; the first `listguard` line is the node that matters,
and `PS3BASE_RSV_LINE=<node line>` will then name its writer.

## Reproduction

Not yet stable. Three long runs with no crash: 1500 s (pre-fix), 1700 s and
1242 s (post-fix), 0 `crash: signal` each. The crash correlates with runs that
reach the server's chunk-update loop and keep playing; a fixed-length run is not
enough to make it fire on demand, so treat any single no-crash run as
non-evidence.
