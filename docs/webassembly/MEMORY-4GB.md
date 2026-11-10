# The 4 GiB question, measured

This was the last unmeasured risk of milestone 3. It is now a number.

## The guest map is exactly 4 GiB, by construction

`runtime/src/memory.cpp:261` reserves `0x200000000` (8 GiB) as a **base plus
slack**, but the regions the title declares end at exactly 4 GiB:

| region | range | size |
|---|---|---|
| main RAM | `0x00010000..0x10000000` | 256 MB |
| user64k | `0x10000000..0x40000000` | 768 MB |
| user1m | `0x30000000..0x40000000` | (overlaps user64k) |
| rsx context | `0x40000000..0x50000000` | 256 MB |
| video | `0xC0000000..0xD0000000` | 256 MB |
| stack | `0xD0000000..0xE0000000` | 256 MB |
| spu | `0xE0000000..0x100000000` | **512 MB** |

Measured by `tests/memspace.cpp`:

    declared regions: 7, total span 0xbfff0000 (3.00 GB), highest address 0x100000000
    wasm32 limit:      0x100000000 (4 GiB)
    headroom inside wasm32: 0 bytes (0.00 MB)

**Zero headroom.** A flat 4 GiB guest map in wasm32 leaves nothing for the
compiled code, the host stack, the function table or the shadow stack. A flat map
is therefore impossible on wasm32 — this is arithmetic, not judgement.

## But the flat map is not what the game uses

Measured over real sessions:

- `sys_vm_memory_map -> 0x100c0000 (used 0x62a0000 / 0xd500000)` — **98.6 MB of
  guest RAM actually used** out of 213 MB declared.
- The highest guest addresses touched reach `0xff801004`, inside the SPU region,
  but that region is allocated sparsely: only the pages the SPU threads use.

So the question becomes: **can a windowed map fit?** From the test:

    windowed estimate: 0.41 GB, leaving 3.59 GB for code+stack+tables
    check: a windowed map fits wasm32 with room for the runtime

**0.41 GB of window, 3.59 GB free.** That is the answer, and it changes the
recommendation from the feasibility study.

## The two options, now with numbers

### Option A — Memory64 (`-sMEMORY64`)

Flat map preserved, zero runtime changes, 4 GiB becomes 8 GiB of addressable
space. Cost: the browser floor rises to Chrome 133+, every pointer doubles, and
Emscripten's Memory64 support is still experimental. **Not needed**: the numbers
above show wasm32 is sufficient with a window.

### Option B — windowed map on wasm32 — RECOMMENDED

Map only the windows the title touches, with the existing `Region` table already
providing the boundaries. `mem::reserve` asks for `0x200000000`; it must ask for
the span the windows need. The page table that `memory.cpp` already maintains
(`g_blocks`, `g_page_epoch`) is the mechanism: it already knows which ranges are
committed, so a window is a range the block map says exists.

The work is in `mem::reserve` and the region placement, **not** in the PPU ops:
they keep writing `base[ea]` because the base is still flat within the window.
The one real constraint is that the window must be laid out so that
`ea -> offset` is monotonic per region, which the existing region table already
guarantees.

## What this means for the plan

The feasibility study recommended Memory64 if the browser floor could be raised,
and priced wasm32 at "8-12 weeks with address translation". **The measurement
says the translation is unnecessary**: a 0.41 GB window fits wasm32 with 3.59 GB
to spare, so the port stays on the mainstream browser floor and the PPU fast path
keeps its plain `base[ea]` addressing.

The remaining work is a change of *allocation policy*, not a change of *addressing
mode*. That is a smaller job than the study feared, and it is the finding this
round produces.
