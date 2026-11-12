#!/usr/bin/env bash
# PISTE I oracle driver: run the native binary and press through the screens.
set -uo pipefail
BIN=/root/Dev/ps3base2/build/ps3base
DISC=/root/Dev/mc-port/disc
RD=/tmp/I/oracle2
cd /root/Dev/ps3base2
rm -rf "$RD"; mkdir -p "$RD/caps" "$RD/frames"
: > "$RD/input.cmd"
t0=$(date +%s)
log() { echo "$(( $(date +%s) - t0 ))s $*" >> "$RD/driver.log"; }

env DISPLAY=:0 XDG_RUNTIME_DIR=/run/user/0 PS3BASE_DISC_DIR="$DISC" PS3BASE_LOG=info \
    PS3BASE_DUMP_FRAMES=300 PS3BASE_FRAME_DIR="$RD/frames" \
    PS3BASE_INPUT_CMD="$RD/input.cmd" \
    timeout -s INT 200 "$BIN" --no-launcher > "$RD/game.log" 2>&1 &
GAME=$!
log "started game pid=$GAME"

press() { printf '%s' "$1" > "$RD/input.cmd"; log "press $1"; sleep "${2:-6}"; }
shot() {
    w=$(DISPLAY=:0 xdotool search --name Minecraft 2>/dev/null | tail -1)
    [ -n "${w:-}" ] && DISPLAY=:0 import -window "$w" "$RD/caps/drv-$(date +%s).png" 2>/dev/null
    log "shot (window=${w:-none})"
}

for _ in $(seq 1 120); do
    grep -aq 'rsx: flip' "$RD/game.log" 2>/dev/null && break
    sleep 2
done
log "first frame"
sleep 15
press cross 8;  shot; log "cross 1 (terms)"
press cross 8;  shot; log "cross 2 (autosave)"
press cross 8;  shot; log "cross 3 (menu?)"
press cross 8;  shot; log "cross 4 (start game?)"
for i in 1 2 3; do press down 3; shot; done
press cross 10; shot; log "cross 5"
for i in $(seq 1 20); do press cross 15; done
log "end of scripted presses"
wait $GAME; log "game exit=$?"