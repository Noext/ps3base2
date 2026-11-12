# M2 -- cellAudio against RPCS3 (in progress)

Worktree /root/Dev/wt-M2, branch explore/m2, based on main 4a5a132.

Measurements are appended below in the order they are taken. Every comparison is
an A/B on this machine within the same minute.

## Measurement 1 -- the baseline boot's cellAudio calls, verbatim

Binary: build-61, from main 4a5a132, shared generated dirs out-rdr.
Command (display :961, own Xvfb):

    cd /root/Dev/wt-M2 && ulimit -s unlimited && \
      DISPLAY=:961 PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      GOWA_IMPORT_ARGS=cellAudioInit,cellAudioPortOpen,cellAudioGetPortConfig,cellAudioPortStart,cellAudioPortStop,cellAudioPortClose,cellAudioQuit \
      timeout 60 build-61/ps3base --no-launcher > /tmp/M2-base2.log 2>&1

    $ grep -n "cellAudio" /tmp/M2-base2.log
    318:I   2.367503 [3766816] sysutil: cellAudioOutGetSoundAvailability(0, type 0, fs 0x4) -> 2
    319:I   2.367563 [3766816] sysutil: cellAudioOutGetSoundAvailability(0, type 1, fs 0x4) -> 0
    321:I   2.367599 [3766816] sysutil: cellAudioOutConfigure(0): channel 2 encoder 0 downMixer 0
    322:I   2.367608 [3766816] sysutil: cellAudioOutGetState(0, 0): state 0 encoder 0 downMixer 0 ch 2
    323:I   2.372676 [3766816] import: cellAudio::cellAudioInit(0x1, 0x13b0000, 0x2, 0x1) lr 0x609c50
    324:I   2.372807 [3766816] cellAudio: cellAudioInit
    325:I   2.372821 [3766816] import: cellAudio::cellAudioInit returned 0x0
    326:I   2.372830 [3766816] import: cellAudio::cellAudioPortOpen(0xd004ecf0, 0x1722700, 0x8, 0x0) lr 0x609c7c
    327:I   2.372861 [3766816] cellAudio: port 0 open: 8 ch, 8 blocks at 0x12190000
    328:I   2.372867 [3766816] import: cellAudio::cellAudioPortOpen returned 0x0
    329:I   2.372874 [3766816] import: cellAudio::cellAudioGetPortConfig(0x0, 0xd004ed10, 0x8, 0x0) lr 0x609c94
    330:I   2.372882 [3766816] import: cellAudio::cellAudioGetPortConfig returned 0x0
    343:I   2.384098 [3766958] import: cellAudio::cellAudioPortStart(0x0, 0x1720000, 0x1, 0x0) lr 0x60a70c
    344:I   2.384104 [3766958] import: cellAudio::cellAudioPortStart returned 0x0
    681:I  15.586108 [3766929] import: cellAudio::cellAudioPortOpen(0xd02affa0, 0x13dee70, 0x0, 0x0) lr 0xff0644
    682:I  15.586788 [3766929] cellAudio: port 1 open: 2 ch, 16 blocks at 0x121a0000
    683:I  15.586795 [3766929] import: cellAudio::cellAudioPortOpen returned 0x0
    684:I  15.586803 [3766929] import: cellAudio::cellAudioGetPortConfig(0x1, 0xd02affc0, 0x0, 0x0) lr 0xff066c
    685:I  15.586818 [3766929] import: cellAudio::cellAudioGetPortConfig returned 0x0
    689:I  15.626766 [3766929] import: cellAudio::cellAudioPortStart(0x1, 0x0, 0x180, 0x422) lr 0xff0c7c
    690:I  15.626810 [3766929] import: cellAudio::cellAudioPortStart returned 0x0

Reading of the sequence, from the lifted call sites (out-rdr/ppu/rdr_061.cpp:5514-5522
and :4885 area): the title passes nChannel=8, nBlock=8, attr=0, level=0 at
2.372830 (the three st64 of 8/8/0 at r1+112/120/128 are exactly
CellAudioPortParam{nChannel=8,nBlock=8,attr=0,level=0}); the second open at
15.586108 is 2 channels x 16 blocks. The title reads the struct at
0xd004ed10 / 0xd02affc0 (32 bytes) and starts both ports.

What the baseline does NOT log: the contents of those two output structures,
the readIndexAddr and portAddr it returned, the index's progress, and any of
PortStop/PortClose/Quit (none is called before the 23 s abort). That is what
the rest of this file measures.


## Measurement 2 -- the port buffer is real, mapped, and the read index advances

Two independent measurements, because the claim has two halves.

### 2a. Unit test: the exact structure, the exact size, the advancing index

    $ cd /root/Dev/wt-M2 && ulimit -s unlimited && tools/build_slot.sh cmake --build build-61 -j 8 --target cellAudio
    [2/2] Linking CXX executable cellAudio
    $ ./build-61/cellAudio
    PASS: 214 checks, 0 failures

The suite drives runtime/src/hle/audio_hle.h with real guest memory. What it
pins down, verbatim from the run (tests/cellAudio.cpp):

  - CellAudioPortConfig is 32 bytes: readIndexAddr 0, status 4, nChannel 8,
    nBlock 16, portSize 24, portAddr 28. status at 4 and not 8 is the vm
    pointer width (refs/rpcs3/rpcs3/Emu/Memory/vm_ptr.h:20,29) -- a 64-bit
    readIndexAddr would shift every later field by four bytes and the title
    would read a channel count out of portSize.
  - portSize is EXACTLY max(1,ch)*blocks*256*4: 0x10000 for the RDR port 0
    (8ch x 8 blocks) and 0x8000 for its port 1 (2ch x 16 blocks).
  - portAddr is a mapped buffer, and every byte of portSize inside it is
    writable: the test writes 0x5A over the whole range and reads it back.
  - the read index is a big-endian u64 at readIndexAddr that goes 1,2,3,4,5,6,7,
    0,1,2 over ten mixer periods on an 8-block port, and does not move while
    the port is stopped or closed.
  - PortStart zeroes the whole buffer (RPCS3 cellAudio.cpp:1850).
  - one CELL_AUDIO_EVENT_MIX event per period per registered queue, with
    data1 = 0, data2 = 0, and data3 = the queue's own source word only under
    CELL_AUDIO_EVENTFLAG_BEFOREMIX; DECIMATE_2 halves the rate; NOMIX stops it.
    The source word is (pid<<32)+0x0e000000+key_count*0x100 (cellAudio.cpp:2242).
  - the error-code ORDER: NOT_INIT before PARAM before PORT_NOT_OPEN, and
    PORT_ALREADY_RUN / PORT_NOT_RUN / PORT_FULL at the right points.

### 2b. Live: the guest reads the index the runtime published, N times

The runtime publishes the index; the guest must READ it, or nothing is driven.
Trap the index slot in the running title and count the guest's own loads:

    $ GOWA_TRAP=r12180010+8 GOWA_TRAP_MAX=100000 DISPLAY=:961 \
      PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd PS3BASE_LOG=info \
      timeout 40 build-61/ps3base --no-launcher > /tmp/M2-trap2.log 2>&1
    $ grep -c "trap 0 read" /tmp/M2-trap2.log
    598
    $ grep "trap 0 read" /tmp/M2-trap2.log | head -8
    I   4.430124 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000000 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.456480 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000000 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.510647 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000001 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.548113 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000001 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.599749 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000002 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.652267 trap: trap 0 read 0x12180010 prev 0x00000000 -> 0x00000000 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    I   4.677980 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000005 (guest lr 0x0060a724) by [RAGE Audio] Multistream
    ...
    I  39.946987 trap: trap 0 read 0x12180014 prev 0x00000000 -> 0x00000004 (guest lr 0x0060a724) by [RAGE Audio] Multistream

598 guest loads of the index word, by the thread named `[RAGE Audio]
Multistream`, from the loop whose call site is out-rdr/ppu/rdr_061.cpp:6317
(`ctx.lr = 0x60a724; f_00920984`). The values are the ones this runtime wrote.
The runtime's own writes on the same page are the mixer:

    $ grep -c "trap 0 write" /tmp/M2-trap2.log
    1543

and the stack of one names the publisher, not a guess:

    trap 0 write 0x12180010 prev 0x00000000 -> 0x00000000 by cellAudio
      <- rsv::store_range <- hle::audio::step<...>(hle::audio::Manager&, ...)

So the loop is closed: this runtime's period advances the index and the
guest's audio thread reads it 598 times in 40 s.

### 2c. Live: the guest FILLS a port buffer, and the mixer measures it

Index movement alone does not prove the port is wired to audio content. Both
RDR ports are live and the per-port fill is now part of the periodic stats
(block counts non-zero bytes in the buffer at the port's own address):

    $ grep "audio stats" /tmp/M2-AB-B.log
    I   8.183604 audio stats: 937 blocks, 1 late wakeups, 0 stale blocks, 0 device underruns, period 937, port0 index 1 after 937 writes: port0 0/8 filled, index 1
    I  13.185980 audio stats: 938 blocks, 2 late wakeups, 0 stale blocks, 0 device underruns, period 1875, port0 index 3 after 1875 writes: port0 0/8 filled, index 3
    I  18.188324 audio stats: 1420 blocks, 1 late wakeups, 0 stale blocks, 0 device underruns, period 2813, port0 index 5 after 2813 writes: port0 0/8 filled, index 5 port1 16/16 filled, index 2
    I  23.190669 audio stats: 1843 blocks, 1 late wakeups, 0 stale blocks, 0 device underruns, period 3751, port0 index 7 after 3751 writes: port0 0/8 filled, index 7

Port 1 (2ch x 16 blocks, opened at 15.47 s by the Bink audio thread) is
16/16 filled: the guest writes all 32 KiB of it and the mixer's index is at 2.
Port 0 (8ch x 8 blocks) is 0/8 filled for the whole run. That is a MEASUREMENT
and it is the next thing to explain, not a claim about correctness: the SPU
does write into it -- a trap on the buffer at 0x12190000 logs `write
0x12190000 by SPU 0 Secondary` -- but every byte it writes is zero, which is
why GOWA_TRAP_NONZERO=1 over 0x12190000+0x10000 reports 0 writes. The
multistream SPU mixer is producing silence in the first 23 s of the boot, so
port 0 stays silent while port 1 plays.

### 2d. No regression: A/B on the same machine, same minute

    $ uptime; # load 26.77
    === A (origin/main binary build-rdr) 22:24:00
    I  22.005452 rsx: flip 601 (buffer 0) worst 101.9 ms, >50ms 1, >100ms 1
    I  23.095316 tty: [0] The SPURS is stopped due to the SPU exception.
    I  23.104774 process: _sys_process_exit(0x8001000c)
    === B (this branch, build-61) 22:24:23
    I  22.653885 rsx: flip 601 (buffer 0) worst 54.9 ms, >50ms 2, >100ms 0
    I  23.095700 tty: [0] The SPURS is stopped due to the SPU exception.
    I  23.109778 process: _sys_process_exit(0x8001000c)
    uptime; # load 24.49

Identical flips (601), identical abort instant (23.09 s), both process exits
0x8001000c. The audio rework does not move the SPURS abort, which is expected:
the abort is the SPU exception chain established at 4a5a132, not audio.

### 2e. The call log the task asked for, now complete

    I   3.161055 sysutil: cellAudioOutGetSoundAvailability(0, type 0, fs 0x4) -> 2
    I   3.161101 sysutil: cellAudioOutGetSoundAvailability(0, type 1, fs 0x4) -> 0
    I   3.161153 sysutil: cellAudioOutConfigure(0): channel 2 encoder 0 downMixer 0
    I   3.161161 sysutil: cellAudioOutGetState(0, 0): state 0 encoder 0 downMixer 0 ch 2
    I   3.166788 cellAudio: cellAudioInit: index area 0x12180000 (0x10000), 4 ports, sdk 0x300001
    I   3.166859 cellAudio: cellAudioPortOpen: port 0, 8 ch, 8 blocks, attr 0x0, level 1.000 -> portSize 0x10000 at 0x12190000, readIndexAddr 0x12180010 (status 0x1)
    I   3.166867 cellAudio: cellAudioGetPortConfig(0, 0xd004ed10): readIndexAddr 0x12180010 status 0x1 nChannel 8 nBlock 8 portSize 0x10000 portAddr 0x12190000
    I   3.183269 cellAudio: cellAudioPortStart(0) -> 0x0: buffer 0x12190000 zeroed (65536 bytes), readIndexAddr 0x12180010
    I  15.471109 sysutil: cellAudioOutGetConfiguration(0): channel 2 encoder 0 downMixer 0
    I  15.474323 cellAudio: cellAudioPortOpen: port 1, 2 ch, 16 blocks, attr 0x1000, level 1.000 -> portSize 0x8000 at 0x121a0000, readIndexAddr 0x12180020 (status 0x1)
    I  15.474335 cellAudio: cellAudioGetPortConfig(1, 0xd02affc0): readIndexAddr 0x12180020 status 0x1 nChannel 2 nBlock 16 portSize 0x8000 portAddr 0x121a0000
    I  15.614579 cellAudio: cellAudioPortStart(1) -> 0x0: buffer 0x121a0000 zeroed (32768 bytes), readIndexAddr 0x12180020
    I  23.011543 cellAudio: cellAudioPortStop(1) -> 0x0
    I  23.011734 cellAudio: cellAudioPortClose(1) -> 0x0

Port 1's arguments are `attr 0x1000` = CELL_AUDIO_PORTATTR_INITLEVEL, and
`level 1.000` is the parameter's own level carried through (RPCS3
cellAudio.cpp:1756-1763). Under the OLD handler `attr` and `level` were never
read at all: the old code read nChannel and nBlock from the parameter and
ignored the other sixteen bytes.

cellAudioSetNotifyEventQueue, CreateNotifyEventQueue, GetPortTimestamp,
GetPortBlockTag and the Add*Data family are NOT called by this title: their
NIDs are absent from out-rdr/ppu/table.cpp:77122-77128 (the seven cellAudio
imports RDR has). They are implemented anyway -- the same bodies RPCS3 has --
because gta-sa imports cellAudioAddData (out-gta/ppu/table.cpp) and because a
registered name that resolves nowhere is a silent CELL_OK.


## Measurement 3 -- a second title: gta-sa, two ports, both filled

RDR alone cannot show that the module generalises, and gta-sa is the one title
here that imports cellAudioAddData. Same worktree, build-gta against
out-gta/ppu:

    $ PS3BASE_DISC_DIR=/root/Dev/gta-port/disc DISPLAY=:961 PS3BASE_LOG=info \
      timeout 150 build-gta/ps3base --no-launcher > /tmp/M2-gta2.log 2>&1
    $ grep -c 'rsx: flip' /tmp/M2-gta2.log
    26        # the log's 26 flip lines report up to flip 1501 at 34.5 s

    I   0.333458 cellAudio: cellAudioInit: index area 0x100a0000 (0x10000), 8 ports, sdk 0x475001
    I   0.811255 cellAudio: cellAudioPortOpen: port 0, 2 ch, 16 blocks, attr 0x1000, level 1.000 -> portSize 0x8000 at 0x100b0000, readIndexAddr 0x100a0010 (status 0x1)
    I   0.811277 cellAudio: cellAudioGetPortConfig(0, 0xd047c360): readIndexAddr 0x100a0010 status 0x1 nChannel 2 nBlock 16 portSize 0x8000 portAddr 0x100b0000
    I   0.816971 cellAudio: cellAudioPortStart(0) -> 0x0: buffer 0x100b0000 zeroed (32768 bytes), readIndexAddr 0x100a0010
    I   1.490817 cellAudio: cellAudioPortOpen: port 1, 2 ch, 16 blocks, attr 0x1000, level 1.000 -> portSize 0x8000 at 0x100c0000, readIndexAddr 0x100a0020 (status 0x1)
    I   1.491204 cellAudio: cellAudioGetPortConfig(1, 0xd05dce80): readIndexAddr 0x100a0020 status 0x2 nChannel 2 nBlock 16 portSize 0x8000 portAddr 0x100c0000
    I   1.491198 cellAudio: cellAudioPortStart(1) -> 0x0: buffer 0x100c0000 zeroed (32768 bytes), readIndexAddr 0x100a0020
    I   5.341895 cellAudio: audio stats: 1570 blocks, 0 late wakeups, 322 stale blocks, 0 device underruns, period 937, port0 index 0 after 848 writes: port0 16/16 filled, index 0 port1 0/16 filled, index 2
    I  10.344240 cellAudio: audio stats: 1876 blocks, 0 late wakeups, 433 stale blocks, 0 device underruns, period 1875, port0 index 10 after 1786 writes: port0 16/16 filled, index 10 port1 0/16 filled, index 12
    I  15.346595 cellAudio: audio stats: 1876 blocks, 1 late wakeups, 765 stale blocks, 0 device underruns, period 2813, port0 index 4 after 2724 writes: port0 16/16 filled, index 4 port1 0/16 filled, index 6
    I  16.190208 cellAudio: cellAudioPortClose(0) -> 0x0
    I  20.348921 cellAudio: audio stats: 1097 blocks, 0 late wakeups, 159 stale blocks, 0 device underruns, period 3751, port0 index 0 after 0 writes: port1 16/16 filled, index 0

Three things that only a second title shows:

  * SDK 0x475001 gives EIGHT ports (cellAudio.cpp:1590) where RDR's 0x300001
    gives four. Both are read from the process parameter, not assumed.
  * the two ports get server_index 0 and 1, so readIndexAddr is 0x100a0010
    and 0x100a0020 -- consecutive slots of the shared area, as the console
    maps them (:1604, :1609 -- free_indices[7]=8, [6]=7, ... for eight ports).
  * the guest FILLS both: port 0 is 16/16 blocks non-zero, and after it is
    closed (16.190208) port 1 is 16/16. That is the difference between 'the
    index advances' and 'the title is actually producing audio through this
    module'. RDR's port 1 shows the same (16/16); RDR's port 0 stays 0/8
    because the SPU multistream mixer writes zeros into it in the first 23 s,
    measured in 2c and left as the open question rather than smoothed over.
  * cellAudioPortClose(0) at 16.19 s answers 0 and the port's index writes
    drop to 0 in the next stats line, which is the mixer honouring the close.

## What is not claimed

RDR's port 0 buffer is 0/8 filled at every sample, and the SPU writes only
zeros into it (GOWA_TRAP=12190000+0x10000 with GOWA_TRAP_NONZERO=1 reports 0
writes). That is the SPU multistream mixer producing silence this early in the
boot; it is not a defect this module can fix from the HLE side and it is not
being reported as green. Everything the task asked for -- the calls logged,
the structures exact, the buffer mapped, the read index advancing and read by
the guest -- is measured above.

## Post-rebase state

    $ git fetch /root/Dev/ps3base2 main:refs/remotes/origin/main -f && git rebase origin/main
    Current branch explore/m2 is up to date.     # origin/main = 4a5a132
    $ cd build-61 && ctest -j 4
    100% tests passed, 0 tests failed out of 40
    $ tools/check_no_dup_symbols.sh
    gowa_stubs: no known duplicate definitions

40 tests rather than the 39 at main HEAD: the 40th is `cellAudio`.
