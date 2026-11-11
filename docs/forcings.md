# Forcings

A forcing is something set by hand that the console would have produced itself.
Each one is a debt: it is recorded here with the evidence that made it necessary
and the condition that would let it be removed.

## F-001 -- rdr2_installed.sdat pre-placed under dev_hdd0

**RETIRED 2026-11-11. It documented a problem that did not exist.**

The archive was copied by hand into
`PS3BASE_DISC_DIR/dev_hdd0/game/BLES00680/USRDIR/`, which the runtime never reads:
`GOWA_HDD0_DIR` defaults to `./vfs/dev_hdd0` (runtime/src/vfs.cpp:86), a directory
of its own. The title's own install step had already written the real one:

    516M  vfs/dev_hdd0/game/BLES00680/USRDIR/rdr2_installed.sdat
          vfs/dev_hdd0/game/BLES00680/PARAM.SFO

was created in a directory the runtime never reads. The install had succeeded, in
`vfs/dev_hdd0/game/BLES00680`, and the FATAL that prompted the forcing was caused
by reading the wrong directory, not by a failed copy.

The lesson is in the record rather than deleted: a forcing that "fixes" a failure
must first prove the failure is real by reading the state the runtime actually
uses, not the state a human assumed it uses.

