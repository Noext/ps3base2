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
