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
