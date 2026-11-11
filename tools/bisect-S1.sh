#!/bin/sh
# One bisect step for the Minecraft regression.
#
# Judges the commit under test by its OWN lifter: relocate the eight proven
# firmware modules above the title, generate the static seeds, lift the nine
# images, build, then run for 120 s. Pass = alive at the budget with more than
# 2000 frame flips and no crash.
#
# setup.py is deliberately not used: inside this range its step_reloc recomputes
# the layout from recomp/lle_modules.txt, which changed in the range, so a
# commit's own module list would move the module bases and the manifest would
# name files that do not exist. The module set here is fixed to
# recomp/lle_modules-proven.txt, so the only thing that varies between bisect
# steps is the lifter under test.
#
# Exit codes follow git-bisect-run's contract:
#   0   good (the commit passes)
#   1   bad
#   125 skip -- lift or build failed, so the commit is not judgeable
set -u
ROOT=/root/Dev/wt-S1
OUT=$ROOT/out-S1/bisect
BUILD=$ROOT/build-S1
LOG=/tmp/S1-bisect.log
exec >>"$LOG" 2>&1
echo "=== $(date -u +%H:%M:%S) commit $(git -C "$ROOT" rev-parse --short HEAD) ==="

rm -rf "$OUT"
mkdir -p "$OUT/mc/gen/reloc" "$OUT/mc/gen/seeds" "$OUT/mc/ppu"

# 1. Firmware relocation: the eight proven modules, bases computed from the
#    title's own text extent.
python3 "$ROOT/tools/layout_for.py" --elf "$ROOT/bin/mc/mc.elf" \
    --modules "$ROOT/out-S1/lle_modules-mc.txt" --out "$OUT/mc/gen/lle_layout.json" || exit 125
python3 "$ROOT/tools/prx_relocate.py" "$OUT/mc/gen/lle_layout.json" "$ROOT/fw/4.91" \
    "$OUT/mc/gen/reloc" || exit 125

# 2. Manifest. Absolute "elf" paths, because the lifter opens them; root-relative
#    "file" paths, because the runtime opens those from the working directory.
python3 "$ROOT/tools/bisect_manifest.py" "$OUT" "$ROOT" || exit 125

# 3. Static seeds per image, exactly as setup.py's step_ppu does.
python3 "$ROOT/tools/bisect_seeds.py" "$OUT" "$ROOT" || exit 125

# 4. Lift. codegen.py directly rather than setup.py, which would relocate again
#    with the commit's own (changing) module list.
PS3BASE_PROFILE= python3 "$ROOT/recomp/ppu/codegen.py" "$OUT/images-mc.json" \
    "$ROOT/ref/nid_db.json" "$OUT/mc/ppu" > "$OUT/lift.log" 2>&1 \
    || { echo "LIFT FAILED"; exit 125; }
grep -q 'total functions' "$OUT/lift.log" || { echo "LIFT INCOMPLETE"; exit 125; }
grep -E '^total functions' "$OUT/lift.log"

# 4b. Build fixup, and it is not part of the bisect: CMakeLists.txt at
#     88d8214 and 2cf1fe0 names tests/sysutil_handlers.cpp, but that file only
#     arrives in 46ea2f1. Configuring those two commits therefore fails with
#     "Cannot find source file: tests/sysutil_handlers.cpp" and no bisect step
#     could build at all. The file is a test-only source that the ps3base target
#     does not compile, so restoring it from the range's tip changes nothing
#     about the runtime or the lifter under test. It is removed again on exit,
#     because at the later commits it is tracked and an untracked copy would
#     block the next checkout.
FIXUP=
if ! git -C "$ROOT" ls-files --error-unmatch tests/sysutil_handlers.cpp >/dev/null 2>&1; then
    git -C "$ROOT" show 01cf7eb:tests/sysutil_handlers.cpp > "$ROOT/tests/sysutil_handlers.cpp"
    FIXUP=1
    echo "fixup: restored tests/sysutil_handlers.cpp from 01cf7eb (untracked here)"
fi
# 4c. Second build fixup, also outside the bisect: cellSysutil.cpp at 88d8214
#     and 2cf1fe0 calls msgdialog::dialog_type(), progress_bar_inc/reset, which
#     msgdialog.cpp already defines there but msgdialog.h only DECLARES from
#     46ea2f1 on. Those two commits therefore do not compile at all:
#
#         cellSysutil.cpp:555:84: error: no member named 'dialog_type' in
#                                 namespace 'hle::msgdialog'
#
#     The declarations are taken from 46ea2f1, the commit that added exactly
#     them and no other line of that header. The runtime's behaviour is
#     unchanged: the definitions were already present.
HDRFIX=
if ! grep -q 'dialog_type' "$ROOT/runtime/include/runtime/msgdialog.h"; then
    git -C "$ROOT" show 46ea2f1:runtime/include/runtime/msgdialog.h \
        > "$ROOT/runtime/include/runtime/msgdialog.h"
    HDRFIX=1
    echo "fixup: msgdialog.h declarations restored from 46ea2f1"
fi

cleanup() {
    [ -n "$FIXUP" ] && rm -f "$ROOT/tests/sysutil_handlers.cpp"
    [ -n "$HDRFIX" ] && git -C "$ROOT" checkout -q -- runtime/include/runtime/msgdialog.h
}
trap cleanup EXIT INT TERM

# 5. Build.
cmake -S "$ROOT" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DPS3BASE_GENERATED_DIR="$OUT/mc/ppu" \
    -DGOWA_SPU_GENERATED_DIR=/root/Dev/ps3base2/out-rdr/spu \
    -DPS3BASE_SPU_NATIVE_GEN_DIR=/root/Dev/ps3base2/out-rdr/gen/spu \
    > "$OUT/cmake.log" 2>&1 || { echo "CMAKE FAILED"; exit 125; }
cmake --build "$BUILD" -j 8 --target ps3base > "$OUT/build.log" 2>&1 \
    || { echo "BUILD FAILED"; exit 125; }

# 6. Run and judge.
run=$OUT/run.log
rm -f "$run"
DISPLAY=:97 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR=/root/Dev/mc-port/disc \
    PS3BASE_LOG=info PS3BASE_VFS_DIR="$ROOT/vfs-S1" \
    timeout 120 "$BUILD/ps3base" --no-launcher > "$run" 2>&1
code=$?
flips=$(grep -a 'rsx: flip' "$run" | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+')
flips=${flips:-0}
crashes=$(grep -ac 'crash: signal' "$run")
echo "run: exit=$code flips=$flips crashes=$crashes"
grep -a 'unknown indirect\|crash: signal\|module .* loaded' "$run" | tail -4
if [ "$code" = 124 ] && [ "$flips" -gt 2000 ] && [ "$crashes" = 0 ]; then
    echo "VERDICT good"
    exit 0
fi
echo "VERDICT bad"
exit 1
