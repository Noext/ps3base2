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
