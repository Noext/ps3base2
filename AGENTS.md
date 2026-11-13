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

## An implemented syscall answers what the console answers

"Never a fake success" is about functions that are **not implemented**: those
return an explicit error and log their name, so the failure is read where it
happens. It is not a licence to refuse work the console performs.

For anything implemented, the target is the console's behaviour, which in
practice means RPCS3's: the same return code, the same values written into the
caller's output structures, the same "offline" state for the network. Success is
a legitimate answer when the console succeeds.

This is written down because getting it backwards cost a session. `sys_fs_chmod`,
`sys_fs_utime`, `sys_net_infoctl` and `sys_net_bnet_sysctl` were made to refuse
things RPCS3 accepts: chmod returned `CELL_EACCES` when the host refused a mode
the console ignores, and the net calls returned `CELL_ENOTSUP` where RPCS3
returns `CELL_OK`. libnet then initialised -- it was told the socket layer was up
-- and the title waited on a stack nobody had built. It went from 7081 frame
flips to zero, and the failure surfaced minutes later with no connection to the
cause.

The rule for a handler is therefore:

1. Read the RPCS3 implementation. Its return codes, its writes into guest
   structures and its side effects are the specification.
2. Implement that. If RPCS3 only validates and returns `CELL_OK`, validate and
   return `CELL_OK`.
3. Reserve explicit refusals for what genuinely is not implemented, and name it
   in the log.


## runtime/src/jobsys/ and runtime/src/spu/edge/ are linked into every binary

Never redefine their symbols in a stub file.

`GOWA_RUNTIME_SOURCES` globs `runtime/src/jobsys/*.cpp` and
`runtime/src/spu/edge/*.cpp` unconditionally, for every title. A symbol defined
in `runtime/src/gowa_stubs.cpp` "because this title links no job system" is
therefore always a duplicate, and it breaks **all three** builds at link time:

    multiple definition of `jsedge::enabled()`
    multiple definition of `edge::gpu::g_active`

This has happened four times: `edge::gpu::g_active`, `jsedge::spu_idle`,
`jsedge::enabled/init/on_wait_previous/on_set_priorities/set_prev_guard`. Every
one was added on the same false premise. If a title genuinely must not link a
directory, exclude it in `CMakeLists.txt` -- do not satisfy the symbol twice.

`tools/check_no_dup_symbols.sh` checks the six symbols that have actually caused
this and exits non-zero when one reappears. Run it after touching
`gowa_stubs.cpp`.


## The merge queue: rebase, ff-only, one at a time

Three times in one session an agent merged work that silently deleted another
agent's files. Each had branched before the other landed, so its diff carried a
removal it never intended: `tests/spu_dma_list.cpp`, `tests/spu_channel_conformance.cpp`,
and the DMA-list header, all deleted by an agent that had simply never seen them.

The rule is therefore mechanical, not judgement:

1. **Rebase before every commit that lands on main, and again just before
   declaring done.** `git fetch && git rebase origin/main`. An agent's branch is
   never allowed to be behind main at the moment it reports.
2. **The agent's tests and everyone else's must pass after the rebase.** A green
   tree before the rebase is not evidence; the combination is what lands.
3. **Merges are ff-only and serialised.** `git merge --ff-only`, one branch at a
   time, under a lock. No merge commits, no octopus merges, no resolving a
   conflict by hand at merge time -- if `--ff-only` refuses, the branch is stale
   and goes back to be rebased.
4. **The verifier rejects two shapes outright:** a branch whose diff deletes a
   file it did not create, and a branch whose diff modifies no source file. Both
   are evidence the branch was built against a tree that no longer exists.

This is written down because the alternative -- reading each diff by eye at merge
time -- failed three times while the diff was being read.

## One subagent at a time, one game at a time, one compile at a time

This is the permanent organisation of a working session. It is mechanical, like
the merge queue above, because the alternative -- several agents in flight and a
report written from memory -- has already produced a false report.

1. **One subagent at a time.** Launch one, wait for its status `done` or
   `blocked`, read its `RESULTS.md` **in full**, have the work verified and
   merged, and only then launch the next. Never two in parallel.
2. **One game instance at a time on the machine**, under a lock, bounded in
   duration, killed by the agent that started it.
3. **At most one compilation at a time.**
4. **One report per turn**, at the end, after the last subagent. No questions in
   between: when a choice appears, take the one that produces a measurement and
   write down that you took it.
5. **Never conclude anything about a subagent without having read its report.**
   A status file is not a result; `done` is a claim the report has to support.
6. **Never compare a descriptor address to a code bound.** A descriptor is data
   that *contains* the code address. `0x3600e930` is an `.opd` in libfiber's
   data segment, not unlifted code; comparing it to `text_hi` produced a whole
   finding that had to be retracted (docs/findings/f-rdr-fiber-fix.md).

## Measurement discipline

Every comparison is an **A/B on the same machine, in the same minute**. Two runs
minutes apart on a shared machine are not comparable; that error produced a false
regression report once already.

- Check `cut -d' ' -f1-3 /proc/loadavg` before a flip measurement and record it.
- Watch **RAM**, not just CPU and disk: an analysis script once took 46 GB and
  saturated the machine. Cap every Python script:
  `ulimit -v 2000000 && timeout 60 python3 <script>`.
- Delete nothing outside your own scope. `/root/Dev/wasm-base`,
  `/root/Dev/t-T1`, `/root/Dev/t-T2`, `/root/Dev/t-V` and their branches are
  not ours.
