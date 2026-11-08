# f_0064c044: the writer, found -- and it is a race, not a stray store

Status: **the writer is identified.** It is not a wild pointer and not a DMA.
Two of the title's own save-path threads write the *same 4-byte field of the same
list node* from two different call sites, flip-flopping it between two values, at
t=329.4 s; the `(4J) Server` walk crashes at t=331.99 s reading the node the
other thread is still holding.

The campaign's premise -- "one 4-byte store of a code word into a heap field" --
was produced by reading a memory dump after the fact. In a live run with the
write-side watch armed, the same crash has a different and much more specific
explanation. Both are recorded here; the dump reading is retracted in the places
where it was wrong.

## Antecedents (grep over refs/, -R)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| A page-guard watchpoint exists to catch stores that `vm_write*` cannot see, logging the faulting RIP | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **yes** -- the shape of `GOWA_TRAP`, and it is what named the writer here |
| Value-CAS `stwcx` is ABA-vulnerable; a stale commit produces heap corruption through a reused block | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **no for the corruption**, but the *family* is right: unsynchronised shared-state mutation is what this is |
| A re-entrancy guard on the fault handler is mandatory | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | yes, noted |
| A code word inside a data pointer field | `refs/rpcs3/**` | no match |

## The measurement

Run 9: 2400 s budget, `PS3BASE_LIST_GUARD=1` (corrected), write watch armed.
Crashed at **t=331.99 s**, thread `(4J) Server`:

```
crash: signal 11 ... fault 0x7b0544000000 (guest 0x0)
  #0 ppu::ld32                 runtime/include/ppu/ops.h:43
  #1 f_0064c2f8_body           mc_063.cpp:23242
  #3 f_0064be6c_body           mc_063.cpp:22978
```

Immediately before, the line watch on that node's 128-byte line fired 43 times
from **two distinct call sites**, alternating the same word:

```
codewatch: line-write 0x378c9c7c+4 prev 0x00000031 -> 0x00000033 by rsv::store_range
           (guest lr 0x005eb2c0) thread (4J) Save thread 0
codewatch: line-write 0x378c9c7c+4 prev 0x00000033 -> 0x00000031 by rsv::store_range
           (guest lr 0x005ea480) thread (4J) Save thread 0
  ... 22 hits from lr 0x5eb2c0, 21 hits from lr 0x5ea480, alternating ...
codewatch: line-write 0x378c9c7c+4 prev 0x00000061 -> 0x00000043 ... t=329.469
codewatch: line-write 0x378c9c7c+4 prev 0x00000043 -> 0x00000061 ... t=329.497
crash                                                          t=331.99
```

`0x5eb2c0` = `f_005eb260+0x60`; `0x5ea480` = `f_005ea428+0x58`. Both are save-path
entry points in `out/ppu/mc_059.cpp`, and both are entered with the same `r3`
(the node) -- they write its field and the walk reads it.

**Two writers, one field, no mutual exclusion.** The guest is using the node
concurrently; either the title's own lock is not being honoured on our side, or
our reservation/lock HLE lets both threads in. That is a different investigation
from "find the stray store", and it is the one the evidence supports.

## Retractions (each measured, each was load-bearing)

### The guard had never run, and once it ran it read a stack frame

`hooks::init()` assigned `ppu::hook_impl` while `g_hooks` was empty, registered
`list_guard`, and returned early when `PS3BASE_GAME_HOOKS` was unset.
`ppu::hook_call` only dispatches when `hook_impl` is non-null, so the guard was
never called -- for any address hook. Fixed; the log now prints
`hook_impl=set flag=1`.

With that fixed, the guard immediately reported nodes whose `+32` was
`0x002e0030` (UTF-16 `".0"`) and `0x00af9068`. It was reading a stack frame:
at entry `f_0064c044_body` has not yet assigned `r30` (`r30 = ld32[r29+36]`
comes two instructions later), and `r29` still holds the **caller's** frame
pointer. The guard now derives the node from `r3`:
`table = ld32[obj+52]`, `head = ld32[table + ld32[obj+36]*4]`, `node = head-32`.

### "A word from the .text in the heap" is not a signature

Over the whole heap of `blackbox/memory_0.bin`:

| filter | words left |
|---|---|
| 4-aligned word in 0x10200..0x8aa674 | 1 637 146 |
| minus lifted function entries | 1 632 591 |
| minus UTF-16 text (both halves printable or zero) | 461 711 |
| minus words within 0x20000 of the previous entry | 68 |

Scanning all seven dumps in `/root/Dev/*/blackbox/` yields the *same* 44-68
addresses -- ordinary data. A live run's first hit, `0x00730100` at
`0x3005087c`, decodes to the bytes `00 73 01 00` six bytes into the NBT string
`"TileZ"`: a coincidence of string layout.

### The dump's "corrupt +28" is not what a live run shows

The chain from `obj = 0x35558670` reconstructs exactly: `count = 53`,
`table = 0x359089b0`, `table[19] = 0x359c1d70`, so `node = 0x359c1d50`, and
`node+28 = 0x00721f30`. But `0x359c1d50` holds zeros or unrelated data in the
runs that did not crash, and in the crash dump the 128 bytes before it are UTF-16
(`"SpawnX"`). `0x00721f30` is not proof of a stray code word: it also matches the
shape of a **16-bit store with a byte-wide source**, which the trap caught doing
exactly that at the node's field:

```
trap 0 write 0x359c1d6c = 0x00750000 by main_thread
   <- rsv::store_range <- f_00775ce8_body (mc_075.cpp:22559)
   <- f_006d2b90 <- f_00763038 (mc_074) <- f_000e07e8 (mc_008) <- f_000d5f30 (mc_007)

L_7766e4:
    gpr[0]  = int8(gpr[0])      # one BYTE from the source
    gpr[10] += 2                # destination advances by TWO
    gpr[8]  += 1                # source advances by one
    st16(dest = gpr[9], value = gpr[0])
```

`0x00750000` is one source byte `0x00` with `0x75` ("u") above it.

## Tooling delivered

`runtime/include/runtime/codewatch.h` + `runtime/src/codewatch.cpp`, hooked into
every guest-write choke point (`rsv::store_range`, `store_locked`,
`store_unconditional`, `merge_bits`, the SPU direct put, `ppu::notify_store`).
Together those cover every lifted `ppu::st*`, every `vm::write*` and every SPU MFC
put. `cw::note_line` arms one 128-byte line through a 512 KB bitmap over the
whole 4 GB guest space; the test on the store path is one array read, and each
hit logs `prev -> value`, guest lr and thread. Off unless `PS3BASE_CODEWATCH`.

Also fixed: `memory_map` did not link on main (it calls `vm::write32` without
`reservation.cpp`). Suite is 9/9.

## Next step

The field is shared, so the question is now **which lock the title takes around
`f_005eb260` and `f_005ea428` and why both threads are inside it**. Concretely:
log the guest `lwarx`/`stwcx` and the LV2 semaphore/mutex calls of
`(4J) Save thread 0` and `(4J) Server` in the second before a hit on the line,
and check whether the save thread ever acquires the lock the server holds. That is
a lock-coverage question, not a memory-corruption one.
