#!/bin/bash
# T1 run: assemble the web dir from the S1 build and capture the canvas.
# One instance, under flock, no relaunch loop (AGENTS.md / handoff rule 2).
set -uo pipefail
ROOT=/root/Dev/t-T1
OUT=/tmp/out-T1
WEB=/tmp/out-T1/web
PORT=${PORT:-8991}

if pgrep -f 'ps3base --no-launcher' >/dev/null; then
  echo "REFUSING: a native instance is running"; exit 1
fi

rm -rf "$WEB"; mkdir -p "$WEB"
cp "$ROOT"/web/game.html "$ROOT"/web/s1-bridge.js "$ROOT"/web/audio-worklet.js "$ROOT"/web/serve.py "$WEB"/
cp "$OUT"/game.js "$OUT"/game.wasm "$OUT"/game.data "$WEB"/
echo "== web dir assembled"; ls -la "$WEB" | head

echo "== md5 provenance"
md5sum "$WEB/game.wasm" | cut -c1-16

echo "== serving on $PORT"
cd "$WEB" && exec python3 serve.py "$PORT"
