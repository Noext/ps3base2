# References

Where a fact comes from, so it does not have to be rediscovered. Read the
matching entry before grepping; each one names the file that answers the
question.

## NID derivation

A PS3 library NID for an exported function is the first four bytes, big-endian,
of `SHA-1(name + 16-byte suffix)`. The suffix is the library's own tag, which
is why the same function name yields different NIDs in different libraries.

**Our NID table is correct.** `out/ppu/table.cpp` is generated from the
import table inside the title's own ELF, so every `{library, nid, name}` row is
the NID that this title actually imports — it is the source of truth, not a
value computed from the name. Verified behaviourally: every `cellPngDec*`
handler is reached with the arguments the title means to pass, which cannot
happen if a NID were bound to the wrong function.

Two consequences worth remembering:

- A NID cannot be derived from a function name alone. Recomputing
  `SHA-1("cellPngDecSetParameter")` gives `0x6217FBA7`, not the
  `0xE97C9BD4` the title imports, because the suffix differs. Do not "fix" a
  table row by recomputing it.
- The name column is a guess from RPCS3's `REG_FUNC` list. If a handler
  receives arguments that make no sense for its supposed signature, suspect the
  **name**, not the NID: the binding is exact and the label may be wrong.

## RSX

- Texture unit enable is bit 31 of `NV4097_SET_TEXTURE_CONTROL0`
  (`0x1A0C + unit*0x20`), not register `0x1D00`.
- `CELL_GCM_TEXTURE_B8` = `0x81`, `A8R8G8B8` = `0x85`, `LN` = `0x20`
  (linear). See `refs/rpcs3/rpcs3/Emu/RSX/gcm_enums.h`.
- Texture location: `(format & 3) - 1`; raw 1 is local, raw 2 is main.
  `refs/rpcs3/rpcs3/Emu/RSX/RSXTexture.cpp`, `fragment_texture::location()`.
- Swizzle (Morton order) for 2D surfaces:
  `refs/rpcs3/rpcs3/Emu/RSX/Utils/image_utils.hpp`,
  `convert_linear_swizzle`. x occupies the even bit positions, y the odd ones.
- Component remap from `TEXTURE_CONTROL1`:
  `refs/rpcs3/rpcs3/Emu/RSX/Utils/color_utils.hpp`,
  `decode_remap_encoding`. Low byte selects the source channel per output
  (A, R, G, B); high byte is the mode (0 zero, 1 one, 2 texture).

## cellPngDec / cellJpgDec

- `refs/rpcs3/rpcs3/Emu/Cell/Modules/cellPngDec.h` for the structure layouts.
- The title asks for **ARGB** (`outputColorSpace` 20) at 4 bytes per pixel for
  its UI atlases. Reading the wrong field offset made the space indeterminate
  and the glyph atlas reached the GPU as noise — the whole "text is unreadable"
  defect.
- `cellPngDecOpen` writes a per-image **sub-handle** into `*stream`, and that
  sub-handle is passed **by value** to ReadHeader/SetParameter/DecodeData/Close.
  Dereferencing it faults at a guest address near zero.

## GTA San Andreas (BLES02219) -- containers

- The four data archives on the disc (`PS3DataMain.obb`, `PS3Data.obb`,
  `PS3DataText.zip`, `PS3DataText2.zip`) are **plain ZIP archives with
  compression method 0 (stored)**. Verified by signature (`504b 0304` at offset
  0) and by `7z l`, which lists 76 files / 708 628 413 bytes for `PS3Data.obb`.
  Do not reach for a bespoke OBB parser: the runtime only has to serve bytes.
- The paired `.idx` is a flat index, not code: `u32 count`, then per entry
  `u32 offset, u32 size, u16 name_len, name`. Offsets are relative to the start
  of the archive's data area. Confirmed: 76 entries parsed for 76 files listed
  by 7z, and the second-to-last `off + size` lands exactly on the last offset.
- Filenames in the archive confirm the PC ancestry: `data/script/mainPS3.scm`
  (Script Compiler Main), `data/handling.cfg`, `Text/PS3/*.gxt`,
  `Textures/fonts/*.met`.

## Red Dead Redemption (BLES00680) -- containers

- `rdr2_layer0.rpf` (7.35 GB) starts with the ASCII magic **`RPF6`** at offset 0.
  This is Rockstar's own archive format, not a PS3 one: RPCS3's tree contains no
  RPF reader at all (`grep -rn 'RPF6' refs/rpcs3/` returns nothing), and it is
  the same container family the PC and Xbox 360 versions use.
- Do not reach for a PS3-side parser. The title reads its own archive through
  its own file code; the runtime's job is to serve the bytes from the VFS, as
  with any other file. A `7z` extraction of the 7.35 GB member is unnecessary
  and slow enough to time out.
- `rdr2_installed.sdat` (539 MB) sits beside it and is installed game data the
  title writes/reads, not something to decode on the host.
- The disc carries its cutscenes as `.bik` (Bink video), two of them here.

## PPU

- `refs/rpcs3/rpcs3/Emu/Cell/PPUInterpreter.cpp` is the reference for
  instruction semantics. Notably `divw` stores a zero-extended 32-bit result
  and `sraw`/`srad` shift arithmetically.
- `tests/ppu_op_fuzz` compares every helper in `ppu/ops.h` against an
  independent reference: 200 million checks, no divergence.
