# The polled table has writers, and they are identified

## The measurement

A line watch on `0xCE5AA0` (the polled address) with the guest context installed
so the writer is named:

    line-write 0x00ce5aa0+1 prev 0x00000000 -> 0x00000000 (guest lr 0x0021d370) thread main_thread
    line-write 0x00ce5a8c+4 prev 0x00000000 -> 0x00000000 (guest lr 0x0021d328) thread main_thread
    ... 35 writes total, some non-zero: -> 0x00000001, -> 0xffffffff, -> 0x95005800

**Something does write the table.** That rules out "no writer exists" — the shape the
`0xD6E800` investigation had — and narrows the question to "what does the guest wait
for that never arrives".

The writers are named by `lr`:

| `lr` | function | what it does |
|---|---|---|
| `0x21d328` | `f_0021d304` | writes zeros at `0xCE0000+23176+4/8/12` — a reset |
| `0x21d370` | `f_0021d350` | calls import 159 after adjusting `0xBE0000-17160` |

## My arithmetic error, again, and the correction

Last round I reported the polled address as `0xCE0E20`. **Wrong.** The lift is

    gpr[4] = 13500416;          /* 0xCE0000, not 0xCE0E00 */
    gpr[4] = gpr[4] + 23200;    /* 0x5AA0, so 0xCE5AA0 */

I had also mis-read `13500416` as `0xCE0E00` when it is `0xCE0000`. Watched on the
correct line, the writes appear; on the wrong one they never did. **A wrong constant
made a working subsystem look absent**, which is exactly the failure mode this
campaign keeps producing and the reason the watch is worth re-running whenever an
address is derived by arithmetic.

## What the two observations together say

- The table **is** written, 35 times, some entries non-zero.
- The guest still polls it at 4 Hz and never proceeds.

So the polled byte is *not* the one being written, or the write happens before the
poll begins and the byte it checks stays zero. The next measurement is to print the
byte the poll actually reads (`gpr[3]` at `f_0021da50`'s entry is the index) and
compare it against what the writers above put in the table.

## Corrections carried forward

Two rounds, two arithmetic mistakes on addresses derived from decimal literals in the
lifted code (`0xD6E800` was fine, `0xCE0E20` was not). The lesson is mechanical:
**compute the address with a tool, never in the head**, and re-derive it whenever the
observation contradicts the expectation. The watch tooling is sound; my arithmetic
was not.
