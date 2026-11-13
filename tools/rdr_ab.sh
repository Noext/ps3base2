#!/bin/sh
# A/B two RDR binaries with the real driver, back to back, same display.
#
# Why not tools/regress.sh for this title: its RDR criterion is the ALERT screen,
# but it sends no input, so the title never leaves "Press START" there and the
# screen check cannot pass. This script drives with tools/drive_rdr.py, which
# presses START; the pair is still back to back on one display, which is what the
# measurement rule asks for.
#
# usage: rdr_ab.sh <binA> <binB> <tag> [seconds]
#
# Environment:
#   RDR_VFS        VFS root        (default: /root/Dev/rdr-port/vfs)
#   GOWA_FLASH_DIR dev_flash tree  (default: /root/Dev/devflash-out/dev_flash)
#   RDR_DISPLAY    X display       (default: :57, started here)
set -u
A=${1:?binary A}
B=${2:?binary B}
TAG=${3:?tag}
SECS=${4:-400}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VFS=${RDR_VFS:-/root/Dev/rdr-port/vfs}
FLASH=${GOWA_FLASH_DIR:-/root/Dev/devflash-out/dev_flash}
DISP=${RDR_DISPLAY:-:57}
OUT=/tmp/rdr-ab-$TAG
LOCK=${GAME_LOCK:-/tmp/ps3base-game.lock}

fail() { echo "rdr_ab: $*" >&2; exit 2; }

[ -x "$A" ] || fail "no binary at $A"
[ -x "$B" ] || fail "no binary at $B"
[ -d "$VFS" ] || fail "no vfs at $VFS"
[ -d "$FLASH" ] || fail "no dev_flash at $FLASH"

if ! mkdir "$LOCK" 2>/dev/null; then fail "$LOCK is held"; fi
echo $$ > "$LOCK/pid"
XPID=""
cleanup() {
    [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
    pkill -9 -f "ps3base --no-launcher" 2>/dev/null
    rm -rf "$LOCK"
}
trap cleanup EXIT INT TERM

pkill -9 -f "ps3base --no-launcher" 2>/dev/null
sleep 1
pgrep -f "ps3base --no-launcher" >/dev/null 2>&1 && fail "a ps3base is still running"

rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > /tmp/xvfb-ab.log 2>&1 &
XPID=$!
sleep 2
DISPLAY="$DISP" import -window root /tmp/ab-probe.png 2>/dev/null || fail "display $DISP did not come up"

run_one() {
    ro_tag=$1; ro_bin=$2
    rm -rf "$VFS/dev_hdd0/home/00000001/savedata"
    mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"
    rm -rf "$OUT-$ro_tag"
    echo "  $ro_tag load=$(cut -d' ' -f1-3 /proc/loadavg)"
    GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" \
    PS3BASE_LOG=info DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
    timeout "$SECS" python3 "$ROOT/tools/drive_rdr.py" \
        "$ro_bin" "$DISP" "$VFS" "$OUT-$ro_tag" "$SECS" \
        --branch cross --cwd "$ROOT" > "$OUT-$ro_tag.log" 2>&1
    ro_rc=$?
    ro_log="$OUT-$ro_tag/run.log"
    echo "  $ro_tag exit=$ro_rc flips=$(grep -a 'rsx: flip' "$ro_log" 2>/dev/null | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+') crashes=$(grep -ac 'crash: signal' "$ro_log" 2>/dev/null) unknown=$(grep -ac 'unknown indirect' "$ro_log" 2>/dev/null) chapter=$(grep -ac 'STATE CHAPTER' "$OUT-$ro_tag.log" 2>/dev/null) alert=$(grep -ac 'STATE ALERT' "$OUT-$ro_tag.log" 2>/dev/null)"
}

echo "A/B $TAG: A=$A B=$B, ${SECS}s each, back to back on $DISP"
run_one A "$A"
run_one B "$B"
echo "A/B $TAG done"
