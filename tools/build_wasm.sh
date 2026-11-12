#!/bin/bash
# Build the milestone-3 wasm module reproducibly.
#
# Everything here was validated round by round; the comments record the measured
# reason for each flag so a change that looks harmless is not silently made.
set -euo pipefail

# Compiles one file at a time on purpose. This machine is shared and the
# workspace rules cap any build at -j 8; a sequential script cannot violate that
# from a shell that does not know the rule. It costs roughly 40 minutes for the
# 242 translation units and needs no babysitting.

ROOT="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"  # the tree this script lives in
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
CXXFLAGS=(-Oz -std=c++20 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -msimd128 -msse4.1 -mfma
          -matomics -mbulk-memory -pthread -w
          -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
          -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu"
          -I "$ROOT/third_party/stb" -I "$ROOT/third_party/imgui"
          -I "$ROOT/runtime/src/spu/renjob" -I "$ROOT/out/include"
          -I "$ROOT/third_party/volk" -I "$ROOT/third_party/Vulkan-Headers/include")

echo "== compile runtime"
find "$ROOT/runtime/src" -name '*.cpp' \
     -not -path '*/launcher/*' -not -name 'main.cpp' \
     -not -name 'host_input.cpp' -not -name 'host.cpp.orig' \
     -not -path '*/jobsys/*' -not -path '*/gpu/edge*' -not -name 'vk_context.cpp' \
     -not -name 'rsx_renderer.cpp' -not -name 'presenter.cpp' -not -name 'motion_vectors.cpp' \
     -not -name 'shader_compiler.cpp' -not -name '*_adapters.cpp' -not -name 'renjob_whole.cpp' \
     -not -path '*/spu/anim/*' \
     -not -path '*/spu/cloth/*' \
     -not -path '*/spu/edge/*' \
     -not -path '*/spu/edgejob/*' \
     -not -path '*/spu/meshhier/*' \
     -not -path '*/spu/meshprep/*' \
     -not -path '*/spu/mpjob/*' \
     -not -path '*/spu/pm/*' \
     -not -path '*/spu/renjob/*' \
     -not -path '*/spu/scream/*' \
     -not -path '*/spu/shadows/*' \
     -not -path '*/spu/tasks/*' | while read -r f; do
    o="$OUT/obj/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    "$EMCC" "${CXXFLAGS[@]}" -DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -DPS3BASE_NO_RENDERER=1 -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_RSXB_PROBE=1 -DPS3BASE_RSXB_PROBE=1 -DPS3BASE_BLOCK_TRACE=1 -DPS3BASE_LD_TRAP=1 -c "$f" -o "$o"
done

# stb_image: the HLE image decoders (cellPngDec/cellJpgDec) call it. The runtime
# compile above excludes */launcher/*, so without this line the three symbols
# fall through to the null stubs in ps3base_wasm_stubs.cpp and every decodable
# PNG is refused -- measured: native Open 190x170 RGBA, wasm 0x80611204.
"$EMCC" "${CXXFLAGS[@]}" -DPS3BASE_NO_RENDERER=1 -c "$ROOT/runtime/src/launcher/stb_image_impl.cpp" -o "$OUT/obj/runtime_src_launcher_stb_image_impl.o"

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
    # The lifted code needs the same defines as the runtime: bc_cond is inline in
    # ops.h, so the block trace only exists if THIS translation unit sees the flag.
    "$EMCC" "${CXXFLAGS[@]}" -DPS3BASE_NO_RENDERER=1 -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_BLOCK_TRACE=1 -c "$f" -o "$o"
done

echo "== link"
# PS3BASE_NAMED=1 keeps the name section so a bare "unreachable" can be
# attributed to a function instead of guessed from wasm-function indices.
"$EMCC" "${CXXFLAGS[@]}" -nostdlib++ ${PS3BASE_NAMED:+-g} \
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
