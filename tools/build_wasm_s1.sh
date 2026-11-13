#!/bin/bash
# Build the S1 wasm module: the emulation WITH the real RSX renderer linked, the
# WebGPU backend that adopts a device created by JS, and the ring producer.
#
# Every difference from tools/build_wasm.sh is here with the measurement behind
# it, because each one is a way a build can link and paint nothing:
#
#   * rsx_renderer.cpp IS compiled. The base script excludes it and defines
#     PS3BASE_NO_RENDERER, which is right for a build that only needs the guest
#     to boot. This one exists to draw.
#   * wgpu_backend.cpp IS compiled, and it is the NATIVE pixel-exact backend
#     (RMS 0.0000 against Vulkan on the 4J logo, sha 46422dcc / 46422dcc).
#     Only the device acquisition and the shader source differ on the web.
#   * --use-port=emdawnwebgpu supplies webgpu.h and the JS bridge to the browser.
#   * -sSTACK_SIZE=8MB: the WGSL emitter needs more than Emscripten's 64 KB
#     default. Measured by R3: with 64 KB the module dies with "memory access
#     out of bounds" on the first vertex emit; with 8 MB, draws=43 and the skip
#     list is empty.
#   * The ring and renderer diagnostics are exported so a run can be measured
#     instead of described.
set -euo pipefail

ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
OUT="${2:-/tmp/out-S1}"
EMSDK=${EMSDK:-/root/Dev/ps2-web-recomp/tools/emsdk/upstream}
EMCC="$EMSDK/emscripten/emcc"
LIB="$EMSDK/emscripten/cache/sysroot/lib/wasm32-emscripten"
DISK="${DISK:-/tmp/disk-r4}"

mkdir -p "$OUT/obj"

CXXFLAGS=(-Oz -std=c++20 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -msimd128 -msse4.1 -mfma
          -matomics -mbulk-memory -pthread -w
          -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
          -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu"
          -I "$ROOT/third_party/stb" -I "$ROOT/third_party/imgui"
          -I "$ROOT/runtime/src/spu/renjob" -I "$ROOT/out/include"
          -I "$ROOT/third_party/volk" -I "$ROOT/third_party/Vulkan-Headers/include")

# The VENDORED port, not the one in the emsdk cache. It is the same package with
# one deliberate change: the JS entries on the render path carry
# __proxy:'sync', so a call from a guest PTHREAD runs on the realm that owns the
# device and blocks the caller. Measured: without it the pthread gets 'undefined'
# from wgpuDeviceCreateCommandEncoder and nothing is ever drawn; with it, a
# pthread clears the canvas and the pixel is exact. The port is text-only (14 KB),
# so it is vendored rather than patched in a cache the next emsdk update wipes.
PORTFILE="$ROOT/third_party/emdawnwebgpu/emdawnwebgpu_pkg/emdawnwebgpu.port.py"
if [ -f "$PORTFILE" ]; then PORT=(--use-port="$PORTFILE"); else PORT=(--use-port=emdawnwebgpu); fi

DEFS=(-DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -DPS3BASE_PAD_PROBE=1
      -DPS3BASE_PAD_LOG=1 -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_RSXB_PROBE=1
      -DPS3BASE_BLOCK_TRACE=1 -DPS3BASE_LD_TRAP=1)

echo "== compile runtime"
find "$ROOT/runtime/src" -name '*.cpp' \
     -not -path '*/launcher/*' -not -name 'main.cpp' \
     -not -name 'host_input.cpp' -not -name 'host.cpp.orig' \
     -not -path '*/jobsys/*' -not -path '*/gpu/edge*' -not -name 'vk_context.cpp' \
     -not -name 'presenter.cpp' -not -name 'motion_vectors.cpp' \
     -not -name 'shader_compiler.cpp' -not -name '*_adapters.cpp' -not -name 'renjob_whole.cpp' \
     -not -path '*/spu/anim/*' -not -path '*/spu/cloth/*' -not -path '*/spu/edge/*' \
     -not -path '*/spu/edgejob/*' -not -path '*/spu/meshhier/*' -not -path '*/spu/meshprep/*' \
     -not -path '*/spu/mpjob/*' -not -path '*/spu/pm/*' -not -path '*/spu/renjob/*' \
     -not -path '*/spu/scream/*' -not -path '*/spu/shadows/*' -not -path '*/spu/tasks/*' \
     | while read -r f; do
    o="$OUT/obj/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    echo "cc $(basename "$f")"
    "$EMCC" "${CXXFLAGS[@]}" "${PORT[@]}" "${DEFS[@]}" -c "$f" -o "$o"
done

# stb_image: the HLE image decoders (cellPngDec/cellJpgDec) call it. Excluded from
# the runtime sweep by the launcher/ filter; without it every PNG is refused with
# 0x80611204 (measured in the base tree).
"$EMCC" "${CXXFLAGS[@]}" "${PORT[@]}" "${DEFS[@]}" \
    -c "$ROOT/runtime/src/launcher/stb_image_impl.cpp" -o "$OUT/obj/runtime_src_launcher_stb_image_impl.o"

echo "== compile lifted PPU"
if [ -n "${PROFILE:-}" ] && [ -f "$PROFILE" ]; then
    python3 "$ROOT/tools/filter_table.py" "$PROFILE" "$OUT/table_filtered.cpp"
    TABLE="$OUT/table_filtered.cpp"
else
    TABLE="$ROOT/out/ppu/table.cpp"
fi
"$EMCC" "${CXXFLAGS[@]}" "${PORT[@]}" -c "$TABLE" -o "$OUT/obj/ppu_table.o"
for f in "$ROOT"/out/ppu/*.cpp; do
    case "$f" in *table*) continue;; esac
    o="$OUT/obj/ppu_$(basename "$f" .cpp).o"
    "$EMCC" "${CXXFLAGS[@]}" "${PORT[@]}" -DPS3BASE_PAD_PROBE=1 -DPS3BASE_CALL_TARGET_CHECK=1 \
        -DPS3BASE_BLOCK_TRACE=1 -c "$f" -o "$o"
done

echo "== link"
# -sSTACK_SIZE=8388608: the WGSL emitter overflows the 64 KB default (measured by
# R3: 64 KB -> "memory access out of bounds" on the first vertex emit; 8 MB -> the
# decode runs to the end and draws are recorded).
"$EMCC" "${CXXFLAGS[@]}" "${PORT[@]}" -nostdlib++ ${PS3BASE_NAMED:+-g} \
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
md5sum "$OUT/game.wasm" | cut -c1-16
ls -la "$OUT/game.data"
