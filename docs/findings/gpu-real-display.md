# Running the game on the real NVIDIA GPU

## What was already true, and what was not

Every previous run in this campaign was **already** presenting on the NVIDIA
GPU through Vulkan -- the log line says so on each boot:

    gpu: Vulkan presenter on NVIDIA GeForce RTX 4060 Laptop GPU, swapchain 1208x680

What was wrong was the **X server**, not the renderer: those runs presented into
an Xvfb. Xvfb has no GPU, so every present is a software copy plus X11 round-trip,
which is why the measured `Window` thread ate 67% of the profile and why in-game
fps sat at 9-23.

## The display used now

`DISPLAY=:0` is a real X.Org 21.1.11 server with `nvidia_icd.json` present among
the Vulkan ICDs, and the boot log confirms the presenter:

    gpu: Vulkan presenter on NVIDIA GeForce RTX 4060 Laptop GPU, swapchain 1280x720

## Measured, same build, same disc

| phase | Xvfb | real NVIDIA display |
|---|---|---|
| world generation | 60 fps (locked, CPU-bound) | **60 fps, 16.7 ms** |
| in the world | 9-23 fps | **28-33 fps, 30-56 ms** |

The generation phase was already at the 60 fps cap under Xvfb, so it shows no
difference. The world itself is where the presentation cost was hiding: **2-3x**
more fps on the real display.

## What this does to the earlier profile

The profile in `profile-generation.md` was taken under Xvfb and attributed 67% of
CPU to the `Window` thread: SDL/X11 event polling, `X11_PumpEvents`,
`dbus_connection_ref`, `__poll`, `__libc_recvmsg`. **That share is Xvfb's cost, not
the emulator's.** Optimisation 1 in that report ("stop paying for headless
presentation event polling") is therefore a statement about the headless path
only, and must be re-profiled on `:0` before any value is claimed for it.

## Commands

```bash
DISPLAY=:0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
  PS3BASE_VFS_DIR=vfs-gpu tools/play.sh --no-build
```

`tools/play.sh` defaults to whatever `DISPLAY` says; pass `--display :0`
explicitly if the environment has something else set.
