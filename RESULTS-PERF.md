# P1 -- where the time goes on RDR's green screen

**Measured 2026-11-13, one run, one game instance, under `/var/lock/ps3-machine.lock`.**
Binary `build-rdr/ps3base` (695 805 304 bytes, built Nov 13 17:30), HEAD `5887a70`.
Load at run start `7.00 10.21 9.67`, at end `12.63 12.80 11.11` (`cut -d' ' -f1-3 /proc/loadavg`).

This document answers only question (A): **where the time goes**. It does not touch (B), the green.

## The headline

**The title is alive and progressing, but 17x too slowly, and the time is not in guest code.**

| quantity | on the loading card | on the green screen | ratio |
|---|---|---|---|
| frame rate (the game's own flip counter) | **29.97 fps** | **1.75 fps** | 17x slower |
| fibre switches | — | 9 098 /s | — |
| lifted guest code (PPU `f_XXXXXXXX`) share of samples | — | **0.30%** | — |
| runtime `spu::*` share of samples | — | **55.16%** | — |
| kernel/libc share of samples | — | **44.53%** | — |

The single largest consumer is `spu::aot::note_interpreted` at **12.41% self / 30.01% inclusive**,
which is the *diagnostic* path the SPU interpreter calls for every block it fails to find
precompiled. The SPU threads are not running compiled code: they are interpreting, and the
interpreter's bookkeeping is the profile.

The interpreter's cost adds up to a coherent whole -- these are separate rows of the same table:

| component | self% | what it is |
|---|---|---|
| `spu::aot::note_interpreted` | 12.41% | hash + lock + set-insert per interpreted block |
| `spu::{anon}::op_*` (all handlers, summed) | 9.07% | the actual instruction emulation |
| `spu::aot::is_terminator` | 5.77% | per-word block-length scan; inlined, so perf attributes it to `SpuThread::run` |
| `spu::step` | 3.83% | one instruction dispatch |
| `spu::aot::find` | 1.74% | the failed lookup that sends control to the interpreter |

**~33% of all samples is the SPU interpreter and its bookkeeping.** The bookkeeping
(12.41 + 5.77 + 1.74 = 19.92%) costs *more than twice* the instruction emulation it exists to
support (9.07%). That is the finding: the title is not slow because emulation is expensive, it
is slow because every interpreted block is re-hashed and re-registered under a global lock.

## Method actually used

**perf, not the gdb fallback.** `/usr/bin/perf` is the wrapper for the wrong kernel (6.8.0-106);
the working binary is `/usr/lib/linux-tools-6.8.0-149/perf` (reports `perf version 6.8.12`).
`perf_event_paranoid` is 4 and `/proc` is read-only, so it cannot be lowered -- but as root
`perf record` works anyway. It worked on the real process, first try, both windows:

    /usr/lib/linux-tools-6.8.0-149/perf record -g -F 99 -m 256 -p 322892 \
        -o /tmp/rdr-perf/run3/perf-A.data -- sleep 30
    [ perf record: Woken up 1 times to write data ]
    [ perf record: Captured and wrote 1.837 MB /tmp/rdr-perf/run3/perf-A.data (18124 samples) ]

    /usr/lib/linux-tools-6.8.0-149/perf record -g --call-graph dwarf,16384 -F 99 -m 256 \
        -p 322892 -o /tmp/rdr-perf/run3/perf-B.data -- sleep 15
    [ perf record: Woken up 282 times to write data ]
    [ perf record: Captured and wrote 144.177 MB /tmp/rdr-perf/run3/perf-B.data (9072 samples) ]

Window A is frame-pointer callgraph (30 s), window B is DWARF callgraph (15 s). **They agree**
(top symbol 16.91% vs 17.99%; class shares 55.16/44.53 vs 55.57/44.15), which is the check that
the frame-pointer unwind is not inventing the picture.

`-g` defaults to `fp` for user space, and the build is `RelWithDebInfo` (`-O2 -g`, no
`-fno-omit-frame-pointer`), so the fp chains are real but shallow -- several SPU frames show the
leaf as the root. That is why window B (DWARF) exists as the cross-check.

### The harness had three defects; they are fixed in the copy used here

`tools/green_screen_probe.sh` as committed cannot produce this measurement:

1. **It starts two game instances.** Lines 27-30 launch `$BIN --no-launcher` directly, then
   line 35 runs `tools/drive_rdr.py`, whose `run()` launches the title again with
   `subprocess.Popen`. Two guests, which breaks the one-game rule. My copy
   (`/tmp/rdr-perf/probe3.sh`) lets the driver be the only launcher.
2. **Killing the driver kills the guest.** `drive_rdr.py`'s `finally` block SIGKILLs its child;
   `kill "$DRV"` (SIGTERM) therefore never leaves a guest to profile. The copy uses `kill -9`
   on the driver, which cannot be caught, so the `finally` never runs and the guest survives.
3. **The "solid screen" test is wrong.** `%[fx:standard_deviation]` is 0..1, not 0..255, so a
   `< 3.0` threshold matches every frame including the red chapter card. My first attempt
   profiled the **chapter card** for exactly this reason; that run is discarded, not reported.
   The copy tests the actual clear colour instead (below).

## How the green screen was identified (so the profile is of the right state)

The test is the one RESULTS-S3.md validated, re-verified here against
`/tmp/rdr-crash-s3/run1/shots`: fraction of pixels with **G>60, R<40, B<40** on a 160x90
downscale (`/tmp/rdr-perf/green.sh`).

    $ for f in $(ls | sed -n '300p;370p;380p;400p;600p;900p'); do printf '%-46s green=' "$f"; green.sh "$f"; done
    shot-00300-...png  green=0.0000 14400     <- loading / card
    shot-00370-...png  green=0.0000 14400     <- card, still animating
    shot-00380-...png  green=0.8744 14400     <- green arriving (fade)
    shot-00400-...png  green=0.9062 14400     <- green complete
    shot-00600-...png  green=0.9060 14400
    shot-00900-...png  green=0.9063 14400

The measured clear colour is `srgb(0,145,0)`, 89-90% of the frame (the rest is black bars).

**The profiled state is confirmed green, not assumed.** The sampler grabbed the screen once per
second for the whole profile window, and every one of those 34 frames measures 0.906:

    $ for f in $(ls live-*.png | sort -V); do printf '%-14s ' $f; green.sh $f; done
    live-1.png     0.9064 14400
    live-2.png     0.9064 14400
    ...            (all 34 identical)
    live-34.png    0.9064 14400

Green onset in this run: 92 s after the chapter card was first seen
(`[18:50:02] GREEN screen at sample 222: green_frac=0.8740`), complete 8 s later.

## 1. Top 30 functions by self time, classified

`perf script -i run3/perf-A.data --no-inline` parsed by `/tmp/rdr-perf/top30.py`.
Classification: lifted = `f_<8 hex>` / `f_<8 hex>_body`; HLE = `hle_<module>_<name>`;
kernel/libc = the DSO is not `ps3base`; runtime = anything else inside `ps3base`.

| # | self% | class | symbol | dso |
|---|-------|-------|--------|-----|
| 1 | 16.91% | runtime | spu::SpuThread::exec | ps3base |
| 2 | 12.41% | runtime | spu::aot::note_interpreted | ps3base |
| 3 | 7.42% | kernel/libc | pthread_mutex_lock | libc.so.6 |
| 4 | 5.77% | runtime | spu::aot::is_terminator | ps3base |
| 5 | 3.83% | runtime | spu::step | ps3base |
| 6 | 1.74% | runtime | spu::aot::find | ps3base |
| 7 | 1.54% | kernel/libc | pthread_mutex_lock (2nd site) | libc.so.6 |
| 8 | 1.41% | runtime | spu::{anon}::op_shufb | ps3base |
| 9 | 1.38% | kernel/libc | pthread_mutex_unlock | libc.so.6 |
| 10 | 1.04% | kernel/libc | entry_SYSCALL_64 | [kernel.kallsyms] |
| 11 | 1.04% | kernel/libc | __GI___lll_lock_wait | libc.so.6 |
| 12 | 1.00% | kernel/libc | [unknown] | [vdso] |
| 13 | 0.97% | kernel/libc | futex_wake | [kernel.kallsyms] |
| 14 | 0.94% | runtime | std::_Hashtable<unsigned long,...>::_M_insert_unique | ps3base |
| 15 | 0.83% | kernel/libc | getenv | libc.so.6 |
| 16 | 0.73% | runtime | rsv::store_range | ps3base |
| 17 | 0.71% | kernel/libc | pthread_mutex_unlock (2nd site) | libc.so.6 |
| 18 | 0.67% | kernel/libc | futex_q_unlock | [kernel.kallsyms] |
| 19 | 0.58% | kernel/libc | _raw_spin_lock | [kernel.kallsyms] |
| 20 | 0.54% | runtime | lv2::sys_rsx_context_allocate | ps3base |
| 21 | 0.52% | runtime | hle::run_runnable_fibers | ps3base |
| 22 | 0.51% | kernel/libc | native_queued_spin_lock_slowpath | [kernel.kallsyms] |
| 23 | 0.47% | runtime | spu::{anon}::op_fma | ps3base |
| 24 | 0.47% | kernel/libc | __get_user_nocheck_4 | [kernel.kallsyms] |
| 25 | 0.45% | runtime | spu::{anon}::op_lqd | ps3base |
| 26 | 0.45% | kernel/libc | native_queued_spin_lock_slowpath | [kernel.kallsyms] |
| 27 | 0.44% | kernel/libc | futex_q_lock | [kernel.kallsyms] |
| 28 | 0.44% | runtime | spu::{anon}::op_lqx | ps3base |
| 29 | 0.43% | kernel/libc | try_to_wake_up | [kernel.kallsyms] |
| 30 | 0.41% | kernel/libc | native_queued_spin_lock_slowpath | [kernel.kallsyms] |

Top 30 = 66.06% of samples. Reproduce with:

    cd /tmp/rdr-perf && ulimit -v 2000000 && timeout 300 python3 top30.py run3/perf-A.data 30

### Share of each class

    $ python3 classify.py run3/perf-A.data --top 1
    == class shares of 18124 samples ==
    runtime      55.16%  (9998)
    kernel/libc  44.53%  (8071)
    lifted        0.30%  (54)
    HLE           0.01%  (1)

Window B (DWARF, independent):

    == class shares of 9072 samples ==
    runtime      55.57%  (5041)
    kernel/libc  44.15%  (4005)
    lifted        0.28%  (25)
    HLE           0.01%  (1)

**Lifted guest code is 0.30% of the profile.** The 54 lifted samples are spread over 8 distinct
functions (max 3 samples each: `f_00ea5b80`, `f_0063e648`, `f_00685820`, ...). There is no hot
guest function, because the guest is not running.

### Inclusive share -- this is the finding

Self time says which function is on the CPU; inclusive says how much of the run passes through it:

    $ python3 inclusive.py run3/perf-A.data 'note_interpreted' 'spu::step' 'aot::find' \
          'is_terminator' 'SpuThread::exec' 'run_runnable_fibers' 'pthread_mutex_lock'
    symbol (anywhere in stack)                 samples    share
    note_interpreted                              5439   30.01%
    spu::step                                      703    3.88%
    aot::find                                      316    1.74%
    is_terminator                                 1049    5.79%
    SpuThread::exec                               3071   16.94%
    run_runnable_fibers                             96    0.53%
    pthread_mutex_lock                            1768    9.76%

`spu::aot::note_interpreted` (runtime/src/spu/aot.cpp:130) is called from
`runtime/src/spu/thread.cpp:2397` -- the **interpreter fallback path**, reached only when
`aot::find()` fails to match a compiled block. It hashes the block (FNV, up to
`MAX_BLOCK_WORDS` words, calling `is_terminator` per word), takes a global mutex, and inserts
into an `unordered_set`. That is why `is_terminator` (5.77%), `_M_insert_unique` (0.94%) and
`pthread_mutex_lock` (7.42% + 1.54%) all sit in the top 15: **they are the cost of noticing,
over and over, that a block was never compiled.**

Caller attribution confirms it:

    $ python3 callers.py run3/perf-A.data 'pthread_mutex_lock' --depth 3
    samples: 18124, leaf matching 'pthread_mutex_lock': 1762 (9.72%)
    == immediate callers ==
     78.09% of hits  1376  spu::SpuThread::run
     21.51% of hits   379  spu::aot::note_interpreted
    ...

    $ python3 callers.py run3/perf-A.data 'SpuThread::exec' --depth 3
    samples: 18124, leaf matching 'SpuThread::exec': 3065 (16.91%)
    == immediate callers ==
    100.00% of hits  3065  spu::SpuThread::run

The log confirms AOT was enabled and 19 584 blocks were compiled, so this is not
"the AOT index is empty" -- it is a working compiled set that does not cover the blocks the
title is running on this screen:

    $ grep 'compiled SPU blocks' run3/drv/run.log
    I   0.452807 [323054] spu: 19584 compiled SPU blocks

## 2. CPU time per thread

From `/proc/<pid>/task/<tid>/stat` fields 14/15 (utime+stime), sampled at 1 Hz by the harness;
`CLK_TCK=100` (`getconf CLK_TCK`). Window: 46.0 s of wall clock, 46.397 s of guest log clock.
The 47 threads are named by the runtime (`host::set_thread_name`, runtime/src/host.cpp:391).

    $ python3 rates.py run3/samples.txt
    == per-thread CPU over the window (utime+stime, CLK_TCK=100) ==
    tid      comm                     cpu_s    user_s    sys_s    cpu%
    323057   SPU_3_PrimaryCe          45.58     39.42     6.16    99.1
    323055   SPU_1_PrimaryCe          45.56     39.61     5.95    99.0
    323056   SPU_2_PrimaryCe          45.48     39.57     5.91    98.9
    323058   SPU_4_PrimaryCe          45.35     38.08     7.27    98.6
    323108   RSX_FIFO                 44.99     15.15    29.84    97.8
    323061   SPU_0_Secondary          23.23     19.45     3.78    50.5
    323054   SPU_0_PrimaryCe          23.14     18.49     4.65    50.3
    323050   PPU_main_thread           3.31      2.95     0.36     7.2
    323909   PPU_[RDR2]_Rend           2.76      2.03     0.73     6.0
    324017   PPU_[RAGE_Audio           1.38      0.99     0.39     3.0
    322894   Window                    1.03      0.39     0.64     2.2
    326620   PPU_AsyncProbeM           0.33      0.20     0.13     0.7
    328580   PPU_[RAGE]_crmt           0.23      0.20     0.03     0.5
    323048   [vkps]_Update             0.18      0.04     0.14     0.4
    326236   PPU_[RAGE]_netC           0.12      0.10     0.02     0.3
    323049   ps3base                   0.11      0.03     0.08     0.2
    324010   cellAudio                 0.06      0.03     0.03     0.1
    323109   PPU__gcm_intr_t           0.05      0.02     0.03     0.1
    323107   RSX_VBlank               0.04      0.00     0.04     0.1
    (remaining 28 threads: 0.02 s or less each; full table in the command output)
    TOTAL    47 threads              282.99                       615.2

**615% CPU across 47 threads, and the guest main thread is 7.2% of it.** Six SPU threads and
the RSX FIFO thread take 275 of the 283 CPU-seconds. This is the shape of the problem: the
machine is busy, the title's own main thread is nearly idle, and the frame rate is 1.75 fps.

The RSX FIFO thread is 97.8% busy but **87.7% of that is idle-wait**, per its own accounting:

    $ grep 'fifo 5s:' run3/drv/run.log | awk '$2>250 && $2<320'
    253.296458 5s: 32.0%, 0.0%, wait (30/s), idle
    258.296812 5s: 15.5%, 73.6%, wait (5/s), idle
    263.297393 5s: 12.6%, 87.4%, wait (0/s), idle
    ...
    318.316376 5s: 11.4%, 88.6%, wait (0/s), idle

(busy%, idle%, semaphore-wait). Before the green it was ~30% busy with 30 semaphore waits/s;
on the green it is ~12% busy with **0 semaphore waits/s** -- the guest has stopped submitting
work to the FIFO. The thread is spinning on an empty queue, not doing work.

## 3. Fibre rates

Two independent methods, agreeing.

**(a) Exact, from the guest log** (`/tmp/rdr-perf/logwin.py` counts the log's own timestamps over
the window 270.19..316.59 guest-seconds = 46.400 s):

    $ python3 logwin.py run3/drv/run.log 270.19 316.59
    == fibre rates over 46.400 s of guest clock ==
    CreateFiber        243       5.24 /s
    RunFibers           82       1.77 /s
    JoinFiber          246       5.30 /s
    switches        422155     9098.2 /s
    switches per RunFibers call: 5148.2

**(b) 1 Hz /proc sampler over the same window** (46.0 s wall):

    CreateFiber/s       5.28   (3528 -> 3771)
    RunFibers/s         1.76   (1688 -> 1769)
    JoinFiber/s         5.28   (3528 -> 3771)
    switches/s        9150.0   (9183372 -> 9604272)
    switches per RunFibers call: 5196.3

**~5 200 fibre switches per RunFibers call, ~9 100 switches/s.** For scale, the same title on the
loading card (from the earlier 900 s run, `/tmp/rdr-crash-s3/run1/drv/run.log`, t 450-480) does
5 342 switches/s -- so the switch rate is not itself anomalous. What is anomalous is that the
main thread does this **instead of** running game code: `hle::run_runnable_fibers` is 0.52% self
and 0.53% inclusive, i.e. the fibre machinery is cheap, and the main thread is only 7.2% of CPU.
The fibre churn is a symptom of how the title is structured (a job-per-frame pattern), not the
cost.

## 4. Is the game making progress? Yes -- slowly, and the counters say exactly how slowly

Three independent counters over the same window.

**(i) The game's own frame counter** (`g_rsx.flips`, printed every 60 flips by
runtime/src/lv2/rsx.cpp:182):

    $ awk '/rsx: flip/ {print $2, $6}' run3/drv/run.log | tail -6
    246.489554 5821
    248.492536 5881
    250.493729 5941
    252.495506 6001
    261.121055 6061
    295.330678 6121

    $ ... same lines, differenced
    t= 250.494 flip=  5941  +60 flips in   2.001 s =  29.98 fps
    t= 252.496 flip=  6001  +60 flips in   2.002 s =  29.97 fps
    t= 261.121 flip=  6061  +60 flips in   8.626 s =   6.96 fps    <- green arriving
    t= 295.331 flip=  6121  +60 flips in  34.210 s =   1.75 fps    <- green complete

**The counter is advancing, not frozen: 60 flips every 34.2 s = 1.75 fps.** The title is
progressing at 1/17th of the 29.97 fps it held on the loading card. This is "the game is
progressing slowly", not "the game is alive but not progressing".

**(ii) RAGE script opcodes per second -- NOT MEASURED, and here is why.** The task suggested
counting them via the interpreter `f_00d19278`. That function does **not appear in the profile
at all**:

    $ perf script -i run3/perf-A.data --no-inline | grep -c 'd19278'
    0
    $ perf script -i run3/perf-B.data --no-inline | grep -c 'd19278'
    0

It is not on the CPU during the green screen, so counting its opcodes would measure nothing.
What *is* visible is the guest's own per-frame write counter, which the runtime prints every 30 s:

    $ grep 'main: pgw' run3/drv/run.log | awk '{gsub(/,/,"",$6); print $2, $6}'
    t=  60.4 writes=  7183   delta= 4768   rate= 158.9/s
    t=  90.4 writes= 10408   delta= 3225   rate= 107.5/s
    t= 120.4 writes= 11295   delta=  887   rate=  29.6/s
    t= 150.4 writes= 14759   delta= 3464   rate= 115.5/s
    t= 180.4 writes= 17689   delta= 2930   rate=  97.7/s
    t= 210.4 writes= 20712   delta= 3023   rate= 100.8/s
    t= 240.5 writes= 23683   delta= 2971   rate=  99.0/s
    t= 270.5 writes= 25521   delta= 1838   rate=  61.3/s
    t= 300.5 writes= 26447   delta=  926   rate=  30.9/s

The guest keeps writing to GPU memory, but the rate falls from ~100/s to ~31/s as the green
takes hold -- consistent with 1.75 fps.

**(iii) File reads per second: zero, and this is a real measurement, not a missing one.**
`PS3BASE_FS_LOG=1` was set for this run (`probe3.sh` line 49), which enables the per-read line
at runtime/src/lv2/fs.cpp:292. The count is 1 for the whole run and it does not advance:

    $ python3 logwin.py run3/drv/run.log 270.19 316.59
    fs: read fd lines 0   0.000 /s

**The green screen does no file I/O at all.** The loading is genuinely finished; nothing is
being read.

## Where the time goes -- the statement

1. **The title is alive and progressing at 1.75 fps** (its own flip counter: 60 flips / 34.2 s),
   against 29.97 fps on the loading card. It is not frozen and it is not crashed.
2. **Only 0.30% of sampled CPU time is in lifted guest code.** Whatever the title is doing, it
   is not spending its time executing the recompiled PPU code. The 54 lifted samples are spread
   over 8 functions with no hot one.
3. **55.16% is runtime, and the runtime's largest single item is the SPU interpreter's
   "I have seen this block before" bookkeeping**: `spu::aot::note_interpreted` 12.41% self /
   30.01% inclusive, plus `is_terminator` 5.77% and `aot::find` 1.74%. The SPU threads are
   executing interpreted blocks, and paying a hash, a global lock and a set insert per block to
   record that fact. Mutex time is 7.42% + 1.54% + 1.38% + 0.71% of `pthread_mutex_lock/unlock`;
   of the lock samples, **78.09% are called directly from `SpuThread::run`** (the
   `check_interrupt` mutex `m`, runtime/src/spu/thread.cpp:194) and **21.51% from
   `note_interpreted`** (`g_interpreted_lock`, aot.cpp:146). These are two different mutexes and
   I did not attribute them further; what the split does establish is that lock contention is
   concentrated in the SPU run loop, not in the guest code.
4. **44.53% is kernel/libc**, dominated by futex/spinlock/syscall traffic (futex_wake 0.97%,
   `__lll_lock_wait` 1.04%, `native_queued_spin_lock_slowpath` 0.51%+0.45%+0.41%,
   `entry_SYSCALL_64` 1.04%) -- the same contention seen from the kernel side.
5. **615% CPU is being spent, and the guest main thread is 7.2% of it.** Six SPU threads at
   ~99% each and the RSX FIFO thread at 97.8% (87.7% of which is its own idle-wait, with
   **0 semaphore waits/s** -- the guest has stopped feeding the FIFO) account for 275 of the
   283 CPU-seconds.
6. **The fibre churn is not the cost.** ~9 100 switches/s across ~5 200 switches per RunFibers
   call, with `hle::run_runnable_fibers` at 0.53% inclusive. It is the title's job-per-frame
   structure showing up, not the thing to optimise.
7. **No file I/O happens on the green screen** (0 `fs: read fd` lines/s with `PS3BASE_FS_LOG=1`
   set): the load has genuinely completed.

**The one sentence:** the green screen is slow because the SPU threads are interpreting blocks
instead of running compiled ones, and the interpreter's per-block diagnostic bookkeeping
(`note_interpreted` + `is_terminator` + `aot::find` + the lock around them) is the single
largest consumer of a machine that is already at 615% CPU -- while the title's own main thread
sits at 7.2% and the frame counter crawls at 1.75 fps.

## What was NOT measured, stated plainly

- **RAGE script opcodes/s.** Not countable without instrumenting (which the task forbids), and
  the interpreter `f_00d19278` is absent from both profiles (0 samples), so there is nothing to
  count. Substituted with the runtime's own `main: pgw` guest-write counter, which is reported.
- **Per-second breakdown of the 30 s perf window.** perf's `-F 99` sampling gives an aggregate
  over the window, not a per-second series; the per-second series comes from the 1 Hz /proc
  sampler and the guest log instead.
- **The discarded run.** The first attempt (`/tmp/rdr-perf/run2`) profiled the red chapter card
  because of the `standard_deviation` 0..1 bug described above. Its data is retained on disk
  but is not used for any number in this document.

## Reproduce

    # one instance, under the machine lock
    cd /root/Dev/ps3base2
    tools/machine_lock.sh status                      # must be "free"
    pgrep -f -- 'build-rdr/ps3base --no-launcher'     # must print nothing
    tools/machine_lock.sh run 'p1 RDR green-screen profile' -- \
        /tmp/rdr-perf/probe3.sh /tmp/rdr-perf/run3

    # analysis (all under a memory cap, per AGENTS.md)
    cd /tmp/rdr-perf && ulimit -v 2000000
    python3 top30.py     run3/perf-A.data 30
    python3 classify.py  run3/perf-A.data --top 30 --by-thread
    python3 callers.py   run3/perf-A.data 'pthread_mutex_lock' --depth 3
    python3 inclusive.py run3/perf-A.data 'note_interpreted' 'is_terminator' 'aot::find'
    python3 rates.py     run3/samples.txt
    python3 logwin.py    run3/drv/run.log 270.19 316.59

Artifacts: `/tmp/rdr-perf/run3/` (perf-A.data, perf-B.data, samples.txt, orch.log,
driver.log, drv/run.log, shots/), tooling in `/tmp/rdr-perf/*.py` and `probe3.sh`.
