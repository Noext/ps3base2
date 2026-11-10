# Decisions

Structural choices and the reasoning behind them. A decision is recorded here
**before** the code that depends on it, so the next person knows why rather than
guessing from the shape.

---

## D-001 -- Sony's libraries are lifted; only the LV2 kernel is rewritten

Firmware modules are compiled from the user's own `.prx` by the pinned lifter
and run as guest code. Only the syscall surface is reimplemented.

**Why.** A reimplemented library is a second implementation of something Sony
already wrote correctly, and every difference is a bug that only shows on one
title. The syscall surface is the exception because it is the hardware boundary:
there is no PPU code to lift for "allocate 256 MiB".

**Consequence.** `tools/prx_relocate.py` places each module at a fixed base and
`recomp/ppu/codegen.py` lifts it. A module with no lifted image falls back to
HLE, and the log says which.

## D-002 -- Never a fake success

An unimplemented syscall, NID or import returns an explicit error and logs its
name. Never `CELL_OK`.

**Why, with a measured example.** This tree's dispatch used to log a warning
for an unimplemented syscall and then set `r3 = 0`. Every caller believed its
object had been created: a mutex existed, a heap was mapped, a thread started.
The failure then surfaced minutes later, far from the cause. The predecessor
project lost days to exactly this through `sys_ppu_thread_once`.

## D-003 -- There is no instruction interpreter

The runtime executes lifted code only.

**Why.** An interpreter is a second execution path with its own correctness
surface and its own bugs, and every report would then have two possible causes.
It also invites the worst failure available here: an emulator that is *almost*
right, so a title appears to work while doing something subtly different from
what the disc contains.

## D-004 -- No key, no decrypted image, no lifted source in the repository

`.gitignore` and the pre-commit hook both refuse derived material. Producing it
is the operator's own business, done with their own tools.

**Why.** The repository must be distributable and reviewable without carrying
anything that is not ours to distribute.

## D-005 -- Everything title-specific lives behind `game::Profile`

The runtime may not know a game name. Image name, CRT `argv[0]`, address-keyed
hooks and SPU job natives arrive through `runtime/include/runtime/game_profile.h`.

**Why, measured.** The fork installed the God of War layer unconditionally, in
**two** places: `hooks::init()` at startup and `install_builtin()` lazily from
the first SPU thread. Gating only the first still faulted before the guest ran a
single instruction on a foreign disc.

**Consequence.** A build with no profile is the supported way to measure a
foreign title: it gets the generic machine and nothing else.

## D-006 -- Coverage is measured against the specification, not against demand

The LV2 table is generated from the specification, and the generator scans this
tree's own registrations to report what is implemented, per group.

**Why.** Implementing what a title asks for produces a kernel that works for one
title and fails silently for the next. A number with a per-group breakdown can
be improved; a claim cannot.

---

## The abandon criterion

If the runtime cannot carry a second title to a rendered frame within **three
weeks** or **thirty recorded attempts** on the same wall, and two of these three
hold -- no new syscall implemented in a week, no flip count increase in a week,
the blocking defect reproduces identically after three distinct diagnoses --
then stop and document the wall rather than continuing.

---

## D-007 -- A system module that owns dispatcher state runs in HLE, not LLE

`libsysutil` is forced to HLE by a `ModuleOverride`, against the general rule of
D-001 (lift Sony's libraries, rewrite only the kernel).

**Why, measured.** With `libsysutil.sprx` lifted and loaded as LLE, Red Dead
Redemption's main thread parks on a futex before it calls a single import: the log
stops right after `mmapper: allocate_address`, no syscall is issued, no import is
reached, and the process sits at 18% CPU with **zero frame flips** for the whole
run. The same binary with the module on the HLE path presents **2341 frames in
40 seconds**.

The module is the `cellSysutil` dispatcher. Its lifted code drives callback and
event state that the HLE handlers already own, so running both gives one state
two writers and the guest waits for a wakeup the other side never sends. D-001
assumes the lifted module is the only writer; where that is false, the choice is
between deleting the HLE handlers or not lifting the module, and the HLE handlers
are what the other two titles use.

**Consequence.** `hle/support.cpp` registers the override with the measurement in
its comment. A module belongs on this list when its lifted code and an HLE
handler would each believe they own the same state. It is not a way to paper over
a broken lift: the LLE attempt is what proved it deadlocks.


---

## D-008 -- Always start from the proven LLE list; add only self-contained libraries

A new title begins with **exactly** the modules Minecraft and GTA run on:

    liblv2  libsysmodule  libsre  libgcm_sys  libfs  libvdec  libsmvd2

Everything else goes through the existing HLE until the count says otherwise. Then,
and only then, a module may be added to LLE if it is **self-contained**: it
provides functions and owns no dispatcher state.

**Never in LLE:** `libsysutil` and its family (`libsysutil_np`, `_np2`,
`_np_trophy`), savedata, the dialogs, NP, the pad, the audio. Those hold state the
HLE already holds, so lifting them gives that state two writers.

**Why, measured twice.**

1. `libsysutil` as LLE parks Red Dead Redemption's main thread in
   `sys_cond_wait` forever: `PS3BASE_COND_TRACE` shows one wait on cond
   `0x86001400`, `timeout=0`, and **zero signals**. Only one PPU thread exists, so
   nothing can signal. Black screen, 0.2% draws, no crash, no unimplemented import.
2. `libsysutil` as HLE flips (2341 frames in 40 s) but the title calls 230 exported
   C++ symbols the HLE does not provide (`cxml::Document`,
   `sysutil_cxmlutil::FixedMemory`, `cellSysutilPacketBegin`), so it renders
   nothing. It is a library of 230 exports, not a dispatcher a handler can replace.

So the module belongs to neither list, and the title needs its HLE surface written
instead. That is the work D-006 measures.

**Consequence.** `recomp/lle_modules.txt` carries the seven and a marked section
of additions, each with the measurement that justified it. Red Dead Redemption
adds `libnet`, `libfiber`, `libssl`, `libhttp`, `librtc`: 153 of its 343 imports
are covered, leaving **190 for HLE**.

## D-009 -- Firmware modules live above every region the runtime allocates

`layout_for.py` places modules from the title's text extent upward, in the band
`0x30000000..0x3F000000` -- where RPCS3 maps its fixed segment
(`PPUModule.cpp:2242`).

**Why, measured.** An earlier band at `0x20000000` was chosen as "above the slot
allocator's floor", and that floor is the problem: `reserve_slot_locked` walks up
from `0x10000000` in `0x10000000` steps, so `sys_mmapper_allocate_address` handed
the guest `0x20000000` for a 256 MB request -- the same memory eight modules were
placed in. `mmapper.cpp` now refuses and logs any allocation that overlaps an
image, and the layout is asserted against the video, stack and SPU regions,
including the compact layout's `0x50000000`.

