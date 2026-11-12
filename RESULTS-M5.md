# M5 -- closed-loop driving: read the screen, press START, capture what follows

Branch explore/m5 from main HEAD 4a5a132.

Task: RDR reaches its title screen ("RED DEAD REDEMPTION" + "Press START") at ~22 s
and the SPURS abort kills it at ~23 s. Drive it from the screen: read the window,
press START through xdotool, capture every following screen before the abort.
Deliverable: the capture of the screen that follows Press START.

## Status: started

Proof follows, measurement by measurement, appended with the raw output.

## Environment

- worktree /root/Dev/wt-M5, branch explore/m5, from main 4a5a132
- display :964 (Xvfb 1280x720x24, own display so no focus stealing)
- build-64 from the shared generated dirs (out-rdr), ccache wired
- inputs symlinked: bin fw keys ref vfs
