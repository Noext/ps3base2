# Retraction: the sleep is 250 ms, not 69 hours

## The error I made last round

I reported:

    sleep pc=0x003a18b0 r3=250000 r11=141 lr=0x0021c18c
    "r3=250000 is SECONDS -- 69 hours"

**Both readings were wrong.**

1. `r11=141` is `sys_timer_usleep`, which takes **microseconds**. It is not
   `sys_timer_sleep` (142, seconds). `runtime/src/lv2/process.cpp:113` and the spec
   table at `syscall_table.h:122` both say so.
2. `r3=250000` is therefore **250 milliseconds**, an ordinary poll interval.

Confirmed by timing: the syscall counter advances **4 per second** (570, 574, 578,
582, 586, 590 across six seconds) — exactly `1/0.25s`. A 69-hour sleep would have
shown one syscall and no further output.

## What the guest is actually doing

An entry probe on `f_0021c178` (the sleep wrapper) shows the real argument:

    [sleepprobe] #0 f_0021c178 r3=250 lr=0x21e268
    [sleepprobe] #1..5 f_0021c178 r3=250 lr=0x21e378

And the lift at `mc_021.cpp:1408-1416` is a **poll loop**:

    L_21e360:
        f_002d60f0(cleanup);
        f_002d63e4(cleanup);
        f_0021c178(250);                 /* usleep(250 ms) */
        call_import(143);
        if (!f_0021da50()) goto L_21e360;

`f_0021da50` is a one-line predicate (`mc_021.cpp:645`):

    ctx.gpr[4] = 0xCE0E20 + idx;
    ctx.gpr[3]  = ld8(base, ctx.gpr[4]);   /* return the byte */
    return;

**The guest polls `byte[0xCE0E20 + idx]` every 250 ms, waiting for it to become
non-zero.** That table is written by a subsystem this build never starts.

## What that changes

The blocker is not a corrupt argument, not a shutdown path, and not a 69-hour park.
It is a **polling wait on a state table that nothing in this build populates** —
which is the same class of problem as the RSX field two rounds ago, and it has the
same shape of answer: find what should write it, or accept that the module it lives
in is not part of a no-display build.

## Progress that is real and measured

The RSX stub from last round moved the boot a long way. `f_005d2b44` — the function
that crashed on a null `this` for four rounds — now runs **40 times with a real
object**:

    [entryprobe] #39 f_005d2b44 r3=0x130c3fc r4=0x41 lr=0x2dc3b8

`r3=0x130c3fc` replaces the `0x0` of round 13. That is the single clearest evidence
that the port crossed the RSX wall and is now executing the title's own rendering
setup.

## Honest position

Milestone 3 stays **open**: the menu is not reached. What this round produced is a
correction (the sleep is 250 ms), a precise identification of the wait
(`byte[0xCE0E20+idx]` polled at 4 Hz), and confirmation that the null-`this` failure
is gone.
