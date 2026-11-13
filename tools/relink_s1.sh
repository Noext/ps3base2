#!/bin/bash
# Relink the S1 module from objects already compiled.
#
# The compile sweep is ~40 minutes; relinking is ~1 minute. Every measurement after
# the first uses this, so a change to the JS side (the bridge, the vendored port's
# proxy annotations, the page) can be tested without recompiling 200 TUs.
#
# usage: bash tools/relink_s1.sh <tree> <outdir>
set -euo pipefail
ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
OUT="${2:-/tmp/out-S1}"
EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
EMCC="$EMSDK/emscripten/em++"
LIB="$EMSDK/emscripten/cache/sysroot/lib/wasm32-emscripten"
DISK="${DISK:-/tmp/disk-r4}"

CF=(-Oz -std=c++20 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false
    -msimd128 -msse4.1 -mfma -matomics -mbulk-memory -pthread -w
    -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
    -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu"
    -I "$ROOT/third_party/stb" -I "$ROOT/third_party/imgui"
    -I "$ROOT/runtime/src/spu/renjob" -I "$ROOT/out/include"
    -I "$ROOT/third_party/volk" -I "$ROOT/third_party/Vulkan-Headers/include")

PORTFILE="$ROOT/third_party/emdawnwebgpu/emdawnwebgpu_pkg/emdawnwebgpu.port.py"
PORT=(--use-port="$PORTFILE")

echo "== link (objects: $(ls "$OUT"/obj/*.o | wc -l))"
"$EMCC" "${CF[@]}" "${PORT[@]}" -nostdlib++ ${PS3BASE_NAMED:+-g} \
    -sPTHREAD_POOL_SIZE=24 -sPTHREAD_POOL_SIZE_STRICT=2 -sPROXY_TO_PTHREAD=1 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPs3base \
    -sENVIRONMENT=web -sALLOW_BLOCKING_ON_MAIN_THREAD=0 \
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8 \
    -sEXPORTED_FUNCTIONS=_main,_ps3_pad_words_address,_ps3_pad_connected_address,_rsx_ring_address,_rsx_ring_capacity,_rsx_ring_control,_rsx_ring_word,_rsx_ring_stat,_rsx_ring_tee,_r3_draws,_r3_skipped,_r3_skip_reasons,_r3_n_method_get,_r3_n_enabled_false_get \
    --js-library "$ROOT/web/s1-bridge.js" \
    --preload-file "$DISK@/" \
    -o "$OUT/game.js" "$OUT"/obj/*.o \
    "$LIB/libc++-mt-wasmexcept.a" "$LIB/libc++abi-mt-wasmexcept.a"

echo "== report"
stat -c 'module: %s bytes' "$OUT/game.wasm"
echo -n 'md5: '; md5sum "$OUT/game.wasm" | cut -c1-16
ls -la "$OUT/game.data"
