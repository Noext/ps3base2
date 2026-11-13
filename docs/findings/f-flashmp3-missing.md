# The zero effective address is a missing flashMP3.pic

Measured chain, 2026-11-13.

`__gafMP3` (guest `0x009207d4`) opens `/dev_flash/sys/external/flashMP3.pic`. With
no `dev_flash` on the host the open returns `ENOENT`, the title prints

    Multistream: mp3 failed to load MP3 codec (are you using the correct flash?)

and the codec buffer address at guest `0x0177cc64` is never written. The SPU
kernel then builds a DMA whose effective address is that zero and the runtime
refuses it -- correctly -- so SPURS aborts about 20 s later.

    W 2.720540 fs: open '/dev_flash/sys/external/flashMP3.pic' -> ENOENT
      (host '/root/.config/rpcs3/dev_flash/sys/external/flashMP3.pic')

A/B, same binary, back to back:

| arm | flash | flips | refusals | ENOENT | refusal pc |
|---|---|---|---|---|---|
| A | absent | 721 | 0 | 1 | 0x06428 |
| B | stub | 721 | 1 | 0 | 0x06510 |

Arm B moves the refusal to the **next descriptor element**: filling `+0x64` makes
the kernel advance to `+0x70`. So the missing file is necessary on the chain but
not sufficient, and a stand-in is not a fix. The next link is the element at
`pc 0x06510`.

Two facts that made this hard to see, both measured by sub-agent N1b:

- the refused field is `+0x64`, not `+0x60`: `rotqbyi rt,ra,i7` selects the
  quadword base plus `i7`, confirmed three ways against that run's own channel
  history and against RPCS3 SPUInterpreter.cpp:972-978;
- the branch at `0x6900` (`brz r104`) is **not** the divergence: r104 is
  kernel-local, set to 0 by `fsmbi r104,0` on entry, so the console takes the
  same branch on its first descriptor element.
