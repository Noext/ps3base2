#!/bin/sh
# vmsize_probe.sh -- measure VmSize/VmRSS while the title creates fibres.
# The leak is in VIRTUAL address space: a 256 KB stack that is never freed is
# never munmap'd, but an untouched stack is not resident, so RSS does not show
# it. This samples both.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VFS=/root/Dev/rdr-port/vfs; FLASH=/root/Dev/devflash-out/dev_flash
L=$1; BIN=$2; DISP=$3
OUT=/tmp/vm-$L; rm -rf "$OUT"; mkdir -p "$OUT"
rm -rf "$VFS/dev_hdd0/home/00000001/savedata"; mkdir -p "$VFS/dev_hdd0/home/00000001/savedata"
rm -f "/tmp/.X11-unix/X${DISP#:}"
Xvfb "$DISP" -screen 0 1280x720x24 -ac +extension GLX +render -noreset > "$OUT/xvfb.log" 2>&1 &
XV=$!; sleep 2
GOWA_FLASH_DIR="$FLASH" PS3BASE_DISC_DIR="$VFS/dev_bdvd" PS3BASE_LOG=info \
DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)" \
  python3 "$ROOT/tools/drive_rdr.py" "$BIN" "$DISP" "$VFS" "$OUT/drv" 400 \
  --branch cross --cwd "$ROOT" > "$OUT/driver.log" 2>&1 &
DRV=$!
i=0
while kill -0 "$DRV" 2>/dev/null && [ "$i" -lt 260 ]; do
  i=$((i+1))
  G=$(pgrep -f -- "$BIN --no-launcher" | head -1)
  if [ -n "$G" ]; then
    VS=$(awk '/VmSize/{print $2}' /proc/$G/status 2>/dev/null)
    VR=$(awk '/VmRSS/{print $2}' /proc/$G/status 2>/dev/null)
    NF=$(grep -ac 'HLE CreateFiber' "$OUT/drv/run.log" 2>/dev/null)
    echo "$i $VS $VR $NF" >> "$OUT/vm.txt"
  fi
  sleep 1
done
kill -9 "$DRV" 2>/dev/null
G=$(pgrep -f -- "$BIN --no-launcher" | head -1); [ -n "$G" ] && kill -9 "$G" 2>/dev/null
kill "$XV" 2>/dev/null
echo "$L done samples=$i"
