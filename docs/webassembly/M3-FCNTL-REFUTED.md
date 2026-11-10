# The fcntl capacity hypothesis, tested and refuted

## What I found

A probe on `sys_fs_fcntl` (817) showed the boot's calls are **100 % `op 0xC0000006`**:

    fcntlprobe: op 0xc0000006 fdn 4294967295 size 32 (slots 04:0 0A:0 0B:0 09:0 other:100)
    fcntlprobe: op 0xc0000006 fdn 4294967295 size 32 (slots ... other:200)

The handler for that op writes **`0x80010037` — `CELL_ENOTSUP`** into the reply. So the
guest asks for something, is told "not supported", and retries. That looked like the
stall, and it looked like an easy fix: answer with a real capacity.

## The test, and the result

New handler:

    vm::write32(arg + 0x18, 0);
    vm::write32(arg + 0x1C, 0x2000);       /* block size */
    vm::write64(arg + 0x20, 0x80000);      /* block count -> 4 GiB */

Result:

| | ENOTSUP | capacity |
|---|---|---|
| asset events (`MediaPS3` + `pngdec`) | **125** | **0** |
| `main_thread` pc | `0x01401ee4` (filesystem) | `0x003a18b0` (worker loop, the old stuck state) |

**Answering the query broke the boot completely.** Every asset load stopped and the
program fell back to the state it was stuck in six rounds ago.

So `CELL_ENOTSUP` is not a defect here — it is the answer that makes the title take the
code path which *does* load assets. A valid capacity sends it down a different path that
stalls earlier.

## What that means

The retry loop on `0xC0000006` is **downstream** of the real stall, not the stall itself.
The guest is doing something else, and the fcntl calls are a symptom of it being alive and
spinning.

I reverted the change (the revert is the code now) and recorded the negative result in
the handler's comment, so the next reader does not repeat the experiment.

## Where the build stands

Unchanged from last round, and correctly so:

| | |
|---|---|
| asset events | **125** |
| PPU threads | 9 |
| PNG decodes | 101 |
| `main_thread` pc | `0x01401ee4` |
| crashes | 0 |
| main menu | **not reached** |

## Honest position

Milestone 3 remains **open**. This round produced no forward progress — it produced a
**negative result with numbers**: a plausible fix that made things strictly worse, caught
because the asset-event count is measured on every run rather than judged by eye.

That is worth recording. The value of a measurement discipline is not that every
hypothesis is right, but that a wrong one is caught in one run instead of being shipped.
