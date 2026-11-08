# f_0064c044: the writer hunt -- instrumentation, and two measured findings

Status: the writer is **not yet named**. What this session establishes is
(a) that the guard everyone was relying on never ran, (b) that the "text word in
the heap" test is worth nothing without a signature, and (c) a working,
low-cost writer watch that is armed on the real nodes of the real list.

## Antecedents (grep over refs/, -R)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| A page-guard watchpoint exists exactly to catch "raw memcpy/vector stores that `vm_write*` instrumentation can't see", armed on a guest EA, logs the faulting RIP, single-steps and re-arms | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **yes** -- this is the same shape as our `GOWA_TRAP`, and it names the reason a value-based test is not enough |
| The page-guard handler needs a re-entrancy guard, because logging into a guarded page faults inside the handler forever | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | **yes** -- noted; our trap logs only from the SIGTRAP step, not from the SIGSEGV, so it is already outside that trap |
| Value-CAS `stwcx` is ABA-vulnerable and produces *heap corruption* through a reused block; the fix is full reservation semantics plus invalidation on any store | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **no for this crash** -- our reservation is already exact (previous session measured it at 2 and 4 threads) |
| `lwarx`/`stwcx` correctness is a known source of nondeterministic heap corruption in these ports | same | no, refuted by measurement (see the earlier finding) |
| A code word inside a *data* pointer field | `refs/rpcs3/**` | **no match** -- RPCS3 has no comparable report |

## Finding 1 (measured): the list guard never ran

`f_0064c044` is reached through `ppu::hook_call`, and that only calls
`hook_impl` **when `ppu::hook_impl` is non-null**. `hooks::init()` did:

```
if (!g_hooks.empty() && !ppu::hook_impl) ppu::hook_impl = impl;   // g_hooks still empty
...
if (getenv("PS3BASE_LIST_GUARD")) { add_guard(0x64c044, list_guard); }
if (getenv("PS3BASE_GAME_HOOKS") == nullptr) return;              // the documented way
```

With `PS3BASE_GAME_HOOKS` unset, `g_hooks` was empty at the first line, so
`hook_impl` stayed null, and the guard registered afterwards was **never
invoked**. The log line it prints is emitted by `init()`, not by the guard:

```
I  0.001058 listguard: guard installed on f_0064c044: node +28/+32 are checked ...
```

so the earlier report "installed, 0 false alarms, world reached" was measuring
an inert guard. **Fixed**: `hook_impl` is re-assigned after the guard is
registered, and the flag bit is set explicitly.

## Finding 2 (measured): "a text address in the heap" is not a signature

`blackbox/memory_0.bin` (503 MB, 13 segments, format `{u32 addr, u32 len}` +
bytes) contains, in the 0x30000000 heap:

```
4-aligned words in 0x10200..0x8aa674           1 637 146
not equal to any lifted function entry         1 632 591
```

Half of those are UTF-16 game text (`0x00640065` = "de"). After dropping every
word whose two halves are both printable or zero, **908 219** remain, and
**904 955** of those are still not function entries. A watch that fires on "a
value inside the code range" therefore fires constantly; a first version of this
instrument produced 60 hits before the first frame and 338 with stacks included.

The measured corrupt values are different in one respect:

```
0x00721f30  (nearest entry 0x0070d138, +85 496)   -> stw r3,0(r26)
0x00730100  (nearest entry 0x0070d138, +143 304)  -> stw r9,4(r11)
```

both **4-aligned mid-function instruction addresses**, in a field that held a
**heap pointer**. That pair is the signature the watch now requires.

## Finding 3 (measured): what the corrupt object actually looks like

Variant A, a node at `0x359c1d50` (the orchestrator's reading)

```
+0 00000028  +4 357a22e0  +8 00000028  +c 00000028  +10 00000028
+14 0000000b +18 0000000f +1c 00721f30 <-- +20 35906b00 +24 af223bc5
```

Variant B, the same word as `+32` of a node at `0x359c1d4c`

```
+0 00000033 +4 00000028 +8 357a22e0 ... +20 00721f30 +24 35906b00
```

The **second** reading is right, and it is not ambiguous: `0x359c1d50` occurs
**exactly once** in the whole 503 MB dump, and that occurrence is
`0xd030f1d4` -- a word of the `(4J) Server` stack, four bytes from
`0xd030f1f4 = 0x0064c0fc`, the crash's own return address. That is the local
variable `f_0064c044` keeps `r30` in, so the node base the walk holds is
`0x359c1d50`, and the corrupted field is `+28`, not `+32`. `0x359c1d4c` is
then the word *before* the node, and `0x00000033` there is a neighbour's count.

This corrects the brief in one place and the orchestrator in another: the field
is `+28` (as the original report said) and the node is heap, not .data.

## What was built (this session)

`runtime/include/runtime/codewatch.h`, `runtime/src/codewatch.cpp`, and one
hook in each of the three write choke points:

| Path | Where | Covers |
|---|---|---|
| `rsv::store_range` | `reservation.cpp` | every lifted `ppu::st*`, `vm::write*`, SPU MFC put |
| `rsv::store_locked`, `rsv::store_unconditional` | `reservation.cpp` | `stwcx`, SPU `putlluc` |
| `rsv::merge_bits` | `reservation.cpp` | SPU put-merge |
| SPU direct put | `spu/thread.cpp` | the `g_edge_direct` fast path that memcpy's into `vm::ptr` |
| `ppu::notify_store` | `ppu_ops.cpp` | `dcbz` (a 128-byte zero fill that can erase a node) |

Two layers, because one is not enough:

1. **Value watch** (`PS3BASE_CODEWATCH=1`): a store of a 4-aligned mid-function
   code address over an existing heap pointer. Off until
   `PS3BASE_CODEWATCH_AT=<s>`, because the first 90 s legitimately install
   switch/jump-table code pointers into heap structures.
2. **Line watch** (`note_line`): a 512 KB bitmap over the whole 4 GB guest
   space at 128-byte lines; arming a line costs one bit set, and the test on the
   store path is one array read. `list_guard` arms the line of **every node the
   walk actually reaches**, plus the container, so the history of those lines is
   recorded *before* anything looks wrong. Each hit logs address, size, value,
   guest lr and thread.

Plus `PS3BASE_CODEWATCH_SCAN=1`, a 5 s sweep that names any standing code word
in the heap and arms its line, and `measure_pattern()` which counts how
specific the dump's shape is.

## Measurements so far

* 23-check suite unaffected: the watch is off unless `PS3BASE_CODEWATCH` is set,
  and `codewatch.cpp` links into the small test binaries through
  `rsv::store_range` without them (it is in the runtime object library).
* Loud-noise runs (kept as evidence, not as results): 60 hits at boot with the
  range test only; 338 with stacks included; 174 with the sweep unfiltered.
* The 3000 s run with `PS3BASE_CODEWATCH=1 PS3BASE_CODEWATCH_AT=90
  PS3BASE_LIST_GUARD=1` reached the world (flips 26 341, 0 crashes) and logged
  **0 value hits and 0 line hits** -- the guard fix landed after this binary was
  built, so the line watch was still inert in it. Re-run pending.

## Next step, precisely

Run the binary that contains the `hook_impl` fix, with
`PS3BASE_CODEWATCH=1 PS3BASE_LIST_GUARD=1 PS3BASE_CODEWATCH_AT=60`, for
2400 s+, and drive to the world. Then:

* a `line-write` line whose value is a text word is the writer: thread + guest
  lr + value, which is the proof the brief asks for;
* a `line-write` history with no bad value but a `listguard` hit means the
  writer does not cross any of the five choke points -- then the remaining
  candidates are the RSX download path (`rsx_renderer.cpp`) and
  `lv2/mmapper.cpp`, both of which memcpy into `vm::ptr` directly.
