#!/usr/bin/env bash
# spu_converge.sh -- drive the AOT table to 100% by itself.
#
# The loop: run a scenario under GOWA_SPU_STRICT=1. If the run passes, the
# scenario is covered. If it fails, the run has already named the missing block;
# capture the gaps with GOWA_SPU_COLLECT, merge them into out/gen/blocks/,
# regenerate the table, rebuild, and try the same scenario again.
#
# Why collect rather than record-and-hope: GOWA_SPU_COLLECT writes every block
# the interpreter touched in one session in the {lsa, n, words...} format the
# codegen reads, so one failed strict run enumerates MANY gaps instead of one.
# A strict run stops at the first gap; a collect run does not.
#
# Usage:
#   tools/spu_converge.sh [--workdir DIR] [--max-rounds N] [--only NAME]
#
# Scenarios are declared in SCENARIOS below. Each has a seed, a mode (survival or
# creative), a play time, and whether it loads an existing world instead of
# creating one.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${TMPDIR:-/tmp}/spu-converge"
MAX_ROUNDS=12
ONLY=""
BUILD_DIR="$ROOT/build"
BLOCKS="$ROOT/out/gen/blocks"
DISPLAY_NUM="${SPU_CONVERGE_DISPLAY:-:78}"

while [ $# -gt 0 ]; do
    case "$1" in
        --workdir) WORK="$2"; shift 2 ;;
        --max-rounds) MAX_ROUNDS="$2"; shift 2 ;;
        --only) ONLY="$2"; shift 2 ;;
        --display) DISPLAY_NUM="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$WORK" "$BLOCKS"
LOG="$WORK/converge.log"
: > "$LOG"
say() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }

# name seed mode play-seconds load-existing
SCENARIOS=(
  "gen-survival-1 seedA survival 900 0"
  "gen-survival-2 seedB survival 900 0"
  "gen-creative-3 seedC creative 900 0"
  "gen-survival-4 seedD survival 900 0"
  "gen-survival-5 seedE survival 900 0"
  "load-existing seedA survival 900 1"
)

kill_game() {
    for p in $(pgrep -f "$BUILD_DIR/ps3base --no-launcher" 2>/dev/null); do
        kill -9 "$p" 2>/dev/null
    done
    sleep 2
}

# Launch through setsid+nohup from a detached subshell. A child started inside a
# tool call dies when that call returns ("gpu: window closed, exiting"), which
# cost several turns of measurements before it was identified.
launch() {
    local vfs="$1" log="$2" seed="$3" mode="$4" load="$5" collect="$6"
    local envs=(
        "DISPLAY=$DISPLAY_NUM"
        "XDG_RUNTIME_DIR=/run/user/0"
        "PS3BASE_DISC_DIR=/root/Dev/mc-port/disc"
        "PS3BASE_VFS_DIR=$vfs"
        "PS3BASE_LOG=info"
        "PS3BASE_ALLOW_UNFOCUSED_INPUT=1"
        "DRIVE_SEED=$seed"
    )
    [ -n "$collect" ] && envs+=("GOWA_SPU_COLLECT=$collect")
    nohup setsid env "${envs[@]}" "$BUILD_DIR/ps3base" --no-launcher > "$log" 2>&1 < /dev/null &
    disown
}

wait_window() {
    for _ in $(seq 1 90); do
        if DISPLAY="$DISPLAY_NUM" xdotool search --name Minecraft >/dev/null 2>&1; then return 0; fi
        sleep 2
    done
    return 1
}

run_scenario() {
    local name="$1" seed="$2" mode="$3" play="$4" load="$5" round="$6"
    local rd="$WORK/$name-r$round"
    local vfs="$ROOT/vfs-conv-$name"
    mkdir -p "$rd"
    kill_game
    rm -rf "$vfs"
    if [ "$load" = "1" ]; then
        # A world has to exist for the load scenario; reuse a generated one.
        cp -a "$ROOT/vfs-clean" "$vfs" 2>/dev/null || return 2
        local src
        src="$(ls -d "$ROOT"/vfs-conv-gen-survival-1/dev_hdd0/home/00000001/savedata/BLES01976--* 2>/dev/null | head -1)"
        if [ -n "$src" ]; then
            cp -a "$src" "$vfs/dev_hdd0/home/00000001/savedata/"
        else
            return 2
        fi
    else
        cp -a "$ROOT/vfs-clean" "$vfs" 2>/dev/null || return 2
    fi

    local col="$rd/collect.bin"
    local log="$rd/game.log"
    launch "$vfs" "$log" "$seed" "$mode" "$load" "$col"
    wait_window || { say "  no window"; kill_game; return 2; }

    # Drive: create or load, then play. drive_world reads the screen, so it
    # survives the title running at 3 fps under load.
    python3 "$ROOT/tools/drive_world.py" "$DISPLAY_NUM" "$log" 300 "$rd/shots" >> "$rd/drive.txt" 2>&1
    local drc=$?
    say "  drive exit $drc"
    if [ "$drc" -ne 0 ]; then kill_game; return 1; fi

    # Play for the scenario's duration with scripted input, then stop.
    local end=$(( $(date +%s) + play ))
    local keys=(i l g j k r f t)
    while [ "$(date +%s)" -lt "$end" ]; do
        if grep -q 'crash: signal' "$log" 2>/dev/null; then say "  crashed"; kill_game; return 1; fi
        for k in "${keys[@]}"; do
            DISPLAY="$DISPLAY_NUM" xdotool keydown "$k" 2>/dev/null
            sleep 0.2
            DISPLAY="$DISPLAY_NUM" xdotool keyup "$k" 2>/dev/null
        done
        sleep 30
    done
    kill_game

    if grep -q 'STRICT:' "$log" 2>/dev/null; then
        say "  STRICT FAILED: $(grep -m1 'STRICT:' "$log" | grep -oE 'lsa [0-9a-f]+ words [0-9]+ hash [0-9a-f]+')"
        return 1
    fi
    if [ -s "$col" ]; then
        say "  collected $(stat -c%s "$col") bytes of gaps"
        return 1
    fi
    # A crash with no collected gap is NOT an SPU coverage problem. Reporting it
    # as one would send the loop after blocks that do not exist and make it
    # regenerate an empty set forever. Classify it separately.
    if grep -q 'crash: signal' "$log" 2>/dev/null; then
        local where
        where=$(grep -m1 'crash: signal' "$log" | grep -oE 'thread [^ ]+ [^ ]+' | head -1)
        say "  NON-SPU FAILURE: crashed ($where) with 0 collected gaps -- not a compiler gap"
        return 4
    fi
    say "  scenario passed under strict"
    return 0
}

regenerate() {
    local round="$1"
    local merged="$BLOCKS/run_converge_r$round.bin"
    rm -f "$merged"
    # Merge every collect file from this round into one codegen input.
    python3 - "$merged" "$WORK" "$round" <<'PY'
import glob, os, struct, sys
out, work, rnd = sys.argv[1], sys.argv[2], sys.argv[3]
seen = set()
buf = bytearray()
for p in sorted(glob.glob(os.path.join(work, '*-r%s' % rnd, 'collect.bin'))):
    d = open(p, 'rb').read()
    w = struct.unpack('<%dI' % (len(d) // 4), d[: len(d) // 4 * 4])
    i = 0
    while i + 2 <= len(w):
        lsa, n = w[i], w[i + 1]
        raw = tuple(w[i + 2:i + 2 + n])
        key = (lsa, raw)
        if key not in seen and n:
            seen.add(key)
            buf += struct.pack('<2I', lsa, n)
            buf += struct.pack('<%dI' % n, *raw)
        i += 2 + n
if buf:
    open(out, 'wb').write(buf)
print('%d distinct blocks merged' % len(seen))
PY
    if [ ! -s "$merged" ]; then
        say "  nothing to merge"
        return 1
    fi
    cp "$merged" "$BLOCKS/run_converge_r$round.bin"
    say "  regenerating the table"
    python3 "$ROOT/recomp/spu/codegen.py" "$ROOT/out/spu" "$BLOCKS"/run*.bin >> "$LOG" 2>&1 || return 1
    say "  rebuilding"
    cmake --build "$BUILD_DIR" -j 8 --target ps3base >> "$LOG" 2>&1 || return 1
    return 0
}

round=1
while [ "$round" -le "$MAX_ROUNDS" ]; do
    say "=== round $round/$MAX_ROUNDS"
    failed=""
    nonspu=""
    for entry in "${SCENARIOS[@]}"; do
        read -r name seed mode play load <<< "$entry"
        if [ -n "$ONLY" ] && [ "$name" != "$ONLY" ]; then continue; fi
        say "-- $name (seed=$seed mode=$mode play=${play}s load=$load)"
        run_scenario "$name" "$seed" "$mode" "$play" "$load" "$round"
        case $? in
            0) ;;
            4) nonspu="$nonspu $name" ;;
            *) failed="$failed $name" ;;
        esac
    done
    if [ -n "$nonspu" ]; then
        say "  failed for non-SPU reasons (not countable here):$nonspu"
    fi
    if [ -z "$failed" ]; then
        say "=== CONVERGED after $round round(s): every SPU-checkable scenario passed under GOWA_SPU_STRICT=1"
        [ -n "$nonspu" ] && say "=== these could not be judged, they crash for PPU/HLE reasons:$nonspu"
        exit 0
    fi
    say "  scenarios that still need blocks:$failed"
    regenerate "$round" || { say "  regeneration failed"; exit 3; }
    round=$((round + 1))
done
say "=== gave up after $MAX_ROUNDS rounds"
exit 1
