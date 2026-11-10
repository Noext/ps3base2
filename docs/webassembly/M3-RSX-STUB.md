# The RSX wall is down; the guest now waits 250,000 seconds

## What the stub did

The prompt's point 6 for milestone 3 asks for exactly this: "le chemin d'affichage
est remplacé par un stub qui compte les commandes RSX". New
`runtime/src/gpu/rsx_no_display.cpp`, compiled under `PS3BASE_NO_RENDERER`
(which the wasm link defines), provides:

| symbol | behaviour |
|---|---|
| `gpu::rsx::method(reg, arg)` | counts the command by class (vp words, const writes, draws, clears) |
| `gpu::rsx::method_run(reg, args, n)` | consumes one word, keeping the FIFO pointer advancing |
| `gpu::rsx::sync_decodes()` | no-op: no textures to sync |
| `gpu::rsx::present(id)` | returns false: nothing is flipped |
| `gpu::rsx::zpass_value(ea)` | returns 0: not visible, so the guest skips rather than blocks |
| `gpu::set_display_buffer(id, off, pitch, w, h)` | records the buffer description the guest supplies |
| `gpu::native_window()` | returns null |
| `gpu::init()` | returns true, so RSX setup completes |
| `gpu::flip(id)` | no-op |

The renderer keeps every one of these when `PS3BASE_NO_RENDERER` is not defined,
so a graphical build is unchanged (native: **0 errors**).

## The effect, measured

    before: pc=0x01307a08 (libgcm_sys)  syscalls=192167  -- spinning on an RSX field
    after:  pc=0x003a18b0 (game code)   syscalls=896     -- waiting on a sleep

**The PC moved out of `libgcm_sys` and into the title's own code**, and the syscall
rate collapsed from 6,400/s to under 1/s. The RSX wait loop is gone.

## The new wait, measured

    sleep pc=0x003a18b0 r3=250000 r11=141 lr=0x0021c18c

`r11=141` is `sys_timer_sleep`, and `r3=250000` is **seconds** — 69 hours. The
lift at `mc_036.cpp:13008` multiplies by 1000 and calls it:

    ctx.gpr[3] = ctx.gpr[3] * 1000;
    ctx.gpr[11] = 141;  syscall();

`ppu::time_scale()` returns 1.0 here, so the divisor is not the cause: the guest
genuinely asks for 250,000 seconds. The caller is `f_0021c178`, which computes the
duration.

## What that means

This is **not** a port bug. A 69-hour sleep from game code is what a title does when
it has decided something will never happen and parks the thread — a graceful
shutdown path, or a wait for an event this build never delivers. Either way, the
line the milestone needs is not reached.

Two candidates, both plausible and both testable:

1. the sleep is a shutdown/error path, and something earlier in the boot already
   failed silently (an HLE returning a code the title treats as fatal);
2. the wait is for a real event (a save-file operation, a system callback) that the
   stub layer does not deliver.

## Honest position

Milestone 3 stays **open**. What this round achieved is concrete and measured: the
RSX command path no longer blocks the boot, the guest now reaches its own code, and
the next blocker is identified precisely enough to attack. It is **not** the menu,
and I am not calling it the menu.

Compile-time evidence for the report, from the earlier rounds and still current:
module 91,394,071 bytes; worst function 2.2 MB of source → 1.4 MB of wasm, proven to
run; 268/268 lifted units and 17/18 + 9/18 subdirectory sources compile.
