#!/bin/sh
# Lift one title with THIS tree's lifter and build it, into a per-title output
# tree under out-S1/.  $1 = mc|gta|rdr
#
# The firmware relocation is NOT redone: prx_relocate.py is untouched by the
# commit under test, and out-gta/gen/reloc, out-rdr/gen/reloc already exist in
# the parent checkout (symlinked into this worktree). Only the PPU code
# generation differs, which is exactly the lifter being tested.
set -u
ROOT=/root/Dev/wt-S1
T=$1
case "$T" in
  mc)  MAN=out-S1/images-mc.json;  OUT=out-S1/mc;  BUILD=build-S1 ;;
  gta) MAN=recomp/images-gta.json; OUT=out-S1/gta; BUILD=build-gta-S1 ;;
  rdr) MAN=recomp/images-rdr.json; OUT=out-S1/rdr; BUILD=build-rdr-S1 ;;
esac
LOG=/tmp/S1-lift-$T.log
exec >>"$LOG" 2>&1
echo "=== $(date -u +%H:%M:%S) lift $T commit $(git -C "$ROOT" rev-parse --short HEAD) ==="

mkdir -p "$ROOT/$OUT/gen/seeds" "$ROOT/$OUT/ppu"

python3 - "$ROOT" "$MAN" "$OUT" <<'PY' || exit 125
import json, os, subprocess, sys
root, man, out = sys.argv[1], sys.argv[2], sys.argv[3]
mpath = os.path.join(root, man)
m = json.load(open(mpath))
# Seeds next to the output tree the lift writes, one per manifest entry.
for spec in m:
    elf = spec["elf"]
    if not os.path.isabs(elf):
        elf = os.path.normpath(os.path.join(os.path.dirname(mpath), elf))
    dst = os.path.join(root, out, "gen/seeds", spec["name"] + ".txt")
    if subprocess.run([sys.executable, root + "/tools/static_seeds.py", "--elf", elf,
                       "--name", spec["name"], "--out", dst], cwd=root).returncode:
        sys.exit("static_seeds failed for " + spec["name"])
PY

PS3BASE_PROFILE= python3 "$ROOT/recomp/ppu/codegen.py" "$ROOT/$MAN" \
    "$ROOT/ref/nid_db.json" "$ROOT/$OUT/ppu" > "$ROOT/$OUT/lift.log" 2>&1 \
    || { echo "LIFT FAILED"; exit 125; }
grep -E '^total functions' "$ROOT/$OUT/lift.log"

cmake -S "$ROOT" -B "$ROOT/$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DPS3BASE_GENERATED_DIR="$ROOT/$OUT/ppu" \
    -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
    -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu \
    > "$ROOT/$OUT/cmake.log" 2>&1 || { echo "CMAKE FAILED"; exit 125; }
cmake --build "$ROOT/$BUILD" -j 8 --target ps3base > "$ROOT/$OUT/build.log" 2>&1 \
    || { echo "BUILD FAILED"; exit 125; }
echo "BUILD OK"
