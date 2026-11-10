# The FIFO thread starts, and the wall behind it

## The experiment

Having restored `vblank_thread` without effect, I started `fifo_thread` too, with a
plain `std::thread` instead of the 32 MB `pthread_attr_setstacksize` request:

    std::thread(fifo_thread).detach();

**It starts.** That is the first time the FIFO drain has run in wasm. And the guest
immediately asks for the next thing:

    Aborted(missing function: _ZN3gpu3rsx6methodEjj)

## What that means

`gpu::rsx::method(uint32_t reg, uint32_t arg)` (`rsx_renderer.cpp:4166`) is the RSX
command interpreter: 200+ lines of `switch` over register writes — transform
program words, transform constants, inline arrays, draw ranges, array elements. It
does **not** call Vulkan. It writes into `g_regs`, `g_vp_constants` and the draw-range
list.

So the wall is no longer a missing library or a stalled thread. It is: **the FIFO
thread needs the RSX command interpreter, which lives in the renderer file only
because that is where it was written.**

## The one instability, and why I am not papering over it

The FIFO thread is the one that *draws*. In a no-display build, letting it run
means it will consume command buffers whose output nobody consumes, and the
guest's flip counter will advance against a surface that does not exist. That is
progressive work — but the milestone's criterion is the **menu**, which the title
renders, so a build that pretends to draw is not closer to the gate than one that
does not.

The honest options, in order:

1. **Extract the RSX command interpreter** (`method`, `method_run`, `vp_word`,
   `immediate_method` and the register file they share) into a
   `gpu/rsx_state.cpp` with no Vulkan, exactly as `special_methods` and `registers`
   were extracted. That unblocks the FIFO drain and is mechanical.
2. **Wire the interpreter to the wgpu backend** — the real milestone-2 work, which
   would make the menu reachable rather than simulated.

Option 1 is what the next round should do: it is bounded, it is measurable, and it
is the last structural split between "the renderer file" and "the RSX state that
the guest manipulates directly".

## State of the boot, measured

| | |
|---|---|
| firmware libraries loaded | **6** |
| PRX modules started | **24** |
| RSX context / local mem / IO map | allocated (`0x40100000`, `0xf900000`, `0x30100000`) |
| syscalls in 30 s | **192,616** |
| crashes | **0** |
| main menu reached | **no** |

Milestone 3 remains **open**. The gate is the menu; this round moved the blocker
from "missing firmware" to "the RSX interpreter is in the wrong file", which is a
smaller and more concrete problem, but it is not the gate.
