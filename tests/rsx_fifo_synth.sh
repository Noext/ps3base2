#!/bin/bash
# Build and run tests/rsx_fifo_synth.cpp as WebAssembly.
#
# The test links the REAL runtime/src/lv2/rsx.cpp -- the FIFO walker and every
# method handler -- plus the no-display GPU stub, so what executes is the same
# code the game's command stream goes through, not a reimplementation.
#
# One build at a time, -j 8 is irrelevant here (sequential), and the out
# directory is per-task so two tracks never share objects.
set -euo pipefail
ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
OUT="${2:-/tmp/out-C}"
EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
CXX="$EMSDK/emscripten/em++"

FLAGS=(-O1 -std=c++20 -msimd128 -msse4.1 -mfma
       -matomics -mbulk-memory -pthread -w
       -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
       -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu"
       -I "$ROOT/out/ppu" -I "$ROOT/out/include"
       -DPS3BASE_NO_RENDERER=1)

mkdir -p "$OUT/obj"

# The runtime files the RSX command path actually needs. Anything not listed is
# absent on purpose: a smaller link is what makes the failure, if any, name the
# one missing piece.
RT=(
  lv2/rsx.cpp gpu/rsx_no_display.cpp gpu/rsx_special_methods.cpp
  memory.cpp regions.cpp mem.cpp reservation.cpp codewatch.cpp
  backtrace_port.cpp host.cpp log.cpp cycles.cpp trace.cpp critpath.cpp
  placement.cpp rdcapture.cpp hostsleep.cpp ppu_ops.cpp ppu_runtime.cpp
  lv2/object.cpp lv2/event.cpp fps_unlock.cpp
)

echo "== compile runtime"
for f in "${RT[@]}"; do
  o="$OUT/obj/$(echo "$f" | tr '/' '_' | sed 's/\.cpp$/.o/')"
  if [ ! -f "$o" ] || [ "$ROOT/runtime/src/$f" -nt "$o" ]; then
    "$CXX" "${FLAGS[@]}" -c "$ROOT/runtime/src/$f" -o "$o"
  fi
done

echo "== compile test"
"$CXX" "${FLAGS[@]}" -c "$ROOT/tests/rsx_fifo_synth.cpp" -o "$OUT/obj/rsx_fifo_synth.o"
"$CXX" "${FLAGS[@]}" -c "$ROOT/tests/rsx_fifo_synth_support.cpp" -o "$OUT/obj/rsx_fifo_synth_support.o"

echo "== link"
"$CXX" "${FLAGS[@]}" -sPTHREAD_POOL_SIZE=8 -sALLOW_MEMORY_GROWTH=1 \
  -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 -sEXIT_RUNTIME=1 \
  -o "$OUT/rsx_fifo_synth.js" "$OUT"/obj/*.o

echo "== run"
(cd "$OUT" && node "$OUT/rsx_fifo_synth.js")
