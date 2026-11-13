# RESULTS-Q1 -- the lift gap at rdr+0xad5618

Branch explore/q1, worktree /root/Dev/wt-Q1, base main d2269e5.

## Task

`F ppu: unknown indirect target 0x00ae5818 -- rdr+0xad5618 (no lifted code) (lr=0x00adc030)` at ~48 s.

Four questions: nature of the stop, calling site, why each of the three discovery
paths missed it, and the general rule change in recomp/ppu/analysis.py plus loop
convergence.

## Log

(entries appended after every measurement)


### M1 -- the stop is a JUMP TABLE dispatch, and 0xae5818 is entry 13 of 17

Static, from `bin/rdr/EBOOT.elf` (`tools/inventory.py` Elf), no run needed.

The dispatcher is `f_00ae50f0`, 35 instructions, ending at 0xae5178:

    00ae5120  3f00013e  addis  r24, r0, 0x013e        ; table base high half
    00ae5138  33185c8c  addic  r24, r24, 0x5c8c      ; table base = 0x013e5c8c
    00ae5160  889e0000  lbz    r4, 0(r30)             ; the opcode byte
    00ae5168  5484103a  rlwinm r4, r4, 2, 0, 29        ; * 4
    00ae516c  7c98202e  lwzx   r4, r24, r4            ; entry
    00ae5174  7c8903a6  mtspr  ctr, r4
    00ae5178  4e800420  bcctr

The table at 0x013e5c8c, dumped word by word:

     0 00ae517c   1 00ae51f0   2 00ae5284   3 00ae5328   4 00ae5580
     5 00ae53d4   6 00ae55d0   7 00ae5630   8 00ae5484   9 00ae5528
    10 00ae56a8  11 00ae5720  12 00ae57d8  13 00ae5818  14 00ae5424
    15 00ae5858  16 00ae58b8  17 00000000  18 00000000  19 00000000 ...

**0x00ae5818 is index 13 of a 17-entry table of code addresses.** The stop is
not a corrupted pointer and not a tail call: it is a switch case the lifter did
not emit a `case` for, so the generated `bcctr` fell through to
`ppu::call_indirect`.

Answer to question 1: **a jump table**, whose base is an absolute `addis/addic`
pair in the dispatcher itself.

### M2 -- the calling site: lr 0x00adc030 is the return address of the bl that calls the dispatcher

    00adc02c  480090c5  bl 0x00ae50f0        ; LK=1, target = 0xadc02c + 0x90c4
    00adc030  8b41008c  lbz r26, 0x8c(r1)    ; <- lr recorded by the lift

Verified: `analysis.branch_target(0xadc02c, 0x480090c5) = 0x00ae50f0` and
`0xadc02c + 4 = 0x00adc030`. So 0xadc030 is **not** a call site of its own; it
is the instruction after the call into the dispatcher. The generated code shows
exactly that:

    out-rdr/ppu/rdr_110.cpp:17316:  ctx.lr = 0xadc030; f_00ae50f0(ctx, base);

The containing function is `f_00adbf60` (prologue `f821ff11 stdu r1,-240(r1)`).
It sets up r4..r8 and calls the dispatcher, then compares the byte the
dispatcher wrote with `[r23]`, i.e. it is a *match* helper: try an opcode
handler, compare the result, fall back to 0xff. The four call sites of
f_00ae50f0 in the image are lr 0xadc030, 0xadc84c, 0xadc888, 0xadcb2c -- all
four are this same compare-and-retry pattern, so the dispatcher is a shared
bytecode/opcode matcher, not a one-off.

### M3 -- discovery path 1 (static seeds): the table scan is OFF, and it is the only scan that sees this

Measured, same ELF, same minute:

    \$ python3 tools/static_seeds.py --elf bin/rdr/EBOOT.elf --name rdr --json
    {"image": "rdr", "text_lo": 65536, "text_hi": 20080736, "toc": 20573104,
     "relocation_seeds": 0, "descriptor_seeds": 38522, "code_address_seeds": 0, "total": 38522}

    \$ python3 tools/static_seeds.py --elf bin/rdr/EBOOT.elf --name rdr --tables --out /tmp/q1-seeds-tables.txt --json
    {"...", "descriptor_seeds": 38522, "total": 42345}
    \$ grep -n '0x00ae5818\|0x00ae517c' /tmp/q1-seeds-tables.txt
    13147:0x00ae517c rdr
    13161:0x00ae5818 rdr

So the static path *can* see it -- `code_tables()` finds both the table base
entry and the failing target -- but **`setup.py` invokes static_seeds.py without
`--tables`**, and the flag's own help says why:

    --tables   OFF by default: the first run of this scan added 3,460 functions
               at once and the title segfaulted in its renderer. A table word is
               often a switch LABEL inside a function already found, not a new
               entry, so lifting it as one splits a function.

That is the correct diagnosis of the *seed* form of the fix: a jump-table word is
a case label, and seeding it as a function entry splits the function. The static
path was disabled for a good reason and cannot be re-enabled as-is.

Also measured: the descriptor scan (38522 seeds) and the code-address scan
(0 relocations; 1492 lis/addi pairs with --code-addresses) both miss it, because
0xae5818 is named by neither: it is not an OPD entry (no TOC word beside it) and
no `lis/addi` pair in the code builds it.

### M4 -- discovery path 2 (the internal dispatcher): BOTH detectors return None, for two different bugs

    \$ python3 - <<'EOF'
    import analysis
    img = analysis.Image('bin/rdr/EBOOT.elf', profile=analysis.profile_for('rdr'))
    print(analysis.jump_table(img, 0xae5178))                 # None
    print(analysis.table_dispatch_bctr(img, 0xae5178, 0xae50f0, 0xae90f0))  # None
    f = analysis.explore(img, 0xae50f0, {0xae50f0})
    print(len(f.insns), hex(max(f.insns)), f.jump_tables, f.indirect_jumps)
    EOF
    35 0xae5178 {} ['0xae5178']

The function reaches 35 instructions and stops at the bcctr with an empty
`jump_tables` and 0xae5178 in `indirect_jumps`.

**jump_table() -- rejected because there is no bound compare and no default
branch in the window.** Its own code path:

    positional = base == bctr + 4          # 0x013e5c8c != 0xae517c -> False
    ...
    elif default_off is None:
        return None

The base IS recovered correctly (addis/addic at 0xae5120/0xae5138), the index IS
scaled by 4 (rlwinm at 0xae5168) and the load IS through base+index (lwzx at
0xae516c). The only missing ingredient is a `cmpwi`+`bgt` pair: this dispatcher
does not bound-check its opcode, because the *caller* only calls it for bytes it
has already validated. A table whose extent is decided by its own contiguous run
of code words is rejected for the lack of a bound it never had.

**table_dispatch_bctr() -- finds the register, then reconstructs the base from
the WRONG definition.** Traced step by step:

    ctr_reg  = 4    (mtspr ctr, r4 at 0xae5174)
    base_reg = 24   (lwzx r4, r24, r4 at 0xae516c; _register_is_table_base -> True)
    setter   = 0xae79f4   <- the LAST write to r24 anywhere in [entry, entry+0x4000)
    base     = 0x01440958 <- the last lis/addi pair before 0xae79f4
    0x01440958 is not in text, so the entry scan yields ZERO targets and the
    detector returns None.

The true definition is 861 instructions *above* the bctr and 0x2a00 bytes below
the last writer:

    definitions of r24 before 0xae5178:  0x00ae5120 addis, 0x00ae5138 addic
    true base = 0x013e5c8c, 17 entries, contains 0xae5818 at index 13

The function scans the whole function body for the last setter and then scans
from the entry to that setter, so any later, unrelated write to the same
register (here a different basic block at 0xae79f4) moves the reconstructed base
off the table. The rule "the base is set once and never rewritten" is right; the
implementation of "which setter" is wrong -- it must be the definition that
dominates the bctr, i.e. the nearest one *before* it.

### M5 -- discovery path 3 (dynamic): never reached, and it is the safety net, not the rule

The run that produced the stop (P3, main HEAD) reached it at 48 s only after 724
fibres and 10 407 switches, i.e. after gameplay. `PS3BASE_DISCOVERY` records the
address and exits 3, and `tools/discover_loop.sh` feeds it back -- but a seed is
a function ENTRY, so seeding 0xae5818 would lift one case body as a function and
leave the other 16 cases still falling through `call_indirect`. The dynamic path
is what proves convergence; it is not the fix.


