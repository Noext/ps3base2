# RESULTS-Q1 -- the lift gap at rdr+0xad5618

Branch explore/q1, worktree /root/Dev/wt-Q1, base main d2269e5.

## Task

`F ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code) (lr=0x00adc030)` at ~48 s.

Four questions: nature of the stop, calling site, why each of the three discovery
paths missed it, and the general rule change in recomp/ppu/analysis.py plus loop
convergence.

## Log

(entries appended after every measurement)

