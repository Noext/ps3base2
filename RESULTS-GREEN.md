# RESULTS-GREEN.md -- why RDR's post-loading screen is solid green

**Measured 2026-11-13, six runs, one game instance at a time, each under
`/var/lock/ps3-machine.lock`.** Binary `build-rdr/ps3base` (695 860 400 bytes,
Nov 13 19:56), HEAD `d77d3a3`. Nothing was compiled and no source file was
modified; every run used pre-existing environment variables only.

## The verdict, in one paragraph

**The scene is drawn, and it is lost at composition.** The title renders a
complete Red Dead Redemption landscape into a local colour surface at guest
offset `0xe20000` (1152x640). The two surfaces the swapchain actually presents
(`0x0` and `0x384000`, both 1280x720) contain a *constant* srgb(0,145,0) --
standard deviation exactly 0, i.e. a flat fill, not a scene that faded. The
title then samples the scene back as a texture, but it asks for a **2304x640**
region -- twice the width of the 1152x640 surface that holds it -- so the
renderer's surface fast path never matches, and the sample falls through to
guest memory. Guest memory is never written for that surface, because
`schedule_writebacks()` only writes surfaces with `main == true` and the scene
surface is `main == false`. The title therefore composes its final image from
stale guest memory. Presenting `0xe20000` directly (`GOWA_SHOW_SURFACE`) puts
the full game on screen, which proves the drawing half.

---

## 1. The render-target table

The renderer creates a `Surface` only from `color_targets()` /
`depth_target()` (`runtime/src/gpu/rsx_renderer.cpp:1208`, `:1226`), so every
line the renderer logs as `new color surface` **is** a render target. The pass
structure holds at most four colour attachments plus one depth
(`Pass::colors[4]`, `rsx_renderer.cpp:253`); in this run one colour and one
depth were bound at a time.

Command:

```
$ grep -c 'new color surface' /tmp/green/run4/drv/run.log
261
$ grep -c 'new depth surface' /tmp/green/run4/drv/run.log
9
$ grep 'new color surface' /tmp/green/run4/drv/run.log | sed -E 's/.*format ([0-9]+).*/\1/' | sort | uniq -c
      6 100
    142 37
    112 8
      1 9
```

Over the whole 323 s run 261 colour surfaces and 9 depth surfaces were created
(many are mip-sized copies of one another). The ones that matter for the green
frame are these:

| offset (guest local) | format | size | pitch | role | dump mean / sd |
|---|---|---|---|---|---|
| `0x00000000` | 37 | 1280x720 | 0x1400 | **display buffer A** (swapchain source) | 0.189542 / **0** |
| `0x00384000` | 37 | 1280x720 | 0x1400 | **display buffer B** | 0.189542 / **0** |
| `0x00e20000` | 37 | 1152x640 | 0x2800 | **the scene** | 0.4976 / 0.2221 |
| `0x021b0000` | 37 | 1152x640 | 0x2800 | sampled 2304x640, green | 0.189542 / **0** |
| `0x01b70000` | 37 | 1152x640 | 0x2800 | sampled 2304x640, black | 0 / 0 |
| `0x03bb0000` | 37 | 1152x640 | 0x1400 | sampled 1152x640 (correct) | 0 / 0 |
| `0x00000000` (depth) | 129 | 1280x720 | - | depth for display buffer | - |
| `0x00710000` (depth) | 129 | 1280x720 | - | depth | - |

Format 37 is `0x25`: `color_format(0x25 & 0x1F = 0x5)` hits the `default`
arm of the switch at `rsx_renderer.cpp:932` and becomes
`VK_FORMAT_B8G8R8A8_UNORM`. Format 129 is `0x81`, depth: `depth_format`
(`:946`) returns `VK_FORMAT_D24_UNORM_S8_UINT` because
`((0x81 >> 5) & 7) == 0`, not 1.

**How many are simultaneously bound: one colour + one depth.** The pass
structure allows four colour attachments, but this title never used more than
one at a time on this screen.

## 2. Draws that sample a rendered surface as a texture

The renderer has two ways to satisfy a texture bind whose guest address matches
a render target, and it names which one it used in `g_bind_kind`
(`rsx_renderer.cpp:2131`, `:2150`, `:2200`). The trace records it per bind.

Command (decodes `GOWA_EVTRACE=tex`, 1 541 962 `T_TEX` records, 56-byte
`Rec` as declared at `runtime/src/trace.cpp:28`):

```
$ python3 - <<'PY'
import struct, collections
f=open('/tmp/green/run4/trace.bin','rb'); f.read(16); size=56
data=f.read(); n=len(data)//size
LOCAL=0xC0000000
kinds=['dummy','memory','depth redirect','feedback copy','surface region','depth surface','color surface']
surfaces={0x0:(1280,720),0x384000:(1280,720),0x21b0000:(1152,640),
          0xe20000:(1152,640),0x1b70000:(1152,640),0x3bb0000:(1152,640)}
out=collections.defaultdict(collections.Counter)
for i in range(n):
    o=i*size
    typ,=struct.unpack_from('<H',data,o+8)
    if typ!=11: continue
    a,=struct.unpack_from('<Q',data,o+16)
    off=a-LOCAL
    if off not in surfaces: continue
    x,=struct.unpack_from('<I',data,o+12)
    c,=struct.unpack_from('<Q',data,o+32)
    out[off][(kinds[(x>>8)&0xff], c>>16, c&0xffff)]+=1
for off in sorted(out):
    sw,sh=surfaces[off]
    print('surface 0x%08x is %dx%d:'%(off,sw,sh))
    for (k,w,h),v in out[off].most_common(4):
        note=' <-- DOES NOT FIT' if (w>sw or h>sh) else ''
        print('   %-14s rect %dx%d : %d%s'%(k,w,h,v,note))
PY
surface 0x00000000 is 1280x720:
   surface region rect 1152x640 : 7825
surface 0x00384000 is 1280x720:
   surface region rect 1152x640 : 7829
surface 0x00e20000 is 1152x640:
   memory         rect 2304x640 : 2890 <-- DOES NOT FIT
surface 0x01b70000 is 1152x640:
   memory         rect 2304x640 : 136387 <-- DOES NOT FIT
   color surface  rect 1152x640 : 485
surface 0x021b0000 is 1152x640:
   memory         rect 2304x640 : 126212 <-- DOES NOT FIT
   color surface  rect 1152x640 : 243
surface 0x03bb0000 is 1152x640:
   color surface  rect 1152x640 : 2102
   memory         rect 1152x640 : 12
```

**Yes, the title samples surfaces it just rendered -- and it does so by the
guest-memory path, not the surface path.** The mechanism is one line,
`rsx_renderer.cpp:2111`:

```cpp
if (s->offset != t.offset || s->width < w || s->height < h) continue;
```

The title asks for a texture of **2304x640** at offset `0x21b0000`. The
`Surface` there is **1152x640**, so `s->width (1152) < w (2304)` and the loop
matches nothing. `exact`, `larger` and `redirect` all stay null, the fast path
at `:2121` is skipped, and control reaches `:2170`:

```cpp
uint32_t ea = lv2::rsx_address(t.offset, loc);
Texture* tex = memory_texture(t, ea);   // <- decodes GUEST MEMORY
```

2304 = 2 x 1152 exactly: the title is sampling **two 1152-wide surfaces side by
side as one texture** (a double-buffered / split-screen layout). The renderer
has no surface that wide -- `grep -c '2304x' /tmp/green/run4/drv/run.log`
returns **0**.

The display buffers, by contrast, *are* matched correctly: they are 1280x720
and the request is 1152x640, which fits, so they take the `surface region` path
and are copied from the live image.

## 3. Unhandled formats

**None.** The per-format decode census (`PS3BASE_TEX_LOG=1`) lists every format
the title decoded, and every one is in the table at
`runtime/src/gpu/texture_decode.cpp:33`:

```
$ grep 'texstat' /tmp/green/run4/drv/run.log | sed -E 's/.*fmt (0x[0-9a-f]+) ([a-z]+) ([0-9]+x[0-9]+).*/\1 \2 \3/' | sort -u
0x81 swizzled 512x64
0x85 swizzled 256x256
0x85 swizzled 4x4
0x86 swizzled 1024x512
0x86 swizzled 32x32
0x86 swizzled 64x64
0x88 swizzled 32x32
0xa1 linear 1280x720
0xa5 linear 640x256
$ grep -c 'UNSUPPORTED' /tmp/green/run4/drv/run.log
0
```

`0xa1` and `0xa5` look absent from the table but are not: `find_format`
(`texture_decode.cpp:89`) masks `fmt & ~0x60`, and `0xa1 & ~0x60 = 0x81`,
`0xa5 & ~0x60 = 0x85`. Both are present. The `UNSUPPORTED format` warning at
`texture_decode.cpp:371` never fired.

The one format that *does* fall through is the **render-target** format 37
(`0x25`), which hits `default: return VK_FORMAT_B8G8R8A8_UNORM` at
`rsx_renderer.cpp:942`. That is not a failure -- B8G8R8A8 is the correct XRGB8
target -- but it is worth naming because it is a silent default rather than an
explicit case.

## 4. The final compose draw

Run 6 turned the per-draw log on for the green phase (`GOWA_TRACE_AT=300`)
and logged 4313 draws before reproducing the crash described under *Two
defects found in the code while measuring*. Those records
name the colour target and the fragment-program texture mask directly.

**Every one of the last 20 draws targets the scene, not the display buffer:**

```
$ grep 'draw #' /tmp/green/run6/run.log | tail -20 | grep -o 'c0 0x[0-9a-f]*' | sort | uniq -c
     20 c0 0xe20000
```

The last draw of the frame:

```
$ grep 'draw #' /tmp/green/run6/run.log | tail -1
I 303.778191 rsx: draw #2108 prim 8 idx 1680 verts 1120 [0..1119] targets 1
  c0 0xe20000 fmt 16 depth 0x710000 1152x640 scissor 0,0-1152,640
  vs 576.000 -320.000 1.000 off 576.000 320.000 0.000
  fptex 0x8001 inline 0 blend 0xf mask 0xf omask 0x3c000
```

**The compose is a separate, much smaller set of draws that write the display
buffer.** Over the whole logged window only 22 draws target a display buffer at
all, against 1874 that target the scene:

```
$ grep -o 'c0 0x[0-9a-f]*' /tmp/green/run6/run.log | sort | uniq -c | sort -rn
  10088 c0 0x80060000
   3112 c0 0x80060020
   1874 c0 0xe20000      <- the scene
   1039 c0 0x0           <- display buffer A (mostly targets 0, see below)
    895 c0 0x1b70000
    338 c0 0x3ac0000
     21 c0 0x384000      <- display buffer B
      7 c0 0x21b0000
$ grep -c 'targets 1 c0 0x0 '     /tmp/green/run6/run.log
1
$ grep -c 'targets 1 c0 0x384000 ' /tmp/green/run6/run.log
21
```

(`targets 0` means the draw had a depth target only; those 1038 records on
`0x0` are depth-only passes.)

The compose draw that writes display buffer B at full size:

```
$ grep 'draw #2144' /tmp/green/run6/run.log
I 301.269549 rsx: draw #2144 prim 8 idx 6 verts 4 [0..3] targets 1
  c0 0x384000 fmt 16 depth 0x0 1280x720 scissor 0,0-1280,720
  vs 640.000 -360.000 1.000 off 640.000 360.000 0.000
  fptex 0x4 inline 1 blend 0x0 mask 0xf omask 0x4000
```

**Inputs:** `fptex 0x4` is a fragment-program texture mask with bit 2 set, so
this draw samples texture unit 2. From the run-4 bind trace, the units in this
compose draw bind the scene and its neighbours:

```
  1541056 unit=15 kind=4 ea=0xc0384000 fmt=0xa5 rect=1152x640   <- display buffer B
  1541058 unit= 4 kind=1 ea=0xc0e20000 fmt=0xa5 rect=2304x640   <- THE SCENE, 2304 wide
  1541059 unit= 4 kind=6 ea=0xc1b70000 fmt=0xa5 rect=1152x640
  1541060 unit= 6 kind=4 ea=0xc0384000 fmt=0xa5 rect=1152x640   <- display buffer B
  1541090 unit= 2 kind=6 ea=0xc21b0000 fmt=0xa5 rect=1152x640   <- the GREEN surface
  1541096 unit=11 kind=0 ea=0xc21b0000 fmt=0xa5 rect=2304x640   <- GREEN, 2304 wide
  1541097 unit=13 kind=0 ea=0xc1b70000 fmt=0xa5 rect=2304x640
```

`kind` is the `g_bind_kind` index: 0 = dummy, 1 = memory, 4 = surface region,
6 = color surface.

**What it writes:** the display-buffer surfaces `0x0` / `0x384000` (bound at
unit 15 / unit 6 as `surface region`). **What it reads:** the scene surface
`0xc0e20000` (1152x640, sampled as a 2304x640 *memory* texture), the green
surface `0xc21b0000` (same), a black surface `0xc1b70000` (same), plus smaller
colour surfaces and BC1/BC3 textures. The one input that carries the picture is
read through guest memory, and guest memory holds nothing for it.

The shader is a fragment program the renderer compiled from the guest's ucode.
The renderer reports `fragment program 0b9b9cb47b39b711 at 0xc1460000 (17 slots,
5 literals): ok` at t = 18.54 s, and no `fp_fail` dump was written, so every
fragment program on this screen compiled. **The shader is not the problem; its
input is.**

## 5. The PNG dumps

All surfaces were dumped with `PS3BASE_DUMP_FRAMES=20` (which calls
`schedule_dumps()`, `rsx_renderer.cpp:3776`) and the presented image with
`GOWA_SNAP_EVERY=10` (`schedule_snapshot`, `:3799`). 156 surface PPMs, 32 snapshot PPMs and 284 presented-buffer PPMs were
produced; the ones that answer the question are in
`docs/findings/green/`:

| file | what is in it |
|---|---|
| [scene-0xe20000.png](docs/findings/green/scene-0xe20000.png) | **The RDR riverboat scene, fully drawn** -- sky, mountains, water, the paddle steamer. mean 0.4976, sd 0.2221. This is the picture the title made. |
| [dispbufA-0x0-green.png](docs/findings/green/dispbufA-0x0-green.png) | **Flat srgb(0,145,0), sd exactly 0.** The surface the swapchain presents. |
| [surface-0x21b0000-green.png](docs/findings/green/surface-0x21b0000-green.png) | Flat green, sd 0. The 1152x640 surface the title samples as 2304x640. |
| [surface-0x1b70000-black.png](docs/findings/green/surface-0x1b70000-black.png) | Flat black, sd 0. The other half of the 2304-wide sample. |
| [surface-0x3ac0000.png](docs/findings/green/surface-0x3ac0000.png) | A 320x256 colour surface with real content (mean 87.9/93.4/95.1) -- one of the many intermediate targets that do get drawn. |
| [chapter-card.png](docs/findings/green/chapter-card.png) | The "EXODUS IN AMERICA" card, from `snap_00260.0.ppm`, before the fade. |
| [green-screen.png](docs/findings/green/green-screen.png) | The screen the user sees: srgb(0,145,0), 88.95% of the frame. |
| [scene-presented-GOWA_SHOW_SURFACE.png](docs/findings/green/scene-presented-GOWA_SHOW_SURFACE.png) | **The decisive image.** Run 5 with `GOWA_SHOW_SURFACE=0xe20000`: the renderer is told to present the scene surface instead of the display buffer, and the full game appears -- a desert landscape at sunset. mean 0.328958, sd 0.205611, `is_green` 0.0000. |

**Reading:** the scene is drawn somewhere and lost at composition. Nothing is
missing from the drawing; the composition reads the wrong memory.

## 6. The "texture from a rendered surface" path, and writeback

Two separate mechanisms, and the title needs both:

**(a) Sampling a live surface.** `bind_texture_impl` (`rsx_renderer.cpp:2092`)
looks up `surfaces_at(t.offset)` and, if a surface is big enough, copies it into
a temp image (`copy_to_temp`, `:1949`) and samples that. This path works -- it
is what the display buffers use. It does **not** fire for the scene surfaces,
because of the 2304x640 request described in section 2.

**(b) Writeback of a rendered surface into guest memory.** The function is
`schedule_writebacks()` (`rsx_renderer.cpp:3926`), called once per present at
`:4445`; the copy is completed by `complete_writebacks()` (`:3950`) from
`frame_complete_body()` (`:4133`). **It runs every frame -- but it only writes
surfaces whose `dirty` flag is set, and `dirty` is set in exactly two places,
both gated on `main`:**

```
$ sed -n '1309p;2825p' runtime/src/gpu/rsx_renderer.cpp
            if (colors[i]->main) colors[i]->dirty = true;
        if (s->main) s->dirty = true;
```

`main` is true only when the DMA context is `CELL_GCM_LOCATION_LOCAL + 1`
(`rsx_renderer.cpp:1219`). On this screen the title created exactly **two** main
surfaces, and neither is a scene surface:

```
$ grep 'new color surface at main' /tmp/green/run4/drv/run.log
I  76.054097 rsx: new color surface at main 0x16cf100 1x1 format 100 pitch 0x100
I  76.174137 rsx: new color surface at main 0x16c7080 128x128 format 9 pitch 0x80
```

**So the answer to question 6 is: the renderer does write rendered surfaces back
to guest memory, but only for `main` surfaces, and the surfaces this title
samples through guest memory are local. The scene surface `0xe20000` is never
written back.** That is the equivalent of RPCS3's "Write Color Buffers" being
off for exactly the surfaces this game needs it on.

The consequence is visible in the data: the `0xa1` 1280x720 textures decoded
from guest memory converge to a constant (sd falls 0.0112 -> 0.0005 -> **0**),
which is the signature of reading memory nobody is updating.

```
$ for f in $(ls tex_*_fa1_1280x720.pgm | tail -3); do convert "$f" -format '%[fx:mean] %[fx:standard_deviation]' info:; echo; done
0.0655212 0.00579512
0.0633995 0.00176345
0.0627451 0
```

## What a fix would need (stated, not done -- this task measures)

Two independent changes, either of which would change this screen:

1. **Honour a texture request wider than any surface at that offset.** At
   `rsx_renderer.cpp:2111` the test `s->width < w` rejects a 2304-wide request
   against a 1152-wide surface. A fix would either create the surfaces at the
   width the title samples, or stitch two adjacent surfaces into the temp image
   when `w` is a small multiple of `s->width`.
2. **Write back non-main surfaces** (RPCS3's "Write Color Buffers"). The
   machinery exists and runs; the gate is `s->main` at `:1309` and `:2825`.
   Removing that gate would put the scene into guest memory and make path (b)
   read real data.

Which of the two is *the* fix cannot be settled by measurement alone: both are
defects, and the title needs at least one of them corrected. What the
measurements do settle is that the picture exists and the composition is what
throws it away.

## Two defects found in the code while measuring (not fixed)

**1. `GOWA_DRAW_LOG` and `GOWA_TRACE_AT` crash the run.** The block at
`rsx_renderer.cpp:3299` (entered by either variable) ends with this at `:3355`:

```cpp
if (tea && readable(tea, 64)) {
    const uint8_t* tp = vm::ptr(tea + 0x1400 * 360 + 640 * 4);
    LOG_INFO("rsx", "  tex0 ea 0x%08x center ...", tea, tp[0], ...);
}
```

The guard checks 64 readable bytes at `tea`; the dereference is at
`tea + 0x1c2a00`. Measured, run 2:

```
E   4.375206 crash: signal 11 at 0x5a0deaf8f177 (exe+0xb62177) fault 0x7fb20ba9b600 (guest 0xcfa9b600) thread RSX FIFO
```

Video memory ends at `0xc0000000 + 0xf900000 = 0xcf900000`; the fault is
`0xcfa9b600`, past the end. This is why runs 4 and 5 do not set either
variable, and why the per-draw target list in section 4 comes from the bind
trace rather than from the draw log.

**2. The green surfaces have standard deviation exactly 0.** A fade would leave
a gradient; `sd = 0` means the surface was filled with one value and never
drawn into. That is consistent with the composition reading stale memory, and
it rules out "the scene faded to green".

---

## The runs

| run | dir | what it was | outcome |
|---|---|---|---|
| 1 | `/tmp/green/recon1` | recon, sampling loop bounded by a 600-sample count | killed the driver at 69 s, during loading (the title needs ~110 s). Discarded except for one useful capture: the title screen shows a large bright-green band where art belongs. |
| 2 | `/tmp/green/run2` | first instrumented | crashed at 4.4 s in the draw-log OOB read (defect 1 above). Discarded. |
| 3 | `/tmp/green/run3` | instrumented, after a bad `sed` edit broke a line continuation | `GOWA_ROOT` was not exported, `loader: cannot read ./bin/rdr/EBOOT.elf`. Discarded. |
| 4 | `/tmp/green/run4` | **the main run**: surface dumps + texture census + bind trace, wall-clock bounded | reached green at 276 s, held 45 s. Green captured at 88.95%, srgb(0,145,0). 156 surface dumps, 1 541 962 bind records. |
| 5 | `/tmp/green/run5` | **the decisive run**: `GOWA_SHOW_SURFACE=0xe20000` + texture dumps | presents the scene surface; the full game appears. 300 texture dumps. |
| 6 | `/tmp/green/run6` | per-draw log for the green phase (`GOWA_TRACE_AT=300`) | 4313 draws logged, then the draw-log OOB read faulted at 303.8 s -- which is itself the reproduction of defect 1. The logged draws are the evidence in section 4. |

Load at run 4 start `4.47 7.94 8.22`, at end `11.36 11.21 9.67`
(`cut -d' ' -f1-3 /proc/loadavg`). Run 5 start `1.85 4.90 7.31`.

### The harness defect that cost the first run

`tools/green_screen_probe.sh` samples with a count bound (`i -ge 600`), and each
sample is an `import` call. At ~0.1 s per sample that is 69 s of wall clock --
but the title does not reach the chapter card until ~167 s. The probe therefore
kills the driver during loading and reports "NO GUEST ALIVE". My copy
(`/tmp/green/probe2.sh`) bounds the loop by wall clock instead. The fixed probe
is otherwise used as given: the driver is the only launcher, and it is killed
with SIGKILL so its `finally` cannot kill the guest.
