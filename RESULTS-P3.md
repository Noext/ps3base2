# RESULTS-P3 -- fibres (cellFiber) before the RDR crash

Branch explore/p3, worktree /root/Dev/wt-P3, based on main 01c5e16.

## Task
Instrument every cellFiber* call (create, switch, return) plus the guest stack
pointer r1 before and after each switch, ahead of the RDR host SIGSEGV at guest
0x10 reached through ppu::call_indirect.

## Log
