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

# The world is rendered (verified on main, no forcing)

**Capture**: `docs/world-on-main.png` -- a snowy biome with trees, ten hearts,
hotbar, targeting cross and the `Crafting`/`Inventory` prompts, 22 fps.

Reproduced on the **fully merged** `main` (after explore/11, /12 and /13) via
`tools/run_world.sh vfs-proof --seconds 1400`, with **no forcing of any kind**:
`APPLIED` is 0 for the whole session and the only `force:` line is
"save completed; flag rule armed". 434 flips, no crash, three world saves.

Earlier evidence, same result on an intermediate main: `/tmp/run-world-mainrepro`
-- plains biome, 57 fps, 3 saves, 336 flips.

## There was no forcing left to remove

`f216258=1` had already become a no-op (the override fired as
`r3 0x1 -> 0x1`) once the real NP chain returned a signed-in identity. The world
renders without it, so it is gone from every default command.

## What actually blocked the view

Not the emulator. **The input sequence.**

1. The autosave dialog "This game has a level autosave feature" appears *several*
   times -- once for the world save, then periodically in game. Each one swallows
   one press. A fixed sequence that sends one `cross` per anticipated dialog
   desynchronises and then sits on a dialog forever. Every 30 s capture lands on
   that dialog, which reads exactly like a frozen screen.
2. On the new-world form the cursor sits on `Difficulty: Easy`, not on
   `Create New World`. Pressing `cross` there toggles the difficulty and nothing
   happens. It needs two `down` presses first.
3. `input.cmd` is consumed on read: a press written during a transition can be
   lost. Always screenshot after a press instead of assuming.

## Two claims this campaign refuted, with measurements

- **"`(4J) FullscreenProgress` never gets its flag, so the screen cannot close."**
  Wrong. On the successful run it waits 169 s and is then served:
  `wait-in t=237.245 lr=0x0006f1f8 bits=0x1 mode=0x11 pattern 0x0` then
  `wait-ok t=406.447 pattern 0x0 -> 0x1`. The producer is `(4J) Server`,
  `lr=0x00072144` -> `f_00072130` (`out/ppu/mc_003.cpp:23353`), calling
  `sys_event_flag_set` right after `_sys_ppu_thread_create 'thread_autosave'`.
  It is late because the generator must finish first, not absent.
- **"The flag id is a stable identifier."** Wrong. It is heap-allocated and
  differs every run (`0x98046c00`, `0x98066e00`, `0x98068f00`, `0x9806f000`
  observed). Any forcing keyed on a flag address cannot survive a relaunch.
  The `lr` are stable; use those.

## Timeline of a successful world (no forcing)

| t | event |
|---|---|
| ~362 s | `(4J) Post processing` starts generating |
| 406.4 s | `UserAutoSave` writes `GAMEDATA` (~3.4 MB) |
| 406.44 s | `_sys_ppu_thread_create 'thread_autosave'` |
| 406.447 s | `(4J) Server` sets the FullscreenProgress flag |
| ~410 s | first world frame |

## Real defects found and fixed while getting here

| Defect | Fix |
|---|---|
| `cellSaveDataUserListAutoLoad` never read the disk | enumeration with `directory_iterator`, prefix filter, real counts |
| Save directories carried an invented `u0_` prefix; RPCS3 stores the title's `dirName` verbatim | store and list the name as given |
| `CellSysutilCallback` argument order reversed -- `(fn, arg, event, 0)` instead of `(fn, event, param, arg)` | corrected; `event` is r3, so no system event was ever recognised |
| `CELL_NET_CTL_STATE_IPObtained` was 4, the real value is 3 | corrected |
| `cellSaveDataGetListItem` unimplemented, so list rows had no content | implemented to RPCS3 offsets |
| `fill_stat` reported `SECUREFILE` for ordinary files, and did not sort | `NORMALFILE`, sorted by name, per-file rounded size |
| `tools/run_world.sh` never passed `PS3BASE_VFS_DIR`, so runs read the wrong VFS | passed |
| `tools/run_world.sh` launched through a subshell, so `GAME_PID` was the subshell's -- capture could photograph another agent's window | `env --chdir` |

## Still open (does not block the world)

- Loading an existing world: `Load` issues `savedata: load` and the names
  display, but the title's file callback returns `-2` after `METADATA` and
  `THUMB`, before `GAMEDATA`.

  **Correction of an earlier note**: `THUMB` is *not* misclassified. The title's
  own load callback (`f_002ea6ac`, `mc_029.cpp:5841-5855`) asks for `THUMB` with
  `fileType = 1` (`NORMALFILE`) in hardcoded form, RPCS3's `CellSaveDataFileType`
  does not even list `THUMB` (`cellSaveData.h:103-108`), and its `statGet` maps
  only `ICON0.PNG`/`ICON1.PAM`/`PIC1.PNG`/`SND0.AT3` to content types, leaving
  `THUMB` as `NORMALFILE` (`cellSaveData.cpp:1676`). We already do the same.

  The real gate is inside the title's callback, which is a state machine over a
  global at `0xbe4a08 + 332`. Two branches return `-2`: after reading `THUMB`,
  if the exceeded size does not match, or if `CRC32(THUMB)` does not match the
  expected value; and if the `GAMEDATA` buffer was not allocated by the title.
  `METADATA` is a 256-byte manifest (`count`, sizes and CRC32s for both files)
  and its four fields match the files on disk exactly, so the disk is coherent
  and the fault is in what we present to the title at read time.
- `f_0064c044` crashes `(4J) Server` (`lr=0x64c0fc`) after long sessions: a
  corrupted list pointer, unrelated to the render.

# Campaign round: playability, loading, crash — state on merged main

Proof capture: `docs/world-on-main.png` (oasis, trees, lake, ten hearts, hotbar,
7 fps). Taken on fully merged `main` via `tools/run_world.sh vfs-play
--seconds 1300`: **610 flips, 0 crashes, 7 world saves on disk, APPLIED = 0**.

## What was fixed (all merged, all with tests or measurements)

| Defect | Fix | Evidence |
|---|---|---|
| `xdotool key` presses were lost | `host::note_key` latches a scancode for 5 frames on `SDL_EVENT_KEY_DOWN` | 10 presses at 0 ms: **6 → 57** active pad frames |
| No keyboard binding for the right stick | R/F/T/G mapped to `b[4]`/`b[5]` | without them the camera cannot aim, so L2/R2 have no target |
| `userdata` read at `sp+0x60` instead of `sp+0x70` | all 3 savedata sites use `sp+0x70` | `-2` results in a full list walk: **6 → 0** |
| HLE lwmutex lost ownership; destroy could admit a second thread | ownership tracked, foreign unlock refused, destroy marks dead instead of erasing | `lwmutex_hle` 5 checks, `lwmutex_contention` 3 checks, 0 failures |
| `tools/run_world.sh` hardcoded `DISPLAY=:0` | a display per run, derived from the VFS path | run picked `:45` on its own beside 8 other servers |

The keyboard result is the one to keep: the game was driven **from the main menu
into a world by real X events alone**, with no input file and no forcing.

## Retracted claims

- **"The SDL focus guard blocks keyboard input."** Wrong: under Xvfb with no
  window manager `SDL_GetKeyboardFocus()` returns our window immediately and the
  guard passes. The press itself was too short to sample.
- **"THUMB is misclassified as NORMALFILE."** Wrong: the title's own load callback
  asks for `fileType = 1` in hardcoded form (`mc_029.cpp:5844`), RPCS3's enum does
  not list THUMB at all, and its `statGet` leaves it as NORMALFILE. Nothing to fix.

## Still open, each with its diagnosis closed

**Loading an existing world — the thumbnail wall.** The only writer of
list-entry `+168` (thumbnail size) and `+172` (pointer) is `f_002eeae4`
(`mc_029.cpp:11016-11021`), and it is **never called**: `grep -c 'thumbset:' = 0`
across a whole run. So `+168` stays 0, `f_002e8ae8` takes its hardcoded `(0,0)`
branch (`mc_029.cpp:3714-3715`), `f_001013b4` stores zeros at `+2512/+2516`, and
`f_0004a7a4` finally does `ld8[0]` → SIGSEGV with `guest 0x0`, `r3=r4=r5=0`.
Reproduced 6 of 6. What is missing is the branch point that should call the
setter — its OPD `0xbbe7f8` has zero data references, so it is only reachable
through a vtable filled at run time.

**The `f_0064c044` crash — mechanism known, cause not proven.** One 4-byte word
of one list node is overwritten with a word from the title's own `.text`
(`0x00721f30` in one dump, `0x00730100` in another; `0x721f30` disassembles to
`stw r3,0(r26)`). The other 52 table heads are intact and the chain from the
crashing object is clean for 29 nodes with a proper `+32 = 0` terminator.

Both hypotheses that seemed obvious were **refuted by test**:

- the guest reservation is fine — a real `lwarx`/`addi`/`stwcx` loop gives exact
  counts at 2 and 4 threads over three rounds, and invalidation fails exactly
  when it should;
- the HLE lwmutex is not involved — with `PS3BASE_LWMUX_TRACE=1` it is never
  called; the title uses syscalls 95-99 (1271 create, 1155 destroy, 37 lock,
  32 unlock).

`PS3BASE_LIST_GUARD=1` now names the node and both values **before** the
dereference, so the next attempt starts from the node rather than from the
crash. Reproduction is unstable (about 3 runs in 16), so a clean run is not a
proof.
