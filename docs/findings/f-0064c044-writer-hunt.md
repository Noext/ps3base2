# f_0064c044: the writer hunt -- what the eighth run settled

Status: **the writer is not identified, and the furthest progress is a
retraction.** Three claims in the earlier notes were wrong, and each was wrong in
a way that made the next step look impossible. This document records the
measurements that killed them, so the next session starts from the real picture
rather than from the flattering one.

## Antecedents (grep over refs/, -R)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| A page-guard watchpoint exists precisely to catch "raw memcpy/vector stores that `vm_write*` instrumentation can't see", logs the faulting RIP, single-steps and re-arms | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **yes** -- the shape of our `GOWA_TRAP`, and the reason a value test alone is not enough |
| That handler needs a re-entrancy guard because logging into a guarded page faults inside the handler forever | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | **yes** -- our trap logs from the SIGTRAP step, outside that trap |
| Value-CAS `stwcx` is ABA-vulnerable and causes heap corruption through reused blocks; the fix is full reservation semantics plus invalidation on any store | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **no** -- our reservation is exact at 2 and 4 threads (measured, previous session) |
| A code word inside a data pointer field | `refs/rpcs3/**` | **no match** |

## Retraction 1: the guard had never run, and when it did it read the wrong register

`hooks::init()` assigned `ppu::hook_impl` while `g_hooks` was empty, then
registered `list_guard` and (without `PS3BASE_GAME_HOOKS`) returned. `ppu::hook_call`
only dispatches when `hook_impl` is non-null, so the guard was installed, logged,
and never called. That is fixed, and the log now prints the wiring:

```
listguard: guard installed on f_0064c044: hook_impl=set flag=1
```

Once it ran, its output was nonsense:

```
listguard: node 0x355e5c10 [32]=0x002e0030 is a TEXT address, not a next pointer
listguard: node 0x364359a0 [32]=0x00af9068 is a TEXT address, not a next pointer
listguard: node 0x364359a4 [28]=0x00af9068 is a TEXT address, not an object
```

`0x002e0030` is UTF-16 for `".0"`; `0x00690000` is `"i"`. **The guard was
reading a stack frame.** At entry, `f_0064c044_body` has not yet assigned `r30`:

```
r29 = r3 ; r31 = r4 ; ld32[r29+40] ; SAVE r30 ; ld32[r29+52] ; r30 = ld32[r29+36]
```

so `ctx.gpr[30]` at entry is the caller's saved r30 and `ctx.gpr[29]` is the
caller's frame pointer (0xd0xxxxxx, a guest stack address) -- not the object. The
guard now derives the node from `r3`, which is correct by construction:
`table = ld32[obj+52]`, `head = ld32[table + ld32[obj+36]*4]`, `node = head-32`.

## Retraction 2: "a word from the .text in the heap" is not the signature

Measured on `blackbox/memory_0.bin`, over the whole 0x30000000 heap:

| filter | words left |
|---|---|
| 4-aligned, 0x10200..0x8aa674 | 1 637 146 |
| minus lifted function entries | 1 632 591 |
| minus UTF-16 text (both halves printable or zero) | 461 711 |
| minus words within 0x20000 of the previous entry | **68** |

The last row is the only useful one, and even it is not a detector: scanning all
seven dumps in `/root/Dev/*/blackbox/` gives **the same 44-68 addresses**, none of
them differing by more than a heap slide. They are ordinary data that happens to
look like a code pointer.

And the first "hit" of a live run was a false positive of exactly this kind:

```
NEW text word 0x00730100 at 0x3005087c
```

decoding the bytes at that address gives NBT:

```
30050870  00 26 03 00 05 54 69 6c 65 5a 00 00 00 73 01 00
              ^-- 0x00050026      "TileZ"        ^-- 00 00 00 73
```

`0x00730100` is the four bytes `00 73 01 00` at offset +6 of the string
`"TileZ"` followed by a length. **It is a coincidence of string layout, not a
code word**, and it appears at the same address in all seven dumps because the
heap is deterministic.

## Retraction 3: the dump does not contain a node with a corrupt +28

The chain from `obj = 0x35558670` was reconstructed exactly:

```
obj+36 (count) = 53 ; obj+52 (table) = 0x359089b0
table[53] ... table[19] = 0x359c1d70  ->  node = 0x359c1d70 - 32 = 0x359c1d50
node+0 = 0x28  node+4 = 0x357a22e0  node+8..+16 = 0x28  node+20 = 0x0b
node+24 = 0x0f node+28 = 0x00721f30   node+32 = 0x35906b00
```

`0x359c1d50` appears exactly once in 503 MB, at `0xd030f1d4` -- the `(4J) Server`
stack, 0x20 from `0x0064c0fc` (the crash lr). So it *is* the node the walk held.

But the same address in the runs that did **not** crash holds:

```
wt21: 00000000 00000000 ...        (unallocated)
wt31: 262000f2 00060000 359c7bf0   (other data entirely)
```

and, in the crash dump, the 128 bytes before it are UTF-16 strings
(`"SpawnX"`). The object is allocated, filled with a name, and only then does
`+28` hold a code address. **That is consistent with a copy of a string into a
node whose +28 is a separate field that was never a pointer** -- which is what the
disassembly of the write path shows.

## What a live run actually measured

Run 8 (2400 s budget, `PS3BASE_LIST_GUARD=1`, hardware trap
`GOWA_TRAP=359c1d00+128`) **crashed at t=500.7 s**, the eighth crash of the
campaign and the second at ~500 s. The calls to the walk were finally visible:

```
t=496.64  (4J) Server     r30=0x355ac710 r29=0xd029f420 lr=0x004e2ba0  x2
t=496.66  (4J) Save thread 1 ... (4J) Save thread 0
t=500.77  crash: signal 11 thread (4J) Server
```

**The walk runs only around a save**, which matches the fact that it never
appeared in 400 s of healthy play, and it is the window a watch must cover.

The trap captured 180 writes into the node's page. The one that matters:

```
trap 0 write 0x359c1d6c = 0x00750000 by main_thread
   <- rsv::store_range+0x5a0 <- f_00775ce8_body (mc_075.cpp:22559)
   <- f_006d2b90_body <- f_00763038_body (mc_074.cpp:29598)
   <- f_000e07e8_body (mc_008.cpp:10393) <- ... <- f_000d5f30_body (mc_007.cpp:23338)
```

and the instruction there is a **16-bit string-copy loop**:

```
L_7766e4:
    gpr[9]  = gpr[10]           # destination
    gpr[11] = gpr[8]            # source
    gpr[0]  = int8(gpr[0])      # one BYTE from the source
    gpr[10] += 2                # destination advances by TWO
    gpr[8]  += 1                # source advances by one
    st16(dest = gpr[9], value = gpr[0])   # <-- writes 2 bytes, high half stale
    gpr[0] = ld8(gpr[11])
    if (bc_cond(4,30)) goto L_7766e4
```

A byte-wide source is stored as 16-bit words with the high half left over from the
previous iteration. `0x00750000` is exactly that shape: one source byte `0x00`
with `0x75` ("u") above it. **This is the mechanism that puts a word that looks
like a text address into a neighbouring field.** It is the guest's own bytecode,
though -- the lifted code is a faithful transcription of what the title executes --
so the bug is in what the title's C++ passed to that routine, not in the lifter.

## What was built and kept

`runtime/include/runtime/codewatch.h`, `runtime/src/codewatch.cpp`:

* `rsv::store_range`, `store_locked`, `store_unconditional`, `merge_bits`
  (reservation.cpp), the SPU direct put (`spu/thread.cpp`) and `ppu::notify_store`
  (dcbz) each call `cw::check_store` / `cw::note_hit`. Together these are every
  lifted `ppu::st*`, every `vm::write*`, and every SPU MFC put.
* `cw::note_line(ea)` arms one 128-byte line via a 512 KB bitmap over the whole
  4 GB guest space; the store-path test is one array read. Each hit logs
  `prev -> value`, guest lr and thread.
* The list guard now arms the real node and object lines and reports only a code
  word that is neither a function entry nor UTF-16.

## Also fixed on the way (independent of this crash)

* `hook_impl` was assigned before `g_hooks` was populated, so **any** address
  hook -- not just this guard -- silently did nothing unless `PS3BASE_GAME_HOOKS`
  was set.
* `memory_map` did not link on main: it calls `vm::write32`, which goes through
  `rsv::store_range`, but the target did not include `reservation.cpp`. The suite
  is now 9/9.

## Where the next session should go, concretely

The writer is a **16-bit store with a byte-wide source and destination striding by
2**, at `f_00775ce8` (`mc_075.cpp`, guest pc around `0x7766e4`), called from
`f_006d2b90` / `f_00763038` / `f_000e07e8` / `f_000d5f30`. That is a *string
length* being passed one unit too large, in the save path.

1. Break at `0x7766e4` with `r10` (destination) and `r8` (source) logged for the
   write that lands on `0x359c1d6c`: the caller's length argument is the bug.
2. The guard's new node derivation makes `node` and `obj` printable at the moment
   of the walk, so a rerun with the corrected guard names the node in *this* run
   rather than in a two-day-old dump.
3. Do not trust a `TEXT address` alarm from the old guard: it was reading a stack
   frame. Filter on the UTF-16 test or it reports `".0"` as code.
