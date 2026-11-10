#!/bin/bash
# Build the milestone-3 wasm module reproducibly.
#
# Everything here was validated round by round; the comments record the measured
# reason for each flag so a change that looks harmless is not silently made.
set -euo pipefail

ROOT="${1:-/root/Dev/ps3base2}"
OUT="${2:-/tmp/wasm-out}"
EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
EMCC="$EMSDK/emscripten/emcc"
LIB="$EMSDK/emscripten/cache/sysroot/lib/wasm32-emscripten"
PROFILE="${PROFILE:-}"          # optional coverage file: one hex address per line
DISK="${DISK:-/tmp/disk}"       # disc image tree mounted at /

mkdir -p "$OUT/obj"

# Flags measured necessary (prompt item 3, re-verified here):
#   -msimd128 -msse4.1 -mfma   __m128/__m128i and the SSE intrinsics do not compile without them
#   -fwasm-exceptions          the runtime uses C++ exceptions
#   -matomics -mbulk-memory    requires --shared-memory; every object needs them or the link fails
#   -pthread                   the title spawns one host thread per guest thread
CXXFLAGS=(-Oz -std=c++20 -fwasm-exceptions -msimd128 -msse4.1 -mfma
          -matomics -mbulk-memory -pthread -w
          -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
          -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu")

echo "== compile runtime"
find "$ROOT/runtime/src" -name '*.cpp' | while read -r f; do
    o="$OUT/obj/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    "$EMCC" "${CXXFLAGS[@]}" -DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -c "$f" -o "$o"
done

echo "== compile lifted PPU"
# The table is filtered when a coverage profile is supplied: 92% of the linked
# functions are never executed (measured: 1,775 of 22,685), and the filtered
# module links at 50 MB instead of 92 MB.
if [ -n "$PROFILE" ] && [ -f "$PROFILE" ]; then
    python3 "$ROOT/tools/filter_table.py" "$PROFILE" "$OUT/table_filtered.cpp"
    TABLE="$OUT/table_filtered.cpp"
else
    TABLE="$ROOT/out/ppu/table.cpp"
fi
"$EMCC" "${CXXFLAGS[@]}" -c "$TABLE" -o "$OUT/obj/ppu_table.o"
for f in "$ROOT"/out/ppu/*.cpp; do
    case "$f" in *table*) continue;; esac
    o="$OUT/obj/ppu_$(basename "$f" .cpp).o"
    "$EMCC" "${CXXFLAGS[@]}" -c "$f" -o "$o"
done

echo "== link"
"$EMCC" "${CXXFLAGS[@]}" -nostdlib++ \
    -sPTHREAD_POOL_SIZE=4 -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 \
    --preload-file "$DISK@/" \
    -o "$OUT/game.js" "$OUT"/obj/*.o \
    "$LIB/libc++-mt-wasmexcept.a" "$LIB/libc++abi-mt-wasmexcept.a"

echo "== report"
stat -c 'module: %s bytes' "$OUT/game.wasm"
python3 "$ROOT/tools/wasm_sizes.py" "$OUT/game.wasm"
echo "run: node --experimental-wasm-exnref $OUT/game.js"
