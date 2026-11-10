# Two real bugs found while chasing the libfs wait

## Bug 1: `ppu::hook_flag_for` was never linked, and the entry called it

The wasm entry printed the hook flag of `f_005d2b44`:

    ppu::hook_flags[0x5d2b44 >> 2]

Both halves of that are wrong:

1. **`hook_flags` is `extern uint8_t* const[]`** — an array of *per-image pointers*,
   indexed by image then by word offset (`ppu_ops.cpp:175`). Reading it as a flat
   byte array reads a pointer value as a flag.
2. **`ppu::hook_flag_for` is not linked into the wasm build.** Emscripten generated a
   stub for it, so calling it reached
   `abort("missing function: _ZN3ppu13hook_flag_forEj")`.

That abort killed every run after `WASM-BOOT-IMAGES-LOADED`, which I initially
misread as "the boot stops at image load". The real cause was a diagnostic line in my
own entry doing an invalid read.

## Bug 2: the file provider's mount prefix

The provider rewrote loader paths to `/game/<path>`, but `--preload-file /tmp/disk@/`
mounts at the root, so the guest's `/dev_bdvd/...` resolves to `/<path>`. With the
wrong prefix every asset open failed with ENOENT and the boot fell back to three
threads, exactly as before the assets were added.

Fixed: the provider prefixes `/`, matching the mount.

## The state now, measured

    fs: open '/dev_bdvd/PS3_GAME/USRDIR/Common/Media/MediaPS3.arc' -> ok
    _sys_ppu_thread_create 'trophy_event_thread' -> ...
    _sys_ppu_thread_create '(4J) Tile update' -> ...
    _sys_ppu_thread_create 'MSSAsync' -> ...
    t=30s pc=0x01401ee4 syscalls=8503

Seven PPU threads, the asset archive open, 8,503 syscalls, and a wait inside `libfs`
at `0x01401ee4`.

## What that address is

From `out/ppu/libfs_000.cpp:3735`:

    ctx.gpr[11] = 808;                 /* sys_fs_stat */
    ctx.cia = 0x1401ee4; ppu::syscall(ctx, base);

`sys_fs_stat` is implemented (`lv2/fs.cpp:345`), so the guest is not waiting on a
missing HLE. The syscall counter is **frozen at 8,211 → 8,503 over 30 s**, and the top
of the histogram is `_sys_lwmutex_create=827 / destroy=816` — a lock being created and
destroyed hundreds of times.

That pattern is a lock-contention loop: the guest takes a filesystem lock, finds the
state it wants not ready, releases, retries.

## Honest position

Milestone 3 remains **open**. This round fixed two genuine defects (an invalid flag
read in a diagnostic, and a wrong mount prefix that silently disabled the asset fix from
last round) and confirmed the boot reaches the same libfs wait as before, with the
assets properly mounted.
