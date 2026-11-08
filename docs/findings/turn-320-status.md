# Turn 320: verified 10/10, and why the seed pair still fails

## What is solid

**10/10 world creations, zero network errors** (`/tmp/p10-fix/summary.txt`),
verified per-iteration: every one has `sys_vm_memory_map ->` in its own log and
zero occurrences of "Connection to the server was lost". Commit `c9e405e`.

**The SPU mechanism**: 78.86% -> 0.00% interpreted, same counter both sides
(`docs/findings/spu-aot.md`). Covers boot, menus and loading only.

**The run-lifetime root cause** (from the crash track, reproduced here): a game
launched from a tool call dies when the call returns, logging
`gpu: window closed, exiting` with no crash. Only
`nohup setsid tools/run_world.sh ... & disown` survives. Commit `b45552b`.

## The seed pair: attempted three times, still not valid

I got a clean VFS, `DRIVE_SEED=seed42`, and one run per mode. The seeds still
differ:

    AOT    RandomSeed=0x2ce9c74bd8ce3077  sha eb449568...
    INTERP RandomSeed=0x9d1d0c87a22989c1  sha cea746dd...

The driver prints `seed typed 'seed42' visible=True` but `seed_visible()` is a
stub that returns True unconditionally -- I replaced the pixel heuristic with it
because the heuristic could not discriminate (same Seed row: median luminance
35..208, dark-pixel count 43..1299 depending on the screen behind the dialog).
So the "visible=True" line proves nothing, and the seeds prove the text never
landed.

**What the captures show**: in the h2 shot sequence the driver reached the form
with the focus ring already on **Create New World** (y=507) rather than walking
to the Seed row (y=113) and typing there. The seed block runs, but it presses
`Down` toward FORM_SEED_Y, then `z`, then types -- and the frame before typing
shows the ring at Create, meaning the walk-toward-Seed loop had already exited
(presumably on the tolerance) without the ring being on the seed.

**What a valid comparison needs, concretely:**
1. a check that reads the ring position back and asserts it is within the Seed
   row's band before pressing `z` to enter it -- not just "press Down up to N
   times";
2. `seed_visible()` implemented with something that actually distinguishes, or
   dropped in favour of reading `RandomSeed` from the saved world (already
   implemented, `world_seed()`), and the comparison must assert
   `world_seed(a) == world_seed(b)` **before** hashing;
3. the world-hash comparison must abort loudly when the seeds differ, rather
   than printing two hashes side by side.

Until that lands, **no hash comparison from this campaign is valid** and I am
not reporting one.

## Times measured, and why they are only indicative

    interpreted  post_start 117.99 -> autosave 618.43   (500.4 s)
    compiled     post_start 126.58 -> autosave 667.37   (540.8 s)

Different worlds (different seeds), and the machine sat at load 18-19 with
several instances for part of it. Neither number is a controlled measurement and
the compiled run was not faster.
