# A diagnostics attempt that broke the boot, and the corrected baseline

## The negative result

I tried to instrument the RSX stub with a periodic report of the commands it sees, so the
"3k/s" figure in the RSX log could be attributed to the guest rather than to the counter:

    void no_display_report() { LOG_INFO("rsxstub", ...); }   /* called from the wait loop */
    ...

**The module stopped producing any output at all.** No boot lines, no error, exit code 0 —
a silent abort during initialisation, before logging worked. Reverting both edits restored
the boot immediately.

The likely cause is calling `LOG_INFO` from the RSX FIFO thread while the runtime is still
initialising that subsystem. I did not chase it further: the diagnostic was optional and the
cost was a broken build. **Recorded so it is not repeated.**

## The finding that matters from this round

While preparing that diagnostic I checked the native baseline I have been comparing against
since round 25, and it had **crashed**:

    E crash: signal 11 at ... (std::ostream::sentry::sentry(...))
    W blackbox: capture #0: crash

`std::ostream::sentry` is the decompiler stack-overflow the code documents itself. **That
run never reached the menu.** Several "native reaches X, wasm does not" comparisons in
recent rounds were therefore made against a run that had failed in its own way.

## The valid baseline

`/tmp/cmp.log` is a native session that reached the world: 34 `UserAutoSave` markers, a
9,126-second run, real rendering load.

| | valid native | wasm |
|---|---|---|
| `draw` rate | **59–65 k/s** | **3 k/s** |
| `flip` share | 29–30 % | 93 % |
| `UserAutoSave` | **yes, repeatedly** | **no** |

The wasm build draws about **20× less per second** and has never written a world save. Its
FIFO is 93 % flip, i.e. it presents frames that are mostly empty.

## Honest position

Milestone 3 remains **open**, and this round produced **no forward progress**: one
diagnostic attempt that broke the build (reverted), and one correction to the baseline I
measure against.

That correction is worth having. The remaining gap is now stated against a run that
actually works: 3k vs 60k draws per second, no world save, 93 % flips — a
rendering-pipeline difference, not a scheduling one.
