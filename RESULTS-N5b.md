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

