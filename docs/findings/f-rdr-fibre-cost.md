# The fibre cost: what was measured, and what was not

**Measured 2026-11-13**, on `build-rdr/ps3base`, one instance at a time under
`/var/lock/ps3-machine.lock`.

## The leak, proved

The old `CreateFiber` malloc'd 256 KB per fibre and freed it only on the
`getcontext` failure path. RSS could not show it -- an untouched stack is not
resident -- so the measurement is **VmSize against the fibre count**, sampled at
1 Hz from `/proc/<pid>/status`.

| binary | window | fibres | VmSize | per fibre |
|---|---|---|---|---|
| old (malloc per fibre) | 145 s | 117 → 3640 | 13 304 368 → 14 224 944 kB (**+899 MB**) | **261.3 KB** |
| new (stack pool) | 165 s | 116 → 3849 | 13 304 908 → 13 373 004 kB (+66 MB) | **18.2 KB** |

261.3 KB per fibre against a 256 KB stack is the allocation overhead on top of
the stack itself: that is the leak, named to the kilobyte. On the new binary the
residual 18.2 KB per fibre is the ordinary heap the guest allocates anyway -- RSS
grows 128 MB on **both** sides, which is the guest's own memory, not stacks.

On the new binary VmSize is **exactly constant** (13 304 908 kB) from 245 to 2013
fibres, which is the direct signature of a pool that reuses instead of allocating.

## The three other fixes

- **`ThreadState*` was unstable.** It was `&g_threads.back().second` into a
  `std::vector`, so a second guest thread creating fibres reallocates the vector
  and dangles every pointer in flight. Now held by `shared_ptr`, never moves.
  RDR creates fibres on one thread only, so this never fired here; it was a
  latent use-after-realloc.
- **A switch is no longer a syscall.** `swapcontext` saves and restores the
  signal mask, which is two `sigprocmask` syscalls per switch. At the measured
  9 098 switches/s that is ~18 000 syscalls/s on a mask the runtime never changes
  across a switch. The first entry into a fibre still uses `swapcontext`
  (`makecontext` set up its stack and only `swapcontext` can start it there);
  every later switch is `_setjmp`/`_longjmp`.
- **`yield_to_scheduler` was marked `[[noreturn]]`** although it returns when the
  scheduler resumes the fibre. The compiler warned; the attribute would have let
  it optimise away the code after the yield.

## What the frame rate did: nothing, and that was predictable

A same-minute A/B of the two binaries on the green screen:

| | frame rate on the green |
|---|---|
| old | 0.99 – 1.12 fps |
| new | 1.20 – 1.25 fps |

That difference is inside this machine's spread and is **not** evidence of an
improvement. It was never going to be: `RESULTS-PERF.md` measures
`hle::run_runnable_fibers` at **0.53% inclusive**, so the fibre machinery was
never where the time went. The time is in the SPU interpreter's per-block
bookkeeping (~33% of samples) while the guest main thread sits at 7.2% of a
615% CPU load.

**So the honest statement is: the leak is fixed and proved, the switch no longer
makes a syscall, and the frame rate did not move because the profile said it
would not.** Fixing the fps means fixing the SPU interpreter's bookkeeping, which
is a different piece of work.

## The bug I introduced and caught

The first version of the `_setjmp` change set `st->scheduler_jmp` only on the
non-first switch path. The very first `Yield` then `_longjmp`'d into an
**uninitialised `jmp_buf`** and the process died at the first `CreateFiber`
(exit -11 at 118.8 s, right after `HLE CreateFiber #0`). Fixed by setting
`scheduler_jmp` before either path. This is recorded because a fibre change that
"looks right" and kills the title at the first switch is exactly the failure mode
the comment on `ThreadState` warns about.
