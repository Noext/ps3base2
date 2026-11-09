# Where the time goes during world generation

`perf record -F 199 -g` on the emulator through a full seeded generation that
reached a save (world created, 0 crashes). 82 MB of samples, 1 000 recorded
cycles samples, ~13.46 billion cycles attributed.

## CPU time per thread

| thread | share | what it is |
|---|---|---|
| `Window` | **67.7%** | the RSX/presentation thread: SDL, X11, Xvfb round-trips, kernel poll/seccomp |
| `[vkps] Update` | 17.7% | the NVIDIA Vulkan driver's update thread |
| `ps3base` | 11.1% | the emulator's own PPU work |
| `SPU 0..4 Minecraft` | ~15.4% each (of their own samples) | the five SPU threads |
| `[vkcf] Analysis` | 2.7% | NVIDIA shader compiler |

The single largest consumer is **not** emulation: it is the presentation path
paying for X11/Xvfb round-trips and driver synchronisation.

## Top functions inside the emulator

| self | thread | function |
|---|---|---|
| **4.27%** | PPU (4J) Post p | `rsv::store_range` |
| 1.88% | PPU main_thread | `rsv::store_range` |
| 0.81% + 0.80% + 0.79% + 0.79% + 0.78% | each SPU thread | `rsv::wait_lost_impl` |
| 0.51% | PPU (4J) Server | `rsv::store_range` |
| 0.24% | PPU (4J) Post p | `cw::note_hit` (codewatch) |
| 0.21% | PPU (4J) Post p | `f_005e8df4_body` |
| ~0.13% each | SPU threads | `spu::SpuThread::exec` |

Outside the emulator, in the same profile: `entry_SYSRETQ_unsafe_stack` 3.68%,
`nvidia osDevReadReg032` 3.34%, `pthread_mutex_lock` 2.15%,
`pthread_mutex_unlock` 1.90%, `do_sys_poll` 1.42%.

## The three most profitable optimisations, in order

### 1. Stop paying for the presentation path (biggest, ~67% of the profile)

The `Window` thread spends its time in `X11_PumpEvents`, `X11_WaitEventTimeout`,
`XCheckIfEvent`, `SDL_UDEV_Poll`, `dbus_connection_ref`, `__poll` and
`__libc_recvmsg` -- all of it polling for events that a headless emulator does
not need. Because the display is Xvfb, every one of these is a full X11
round-trip. `SDL_UDEV_Poll` and dbus are pure overhead with no input devices.

Fix: when no real display is attached, drive the presentation from a timer and
skip udev/dbus event polling entirely; batch X11 flushes. This is measurable and
low-risk because it changes nothing about emulated behaviour.

### 2. Make `rsv::store_range` cheaper on the hot PPU path (~7% combined)

It is the single hottest emulator function, 4.27% in the post-processing thread
alone, plus 1.88% on main and 0.51% on the server. It runs on **every** guest
store, and its job is a range check plus a codewatch test. Two of those are
avoidable: hoist the codewatch `g_on` test out of the per-store path (the
profile shows `cw::note_hit` still costing 0.24% even when the watch is off), and
cache the page-reservation lookup instead of recomputing it per call.

### 3. Reduce `rsv::wait_lost_impl` on the SPU threads (~4% combined)

Each of the five SPU threads spends about 0.8% there, and it is their single
largest emulator-side cost. It is the lost-reservation retry path; if the common
case is 'no wait needed', the branch should be decided before entering a
function that touches the reservation table.

## What this profile does not say

- It was taken with Xvfb present, which inflates the `Window` thread. On a real
  GPU-backed display that share would be smaller, so optimisation 1 must be
  re-measured there before its value is claimed.
- The generation completed under profiling, but the samples include the menu
  phase; a phase-filtered profile would sharpen the percentages.