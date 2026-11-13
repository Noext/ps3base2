#!/bin/sh
# One RDR instance at a time, under a lock, bounded, killed by whoever started it.
#
# The lock is a directory (atomic) and the cleanup is by PID: an earlier version
# used `pkill -f 'Xvfb :55 '` and the pattern matched the wrapper's own command
# line, so the run killed itself.
#
# usage: rdr_run_once.sh <out-dir> [seconds] [branch]
#   branch: square = Continue Without Saving, cross = Create A New Save Game
#
# Environment:
#   RDR_BIN        binary to run        (default: this tree's build-rdr/ps3base)
#   RDR_VFS        VFS root             (default: /root/Dev/rdr-port/vfs)
#   GOWA_FLASH_DIR dev_flash tree       (default: /root/Dev/devflash-out/dev_flash)
#   RDR_DISPLAY    X display to use     (default: :55, started here)
set -u
OUT=${1:?out dir}
SECS=${2:-240}
BRANCH=${3:-cross}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${RDR_BIN:-$ROOT/build-rdr/ps3base}
VFS=${RDR_VFS:-/root/Dev/rdr-port/vfs}
FLASH=${GOWA_FLASH_DIR:-/root/Dev/devflash-out/dev_flash}
DISP=${RDR_DISPLAY:-:55}
LOCK=${GAME_LOCK:-/tmp/ps3base-game.lock}

[ -x "$BIN" ] || { echo "rdr_run_once: no binary at $BIN" >&2; exit 2; }
[ -d "$VFS" ] || { echo "rdr_run_once: no vfs at $VFS" >&2; exit 2; }
[ -d "$FLASH" ] || { echo "rdr_run_once: no dev_flash at $FLASH" >&2; exit 2; }

if ! mkdir "$LOCK" 2>/dev/null; then
    echo "rdr_run_once: $LOCK is held (owner $(cat "$LOCK/pid" 2>/dev/null))" >&2
    exit 9
fi
echo $$ > "$LOCK/pid"
XPID=""
cleanup() {
    [ -n "$XPID" ] && kill "$XPID" 2>/dev/null
    pkill -9 -f "ps3base --no-launcher" 2>/dev/null
    rm -rf "$LOCK"
}
trap cleanup EXIT INT TERM

# A stray from a previous run takes every core and voids the measurement.
pkill -9 -f "ps3base --no-launcher" 2>/dev/null
sleep 1

# savedata must be empty or the title loads a save instead of showing the ALERT.
rm -rf "$VFS/dev_hdd0/home/00000001/savedata"
mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"

rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > /tmp/xvfb-rdr.log 2>&1 &
XPID=$!
sleep 2

mkdir -p "$OUT"
echo "loadavg before: $(cut -d' ' -f1-3 /proc/loadavg)"
GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" \
PS3BASE_LOG=info DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
timeout "$SECS" python3 "$ROOT/tools/drive_rdr.py" \
    "$BIN" "$DISP" "$VFS" "$OUT" "$SECS" --branch "$BRANCH" --cwd "$ROOT"
echo "driver exit=$?"
echo "loadavg after: $(cut -d' ' -f1-3 /proc/loadavg)"
