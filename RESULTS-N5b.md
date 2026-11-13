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
