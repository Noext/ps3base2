#!/bin/bash
# Piste G rebuild with the one flag Chromium needs:
#   -mllvm -wasm-use-legacy-eh=false
# clang's default emits the LEGACY EH proposal while libc++-mt-wasmexcept.a uses
# the NEW one; a module with both fails WebAssembly.compile() (measured by J).
#
# Only runtime TUs are recompiled; the lifted PPU objects carry no exception
# constructs, so they are reused from /tmp/out-G/obj.
set -euo pipefail
ROOT=/root/Dev/explore-G
OUT=/tmp/out-G2
EMSDK=/root/Dev/ps2-web-recomp/tools/emsdk/upstream
EMCC="$EMSDK/emscripten/emcc"
LIB="$EMSDK/emscripten/cache/sysroot/lib/wasm32-emscripten"

mkdir -p "$OUT/obj"
CF=(-Oz -std=c++20 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false
    -msimd128 -msse4.1 -mfma -matomics -mbulk-memory -pthread -w
    -I "$ROOT/runtime/include" -I "$ROOT/runtime/src"
    -I "$ROOT/runtime/src/lv2" -I "$ROOT/runtime/src/spu" -I "$ROOT/out/ppu"
    -I "$ROOT/third_party/stb" -I "$ROOT/third_party/imgui"
    -I "$ROOT/runtime/src/spu/renjob" -I "$ROOT/out/include"
    -I "$ROOT/third_party/volk" -I "$ROOT/third_party/Vulkan-Headers/include")
DEFS=(-DPS3BASE_BOOT_PROBE=1 -DPS3BASE_INPUT_BUILTIN=1 -DPS3BASE_NO_RENDERER=1
      -DPS3BASE_CALL_TARGET_CHECK=1 -DPS3BASE_BLOCK_TRACE=1 -DPS3BASE_HLE_OVER_LLE=1 -DPS3BASE_LD_TRAP=1)
# xargs runs a fresh shell, so the flag list travels as a NUL-safe string.
printf '%s\0' "${CF[@]}" > "$OUT/cf.bin"
printf '%s\0' "${DEFS[@]}" > "$OUT/defs.bin"

find "$ROOT/runtime/src" -name '*.cpp' \
     -not -path '*/launcher/*' -not -name 'main.cpp' \
     -not -name 'host_input.cpp' -not -name 'host.cpp.orig' \
     -not -path '*/jobsys/*' -not -path '*/gpu/edge*' -not -name 'vk_context.cpp' \
     -not -name 'rsx_renderer.cpp' -not -name 'presenter.cpp' -not -name 'motion_vectors.cpp' \
     -not -name 'shader_compiler.cpp' -not -name '*_adapters.cpp' -not -name 'renjob_whole.cpp' \
     -not -path '*/spu/anim/*' -not -path '*/spu/cloth/*' -not -path '*/spu/edge/*' \
     -not -path '*/spu/edgejob/*' -not -path '*/spu/meshhier/*' -not -path '*/spu/meshprep/*' \
     -not -path '*/spu/mpjob/*' -not -path '*/spu/pm/*' -not -path '*/spu/renjob/*' \
     -not -path '*/spu/scream/*' -not -path '*/spu/shadows/*' -not -path '*/spu/tasks/*' \
  > "$OUT/tus.txt"
echo "== $(wc -l < "$OUT/tus.txt") runtime TUs"

cat > "$OUT/one.sh" <<'ONE'
#!/bin/bash
set -e
f="$1"; ROOT="$2"; OUT="$3"; EMCC="$4"
mapfile -d '' CF < "$OUT/cf.bin"
mapfile -d '' DEFS < "$OUT/defs.bin"
o="$OUT/obj/$(echo "${f#$ROOT/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
[ -f "$o" ] && exit 0
"$EMCC" "${CF[@]}" "${DEFS[@]}" -c "$f" -o "$o"
ONE
chmod +x "$OUT/one.sh"

echo "== compile (6 at a time; the workspace caps any build at -j 8)"
xargs -a "$OUT/tus.txt" -P 6 -I{} "$OUT/one.sh" {} "$ROOT" "$OUT" "$EMCC"
echo "== objects: $(ls "$OUT"/obj/*.o | wc -l)"

echo "== link"
"$EMCC" "${CF[@]}" -nostdlib++ \
    -sPTHREAD_POOL_SIZE=4 -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=3221225472 \
    -sERROR_ON_UNDEFINED_SYMBOLS=0 \
    --preload-file /tmp/disk@/ \
    -o "$OUT/game.js" "$OUT"/obj/*.o /tmp/out-G/obj/ppu_*.o \
    "$LIB/libc++-mt-wasmexcept.a" "$LIB/libc++abi-mt-wasmexcept.a"
stat -c 'module: %s bytes' "$OUT/game.wasm"
