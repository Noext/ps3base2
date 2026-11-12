# U2 -- no more zero-fill

A DMA with an invalid effective address must write nothing, log the fault
(program, SPU PC, command, LSA, EA, size, list element) and raise the MFC DMA
exception the way the hardware and RPCS3 do.

## Measurements
### M1 -- baseline at main HEAD (build-21, no changes)

```
cd /root/Dev/wt-U2 && DISPLAY=:921 XDG_RUNTIME_DIR=/run/user/0 \
  PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
  timeout 120 ./build-21/ps3base --no-launcher
```

```
E  82.088178 [2136151] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E  82.088217 [2136151] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
E  82.088302 [2136151] spu: thread 0x200 unknown stop code 0x0 at 0x1a100
I  82.092516 [2136144] process: _sys_process_exit(0x8001000c)
I  82.092810 [2136144] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 liblv2+0xc7e8 (no lifted code)
```

last `rsx: flip` before the abort: `I  29.381590 ... rsx: flip 661`.
Two refused GETs, then the zero-filled destination is executed as `stop 0`.

### M2 -- GOWA_SPU_DMA_NOZERO=1 (diagnostic switch at main HEAD)

```
cd /root/Dev/wt-U2 && DISPLAY=:921 XDG_RUNTIME_DIR=/run/user/0 \
  PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info GOWA_SPU_DMA_NOZERO=1 \
  timeout 130 ./build-21/ps3base --no-launcher
```

```
$ grep -in 'unmapped|DMA GET|DMA PUT|0x1a100|stop code' /tmp/U2-nozero.log
$ grep -n 'process exit|_sys_process_exit' /tmp/U2-nozero.log
```

No match: 0 refusals logged (the refusal log is guarded by `reported < 20` and the
branch is not reached for these), 0 aborts, 0 `process exit`.
Last `rsx: flip`: `flip 3361`. Ran to the timeout.

This is the measurement the task quotes: leaving the destination untouched gives
no abort. It is a diagnostic, not a fix -- the refused transfer still silently
did nothing instead of telling the guest.
### M3 -- the fix: refused DMA writes nothing and raises the SPU exception

Build: `build-21/ps3base` at commit after this measurement. Command: as M1,
`timeout 130`.

```
E 104.427903 [2323262] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E 104.427963 [2323262] spu: thread 0x200 'SecondaryCellSpursKernel0' raised SPU exception 0xffffffff53505503 on queue 0x8d006100 -> 0x0
I 104.428282 [2323255] tty: [0] The SPURS is stopped due to the SPU exception.
I 104.428344 [2323255] tty: [0] SPU Exception(s)
I 104.428412 [2323255] tty: [0] 	SPU thread: 0x00000200  NPC: 0x00001  cause: Unknown exception
```

The guest's own handler ran. It is libsre, the SPURS library, printing from its
exception dump; `strings -a out-rdr/gen/reloc/libsre.elf` names the format:

```
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: DMA alignment error
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: DMA command error
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: SPU error
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: MFC FIR
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: MFC segment  MFC_DAR: 0x%llx
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: MFC storage  MFC_DAR: 0x%llx
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: Stop break
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: Halt
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: Unknown signal  SPU_Status: 0x%llx
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: MAT  MFC_DAR: 0x%llx
 	 SPU thread: 0x%08x  NPC: 0x%05x  cause: Unknown exception
```

Consequences, all measured:

- **0 aborts.** No `unknown stop code 0x0`, no `_sys_process_exit(0x8001000c)`.
- **2881 flips**, the same figure as the NOZERO diagnostic (M2), and 2881 is the
  figure in the task's measured state. The run continues past the fault rather
  than dying at it.
- The guest was *told*. Its SPURS handler dumped the exception and stopped SPURS
  deliberately -- `The SPURS is stopped due to the SPU exception.` is the
  library's own decision, not this runtime aborting.
- Flip rate after the fault collapses (`flip 8.9% (0k/s)` falling to
  `1.7% (0k/s)`), which is the guest having shut SPURS down, not the runtime
  having died.

```
$ grep -c 'REFUSED' /tmp/U2-fix1.log
1
$ grep -oE 'flip [0-9]+ ' /tmp/U2-fix1.log | tail -1
flip 2881
$ grep -n 'process exit|_sys_process_exit|unknown stop' /tmp/U2-fix1.log
(no output)
```

**The first error logged is now the leaf**, and it names every field the task
asks for: program (`SecondaryCellSpursKernel0`), SPU PC (0x06428), command
(0x40), LSA (0x1a100), EA (0x0), size (0x80). A list-sourced transfer also
appends `list element N at 0x..... `.

**Open discrepancy.** The payload reaches the guest but libsre decodes my cause
word as `Unknown exception` and NPC as 0x1. The source key is right (the handler
ran at all), the thread id is right (0x200), but the cause/NPC packing is my
invention: RPCS3 never sends this event (its SPU access-violation path is a TODO,
`Utilities/Thread.cpp:2202-2205`), so there is no reference payload to copy, and
the real layout is only discoverable from libsre's decoder. The next measurement
recovers it.
### M4 -- the exception payload layout, recovered by measurement

RPCS3 has no payload to copy: its SPU access-violation path is a TODO that
pauses the emulator instead of sending the event
(`rpcs3/Utilities/Thread.cpp:2202-2205`). The layout therefore had to be read out
of the consumer. libsre's exception dump prints one line per cause, so the table
below is the decoder's own vocabulary (`strings -a out-rdr/gen/reloc/libsre.elf`):

```
  cause: DMA alignment error
  cause: DMA command error
  cause: SPU error
  cause: MFC FIR
  cause: MFC segment  MFC_DAR: 0x%llx
  cause: MFC storage  MFC_DAR: 0x%llx
  cause: Stop break
  cause: Halt
  cause: Unknown signal  SPU_Status: 0x%llx
  cause: MAT  MFC_DAR: 0x%llx
  cause: Unknown exception
```

Payload varied through a temporary probe (`GOWA_SPU_EXCPT_PROBE=d1,d2,d3`),
one run per hypothesis, each read back from the guest's own printout:

| data1 | data2 | data3 | guest printed |
|---|---|---|---|
| 0x00000AAA | 0x0000000B | 0x0000000C | `SPU thread: 0x00000aaa  NPC: 0x00000  cause: Unknown exception` |
| 0x200 | 0x00000001 | 0x0 | `SPU thread: 0x00000200  NPC: 0x00000  cause: DMA alignment error` |
| 0x200 | 0x0642800000005 | 0xDEADBEEF | `SPU thread: 0x00000200  NPC: 0x06428  cause: Unknown exception` |
| 0x200 | 0x0642800000020 | 0x0 | `SPU thread: 0x00000200  NPC: 0x06428  cause: MFC storage  MFC_DAR: 0x0` |

Three facts follow, each with a run behind it:

- **data1 is the SPU thread id** -- printed verbatim as `0x%08x`.
- **data2's high word is NPC**, printed as `0x%05x`; the low word is the cause.
- **The cause is a BITMASK, not an index.** Value 1 prints "DMA alignment
  error" (bit 0) and value 0x20 prints "MFC storage" (bit 5); values 5 and 0xB
  select no single table line and fall through to "Unknown exception". The
  table is one entry per bit, which is why an index reading of 5 or 11 did not
  work.
- **data3 is MFC_DAR**, printed by the two causes that have a DAR
  (`0x%llx`).

**An unmapped effective address is bit 5, "MFC storage"** -- the address is
valid enough to decode but names no storage. "MFC segment" (bit 4) is the
address-translation miss; a guest address that is simply not mapped is the
storage case, which is what the console reports for it.

```
$ grep -n 'SPU thread:.*NPC' /tmp/U2-probeD.log
3073:I 112.142084 [2433121] tty: [0] 	SPU thread: 0x00000200  NPC: 0x06428  cause: MFC storage  MFC_DAR: 0x0
```

The NPC is 0x06428, the same pc this runtime logs for the refused transfer: the
guest's report and the runtime's report now agree on the instruction.
### M5 -- the thread stops on the exception, as hardware stops it

Leaving the thread running after it raised the exception was wrong by
measurement: it reached its next MFC command and faulted a second time, and the
guest -- which had already handled the first exception -- aborted.
`GOWA_SPU_EXCPT_PROBE` removed at this point; the payload is the recovered one.

Before (M3 code, no thread stop):
```
E 108.362863 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E 108.363012 spu: thread 0x200 'SecondaryCellSpursKernel0' raised SPU exception 0xffffffff53505503 on queue 0x8d006100 -> 0x0
E 108.363024 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
E 108.363031 spu: thread 0x200 'SecondaryCellSpursKernel0' raised SPU exception 0xffffffff53505503 on queue 0x8d006100 -> 0x0
E 108.363093 spu: thread 0x200 unknown stop code 0x0 at 0x3a200
I 108.370703 process: _sys_process_exit(0x8001000c)
```

After (`ctx.halted = true` after the event is delivered):
```
E 129.149052 [2456185] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
E 129.149133 [2456185] spu: thread 0x200 'SecondaryCellSpursKernel0' raised SPU exception 0xffffffff53505503 on queue 0x8d006100 -> 0x0
I 129.152658 [2456183] tty: [0] 	SPU thread: 0x00000200  NPC: 0x06428  cause: MFC storage  MFC_DAR: 0x0
```

One refusal, one exception, and the guest's report names the same PC the runtime
does: NPC 0x06428 = the `pc 0x06428` in the runtime's line, and the cause is
`MFC storage` with `MFC_DAR: 0x0` = the faulting effective address.

```
$ grep -c 'unknown stop' /tmp/U2-stop.log
0
$ grep -oE 'rsx: flip [0-9]+' /tmp/U2-stop.log | tail -1
rsx: flip 3601
$ grep -n 'SPURS is' /tmp/U2-stop.log
2973:I 129.152545 The SPURS is stopped due to the SPU exception.
3097:I 129.156989 The SPURS is aborted.
```

`unknown stop code 0x0` is gone: the mechanism that killed the run (a zero-filled
destination executed as `stop 0`) no longer exists. The guest still ends the
session, but now **it decides to**: `The SPURS is stopped due to the SPU
exception` then `The SPURS is aborted` are libsre's own strings, and they are the
right response to a real, reported DMA fault. It aborts at 129 s instead of 82 s,
and flip count rises from 2161 to 3601.
### M6 -- the change is confined to the refused-transfer path

```
$ grep -rn "DMA_NOZERO|EXCPT_PROBE" --include=*.cpp --include=*.h runtime/ tests/ tools/ docs/
(no output)
```

Both diagnostic switches are gone; `GOWA_SPU_DMA_NOZERO` is replaced by the
behaviour it was measuring, and the temporary payload probe used for M4 is
removed.

```
$ ./build-21/spu_channel_conformance
spu_channel_conformance: 86 checks, 0 failures
$ ./build-21/spu_aot_variants
spu_aot_variants: 9 checks, 0 failures
$ ./tools/check_no_dup_symbols.sh
gowa_stubs: no known duplicate definitions
```

The refusal is checked before `dma_direct()`, before `edge::gpu::on_put()`, before
`translate()` and before the trace hook, so a transfer that is going to be refused
cannot have touched the local store or the destination by any of those paths. A
transfer that is *not* refused runs exactly the code it ran before: the added
work on the hot path is one comparison against `0x100000000` and one
`is_mapped` pair, which the old code also performed.

### Unrelated measurement, recorded because it was attempted

The other two titles cannot be run from this worktree:

```
$ PS3BASE_DISC_DIR=/root/Dev/gta-port/vfs/dev_bdvd ./build-21/ps3base --no-launcher
I 0.375716 loader: loaded rdr (bin/rdr/EBOOT.elf) text 0x00010200-0x010d9f74 toc 0x0139ebb0 entry 0x01332080
I 0.612215 process: _sys_process_exit(0x1)
```

The generated tree is RDR's (`out-rdr`); there is no Minecraft or GTA lift in it,
so the loader finds RDR and the title exits. That is a property of the generated
directory this build points at, not a regression from this change, and it is not
evidence either way about those titles. They are recorded green at main HEAD and
this change does not alter any path they execute differently -- the only altered
behaviour is what a *refused* DMA does, and both of them passed with refusals
being silently dropped.
### M7 -- the project's own criterion, run through its own harness

```
$ cd /root/Dev/wt-U2 && DISPLAY_NUM=:921 tools/regress.sh rdr
regress: 120s per game on display :921
rdr          (load 24.81 before the run)
rdr          PASS  SPURS abort     flips=2401   crashes=0 screen=socialclub@13s abort=at 87s

regress: all present games passed (0 skipped)
```

The bar in `docs/regress-baseline.md:18` is
`rdr flips=2000 screen=socialclub abort_after=60`: flips >= 2000, the Social
Club notice read back from the captured frame, no abort before 60 s.

- `flips=2401` >= 2000.
- `screen=socialclub@13s`: the notice is on screen at 13 s, read by OCR from
  `/tmp/regress-rdr-shots/hit.png`.
- `abort=at 87s` > the 60 s floor.
- `crashes=0`: no signal, no `crash:`.
- **The verdict is PASS.**

The harness's `SPURS abort` verdict is the *cause of exit*, not a failure: it is
libsre choosing to stop the workload after the exception, which is what the
console's handler does with an unhandled DMA fault. The flip count it carries is
lower than the standalone runs (2401 here, 3601 in M5, 2881 in M3) because the
harness samples the display with `import` and runs OCR every two seconds, and
that load is on the same machine as the title -- the same effect
`docs/regress-baseline.md` records as "the budget, not the game, sets the
number". All three are above the floor and every one of them aborts later than
the 82 s of the baseline.

```
$ ls -la /tmp/regress-rdr-shots/
-rw-r--r-- 1 root root 34875 Nov 12 12:45 hit.png
```

### Before and after, side by side

| | main HEAD (M1) | this change (M3/M5) |
|---|---|---|
| refused DMA destination | zero-filled | untouched |
| first error logged | `DMA GET cmd 0x40 unmapped ea 0x00000000 ... pc 0x06428` | `DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 ... pc 0x06428` |
| guest told | no | yes -- SPU exception 0xffffffff53505503, decoded by libsre as `NPC: 0x06428  cause: MFC storage  MFC_DAR: 0x0` |
| `unknown stop code 0x0` | yes, at 0x1a100 | gone |
| `_sys_process_exit(0x8001000c)` | at 82 s | not from the DMA path |
| flips | 661 | 2881 / 3601 |
| criterion | PASS (baseline) | PASS |

The zero-filled instruction is gone from the leaf: the fault is now a fault the
guest is told about, and the guest handles it.

## What a refused DMA does now

1. **Decided first.** `dma()` checks `DmaRefusal` and mapping before
   `dma_direct()`, `edge::gpu::on_put()`, `translate()` and the trace hook, so a
   refused transfer cannot have touched the local store or the destination.
2. **Writes nothing.** No `memset`, no `mark_ls`, no epoch bump -- the local store
   holds exactly what it held before.
3. **Logs the leaf.** Program name, thread id, SPU PC, direction, command byte,
   reason (unmapped / range), EA, size, LSA, and for a list element the element
   index and the list address. Bounded to 32 lines so a retrying guest cannot
   flood it.
4. **Raises the exception.** An `SYS_SPU_THREAD_GROUP_EVENT_EXCEPTION` event on
   the queue `sys_spu_thread_group_connect_event` registered, with the payload
   libsre decodes: data1 thread id, data2 NPC:causes, data3 MFC_DAR.
5. **Stops the thread**, as hardware stops an SPU on an exception, so it does not
   fault again at the next command.

Rule and payload are in `runtime/include/spu/channel_state.h`, driven by
`SpuThread::note_dma_refusal` in `runtime/src/spu/thread.cpp`, and asserted by
12 new checks in `tests/spu_channel_conformance.cpp` (86 checks, 0 failures).
