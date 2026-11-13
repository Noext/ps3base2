# RESULTS-Q2 -- the export coverage gate

Branch explore/q2, worktree /root/Dev/wt-Q2, from main HEAD d2269e5.

## Task

1. Build-time gate: for every LLE module, every exported function
   (export table -> descriptor -> code address) must have a lifted function;
   otherwise the build FAILS listing the missing ones.
2. Run it on all modules of all three games; give the table.
3. Prove why 47/47 exports verified was false -- what
   tools/check_lle_exports.py actually checked (it reported PASS on 12
   modules and 954 exports).
4. Prove why the libfiber lift stopped at 0x36005c80.

## Log

- 2026-11-13T12:25Z worktree created from main d2269e5, claim + status written.
