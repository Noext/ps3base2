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

