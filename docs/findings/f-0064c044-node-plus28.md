# f_0064c044: the corruption is real, it is at +28, and it is a code word

Status: the crash target is **proven by disassembly and by the live register
dump**, and two claims from other sessions are corrected below with the bytes
that settle them. The writer is still not named.

## Antecedents (grep over refs/, -R)

| Finding | Source (path:line) | Applies here? |
|---|---|---|
| A page-guard watchpoint exists to catch stores `vm_write*` cannot see, logging the faulting RIP | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1171` | **yes** -- same shape as `GOWA_TRAP` / `PS3BASE_CODEWATCH` |
| Value-CAS `stwcx` is ABA-vulnerable and corrupts through reused blocks | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:821` | **no** -- reservation measured exact at 2 and 4 threads |
| A re-entrancy guard on the fault handler is mandatory | `refs/jdc-ps3recomp/runtime/ppu/ppu_loader.cpp:1179` | yes, noted |
| `_sys_lwmutex_lock` takes its own mutex around a `signaled` word; the retry loop lives in the *caller* | `refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_lwmutex.cpp:135` | **yes** -- this is why two lr values in a log are not two writers |

## What the crash actually is (measured, this session)

`f_0064c044`'s crash is reached from `f_0064c2f8` -> `f_0064be6c`. Disassembly
of `bin/mc/mc.elf` (segment 0: vaddr 0x10000, file offset 0) at `0x64be6c`:

```
0064be6c  stdu   r1,-0xa0(r1)
0064be90  lwz    r4, 0x28(r31)      <- count / list presence, NULL check
0064beac  lwz    r29, 0x24(r31)
0064bef0  lwz    r3, 0(r3)          <- table[count]
0064bef4  addic  r6, r3, -0x20      <- node = head - 32
0064bf28  lwz    r28, 0x1c(r29)     <- NODE+28, the field
0064bf30  beq    0x64bf4c
0064bf34  lwz    r4, 0(r28)         <- vtable load
0064bf3c  lwz    r4, 0x14(r4)
0064bf40  lwz    r4, 0(r4)
0064bf44  mtctr  r4
0064bf48  bctrl                    <- indirect call through it
```

The run-9 live registers at the fault (`/tmp/run-world-33/game.log`):

```
r29 = 0x378c9db0   <- the node
crash: object r6 (0x378c9db0): 00000028 378c9de0 00000028 00000028
                               00000028 00000008 0000000f 0048f640
```

so `node+0x1c = 0x0048f640`, and the fault is guest 0x0 because
`lwz r4,0x14(r4)` walks off the code word.

**`0x0048f640` is an instruction, not a vtable.** Disassembled:

```
0048f640  addic  r4,r4,0xb
0048f644  clrldi r4,r4,0x10
0048f648  sradi  r6,r4,0x2f
0048f64c  subfc  r5,r6,r5
```

It is **not** a lifted function entry (`grep f_0048f640 out/ppu/funcs.h`
returns 0 matches); the nearest entry is `0x0048f628`, 24 bytes earlier. So it
is an address *inside* a function body: precisely the "code word in a pointer
field" signature.

## Two corrections, each from bytes

### Corrected: "two save threads race on the field" (piste 33, retracted there)

```
$ grep 'line-write 0x378c9c7c+4' /tmp/run-world-33/game.log | grep -oE '\[[0-9]+\]' | sort | uniq -c
     38 [1054653]   # (4J) Server
      5 [1059742]   # (4J) Save thread 0
```

38 of 43 writes are one host thread. The two lr values, `0x005eb2c0` and
`0x005ea480`, are the instructions *after* `bl 0x5e9b10`, which is
`call_import(ctx, base, 326)` = `sysPrxForUser::sys_lwmutex_lock`. A
lock-acquisition return address is what `ctx.lr` holds at that moment; it is
not a store site. Piste 33 reached the same conclusion independently.

### Corrected: "the +28 field is not corrupt, it is UTF-16" (piste 33, retraction 5)

That retraction generalised from a *different* run's dump. In run 9 the field
is `0x0048f640`, an instruction word at a mid-function address, and the
neighbours are plausible list bookkeeping (`0x28, 0x378c9de0, 0x28, 0x28,
0x28, 0x08, 0x0f`). The original dump reading -- one 4-byte field of one node
overwritten with a word from the code stream -- is **right**. A UTF-16 region
can also hold a word-shaped value, which is why a value test needs the "is it
a legal function entry" filter; but a register dump is not ambiguous.

## Tooling delivered: syncwatch

`runtime/include/runtime/syncwatch.h` keeps a per-thread stack of the LV2
objects a guest thread owns. Pushes in `sys_mutex_lock/trylock`,
`_sys_lwmutex_*` (both the LV2 syscalls and the `sysPrxForUser` HLE this title
actually uses), `sys_semaphore_wait/trywait` and `sys_rwlock_wlock`; pops in
the matching releases. `codewatch`'s `line-write` now ends with `held <list>`.

This answers the question the previous session could only pose: *was the field
written while the guest's own lock was held?* The answer is the `held` field,
per write, in the log.

## Measurements on run 42 (this session)

* Generation is **much** slower under load: `load average` 24-33 with four
  sessions building. The progress bar advances and restarts per phase; at
  t=2500 s the world was still generating.
* `vm: sys_vm_memory_map -> 0x100c0000` at t=862 s proves **allocation**, not
  gameplay. `UserAutoSave == 0` is the signal that no world save has happened
  yet, and therefore that the game is still in generation.

### Lesson of method (worth keeping)

A capture detector keyed on "dark pixels at the bottom of the screen"
classified the *loading screen* as a world, because the grey tip box sits at
the bottom. The replacement tests structure: a saturated green progress bar or
the splash logo means loading; neither means world. Validated against
`docs/world-on-main.png`, `docs/playability-31/world-survival.png` and
`mine-hole.png` (all WORLD) and the loading captures (all LOADING).

## What is still open

Who writes `node+28`. The watch is armed on the node lines the walk reaches,
and `held` records lock state at each write. Do not accept a story about this
crash that comes from a post-mortem dump alone: four narratives have already
died on measurement ("reservation is broken", "a code word was stored",
"two threads race", "+28 is UTF-16").
