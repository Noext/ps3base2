# Same work, same phase, different thread count — and the timing says why

## The exact comparison

`_sys_ppu_thread_create` calls, both runs:

| thread | native | wasm |
|---|---|---|
| `MinecraftSpursHdlr1` | yes | **yes** |
| `MinecraftSpursHdlr0` | yes | **yes** |
| `_gcm_intr_thread` | yes | **yes** |
| `trophy_init_thread` | yes, **t=70.4 s** | no |
| `trophy_event_thread` | yes | no |
| `(4J) Tile update` | yes | no |
| `MSSAsync` / `MSSTimer` | yes | no |
| `(4J) Chunk update`, `Basic Event Handler`, `thread_options_load`, `OpenStreamThreadProc` | yes | no |

**The three threads wasm creates, it creates correctly** — same names, same entry
points, same arguments. There is nothing wrong with thread creation.

The fourth native thread appears at **t=70.4 s**. The wasm run is sampled at t=5 s.

## The RSX phase is identical

Immediately after `_gcm_intr_thread`, both runs execute the same GCM sequence:

    cellVideoOutConfigure res 2 format 0 aspect 1 pitch 5120
    rsx: context attribute 0x101 a3 0x1 a4 0x2 ...
    rsx: context attribute 0x300 a3 0x0 a4 0x3100270000 a5 0x14006404e000 ...
    rsx: context attribute 0x301 a3 0x0 a4 0x2105000200 a5 0xff80100400202000 ...

The wasm run reaches this at **t=0.96 s**, the native at **t=0.72 s** — faster, if
anything.

## So where do they diverge

After the RSX attributes, the wasm `main_thread` stays at `pc=0x003a18b0` with the
syscall counter crawling (750 → 856 over 25 s). The native run spends the next
**70 seconds** doing work that produces no log lines, then creates the trophy thread.

That 70-second gap is the thing to explain, and it is not a deadlock: native is
*doing something* for 70 s. The wasm run is polling at 4 Hz and doing nothing.

## What I now believe, stated as a hypothesis to test

The wasm `main_thread` entered `f_0021e060` — a worker loop — because the thread that
*should* be the game's main thread never got created. Everything up to and including the
RSX setup runs on the wrong thread, which is why it then sits in a worker loop that
expects another thread to be driving.

If that is right, the missing piece is a thread creation the guest performs through a
path that wasm does not take — not a missing module, not a missing SPU, and not a
throughput problem.

## Honest position

Milestone 3 remains **open**. This round produced the sharpest statement of the gap so
far: *three identical threads created, RSX initialised identically and faster, then the
wasm main thread parks in a worker loop while native spends 70 s doing unlogged work.*
The next measurement is the 70-second gap itself: what native executes there, since it
is exactly the work wasm never reaches.
