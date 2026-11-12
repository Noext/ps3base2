# M5 -- closed-loop driving: read the screen, press START, capture what follows

Branch explore/m5 from main HEAD 4a5a132.

Task: RDR reaches its title screen ("RED DEAD REDEMPTION" + "Press START") at ~22 s
and the SPURS abort kills it at ~23 s. Drive it from the screen: read the window,
press START through xdotool, capture every following screen before the abort.
Deliverable: the capture of the screen that follows Press START.

## Status: started

Proof follows, measurement by measurement, appended with the raw output.

## Environment

- worktree /root/Dev/wt-M5, branch explore/m5, from main 4a5a132
- display :964 (Xvfb 1280x720x24, own display so no focus stealing)
- build-64 from the shared generated dirs (out-rdr), ccache wired
- inputs symlinked: bin fw keys ref vfs

## Measurement 1 -- the title screen is on the root, not in the window pixmap; it lands 0.3 s before the exit

Build: build-64 from main 4a5a132, shared generated dirs out-rdr, ccache, -j 8.
Display :964 (Xvfb 1280x720x24). Binary identical in behaviour to build-rdr (601 flips,
SPURS abort ~23 s).

Capture methods compared on the same run (window id from `xdotool search --name "Red Dead"`,
geometry 1208x680+36+20):

    xwd -id <win>   -> the title screen is NOT in it: after ~21.9 s every frame reads
                       nb=1089 non-black pixels, mean 0.04, i.e. the window pixmap is black
    xwd -root       -> the title screen IS in it: at t=23.188 a frame containing
                       "RED DEAD REDEMPTION" + "Press START", darkred=18377 px

The Vulkan present path (swapchain 1208x680, present mode 2) leaves the X window pixmap
black; the composited root is what a screenshot must read. Every earlier window-only
capture (mine, and the 0.4 s sampler that produced docs/evidence) is this same path --
the evidence frame is a root grab.

Raw timeline (root grab, 1208x680 game rect, ~18 fps), columns t, non-black>8, dark-red
(title text) px, mean luma, max luma:

    1.317 39548 1329 1.58 255   <- Rockstar logo
   15.830 8623  55   1.12 140
   16.159 279953 12019 7.16 140  <- intro scene
   16.848 259410 27268 16.38 255
   17.907 259981 27264 16.41 255  <- scene stabilises
   21.123 206148 58 5.11 140
   21.454 187476 65 4.16 140
   21.828 140188 63 2.89 140
   22.119  59259 53 1.41 140   <- fade to black
   22.436    937 59 0.46 140
   22.947    814 55 0.02 140
   23.188  19454 18377 0.51 140  <- TITLE: dark-red logo + Press START
   23.239      0 0 0.00 0      <- window gone

The title is presented as a fade-in and the process exits during it: in this run the
title is visible for 0.05 s of capture (one sample) before the SPURS abort at 23.56 s;
the committed evidence frame (nb=80289, darkred=83401, mean 1.45) is the same screen at
a later point of its fade from a slower run. The exit is the SPURS abort
(`spu: thread 0x200 'SecondaryCellSpursKernel0' DMA GET REFUSED cmd 0x40 (unmapped)
ea 0x00000000 size 0x80 lsa 0x1a100 pc 0x06428`), at 23.0-23.6 s, unchanged by any input.

## Measurement 2 -- RDR does poll the pad, and the pad bytes do carry START

`PS3BASE_CENSUS=1 PS3BASE_CENSUS_CALLS=1` on the same binary:

    I   1.643073 census: imp t=1.23 main_thread sys_io::cellPadInit -> 0x0 lr=0x005931a8
    I   1.643087 census: imp t=1.23 main_thread sys_io::cellPadSetPortSetting -> 0x0 lr=0x005931d0  (x9)
    I  27.427598 census: imp t=27.01 main_thread sys_io::cellPadGetData -> 0x0 lr=0x00592ccc
    I  27.468938 census: imp t=27.05 ...
    ... 40+ calls, one per frame, after the first screen

A B-run with START genuinely held (`xdotool keydown Return` kept down) shows the bytes
the title receives, through PS3BASE_PAD_LOG:

    I  16.380701 [3877500] pad: frame 0 bytes 00 7c 08 00 80 80 80 80 t=14.87
    I  22.862558 [3877500] pad: frame 208 bytes 00 7c 08 00 80 80 80 80 t=21.35

b[2] = 0x08 is START (PS3 pad layout, runtime/src/host_input.cpp:427) and it is present
in the bytes cellPadGetData hands the title. So the input path works end to end; the
presses in that run simply ended at game t=21.35 s, ~0.8 s before the title is drawn.


## Measurement 3 -- the deliverable does not exist: the title screen IS the last frame

Two arms, same binary, same display (:964), same minute, back to back, each recorded at
60 fps from before launch to after the process died. INPUT IS INJECTED THROUGH THE
RUNTIME'S OWN PAD QUEUE (`PS3BASE_INPUT_CMD`, applied in fill() AFTER poll_pad, so X
latency and window focus cannot be blamed):

    arm P: input.cmd = "start 100000"  -> hold = atof(100000)*60/1000 frames, i.e. START
           down for 100 s, so b[2]=0x08 is in EVERY cellPadGetData the title makes
    arm N: input.cmd empty            -> same runtime, no input

The pad bytes the title sees, from PS3BASE_PAD_LOG (arm P, W-P run, verbatim):

    I  16.103531 [125662] pad: frame 1 bytes 00 7c 08 00 80 80 80 80 t=14.69
    I  22.908559 [125662] pad: frame 330 bytes 00 7c 08 00 80 80 80 80 t=21.49
    I  22.979285 [125662] pad: frame 331 bytes 00 7c 08 00 80 80 80 80 t=21.56

331 frames with b[2]=0x08. The last one the title ever takes is at game t=21.56 s.

The frames (root grab, 1280x720, 60 fps, video index / 60 = video seconds; the recorder
starts 0.30 s before the process so game t = video - 0.30):

    arm P:  i=1391 t=23.18  2130 non-black, 72817 dark-red px, mean 0.69   TITLE (fade in)
            i=1392..1399 t=23.20-23.32  the same frame, 8 samples
            i=1400 t=23.33  60 non-black, 0 dark-red, mean 0.02           WINDOW GONE
            SPURS abort at log t=22.999838, DMA GET REFUSED ea 0x0

    arm N:  i=1370 t=22.83  2130 non-black, 72817 dark-red px, mean 0.69   TITLE (fade in)
            i=1370..1376 t=22.83-22.93  the same frame, 7 samples
            i=1377 t=22.95  60 non-black, 0 dark-red, mean 0.02           WINDOW GONE
            SPURS abort at log t=22.611980

Same screen, same duration, with START held throughout and with no input at all. There
is no "screen that follows Press START": the guest is killed during the title's own fade
in, 0.15 s (arm P) and 0.18 s (arm N) after the first frame of that fade.

Why the press cannot win, stated as a number rather than a guess: the title's
cellPadGetData loop runs until game t=21.56 s; the title is not drawn until game t≈22.85 s.
For the whole 0.15 s the title is on screen, the guest makes **zero** pad reads, so no
press -- held, tapped, queued, or through X -- can reach the title's wait-for-START
routine inside the window it exists. Everything the press could do it had already done
1.3 s earlier, on the intro.

Corroboration from the census run (PS3BASE_CENSUS_CALLS=1, a slower run kept alive to
35.6 s): cellPadGetData stops at `t=33.01`, the SPURS abort is at 35.57 s, and the
title is drawn at video frame 2152/60 = 35.9 s -- again after the last pad read.

Evidence committed (the frames above, unchanged):

    docs/evidence/rdr-title-start-held.png   arm P title frame, START held in every poll
    docs/evidence/rdr-title-control.png      arm N title frame, no input
    docs/evidence/rdr-after-title-exit.png   what follows in BOTH arms: black, window gone

Negative controls run and rejected as explanations (each a same-minute A/B, each
recorded at 60 fps, each showing the same 0.15 s budget):

    xdotool keydown/Return held from 14-20 s     title still appears and the process dies
    xdotool taps at 25 Hz from the intro         same
    START held from t=1 s before the first poll  same
    queue fed continuously to process death      same (measured: title spans 23.18..23.32)

## Status: done

The closed loop was built and run. It cannot yield a post-START screen, because the
SPURS abort that this tree still has kills the guest during the title screen's fade-in,
and the guest stops polling the pad 1.3 s before that screen is drawn. The deliverable
capture therefore does not exist, and no amount of input timing creates it: the blocker
is the DMA GET REFUSED at ea 0 (SPU wrch ch18 with the value that the skipped store at
0x920054 should have supplied), not the input path. The input path is proven good --
b[2]=0x08 is in the pad bytes at every one of 331 consecutive polls.

Scripts (in the worktree, reproducible verbatim):

    /tmp/m5/W.sh        the two-arm run above (8 lines each, PID-waited, not pgrep)
    /tmp/m5/W-P/f/      every recorded frame of arm P
    /tmp/m5/W-N/f/      every recorded frame of arm N


## Measurement 4 -- post-rebase confirmation on 123c925

Rebased onto origin/main 123c925 (M1/M2/M3 landed cellAudio changes) and rebuilt
build-64 from the shared generated dirs. Same two-arm procedure, same display, same
minute, fresh binary:

    $ /tmp/m5/POST.sh
    == P fill=yes frames=1448 padframes=588 abort=23.142482
    == N fill=no  frames=1442 padframes=0   abort=23.049409

    arm P: title frames video i=1405..1408, t=23.42..23.47, non-black 2130, dark-red
           72817 -- four consecutive samples, START in every pad poll (588 of them)
           then i=1409 t=23.48 non-black 60, dark-red 0, window gone
    arm N: no dark-red frame in this run; the intro fade runs down to non-black 1186 at
           t=23.08 and the window is gone at t=23.40

Raw post-rebase escape (the pad bytes, verbatim):

    $ grep -ac 'pad: frame' /tmp/m5/POST-P/game.log
    588
    $ grep -a 'aborted' /tmp/m5/POST-P/game.log | tail -1
    I  23.142482 ... tty: [0] The SPURS is aborted.

The conclusion is unchanged on the rebased tree: with START held for 588 consecutive
cellPadGetData calls the title is on screen for 0.067 s and the process exits into the
SPURS abort; with no input it is 0 to 0.117 s in the runs measured (W-N: 7 frames,
F-N: 8 frames, POST-N: 0). The spread is the abort landing inside the title's fade-in,
not an effect of the press.

Honest statement of the variation: the control arm does not show the dark-red title in
every run. Across the control runs measured today it appears in W-N (7 frames), F-N
(8 frames), Q-C1 (4), Q-C2 (9), T-C (0), POST-N (0); across the pressed runs in W-P (9),
F-H (0 -- the title's place in the recording is a bright flash), Q-P2 (11), T-T (10),
POST-P (4). Title lifetime overlaps the abort in every case and never survives it.

