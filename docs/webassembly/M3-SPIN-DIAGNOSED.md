# The spin: the guest waits on a flip counter it never seeds

## The measurement

A compile-time probe inside the wait loop prints the object and the two values the
comparison uses (an environment variable cannot: `getenv` is inert under wasm):

    spin obj=0x40100040 cur=0 target=4294967295

and the surrounding lift, from `out/ppu/libgcm_sys_000.cpp:10196-10204`:

    L_1307a00:
        ctx.gpr[3]  = 30;                              /* 30 microseconds */
        ctx.cia     = 0x1307a08; ppu::syscall(ctx);    /* 141 sys_timer_usleep */
        ctx.gpr[29] = ld32(base, ctx.gpr[30] + 8);     /* cur */
        cr_set(7, r31, r29);
        if (target != cur) goto L_1307a00;

So the guest does:

    while (*(uint32_t*)(obj + 8) != 0xFFFFFFFF) usleep(30);

`obj` is `0x40100040`, which is inside the **RSX context** region
(`0x40000000..0x50000000`, allocated at `0x40100000` — confirmed by
`rsx: context at 0x40100000 (driver 0x40200000 reports 0x40300000)`).

**`cur` is 0 and never changes; `target` is `0xFFFFFFFF`.** The guest is waiting for
a counter to *reach its terminal value*, which means it is waiting for something
that has already finished — the sentinel is what you compare against when a
transfer is complete, not a value that arrives.

## Why the vblank thread did not fix it

I restored `vblank_thread` under wasm this round, on the theory that the guest was
polling `g_rsx.flips` (which that thread increments). The thread now starts —

    W rsx: fifo thread skipped, vblank thread started (no renderer in this build)

— and the guest still spins. So the counter it waits on is **not** `g_rsx.flips`; it
is a field of the RSX context structure that the *FIFO* thread would write as it
processed command buffers.

The FIFO thread is the one still skipped: it asks for a 32 MB pthread stack
(`pthread_attr_setstacksize`) because the vertex decompiler builds multi-megabyte
GLSL strings, and that aborts in the browser.

## The honest conclusion for this round

The wasm boot now:

- loads 6 firmware libraries and starts **24 PRX modules**;
- allocates the RSX context, local memory (`0xf900000` bytes at `0x50000000`) and
  the IO map;
- runs **192k syscalls / 30 s**, of which **99 % are `sys_timer_usleep`** — the wait
  loop above;
- does not crash.

It does **not** reach the main menu, and the blocker is now a single, precisely
identified wait: the guest polls an RSX-context field that only the FIFO thread
would advance, and the FIFO thread cannot start in a browser as written.

## What the next step has to be

Two options, both real work:

1. **Give the FIFO thread a smaller stack.** The 32 MB request is a workaround for
   the vertex decompiler; a bounded decompiler buffer plus an ordinary stack may
   suffice. This is the option that makes the real path work.
2. **Run the FIFO drain cooperatively** on the main thread between guest syscalls,
   which is what `ps2-web-recomp` does with its unwind-on-switch technique.

Either way, the answer is no longer "which object is missing" — it is "how does
this build run a thread that the browser cannot give a 32 MB stack".
