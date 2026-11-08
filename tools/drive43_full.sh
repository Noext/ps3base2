#!/bin/bash
# drive43_full.sh -- run the game AND drive it, entirely detached. No tool call
# has to stay open, so a timed-out tool call can never take the tree with it.
set -u
DISP="${DISP:-:20}"; CAP="${CAP:-/tmp/run-world-43/caps43}"; RD="${RD:-/tmp/run-world-43}"
mkdir -p "$CAP"
export DISPLAY="$DISP"
log(){ echo "[$(date +%T)] $*" >> "$RD/driver.log"; }
shot(){ local w; w=$(xdotool search --name Minecraft 2>/dev/null | tail -1)
  [ -n "$w" ] && import -window "$w" "$CAP/$1.png" 2>/dev/null; }
press(){ xdotool keydown "$1"; sleep 0.35; xdotool keyup "$1"; sleep "${2:-3}"; }
: > "$RD/driver.log"
log "waiting for window"
for i in $(seq 1 120); do [ -n "$(xdotool search --name Minecraft 2>/dev/null | tail -1)" ] && break; sleep 2; done
sleep 45
log "startup screens"
for i in 1 2 3 4; do press z 5; shot "a$i"; done
log "Play Game"
press z 6; shot b1
log "world list -> select save"
press Down 1.5; press Down 1.5; shot c1
log "open detail"
press z 4; shot d1
log "Load"
press z 6; shot e1
for i in $(seq 1 60); do sleep 10; shot "load$i"; done
log done

