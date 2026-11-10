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

## The grep, done — and the change is bigger than I wrote

I said the change was confined to two places. The grep says otherwise:

| site | what it is |
|---|---|
| `memory.cpp:38,39,40` | the region table itself |
| `memory.cpp:48` | `a + size <= 0xC0000000` — the user-region upper bound |
| `lv2/rsx.cpp:44` | `constexpr uint32_t LOCAL_BASE = 0xC0000000` |
| `gpu/presenter.cpp:34` | the same constant, again |
| `gpu/rsx_renderer.cpp:1653,1655,4362,4422` | display-buffer offsets computed as `0xC0000000u + off` |

**Six places, in four files, not two.** And the RSX ones are not bookkeeping: the
title reaches RSX-visible memory through `get_address()`, whose `case 0` is
`return LOCAL_BASE + offset`. Moving the video region moves what every RSX read
and write resolves to.

The good news, established by reading `io_to_ea` and `get_address`: the base is
**our constant**, not a value the title supplies. `sys_rsx_context_allocate`
allocates its area through `vm::alloc(Region::RsxContext, ...)`, so the RSX
subsystem already uses the region allocator. That means the change is *possible*
without a rewrite — but it touches the renderer's memory resolution, and it needs
its own test, not a hopeful edit.

## What is not established

- Whether the title itself probes video memory at `0xC0000000` at boot. The log
  shows no such request, and `sys_rsx_context_allocate` gets its area from our
  allocator, so the evidence points to "no" — but it is one log, not proof.
- **Resolved:** the lifted code does not reference these constants at all. Grepping
  all 98 files of `out/ppu/` for `0xC0000000`, `0xD0000000`, `0xE0000000` and the
  decimal `3221225472` returns **0 files**. The game accesses these regions only
  through pointers it obtained from us, never by naming an address. So the entire
  dependency is the six runtime sites listed above, all of which we control.

## Conclusion

The map can be compacted from 4.0 GiB to 2.25 GiB, leaving 1.75 GiB for the
runtime inside wasm32, with no address translation and no Memory64. The work is
six sites in four files, and the guest never observes the move because it never
names those addresses itself.
