#!/bin/bash
# drive32.sh -- press-by-press driver for piste 32 (load an existing world).
# Each press is written to the run's input.cmd (consumed on read) and followed
# by a capture, so a transition that shifts by a second is visible instead of
# assumed. Used with a per-run DISPLAY, never :0.
set -u
RD="${RD:-/tmp/run-world-32}"
DISP="${DISP:?set DISP}"
CAP="$RD/caps"
mkdir -p "$CAP"

shot() {
    local name="$1"
    local win
    win=$(DISPLAY="$DISP" timeout 3 xdotool search --name 'Minecraft' 2>/dev/null | tail -1)
    if [ -n "$win" ]; then
        DISPLAY="$DISP" import -window "$win" "$CAP/$name.png" 2>/dev/null && echo "  shot $CAP/$name.png"
    else
        echo "  shot $name: no window"
    fi
}

press() {
    local b="$1" hold="${2:-150}"
    printf '%s %s' "$b" "$hold" > "$RD/input.cmd"
    echo "press $b (hold ${hold}ms)"
    sleep 2
}

press_wait() {
    local b="$1" secs="${2:-6}"
    press "$b"
    sleep "$secs"
}

echo "driver start $(date +%T)"
"$@"
