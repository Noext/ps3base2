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

## D-010 -- The interpreter's dispatch site is identified by its mtctr, not by bctr-4

The shared-table detector (`recomp/ppu/analysis.py:table_dispatch_bctr`)
recovers the RAGE script
interpreter's 256-entry opcode table from the code that dispatches through it.
It made two assumptions that are false on the title, and each produced a
different failure about 21 s into the boot.

**Rule 1: mtctr is not always the instruction before bctr.** Of f_00d19208's
163 bcctr, 130 put mtctr at -1, 26 at -2, 3 at -3 and 4 at -4. The gap holds
real work, not a nop:

    0xd19e90  mtctr r5
    0xd19e94  subfc r30, r3, r30
    0xd19e98  bctr

The old gate, 'if not mtctr at bctr-4: return None', rejected that site, the
lift emitted a switch with NO cases at all, and the run stopped with the
CORRECT target already in ctr:

    F ppu: unknown indirect target 0x00d1bbec (lr=0x0085c000)
      ctr=0xd1bbec r16=0x013f6400 r5=0x00d1bbec

0xd1bbec is table[0x95] at 0x013f6400, and the opcode byte the guest read was
0x95. The interpreter was working; the switch was empty.

**Rule 2: the feeding lwzx is the one whose RT is the mtctr source.** Ignoring
RT picked the loop consumer's lwzx (0x00d1a008 lwzx r9, r3, r6) instead of
0x00d19ff0 lwzx r5, r16, r5, and rebuilt 'a table' at 0x013323f0 with one
entry.

**Consequence.** The mtctr is now searched for within 8 instructions, the
feeding lwzx is required to write the mtctr source register, and the look-back
window is 40 instructions rather than 11. Both rules are pinned by
tests/ppu_dispatch_table.py (4 checks, registered as the ctest named
ppu_dispatch_table); reverting either rule stops it at the first check.
Measured after the fix: 163 of 163 shared-table dispatches in f_00d19208
resolve to base 0x013f6400.

**The poison was a red herring, and that is worth recording.** The crash dump
prints cdcdcdcd / dfdfdfdf in several register slots, which is what shaped an
earlier hypothesis about an unfilled buffer. It is wrong. The bytes the
interpreter consumed were reconstructed from the dump's own word lines: the
byte at the read is 0x95, a real opcode. 0xcd/0xdf sit in other heap objects
printed in the same dump, at unaligned offsets. Nothing was missing from the
buffer, so no file read, decrypt or SPURS job was skipped on this path.

## D-011 -- A seed names (image, offset); an address is a fact about one layout

**Context.** Every mechanism that records a guest address -- a discovery seed, a
static seed, a hand-written landmark -- wrote it as an absolute number. Red Dead's
liblv2 sat at 0x21000000 during one session and 0x30000000 after layout_for.py
recomputed the module band from the title's text extent. `seed_is_stale()` can
only DROP a seed whose address names no image and ask for another run; the file
it was written to no longer says which function it meant, so the information is
gone, not delayed.

**Decision.** A seed may be written `<image>+<offset>`, and the lifter resolves
it at lift time against the base this lift actually gave that image
(`parse_seed` / `resolve_seeds` in recomp/ppu/codegen.py). The offset is a byte
offset into that image's own code segment, so it survives any relayout. The
absolute form still parses, because a discovery run emits it and cannot know the
image base before the lift runs.

`recomp/landmarks.txt` is committed and read by every lift. A landmark is a seed
whose purpose is to be READ rather than lifted: it names an address the runtime
already reaches, and the label beside it is what a report prints. It is a
property of the firmware MODULE -- all three titles load the same liblv2 and
libsre -- which is why the file lives beside the other committed facts about the
firmware and not in a title's output tree. An absolute landmark is refused
loudly, because a landmark that can go stale has no reason to exist.

**Consequence, measured.** Red Dead's SPURS abort reports

    ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 spurs-abort-tail (_sys_panic+0x58)

where before it said `lr=0x3000c7e8 (not lifted)`. Both describe the same byte;
only one names it. The address is the instruction after `bl 0x3000fad0` at
0x3000c7e4, so the second report was also WRONG: the function is f_3000c790, and
it was in the dispatch table at HEAD (out-rdr/ppu/table.cpp:74668). See D-012 for
why the classification was wrong.

tests/ppu_seed_forms.py pins the two forms, the per-image resolution (the same
file resolves to 0x3000c7e8 for liblv2 at 0x30000000 and 0x2100c7e8 at
0x21000000), and the refusal of an absolute landmark.

## D-012 -- "Is there lifted code here" is a span question in a report, an entry question at a call

**Context.** `has_code_at(addr)` is `lookup(addr) != nullptr`, and `lookup` is a
per-word table built from the FuncEntry list -- keyed by function ENTRY. So it
answers false for every return address in the guest, which is to say for most
PCs a report ever shows. Red Dead's process-exit line used that answer as its
entire verdict, and so printed "(not lifted)" for a PC inside a running, lifted
function.

**Decision.** The two questions are separated. `has_code_at` keeps its precise
meaning: an indirect call needs an entry, because that is what can be called.
Reporting uses a new `span_entry_at(addr)`, backed by a `FunctionSpan` table the
lifter emits (lo, hi, entry, image per function), and `lv2::describe_address()`
renders image + landmark + containing function + offset. It replaces the
entry-only test in the process-exit, thread-exit and unknown-indirect-target
lines.

**Consequence.** The same abort now reads
`liblv2+0xc7e8 spurs-abort-tail (_sys_panic+0x58)`; the two "Call Stack Trace"
frames the title prints resolve to `libsre+0xcb24 spurs-abort-frame0
(f_3200cab8+0x6c)` and `libsre+0x9754 spurs-abort-frame1 (f_3200930c+0x448)`.
The lifted code is UNCHANGED -- diff of the generated tree against HEAD is
table.cpp only, 76793 functions both sides -- because the gap was in the report,
not in the lift.

