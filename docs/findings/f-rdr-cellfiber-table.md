# cellFiber: the real library against our HLE, function by function

**Measured 2026-11-13**, on `out-rdr/gen/reloc/libfiber.elf` (the relocated
firmware module RDR loads) and `runtime/src/hle/cellFiber.cpp`. The real code is
read from the **lifted** output, not from memory and not from a disassembly
listing that could disagree with what runs: every address below is a
`f_<addr>` in `out-rdr/ppu/libfiber_000.cpp`.

## Which functions RDR actually calls

From the three 900 s runs (`/tmp/rdr-crash-s2/run{1,2,3}/drv/run.log`), the HLE
log names exactly five entry points:

    16678  HLE CreateFiber
    16675  HLE JoinFiber
     7419  HLE RunFibers
        3  HLE InitializeScheduler
        3  HLE _cellFiberPpuInitialize

`Yield` and `Exit` never appear in that log **because their handlers contain no
LOG line** -- not because they are unused. Run 1's import trace proves Yield is
called: `cellFiberPpuYield (0x0,0x1e,0x0) = 0xffffffff80760009` repeating to the
last record at t=900.97 s. That is an observability gap in the handlers, not a
fact about the title.

## The import stubs, resolved through their descriptors

The import table gives a stub address, and the stub is an `.opd` whose first word
is the code address. Comparing a stub to a text bound would be the category error
`AGENTS.md` forbids; the descriptor is what names the code:

| export | stub | descriptor -> code | lifted |
|---|---|---|---|
| cellFiberPpuYield | 0x3600e930 | **0x360033dc** | yes |
| cellFiberPpuRunFibers | 0x3600e918 | **0x3600254c** | yes |
| cellFiberPpuJoinFiber | 0x3600e940 | **0x36003574** | yes |
| cellFiberPpuCreateFiber | 0x3600e950 | **0x36003be0** | yes |
| cellFiberPpuExit | 0x3600e948 | **0x36003b84** | yes |

## The table

| function | the real library | our HLE | deviation |
|---|---|---|---|
| **JoinFiber** | Validates, then **waits**: it polls the fibre's state word at `+772` in a loop (`L_36003704`), and only when that word says terminated does it read the exit status from `+792` and store it to the caller's `*status` (`st32 r31+0`). It also clears the fibre's state (`st32 r30+0, 0`). | Validates, then **does not wait**: if the fibre is not `Terminated` it logs `"still in state N (not yet terminated)"` and returns `CELL_OK` with `f.exit_status` -- a value the fibre never produced if it has not run. | **REAL.** The console blocks; we return success with a stale status. Measured 20 times in the three runs. |
| **Yield** | Reads the per-thread fibre context at `TOC-32712`; if it is `-1`, returns **`0x80760020` (NOSYSINIT)**. Otherwise, if the derived pointer is zero, returns **`0x80760009` (PERM)**. | Returns `0x80760009` (PERM) for **both** cases (`!st \|\| !st->initialized \|\| st->current < 0`). | **Real.** NOSYSINIT is never returned. The title cannot tell "no scheduler" from "not in a fibre". |
| **RunFibers** | Round-robin over the scheduler's fibre list until none is runnable, then returns. | Same shape, plus a guard: stops after **100 000 switches** and logs a warning. | **Deliberate, and it is a deviation.** The guard fired 16 times in the three runs. It prevents a livelock from hanging the process, but it also means we return before the real scheduler would. |
| **CreateFiber** | Allocates the guest fibre from the caller's stack and registers it. | Same, with real host `ucontext` fibres. | None measured. |
| **Exit** | Terminates the current fibre and returns to the scheduler. | Same. | None measured, but see JoinFiber: we never give Exit a chance to publish a status the join can read. |
| **Self** | Returns the fibre structure, **zero for a plain PPU thread** (RPCS3 `cellFiber.cpp:221`). | `ret(ctx, 0)`. | None. |
| **CheckStackLimit / SendSignal / WaitSignal** | Real implementations. | `ret(ctx, 0)` -- a **fake success**. | **Real, and against `AGENTS.md`.** These should name themselves and refuse, not claim CELL_OK. |
| **ContextRun / ContextSwitch / ContextRunScheduler / ContextEnterScheduler** | Real. | `ret(ctx, 0)` or unimplemented. | Not called by RDR in these runs; still a fake success. |

## Why this matters for the observed state

The title spins in `Yield` being told PERM forever. On the real library that is a
legitimate answer for a thread that is not inside a fibre -- but the real
`RunFibers` would have run the fibre to completion first, and the real
`JoinFiber` would have **waited** instead of returning. Our combination is:

1. `RunFibers` gives up after 100 000 switches (guard),
2. `JoinFiber` returns `CELL_OK` with a status for a fibre that is still
   `Suspended`,
3. the title, told the join succeeded, proceeds as if the work is done,
4. and the main thread ends up yielding outside any fibre, getting PERM.

That chain is a **diagnosis from the code and the log**, not a measured causal
proof. What is measured is: 20 `still in state 3` joins, 16 guard trips, and
PERM on every Yield at the end.

## The fix, and what it changes

`JoinFiber` must wait for the fibre to terminate, running the other runnable
fibres while it waits -- which is what the real code does by polling its state
word. Concretely: while the target fibre is not `Terminated`, run one
round-robin pass of the scheduler, then re-check. The status written to the
caller must be the fibre's own `exit_status`, which `Exit` publishes.

`Yield` must return `NOSYSINIT` when the scheduler was never initialised, and
`PERM` only when it was initialised but the thread is not in a fibre.

The `CheckStackLimit`/`SendSignal`/`WaitSignal`/`Context*` handlers must stop
returning a bare `CELL_OK`: they are not implemented, and `AGENTS.md` requires
them to name themselves and return an explicit error.
