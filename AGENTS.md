# ps3base v2 -- rules for agents

A generic static-recompilation runtime for PS3 titles. This tree was forked from
GoWA-pc and hollowed out: the God of War specifics live behind a game profile.

## Milestone discipline

Every milestone is **one branch, one commit, one line in `docs/progress.md`
with its proof**. A milestone is done when its criterion is green and the proof
is a command and its output, not a description of what should happen.

Before starting a milestone, write the antecedents table: what a neighbour
project already learned about this problem, with file and line. `refs/` holds
symlinks to those projects, so search it with `grep -R` -- `grep -r` does not
follow symlinks and silently returns nothing.

## Never a fake success

An unimplemented syscall, NID or firmware import returns an explicit error and
logs its name. `CELL_OK` from something that did not run tells the caller its
state was created when it was not, and the failure then surfaces far from the
cause.

## Libraries are lifted, the kernel is rewritten

Sony's libraries are compiled from the user's own firmware by the pinned
lifter. Only the LV2 syscall surface is reimplemented, and it is generated from
the specification so coverage is a number rather than a claim.

## Generated and derived material is never committed

`.gitignore` and the pre-commit hook both refuse it: no key, no decrypted
image, no lifted source.

## Build resources

Cap parallel builds at `-j 8`. This machine is shared.

## Never transcribe a structure, enum or table from memory

When a header, a reference implementation or a vendor source is available, include
or read it. Do not retype field order, offsets, enum values or function
signatures from memory, and do not compute a vtable slot instead of naming the
member.

This is written down because it has cost this project three separate debugging
sessions, each presenting as a silent wrong result rather than an error:

- `CellVideoOutDeviceInfo` was reproduced as 0x3A0 bytes when the real structure
  is 0x1C8; the handler wrote 472 bytes past the caller's structure and the
  resulting crash appeared at a guest address several functions away.
- The RenderDoc API vtable was declared by hand with the wrong field count, so
  `TriggerCapture` resolved to a neighbouring slot and the call did nothing while
  the log reported a successful trigger.
- `CellUserInfoUserStat`, `CellPngDecThreadInParam` and the `l10n` functions all
  had their argument shapes guessed first and corrected only after a fault.

If a header is not installed, fetch or vendor it — that is what
`third_party/renderdoc/renderdoc_app.h` is for. A hand-written layout is a guess
that fails quietly.
