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

