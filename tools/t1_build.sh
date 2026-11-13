#!/bin/bash
# T1 -- recompile the named files, then relink. NEVER links a stale object.
#
# WHY THIS EXISTS
# The previous relink script only linked. Twice in one session a compile failed
# (a missing include, an undeclared identifier), the script printed COMPILE_OK
# anyway because the failure was inside a pipeline, and the link then reused the
# PREVIOUS object. The resulting module had a different md5 and looked like a new
# build, so a measurement was attributed to a change that was not in the binary.
#
# This script compiles first, in its own step, and refuses to link if any object is
# older than its source. The fingerprints it prints are the ones a measurement must
# quote, and it proves the JS bridge actually got baked into game.js.
#
# usage: bash tools/t1_build.sh <tree> <outdir> [source.cpp ...]
set -euo pipefail

ROOT="${1:?tree}"
OUT="${2:?outdir}"
shift 2 || true
SOURCES=("$@")

EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
EMXX="$EMSDK/emscripten/em++"
EMCC="$EMSDK/emscripten/emcc"
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

DEFS=(-DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -DPS3BASE_PAD_PROBE=1
      -DPS3BASE_PAD_LOG=1 -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_RSXB_PROBE=1
      -DPS3BASE_BLOCK_TRACE=1 -DPS3BASE_LD_TRAP=1)

obj_for() {
    echo "$OUT/obj/$(echo "${1#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
}

if [ "${#SOURCES[@]}" -gt 0 ]; then
    echo "== compile ${#SOURCES[@]} file(s)"
    for f in "${SOURCES[@]}"; do
        src="$ROOT/$f"
        [ -f "$src" ] || { echo "MISSING SOURCE: $src" >&2; exit 2; }
        o="$(obj_for "$src")"
        echo "cc $f"
        if ! "$EMCC" "${CF[@]}" "${PORT[@]}" "${DEFS[@]}" -c "$src" -o "$o"; then
            echo "COMPILE FAILED: $f -- refusing to link" >&2
            exit 3
        fi
        [ -f "$o" ] || { echo "COMPILE produced no object: $o" >&2; exit 3; }
        if [ "$o" -ot "$src" ]; then
            echo "STALE OBJECT: $o is older than $src -- refusing to link" >&2
            exit 4
        fi
    done
    echo "== compile OK"
else
    echo "== no sources given: relink only"
fi

echo "== verify no stale object"
stale=0
for o in "$OUT"/obj/*.o; do
    b="$(basename "$o" .o)"
    src="$ROOT/$(echo "$b" | sed 's|_|/|g').cpp"
    if [ -f "$src" ] && [ "$o" -ot "$src" ]; then
        echo "STALE: $o older than $src" >&2
        stale=1
    fi
done
if [ "$stale" -ne 0 ]; then
    echo "REFUSING TO LINK: stale objects present (see above)" >&2
    exit 4
fi

echo "== link (objects: $(ls "$OUT"/obj/*.o | wc -l))"
"$EMXX" "${CF[@]}" "${PORT[@]}" -nostdlib++ ${PS3BASE_NAMED:+-g} \
    -sPTHREAD_POOL_SIZE=24 -sPTHREAD_POOL_SIZE_STRICT=2 -sPROXY_TO_PTHREAD=1 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 \
    -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createPs3base \
    -sENVIRONMENT=web -sALLOW_BLOCKING_ON_MAIN_THREAD=0 \
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 \
    -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32 \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_ps3_pad_words_address,_ps3_pad_connected_address,_rsx_ring_address,_rsx_ring_capacity,_rsx_ring_control,_rsx_ring_word,_rsx_ring_stat,_rsx_ring_tee,_r3_draws,_r3_skipped,_r3_skip_reasons,_r3_n_method_get,_r3_n_enabled_false_get,_t1_replay_frame,_t1_replay_stats,_t1_replay_stat_offset,_t1_replay_adopt,_t1_replay_reset,_t1_replay_ready,_t1_replay_capture,_t1_replay_capture_request,_t1_replay_capture_now \
    --js-library "$ROOT/web/s1-bridge.js" \
    --preload-file "$DISK@/" \
    -o "$OUT/game.js" "$OUT"/obj/*.o \
    "$LIB/libc++-mt-wasmexcept.a" "$LIB/libc++abi-mt-wasmexcept.a"

echo "== FINGERPRINTS (quote these in every measurement)"
printf 'wasm    %s  %s bytes\n' "$(md5sum "$OUT/game.wasm" | cut -c1-16)" "$(stat -c %s "$OUT/game.wasm")"
printf 'game.js %s  %s bytes  (the JS bridge is BAKED IN at link)\n' "$(md5sum "$OUT/game.js" | cut -c1-16)" "$(stat -c %s "$OUT/game.js")"
printf 'bridge  %s  web/s1-bridge.js (source, baked into game.js above)\n' "$(md5sum "$ROOT/web/s1-bridge.js" | cut -c1-16)"
if grep -q 's1_canvas_configure' "$OUT/game.js"; then
    echo "bridge: PRESENT in game.js"
else
    echo "bridge: MISSING from game.js -- the link did not take it" >&2
    exit 5
fi
printf 'data    %s bytes\n' "$(stat -c %s "$OUT/game.data")"
