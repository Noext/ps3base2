#!/bin/bash
# q4_ab.sh -- the RoundGuard rounding A/B, interleaved pairs on one display.
#
# A = nearest  (main HEAD, pre-fix)   B = truncate (the fix)
# Same machine, same minute, A then B then A then B. One game at a time.
set -u
GAME=$1; PAIRS=${2:-2}; SECS=${3:-120}
case "$GAME" in
  mc)  DISC=/root/Dev/mc-port/disc ;;
  gta) DISC=/root/Dev/gta-port/disc ;;
  *) echo "usage: $0 mc|gta [pairs] [secs]"; exit 2 ;;
esac
A=/root/Dev/q4-ab/A-nearest-$GAME
B=/root/Dev/q4-ab/B-trunc-$GAME
RUNROOT=/root/Dev/ps3base2
DISP=:77
export GOWA_FLASH_DIR=/root/Dev/devflash-out/dev_flash
export PS3BASE_DISC_DIR=$DISC
export PS3BASE_LOG=info
export DISPLAY=$DISP
export XDG_RUNTIME_DIR=/run/user/0

run_one() {
  tag=$1; bin=$2
  log=/tmp/q4-ab-$tag.log
  rm -f "$log"
  ( cd "$RUNROOT" && timeout "$SECS" "$bin" --no-launcher ) >"$log" 2>&1
  code=$?
  flips=$(grep -a 'rsx: flip' "$log" 2>/dev/null | tail -1 | grep -oE 'flip [0-9]+' | grep -oE '[0-9]+')
  flips=${flips:-0}
  crashes=$(grep -ac 'crash: signal' "$log" 2>/dev/null); crashes=${crashes:-0}
  spu=$(grep -ac 'spu: thread ' "$log" 2>/dev/null); spu=${spu:-0}
  interp=$(grep -a 'compiled SPU blocks' "$log" 2>/dev/null | tail -1 | grep -oE '[0-9]+ compiled' | grep -oE '[0-9]+')
  printf '  %-22s exit=%-4s flips=%-6s crashes=%-3s spu_threads=%-3s %s\n' \
    "$tag" "$code" "$flips" "$crashes" "$spu" "${interp:-}"
  echo "$code $flips $crashes" > /tmp/q4-ab-$tag.num
}

echo "=== $GAME  A=nearest(pre-fix) B=truncate(fix)  ${PAIRS} pair(s) x ${SECS}s  $(date -u +%H:%M:%S)"
i=1
while [ "$i" -le "$PAIRS" ]; do
  echo "-- pair $i  $(date -u +%H:%M:%S)  load=$(cut -d' ' -f1 /proc/loadavg)"
  run_one "$GAME-A$i" "$A"
  run_one "$GAME-B$i" "$B"
  i=$((i+1))
done
echo "=== done $(date -u +%H:%M:%S)"
