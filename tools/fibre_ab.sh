#!/bin/sh
# fibre_ab.sh -- measure the fibre cost and the frame rate on the green screen.
# One instance, under the machine lock, killed by the caller.
#
# usage: fibre_ab.sh <outdir> <label>
set -u
OUT=${1:?outdir}; LABEL=${2:-run}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VFS=${RDR_VFS:-/root/Dev/rdr-port/vfs}
FLASH=${GOWA_FLASH_DIR:-/root/Dev/devflash-out/dev_flash}
DISP=${RDR_DISPLAY:-:62}
BIN=${RDR_BIN:-$ROOT/build-rdr/ps3base}
rm -rf "$OUT"; mkdir -p "$OUT/shots"
rm -rf "$VFS/dev_hdd0/home/00000001/savedata"; mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"
rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > "$OUT/xvfb.log" 2>&1 &
XV=$!; sleep 2
GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" PS3BASE_LOG=info \
PS3BASE_FS_LOG=1 DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
  python3 "$ROOT/tools/drive_rdr.py" "$BIN" "$DISP" "$VFS" "$OUT/drv" 900 \
  --branch cross --cwd "$ROOT" > "$OUT/driver.log" 2>&1 &
DRV=$!
# sample 1/s, and record RSS of the guest for the memory measurement
i=0
while kill -0 "$DRV" 2>/dev/null; do
  i=$((i+1))
  S=$(printf '%s/shots/sec-%04d.png' "$OUT" "$i")
  DISPLAY="$DISP" import -window root "$S" 2>/dev/null
  G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
  if [ -n "$G" ]; then
    echo "$i $(date +%s) $(awk '/VmRSS/{print $2}' /proc/$G/status 2>/dev/null)" >> "$OUT/rss.txt"
  fi
  if [ -f "$S" ] && python3 "$ROOT/tools/is_green.py" "$S" >/dev/null 2>&1; then
    echo "green at $i"; GREEN_AT=$i; break
  fi
  # Time-bounded, not sample-bounded: the card arrives at ~167 s.
  [ "$i" -ge "${MAX_SAMPLES:-900}" ] && break
done
# hold 120 s on the green, sampling RSS and the flip counter
HOLD=120; j=0
while [ "$j" -lt "$HOLD" ]; do
  j=$((j+1)); i=$((i+1))
  S=$(printf '%s/shots/sec-%04d.png' "$OUT" "$i")
  DISPLAY="$DISP" import -window root "$S" 2>/dev/null
  G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
  if [ -n "$G" ]; then
    echo "$i $(date +%s) $(awk '/VmRSS/{print $2}' /proc/$G/status 2>/dev/null)" >> "$OUT/rss.txt"
  fi
  sleep 1
done
kill -9 "$DRV" 2>/dev/null
G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
[ -n "$G" ] && kill -9 "$G" 2>/dev/null
kill "$XV" 2>/dev/null
echo "done $LABEL green_at=${GREEN_AT:-none} shots=$i"
