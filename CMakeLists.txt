cmake_minimum_required(VERSION 3.28)
project(ps3base LANGUAGES C CXX)

# ps3base v2 -- a generic static-recompilation runtime for PS3 titles.
#
# This tree was forked from GoWA-pc and hollowed out: everything that was about
# God of War lives behind game/profile.* and is only compiled when a profile
# says so. What remains is the generic machine -- PPU/SPU execution, the LV2
# kernel, the VFS, the RSX front end -- and the generated code for whatever
# title you point it at.
#
# The two inputs are produced from your own files by tools/setup.py:
#   PS3BASE_GAME_DIR  the decrypted executable and its images
#   PS3BASE_FW_DIR    the decrypted firmware modules
# Neither is tracked. When they are absent configuration stops with a message
# that names what is missing, rather than failing later in the build.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
foreach(lang C CXX)
    string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
endforeach()

set(PS3BASE_GENERATED_DIR "${CMAKE_SOURCE_DIR}/out/ppu" CACHE PATH "Directory with generated PPU sources")
set(PS3BASE_GENERATED_OPT "-O2" CACHE STRING "Optimization level for generated code")
set(PS3BASE_SPU_OPT "-O2" CACHE STRING "Optimization level for generated SPU code")

if(MSVC)
    set(PS3BASE_CLANG /clang:)
    set(PS3BASE_EH /EHsc)
    set(PS3BASE_NOEH /GR- /EHs-c-)
else()
    set(PS3BASE_CLANG "")
    set(PS3BASE_EH "")
    set(PS3BASE_NOEH -fno-rtti -fno-exceptions)
endif()
set(PS3BASE_ARCH_FLAGS ${PS3BASE_CLANG}-march=x86-64-v3 ${PS3BASE_CLANG}-ffp-contract=off ${PS3BASE_CLANG}-fno-strict-aliasing)

# codewatch.cpp is the write-side guard that rsv::store_range calls into. It has
# to live in the same library: the small test binaries link reservation.cpp and
# would otherwise see an undefined reference to the guard's entry point.
add_library(ps3base_ppu_ops STATIC runtime/src/ppu_ops.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/log.cpp runtime/src/host.cpp)
target_include_directories(ps3base_ppu_ops PUBLIC runtime/include "${CMAKE_SOURCE_DIR}/out/include")
target_compile_options(ps3base_ppu_ops PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})

file(GLOB GOWA_RUNTIME_SOURCES CONFIGURE_DEPENDS runtime/src/*.cpp runtime/src/lv2/*.cpp runtime/src/hle/*.cpp runtime/src/media/*.cpp runtime/src/spu/*.cpp runtime/src/spu/edge/*.cpp runtime/src/spu/renjob/*.cpp runtime/src/spu/scream/*.cpp runtime/src/spu/cloth/*.cpp runtime/src/spu/shadows/*.cpp runtime/src/spu/meshprep/*.cpp runtime/src/spu/pm/*.cpp runtime/src/spu/edgejob/*.cpp runtime/src/spu/meshhier/*.cpp runtime/src/spu/anim/*.cpp runtime/src/spu/tasks/*.cpp runtime/src/spu/mpjob/*.cpp runtime/src/jobsys/*.cpp runtime/src/rsx/*.cpp runtime/src/gpu/*.cpp)
list(REMOVE_ITEM GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/ppu_ops.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/main.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/reservation.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/log.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/host.cpp")

set(PS3BASE_SPU_NATIVE_GEN_DIR "${CMAKE_SOURCE_DIR}/out/gen/spu" CACHE PATH "Directory with generated native SPU sources (tools/setup.py)")
set(GOWA_SPU_ROUNDING_SUBDIRS edge renjob scream cloth shadows meshprep jwhole)
# The generated code and the game's own images are produced from the user's
# files and are never tracked, so their absence is a configuration error that
# has to name what is missing. A GLOB over an empty directory would otherwise
# yield a library with no sources and a link error hundreds of lines later.
if(NOT EXISTS "${PS3BASE_GENERATED_DIR}/table.cpp")
    message(FATAL_ERROR
        "No generated PPU code in ${PS3BASE_GENERATED_DIR}.\n"
        "These sources come from your own disc and are never tracked:\n"
        "  python3 tools/setup.py --steps ppu --elf <decrypted title.elf>\n"
        "See docs/setup.md for the full sequence.")
endif()
if(NOT EXISTS "${PS3BASE_SPU_NATIVE_GEN_DIR}/generated.txt")
    message(FATAL_ERROR
        "No generated SPU natives in ${PS3BASE_SPU_NATIVE_GEN_DIR}.\n"
        "They come from your own SPU blocks:\n"
        "  python3 tools/setup.py --steps spu --elf <decrypted title.elf>\n"
        "A title with no SPU natives can still build: create the stamp with an\n"
        "empty file to say so explicitly.")
endif()
file(GLOB GOWA_SPU_GEN_SUBDIRS LIST_DIRECTORIES true RELATIVE "${PS3BASE_SPU_NATIVE_GEN_DIR}" "${PS3BASE_SPU_NATIVE_GEN_DIR}/*")
set(GOWA_SPU_GEN_SOURCES)
foreach(sub ${GOWA_SPU_GEN_SUBDIRS})
    if(NOT IS_DIRECTORY "${PS3BASE_SPU_NATIVE_GEN_DIR}/${sub}")
        continue()
    endif()
    file(GLOB gen_srcs CONFIGURE_DEPENDS "${PS3BASE_SPU_NATIVE_GEN_DIR}/${sub}/*.cpp")
    file(GLOB own_srcs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/${sub}/*.cpp")
    set_source_files_properties(${gen_srcs} PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/runtime/src/spu/${sub}")
    set_source_files_properties(${own_srcs} PROPERTIES INCLUDE_DIRECTORIES "${PS3BASE_SPU_NATIVE_GEN_DIR}/${sub};${CMAKE_SOURCE_DIR}/runtime/src/spu/${sub}")
    if(sub IN_LIST GOWA_SPU_ROUNDING_SUBDIRS)
        set_source_files_properties(${gen_srcs} PROPERTIES COMPILE_OPTIONS "${PS3BASE_CLANG}-frounding-math")
    endif()
    list(APPEND GOWA_SPU_GEN_SOURCES ${gen_srcs})
endforeach()
file(GLOB GOWA_JWHOLE_GEN CONFIGURE_DEPENDS "${PS3BASE_SPU_NATIVE_GEN_DIR}/jwhole/*.cpp")
if(GOWA_JWHOLE_GEN)
    set_source_files_properties(${GOWA_JWHOLE_GEN} PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/runtime/src/spu/renjob;${CMAKE_SOURCE_DIR}/runtime/src/jobsys")
endif()
file(GLOB GOWA_JOBSYS_OWN CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/jobsys/*.cpp")
set_source_files_properties(${GOWA_JOBSYS_OWN} PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/runtime/src;${CMAKE_SOURCE_DIR}/runtime/src/jobsys;${CMAKE_SOURCE_DIR}/runtime/src/spu/renjob;${PS3BASE_SPU_NATIVE_GEN_DIR}/jwhole")
# The GoWA SPU natives are handwritten rewrites of THIS title's jobs and need
# headers generated from its own SPU blocks. For a different disc they do not
# exist, so only the files that actually include a generated header are dropped;
# their SPU work then falls back to the AOT lifter. Everything else in those
# directories (the job syscall plumbing) stays, because the runtime links it.
# Only files whose #include names a generated header (_gen.h / _gen.inc) are
# dropped. A plain substring scan also matched files that merely mention the
# word in a comment and whose symbols the runtime needs, which broke the link.
# The check runs in Python: the CMake regex engine cannot express the character
# class for an include directive cleanly (it errors out on "[<").
execute_process(
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/gen_dep_filter.py" "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GOWA_GEN_DEP_FILES)
string(REPLACE "
" ";" GOWA_GEN_DEP_FILES "${GOWA_GEN_DEP_FILES}")
foreach(f ${GOWA_GEN_DEP_FILES})
    if(f)
        list(REMOVE_ITEM GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/${f}")
    endif()
endforeach()
# The job registry registers this title's own SPU modules, keyed by its block
# table. For a different disc that table has no meaning, so the registrar goes
# too -- its static initialisers would otherwise run against nothing at startup
# and fault before main().
list(REMOVE_ITEM GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/jobsys/jobs_modules.cpp")
list(APPEND GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/window_title.cpp")
list(APPEND GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/rdcapture.cpp")
list(APPEND GOWA_RUNTIME_SOURCES ${GOWA_SPU_GEN_SOURCES})
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glslang EXCLUDE_FROM_ALL)

add_library(ps3base_volk OBJECT third_party/volk/volk.c)
target_include_directories(ps3base_volk PUBLIC third_party/volk third_party/Vulkan-Headers/include)
if(WIN32)
    target_compile_definitions(ps3base_volk PUBLIC VK_USE_PLATFORM_WIN32_KHR VK_NO_PROTOTYPES)
    set(PS3BASE_SYS_LIBS dbghelp winmm)
else()
    target_compile_definitions(ps3base_volk PUBLIC VK_NO_PROTOTYPES)
    find_package(SDL3 REQUIRED CONFIG)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GOWA_AVCODEC IMPORTED_TARGET libavcodec libavutil)
    endif()
    set(PS3BASE_SYS_LIBS SDL3::SDL3 ${CMAKE_DL_LIBS} pthread)
    if(GOWA_AVCODEC_FOUND)
        message(STATUS "libavcodec found, cellVdec HLE enabled")
        list(APPEND PS3BASE_SYS_LIBS PkgConfig::GOWA_AVCODEC)
    else()
        message(STATUS "libavcodec not found, cellVdec falls back to LLE")
    endif()
endif()

# The HLE image decoders call stb_image, whose implementation must exist
# exactly once in the binary. The launcher already provides it when enabled; when
# it is not, the runtime provides it itself.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/runtime/src/launcher/stb_image_impl.cpp")
    list(APPEND GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/launcher/stb_image_impl.cpp")
endif()

add_library(ps3base_runtime OBJECT ${GOWA_RUNTIME_SOURCES})
file(GLOB GOWA_EDGE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/edge/*.cpp")
file(GLOB GOWA_RENJOB_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/renjob/*.cpp")
file(GLOB GOWA_SCREAM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/scream/*.cpp")
file(GLOB GOWA_CLOTH_SHADOWS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/cloth/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/shadows/*.cpp")
file(GLOB GOWA_MESHPREP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/meshprep/*.cpp")
file(GLOB GOWA_PM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/spu/pm/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/edgejob/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/meshhier/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/anim/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/tasks/*.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/spu/mpjob/*.cpp")
set_source_files_properties("${CMAKE_SOURCE_DIR}/runtime/src/spu/texcreator_native.cpp" ${GOWA_EDGE_SOURCES} ${GOWA_RENJOB_SOURCES} ${GOWA_SCREAM_SOURCES} ${GOWA_CLOTH_SHADOWS_SOURCES} ${GOWA_MESHPREP_SOURCES} PROPERTIES COMPILE_OPTIONS "${PS3BASE_CLANG}-frounding-math")
# third_party/stb is on the runtime's include path for the image decoders:
# cellPngDec and cellJpgDec are real, and stb_image is what decodes the pixels.
# runtime/src/lv2 is on the include path for the generated LV2 syscall table,
# which is a checked-in artefact of tools/lv2/gen_syscall_table.py.
target_include_directories(ps3base_runtime PRIVATE third_party/renderdoc third_party/volk third_party/Vulkan-Headers/include third_party/glslang third_party/stb "${CMAKE_SOURCE_DIR}/runtime/src/lv2" "${CMAKE_BINARY_DIR}/include")
target_compile_definitions(ps3base_runtime PRIVATE VK_NO_PROTOTYPES)
target_link_libraries(ps3base_runtime PUBLIC ps3base_ppu_ops)
target_compile_options(ps3base_runtime PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})
if(NOT WIN32)
    target_link_libraries(ps3base_runtime PRIVATE SDL3::SDL3)
    if(GOWA_AVCODEC_FOUND)
        target_link_libraries(ps3base_runtime PRIVATE PkgConfig::GOWA_AVCODEC)
        target_compile_definitions(ps3base_runtime PRIVATE GOWA_HAVE_FFMPEG)
    endif()
endif()

file(GLOB PS3BASE_GENERATED_SOURCES CONFIGURE_DEPENDS "${PS3BASE_GENERATED_DIR}/*_[0-9][0-9][0-9].cpp")
add_library(ps3base_ppu STATIC ${PS3BASE_GENERATED_SOURCES} "${PS3BASE_GENERATED_DIR}/table.cpp")
target_include_directories(ps3base_ppu PRIVATE "${PS3BASE_GENERATED_DIR}")
target_link_libraries(ps3base_ppu PUBLIC ps3base_ppu_ops)
target_compile_options(ps3base_ppu PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_CLANG}${PS3BASE_GENERATED_OPT} ${PS3BASE_NOEH} ${PS3BASE_CLANG}-Wno-unused-label ${PS3BASE_CLANG}-Wno-unused-variable)
set_source_files_properties(${PS3BASE_GENERATED_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

set(GOWA_SPU_GENERATED_DIR "${CMAKE_SOURCE_DIR}/out/spu" CACHE PATH "Directory with generated SPU sources")
if(NOT EXISTS "${GOWA_SPU_GENERATED_DIR}/spu_registry.cpp")
    message(FATAL_ERROR "SPU AOT sources not found in ${GOWA_SPU_GENERATED_DIR}. Run: python tools/setup.py")
endif()
file(GLOB GOWA_SPU_SOURCES CONFIGURE_DEPENDS "${GOWA_SPU_GENERATED_DIR}/spu_*.cpp")
add_library(ps3base_spu STATIC ${GOWA_SPU_SOURCES})
target_include_directories(ps3base_spu PRIVATE runtime/include runtime/src/spu)
target_link_libraries(ps3base_spu PUBLIC ps3base_ppu_ops)
target_compile_options(ps3base_spu PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_CLANG}${PS3BASE_SPU_OPT} ${PS3BASE_CLANG}-frounding-math ${PS3BASE_NOEH})

# No-op stubs for the SPU natives that exist only for this title's own jobs.
# A different disc has no such rewrites, so their symbols are absent; defining
# them as no-ops lets the CPU/kernel boot be measured, and the SPU work they
# would do is simply not done. Remove this file once a real port wants them.
list(APPEND GOWA_RUNTIME_SOURCES "${CMAKE_SOURCE_DIR}/runtime/src/gowa_stubs.cpp")

add_executable(ps3base runtime/src/main.cpp $<TARGET_OBJECTS:ps3base_runtime> $<TARGET_OBJECTS:ps3base_volk>)
# The binary is named after the runtime, not a title: this tree boots whatever
# profile it is given, and the earlier name was the fork's own.
set_target_properties(ps3base PROPERTIES OUTPUT_NAME "ps3base")
if(NOT WIN32)
    target_link_options(ps3base PRIVATE -rdynamic)
endif()
set(GOWA_IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
if(EXISTS "${GOWA_IMGUI_DIR}/imgui.h" AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
    set(GOWA_LAUNCHER_SOURCES runtime/src/launcher/launcher_ui.cpp runtime/src/launcher/stb_image_impl.cpp
        ${GOWA_IMGUI_DIR}/imgui.cpp ${GOWA_IMGUI_DIR}/imgui_draw.cpp ${GOWA_IMGUI_DIR}/imgui_tables.cpp ${GOWA_IMGUI_DIR}/imgui_widgets.cpp)
    if(WIN32)
        list(APPEND GOWA_LAUNCHER_SOURCES runtime/src/launcher/launcher_win32.cpp ${GOWA_IMGUI_DIR}/backends/imgui_impl_win32.cpp ${GOWA_IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
    else()
        list(APPEND GOWA_LAUNCHER_SOURCES runtime/src/launcher/launcher_sdl.cpp ${GOWA_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp ${GOWA_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
    endif()
    target_sources(ps3base PRIVATE ${GOWA_LAUNCHER_SOURCES})
    target_include_directories(ps3base PRIVATE ${GOWA_IMGUI_DIR} ${GOWA_IMGUI_DIR}/backends "${CMAKE_SOURCE_DIR}/third_party/stb")
    target_compile_definitions(ps3base PRIVATE GOWA_HAVE_LAUNCHER)
    if(WIN32)
        target_link_libraries(ps3base PRIVATE d3d11 dxgi)
    endif()
    message(STATUS "Launcher: Dear ImGui at ${GOWA_IMGUI_DIR}")
else()
    message(STATUS "Launcher disabled: third_party/imgui or third_party/stb missing")
endif()
target_link_libraries(ps3base PRIVATE ps3base_ppu ps3base_spu ps3base_ppu_ops ${PS3BASE_SYS_LIBS} glslang SPIRV glslang-default-resource-limits)
target_include_directories(ps3base PRIVATE runtime/include)
target_compile_options(ps3base PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})

# The LV2 table is generated from the specification and checked in, so the
# build has no Python dependency. Regenerate with tools/lv2/gen_syscall_table.py.
enable_testing()
add_executable(lv2_enosys tests/lv2_enosys.cpp)
target_include_directories(lv2_enosys PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src/lv2")
target_compile_options(lv2_enosys PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME lv2_enosys COMMAND lv2_enosys)

# The PS3 memory map. Every other subsystem stands on it and its failures are
# silent, so the guarantees are checked rather than assumed.
# memory_map calls vm::write32, which goes through rsv::store_range, so it has to
# link reservation.cpp. It was missing, and the target did not build on main
# either -- found while checking this branch's own tests.
add_executable(memory_map tests/memory_map.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(memory_map PRIVATE runtime/include)
target_compile_options(memory_map PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(memory_map PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME memory_map COMMAND memory_map)

# Two different blocks at the same LSA: SPU tasks overlay their own code at the
# same address, so the AOT table must keep both and pick by content.
add_executable(spu_aot_variants tests/spu_aot_variants.cpp runtime/src/spu/aot.cpp runtime/src/log.cpp runtime/src/host.cpp)
target_include_directories(spu_aot_variants PRIVATE runtime/include)
target_compile_options(spu_aot_variants PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(spu_aot_variants PRIVATE pthread)
add_test(NAME spu_aot_variants COMMAND spu_aot_variants)

# Every Vulkan format and blend factor the renderer uses must have a WebGPU
# decision, with the inexpressible ones classified rather than omitted.
add_library(wgpu_map_obj OBJECT runtime/src/gpu/wgpu_map.cpp)
target_include_directories(wgpu_map_obj PRIVATE runtime/include)
target_compile_options(wgpu_map_obj PRIVATE ${PS3BASE_ARCH_FLAGS})

add_executable(wgpu_map tests/wgpu_map.cpp $<TARGET_OBJECTS:wgpu_map_obj>)
target_include_directories(wgpu_map PRIVATE runtime/include)
target_compile_options(wgpu_map PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME wgpu_map COMMAND wgpu_map)

# How much guest address space a real session needs: the number that decides
# whether wasm32 can hold the map or wasm64 is required.
add_executable(memspace tests/memspace.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp runtime/src/cycles.cpp)
target_include_directories(memspace PRIVATE runtime/include)
target_compile_options(memspace PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(memspace PRIVATE pthread)
add_test(NAME memspace COMMAND memspace)

# Proves wgpu-native works here and measures the pipeline-cache cost that decides
# whether the WebGPU port is cheap or expensive. Optional: only built when the
# library is present, so a machine without it still configures.
set(WGPU_DIR "/root/Dev/ps2-web-recomp/third_party/wgpu-native" CACHE PATH "wgpu-native prefix")
if(EXISTS "${WGPU_DIR}/lib/libwgpu_native.so")
    add_executable(wgpu_smoke tests/wgpu_smoke.cpp)
    target_include_directories(wgpu_smoke PRIVATE "${WGPU_DIR}/include" runtime/include)
    target_link_libraries(wgpu_smoke PRIVATE wgpu_map_obj)
    target_link_directories(wgpu_smoke PRIVATE "${WGPU_DIR}/lib")
    target_link_libraries(wgpu_smoke PRIVATE wgpu_native pthread)
    set_target_properties(wgpu_smoke PROPERTIES BUILD_RPATH "${WGPU_DIR}/lib")
    target_compile_options(wgpu_smoke PRIVATE ${PS3BASE_ARCH_FLAGS})
    add_test(NAME wgpu_smoke COMMAND wgpu_smoke)
endif()
# Cross-processor reservation invalidation: an SPU write must invalidate an
# active PPU reservation on the same line, or stcx. commits unseen data.
add_executable(reservation_xproc tests/reservation_xproc.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(reservation_xproc PRIVATE runtime/include)
target_compile_options(reservation_xproc PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(reservation_xproc PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME reservation_xproc COMMAND reservation_xproc)

# Two guest threads, one shared counter, one million increments each. A lost
# update proves the reservation fast path is broken.
add_executable(lock_counter tests/lock_counter.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lock_counter PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(lock_counter PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lock_counter PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lock_counter COMMAND lock_counter)

# The title's OWN spinlock (lwarx/stwcx in lifted code), through the same
# ppu::lwarx / ppu::stwcx entry points the guest calls. A lost update here is
# two threads inside one critical section -- the shape of the 0x64c044 crash.
add_executable(guest_spinlock tests/guest_spinlock.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(guest_spinlock PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(guest_spinlock PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(guest_spinlock PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME guest_spinlock COMMAND guest_spinlock)

add_executable(reservation_increment tests/reservation_increment.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(reservation_increment PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(reservation_increment PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(reservation_increment PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME reservation_increment COMMAND reservation_increment)

add_executable(lwmutex_contention tests/lwmutex_contention.cpp runtime/src/lv2/lwmutex.cpp runtime/src/lv2/object.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lwmutex_contention PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(lwmutex_contention PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lwmutex_contention PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lwmutex_contention COMMAND lwmutex_contention)

add_executable(lwmutex_hle tests/lwmutex_hle.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lwmutex_hle PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(lwmutex_hle PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lwmutex_hle PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lwmutex_hle COMMAND lwmutex_hle)

add_executable(list_walk_guard tests/list_walk_guard.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(list_walk_guard PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(list_walk_guard PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(list_walk_guard PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME list_walk_guard COMMAND list_walk_guard)



if(EXISTS "${CMAKE_SOURCE_DIR}/out/conformance/tests.cpp")
    add_executable(ppu_conformance tests/ppu_conformance/harness.cpp "${CMAKE_SOURCE_DIR}/out/conformance/tests.cpp")
    target_include_directories(ppu_conformance PRIVATE tests/ppu_conformance)
    target_link_libraries(ppu_conformance PRIVATE ps3base_ppu_ops)
    target_compile_options(ppu_conformance PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_CLANG}${PS3BASE_GENERATED_OPT})
endif()

set(GOWA_JOBS_OWN "${CMAKE_SOURCE_DIR}/runtime/src/jobsys/jobs_whole.cpp" "${CMAKE_SOURCE_DIR}/runtime/src/jobsys/jobs_inplace.cpp")
set(GOWA_BENCH_GEN ${GOWA_SPU_GEN_SOURCES})
list(FILTER GOWA_BENCH_GEN EXCLUDE REGEX "/jwhole/w_[0-9a-f]+_gen[.]cpp$")
add_executable(spu_bench tests/spu_bench/bench.cpp runtime/src/spu/interpreter.cpp runtime/src/spu/aot.cpp runtime/src/spu/hle.cpp runtime/src/spu/hle_builtin.cpp runtime/src/spu/texcreator_native.cpp ${GOWA_EDGE_SOURCES} ${GOWA_RENJOB_SOURCES} ${GOWA_SCREAM_SOURCES} ${GOWA_CLOTH_SHADOWS_SOURCES} ${GOWA_MESHPREP_SOURCES} ${GOWA_PM_SOURCES} ${GOWA_BENCH_GEN} ${GOWA_JOBS_OWN} runtime/src/log.cpp)
target_include_directories(spu_bench PRIVATE runtime/include runtime/src/spu)
target_link_libraries(spu_bench PRIVATE ps3base_spu ps3base_ppu_ops $<$<BOOL:${WIN32}>:dbghelp>)
target_compile_options(spu_bench PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_CLANG}-frounding-math ${PS3BASE_EH})

add_executable(spu_live tests/spu_bench/bench.cpp $<TARGET_OBJECTS:ps3base_runtime> $<TARGET_OBJECTS:ps3base_volk>)
target_include_directories(spu_live PRIVATE runtime/include runtime/src/spu)
target_compile_definitions(spu_live PRIVATE SPU_BENCH_LIVE)
target_link_libraries(spu_live PRIVATE ps3base_ppu ps3base_spu ps3base_ppu_ops ${PS3BASE_SYS_LIBS} glslang SPIRV glslang-default-resource-limits)
target_compile_options(spu_live PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_CLANG}-frounding-math ${PS3BASE_EH})

set(GOWA_STREAMLINE_DIR "${CMAKE_SOURCE_DIR}/third_party/streamline" CACHE PATH "Extracted NVIDIA Streamline SDK (optional, enables GOWA_DLSSG)")
if(WIN32 AND EXISTS "${GOWA_STREAMLINE_DIR}/include/sl.h" AND EXISTS "${GOWA_STREAMLINE_DIR}/lib/x64/sl.interposer.lib")
    message(STATUS "Streamline SDK: ${GOWA_STREAMLINE_DIR}")
    target_compile_definitions(ps3base_runtime PRIVATE GOWA_HAVE_STREAMLINE)
    if(NOT EXISTS "${GOWA_STREAMLINE_DIR}/external/ngx-sdk/include/nvsdk_ngx_params.h")
        target_compile_definitions(ps3base_runtime PRIVATE GOWA_DLSS_DEFAULT_DIR="${CMAKE_SOURCE_DIR}/dlss")
    endif()
    target_include_directories(ps3base_runtime PRIVATE third_party/renderdoc "${GOWA_STREAMLINE_DIR}/include")
    foreach(t ps3base spu_live)
        target_link_libraries(${t} PRIVATE "${GOWA_STREAMLINE_DIR}/lib/x64/sl.interposer.lib" delayimp)
        target_link_options(${t} PRIVATE /DELAYLOAD:sl.interposer.dll)
    endforeach()
    set(GOWA_SL_DLLS sl.interposer.dll sl.common.dll sl.dlss_g.dll sl.reflex.dll sl.pcl.dll NvLowLatencyVk.dll)
    foreach(dll ${GOWA_SL_DLLS})
        add_custom_command(TARGET ps3base POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GOWA_STREAMLINE_DIR}/bin/x64/${dll}" "$<TARGET_FILE_DIR:ps3base>/${dll}")
    endforeach()
else()
    message(STATUS "Streamline SDK not found, DLSS frame generation disabled")
endif()

set(GOWA_NGX_INCLUDE "${GOWA_STREAMLINE_DIR}/external/ngx-sdk/include")
if(WIN32 AND EXISTS "${GOWA_NGX_INCLUDE}/nvsdk_ngx_params.h" AND EXISTS "${GOWA_NGX_INCLUDE}/nvsdk_ngx_defs_vk.h")
    message(STATUS "NGX SDK headers: ${GOWA_NGX_INCLUDE}")
    target_compile_definitions(ps3base_runtime PRIVATE GOWA_HAVE_NGX GOWA_DLSS_DEFAULT_DIR="${CMAKE_SOURCE_DIR}/dlss")
    target_include_directories(ps3base_runtime PRIVATE third_party/renderdoc "${GOWA_NGX_INCLUDE}")
    add_library(gowa_nvngx SHARED runtime/ngx_bridge/bridge.c)
    set_target_properties(gowa_nvngx PROPERTIES OUTPUT_NAME gowa_nvngx)
    foreach(t ps3base spu_live)
        add_dependencies(${t} gowa_nvngx)
        target_link_libraries(${t} PRIVATE version)
    endforeach()
else()
    message(STATUS "NGX SDK headers not found, DLSS neural rendering disabled")
endif()

file(GLOB GOWA_MEDIA_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/runtime/src/media/*.cpp")
add_executable(vdec_test tests/vdec_test/main.cpp ${GOWA_MEDIA_SOURCES} runtime/src/log.cpp)
target_include_directories(vdec_test PRIVATE runtime/include)
if(NOT WIN32)
    target_link_libraries(vdec_test PRIVATE ps3base_ppu_ops ${PS3BASE_SYS_LIBS})
    if(GOWA_AVCODEC_FOUND)
        target_compile_definitions(vdec_test PRIVATE GOWA_HAVE_FFMPEG)
    endif()
endif()
target_compile_options(vdec_test PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})

add_executable(spu_conformance tests/spu_conformance/harness.cpp runtime/src/spu/interpreter.cpp)
target_include_directories(spu_conformance PRIVATE runtime/include)
target_link_libraries(spu_conformance PRIVATE ps3base_ppu_ops)
target_compile_options(spu_conformance PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})
