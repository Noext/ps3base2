#!/usr/bin/env bash
# Ten sequential world creations, each one proving it actually created a world.
#
# Why ten separate boots rather than ten loops inside one process: the only way
# back from a world to the title is the pause menu's Save & Quit, and this
# campaign never validated that transition. A driver built on an unvalidated
# transition fails for reasons that have nothing to do with the thing under
# test. A fresh boot is a genuine second passage through the autosave screen and
# through creation, and it drops every inherited state -- which is the variable a
# 10/10 claim is actually about.
#
# The proof per iteration is mechanical, not visual: count
# 'sys_vm_memory_map ->' in that iteration's own log. It must be at least 1. A
# run that only reaches the menu leaves it at 0 and is recorded as a FAIL, never
# as a pass.
#
# usage: tools/proof10.sh [vfs] [iterations] [outdir] [tagprefix] [seconds-per-run]
set -uo pipefail
TREE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VFS="${1:-vfs-41}"
N="${2:-10}"
OUT="${3:-/tmp/proof41}"
TAGBASE="${4:-41p}"
BUDGET="${5:-600}"

mkdir -p "$OUT"
: > "$OUT/summary.txt"

CKS="$(printf '%s' "$TREE/$VFS" | cksum | cut -d' ' -f1)"
SHOW=":$(( 10 + (CKS % 40) ))"

pass=0
i=1
while [ "$i" -le "$N" ]; do
    tag="${TAGBASE}$i"
    run="/tmp/run-world-$tag"
    rm -rf "$run"
    echo "=== iteration $i/$N (tag $tag) $(date -Is)" | tee -a "$OUT/summary.txt"

    ( cd "$TREE" && exec tools/run_world.sh "$VFS" --seconds "$BUDGET" --tag "$tag" --no-build \
        --env PS3BASE_ALLOW_UNFOCUSED_INPUT=1 > "$OUT/$tag.out" 2>&1 ) &
    runner=$!

    log="$run/game.log"
    for _ in $(seq 1 90); do
        grep -q 'rsx: flip' "$log" 2>/dev/null && break
        sleep 1
    done
    sleep 5

    python3 "$TREE/tools/drive_world.py" "$SHOW" "$log" "$(( BUDGET - 40 ))"         "$OUT/shots-$tag" >> "$OUT/$tag.drive.txt" 2>&1

    maps=$(grep -c 'sys_vm_memory_map ->' "$log" 2>/dev/null || true)
    dialogs=$(grep -c 'msgdialog: open2' "$log" 2>/dev/null || true)
    saves=$(grep -c "savedata: save '" "$log" 2>/dev/null || true)
    [ -n "$maps" ] || maps=0
    [ -n "$dialogs" ] || dialogs=0
    [ -n "$saves" ] || saves=0
    # How many times the title's OWN autosave box was actually on screen, as
    # counted by the driver frame by frame. This is the number the brief asks
    # for and the one that shows a desynchronisation: the count rises every time
    # the box reappears, and a run that loops scores it over and over without
    # ever reaching a world.
    encounters=$(grep -c ' DIALOG$' "$OUT/$tag.drive.txt" 2>/dev/null || true)
    [ -n "$encounters" ] || encounters=0

    # Photograph the moment of proof before tearing the run down.
    python3 "$TREE/tools/shot.py" "$SHOW" "$OUT/iter-$(printf '%02d' "$i").png" >/dev/null 2>&1 || true

    if [ -f "$run/gamepid" ]; then
        kill -9 -- "-$(cat "$run/gamepid")" 2>/dev/null || true
    fi
    kill -9 -- "-$runner" 2>/dev/null || true
    kill "$runner" 2>/dev/null || true
    wait "$runner" 2>/dev/null

    if [ "$maps" -ge 1 ]; then
        pass=$((pass + 1))
        verdict=PASS
    else
        verdict=FAIL
    fi
    printf 'iter %2d  vm_map=%s  saves=%s  system_dialogs=%s  title_dialog_frames=%s  %s\n' \
        "$i" "$maps" "$saves" "$dialogs" "$encounters" "$verdict" | tee -a "$OUT/summary.txt"
    i=$((i + 1))
done

echo "---" | tee -a "$OUT/summary.txt"
echo "PASS $pass / $N" | tee -a "$OUT/summary.txt"
[ "$pass" -eq "$N" ]
