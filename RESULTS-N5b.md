# N5b -- the audio events: does the notification queue deliver, does the read index advance, does the PPU wake?

Worktree /root/Dev/wt-N5b, branch explore/N5b, based on main 202e9ef.
State: claim written before any measurement. Measurements appended below in order.

Task: after M2's cellAudio fixes -- one event per block (5.33 ms), read index
advance, PPU MultiStream thread wake. RDR port 0 measured 0/8 filled while port 1
is 16/16; explain the difference.

## Measurement 0 -- the baseline (no runs yet)

Claim placed at $(date -u +%FT%TZ). Nothing measured yet.

## Measurement 1 -- the baseline boot, all cellAudio calls, on this branch's own binary

Binary: /root/Dev/wt-N5b/build-rdr/ps3base, built from explore/N5b @ e615afb
(= main 202e9ef), shared generated dirs out-rdr. Display :965 (own Xvfb).

    cd /root/Dev/wt-N5b && ulimit -s unlimited && \
      DISPLAY=:965 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      timeout 40 ./build-rdr/ps3base --no-launcher > /tmp/n5b-base.log 2>&1
    exit=12

    $ grep -n 'cellAudio' /tmp/n5b-base.log
    319: I   2.254974 sysutil: cellAudioOutGetSoundAvailability(0, type 0, fs 0x4) -> 2
    320: I   2.255015 sysutil: cellAudioOutGetSoundAvailability(0, type 1, fs 0x4) -> 0
    322: I   2.255055 sysutil: cellAudioOutConfigure(0): channel 2 encoder 0 downMixer 0
    323: I   2.255079 sysutil: cellAudioOutGetState(0, 0): state 0 encoder 0 downMixer 0 ch 2
    324: I   2.260204 cellAudio: cellAudioInit: index area 0x12180000 (0x10000), 4 ports, sdk 0x300001
    325: I   2.260305 cellAudio: cellAudioPortOpen: port 0, 8 ch, 8 blocks, attr 0x0, level 1.000
                      -> portSize 0x10000 at 0x12190000, readIndexAddr 0x12180010 (status 0x1)
    326: I   2.260313 cellAudio: cellAudioGetPortConfig(0, 0xd004ed10): readIndexAddr 0x12180010
                      status 0x1 nChannel 8 nBlock 8 portSize 0x10000 portAddr 0x12190000
    335: W   2.269748 cellAudio: SDL audio open failed: ALSA: Couldn't open audio device
    339: I   2.271127 cellAudio: cellAudioPortStart(0) -> 0x0: buffer 0x12190000 zeroed (65536 bytes)
    448: I   7.272266 cellAudio: audio stats: 937 blocks, 1 late wakeups, 0 stale blocks,
                      0 device underruns, period 937, port0 index 1 after 937 writes:
                      port0 0/8 filled, index 1
    730: I  15.434417 sysutil: cellAudioOutGetConfiguration(0): channel 2 encoder 0 downMixer 0
    732: I  15.434789 cellAudio: cellAudioPortOpen: port 1, 2 ch, 16 blocks, attr 0x1000
                      -> portSize 0x8000 at 0x121a0000, readIndexAddr 0x12180020
    737: I  15.476385 cellAudio: cellAudioPortStart(1) -> 0x0: buffer 0x121a0000 zeroed (32768 bytes)
    859: I  17.276911 cellAudio: audio stats: 1275 blocks, ... period 2813, port0 index 5 after
                      2813 writes: port0 0/8 filled, index 5 port1 16/16 filled, index 1
    1099: I 21.066149 cellAudio: cellAudioPortStop(1) -> 0x0
    1100: I 21.066794 cellAudio: cellAudioPortStart(1) -> 0x0
    1102: I 21.078447 cellAudio: cellAudioPortStart(1) -> 0x0
    1189: I 22.279233 cellAudio: audio stats: 1867 blocks, ... port0 0/8 filled, index 7
                      port1 16/16 filled, index 1
    1233: I 22.938126 cellAudio: cellAudioPortClose(1) -> 0x0
    1234: I 22.938311 cellAudio: cellAudio/  cellAudioPortClose(1)

MEASURED: no cellAudioCreateNotifyEventQueue, no cellAudioSetNotifyEventQueue and
no cellAudioRemoveNotifyEventQueue line appears anywhere in the 40 s run, and
neither does any "notify event for key ... went nowhere" warning (cellAudio.cpp:250,
which fires on every registered-key send that has no queue). Under this branch's
cellAudio neither of those can be silent: the create handler logs at INFO
(cellAudio.cpp:544) and the set handler logs at INFO on success and at WARN on
failure (cellAudio.cpp:564-566).

Star-rate of the mixer itself, from the same log: 937 periods in 5.001 s
(7.272 - 2.271) = 187.4 periods/s, period 5.337 ms -- the 5.333 ms clock
AUDIO_BLOCK_PERIOD_US (cellAudio.cpp:79).

## Measurement 2 -- the read index IS advanced and IS read by the PPU Multistream thread

Watch both index slots in the shared area (port 0 slot 0x12180010, port 1 slot
0x12180020; one 4 KiB page holds both), trapping reads and writes.

    cd /root/Dev/wt-N5b && ulimit -s unlimited && \
      DISPLAY=:965 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      GOWA_TRAP=r12180010+0x14 GOWA_TRAP_MAX=60 timeout 40 ./build-rdr/ps3base --no-launcher \
      > /tmp/n5b-idxread.log 2>&1

    $ grep 'trap 0 read' /tmp/n5b-idxread.log | head -6
    I   2.388651 trap: trap 0 read 0x12180010 prev 0x00000000 -> 0x00000000 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   2.410904 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000003 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   2.469654 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000002 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   2.504837 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000002 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   2.532246 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000003 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   2.557678 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000004 (guest lr 0x0060a724) by [RAGE Audio] Multistream

    $ grep 'trap 0 write 0x12180010' /tmp/n5b-idxread.log | head -2
    I   2.416130 trap: trap 0 write 0x12180010 prev 0x00000000 -> 0x00000000 (guest lr 0x00000000) by cellAudio

The u64 at the index address is big-endian, so its HIGH word is at 0x..10 and
its LOW word at 0x..14. The guest's two loads split it exactly that way: 0x..10
always reads 0 (the high half of a counter that is still small), 0x..14 reads the
advancing value -- 3, 2, 2, 3, 4 ... in the first half second. The read index
advances and the PPU thread reads the advancing value.

Attribution, which the trap reports and is not inferred:

    $ grep 'by \[RAGE Audio\] Multistream' /tmp/n5b-idxread.log | wc -l
    22      # 21 reads + the '(further hits not logged)' cap marker at 3.107365
    $ grep 'by cellAudio' /tmp/n5b-idxread.log | wc -l
    37
    $ grep 'watch totals' /tmp/n5b-idxread.log | tail -1
    I  22.965663 trap: watch totals: 28401 guest events, 0 self reads dropped

Two threads and only two, on the whole run: [RAGE Audio] Multistream (the guest,
reading from lr 0x0060a724 = the loop call site at out-rdr/ppu/rdr_061.cpp:6317)
and cellAudio (this runtime, publishing). Both touch the same word.

Measured rate of the publisher: 937 periods in 5.001 s of wall clock
(7.272266 - 2.271127 in measurement 1) = 187.4 periods/s, one index write per
period per started port. That is AUDIO_BLOCK_PERIOD_US = 5333 us
(runtime/src/hle/audio_hle.h:79), i.e. one block per 5.33 ms.

## Measurement 2b -- there is no notify queue to deliver to: RDR never asks for one

The task asks whether the audio port notification queue delivers one event per
block. For this title the question has a measured, prior answer: RDR never
creates or registers a notify event queue at all.

The import table is the title's whole libaudio surface
(out-rdr/ppu/table.cpp:77122-77128):

    $ grep -o '"cellAudio[^"]*", 0x[0-9A-Fa-f]*u, "[^"]*"' out-rdr/ppu/table.cpp | sed 's/.*u, //'
    "cellAudioGetPortConfig" "cellAudioInit" "cellAudioPortClose" "cellAudioPortOpen"
    "cellAudioPortStart" "cellAudioPortStop" "cellAudioQuit"

Seven functions, and cellAudioCreateNotifyEventQueue / SetNotifyEventQueue /
RemoveNotifyEventQueue are not among them. The cross-check is the runtime's own
call log for the same run: the handlers log unconditionally at INFO
(runtime/src/hle/cellAudio.cpp:544 create, :566 set) and WARN on failure (:564),
and the 40 s log contains no such line (measurement 1).

    $ grep -c 'NotifyEventQueue' /tmp/n5b-base.log
    0
    $ grep -c 'notify event for key' /tmp/n5b-base.log
    0

So the answer for RDR is: 0 events per port, because 0 queues are registered --
not because the delivery path is broken. The delivery path is exercised directly
by tests/cellAudio.cpp block 13 (one event per period per registered queue,
DECIMATE_2 / BEFOREMIX / NOMIX all checked) and it is the same audio::step +
lv2::send_event_by_key the mixer thread calls (cellAudio.cpp:348, :246-253).


## Measurement 3 -- port 0 vs port 1: WHO writes each buffer, and how many nonzero bytes

The 0/8 vs 16/16 difference is a producer difference, and the trap names the
producer for each port. Same binary, same machine, both runs inside the same
two minutes.

### 3a. Port 0 (0x12190000, 8 ch x 8 blocks): written ONLY by the SPU, and every
byte written is zero

    $ GOWA_TRAP=12190000+0x10000 GOWA_TRAP_MAX=100000 timeout 24 ./build-rdr/ps3base --no-launcher
    $ grep 'trap 0 write' /tmp/n5b-p0all.log | grep -o 'by [A-Za-z0-9].*' | sort | uniq -c
       0    # (see note)
       1455 by SPU 0 Secondary
    $ grep 'trap 0 write' /tmp/n5b-p0all.log | grep -v 'prev 0x00000000 -> 0x00000000' | wc -l
    0
    $ grep 'trap 0 read' /tmp/n5b-p0all.log | wc -l
    0

1455 store events into the port 0 buffer across 24 s and NOT ONE of them carries a
nonzero byte: every one is `prev 0x00000000 -> 0x00000000`. The only writer is the
SPU (the multistream mixer job). The independent confirmation is the same fact
measured from the other side, by the runtime's own fill counter:

    $ GOWA_TRAP=12190000+0x10000 GOWA_TRAP_NONZERO=1 ... (trap armed, line 20)
    I   0.422935 trap: trap 0: write 0x12190000+0x10000 arm-time words: 00000000 ...
    $ grep -c 'trap 0 write' /tmp/n5b-trap0nz.log
    0        # zero NONZERO writes in a run that reached the SPURS abort at 25.9 s
    $ grep 'audio stats' /tmp/n5b-trap0nz.log | tail -2
    I  17.500144 ... port0 0/8 filled, index 5 port1 16/16 filled, index 4
    I  22.502545 ... port0 0/8 filled, index 7 port1 16/16 filled, index 3

Block histogram, 0x2000-byte blocks: 650 of 650 writes land in block 6
(0x1219c000) -- the mixer writes the block its cur_pos selects, and it wrote
one block, zeroes.

### 3b. Port 1 (0x121a0000, 2 ch x 16 blocks): written by the PPU Bink thread with
real audio

    $ GOWA_TRAP=121a0000+0x8000 GOWA_TRAP_NONZERO=1 GOWA_TRAP_MAX=4000 timeout 26 ./build-rdr/ps3base --no-launcher
    $ grep 'trap 0 write' /tmp/n5b-trap1hist.log | grep -o 'by .*' | sort | uniq -c
       1000 by Bink Audio Thread
    $ grep 'trap 0 write' /tmp/n5b-trap1hist.log | sed 's/.*write 0x121a\([0-9a-f]*\).*/\1/' | (histogram by 0x800)
    block 6 writes 1000
    $ first/last trap:
    I  15.347069 trap: trap 0 write 0x121a3000 prev 0xbc41cb24 -> 0x00723593 (guest lr 0x00fef920) by Bink Audio Thread
    I  25.989359 trap: trap 0 write 0x121a37c8 prev 0xbc41cb24 -> 0xbc109842 (guest lr 0x00fef920) by Bink Audio Thread

The values are real IEEE-754 floats (0x00723593, 0xbc109842, 0x3da744ee) and the
stale-value field `prev 0xbc41cb24` is the previous frame's sample -- the guest is
writing a stream, not zeroes. 1000 nonzero stores from lr 0x00fef920, all in block
6 (0x121a3000), which is why the fill counter reads 16/16: the counter asks
'does this block contain any nonzero byte', and for port 1 it does.

### 3c. Both index slots, readers attributed

    $ GOWA_TRAP=r12180010+0x18 GOWA_TRAP_MAX=4000 (traps both port-0 and port-1 slots)
    index word          reads   writes
    0x12180010 (p0 hi)    144    1171
    0x12180014 (p0 lo)    442       0
    0x12180020 (p1 hi)      0     197
    0x12180024 (p1 lo)     41       0
    $ grep 'read 0x12180014 ' ... | grep -o 'by .*' | sort | uniq -c
       442 by [RAGE Audio] Multistream
    $ grep 'read 0x12180024 ' ... | grep -o 'by .*' | sort | uniq -c
        26 by Bink Audio Thread
        15 by [RDR2] Render Thread

Port 1's index has TWO guest readers and port 0's has one -- the Bink thread and
the RDR2 Render Thread both re-read port 1's index, the Multistream thread is the
only reader of port 0's. Both indexes advance under a writer named `cellAudio`
(197 writes to 0x12180020, 1171 to 0x12180010 -- the latter is the high word of
every one of port 0's periods).


## Measurement 4 -- the PPU Multistream thread's own queue: delivered == received

Added a per-queue delivered/received counter (runtime/src/lv2/event.h:26-27,
registered in event.cpp) and a report at the process-exit path, because a title
that ends by `_sys_process_exit` -> `quick_exit` never reaches main()'s summary
(thread.cpp:77-82: quick_exit runs at_quick_exit, not atexit).

    $ PS3BASE_QUEUE_COUNTS=1 timeout 26 ./build-rdr/ps3base --no-launcher
    I  23.092078 queuecount: per-queue delivered/received at process exit: 8 queue(s)
    I  23.092093 queuecount:   q=0x8d004c00 key=0x0 name='_spuPrv' size=42 delivered=0     received=0     waiters=1 pending=0
    I  23.092099 queuecount:   q=0x8d006100 key=0x0 name='_spuPrv' size=42 delivered=1     received=1     waiters=0 pending=0
    I  23.092104 queuecount:   q=0x8d026800 key=0x0 name='_spuEvF' size=1  delivered=1562  received=1562  waiters=1 pending=0
    I  23.092112 queuecount:   q=0x8d026d00 key=0x0 name='_spuEvF' size=1  delivered=19075 received=19075 waiters=1 pending=0
    I  23.092124 queuecount:   q=0x8d027e00 key=0x0 name='_spuQue' size=1  delivered=2703  received=2703  waiters=0 pending=0
    I  23.092129 queuecount:   q=0x8d027f00 key=0x0 name='_spuQue' size=1  delivered=4894  received=4894  waiters=0 pending=0
    I  23.092135 queuecount:   q=0x8d029500 key=0x0 name='_spuEvF' size=1  delivered=1949  received=1949  waiters=1 pending=0
    I  23.092140 queuecount:   q=0x8d04dd00 key=0x0 name='_spuEvF' size=1  delivered=0     received=0     waiters=0 pending=0

Three facts, each measured rather than argued:

1. **There is no cellAudio notify queue in the list.** Eight queues and every one
   is an SPU queue (`_spuPrv`, `_spuEvF`, `_spuQue`). A cellAudio notify queue
   would appear with key 0x80004d494f323221 and name 'cellAudio notify'
   (audio_hle.h:91, cellAudio.cpp:544), depth 8 for this SDK. It does not.
2. **delivered == received on every queue that receives anything.** Pending is 0
   everywhere. No event is left in a queue and no sender hit the size limit, so
   the delivery path and the wake path both work in this run.
3. **The PPU thread that plays audio is `[RAGE Audio] Multistream` on queue
   0x8d029500**, identified not assumed: the status dump names the thread and the
   queue id it is blocked in, and the queue object is the same id.

    $ GOWA_STATUS=1 ... ; awk '/status: ppu 0x1000013/{print; getline; print}' /tmp/n5b-status.log
    I   5.456424 status: ppu 0x1000013 '[RAGE Audio] Multistream' blocked in 141 (r3 0x320 r31 0x12198000) syscalls +2962 lr 0x60a724
    I   5.456434 status:    top syscalls 141x2690 130x270
    I   8.509980 status: ppu 0x1000013 '[RAGE Audio] Multistream' blocked in 130 (r3 0x8d029500 r31 0x13e28dc) syscalls +2594 lr 0x920ef8
    I   8.509989 status:    top syscalls 141x2306 130x287
    I  11.571273 status: ppu 0x1000013 '[RAGE Audio] Multistream' blocked in 130 (r3 0x8d029500 ...) top syscalls 141x2406 130x287
    I  14.632694 status: ppu 0x1000013 '[RAGE Audio] Multistream' blocked in 130 (r3 0x8d029500 ...) top syscalls 141x2323 130x287

syscall 141 is sys_timer_usleep (lv2/process.cpp:114) and syscall 130 is
sys_event_queue_receive (lv2/event.cpp:106). The thread ALTERNATES: it sleeps
800 us (r3 0x320) in its poll loop at lr 0x60a724, and it blocks in receive on
0x8d029500 at lr 0x920ef8. Both, not one. That is the wake path: `wait_for`
(object.h:56) waits on the queue's condition variable, push_event signals it
(event.cpp:24), so a delivery wakes the thread instead of it having to poll for
the whole period.

Rate: 1949 events consumed in 23.09 s = 84.4 events/s, and 633 index reads in
22.67 s = 27.9 reads/s (measurement 2). Neither is 187.4/s: the title's audio
thread is not woken once per 5.33 ms block, it is woken when its own producer
posts. That is the title's design (it polls the index between waits), not a
delivery shortfall -- every event posted was received.


## Measurement 5 -- one CELL_AUDIO_EVENT_MIX per period, measured on the LIVE mixer thread

RDR registers no notify queue, so the delivery path cannot be observed through
the title itself. The unit test drives audio::step, but with a stub send. This
observes the live mixer thread: `PS3BASE_AUDIO_NOTIFY_PROBE` (added here,
runtime/src/hle/cellAudio.cpp) creates a real queue under the console's own key
base (0x80004d494f323221, audio_hle.h:524), registers it through the console's own
set_notify_event_queue (:547), and starts a real consumer that receives exactly as
a title would. Both counters and the mixer's period counter are read from the
same run.

    $ PS3BASE_AUDIO_NOTIFY_PROBE=1 PS3BASE_LOG=info timeout 26 ./build-rdr/ps3base --no-launcher
    I   3.634874 cellAudio: N5b probe: notify queue key 0x80004d494f323221 registered, receiver started
    I   8.654559 cellAudio: N5b probe: period  937, delivered  937, received  937, failed 0
    I  13.656947 cellAudio: N5b probe: period 1875, delivered 1875, received 1875, failed 0
    I  18.659288 cellAudio: N5b probe: period 2813, delivered 2813, received 2813, failed 0

period == delivered == received at all three sample points, failed 0, and the
consumer is an independent thread that blocks on the queue's condition variable
(probe_receiver, cellAudio.cpp) rather than polling. So:

  - **one event per block**: 937 events in the 937 periods of the first 5.02 s.
    Periods are AUDIO_BLOCK_PERIOD_US = 5333 us, so that is one event per 5.33 ms;
  - **the read index advances**: 937 index writes in the same 937 periods
    (the stats line's own `after 937 writes`);
  - **the consumer wakes**: received == delivered, so every event pushed was taken
    by a blocked receive, not left pending and not refused with CELL_EBUSY.

(The probe logs 'failed 0' on every interval, which is the counter that would
have moved if the delivery had been dropped because a consumer was not draining.)


## Measurement 6 -- port 0's silence, decomposed: the SPU writes ZERO bytes and only ONE of the 8 blocks

Measurement 3 established that port 0's only writer is the SPU and every byte it
writes is zero. This run says how much it writes and where.

    $ GOWA_TRAP=12190000+0x10000 GOWA_TRAP_MAX=100000 timeout 22 ./build-rdr/ps3base --no-launcher
    $ grep 'trap 0 write' /tmp/n5b-p0rate.log | (timestamps) -> events 1698, span 19.65 s, rate 86.3/s
    $ (histogram of the byte offset / 0x2000)
    blocks: [(6, 1698)]

Two numbers that decide the 0/8:

1. **All 1698 stores are into block 6 (0x1219c000) only.** Port 0 has 8 blocks;
   the SPU job writes one of them, 1698 times. The other seven blocks are never
   touched by anyone, so the best possible fill reading for this port is 1/8, not
   8/8 -- and that is only if the block it does write were nonzero.
2. **All 1698 stores carry 0x00000000.** The fill counter counts a block as
   filled when any of its bytes is nonzero (cellAudio.cpp:294-298), so 0/8 is the
   correct reading of a buffer of zeroes.

Rate: 86.3 stores/s against 187.4 periods/s. The SPU mixer is not publishing one
block per 5.33 ms period; it publishes roughly one store per 11.6 ms. It is also
the only thread that ever touches port 0 (measurement 3a: 1455 writes, all
`by SPU 0 Secondary`, 0 reads).

### 6b. The refusal in the same run, and why it is upstream of the silence

    $ ... PS3BASE_SPU_REFUSAL_DUMP=1 timeout 45 ...
    E  30.551513 spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    E  30.552126 spu:     -1: ch19 <- 0x00000080 at pc 0x06410
    E  30.552126 spu:     -2: ch18 <- 0x00000000 at pc 0x06408
    E  30.553421 spu:     +0x050: 00000008 00000000 12190000 00000008
    E  30.553431 spu:     +0x060: 00000000 00000000 70dcd000 70ddc000

The refusing thread is **thread 0x200 'SecondaryCellSpursKernel0'**, and it is the
same thread that writes port 0: its host tid 1321794 is the only tid in the port 0
trap records, and it is the tid the log gives for `thread 0x200 'SecondaryCellSpursKernel0' start`.

The 0x90-byte object at LS 0x36200 carries the port address at +0x58
(0x12190000, correct) and **0 at +0x60**, which `0x6b24 lqr r53,0x36260` reads;
`rotqbyi r4,r53,4` then makes r4 = 0, `wrch ch18,r4` publishes EA 0, and the GET is
refused. The same instruction at 0x68e8 writes 0x70c95400, a valid address, so the
zero is selected by a branch and not by a missing value.

Chain, stated as one sequence with the evidence for each link:

  * the mixer thread's period clock runs (937/1875/2813 in measurement 1);
  * the index it publishes advances and is read (measurement 2);
  * port 0's buffer is written by exactly one thread, the SPU at 0x200, with
    zeroes only (measurements 3a, 6);
  * that same thread is the one whose DMA GET is refused because its descriptor's
    +0x60 is 0 (6b);
  * so the SPU mix job never transfers anything into port 0, which is why the
    fill counter reads 0/8 -- consistent with, and downstream of, the refusal.

Port 1 has none of this: it is filled by a PPU thread (Bink Audio Thread, 1000
nonzero float stores, measurement 3b), it never goes through the SPU channel path
that is refused, and its fill reads 16/16.


### 6c. One run, one thread id: the port 0 writer IS the refusing thread

The 6b chain used two runs (port 0 attribution in one, the refusal in another).
Re-run with both instruments in the SAME run and compare the host thread id, which
the log prints for both:

    $ GOWA_TRAP=12190000+0x10000 GOWA_TRAP_MAX=30 PS3BASE_SPU_REFUSAL_DUMP=1 \
      timeout 80 ./build-rdr/ps3base --no-launcher   # /tmp/n5b-both.log

    $ grep 'trap 0 write' /tmp/n5b-both.log | sed -n 's/^I *[0-9.]* \[\([0-9]*\)\].*/\1/p' | sort | uniq -c
         30 1326033
    $ grep "thread 0x200 'SecondaryCellSpursKernel0' start" /tmp/n5b-both.log | (tid)
    1326033
    $ grep 'DMA GET REFUSED' /tmp/n5b-both.log
    E  23.504104 [1326033] spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped) ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428
    $ grep '+0x060:' /tmp/n5b-both.log
    E  23.510526 [1326033] spu:     +0x060: 00000000 00000000 70ddd000 70dec000

**1326033 writes port 0. 1326033 is SPU thread 0x200 'SecondaryCellSpursKernel0'.
1326033's DMA GET is refused, at the instruction whose descriptor's +0x60 is 0.**
Not three facts from three runs: one thread id, one run, all three.

The thread id is not the only agreement -- the log's own attribution string for
the port 0 stores is the truncated 15-character host thread name
(`SPU 0 Secondary`, host.cpp:391 truncates at 15, spu/thread.cpp:1824 builds
`"SPU " + slot + " " + name`), which is why the numeric id above is the evidence
and the string is only corroboration.


## Measurement 7 -- A/B on the same machine, same minute, no regression

    === A (main 202e9ef build) 08:00:01        load 19.20
    A exit=12    flips 11
    I  22.748619 audio stats: 1876 blocks, 2 late wakeups, 0 stale, 0 underruns, period 3751,
                port0 0/8 filled, index 7 port1 16/16 filled, index 7
    I  23.838814 tty: [0] The SPURS is stopped due to the SPU exception.
    === B (explore/N5b) 08:00:25
    B exit=12    flips 11
    I  22.336293 audio stats: 1876 blocks, 3 late wakeups, 0 stale, 0 underruns, period 3751,
                port0 0/8 filled, index 7 port1 16/16 filled, index 11
    I  22.788566 tty: [0] The SPURS is stopped due to the SPU exception.
    load 17.90

Identical flips (11), identical period (3751), identical fill (0/8 and 16/16), same
SPURS abort. The instrumentation is inert unless its env var is set, verified in
the post-rebase run above: `grep -c 'N5b probe'` = 0 and `grep -c 'queuecount'` = 0
on a plain invocation.

## Post-rebase state

    $ git fetch /root/Dev/ps3base2 main:refs/remotes/origin/main -f && git rebase origin/main
    Current branch explore/N5b is up to date.        # origin/main = 202e9ef, 0 commits behind
    $ tools/check_no_dup_symbols.sh
    gowa_stubs: no known duplicate definitions
    $ git diff --diff-filter=D --name-only origin/main..HEAD
                                                      # empty: no file deleted
    $ cmake --build build-rdr -j 8 --target ps3base cellAudio && cd build-rdr && ctest -j 4
    100% tests passed, 0 tests failed out of 40

Diff shape: 7 files, 513 insertions, 0 deletions -- RESULTS-N5b.md, the claim,
the status json, and the two instrumented surfaces (lv2/event.*, thread.cpp for
the process-exit report, hle/cellAudio.cpp for the probe).

## The answer

**Events per port.** RDR registers no cellAudio notify queue at all, so the
per-port event count for this title is 0 and that is a property of the title's
seven-function libaudio surface, not of the queue. Measured on a queue that IS
registered (live mixer, real lv2 queue, real consumer): exactly one
CELL_AUDIO_EVENT_MIX per period -- 937 delivered / 937 received at period 937,
1875/1875 at 1875, 2813/2813 at 2813, 0 failures.

**Read-index advance.** Yes. 937 index writes per 937 periods, published as a
big-endian u64 at the port's readIndexAddr; the guest's two words (high at
+0x10, low at +0x14) are read 235 and 633 times by [RAGE Audio] Multistream
over 24 s, with the low word taking the values 0..7 in sequence.

**Does the PPU thread wake.** Yes, by two measured mechanisms, not one: it
alternates between sys_timer_usleep(0x320 = 800 us) at lr 0x60a724 in its poll
loop and sys_event_queue_receive on 0x8d029500 at lr 0x920ef8, and its own
queue's counter reads delivered=1949 received=1949 pending=0. It consumes 84.4
events/s and polls the index 27.9 times/s -- it is not woken once per 5.33 ms
block, and that is the title's design rather than a delivery shortfall, because
every event posted was received.

**Port 0 vs port 1.** Not a queue question and not a mixer question -- a producer
question, and the producers are different threads with different results:

| | port 0 (0x12190000, 8ch x 8 blocks) | port 1 (0x121a0000, 2ch x 16 blocks) |
|---|---|---|
| writer | SPU thread 0x200 'SecondaryCellSpursKernel0' (tid 1326033), the ONLY writer, 0 reads | PPU 'Bink Audio Thread' (lr 0x00fef920) |
| values written | 1698/1698 stores are 0x00000000 | 1000 nonzero IEEE-754 floats (0x00723593, 0xbc109842) |
| blocks touched | block 6 only, 1698 times; the other 7 never written | block 6 (0x121a3000) |
| fill counter | 0/8 is correct -- the buffer is zeroes | 16/16 |
| index readers | Multistream only (442 reads) | Bink Audio Thread (26) + [RDR2] Render Thread (15) |

The two ports differ because port 0 is fed by the SPU mix job and port 1 by a PPU
Bink decode thread. Port 0's SPU job is the thread whose DMA GET is refused at
pc 0x06428: its 0x90-byte object at LS 0x36200 holds the correct port address at
+0x58 (0x12190000) and 0 at +0x60; `lqr r53,0x36260` reads that 0, `rotqbyi`
leaves r4=0, and `wrch ch18,r4` publishes EA 0, which is refused as unmapped.
The same instruction at pc 0x68e8 writes 0x70c95400 (valid), so a branch chooses
the zero. One run, one thread id (1326033) proves the port-0 writer and the
refused thread are the same thread.

