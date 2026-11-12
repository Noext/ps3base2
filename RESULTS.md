# T4 -- visibility: lift 0x3000c7e8 (the SPURS abort path)

Branch explore/t4, worktree /root/Dev/wt-T4, base commit b1ba5ee.

## Measurements

### M1 -- the abort at HEAD, reproduced on this worktree's build (b1ba5ee + build-T4)

Command:

    cmake -S /root/Dev/wt-T4 -B /root/Dev/wt-T4/build-T4 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DPS3BASE_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/ppu \
      -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
      -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu
    cmake --build /root/Dev/wt-T4/build-T4 -j 8 --target ps3base        # done=0
    nohup setsid env DISPLAY=:919 XDG_RUNTIME_DIR=/run/user/0 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      timeout 110 /root/Dev/wt-T4/build-T4/ps3base --no-launcher > /tmp/T4.log 2>&1 < /dev/null & disown

Raw log, /tmp/T4.log:1823-1950 (verbatim):

    E 108.585986 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E 108.586030 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET cmd 0x40 unmapped ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06510
    I 108.586113 [1702359] spu: thread 0x200 'SecondaryCellSpursKernel0' stopped at 0x1a104
    ...
    I 108.589527 [1702358] tty: [0]   SPURS instance 0x01646400
    I 108.589549 [1702358] tty: [0]   SPU number     0
    I 108.589570 [1702358] tty: [0]   policy module  0x32023680
    I 108.589605 [1702358] tty: [0]   workload ID    1
    I 108.589637 [1702358] tty: [0] Call Stack Trace:	0x3200cb24
    I 108.589662 [1702358] tty: [0]                  	0x32009754
    I 108.589680 [1702358] tty: [0]                  	0x0
    I 108.589704 [1702358] tty: [0] The SPURS is aborted.
    I 108.589720 [1702358] process: _sys_process_exit(0x8001000c)
    I 108.590207 [1702358] ppu: process exit -2147418100 from thread 0x1000004 lr=0x3000c7e8 (not lifted)

51 "rsx: flip" lines, last flip 2761 at 97.3 s (the run was still alive at the
110 s timeout on this pass; the 2161-flip/77.9 s figure in the task text is the
timing variability of the same state).
