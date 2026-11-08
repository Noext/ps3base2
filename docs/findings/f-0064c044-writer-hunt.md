# f_0064c044: the writer hunt -- corrected again, the "two threads" reading was wrong

Status: **a fourth retraction.** The previous version of this document claimed
"two save-path threads write the same node field, no mutual exclusion". That is
**false**, and it was refuted from my own log by piste 42 in one grep. The
measurement that survives is narrower: one thread oscillates a field inside a
lock it already holds, the other thread touches it five times, and the crash is
in a *different* function on a *different* node. What follows separates what was
measured from what was guessed.

## Antecedents (grep over refs/, -R)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| A page-guard watchpoint exists to catch stores that `vm_write*` cannot see, logging the faulting RIP | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **yes** -- the shape of `GOWA_TRAP`, and it is what produced the raw data below |
| Value-CAS `stwcx` is ABA-vulnerable; stale commits corrupt through reused blocks | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **no** -- reservation is exact at 2 and 4 threads (measured earlier) |
| A re-entrancy guard on the fault handler is mandatory | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | yes, noted |
| A deadlock/corruption report traced to a lock held across a call into a callback | `refs/rpcs3/**` | no match |

## Retraction 4: there are not two writers

The previous text said the 43 writes of `0x378c9c7c+4` alternated "from two
distinct call sites, two threads". Both halves are wrong:

```
$ grep 'line-write 0x378c9c7c+4' /tmp/run-world-33/game.log | grep -oE '\[[0-9]+\]' | sort | uniq -c
     38 [1054653]        # (4J) Server
      5 [1059742]        # (4J) Save thread 0
```

**38 of the 43 writes are one thread.** The alternation I reported is *within*
that thread, between two return addresses -- not between two writers. Piste 42
found this; the grep is trivial and I should have run it before writing the
claim. The 5 Save-thread writes are a single burst at t=329.469.

## Retraction 5: the two "call sites" are lock returns, not stores

`0x5eb2c0` and `0x5ea480` are not store sites. Disassembling `bin/mc/mc.elf` at
both:

```
005eb2bc  bl    0x5e9b10      ; f_005e9b10 -> call_import(ctx, base, 326)
005eb2c0  cmpwi cr7, r3, 0   <<< the "lr" I reported
```
```
005ea47c  bl    0x5e9b10
005ea480  cmpwi cr7, r3, 0   <<< the "lr" I reported
```

Both are the instruction *after* the call, and `f_005e9b10` is
`sys_lwmutex_lock` (import 326; 327 is `sys_lwmutex_unlock`). Scanning forward
from `0x5eb2c0` to the next `bl` finds **no store at all**. So the `lr` my
line-watch printed is the return address of the lock acquisition, which is what
the thread's `ctx.lr` naturally holds at that moment -- not the store that
triggered the watch.

This also explains the oscillation shape. `f_005eb260` takes the lock, checks
whether the block is already in use, and on the failure path sets `r31 = 0`,
releases at `0x5eb328`, and returns -- **the acquire/release pair runs in a retry
loop**, and each iteration re-reads and re-writes the same header word. 0x31 and
0x33 are not data; they are the allocation bookkeeping of that retry.

## What is actually established

1. The field `0x378c9c7c+4` is written **only** by the save path, inside
   `sys_lwmutex_lock`-protected code, by one thread, in a retry loop.
2. The crash is **not** at that field. Run 9 crashed at
   `f_0064c2f8_body` (mc_063.cpp:23242) via `f_0064be6c_body` (mc_063.cpp:22978),
   fault guest `0x0` -- a null dereference, in `(4J) Server`, at t=331.99, two and
   a half seconds after the save burst.
3. A fresh dump from that run (`/root/Dev/ps3base2-wt33/blackbox/memory_0.bin`,
   503 MB, taken at crash time) shows the watched word holding `0x0068006f`
   (UTF-16 `"ho"`) followed by `0x006c0064` (`"ld"`) -- **the region is a UTF-16
   string**, not a list node. The old dump's node `0x359c1d50` holds
   `0x314142b8` in this run, entirely different data.

So the earlier narrative -- "a list node's +28 holds a code word, find the stray
store" -- is not supported by any live measurement. It was an artefact of reading
one dump without a control. **The +28 field was read from a memory image whose
contents at that address are a string.**

## The earlier findings that still stand

* **The guard had never run**: `hook_impl` was assigned while `g_hooks` was empty,
  so any address hook was inert without `PS3BASE_GAME_HOOKS`. Fixed; the log now
  prints `hook_impl=set flag=1`.
* **The guard read `r30` at entry**, before `f_0064c044_body` assigns it, so it
  inspected the caller's stack frame -- hence "code words" that were UTF-16
  (`0x002e0030` = `".0"`). Now derived from `r3`.
* **"A word from .text in the heap" is not a signature**: 1 637 146 aligned words
  in the text range; 461 711 after removing UTF-16; 68 after removing words near
  the previous function entry -- and those 68 are identical across all seven
  dumps. `0x00730100` at `0x3005087c` is the bytes `00 73 01 00` inside the NBT
  string `"TileZ"`.
* **`memory_map` did not link on main** (calls `vm::write32` without
  `reservation.cpp`). Suite is 9/9.

## Tooling delivered (unchanged, and it is what made these corrections possible)

`runtime/include/runtime/codewatch.h` + `runtime/src/codewatch.cpp`, hooked into
`rsv::store_range`, `store_locked`, `store_unconditional`, `merge_bits`, the SPU
direct put and `ppu::notify_store` -- together every lifted `ppu::st*`, every
`vm::write*` and every SPU MFC put. `cw::note_line` arms one 128-byte line via a
512 KB bitmap over the whole 4 GB guest space; the store-path test is one array
read, and each hit logs `prev -> value`, guest lr and thread. Off unless
`PS3BASE_CODEWATCH`.

The **`prev -> value` field is what made this retraction possible**: without it,
38 identical-looking writes would have hidden the retry loop. The **guest lr
being a lock return** is the tool's limitation to document: `ctx.lr` at a store
is the *current* call's return address, not the store's own location, so a
watch hit must be read together with a disassembly of `lr`.

## Next step: stop chasing the field

The live evidence no longer supports "the writer of a corrupted list node". It
supports a **null dereference in `(4J) Server` after a save**, at
`f_0064c2f8` / `f_0064be6c`. The productive next question is what
`f_0064c2f8_body` dereferences at mc_063.cpp:23242 and which field of the object
is zero there -- and whether that object is the one the save path just rewrote.
That is checkable with the watch already in place, by logging the faulting
object rather than the whole line.

A caution for whoever picks this up: three successive narratives about this
crash ("reservation is broken", "a code word was stored into a node", "two threads
race on the field") all died on measurement. Treat any story that comes from a
post-mortem dump alone as a hypothesis, not a finding.
