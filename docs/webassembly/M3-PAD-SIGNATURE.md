# Two real bugs, and the boot goes from 3 threads to 16

## The mistake I had been making

The wasm entry's wait loop ran **30 iterations**. The native run needs **86 seconds** to
reach the world. Every "the boot stalls at X" conclusion in the recent rounds was drawn
inside a window shorter than the boot itself.

Extended to 200 s, the boot immediately went further and exposed two real defects.

## Bug 1: `host::poll_pad` had the wrong return type

The longer window let the guest reach `cellPadGetData`, which calls
`host::poll_pad(uint16_t*)`. The wasm build links my `host_input_port.cpp` stub (the SDL3
`host_input.cpp` cannot build in the browser), and my stub declared:

    bool poll_pad(uint16_t* b);

The real one, `host_input.cpp:80`, is:

    void poll_pad(uint16_t* b);

A mismatched return type in a wasm function is not a compile error across translation
units — it is a **`signature_mismatch` trap at the call site**. Emscripten named it
exactly:

    RuntimeError: unreachable
    at ps3dbg2.wasm.signature_mismatch:host::poll_pad(unsigned short*)
    at hle_sys_io_cellPadGetData(...)

Every run since the pad stub was written died there as soon as the guest polled input —
which it only does far enough into the boot that the 30 s window never reached it.

## Bug 2: the wait window itself

30 iterations of 1 s is not a boot budget. It is a sample. The native reference takes
86 s; anything shorter cannot distinguish "stalled" from "still working", and I drew
several conclusions inside that blind spot.

## The result, measured

| | before this round | after |
|---|---|---|
| PPU threads created | **3** | **16** |
| syscalls at t=30 s | 8,503 | **32,022** |
| syscalls at t=90 s | — | **63,332** |
| `main_thread` pc | pinned at `0x01401ee4` | moving, settles at `0x004e2064` |

Threads that now appear and did not before: `thread_options_load`,
`(4J) OpenStreamThreadProc`, and `MountInstalledDLC_thread` (five instances).

**The game is mounting its DLC and loading its option files.** That is well past asset
loading and into normal start-up.

## Honest position

Milestone 3 is **not** reached — the menu is not up. But this round found a genuine defect
that had been silently killing the boot at the first input poll, and it removed a
measurement error that had been distorting my conclusions for several rounds.

Both are the kind of thing that only shows up when the window is long enough and the
probe is precise.
