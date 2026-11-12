# T3 -- SPU channels and MFC conformance vs RPCS3

## Measurements

### Baseline at 70daf19, my own build-T3, DISPLAY=:918

Run reproduces the reported abort. Raw tail of /tmp/T3.log:

```
I  86.015405 [1683294] tty: [0]       system workload
I  86.015419 [1683294] tty: [0]   Workloads:
I  86.018281 [1683294] tty: [0]   policy module  0x32023680
I  86.018298 [1683294] tty: [0]   workload ID    1
I  86.018319 [1683294] tty: [0] Call Stack Trace:	0x3200cb24
I  86.018401 [1683294] tty: [0] The SPURS is aborted.
I  86.018418 [1683294] tty: [0] process: _sys_process_exit(0x8001000c)
I  86.018709 [1683294] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)
```

flip count at abort: 2341.
