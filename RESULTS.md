# L3 -- invisible host writes: one funnel, a read watch, a page guard

Branch explore/l3 from main HEAD 22d2a0c, worktree /root/Dev/wt-L3.

## Task

The ~80 sites that do `memcpy/memset(vm::ptr(...))` bypass rsv entirely: they
neither bump the 128-byte line version (so no lwarx/stwcx reservation is
invalidated) nor feed the code/line/block watch. Deliverables:

1. one funnel that both invalidates reservations and feeds the watch,
2. a READ watch (the existing GOWA_TRAP read mode only covers addresses the
   caller names up front; the scanned read is what shows who *reads* a field),
3. a non-fatal page guard so any address can be traced,
4. a measurement showing a host write invalidating a reservation.

## Status

running. Measurements are appended below as they are taken, raw.
