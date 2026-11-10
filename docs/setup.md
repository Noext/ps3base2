# Setup

ps3base v2 compiles nothing by itself. The runtime is generic; the code it runs
is generated from **your own** copy of a disc and of the firmware. None of that
material is tracked by git.

## What you supply

| Path | What it is |
|---|---|
| `bin/<title>/` | the decrypted executable of your disc, as a plain ELF |
| `fw/<version>/` | the decrypted `.prx` modules of your own firmware dump |
| `keys/` | the key material the decryption tools need (see below) |
| `ref/nid_db.json` | the NID database, regenerable from RPCS3 sources |

`bin/`, `fw/` and `keys/` are in `.gitignore` and the pre-commit hook refuses
binaries, so none of this can be committed by accident.

## Getting from a disc to a plain ELF

The tools below need `pycryptodome` (`python3 -m pip install pycryptodome`).
Without it, importing `tools/unself.py` fails with
`ModuleNotFoundError: No module named 'Crypto'`, before any file is touched.

**Corrected, measured 2026-11-10:** this section used to claim the dependency was
not installed here. It is: `python3 -c 'import Crypto; print(Crypto.__version__)'`
prints `4.4.0` on `/usr/bin/python3`. The claim was true of a different
interpreter, not of this machine.

The tools exist and are tracked; what is untracked is their **input** (the disc
image, the update package) and their **output** (`bin/`, `keys/`). This section
was previously wrong: it said nothing here decrypts anything, which was not true.

### 1. Keys

Two files under `keys/`, both produced from sources you supply:

| File | Produced by | Contains |
|---|---|---|
| `keys/keys.json` | you | named hex keys: `NP_KLIC_KEY`, `NP_KLIC_FREE`, `NP_OMAC_KEY_2`, `D1_KEY`, `D1_IV` |
| `keys/self_keys.json` | `python3 tools/extract_keys.py <rpcs3 source> keys/self_keys.json` | the SELF key table: `{table, ver_start, ver_end, revision, type, erk, riv}` |

`extract_keys.py` parses the `sk_<table>_arr.emplace_back(...)` initialisers out
of RPCS3's `rpcs3/Crypto/key_vault.cpp` and writes them as JSON.
`tools/keystore.py` is the only reader and fails loudly with
`missing key <name> in keys/keys.json` rather than returning zeros.

### 2. Decrypt the executable

```bash
python3 tools/decrypt_all.py bin
```

For every `bin/<version>/` it:

1. reads `EBOOT.BIN`, builds `tools.unself.Self` and writes `EBOOT.elf`;
2. for every other SELF in that directory (`GOWA.SELF`, and any update payload),
   finds the licence key and writes the matching `.elf`;
3. writes `bin/ELF_SHA256SUMS` listing the hash, size and KLIC of every output.

### How the decryption actually works

`tools/unself.py` parses the SELF container:

- header: `key_revision`, `header_type`, metadata offset, `appinfo_off`,
  `elf_off`, `phdr_off`, `shdr_off` from the extended header at `0x20`;
- app info: `auth_id`, `vendor_id`, `self_type`, `app_version`;
- metadata block at `meta_off + 0x20`, decrypted **twice** for NPDRM: first the
  NPD header with `AES-ECB(NP_KLIC_KEY)` then `AES-CBC(zero IV)` on the licence;
  and always with the table key `AES-CBC(erk, riv)` selected by
  `(self_type, key_revision, app_version)`;
- the section/key tables then come from `AES-CTR(info[0:16], info[32:48])` over
  the rest of the header;
- each section of type 2 is copied, decrypted with `AES-CTR(key, iv)` when
  `enc == 3`, and inflated with zlib when `comp == 2`, into an ELF built from
  the original program headers.

The **KLIC** is the interesting part and is not stored anywhere:

`tools/find_klic.py` takes the target SELF's NPD header and scans candidate
16-byte windows, computing `CMAC-AES(KLIC XOR NP_OMAC_KEY_2, npd[:0x60])` and
comparing it against `npd[0x60:0x70]`. The candidate that matches *is* the key.
`decrypt_all.py` searches those candidates **inside the already-decrypted
EBOOT**, which is why the EBOOT must be decrypted first: the licence for the
other payloads lives in it. A SELF with no NPDRM entry uses `NP_KLIC_FREE`.

### 3. Packages and firmware

| Tool | Use |
|---|---|
| `tools/pkg.py <file.pkg> [outdir]` | extract a package (the 01.12 update, firmware updates) |
| `tools/ird.py` | read the disc IRD metadata |
| `tools/psarc.py`, `tools/install_updates.py` | archive extraction and update installation helpers |

`fw/<version>/` then holds the decrypted `.prx` modules, which
`tools/prx_relocate.py` places at a fixed base for lifting.

## Sequence, end to end

```bash
# 1. keys, once
python3 tools/extract_keys.py <rpcs3>/rpcs3/Crypto/key_vault.cpp keys/self_keys.json

# 2. disc executable -> plain ELF
python3 tools/decrypt_all.py bin

# 3. NID database, from RPCS3
python3 tools/nid_db.py <rpcs3>/rpcs3/Emu/Cell ref/nid_db.json

# 4. lift the game
python3 tools/setup.py --steps ppu --elf bin/<title>/EBOOT.elf
python3 tools/setup.py --steps spu --elf bin/<title>/EBOOT.elf

# 5. build
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build -j 8
```

`setup.py` writes `out/ppu/`, `out/spu/` and `out/gen/`. If they are absent,
`cmake` stops immediately and names the missing step rather than failing at
link time.

## Firmware modules are lifted, not emulated

`tools/prx_relocate.py` places each firmware module at a fixed base and
`recomp/ppu/codegen.py` lifts it like any other guest image. The LV2 syscall
surface is the exception: it is reimplemented in `runtime/src/lv2/`, generated
from the LV2 specification so that every number is accounted for.
