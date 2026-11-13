#!/bin/sh
# green_screen_probe.sh -- run RDR to the green screen and HOLD it there so a
# profiler can sample the live guest. One instance, under the machine lock.
#
# Three defects in the first version, all found by a profiling run that could not
# use it, and all fixed here:
#
#  1. It launched the title itself AND through tools/drive_rdr.py, which launches
#     it again -- two guests, which breaks the one-game rule. The DRIVER is now
#     the only launcher.
#  2. Killing the driver killed the guest: drive_rdr.py's finally block sends
#     SIGKILL to its child, and SIGTERM to the driver runs that finally. The
#     driver is now killed with SIGKILL, which cannot be caught, so the guest
#     survives for the profiler.
#  3. The "is it green" test used ImageMagick's %[fx:standard_deviation] as if it
#     were 0..255; it is 0..1, so the threshold matched every frame and the first
#     profile was of the RED CHAPTER CARD. The test here measures the fraction of
#     pixels with G>60, R<40, B<40, the same test RESULTS-S3.md validated.
#
# usage: green_screen_probe.sh <outdir> [hold-seconds]
# Writes <outdir>/{run.log,driver.log,shots/,green-at.txt,game.pid}
set -u
OUT=${1:?outdir}
HOLD=${2:-40}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VFS=${RDR_VFS:-/root/Dev/rdr-port/vfs}
FLASH=${GOWA_FLASH_DIR:-/root/Dev/devflash-out/dev_flash}
DISP=${RDR_DISPLAY:-:61}
BIN=${RDR_BIN:-$ROOT/build-rdr/ps3base}

rm -rf "$OUT"; mkdir -p "$OUT/shots"
rm -rf "$VFS/dev_hdd0/home/00000001/savedata"
mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"
rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > "$OUT/xvfb.log" 2>&1 &
XV=$!
sleep 2
cleanup() { [ -n "${XV:-}" ] && kill "$XV" 2>/dev/null; }
trap cleanup EXIT INT TERM

# The driver is the ONLY launcher: it starts the guest, drives the pad, and
# journals each state. It is killed with SIGKILL so its finally never runs and
# the guest is left alive for the profiler.
GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" \
PS3BASE_LOG=info DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
  python3 "$ROOT/tools/drive_rdr.py" "$BIN" "$DISP" "$VFS" "$OUT/drv" 900 \
  --branch cross --cwd "$ROOT" > "$OUT/driver.log" 2>&1 &
DRV=$!

# Sample once a second; stop when the frame is green, then hold.
i=0
GREEN=0
while kill -0 "$DRV" 2>/dev/null; do
  i=$((i+1))
  SHOT=$(printf '%s/shots/sec-%04d.png' "$OUT" "$i")
  DISPLAY="$DISP" import -window root "$SHOT" 2>/dev/null
  if [ -f "$SHOT" ] && python3 "$ROOT/tools/is_green.py" "$SHOT" >/dev/null 2>&1; then
    GREEN=1
    date -u '+%Y-%m-%dT%H:%M:%SZ' > "$OUT/green-at.txt"
    echo "green at sample $i"
    break
  fi
  if [ "$i" -ge 600 ]; then break; fi
done

# SIGKILL the driver: its finally block would SIGKILL the guest.
kill -9 "$DRV" 2>/dev/null
sleep 1
GAME=$(pgrep -f -- "$BIN --no-launcher" | head -1)
if [ -z "$GAME" ]; then echo "NO GUEST ALIVE after the driver was killed" >&2; exit 3; fi
echo "$GAME" > "$OUT/game.pid"
echo "guest pid $GAME alive, green=$GREEN, holding ${HOLD}s"
sleep "$HOLD"
kill -TERM "$GAME" 2>/dev/null
sleep 1
kill -9 "$GAME" 2>/dev/null
echo "hold done"
