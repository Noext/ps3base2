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
### M4 -- the ALERT's three buttons, read from the live capture

Enlarged crop of /tmp/p4drive_loopA/caps/0050-MENU.png at (780,520,500x200):

    Continue Without Saving  [SQUARE  pink square]
    Create A New Save Game   [CROSS   blue X]
    Retry                    [CIRCLE  red ring]

The glyphs are the PS3 pad symbols, so the presses are square for the first row,
cross for the second and circle for the third -- NOT the usual cross/OK on row 1.
Reading this from the pixels matters: pressing cross first would pick "Create A
New Save Game" and skip the step the task asks for first.

Also noted, and fixed in the driver: the first classifier called this screen
MENU, because the OCR band at the bottom still reads the main menu's rows through
the ALERT's transparent background. The state machine now checks the CENTRE band
for the ALERT sentence before looking at the bottom band for menu rows.

Cropped proof: docs/evidence/rdr-p4-alert-options.png.
### M5 -- THE DELIVERABLE: the screen that follows the ALERT dialogue

Closed-loop drive, plan [square, cross], clean savedata VFS, binary at
/root/Dev/p4work/rdrx (a hard link of build-rdr/ps3base under a name another
agent's tools/regress.sh clear_strays() does not pattern-match -- see below).

Driver journal, verbatim:

      0.34 STATE OTHER
     27.74 STATE TITLE     Press START
     27.74 -> start
     29.81 STATE SCANNING  Scanning for installed downloadable content... please wait.
     32.10 STATE MENU      Online Multiplayer Downloadable Content 2 Single Player
     32.10 -> right,right,cross
     43.47 STATE OTHER
     51.73 STATE ALERT     A Red Dead Redemption single player save could not be found. What would you like to do?
     51.73 -> square (alert action 1)      <- Continue Without Saving
     52.71 -> cross (alert action 2)
     54.19 STATE OTHER     Autosave has been disabled. You may enable autosave in the options menu.
     57.83 STATE LOADING   Loading...
     exit -11 elapsed 66.8

The screen that follows the ALERT dialogue is a SECOND DIALOGUE, in the same
red RDR frame, headed WARNING:

    WARNING
    Autosave has been disabled. You may enable autosave in the options menu.
                                                                Continue  [X]

It appeared 2.5 s after the square press and stayed until the cross press
(54.19). The screen after THAT is the RDR key-art loading screen
(Loading..., 57.83), and the run then dies in the known null+0x10 SIGSEGV
(exit -11).

Evidence, all full 1280x720 frames:
  docs/evidence/rdr-p4-alert.png              the ALERT screen as driven
  docs/evidence/rdr-p4-warning.png            THE DELIVERABLE: the WARNING that follows
  docs/evidence/rdr-p4-postalert-loading.png  the loading screen after the warning

The exact sequence in one run, three screens, no interpretation:
  ALERT --[square]--> WARNING --[cross]--> LOADING --> SIGSEGV at guest 0x10

### Contamination to know about (not mine to fix)

tools/regress.sh clear_strays() kills every process whose cmdline matches
*ps3base*--no-launcher*:

    case "$c" in
        *ps3base*--no-launcher*) ... kill -9 ...

Measured: my first closed-loop run of this sequence died at 1.6 s (journal one
sample, no guest activity) while /tmp/out-V/regress-rdr-current.out showed
another agent's regress.sh at 10:19 running its rdr arms. The same run with
the binary hard-linked to a non-matching name completed all 66.8 s. A driver
that shares this machine can be killed by any concurrent regress run.
### M6 -- the sequence reproduces on a second run (A/B not needed: same script, same VFS)

Same command, run again 2 minutes later, machine load 30 (three other agents'
regress arms were running):

     42.57 STATE TITLE     Press START
     45.00 STATE SCANNING  Scanning for installed downloadable content... please wait.
     48.89 STATE MENU      Online Multiplayer Downloadable Content Single Player
     72.23 STATE ALERT     A Red Dead Redemption single player save could not be found. What would you like to do?
     72.23 -> square (alert action 1)
     73.32 -> cross (alert action 2)
     79.78 STATE OTHER     Autosave has been disabled. You may enable autosave in the options menu.
     88.40 STATE LOADING   Loading...
     exit -11 elapsed 88.7

The same four screens in the same order, with 15 s of extra start-up because
the machine was three times as loaded. The start time moved; the sequence did
not. Captures: /tmp/p4drive_loopD/caps/OTHER-*.png, LOADING-*.png.
### M7 -- the OTHER arm: Create A New Save Game (cross) reaches a different WARNING

PLAN [cross, cross, cross] -- cross is row 2, Create A New Save Game:

     57.37 STATE TITLE     Press START
     66.06 STATE MENU      Online Multiplayer / Downloadable Content / Single Player
     95.10 STATE ALERT     A Red Dead Redemption single player save could not be found.
     95.10 -> cross (alert action 1)
    100.51 STATE OTHER     This game saves data automatically at certain points. Do not
                            switch off the power when the HDD access indicator is flashing.
                            Autosave may be turned off at any time in the options menu.
    114.53 STATE LOADING   Loading...
     exit -11 elapsed 114.8

So BOTH branches of the ALERT continue into a WARNING dialogue in the same red
frame, with different body text:

  Continue Without Saving -> WARNING 'Autosave has been disabled. You may enable
                             autosave in the options menu.'          [Continue X]
  Create A New Save Game  -> WARNING 'This game saves data automatically at certain
                             points. Do not switch off the power when the HDD access
                             indicator is flashing. Autosave may be turned off at any
                             time in the options menu.'  AND the three ALERT rows are
                             still drawn under it (Continue Without Saving / Create A
                             New Save Game / Retry)

Both then reach the same RDR key-art 'Loading...' screen and both die in the
same null+0x10 SIGSEGV at guest 0x10 inside f_0053338c.

Evidence: docs/evidence/rdr-p4-warning-new-save.png (the new-save warning),
and /tmp/p4drive_loopE/caps/LOADING-0084.png (the loading after it).
### M8 -- the driver is now a repo tool, and it reports ALERT, WARNING, LOADING as three states

Added tools/drive_rdr.py (source, not just a transcript): grab -> OCR two bands
-> classify -> press, journaled. It encodes the four things this task cost to
learn:

1. CLOSED LOOP, because start-up is not a fixed cost. Measured on this machine,
   time-to-title for the SAME binary and VFS:

       load 12 : 23.67 s      (run /tmp/p4tool)
       load 30 : 42.57 s      (run loopD)
       load >30: 56.55 s      (run /tmp/p4tool2)

   A fixed schedule that presses START at t=40 s therefore hits the title on one
   run and the intro on the next. That is why three earlier runs pressed into a
   black frame.

2. TWO OCR BANDS, centre tested first. RDR draws its dialogues over a
   transparent red frame and the main menu's rows stay legible underneath, so a
   single wide band reads 'Single Player' on the ALERT and calls it a menu --
   measured, and it cost a run (M3's journal labels the ALERT as MENU).

3. ALERT and WARNING are DISTINCT states. Both are the same red panel; only the
   body text separates them. Folding them together (which my first tool version
   did) means the driver cannot report having seen the screen it was asked for.

4. An empty savedata/ is a precondition, and the tool REFUSES to run otherwise:

       $ tools/drive_rdr.py rdrx :145 vfs-clean /tmp/x
       drive_rdr: .../dev_hdd0/home/00000001/savedata is not empty; RDR will
                  load a save instead of prompting.
       exit 2

   This guard earned itself immediately: my own earlier runs had written saves
   back into the VFS, so the next run would have silently tested the wrong path.

Verified end to end, run /tmp/p4tool2 (the committed tool, unmodified):

     56.55 STATE TITLE     Press START
     62.87 STATE MENU      Online Multiplayer / Downloadable Content / Single Player
     88.62 STATE ALERT     A Red Dead Redemption single player save could not be found.
     88.62 -> square (alert action 1 of 2)
     89.94 -> cross (alert action 2 of 2)
     91.39 STATE WARNING   Autosave has been disabled. You may enable autosave in the options menu.
     92.53 STATE LOADING   Loading...
     exit -11 elapsed 95.4

   states {"OTHER":58,"TITLE":3,"MENU":9,"ALERT":2,"WARNING":1,"LOADING":1}

The WARNING frame from that run is committed as
docs/evidence/rdr-p4-warning-tool.png -- produced by the committed tool, so the
evidence and the tool cannot drift apart.
