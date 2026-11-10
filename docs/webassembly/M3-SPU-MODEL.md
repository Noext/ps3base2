# The SPU execution model: the exact seam

## Where the threads are created

`runtime/src/spu/thread.cpp:1292`:

    void SpuThread::start() {
        ...                                   /* set up ctx, LS, pc, args */
        running = true;
        std::thread([this] { run(); }).detach();   /* one host thread per SPU */
    }

That single line is the entire problem for a browser build. Every SPU the title
creates costs one host thread, and Emscripten's pthread pool is fixed at build time
(`-sPTHREAD_POOL_SIZE=4` here). The four PPU threads already in the dump
(`main_thread`, `MinecraftSpursHdlr0/1`, `_gcm_intr_thread`) consume it.

## What the fix has to change, and what it does not

`SpuThread::run()` is the interpreter loop. It does not need to be rewritten — it
needs to be **driven** differently. The two options, both named in the prompt:

**A. Cooperative pool.** A fixed set of host threads pulls runnable SPUs from a
queue. Cost: preemption must be added inside `run()` at safe points (SPU code is
statically compiled, so block boundaries are known), and every `wait` must yield the
host thread instead of blocking it.

**B. Unwind-on-switch**, which `ps2-web-recomp` uses and the prompt recommends: a
single host thread runs whichever SPU is runnable. Switching away means unwinding
the host stack (a `setjmp`/`longjmp` pair per SPU) and resuming another. The SPU
context already lives in `SpuThread` (`ctx`, `ls`, `pc`), so nothing about the
machine state needs to move — only the host call stack.

B is the smaller change here: the SPU state is already externalised into `SpuThread`,
so an unwind is a matter of `jmp_buf` per thread plus a scheduler tick. The
`g_rsx`/`guest` side is untouched.

## Why this is the last structural item

Everything above it now works: images load, the LV2 boot runs, the PPU scheduler
creates four threads, the RSX context is allocated, the RSX command path is stubbed,
and the guest executes its own code with zero crashes. What is missing is that the
SPU half of the machine never executes, so the SPURS event queues stay empty and the
PPU handlers park.

## The honest position on the gate

The prompt says milestone 3 is the gate and the project may fail here. **It has not
been reached, and reaching it requires this SPU model.** I am not going to claim the
gate is passed; what this campaign has produced is every measurement up to the gate,
the gate's precise cause, and the two documented routes through it.
