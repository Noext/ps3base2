# Forced values (debts)

Each entry makes the title proceed past a place where our emulation differs from
the PS3. They are debts: every one must be replaced by the real fix.

Mechanism: `PS3BASE_FORCE="f<hexaddr>=<value>"` overrides r3 on return from the
lifted function at that address. `w<hexaddr>:<n>=<value>` writes n bytes at a
guest address. `lr<hexaddr>=<value>` matches the caller. Each rule logs once,
with the original and forced values.

| # | site | original | forced | unlocked |
|---|---|---|---|---|
| 1 | `f_00216258` (`mc_020.cpp:21243`, `ld8[obj+593]`) | `0` | `1` | The server loop `f_00188428` no longer exits at 66 ms. The title reaches **"Initializing server / Generating spawn area"** and the progress bar advances (measured 14060 → 17898 green pixels, then a second phase 2526 → 5604) instead of freezing on "Finalizing..." |

## Why forcing is legitimate here

The value read at `obj+593` is set by `f_00216560`, which leaves it at 1 unless
`f_00216260` returns 0, which happens when `sceNpMatching2Init2` (import 308)
is taken as succeeded. We do not implement that import, so it returns 0 through
the generic unimplemented path. The forcing substitutes for the missing NP
initialisation; the real fix is to implement `sceNp2::sceNpMatching2Init2`,
`sceNp2::sceNpMatching2CreateContext` and `sceNp::sceNpManagerGetNpId` with the
RPCS3 return values.

## What forcing #1 unlocked, measured

A 20-minute run with forcing #1 only. The server kept running and the spawn-area
bar advanced steadily: 11951 -> 14268 -> 15218 -> 17156 -> 18867 green pixels at
30 s intervals. At t=519 s the run **crashed** (SIGSEGV) in `(4J) Server`:

```
crash: signal 11 ... fault 0xbcf8d80f thread (4J) Server 0x1000016
  #0 ppu::ld32            (ops.h:43)
  #3 f_0064c044_body      (mc_063.cpp:23079)
  #4 ppu::call_indirect
  #5 f_004e2ac0_body      (mc_049.cpp:10084)
```

`f_0064c044` walks a linked list and does `ld32[ node + 28 ]`; the node pointer
was `0xbcf8d80f`, a corrupted value. Just before the crash, a new worker thread
`(4J) Post processing` was created and exited cleanly, so generation had
progressed past spawn area into post-processing.

**Time to reach this point: ~519 s of emulated time** for spawn-area generation.
Speed of the SPU work is a separate concern, to be addressed later.

## Experiment: forcing the chunk-update flag (forcing #2, NOT a solution)

Rule: `e98046c00:1:5000` -- call sys_event_flag_set(0x98046c00, 0x1) once, 5 s
after the first UserAutoSave completes.

Result, measured:

```
349.721  force: save completed; flag rule armed
354.721  force: APPLIED flag 0x98046c00 bits=0x1 -> 0x0 (5000 ms after save)
354.721  flagtrace: wait-ok (4J) Chunk update lr=0x0014f974 bits=0x3
354.721  flagtrace: set  from=(4J) Chunk update lr=0x0014f994 bits=0x1 0x0 -> 0x1
354.726  flagtrace: set  from=(4J) Chunk update lr=0x0014fbe4 bits=0x2 0x1 -> 0x3
```

**(4J) Chunk update wakes up.** The path that was unreachable before now runs:
both the index-0 set at 0x14f994 and the index-1 set at 0x14fbe4 execute. The
thread finishes one round of work and **blocks again** on the same flag.

**The screen does not close** and the world is not rendered.

### Correction: there is no MSSTimer producer

An earlier reading of the same log attributed the first `set bits=0x1` to
`MSSTimer`. That is wrong. `force::tick()` runs from inside `call_indirect`,
so it fires on whatever thread happens to call an import at that instant, and
`(4J) Chunk update` was already waiting, which is why the guest thread named in
that line is `MSSTimer`. The line 180 us later is the force rule reporting
itself.

**Measured**: in runs without the flag rule (`world3.log`, `world4.log`) the
flag is set exactly **once** in the whole run, at startup, and never again. There
is no periodic producer to fix. The flag is simply never set by the guest after
the save.

So forcing it once buys exactly one cycle of work, and the screen stays. The
remaining problem is not a missing periodic setter: it is that the title expects
the chunk thread to have completed its work and never re-arm it. That needs the
call chain above the flag, not another forcing.

## Fixed (not a forcing): the world list now enumerates the disk

Two real defects, both fixed on main (commits ece42c8 + 70775dc):

1. `cellSaveDataUserListAutoLoad` never read the filesystem -- it fabricated a
   zeroed `CellSaveDataListGet` and always returned `ERR_NODATA`. It now walks
   `save_root()`, filters on `dirNamePrefix`, respects `dirListMax`/`bufSize`,
   fills `CellSaveDataDirList` (0x30 bytes per entry, `SAVEDATA_LIST_PARAM`
   from each PARAM.SFO) and reports the real counts.
2. Our save directories carried a `u0_` prefix we invented. RPCS3 stores the
   title's `dirName` verbatim under `/dev_hdd0/home/<uid>/savedata/`
   (`refs/rpcs3/.../cellSaveData.cpp:802`), which is what a title asking for
   the prefix `BLES01976--` expects. Saves are now stored and listed under the
   name the title gives.

**Verified**: `UserListAutoLoad: 4 savedata match prefix 'BLES01976--',
listing 4`, and the Start Game panel grows from 2 rows to 6. Loading a world
now reaches "Initializing server / Generating spawn area" and a second autosave
dialog, instead of "No Games Found".

### Two autosave dialogs

After the first autosave dialog is acknowledged, a **second identical dialog**
appears and needs its own press. A fixed input sequence that does not expect it
sits there forever. Always screenshot after an OK.

## Forcing #1 is now redundant

The explore/3 work implemented the real NP chain. Measured consequence: with
that in place, `force: APPLIED func 0x00216258: r3 0x1 -> 0x1` -- **the value
before the override is already 1**. `sceNpManagerGetNpId` now returns a
signed-in identity, `f_00216260` returns success on its own, and `f_00216560`
leaves `CellServer+593` at 1 without help.

`PS3BASE_FORCE="f216258=1"` still works and is harmless, but it is no longer
doing anything on the NP path. `PS3BASE_NP_OFFLINE=1` selects the truthful
signed-out state instead, which is the mode to use when comparing branches.

## What the four parallel tracks established

| Track | Finding | State |
|---|---|---|
| 1 -- server loop | `(4J) Server` is **not** stuck: it works actively (memory writes, syscalls advancing). The frozen screen is `(4J) FullscreenProgress`, `(4J) Chunk update` and `(4J) Tile update`, all parked in `sys_event_flag_wait` with `timeout=0` | measured |
| 1 -- flag addresses | LV2 flag ids are heap-allocated and **differ every run** (0x98066e00, 0x98067700, 0x98068400 across runs). `lr` are stable; a forcing keyed on a flag address cannot survive a relaunch | measured |
| 2 -- screen loop | The progress screen is a dedicated `(4J) FullscreenProgress` worker (`UIScene_FullscreenProgress`), exit gate `lr=0x6f1f8`, `mode=0x11` (AND + clear), infinite timeout. A thread already inside the syscall cannot be reached by a return-value override | measured |
| 2 -- two dialogs | A **second identical autosave dialog** appears after the first OK and needs its own press | measured |
| 3 -- NP | `sceNpManagerGetNpId` was the real leaf; returning 0 for "offline" lied, `f_00216260` tests `r3 < 0`. Real implementation makes forcing #1 a no-op | fixed |
| 4 -- callbacks | `sceNpManagerRegisterCallback`, `sceNpBasicRegisterHandler` and `cellNetCtlAddHandler` are registered and **never invoked**; the three sysutil slots only ever see event `0x802`, never `0x02`/`0x03`/`0x0801`/`0x0803` | measured |
| 4 -- savedata | `UserListAutoLoad` ignored the disk **and** our `u0_` prefix broke the title's `dirNamePrefix` query | fixed |

## Still open

`UserListAutoLoad` now lists the saves (`5 savedata match prefix
'BLES01976--', listing 5`) and the Start Game panel grows to six rows with a
scroll arrow, but **the rows render blank and pressing Load does nothing**: no
`savedata: load 'BLES01976--...'` is ever issued. The enumeration reports the
right count while the entry contents the title reads back are not what it
expects -- the next thing to check is a byte-level dump of our `CellSaveDataDirList`
entries against what the title reads at `funcFixed` time.
