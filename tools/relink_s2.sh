#!/bin/bash
# S2 -- recompile ONLY the TUs whose input changed, then relink. The full
# tools/build_wasm.sh recompiles 242 translation units (~40 min); every
# measurement after the first uses this path.
# usage: bash tools/relink_s2.sh <tree> <outdir>
set -euo pipefail
ROOT="$1"
OUT="$2"
EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
EMCC="$EMSDK/emscripten/emcc"
EMPP="$EMSDK/emscripten/em++"
LIB="$EMSDK/emscripten/cache/sysroot/lib/wasm32-emscripten"
DISK=${DISK:-/tmp/disk}

CF=(-Oz -std=c++20 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false
    -msimd128 -msse4.1 -mfma -matomics -mbulk-memory -pthread -w
    -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
    -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu"
    -I "$ROOT/third_party/stb" -I "$ROOT/third_party/imgui"
    -I "$ROOT/runtime/src/spu/renjob" -I "$ROOT/out/include"
    -I "$ROOT/third_party/volk" -I "$ROOT/third_party/Vulkan-Headers/include")
EXTRA_DEFS=(${EXTRA_DEFS:-})
DEFS=(-DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -DPS3BASE_NO_RENDERER=1
      -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_RSXB_PROBE=1 -DPS3BASE_BLOCK_TRACE=1
      -DPS3BASE_LD_TRAP=1)

for f in "$ROOT/runtime/src/gpu/rsx_ring.cpp" \
         "$ROOT/runtime/src/gpu/rsx_ring_glue.cpp" \
         "$ROOT/runtime/src/gpu/gcm_fifo.cpp" \
         "$ROOT/runtime/src/lv2/rsx.cpp"; do
    o="$OUT/obj/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    echo "cc $(basename $f)"
    "$EMCC" "${CF[@]}" "${DEFS[@]}" "${EXTRA_DEFS[@]}" -c "$f" -o "$o"
done

echo "== link"
"$EMPP" "${CF[@]}" -nostdlib++ --use-port=emdawnwebgpu \
    -sPTHREAD_POOL_SIZE=24 -sPTHREAD_POOL_SIZE_STRICT=2 -sPROXY_TO_PTHREAD=1 \
    -sALLOW_MEMORY_GROWTH=1 -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPs3base \
    -sENVIRONMENT=web,node -sALLOW_BLOCKING_ON_MAIN_THREAD=0 \
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
    -sSTACK_SIZE=8388608 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8 \
    -sEXPORTED_FUNCTIONS=_main,_rsx_ring_tee,_rsx_ring_address,_rsx_ring_capacity,_rsx_ring_control,_rsx_ring_word,_rsx_ring_poll,_rsx_ring_poll_size,_rsx_ring_poll_data,_rsx_ring_stat,_r3_draws,_r3_skipped,_r3_skip_reasons \
    --preload-file "$DISK@/" \
    -o "$OUT/game.js" "$OUT"/obj/*.o \
    "$LIB/libc++-mt-wasmexcept.a" "$LIB/libc++abi-mt-wasmexcept.a"

stat -c 'module: %s bytes' "$OUT/game.wasm"
