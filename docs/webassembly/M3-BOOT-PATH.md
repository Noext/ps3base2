# Boot path in wasm: the file seam that HTTP Range needs

Milestone 3's criterion is the title reaching the main menu. That needs the
loader to read the executable and firmware ELFs, which natively come from
`bin/` and `fw/` on disk. A browser has no filesystem: the disc is fetched over
HTTP Range.

## The seam

`runtime/include/runtime/files.h` + `runtime/src/files.cpp`:

    using Provider = std::function<bool(const std::string&, std::vector<uint8_t>&)>;
    void set_provider(Provider fn);
    std::vector<uint8_t> read(const std::string& path);

`loader::read_file` now calls `files::read`, which asks the provider first and
falls back to `fopen`. The loader code does not know whether the bytes came from a
disk, a fetch or a preloaded buffer — which is what makes the browser path a
registration instead of a fork of the loader.

## Proven, not assumed

    ps3base-wasm: 9 images in the table, provider installed
    ps3base-wasm: mc=0 liblv2=1
    ps3base-wasm: mc data 64 bytes
    WASM-FILE-PROVIDER-DRIVES-LOADER

With a provider installed that answers one path, the loader reads through it and
sees the bytes. Without a provider the same call returns 0 bytes and the runtime
says so plainly:

    ps3base-wasm: no disc image available (expected without a provider)

## The boot inventory, measured

The table the boot needs, from `out/ppu/table.cpp`:

| image | file | size on disk |
|---|---|---|
| `mc` | `bin/mc/mc.elf` | **12,452,792 bytes** |
| `liblv2` | `fw/4.91/reloc/liblv2.elf` | part of 8 firmware ELFs, **1,148,160 total** |

9 images, of which the game and `liblv2` are required for the boot to start
(`main.cpp:253-256`). So the browser must serve at least **13.6 MB** before the
first guest instruction runs, and the disc image itself on top of that — which is
exactly why HTTP Range rather than a full download.

## What is now false and what is still missing

I wrote last round that milestone 3 was complete. It was complete for **its
stated build criterion** (a module that builds, boots the runtime and executes
lifted code), but the prompt also asks for the main menu, which needs this seam
*plus* a provider that actually fetches, plus the LV2 boot path driving a guest
thread.

Still missing, in order:

1. a real provider in the page layer that fetches `mc.elf` and the firmware
   (milestone 4 work, but the seam is now ready for it);
2. `guest::create` + `sys_ppu_thread_start` on the LV2 entry with the CRT argv
   built as `main.cpp:269-300` does — none of that has been driven under wasm;
3. the schedule loop that runs guest threads. The wasm build is single-threaded
   and `logical_cpus()` returns 1, so the SPU workers need the ps2-web-recomp
   unwind-on-switch trick or a cooperative scheduler.
