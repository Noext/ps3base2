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
