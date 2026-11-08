#!/bin/bash
# drive43.sh -- piste 43 driver: reach the world list, open a save's detail
# panel, and press Load, capturing between every press.
set -u
DISP="${DISP:?set DISP}"
CAP="${CAP:?set CAP}"
mkdir -p "$CAP"
shot() { local w; w=$(DISPLAY="$DISP" timeout 3 xdotool search --name 'Minecraft' 2>/dev/null | tail -1)
  [ -n "$w" ] && DISPLAY="$DISP" import -window "$w" "$CAP/$1.png" 2>/dev/null && echo "  shot $1"; }
press() { xdotool keydown "$1"; sleep 0.25; xdotool keyup "$1"; sleep "${2:-2.5}"; }
export DISPLAY="$DISP"
press z 3; shot 01-terms
press z 3; shot 02-autosave
press z 3; shot 03-mainmenu
press z 4; shot 04-startgame
# Start Game list: first entry is Create New World; go down to an existing save
press Down; press Down; press Down 3; shot 05-selected
press z 2; shot 06-detail
sleep 1; shot 07-detail-settled
press z 6; shot 08-after-load
sleep 6; shot 09-after-load-2
