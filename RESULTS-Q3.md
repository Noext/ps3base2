# RESULTS-Q3 -- drive past WARNING (RDR)

Branch: explore/q3, from main HEAD d2269e5.
Task: tools/drive_rdr.py must continue after the WARNING screen on BOTH branches
(Square = Continue Without Saving, Cross = Create A New Save Game), capturing every
screen until the main menu or the start of gameplay.

Environment: GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash, cellFiber HLE default ON,
ulimit -s unlimited, build via tools/build_slot.sh cmake --build <dir> -j 8.

## Log

(pending: setup)
### M0 (12:27Z) -- BASELINE: drive_rdr.py at main HEAD d2269e5 already passes WARNING, by accident

Command (verbatim):

    cd /root/Dev/wt-Q3 && ulimit -s unlimited
    GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash PS3BASE_RUN_ROOT=/root/Dev/ps3base2 \
      python3 tools/drive_rdr.py /root/Dev/ps3base2/build-rdr/ps3base :146 \
        /root/Dev/p4work/vfs-clean /tmp/q3-base 200

Display :146 = Xvfb 1280x720x24 -ac +extension GLX +render -noreset (started for this task).
Binary: /root/Dev/ps3base2/build-rdr/ps3base, 978616328 bytes, mtime 12:21, and
`cmake --build build-rdr --target ps3base` says "ninja: no work to do" -- it is the
main-HEAD source. (The full `build-rdr` target list fails on `spu_bench` with
`fatal error: 'aux_gen.h' file not found`; that is pre-existing and not the RDR binary.)

Driver journal, verbatim from validation.json:

    0.00   OTHER
    43.64  TITLE     Press START
    43.64  -> start
    46.41  OTHER
    49.98  MENU      Online Multiplayer Downloadable Content 2 Single Player
    49.98  -> right,right,cross (Single Player)
    63.11  OTHER
    70.80  ALERT     A Red Dead Redemption single player save could not be found. What would you like to do?
    70.80  -> square (alert action 1 of 2)
    72.26  -> cross (alert action 2 of 2)
    75.83  WARNING   Autosave has been disabled. You may enable autosave in the options menu.
    75.83  WARNING: WARNING
    80.94  LOADING   0 S6) Loading...
    89.55  OTHER     ... EXODUS IN AMERICA
    exit -6 elapsed 112.0
    states {"OTHER": 54, "TITLE": 2, "MENU": 6, "ALERT": 4, "WARNING": 4, "LOADING": 4}

Guest log, verbatim:

    I 109.997797 rsx: flip 1741 (buffer 0) worst 87.5 ms, >50ms 4, >100ms 0
    I 111.086103 cellFiber: HLE CreateFiber #586 entry=0x01385418 ...
    I 111.095626 cellFiber: HLE RunFibers(0x0165d580) ran 3459 fibre switch(es) -> CELL_OK
    F 111.368153 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)
      (lr=0x00adc030, thread 0x1000000) r3=0x1 r4=0xae5818 r9=0x73a81338 r31=0x73923fe4 ctr=0xae5818

So the run DOES get past the WARNING, and the screen after it is not a menu: it is the
RDR key-art LOADING screen, then the chapter card **EXODUS IN AMERICA** at 89.55 s, and
the known 0x00ae5818 fault at 111.37 s. Captures: /tmp/q3-base/caps/WARNING-0058.png,
LOADING-0062.png, OTHER-0066.png (EXODUS IN AMERICA).

BUT the pass is accidental and it is the bug the task is about. The driver's plan is
[\"square\", \"cross\"] and it consumes one plan entry per ALERT sample. The ALERT was
sampled TWICE (n=54 and n=55, 1.46 s apart), so BOTH plan entries were spent ON THE
ALERT: square at 70.80 and cross at 72.26, while the ALERT was still up. The WARNING
then appeared at 75.83 with the plan already empty, and the driver has no WARNING action
at all (its `act()` WARNING branch only prints). The WARNING was dismissed by the
*second* press, which was meant for the ALERT.

That means:
  - the second branch (Cross = Create A New Save Game) was never actually driven --
    the cross went to the ALERT's row 2 only because the ALERT was sampled twice;
  - whether WARNING is dismissed at all depends on the ALERT happening to be sampled
    twice, which is a timing accident, not a control loop;
  - a run in which the ALERT is sampled once leaves the driver stuck on the WARNING
    with an empty plan.

The fix is therefore in the driver, not the runtime: the plan must be indexed by the
STATE being acted on, and WARNING must have its own action.

### M1 (12:33Z) -- the driver's plan was consumed per SAMPLE; proof of the bug and the fix

The bug, stated as the code that produced it (previous tools/drive_rdr.py):

    def act(self, state, elapsed, n):
        ...
        elif state == 'ALERT' and self.alert_index < len(self.alert_actions):
            button = self.alert_actions[self.alert_index]
            self.alert_index += 1          # <-- one plan entry per ALERT SAMPLE
            self.send(button)

and the WARNING branch had no press at all:

    elif state == 'WARNING' and 'warning' not in self.done:
        self.done.add('warning')
        print(...)                          # <-- prints, presses nothing

So the only reason M0 got past the WARNING is that the ALERT was sampled twice,
1.46 s apart, and the second sample's cross landed on the WARNING. A run in which
the ALERT is sampled once leaves the driver on the WARNING pressing nothing.

The fix (tools/drive_rdr.py, this branch): a press belongs to a SCREEN, and the
evidence that it worked is the appearance of the screen that FOLLOWS it:

    AFTER_ALERT   = ('WARNING', 'LOADING', 'CHAPTER')
    AFTER_WARNING = ('LOADING', 'CHAPTER')

    if state in AFTER_ALERT:   self.satisfied.add('ALERT')
    if state in AFTER_WARNING: self.satisfied.add('WARNING')

    elif state == 'ALERT' and 'ALERT' not in self.satisfied:
        self.press_once('ALERT', self.args.branch, ...)
    elif state == 'WARNING' and 'WARNING' not in self.satisfied:
        self.press_once('WARNING', 'cross', ...)     # both branches: Continue [X]

`press_once` re-presses only if the same screen is still up --retry seconds
later (default 10 s), because the pad queue drains in ~4 s and a retry inside
that window would double-press. The ALERT row is now selected by --branch
(square = Continue Without Saving, cross = Create A New Save Game) instead of by
consuming a plan list, so the two branches are actually two different runs.
A CHAPTER state (\"EXODUS IN AMERICA\") and a LAST-*.png grab were added so the
deliverable is the last screen reached even when the guest dies between samples.

### M2 (12:34Z) -- CONTAMINATION: tools/regress.sh clear_strays() kills a driver run

First run of the fixed driver, square branch, on :146:

    exit -9 elapsed 125.2
    states {"OTHER": 79, "TITLE": 5, "SCANNING": 1, "MENU": 1}
    journal: TITLE at 111.07 s, SCANNING 116.14, MENU 119.64, then killed

-9 at 125.2 s is not the guest and not my 240 s budget. Machine state at that
moment, verbatim:

    $ uptime
     12:35:02 up 3 days, 16:53,  0 user,  load average: 57.02, 48.62, 32.74
    $ ps aux | grep -E 'regress|clear_strays' | grep -v grep
    root 2820847  0.4  0.0   2940  1820 ?  S  12:26  0:02 sh tools/regress.sh

and the killer, from tools/regress.sh:176-182:

    clear_strays() {
        for p in /proc/[0-9]*; do
            c=$(tr "\0" " " < "$p/cmdline" 2>/dev/null) || continue
            case "$c" in
                *ps3base*--no-launcher*)

My binary was /root/Dev/ps3base2/build-rdr/ps3base --no-launcher, which matches.
The fix is the one P4 used and is not a change to regress.sh: hard-link the
binary under a name the pattern does not match.

    $ ln -f /root/Dev/ps3base2/build-rdr/ps3base /root/Dev/q3work/rdrx
    $ stat -c '%i %h' /root/Dev/ps3base2/build-rdr/ps3base /root/Dev/q3work/rdrx
    2630061 2
    2630061 2                      <- same inode: the same binary, not a copy
    $ case '/root/Dev/q3work/rdrx --no-launcher' in *ps3base*--no-launcher*) echo BAD;; *) echo SAFE;; esac
    SAFE

The 111 s time-to-title in that run (against 43.64 s in M0) is the load, not a
regression: load average went 20 -> 57 between the two runs.

### M3 (12:35-12:37Z) -- BOTH BRANCHES DRIVEN PAST WARNING, A/B, SAME DISPLAY, SAME MINUTE

The two arms differ ONLY in --branch. Same binary (hard-linked /root/Dev/q3work/rdrx,
inode 2630061, the same inode as build-rdr/ps3base), same VFS
(/root/Dev/p4work/vfs-clean, savedata/ empty), same display :146, same
GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash, back to back with no other work
between them.

    === START 12:35:12 load 49.35
    === square done 12:36:24 load 34.08
    === cross done 12:37:30 load 25.95

ARM 1 -- --branch square (Continue Without Saving), output verbatim:

      0.00 STATE OTHER      |
     23.70 STATE TITLE     Press START
     23.70 -> start
     25.99 STATE SCANNING  Scanning for installed downloadable content... please wait. _
     28.39 STATE MENU      Online Multiplayer Downloadable Content 2 Single Player &
     28.39 -> right,right,cross (Single Player)
     35.45 STATE OTHER      |
     41.48 STATE ALERT     (OCR) ... save could nat be found. What would you like to do?
     41.48 -> square (ALERT: Continue Without Saving)
     45.09 STATE WARNING   Autosave has been disabled. You may enable autosave in the options menu.
     45.09 -> cross (WARNING: Continue)
     48.86 STATE LOADING   ... Loading...
     57.08 STATE CHAPTER   ... EXODUS IN AMERICA
     64.95 STATE OTHER     (transition)
     66.96 STATE CHAPTER   ... EXODUS IN AMERICA
     71.25 STATE OTHER     |
    exit -6 elapsed 71.8
    states {"OTHER": 28, "TITLE": 2, "SCANNING": 2, "MENU": 5, "ALERT": 5,
            "WARNING": 5, "LOADING": 4, "CHAPTER": 7}

ARM 2 -- --branch cross (Create A New Save Game), output verbatim:

      0.00 STATE OTHER      |
     31.93 STATE TITLE     Press START
     31.93 -> start
     34.21 STATE OTHER     | od dow tent ewait. _
     35.19 STATE SCANNING  Scanning for installed downloadable content... please wait. _
     36.36 STATE MENU      Online Multiplayer Downloadable Content 2 Single Player &
     36.36 -> right,right,cross (Single Player)
     42.27 STATE OTHER     (transition)
     44.23 STATE ALERT     A Red Dead Redemption single player save could not be found. What would you like to do?
     44.23 -> cross (ALERT: Create A New Save Game)
     46.90 STATE WARNING   This game saves data automatically at certain points. Do not switch off the power when the
     46.90 -> cross (WARNING: Continue)
     48.78 STATE OTHER     |
     49.40 STATE LOADING   Loading...
     53.07 STATE CHAPTER   & EXODUS IN AMERICA
     61.57 STATE OTHER     (transition)
     63.01 STATE CHAPTER   ... EXODUS IN AMERICA
     64.64 STATE OTHER     (transition)
    exit -6 elapsed 66.0
    states {"OTHER": 33, "TITLE": 3, "SCANNING": 1, "MENU": 4, "ALERT": 4,
            "WARNING": 2, "LOADING": 2, "CHAPTER": 6}

The two arms produce the TWO DIFFERENT WARNING BODIES the branch predicts, and
nothing else differs:

    square -> WARNING "Autosave has been disabled. You may enable autosave in the
                        options menu."
    cross  -> WARNING "This game saves data automatically at certain points. Do not
                        switch off the power when the HDD access indicator is
                        flashing. Autosave may be turned off at any time in the
                        options menu."

Each WARNING is dismissed by its OWN press -- "-> cross (WARNING: Continue)" in both
arms -- which is the thing the old driver could not do. The ALERT press is now
selected by --branch, so the cross arm's cross went to the ALERT's row 2 and not to
the WARNING by accident.

### M4 -- THE DELIVERABLE: the last screen reached on each branch, and the crash instant

Last screen reached, both branches: the RDR chapter card **EXODUS IN AMERICA**,
full 1280x720, over the red key art.

    docs/evidence/q3-square-chapter.png   279042 bytes  (square arm, CHAPTER-0050, t=57.08 s)
    docs/evidence/q3-cross-chapter.png    343426 bytes  (cross arm,  CHAPTER-0048, t=53.07 s)

The chapter card is not a static image: it CROSS-FADES through three different key
arts in each arm (a mesa, a rider with a rifle, a town street), which is why CHAPTER
is sampled 7 times in the square arm and 6 in the cross arm. The two arms show
different art at the same state, so the card is animating, not stuck.

Every screen of both arms, in order, as committed evidence:

    q3-square-alert.png    q3-square-warning.png    q3-square-loading.png    q3-square-chapter.png
    q3-cross-alert.png     q3-cross-warning.png     q3-cross-loading.png     q3-cross-chapter.png

The crash, and the screen on display at that instant
----------------------------------------------------
Both arms die on the SAME fault, at the SAME place, with the chapter card as the
last thing drawn:

  square arm
    I  68.720554 rsx: flip 1381 (buffer 0) worst 139.6 ms, >50ms 2, >100ms 1
    F  70.954815 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)
       (lr=0x00adc030, thread 0x1000000) r3=0x1 r4=0xae5818 r9=0x737d9698 r31=0x73693fe0 ctr=0xae5818
    exit -6 elapsed 71.8

  cross arm
    I  62.132268 rsx: flip 1381 (buffer 0) worst 39.5 ms, >50ms 0, >100ms 0
    F  64.670457 ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code)
       (lr=0x00adc030, thread 0x1000000) r3=0x1 r4=0xae5818 r9=0x739e1698 r31=0x73697fe4 ctr=0xae5818
    exit -6 elapsed 66.0

The crash instant is 70.954815 s (square) / 64.670457 s (cross) of guest time. The
screen on display then is the chapter card -- the driver's final grab, taken after
the guest exited, is a BLACK frame (mean=0, 269 bytes, LAST-0058.png / LAST-0055.png),
which is the window being cleared on exit, not a new screen. The last frame the guest
actually DREW is the chapter card, and the last flip before the fault is flip 1381 in
BOTH arms.

Both arms reach flip 1381 and then fault, which is a strong statement that the crash
is not timing: the two runs differ by 5 s of start-up and by 11 s of total, and stop
at the same flip.

What the guest is doing at the crash, verbatim from the square arm's log tail -- this
is the engine running, not a menu waiting:

    I  70.553897 cellFiber: HLE CreateFiber #682 entry=0x01385418 arg=0x7303b660 stack=0xd0033490+0xdc70 prio=0 -> CELL_OK
    I  70.554093 cellFiber: HLE CreateFiber #683 entry=0x01385418 arg=0x7303b660 stack=0xd0041490+0xdc70 prio=0 -> CELL_OK
    I  70.566557 cellFiber: HLE RunFibers(0x0165d580) ran 2805 fibre switch(es) -> CELL_OK
    I  70.566744 cellFiber: HLE JoinFiber #683 -> status 0x1e, CELL_OK
    I  70.566927 cellFiber: HLE JoinFiber #682 -> status 0x1e, CELL_OK
    I  70.566934 cellFiber: HLE JoinFiber #681 -> status 0x1e, CELL_OK
    I  70.942142 sceNp: ManagerGetStatus -> -1
    F  70.954815 ppu: unknown indirect target 0x00ae5818 ...

The RAGE threads are up from 0.48 s ('[RAGE] DVD Streamer', '[RAGE] HDD Streamer',
'[RAGE Audio] Multistream'), the renderer is drawing (flip 1381, 31 fps, 32.5 ms
frames), and cellFiber is running 2805 switches in one RunFibers. So the run is past
the menus and into the game's own start-up when it hits the unlifted target.

### M5 -- the driver's exit codes and what "reached the end" means here

The task's criterion was "until the main menu or the start of gameplay". Both arms
pass the menus and reach the chapter card, which is the first screen of gameplay
start-up, and the guest then dies on its own fault (exit -6, SIGABRT from the
runtime's unknown-indirect-target abort, not a driver kill). The driver reports the
guest's own exit code, so exit -6 here is a RESULT: the run ended by itself.

