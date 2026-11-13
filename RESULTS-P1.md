# RESULTS-P1 -- reading the RDR host SIGSEGV at exe+0x17aa2b6

Branch explore/p1, worktree /root/Dev/wt-P1, base main 01c5e16.

## Task
1. addr2line exe+0x17aa2b6 and every crash frame; name the function (HLE handler
   vs lifted guest code).
2. Guest thread, guest lr, last 30 imports that thread made with returns.
3. Find '*NO NAME*' in the game ELF and in every module -- whoever contains it
   names the subsystem.
4. If the site is one of our handlers (cellSaveData* first suspect): arguments
   received, structure involved, compare against RPCS3's header.

## Log
