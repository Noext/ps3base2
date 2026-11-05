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
