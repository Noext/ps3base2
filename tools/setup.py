import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import spu_blocks

GOWA_SHA256 = {
    "1cb4f2533721acde768b7ad0c65ea4b3424f32c774c51daa35f3dac6847a0c18": "GOWA 01.12 patched",
}

# The generators below rewrite specific SPU jobs of ONE title by hand. They are
# not part of a generic setup: a foreign disc has no such jobs, and running them
# against its blocks feeds one game's addresses to another's code -- which is
# exactly how "bad insn 073e4" used to abort the whole SPU stage.
#
# A profile names the generators it owns; everything outside a profile gets the
# AOT lifter alone, which covers whatever the guest actually calls.
GENERATORS_GOWA = (
    ("edge", "edge", ["tests/edge/spu_cgen.py", "73e0,d618", "{gen}/edge/edge_cull_gen.cpp"]),
    ("edge_softbody", "edge", ["tests/edge/sb_cgen.py", "{gen}/edge/edge_softbody_gen.inc"]),
    ("edge_ovl", "edge", ["tests/edge/ovl_cgen.py", "{gen}/edge/edge_ovl_gen.cpp", "{gen}/edge/edge_ovl_gen.h"]),
    ("renjob", "renjob", ["tests/renjob/rj_cgen.py", "@tests/renjob/roots.txt", "{gen}/renjob/renjob_gen.cpp", "{gen}/renjob/renjob_gen.h"]),
    ("renjob_whole", "renjob", ["tests/renjob/rj_cgen.py", "whole", "{gen}/renjob/renjob_whole_gen.cpp", "{gen}/renjob/renjob_whole_gen.h"]),
    ("wjobs", "jwhole", ["tests/jobs/wj_cgen.py", "{gen}/jwhole"]),
    ("scream", "scream", ["tests/scream/sc_cgen.py", "all"]),
    ("scream_job", "scream", ["tests/scream/sc_job_cgen.py"]),
    ("cloth", "cloth", ["tests/cloth_shadows/cs_cgen.py", "cloth", "gen", "@tests/cloth_shadows/roots_cloth.txt", "{gen}/cloth/cloth_gen.cpp", "{gen}/cloth/cloth_gen.h"]),
    ("shadows", "shadows", ["tests/cloth_shadows/cs_cgen.py", "shadows", "gen", "@tests/cloth_shadows/roots_shadows.txt", "{gen}/shadows/shadows_gen.cpp", "{gen}/shadows/shadows_gen.h"]),
    ("meshprep", "meshprep", ["tests/meshprep/mp_cgen.py", "runtime"]),
    ("pm", "pm", ["tests/pm/pm_cgen.py", "{gen}/pm/pm_gen.cpp", "{gen}/pm/pm_gen.h"]),
    ("edgejob", "edgejob", ["tests/edgejob/ej_cgen.py", "{gen}/edgejob/ej_gen.cpp", "{gen}/edgejob/ej_gen.h"]),
    ("meshhier", "meshhier", ["tests/meshhier/mh_cgen.py", "{gen}/meshhier/mh_gen.cpp", "{gen}/meshhier/mh_gen.h"]),
    ("anim", "anim", ["tests/anim/an_cgen.py", "{gen}/anim/an_gen.cpp", "{gen}/anim/an_gen.h"]),
    ("tasks", "tasks", ["tests/tasks/task_cgen.py"]),
    ("mpjob", "mpjob", ["tests/mpjob/mpj_cgen.py", "{gen}/mpjob"]),
    ("jwhole", "jwhole", ["tests/jobsys/whole_cgen.py", "{gen}/jwhole"]),
)

GENERATORS = ()
PROFILE = os.environ.get("PS3BASE_PROFILE", "")
if PROFILE == "gowa":
    GENERATORS = GENERATORS_GOWA

STAMP = "generated.txt"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def log(msg):
    print("[setup] " + msg, flush=True)


def run(argv, env=None, quiet=True):
    r = subprocess.run([sys.executable] + argv, cwd=ROOT, env=env, stdout=subprocess.PIPE if quiet else None, stderr=subprocess.PIPE if quiet else None, text=True)
    if r.returncode:
        if quiet:
            sys.stderr.write(r.stdout[-4000:] + r.stderr[-4000:])
        raise SystemExit("command failed: %s" % " ".join(argv))
    return r


def step_patch(args):
    if os.path.exists(args.elf):
        return
    src = os.path.join(os.path.dirname(args.elf), "GOWA.elf")
    if not os.path.exists(src):
        raise SystemExit("missing %s and %s: decrypt your GOWA.SELF first (tools/decrypt_all.py)" % (args.elf, src))
    log("patching %s" % src)
    run(["tools/apply_patches.py", "recomp/patches.json"], quiet=False)


def step_check_elf(args):
    digest = sha256(args.elf)
    name = GOWA_SHA256.get(digest)
    if name:
        log("ELF %s: %s" % (args.elf, name))
    elif args.allow_unknown:
        log("ELF %s: unknown sha256 %s, continuing" % (args.elf, digest))
    else:
        raise SystemExit("ELF %s has unknown sha256 %s (expected the patched 01.12 GOWA.elf); use --allow-unknown to force" % (args.elf, digest))


def step_reloc(args):
    """Relocate the firmware modules above THIS title.

    The bases are computed from the title's own text extent, not read from a
    fixed file. A fixed 0x01000000 worked only while every title's text ended
    below it; Red Dead Redemption ends at 0x10d546c and swallowed liblv2 whole,
    which showed up as five "multiple definition" link errors and would have
    been worse at run time (lookup() returns the first range containing an
    address, so every liblv2 call would have run the game's code).
    """
    # A reloc directory PER OUTPUT TREE, not one under fw/. Every title's lift
    # records the module bases it was built against, so a shared fw/<ver>/reloc
    # serves the last title lifted and breaks the others at run time: after
    # relocating Red Dead's modules to 0x30000000, Minecraft and GTA both faulted
    # at guest 0x20013bd8 -- the liblv2 OPD address they were built to expect.
    reloc = os.path.join(args.gen_root, "reloc")
    os.makedirs(reloc, exist_ok=True)
    layout = os.path.join(args.gen_root, "lle_layout.json")
    os.makedirs(args.gen_root, exist_ok=True)
    log("computing firmware module bases for %s" % os.path.basename(args.elf))
    run(["tools/layout_for.py", "--elf", args.elf, "--out", layout], quiet=False)
    log("relocating firmware modules into %s" % reloc)
    run(["tools/prx_relocate.py", layout, args.fw, reloc], quiet=False)


def newest_mtime(paths):
    """The newest mtime among the paths that exist, or 0."""
    best = 0.0
    for p in paths:
        try:
            best = max(best, os.path.getmtime(p))
        except OSError:
            pass
    return best


def lift_is_stale(out, gen_root):
    """True when a lift INPUT is newer than the generated code.

    Why this exists: on 2026-11-13 the lift output was newer than the seed file,
    so a timestamp check would NOT have caught that run -- but the same class of
    bug was live: `out-rdr/ppu` was built from a seed set that did not contain a
    function the title calls (0x00b58430, the cellFiber entry), and nothing in
    the build said so. The lift is a pure function of (ELF, seeds, switch tables,
    images.json); if any of those is newer than table.cpp, the generated code is
    describing a different binary than the one on disk.

    The check is deliberately conservative: it can only say "stale, relift", not
    "fresh, skip". A lift is expensive but a stale lift is a wrong measurement.
    """
    generated = os.path.join(out, "table.cpp")
    if not os.path.exists(generated):
        return True
    gen_mtime = os.path.getmtime(generated)
    seeds_dir = os.path.join(gen_root, "seeds")
    inputs = [os.path.join(gen_root, "switch_tables.toml")]
    if os.path.isdir(seeds_dir):
        inputs += [os.path.join(seeds_dir, n) for n in os.listdir(seeds_dir)]
    inputs.append(os.path.join(gen_root, "lle_layout.json"))
    newest = newest_mtime(inputs)
    if newest > gen_mtime:
        return True
    return False


def seed_fingerprint(gen_root):
    """sha256 of every seed file, so a build can say WHICH seeds it used.

    A lift is only reproducible if the inputs are named. The fingerprint goes
    into the log and into a stamp beside the generated code, so a later reader
    can tell whether two lifts that produced different function counts were
    reading the same seeds.
    """
    seeds_dir = os.path.join(gen_root, "seeds")
    h = hashlib.sha256()
    if os.path.isdir(seeds_dir):
        for name in sorted(os.listdir(seeds_dir)):
            p = os.path.join(seeds_dir, name)
            if not os.path.isfile(p):
                continue
            h.update(name.encode())
            h.update(b"\0")
            with open(p, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            h.update(b"\0")
    return h.hexdigest()


def step_ppu(args):
    out = os.path.join(args.out_root, "ppu")
    if not os.path.exists(args.nid_db):
        raise SystemExit("missing NID database %s (tools/nid_db.py)" % args.nid_db)
    # Static seeds first: relocation targets and function descriptors the binary
    # already names. They cost nothing at run time and shrink the dynamic loop to
    # what neither source reaches.
    seeds_dir = os.path.join(args.gen_root, "seeds")
    os.makedirs(seeds_dir, exist_ok=True)
    manifest = json.load(open(args.images))
    root = os.path.dirname(os.path.abspath(args.images))
    for spec in manifest:
        elf = os.path.join(root, spec["elf"])
        if not os.path.exists(elf):
            continue
        dst = os.path.join(seeds_dir, spec["name"] + ".txt")
        r = run(["tools/static_seeds.py", "--elf", elf, "--name", spec["name"], "--out", dst], quiet=False)
    # Say which seeds this lift is about to use, and refuse to leave a lift that
    # no longer matches its inputs. The fingerprint is the answer to "did these
    # two runs read the same seeds", which a function count alone cannot give.
    fp = seed_fingerprint(args.gen_root)
    log("seed fingerprint %s (sha256 over %s)" % (fp, os.path.join(args.gen_root, "seeds")))
    if lift_is_stale(out, args.gen_root):
        log("PPU lift is STALE: an input is newer than %s; relifting" % os.path.join(out, "table.cpp"))
    else:
        log("PPU lift inputs are older than %s; lifting anyway (this step always lifts)"
            % os.path.join(out, "table.cpp"))
    log("PPU recompilation into %s from %s" % (out, args.images))
    if not try_ppu(args, out, use_static_seeds=True):
        # One image's static seeds can produce code that does not compile (a
        # seed inside an already-explored function used to split it and leave a
        # goto to a label the generator never emitted). Rather than block the
        # whole lift, retry without them and say so: the dynamic loop is the
        # safety net, and a lift that builds beats a lift that is "complete".
        log("PPU recompilation FAILED with static seeds; retrying without them")
        if not try_ppu(args, out, use_static_seeds=False):
            raise SystemExit("PPU recompilation failed with and without static seeds")


def try_ppu(args, out, use_static_seeds):
    env = dict(os.environ)
    env["PS3BASE_STATIC_SEEDS"] = "1" if use_static_seeds else "0"
    import subprocess
    r = subprocess.run([sys.executable, "recomp/ppu/codegen.py", args.images, args.nid_db, out],
                       cwd=ROOT, env=env, text=True)
    return r.returncode == 0


def step_blocks(args):
    path = os.path.join(args.gen_root, "blocks", "run_manifest.bin")
    n = spu_blocks.build(spu_blocks.MANIFEST, args.elf, args.fw, path)
    log("%d SPU blocks extracted into %s" % (n, path))
    return os.path.dirname(path)


def step_spu_aot(args, blocks_dir):
    out = os.path.join(args.out_root, "spu")
    log("SPU AOT codegen into %s" % out)
    run(["recomp/spu/codegen.py", out, os.path.join(blocks_dir, "run*.bin")], quiet=False)


def step_natives(args, blocks_dir):
    gen = os.path.join(args.gen_root, "spu")
    env = dict(os.environ, GOWA_ELF=os.path.abspath(args.elf), GOWA_SPU_BLOCKS=os.path.abspath(blocks_dir), GOWA_GEN_DIR=os.path.abspath(gen))
    stamp = os.path.join(gen, STAMP)
    os.makedirs(gen, exist_ok=True)
    if os.path.exists(stamp):
        os.remove(stamp)
    want = set(args.only.split(",")) if args.only else None
    for name, sub, argv in GENERATORS:
        if want and name not in want:
            continue
        os.makedirs(os.path.join(gen, sub), exist_ok=True)
        t0 = time.time()
        run([a.replace("{gen}", gen) for a in argv], env=env)
        log("generated %-10s (%.1f s)" % (name, time.time() - t0))
    files = []
    for d, _, names in os.walk(gen):
        for n in names:
            if n.endswith((".cpp", ".h", ".inc")):
                p = os.path.join(d, n)
                files.append((os.path.relpath(p, gen).replace("\\", "/"), sha256(p)))
    files.sort()
    open(stamp, "w", newline="\n").write("".join("%s %s\n" % (h, p) for p, h in files))
    log("%d native SPU sources in %s" % (len(files), gen))


def main():
    ap = argparse.ArgumentParser(description="Generate all game-derived sources from your own game files")
    ap.add_argument("--elf", default=os.path.join(ROOT, "bin", "01.12", "GOWA.patched.elf"), help="patched decrypted GOWA 01.12 ELF")
    ap.add_argument("--fw", default=os.path.join(ROOT, "fw", "4.91"), help="directory with decrypted firmware .prx modules")
    ap.add_argument("--images", default=os.path.join(ROOT, "recomp", "images.json"),
                    help="manifest of guest images to lift; replace it to lift a different title")
    ap.add_argument("--nid-db", default=os.path.join(ROOT, "ref", "nid_db.json"))
    ap.add_argument("--out-root", default=os.path.join(ROOT, "out"), help="root for out/ppu and out/spu")
    ap.add_argument("--gen-root", default=os.path.join(ROOT, "out", "gen"), help="root for extracted blocks and generated SPU natives")
    ap.add_argument("--steps", default="spu", help="comma list of: ppu, aot, natives, spu (=aot,natives), all")
    ap.add_argument("--only", default="", help="comma list of native generator names")
    ap.add_argument("--allow-unknown", action="store_true")
    args = ap.parse_args()
    steps = set(args.steps.split(","))
    if "spu" in steps:
        steps |= {"aot", "natives"}
    if "all" in steps:
        steps |= {"ppu", "aot", "natives"}
    step_patch(args)
    step_check_elf(args)
    if "ppu" in steps:
        step_reloc(args)
        step_ppu(args)
    if steps & {"aot", "natives"}:
        blocks_dir = step_blocks(args)
        if "aot" in steps:
            step_spu_aot(args, blocks_dir)
        if "natives" in steps:
            step_natives(args, blocks_dir)
    log("done")


if __name__ == "__main__":
    main()
