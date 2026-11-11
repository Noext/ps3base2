#!/bin/sh
# Lift Minecraft with THIS tree's lifter and build it. Usage: tools/lift-build-S1.sh <tag>
set -u
ROOT=/root/Dev/wt-S1
TAG=$1
OUT=$ROOT/out-S1
BUILD=$ROOT/build-S1
LOG=/tmp/S1-lift-$TAG.log
exec >>"$LOG" 2>&1
echo "=== $(date -u +%H:%M:%S) lift-build $TAG commit $(git -C "$ROOT" rev-parse --short HEAD) ==="

rm -rf "$OUT/mc"
mkdir -p "$OUT/mc/gen/reloc" "$OUT/mc/gen/seeds" "$OUT/mc/ppu"
python3 "$ROOT/tools/layout_for.py" --elf "$ROOT/bin/mc/mc.elf" \
    --modules "$ROOT/lle_modules-mc.txt" --out "$OUT/mc/gen/lle_layout.json" || exit 125
python3 "$ROOT/tools/prx_relocate.py" "$OUT/mc/gen/lle_layout.json" "$ROOT/fw/4.91" \
    "$OUT/mc/gen/reloc" || exit 125

python3 - "$OUT" "$ROOT" <<'PY' || exit 125
import json, sys
out, root = sys.argv[1], sys.argv[2]
mods = [l.strip() for l in open(root + "/lle_modules-mc.txt") if l.strip()]
m = [{"name": "mc", "elf": root + "/bin/mc/mc.elf", "file": "bin/mc/mc.elf"}]
for name in mods:
    n = name[:-4]
    f = "out-S1/mc/gen/reloc/%s.elf" % n
    m.append({"name": n, "elf": root + "/" + f, "file": f})
json.dump(m, open(out + "/images-mc.json", "w"), indent=1)
PY

python3 - "$OUT" "$ROOT" <<'PY' || exit 125
import json, os, subprocess, sys
out, root = sys.argv[1], sys.argv[2]
manifest = json.load(open(out + "/images-mc.json"))
for spec in manifest:
    dst = os.path.join(out, "mc/gen/seeds", spec["name"] + ".txt")
    if subprocess.run([sys.executable, root + "/tools/static_seeds.py", "--elf", spec["elf"],
                       "--name", spec["name"], "--out", dst], cwd=root).returncode:
        sys.exit("static_seeds failed for " + spec["name"])
PY

PS3BASE_PROFILE= python3 "$ROOT/recomp/ppu/codegen.py" "$OUT/images-mc.json" \
    "$ROOT/ref/nid_db.json" "$OUT/mc/ppu" > "$OUT/mc/lift.log" 2>&1 \
    || { echo "LIFT FAILED"; exit 125; }
grep -E '^total functions' "$OUT/mc/lift.log"
grep -c 'f_00739fb4' "$OUT/mc/ppu/funcs.h"
grep -c '739fb4' "$OUT/mc/ppu"/*.cpp | grep -v ':0' | head

cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DPS3BASE_GENERATED_DIR="$OUT/mc/ppu" \
    -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
    -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu \
    > "$OUT/mc/cmake.log" 2>&1 || { echo "CMAKE FAILED"; exit 125; }
cmake --build "$BUILD" -j 8 --target ps3base > "$OUT/mc/build.log" 2>&1 \
    || { echo "BUILD FAILED"; exit 125; }
echo "BUILD OK"
