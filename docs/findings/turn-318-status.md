# Turn 318: what is established, and what is not

## 1. Regression "Connection to the server was lost" -- FIXED, 10/10 verified

**The regression was real and I reproduced it.** The NP fixes left the runtime
reporting the *simulated sign-in* by default. With ONLINE reported, the title
believes it has a session, starts one, and dies on

    Network Error -- Connection to the server was lost. Exiting to the main menu.

**Why the first 10/10 passed anyway**: `proof10.sh` cuts each iteration at
200 s, and the network box appeared *after* the world was allocated. The 200 s
runs all passed; a 700 s run failed, and the screenshot at t=250 s shows the
network box with `vm=1` behind it.

**Fix** (`c9e405e`): offline is the default again, `PS3BASE_NP_ONLINE=1` opts
into the simulated sign-in. Two measured reasons: it is the mode the loading
proof used, and in it the manager callback delivers status -1 so the title never
opens a session.

The driver also now classifies the "Online ID currently not online" prompt and
takes its second row ("Continue playing offline") instead of the default Connect.

**Proof, run on the fixed main:**

    PASS 10 / 10     (/tmp/p10-fix/summary.txt)

Verified independently for all ten: `sys_vm_memory_map ->` present in every
iteration's own log, and **zero** occurrences of "Connection to the server was
lost" across all ten.

## 2. SPU: 78.86% -> 0.00% interpreted, but NOT on the three sessions asked for

Measured with the runtime's own counter:

| | interpreted share |
|---|---|
| baseline | **78.86%** of 226 552 351 insns |
| after compiling the captured blocks | **0.00%** of 231 204 497 insns |

**What this number covers, stated plainly: boot, menus, and world loading.**
Both sessions reached `sys_vm_memory_map ->` and one ran 4201 flips at 60 fps,
but I did **not** complete a full spawn-area generation plus ten minutes of
gameplay in either, so the 0.00% does not cover the generation phase.

**Not done, and I will not claim it:**
- three different seeds: not run, only one;
- ten minutes of gameplay with movement, breaking and placing: not run;
- convergence to under 1% on those three sessions: not measured.

The machine sat at load 18-19 with several instances and compilers running for
most of this turn, which is why the generation phase never finished inside the
budget. That is an orchestration failure on my side, not a property of the code.

## 3. Correctness: the hash comparison was a FALSE POSITIVE

I reported earlier that the AOT and interpreted runs produced
`dd35dda2...2288`. **That was wrong.** Both runs wrote to the same save
directory `BLES01976--261108210218`, so the second one read a world that was
already there instead of generating its own. Same file, identical hash, no
information.

**What I added to make a real comparison possible** (`be3b354`):
- `DRIVE_SEED=<text>` in the driver: focuses the Seed row, types it, escapes,
  then walks to Create, so two runs are asked for the same terrain;
- the text-row constants were guesses (110/154) that never matched a real
  capture, so the Seed guard never fired. Measured on a real form frame the two
  yellow rings start at **65** and **113**, and their detected top drifts a few
  pixels between captures (127 and 131 for the same row), so the text band uses a
  wider tolerance than the Create-row check.

A clean-VFS fixed-seed run was started and reached 50% of spawn-area generation
before its budget expired under load. **No valid pair of hashes was obtained.**

## What the turn did not deliver

- three-seed SPU convergence under 1% on real generation and gameplay;
- an identical-hash pair from an actual generation in both modes;
- clean before/after generation times and fps on the real GPU via play.sh.

The 10/10 result is solid. The SPU mechanism is proven and reproducible
(`docs/findings/spu-aot.md`), but only on the phases measured. The hash and
timing comparisons need a quieter machine and must be redone.
