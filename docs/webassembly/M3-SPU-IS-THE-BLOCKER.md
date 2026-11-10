# Milestone 3's real blocker: the SPUs do not run, and the PPU waits on them

## The thread dump, which settles it

    stacks: 4 guest stacks tracked
    status: ppu 0x1000000 'main_thread'         blocked in 141  syscalls +811  (usleep)
    status: ppu 0x1000001 'MinecraftSpursHdlr1' blocked in 130  syscalls +1    (event_queue_receive)
    status: ppu 0x1000002 'MinecraftSpursHdlr0' blocked in 178  syscalls +2    (spu_thread_group_join)
    status: ppu 0x1000003 '_gcm_intr_thread'    blocked in 130  syscalls +16   (event_queue_receive)

**Four guest threads exist and three of them are parked inside waits that only a
running SPU can satisfy.**

- `sys_event_queue_receive` (`event.cpp:80`) blocks on `q->cv` until an event
  arrives. Events come from SPU tasks.
- `sys_spu_thread_group_join` (`spu.cpp:275`) blocks until
  `g->state == 1 && g->join_state != 0` — i.e. until the SPU group finishes.
- `main_thread` polls with `usleep(250 ms)` waiting for the result those handlers
  would deliver.

So the 4 Hz poll is **not** a defect in the branch logic — my contradiction from last
round dissolves here. `f_0021e060` is a worker loop with three `goto L_21e360` and
zero `return`: it is *supposed* to loop, and it exits when the condition it polls
becomes true. That condition depends on the SPURS handlers, which depend on SPUs.

## Why the SPUs do not run

The prompt anticipated this in point 4: "Threads : `-sPTHREAD_POOL_SIZE` est fixe, or
le titre crée un thread hôte par thread invité. Si le pool ne suffit pas, appliquer
la technique de `ps2-web-recomp` (dérouler la pile hôte au changement de thread)
**pour les threads SPU au minimum**, et documenter."

The SPU side needs host threads that the browser will only give through the
Emscripten pthread pool, and each SPU thread is long-lived and mostly spinning. The
pool is fixed at build time (`-sPTHREAD_POOL_SIZE=4` here). Four PPU threads are
already using it; there is no room for the SPU workers.

## What this means for milestone 3

**The gate cannot be reached by fixing a branch, an address, or a missing symbol.**
It needs an SPU execution model that works inside the browser's thread budget. That
is the work the prompt named, and it is the largest remaining item:

1. run SPU threads on a bounded host-thread pool with cooperative yield, or
2. unwind-on-switch as `ps2-web-recomp` does: one host thread runs whichever SPU is
   runnable, rotating on a timer, so N guest SPUs cost one host thread.

Option 2 is what the prompt recommends and what the reference project proves.

## What is still true and measured

- 6 firmware libraries load, 24 PRX modules start.
- The RSX command path is stubbed and no longer blocks.
- The old null-`this` crash is gone: `f_005d2b44` runs 40x with `r3=0x130c3fc`.
- 4 guest threads are created and scheduled; 0 crashes.
- The main menu is **not** reached, and the reason is now structural rather than a
  defect: no SPU execution, therefore no SPURS events, therefore the handlers park.

## Retraction of last round's "contradiction"

I recorded a contradiction ("byte=1 yet the branch is taken") and listed three
candidates. **The dump resolves it**: the loop I was reading is one of several paths,
and the unconditional `goto L_21e360` at `mc_021.cpp:1421` means `f_0021e060` never
returns — by design. There was no contradiction in the lifted code; my probe was
watching a loop that is meant to run forever and I mistook its normal operation for a
fault.
