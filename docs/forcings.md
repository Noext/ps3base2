# Forcings

A forcing is something set by hand that the console would have produced itself.
Each one is a debt: it is recorded here with the evidence that made it necessary
and the condition that would let it be removed.

## F-001 -- rdr2_installed.sdat pre-placed under dev_hdd0

**What.** The 539 MB archive is copied from the disc to
`dev_hdd0/game/BLES00680/USRDIR/rdr2_installed.sdat` before the first run,
instead of being written by the title's own install step.

**Why.** Without it the title stops at startup:

    [PACKFILE] Loading archive 'gamedata:/rdr2_installed.sdat' from hard drive
    [FATAL] Cannot load archive 'gamedata:/rdr2_installed.sdat'
    _sys_process_exit(0x1)

The install step opens both the source and the destination, then writes nothing:
the destination file exists but stays at 0 bytes while the title expects
0x6fe02000. The installer thread is created and immediately parks in
sys_timer_usleep, and no sys_fs_write is ever issued.

**What it hides.** cellGameDataCheck and cellGameContentPermit both report
success and hand back `/dev_hdd0/game/BLES00680`, but the directory contents the
console would have created are absent. Any later failure that depends on those
files will look like a game bug.

**Removed when.** The install step runs normally: the title's own copy loop
issues sys_fs_write on the destination and the file reaches 0x6fe02000 bytes
without help. That is the acceptance test for F-001.

