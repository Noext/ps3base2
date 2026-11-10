# SPURS event flags implemented; the null is a guest-side member, not an import

## What was built and proven

New `runtime/src/hle/cellSpurs.cpp`: the event-flag family, using the **real
big-endian guest layout** from `refs/jdc-ps3recomp/libs/spurs/cellSpurs.{h,c}`:

| function | behaviour |
|---|---|
| `_cellSpursEventFlagInitialize` | zeroes 0x80 bytes, writes direction@0x0E, clearMode@0x0F, isIwl@0x0D, taskset EA@0x70 |
| `cellSpursEventFlagAttachLv2EventQueue` | validates direction against SPU2PPU/ANY2ANY and **refuses** an uninitialised flag |
| `cellSpursEventFlagSet/Clear` | OR/AND-NOT the bits at 0x00 in guest byte order |
| `cellSpursEventFlagWait/TryWait` | satisfies immediately (no SPU worker sets bits here, so a block would deadlock the boot) |
| `_cellSpursLFQueue*`, `cellSpursLFQueue*` | `CELL_OK`, matching the reference's forwarding to cellSyncLFQueue |

New `tests/spurs.cpp`: **12 checks, 0 failures**, asserting the field offsets and
the refusal path. It runs natively, which is the order the previous round
committed to.

## The signature bug this found

My first version read `(taskset, flag, clearMode, direction)` from r3..r6. The real
signature is `(spurs, taskset, eventFlag, clearMode, direction)` — five arguments,
so everything was shifted by one. The guest log made it obvious once the handler
was reached:

    EventFlagInitialize(clearMode=14099328 direction=0) flag=0x00d68d00 taskset=0x00000000
                                      ^ 14099328 was the flag ADDRESS

After the fix:

    EventFlagInitialize(clearMode=0 direction=2) flag=0x00d72380 taskset=0x00d68d00
    EventFlagInitialize(clearMode=0 direction=1) flag=0x00ce2400 taskset=0x00000000
    EventFlagAttachLv2EventQueue(flag=0x00ce2400)

Real tasksets, real directions, attaches succeeding. **Source:
`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSpurs.cpp:3267`.**

## The null did not go away, and it is not an import

The boot still dies at the same place:

    F ppu: unknown indirect target 0x00000000 (lr=0x005d2c08) r3=0x0 r31=0x0 ctr=0x0

Reading the lifted body settles it:

    ctx.gpr[31] = ctx.gpr[3] | ctx.gpr[3];     /* r31 = first argument */
    ctx.gpr[12] = ld32(base, ctx.gpr[31] + 8);

**The caller passed a null object.** `f_005d2b44` receives its `this` in r3, and the
*previous* call returned zero into the slot the caller then loads. That is a
guest-side uninitialised member, not a missing HLE — my handlers now run correctly
and the failure simply moved past them.

My earlier claim that SPURS was the blocker was **wrong in the specific** and right
in the general: SPURS was genuinely missing and had to be written, but fixing it
did not unblock the boot.

## Where this leaves the port

The wasm build now runs: memory init, image loading, the LV2 main thread, CRT argv,
SPURS event flags, LV2 allocation and the video-out query. It stops on a null object
member inside the title's own code.

The next diagnostic is to find which call returns zero into that slot. The
register dump already names the calling function (`lr=0x5d2c08`, inside
`f_005d2b44`); what is missing is the identity of *its* caller, so the next step is
to log the call chain at `f_005d2b44` entry rather than guess.
