# The seed cannot be typed, and why: cellOskDialog exists now but the title does not use it

## What I implemented

`runtime/src/hle/cellOskDialog.cpp`: the on-screen keyboard, following the three
neighbouring ports
(`refs/jdc-ps3recomp/libs/misc/cellOskDialog.c:341`,
`refs/dod3-recomp/.../cellOskDialog.c:122`). `LoadAsync` reads the caller's
`CellOskDialogInputFieldInfo`, raises `CELL_SYSUTIL_OSKDIALOG_LOADED` to the
registered sysutil callbacks, and when `PS3BASE_OSK_TEXT` is set fills the answer
and raises `CELL_SYSUTIL_OSKDIALOG_FINISHED`. `UnloadAsync` writes the text back
as UTF-16 big-endian into the caller's `CellOskDialogCallbackReturnParam`.
8 functions on `cellSysutil`, 3 on `cellOskExtUtility` -- the module names matter,
they must match the import strings exactly or the HLE is never found.

## Why the seed still cannot be set

**Measured: the title never calls any `cellOskDialog*` function on the world
form.** Zero hits in the whole boot-to-world sequence. The 7 OSK log lines I had
seen before came from t=21 s during **boot**, where the title installs the
keyboard-event hooks and support languages -- setup calls, not a dialog.

So the title reads that field some other way, and the host has no way to deliver
text: `runtime/src/host_input.cpp` maps every SDL key to a **pad button** and
nothing else. There is no SDL `TEXTINPUT` path to a guest text field, and the
title does not reach for the OSK to get one.

**Consequence for the campaign**: `DRIVE_SEED` cannot work by typing. Every
"seeded" run so far asked for a seed and got a random world, which is exactly
why `compare_worlds.py` kept reporting `SEEDS DIFFER`. That tool was right every
time; the driver was wrong every time.

## What this costs and what it does not

It does not affect any SPU result: the interpreted-share measurements, the
strict runs, the per-hash warnings and the convergence loop all stand, because
none of them compares two worlds.

It does block the identical-hash pair. Two honest options remain:

1. find the title's actual text path (its own keyboard handling, not the SDK OSK)
   and feed it -- that is a reverse-engineering task, not a one-line fix;
2. build the same world twice **from the same saved world** instead of from a
   seed: generate once, copy the save, load it in both modes, and compare what the
   compiled and interpreted paths produce from identical inputs.

Option 2 measures the thing the question actually asks -- do compiled blocks
produce the same result as interpreted ones -- without needing text input at all.

## The generation timings measured along the way

| mode | post-processing start | autosave | generation |
|---|---|---|---|
| interpreted (NO_AOT) | 35.83 | 255.94 | **220.11 s** |
| compiled | 41.41 | 231.76 | **190.35 s** |

Same code path, same disc, one run at a time, machine otherwise idle. The
compiled path is **13.5% faster** on generation. These are on two different
worlds (the seed could not be fixed), so the percentage is indicative, but the
ordering is consistent with the interpreted share going from 65% to 0%.