# The 4 GiB question, measured

This was the last unmeasured risk of milestone 3. It is now a number, and my first
answer to it was wrong in a way worth recording.

## The flat map is exactly 4 GiB

`runtime/src/memory.cpp:261` reserves `0x200000000` (8 GiB) as base plus slack, but
the regions end at exactly 4 GiB:

| region | range | size |
|---|---|---|
| main RAM | `0x00010000..0x10000000` | 256 MB |
| user64k / user1m | `0x10000000..0x40000000` | 768 MB |
| rsx context | `0x40000000..0x50000000` | 256 MB |
| **gap** | `0x50000000..0xC0000000` | **1792 MB, never touched** |
| video | `0xC0000000..0xD0000000` | 256 MB |
| stack | `0xD0000000..0xE0000000` | 256 MB |
| spu | `0xE0000000..0x100000000` | 512 MB |

`tests/memspace.cpp` reports `highest address 0x100000000`, `headroom 0 bytes`. A
flat map in wasm32 leaves nothing for code, stack, tables or the shadow stack.

## My first conclusion was wrong, and why

I wrote that a **windowed** map needs only 0.41 GB because 98.6 MB of guest RAM is
actually used. That confused **committed memory** with **addressable span**.

wasm32 memory is **linear**: to keep the fast path `base[ea]` with no translation,
the memory must be at least `max(ea)` bytes long. Decommitting pages does not
shorten the span. So the windowed idea as I stated it does not work, and the
"0 bytes headroom" number stands as the real constraint on a flat map.

## The actual fix: the gap is ours, not the guest's

The 1792 MB hole between `0x50000000` and `0xC0000000` exists because **we chose
those addresses** in `memory.cpp:kRegions`. Nothing in the title requires
`0xC0000000` for video or `0xE0000000` for SPU: no `mmapper_allocate_address` or
`sys_vm_memory_map` request names them, and `Region::Video`/`Region::Spu` are
referenced by our own allocator only.

Compacting the table — placing the high regions immediately after rsx context —
gives:

    main     0x00010000..0x10000000
    user64k  0x10000000..0x40000000
    rsx      0x40000000..0x50000000
    video    0x50000000
    stack    0x60000000
    spu      0x70000000..0x90000000

    compacted highest: 0x90000000 = 2.25 GiB
    wasm32 headroom  : 1.75 GiB

**2.25 GiB used, 1.75 GiB free** for code, stack, tables and the shadow stack.
That fits wasm32 with room to spare, and the PPU fast path keeps plain
`base[ea]` addressing — no translation layer, no Memory64, no raised browser
floor.

## What this changes

The feasibility study priced wasm32 at 8–12 weeks *including an address
translation layer*. The measurement removes that layer from the plan: the guest
map needs compacting, not translating.

The change is confined to `memory.cpp:kRegions` and `mem::reserve`, and it must be
proven safe before it is adopted: the addresses move, so **every** consumer that
hardcodes one has to be found. Two are known: `memory.cpp:48` uses
`0xC0000000` as the upper bound for user-region placement, and the stack region is
where `stacks.cpp` puts guards. A grep for each old base is the required check,
and it is the first task of the next round — not something to assume is empty.

## What is not established

- Whether any code outside those two places compares a guest address against
  `0xC0000000`/`0xD0000000`/`0xE0000000` as a magic constant. Must be grepped.
- Whether the title itself probes those addresses at boot. The log shows no such
  request, but that is an absence of evidence from one log, not proof.
