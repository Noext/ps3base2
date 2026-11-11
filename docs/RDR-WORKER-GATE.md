# RDR worker gate: what the beq at 0x533148 actually tests

Measured 2026-11-11 on BLES00680 with `PS3BASE_THREAD_PROBE=1`.
Probe: `runtime/src/hooks.cpp` (thread_entry_probe / work_probe / work_54b67c /
loadthread_real). Build: `build-rdr`.

## The premise is wrong: the workers do NOT skip their work

`f_00533064` (OPD 0x01356420) has two separate things that were confused:

   0x533144  cmpwi  r6, 0
   0x533148  beq    0x53316c     ; <-- guards a LINKED-LIST INSERT, not the work
   0x53314c  lis r7,0x166 / addi r7,-0x33e0   -> r7 = 0x165cc20
   0x533154  lwz r8,0x844(r7)    ; [0x165d464] = list head
   0x533158  stw r8,0(r6)        ; ws->next = head
   0x533160  stw r6,0x844(r7)    ; head = ws
   0x533164  lwz r8,0x840(r7)    ; [0x165d460] = count
   0x533168  stw r8+1,0x840(r7)  ; count++
   0x53316c  ... rejoin ...
   0x5331b0  bctrl               ; <-- THE WORK, after the rejoin

The tested value is **the entry's own r3 argument**, copied through
`0x5330ac clrldi r3,r31,32` and `0x5330b4 stw r3,0xd8(r1)`, reloaded at
`0x533140 lwz r6,0xd8(r1)`. It is the **address** of the per-worker structure
(0x0165cc20 / 0x0165cc4c), which is never zero. So the branch is never taken and
the work always runs.

Measured at run time, the exact slot the test reads:

    WORK f_0027e564 #0 th=0x1000005 ... GATE_SLOT[r1+0xd8=0xd00e3f78]=0x0165cc20
    WORK f_0027e564 #1 th=0x1000006 ... GATE_SLOT[r1+0xd8=0xd0103f78]=0x0165cc4c
    WORK f_00545d54 #2 th=0x1000007 ... GATE_SLOT[r1+0xd8=0xd0125f78]=0x0165cc4c

and the list counters moving, which only happens on the "not taken" side:

    LIST count=47 head=0x0165cc20   (before worker 1 inserted)
    LIST count=48 head=0x0165cc4c   (after)

## So which thread exits, and why

Not "all workers". `FreeSpaceCheckThread` and `LoadThread` exit through
0x5331cc because **their work functions return immediately**. The leaf is the
import, not a branch in `f_00533064`:

    ENTRY 0x533064 #8 th=0x100000e r3=0x0165cc20 ... ws0[0]=0x0054b67c (work fn)
    impthr: #1 sys_ppu_thread_get_id(...)      lr=0x00533088  -> 0x0
    impthr: #2 sys_lwmutex_lock(0x165d470,...) lr=0x00533134  -> 0x0
    impthr: #3 sys_lwmutex_unlock(0x165d470,..)lr=0x00533180  -> 0x0
    threadgate: LOADTHREAD work f_0054b67c #0 flag[0x1669dcc]=0 flag[0x17197f8]=0 flag[0x17197f9]=0 -> REAL WORK at 0x54b6e8
    threadgate: LOADTHREAD REAL PATH f_005339a8 #1
    impthr: #4 cellSaveDataAutoLoad2(0x0,0x1661610,0x0,0x16614d0) lr=0x0054b754
    savedata: load 'BLES00680RDR2OPTIONS_SAV' -> no data
    impthr:         -> ret=0xffffffff8002b40b
    impthr: #5 sys_net_free_thread_context(0x0,...) lr=0x005331bc
    impthr: #8 sys_ppu_thread_exit(0xdeadc0de,...) lr=0x005331cc

## The leaf

**(a) The precise thing:** the import `cellSysutil::cellSaveDataAutoLoad2`
(NID 0xFBD5C856, import index 196), called from LoadThread's work function
`f_0054b67c` at guest `0x0054b750 -> 0x010d7f4c` (PLT stub, lr=0x0054b754).

**(b) Its arguments** as measured: `r3=0x0` (version),
`r4=0x1661610` (SAVEDATA_DIRECTORY string, buffer empty at call time),
`r5=0x0` (errDialog), `r6=0x16614d0` (setList/setBuf), matching
`auto_op`'s read of `a32(ctx,1)` = the directory name.

**(c) Its value: `0x8002b40b` = `CELL_SAVEDATA_ERROR_NODATA`**
(`refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:21`).

The host handler decides it in `runtime/src/hle/cellSaveData.cpp:298-306`:

    bool exists = fsys::exists(op.dir + "/PARAM.SFO", ec);
    if (!op.save && !exists) {
        LOG_INFO("savedata", "load '%s' -> no data", op.dir_name.c_str());
        return ERR_NODATA;
    }

`op.dir` is `vfs::host_path("/dev_hdd0/home/00000001/savedata") + "/" + name`
(`cellSaveData.cpp:154, 446`). Measured absent:

    /root/Dev/ps3base2/vfs/dev_hdd0/home/00000001/savedata/BLES00680RDR2OPTIONS_SAV/PARAM.SFO
    -> No such file or directory

## Consequence

LoadThread's work is: "load the OPTIONS save; if there is none, give up". The
three saves it asks for in sequence are RDR2OPTIONS_SAV, RDR2MPSAVE_SAV and
RDR2SAVE0_SAV, all NODATA, so it calls `sys_ppu_thread_exit(0xdeadc0de)` from
its own unconditional tail at 0x5331cc and the main thread creates a fresh
LoadThread one second later, forever. The exit is not a failure of
`f_00533064` and not a skipped work item: it is RAGE correctly reporting "no
save to load", and the fix is on the save-provisioning side, not in the thread
gate.
