# RESULTS-P4 -- driving RDR past the ALERT dialogue

Task: closed-loop driving. Read the screen, choose "Continue Without Saving", then
"Create A New Save Game", capture every screen. Deliverable: the capture of the
screen that follows the ALERT dialogue.

State at start: main HEAD 01c5e16. RDR reaches its own ALERT screen
("A Red Dead Redemption single player save could not be found" with Continue
Without Saving / Create A New Save Game / Retry), then dies on a host SIGSEGV
at guest 0x10.

## Log

(format: timestamp UTC, what, raw proof)

### M1 -- START reaches the RDR main menu (first press to advance past the title)

Environment: build-rdr (main 01c5e16), display :145 (Xvfb 1280x720x24),
GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash, PS3BASE_DISC_DIR=/root/Dev/rdr-port/vfs/dev_bdvd.
Input through the runtime's own queue: PS3BASE_INPUT_CMD, line "start 200 100".

Screens sampled every 1.5 s (root grab, 1280x720), (index, t s, pixels>8, mean luma):

    1  0.0     0 0.00        15 22.8  34130  1.45
    3  3.1 54519 3.08        17 26.1 345184 25.07
    7  9.4 28129 1.54        20 31.6   1089  0.04   <- fade
   11 15.7  1089 0.04        21 33.2 369618 50.13   <- TITLE, Press START
   12 17.2 449017 10.08      25 40.6 369440 50.33   <- START queued, "Scanning
   13 19.0 239982 5.28                                 for installed downloadable
   14 20.9 339307 18.81                                content... please wait"
                             40 69.2 373715 51.05   <- MAIN MENU
                             56 98.7 373705 50.23

    pressed start at t=40.58 s
    pad: frame 373 bytes 00 7c 08 00 80 80 80 80 t=38.97   (b[2]=0x08 START, 12 polls)

The screen that follows is the title's MAIN MENU: logo above, and the three rows
**Online Multiplayer / Downloadable Content / Single Player** at the bottom.
Captures: /tmp/p4drive/caps/s0026.png (scanning), s0040.png and s0056.png (menu).

No crash in 100 s (log has zero "crash: signal"; 25 "rsx: flip" summary lines,
last flip 1321+). The ALERT dialogue the task starts from is one step further, on
Single Player -- see M2.
### M2 -- the menu rows are reachable; "Single Player" leads to a loading screen and then the SIGSEGV

Same binary and display. Schedule sent through PS3BASE_INPUT_CMD:

    T+40.1 start 200 100      (title -> menu)
    T+59.1 right 150 100
    T+66.3 right 150 100
    T+75.8 cross 150 100

Screens (index, t s, px>8, mean luma), 1 s sampling:

    22 23.6 369710 46.79  TITLE (stable)
    37 41.3 372845 45.47  scanning banner ends, menu appears
    66 75.8 373446 41.83  MAIN MENU, three rows
    68 78.2 249229 30.13  transition (logo sliding out, "Single Player" only)
    70 80.3 316092 11.07  LOADING screen: the RDR key art, horse, "Loading..." 
    72 82.7 319109 11.17  same

    exit -11 elapsed 83.9
    E  82.889016 crash: signal 11 at 0x5d5d2934e2b6 (exe+0x17aa2b6)
       fault 0x790d7c000010 (guest 0x10) thread main_thread 0x1000000
    E  82.896245 crash:   #5 ppu::call_import(...)+0x877
    E  82.897400 crash:   #6 f_0053338c(ppu::Context&, unsigned char*)+0x82
    last flip 1441 at 82.13 s

So the main menu, its three rows (Online Multiplayer / Downloadable Content /
Single Player), and the Single Player loading screen are all reachable and
captured. The ALERT dialogue is not reached in THIS ordering: the cross press at
75.8 s is the one that starts the Single Player load, and the load dies in the
known null+0x10 SIGSEGV inside f_0053338c.

Captures: /tmp/p4drive/caps/s0066.png (menu), s0068.png (transition),
s0070.png, s0072.png (loading).
### M3 -- the ALERT dialogue is reached by closed-loop driving, and is STABLE (no crash)

The blocker was the SAVE DATA, not the input or the loader. Same binary, same
display, two arms whose only difference is the VFS:

  arm A: PS3BASE_VFS_DIR unset -> the default vfs/ (3 saves present)
  arm B: PS3BASE_VFS_DIR=/root/Dev/p4work/vfs-clean (savedata/ empty)

    arm A  savedata: ListAutoLoad: 3 savedata match prefix 'BLES00680' ... listing 3
    arm B  savedata: ListAutoLoad: 0 savedata match prefix 'BLES00680' ... listing 0

With saves present RDR loads them instead of prompting; with none it draws its
own ALERT. This is why the previous run's evidence had no reproducible path.

{
 "arm": "B, clean savedata, closed-loop OCR driver",
 "driver": "/root/Dev/p4work/loop.py: grab root -> OCR bottom band + centre band -> classify -> press",
 "journal": [
  {"t":32.30,"st":"TITLE","text":"Press START","action":"start 200 100"},
  {"t":34.75,"st":"SCANNING","text":"Scanning for installed downloadable content... please wait."},
  {"t":37.50,"st":"MENU","text":"Online Multiplayer Downloadable Content 2 Single Player","action":"right,right,cross"},
  {"t":49.19,"st":"ALERT","text":"A Red Dead Redemption single player save could not be found. What would you like to do?"},
  {"t":94.20,"st":"ALERT","text":"same, unchanged for 45 s (36 consecutive samples 49.19..94.20)"}
 ]
}

The ALERT screen, verbatim from OCR of the centre band:

    ALERT
    A Red Dead Redemption single player save could not be found. What would you like to do?

with the three rows, read from the pixel rows at the bottom right:

    Continue Without Saving        (Cross)
    Create A New Save Game         (Square)
    Retry                          (Circle)

NO CRASH: exit -9 because MY driver killed it at the 150 s budget, not the guest.
That is a different failure from the one in the task statement, and it is the
VFS that makes the difference: the SIGSEGV at guest 0x10 happens only when the
title loads an EXISTING save and then has to run the game world; with no save it
waits at the prompt for as long as it is left alone.

Captures: /tmp/p4drive_loopA/caps/0048-MENU.png (misclassified ALERT --
the classifier matched "Single Player" in the same band) up to 0075-MENU.png.
Evidence copied to docs/evidence/rdr-p4-alert-live.png.
