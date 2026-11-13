#!/bin/sh
# fibre_ab2.sh -- A/B the old and new cellFiber in ONE minute, back to back.
# The measurement rule: two runs minutes apart on a shared machine are not
# comparable, so both binaries are run in the same window, one at a time.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VFS=/root/Dev/rdr-port/vfs
FLASH=/root/Dev/devflash-out/dev_flash
run_one() {
  L=$1; BIN=$2; DISP=$3
  OUT=/tmp/fibre-ab2-$L
  rm -rf "$OUT"; mkdir -p "$OUT/shots"
  rm -rf "$VFS/dev_hdd0/home/00000001/savedata"; mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"
  rm -f "/tmp/.X11-unix/X${DISP#:}"
  Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > "$OUT/xvfb.log" 2>&1 &
  XV=$!; sleep 2
  echo "$L load=$(cut -d' ' -f1-3 /proc/loadavg)"
  GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" PS3BASE_LOG=info \
  DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
    python3 "$ROOT/tools/drive_rdr.py" "$BIN" "$DISP" "$VFS" "$OUT/drv" 900 \
    --branch cross --cwd "$ROOT" > "$OUT/driver.log" 2>&1 &
  DRV=$!
  i=0; GREEN=0
  while kill -0 "$DRV" 2>/dev/null; do
    i=$((i+1))
    S=$(printf '%s/shots/sec-%04d.png' "$OUT" "$i")
    DISPLAY="$DISP" import -window root "$S" 2>/dev/null
    G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
    [ -n "$G" ] && echo "$i $(date +%s) $(awk '/VmRSS/{print $2}' /proc/$G/status 2>/dev/null)" >> "$OUT/rss.txt"
    if [ -f "$S" ] && python3 "$ROOT/tools/is_green.py" "$S" >/dev/null 2>&1; then
      GREEN=$i; echo "green at $i"; break
    fi
    # Time-bounded, not sample-bounded: the card arrives at ~167 s.
    [ "$i" -ge "${MAX_SAMPLES:-900}" ] && break
  done
  # hold 90 s on the green
  j=0
  while [ "$j" -lt 90 ]; do
    j=$((j+1)); i=$((i+1))
    S=$(printf '%s/shots/sec-%04d.png' "$OUT" "$i")
    DISPLAY="$DISP" import -window root "$S" 2>/dev/null
    G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
    [ -n "$G" ] && echo "$i $(date +%s) $(awk '/VmRSS/{print $2}' /proc/$G/status 2>/dev/null)" >> "$OUT/rss.txt"
    sleep 1
  done
  kill -9 "$DRV" 2>/dev/null
  G=$(pgrep -f -- "$BIN --no-launcher" | head -1); [ -n "$G" ] && kill -9 "$G" 2>/dev/null
  kill "$XV" 2>/dev/null
  echo "$L done green=$GREEN shots=$i"
}
run_one old "$ROOT/build-rdr/ps3base-old" :63
run_one new "$ROOT/build-rdr/ps3base" :64
