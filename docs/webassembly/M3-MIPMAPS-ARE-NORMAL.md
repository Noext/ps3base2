# The MipMapLevel2 stats are normal: the title generates them

## The measurement

A counter on `sys_fs_stat` and a probe on the failing paths show what the loop is asking
for:

    stat '.../res/1_2_2/misc/waterMipMapLevel2.png' -> ENOENT
    stat '.../res/1_2_2/mob/sheep_furMipMapLevel2.png' -> ENOENT
    stat '.../res/1_2_2/mob/spider_eyesMipMapLevel2.png' -> ENOENT
    stat '.../res/1_2_2/mob/chickenMipMapLevel2.png' -> ENOENT

Every one is a **`*MipMapLevel2.png`** name.

And the disc holds:

| | count |
|---|---|
| base `.png` textures | **243** |
| `*MipMapLevel*.png` files | **2** |

**The title asks for 243 derived names that do not exist, and that is correct.** Minecraft
PS3 stores one base texture and generates its mip levels at load time; the
`MipMapLevel2` stat is how it checks whether a pre-generated version was shipped, and
ENOENT is the expected answer. The native run does exactly the same (one
`MipMapLevel2` reference in its log, same failure).

## What that rules out

The `libfs` stall is **not** a missing asset and **not** a broken mount. The stat loop is
doing its job; the guest gets correct answers.

So the stall at `0x01401ee4` is something else, and the next question is not about the
filesystem at all — it is what the guest *does* after the stat returns ENOENT, and why
that path does not terminate.

## Where the build stands, measured

| | |
|---|---|
| PPU threads | **9** |
| SPUs running | 5, 24,737 blocks compiled |
| PNG decodes | **101** |
| `main_thread` state | **running** (not blocked) |
| `main_thread` top syscall | `802 sys_fs_read` × 5,590 |
| distinct pcs over 10 s | **5** |
| crashes | 0 |
| main menu | **not reached** |

The build reads its assets, decodes its textures, builds nine threads and logs no errors.
It stops at a `sys_fs_stat` call site whose return it handles, which means the wait is
one level above the filesystem.

## Honest position

Milestone 3 remains **open**. This round ruled out the filesystem explanation I proposed
last round — with a count (243 base textures, 2 mipmap files) rather than an argument —
and confirmed the boot is otherwise the healthiest it has ever been.
