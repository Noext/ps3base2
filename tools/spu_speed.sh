#!/usr/bin/env bash
# spu_speed.sh -- the controlled speed table. ONE run at a time, machine at rest.
#
# Two things are measured on the same seed:
#   1. world generation time, interpreted (GOWA_SPU_NO_AOT=1) vs compiled;
#   2. fps in game on the real GPU.
# The two saved worlds are then compared by tools/compare_worlds.py, which reads
# RandomSeed from each and REFUSES to compare hashes if they differ -- three
# earlier "compiler bug" reports were two different worlds compared by mistake.
#
# Refuses to start unless the machine is quiet, because a generation measured
# under load is not a measurement.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SEED="${SPU_SPEED_SEED:-speed1}"
OUT="${SPU_SPEED_OUT:-/tmp/spu-speed}"
DISPLAY_NUM="${SPU_SPEED_DISPLAY:-:78}"
BUILD="$ROOT/build/ps3base"
MAX_LOAD="${SPU_SPEED_MAX_LOAD:-3.0}"

mkdir -p "$OUT"
say() { echo "[$(date +%T)] $*" | tee -a "$OUT/speed.log"; }

others=$(pgrep -f "$BUILD --no-launcher" | grep -v "^$" | wc -l)
# pgrep -f matches the shell that runs it; drop any pid that is not the emulator
others=$(for p in $(pgrep -f "$BUILD --no-launcher" 2>/dev/null); do
    [ "$(readlink /proc/$p/exe 2>/dev/null)" = "$BUILD" ] && echo "$p"
done | wc -l)
if [ "$others" -gt 0 ]; then
    say "REFUSING: $others game instance(s) already running; one at a time means one"
    exit 2
fi
# Load average is the wrong gate on this box: the harness' own web server sits
# near 87% of one core permanently, so the 1-minute average never drops to zero
# even with nothing of ours running. What actually pollutes a measurement is
# ANOTHER EMULATOR, so that is what is checked -- plus the load, against a
# threshold that accounts for the permanent baseline.
load=$(awk '{print $1}' /proc/loadavg)
if awk -v l="$load" -v m="$MAX_LOAD" 'BEGIN { exit !(l > m) }'; then
    busy=$(ps -eo pcpu,comm --sort=-pcpu | sed -n '2,4p' | tr '\n' ' ')
    say "REFUSING: load average $load exceeds $MAX_LOAD (top: $busy)"
    exit 2
fi

# One scenario: launch, drive a seeded creation, wait for the save, stop.
run_mode() {
    local mode="$1"      # interp | compiled
    local vfs="$ROOT/vfs-speed-$mode"
    local seed="$SEED"
    local rd="$OUT/$mode"
    mkdir -p "$rd"
    rm -rf "$vfs"
    cp -a "$ROOT/vfs-clean" "$vfs" || return 2
    local envs=(
        "DISPLAY=$DISPLAY_NUM"
        "XDG_RUNTIME_DIR=/run/user/0"
        "PS3BASE_DISC_DIR=/root/Dev/mc-port/disc"
        "PS3BASE_VFS_DIR=$vfs"
        "PS3BASE_LOG=info"
        "PS3BASE_ALLOW_UNFOCUSED_INPUT=1"
    )
    [ "$mode" = "interp" ] && envs+=("GOWA_SPU_NO_AOT=1")
    say "$mode: launching"
    nohup setsid env "${envs[@]}" "$BUILD" --no-launcher > "$rd/game.log" 2>&1 < /dev/null &
    disown
    for _ in $(seq 1 90); do
        DISPLAY="$DISPLAY_NUM" xdotool search --name Minecraft >/dev/null 2>&1 && break
        sleep 2
    done
    # DRIVE_SEED belongs to the DRIVER, not to the game. Passing it only in the
    # game's environment (as the first version did) left the driver with no seed
    # at all, so both runs built random worlds and the seed check correctly
    # refused the comparison.
    DRIVE_SEED="$seed" python3 "$ROOT/tools/drive_world.py" "$DISPLAY_NUM" "$rd/game.log" 300 "$rd/shots" > "$rd/drive.txt" 2>&1
    local drc=$?
    say "$mode: drive exit $drc"
    # Wait for the world to be written (autosave), up to 25 minutes.
    local waited=0
    while [ "$waited" -lt 1500 ]; do
        if ls -d "$vfs"/dev_hdd0/home/00000001/savedata/BLES01976--* >/dev/null 2>&1; then break; fi
        sleep 10
        waited=$((waited + 10))
    done
    say "$mode: world after ${waited}s"
    # fps from the run's own flip counter, while the world is up.
    local t0 flips0 t1 flips1
    t0=$(tail -1 "$rd/game.log" | grep -oE '^.[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
    flips0=$(grep -ac 'rsx: flip' "$rd/game.log")
    sleep 30
    t1=$(tail -1 "$rd/game.log" | grep -oE '^.[[:space:]]*[0-9.]+' | grep -oE '[0-9.]+')
    flips1=$(grep -ac 'rsx: flip' "$rd/game.log")
    awk -v a="$t0" -v b="$t1" -v f0="$flips0" -v f1="$flips1" \
        'BEGIN { d=b-a; printf "  fps over %.1fs: %.2f\n", d, d>0 ? (f1-f0)*20/d : 0 }'
    kill "$(pgrep -f "$BUILD --no-launcher" | head -1)" 2>/dev/null
    sleep 3
    for p in $(pgrep -f "$BUILD --no-launcher"); do kill -9 "$p" 2>/dev/null; done
}

for mode in interp compiled; do
    run_mode "$mode"
done

# Generation times, read from the runs' own logs.
python3 - "$OUT" "$SEED" <<'PY'
import glob, os, re, sys
out, seed = sys.argv[1], sys.argv[2]
print()
print("mode      post_start   autosave    generation_s")
for mode in ("interp", "compiled"):
    log = os.path.join(out, mode, "game.log")
    try:
        txt = open(log, errors="ignore").read()
    except OSError:
        print("%-9s  (no log)" % mode)
        continue
    def ts(pat):
        m = re.search(pat + r".*?^I\s*([0-9.]+)", txt, re.M)
        if not m:
            m = re.search(r"^I\s*([0-9.]+).*?" + pat, txt, re.M)
        return float(m.group(1)) if m else None
    ps = ts(r"Post processing' start")
    sv = ts(r"UserAutoSave")
    if ps and sv:
        print("%-9s  %10.2f  %10.2f  %12.2f" % (mode, ps, sv, sv - ps))
    else:
        print("%-9s  post_start=%s autosave=%s" % (mode, ps, sv))
PY

say "comparing the two saved worlds"
for mode in interp compiled; do
    g=$(ls -d "$ROOT"/vfs-speed-$mode/dev_hdd0/home/00000001/savedata/BLES01976--*/GAMEDATA 2>/dev/null | head -1)
    [ -n "$g" ] && echo "$mode: $g" >> "$OUT/worlds.txt"
done
gi=$(ls -d "$ROOT"/vfs-speed-interp/dev_hdd0/home/00000001/savedata/BLES01976--*/GAMEDATA 2>/dev/null | head -1)
gc=$(ls -d "$ROOT"/vfs-speed-compiled/dev_hdd0/home/00000001/savedata/BLES01976--*/GAMEDATA 2>/dev/null | head -1)
if [ -n "$gi" ] && [ -n "$gc" ]; then
    python3 "$ROOT/tools/compare_worlds.py" "$gi" "$gc" | tee -a "$OUT/speed.log"
else
    say "no world pair to compare (one mode did not save)"
fi
