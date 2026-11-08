#!/usr/bin/env bash
# Drive the verified world recipe by watching captures instead of guessing:
# press cross, screenshot, and let the caller read. Meant for one run dir.
#
#   tools/drive_world.sh <run-dir> <display> [max-seconds]
set -uo pipefail
RD="${1:?run dir}"; D="${2:?display}"; MAX="${3:-1500}"
CAP="$RD/caps"; mkdir -p "$CAP"
T0=$(date +%s)
press() { printf '%s' "$1" > "$RD/input.cmd"; sleep "${2:-6}"; }
shot() { local w; w=$(DISPLAY=$D xdotool search --name Minecraft 2>/dev/null | tail -1); [ -n "$w" ] && DISPLAY=$D import -window "$w" "$CAP/drv-$(date +%s).png" 2>/dev/null; }
log() { echo "$(( $(date +%s) - T0 ))s $*" >> "$RD/driver.log"; }

# wait for the first frame
for _ in $(seq 1 120); do
    grep -aq 'rsx: flip' "$RD/game.log" 2>/dev/null && break
    sleep 2
done
log "first frame"
sleep 20
press cross 8;  log "cross 1 (terms)"
press cross 8;  log "cross 2 (autosave)"
press cross 8;  log "cross 3 (menu)"
press cross 8;  log "cross 4 (start game)"
for i in 1 2 3 4 5 6; do press down 3; done
press cross 10; log "create"
# From here the autosave dialog recurs and eats presses: keep pressing.
while [ $(( $(date +%s) - T0 )) -lt "$MAX" ]; do
    press cross 20
    shot
    log "cross (loop)"
done
