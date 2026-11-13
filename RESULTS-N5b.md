# N5b -- the audio events: does the notification queue deliver, does the read index advance, does the PPU wake?

Worktree /root/Dev/wt-N5b, branch explore/N5b, based on main 202e9ef.
State: claim written before any measurement. Measurements appended below in order.

Task: after M2's cellAudio fixes -- one event per block (5.33 ms), read index
advance, PPU MultiStream thread wake. RDR port 0 measured 0/8 filled while port 1
is 16/16; explain the difference.

## Measurement 0 -- the baseline (no runs yet)

Claim placed at $(date -u +%FT%TZ). Nothing measured yet.
