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
add_library(ps3base_ppu_ops STATIC runtime/src/ppu_ops.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/log.cpp runtime/src/host.cpp)
target_include_directories(ps3base_ppu_ops PUBLIC runtime/include "${CMAKE_SOURCE_DIR}/out/include")
target_compile_options(ps3base_ppu_ops PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})

file(GLOB GOWA_RUNTIME_SOURCES CONFIGURE_DEPENDS runtime/src/*.cpp runtime/src/lv2/*.cpp runtime/src/hle/*.cpp runtime/src/media/*.cpp runtime/src/spu/*.cpp runtime/src/spu/edge/*.cpp runtime/src/spu/renjob/*.cpp runtime/src/spu/scream/*.cpp runtime/src/spu/cloth/*.cpp runtime/src/spu/shadows/*.cpp runtime/src/spu/meshprep/*.cpp runtime/src/spu/pm/*.cpp runtime/src/spu/edgejob/*.cpp runtime/src/spu/meshhier/*.cpp runtime/src/spu/anim/*.cpp runtime/src/spu/tasks/*.cpp runtime/src/spu/mpjob/*.cpp runtime/src/jobsys/*.cpp runtime/src/rsx/*.cpp runtime/src/gpu/*.cpp runtime/src/crypto/*.cpp)
# The funnel must live in ps3base_ppu_ops, not in the runtime object library: the
# small test binaries link memory.cpp and reservation.cpp directly, and
# pgw::write would otherwise be an undefined reference there. It is removed from
# the GLOB for the same reason codewatch.cpp is -- defining it twice breaks every
# link.
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
# Say WHICH seeds the generated code was lifted from, and warn when an input is
# newer than the lift. A lift is a pure function of (ELF, seeds, switch tables,
# images.json); if one of those changed and the generated code did not, the
# binary describes a different title than the one on disk. That happened on
# 2026-11-13: out-rdr/ppu had no f_00b58430 (the cellFiber entry the title calls)
# and nothing in the build said so.
execute_process(
    COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/lift_stamp.py" "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE PS3BASE_LIFT_STAMP
    ERROR_QUIET)
if(PS3BASE_LIFT_STAMP)
    string(STRIP "${PS3BASE_LIFT_STAMP}" PS3BASE_LIFT_STAMP)
    message(STATUS "${PS3BASE_LIFT_STAMP}")
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

# The export coverage gate.
#
# A module can be in the image table -- loaded, its exports in exports[] -- and
# still be useless, because the lifter never produced the function its export
# descriptor points at. resolve() finds the export, the runtime loads the
# descriptor, and the call lands on a null dispatch slot with no log line
# connecting it to the missing lift. That is exactly how the RDR fibre crash
# happened: cellFiber was "fully served LLE, 47/47 exports verified" while the
# lift was incomplete.
#
# So the check runs at CONFIGURE time, before a single object is compiled, and
# it FAILS the configure listing every missing export. Set
# -DPS3BASE_EXPORT_GATE=OFF only to diagnose a tree that cannot pass it.
option(PS3BASE_EXPORT_GATE "Fail configuration when an LLE export has no lifted function" ON)
if(PS3BASE_EXPORT_GATE)
    execute_process(
        COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_export_coverage.py"
                "${PS3BASE_GENERATED_DIR}/table.cpp" --root "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE PS3BASE_EXPORT_GATE_RC
        OUTPUT_VARIABLE PS3BASE_EXPORT_GATE_OUT
        ERROR_VARIABLE PS3BASE_EXPORT_GATE_OUT)
    if(NOT PS3BASE_EXPORT_GATE_RC EQUAL 0)
        message(FATAL_ERROR
            "Export coverage gate FAILED (tools/check_export_coverage.py).
"
            "Every exported function of every LLE module must have a lifted
"
            "function; these do not:

${PS3BASE_EXPORT_GATE_OUT}
"
            "Re-lift with the missing addresses as seeds, or remove the module
"
            "from the image table. Do not silence the gate.")
    endif()
    # Re-run the gate when the table or the gate itself changes, not only on a
    # fresh configure: a re-lift writes table.cpp in place, and a gate that only
    # runs once is a gate that is stale exactly when it matters.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 "${PS3BASE_GENERATED_DIR}/table.cpp"
                 "${CMAKE_SOURCE_DIR}/tools/check_export_coverage.py")
    message(STATUS "Export coverage gate: PASS")
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

add_executable(ps3base runtime/src/main.cpp
                "${PS3BASE_GENERATED_DIR}/table.cpp"
                $<TARGET_OBJECTS:ps3base_runtime> $<TARGET_OBJECTS:ps3base_volk>)
# table.cpp is compiled into the binary, not only into libps3base_ppu. The
# image table now DEFINES ppu::hook_flags, which hooks.cpp and ppu_ops.cpp
# reference; a static library does not resolve a symbol for an object that was
# already pulled in from an earlier archive, so leaving it to the archive
# produced undefined references in the final link.
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
# hook_flags: one array per image, so a module placed above 0x2000000 stays in
# bounds. Self-contained: it builds its own image table at a high address rather
# than linking the generated table, which would drag in every lifted function.
add_executable(hook_flags tests/hook_flags.cpp)
target_include_directories(hook_flags PRIVATE runtime/include)
target_compile_options(hook_flags PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME hook_flags COMMAND hook_flags)

# cellfiber_contract: the cellFiber HLE against the REAL library's behaviour,
# read from the lifted firmware code. Pins three things that were wrong and are
# each a silent false report rather than an error:
#   - Yield returned PERM where the real code returns NOSYSINIT (no scheduler);
#   - JoinFiber returned CELL_OK with a status for a fibre that never ran, where
#     the real code waits on the fibre's state word at +772;
#   - four unimplemented exports returned a bare CELL_OK, which AGENTS.md
#     forbids.
# It drives the handlers through hle::find, so it cannot drift from the code.
add_executable(cellfiber_contract tests/cellfiber_contract.cpp
               runtime/src/hle/cellFiber.cpp runtime/src/log.cpp runtime/src/host.cpp
               runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
               runtime/src/reservation.cpp runtime/src/codewatch.cpp
               runtime/src/backtrace_port.cpp)
target_include_directories(cellfiber_contract PRIVATE runtime/include runtime/src)
target_compile_options(cellfiber_contract PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME cellfiber_contract COMMAND cellfiber_contract)

# lift_stamp: the lift's staleness check and seed fingerprint, on a synthetic
# tree. A lift is a pure function of (ELF, seeds, switch tables, images.json); if
# an input is newer than table.cpp the generated code describes a different
# binary than the one on disk, and nothing in the build said so until this.
add_test(NAME lift_stamp
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/lift_stamp.py")

# ppu_dispatch_table: the lifter's shared-table bctr detector, on synthetic
# words. Pins the two shape rules that cost a session on Red Dead Redemption:
# mtctr is not always at bctr-4 (26 of f_00d19208's 163 are at -2, 3 at -3 and
# 4 at -4), and the feeding lwzx is the one whose RT is the mtctr source. It
# FAILED on both before the fix, so it is a regression test and not a
# description: reverting either rule stops it at the first check.
add_test(NAME ppu_dispatch_table
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/ppu_dispatch_table.py")

# ppu_seed_forms: the two seed forms, and why the named one exists. An absolute
# seed is only valid for the layout it was recorded in; layout_for.py recomputes
# every module base from the title's text extent, so an absolute seed silently
# names the wrong byte after a relayout. "liblv2+0xc7e8" is resolved at lift time
# and cannot go stale. The test pins the parse, the per-image resolution, and the
# refusal of an absolute landmark.
add_test(NAME ppu_seed_forms
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/ppu_seed_forms.py")

# ppu_run_extent_table: jump_table's run-extent rule and table_dispatch_bctr's
# setter rule, on synthetic words. Pins the fix for the stop at rdr+0xad5618:
# a dispatch table whose base is an absolute lis/addi pair and which has NO
# bound compare (the caller validates the index) was rejected, the lift emitted
# a switch with no cases, and the run stopped on 0x00ae5818 -- entry 13 of the
# 17-entry table at 0x013e5c8c. It FAILED on the old detector at the first
# check, so it is a regression test and not a description.
add_test(NAME ppu_run_extent_table
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/ppu_run_extent_table.py")

# export_coverage: the same gate that runs at configure time, as a test, so a
# generated tree copied in after configuration is still checked. It is the
# regression test for the class of bug that produced the RDR fibre crash: a
# module reported "fully served LLE, 47/47 exports verified" while the lift was
# incomplete. The gate is deliberately NOT a test-only check -- configure fails
# on it -- but a test makes it visible in ctest output beside everything else.
add_test(NAME export_coverage
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tools/check_export_coverage.py"
                 "${PS3BASE_GENERATED_DIR}/table.cpp" --root "${CMAKE_SOURCE_DIR}")

# export_coverage_gate: the gate's own regression test, on a synthetic PRX. It
# needs no disc, so it runs everywhere, and it pins the six cases that matter --
# including the two the old check missed (an unnamed export row, and a
# descriptor whose code word is outside the image text) and the zero-size libent
# link that once made this gate allocate 24 GB.
add_test(NAME export_coverage_gate
         COMMAND python3 "${CMAKE_SOURCE_DIR}/tests/export_coverage.py")

add_executable(memory_map tests/memory_map.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
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

# SPU channel state machines and the control instructions that carry them, one
# check per deviation from RPCS3. Links the interpreter, so the op_* handlers
# under test are the ones the AOT blocks dispatch to.
add_executable(spu_channel_conformance tests/spu_channel_conformance.cpp runtime/src/spu/interpreter.cpp runtime/src/log.cpp runtime/src/host.cpp)
target_include_directories(spu_channel_conformance PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src/spu")
target_link_libraries(spu_channel_conformance PRIVATE ps3base_ppu_ops pthread)
target_compile_options(spu_channel_conformance PRIVATE ${PS3BASE_ARCH_FLAGS} ${PS3BASE_EH})
add_test(NAME spu_channel_conformance COMMAND spu_channel_conformance)

# The DMA list walker: PUTL/PUTLB/PUTLF and GETL/GETLB/GETLF, driven through
# runtime/include/spu/dma_list.h -- the same header SpuThread::run_list calls.
# The decisive case is a list whose second element is only filled after the
# stall-and-notify event.
add_executable(spu_dma_list tests/spu_dma_list.cpp)
target_include_directories(spu_dma_list PRIVATE runtime/include)
target_compile_options(spu_dma_list PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME spu_dma_list COMMAND spu_dma_list)



# Every Vulkan format and blend factor the renderer uses must have a WebGPU
# decision, with the inexpressible ones classified rather than omitted.
add_library(wgpu_map_obj OBJECT runtime/src/gpu/wgpu_map.cpp)
target_include_directories(wgpu_map_obj PRIVATE runtime/include)
target_compile_options(wgpu_map_obj PRIVATE ${PS3BASE_ARCH_FLAGS})

add_executable(wgpu_map tests/wgpu_map.cpp $<TARGET_OBJECTS:wgpu_map_obj>)
add_executable(pipeline_key tests/pipeline_key.cpp)
target_include_directories(pipeline_key PRIVATE runtime/include)
add_executable(pipeline_cache tests/pipeline_cache.cpp runtime/src/gpu/pipeline_cache.cpp)
target_include_directories(pipeline_cache PRIVATE runtime/include)
add_executable(pipeline_cost tests/pipeline_cost.cpp runtime/src/gpu/pipeline_cache.cpp)
target_include_directories(pipeline_cost PRIVATE runtime/include)
add_executable(wgsl_map tests/wgsl_map.cpp runtime/src/gpu/wgsl_map.cpp)
target_include_directories(wgsl_map PRIVATE runtime/include)
add_executable(command_ring tests/command_ring.cpp runtime/src/gpu/command_ring.cpp)
target_include_directories(command_ring PRIVATE runtime/include)
add_executable(audio_ring tests/audio_ring.cpp runtime/src/gpu/audio_ring.cpp)
target_include_directories(audio_ring PRIVATE runtime/include)
target_include_directories(wgpu_map PRIVATE runtime/include)
target_compile_options(wgpu_map PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME wgpu_map COMMAND wgpu_map)

# How much guest address space a real session needs: the number that decides
# whether wasm32 can hold the map or wasm64 is required.
add_executable(memspace tests/memspace.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp runtime/src/cycles.cpp)
target_include_directories(memspace PRIVATE runtime/include)
target_compile_options(memspace PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(memspace PRIVATE pthread)
add_test(NAME memspace COMMAND memspace)

# SPURS event flag: the struct is written in the guest's real big-endian layout,
# so the field offsets are asserted rather than assumed.
add_executable(spurs tests/spurs.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp runtime/src/cycles.cpp)
target_include_directories(spurs PRIVATE runtime/include runtime/src runtime/src/hle runtime/src/lv2 out/ppu)
target_compile_options(spurs PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(spurs PRIVATE pthread)
add_test(NAME spurs COMMAND spurs)

# sys_mmapper against RPCS3, one check per deviation. The handlers are static in
# the module, so the test includes it and drives the logic functions directly;
# the address allocator is the real one, because the deviation that matters most
# -- an area handed out over a guest image -- lives in their interaction.
add_executable(mmapper_conformance tests/mmapper_conformance.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp runtime/src/lv2/object.cpp)
target_include_directories(mmapper_conformance PRIVATE runtime/include runtime/src runtime/src/lv2)
target_compile_options(mmapper_conformance PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(mmapper_conformance PRIVATE pthread)
add_test(NAME mmapper_conformance COMMAND mmapper_conformance)

# Proves wgpu-native works here and measures the pipeline-cache cost that decides
# whether the WebGPU port is cheap or expensive. Optional: only built when the
# library is present, so a machine without it still configures.
# Point this at a wgpu-native prefix to build the smoke test:
#   cmake -DWGPU_DIR=/path/to/wgpu-native ...
# Empty by default, so a clean checkout configures without it.
set(WGPU_DIR "" CACHE PATH "wgpu-native prefix (optional; enables wgpu_smoke)")
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
add_executable(reservation_xproc tests/reservation_xproc.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(reservation_xproc PRIVATE runtime/include)
target_compile_options(reservation_xproc PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(reservation_xproc PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME reservation_xproc COMMAND reservation_xproc)

# Every guest write path reaches the block watch, measured rather than argued:
# one line armed, one store through each of the seven paths, then the log is
# read back and each path's label must be present. A path that stops calling
# cw::block_store fails here instead of silently widening a watch.
add_executable(write_paths tests/write_paths.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(write_paths PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(write_paths PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(write_paths PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME write_paths COMMAND write_paths)

# Two guest threads, one shared counter, one million increments each. A lost
# update proves the reservation fast path is broken.
add_executable(lock_counter tests/lock_counter.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lock_counter PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(lock_counter PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lock_counter PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lock_counter COMMAND lock_counter)

# The title's OWN spinlock (lwarx/stwcx in lifted code), through the same
# ppu::lwarx / ppu::stwcx entry points the guest calls. A lost update here is
# two threads inside one critical section -- the shape of the 0x64c044 crash.
add_executable(guest_spinlock tests/guest_spinlock.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(guest_spinlock PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(guest_spinlock PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(guest_spinlock PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME guest_spinlock COMMAND guest_spinlock)

add_executable(reservation_increment tests/reservation_increment.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(reservation_increment PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(reservation_increment PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(reservation_increment PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME reservation_increment COMMAND reservation_increment)

# A reservation is the 128-byte line, not the 8 bytes lwarx loaded: a write by
# another agent to any word of the line cancels it, and stwcx. may target any
# word of the line. The write in the failing case is a plain memcpy into
# vm::ptr, the shape of the 81 write sites that never reach rsv::.
add_executable(reservation_line tests/reservation_line.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(reservation_line PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(reservation_line PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(reservation_line PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME reservation_line COMMAND reservation_line)

add_executable(lwmutex_contention tests/lwmutex_contention.cpp runtime/src/lv2/lwmutex.cpp runtime/src/lv2/object.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lwmutex_contention PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(lwmutex_contention PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lwmutex_contention PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lwmutex_contention COMMAND lwmutex_contention)

add_executable(lwmutex_create tests/lwmutex_create.cpp runtime/src/lv2/lwmutex.cpp runtime/src/lv2/object.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lwmutex_create PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(lwmutex_create PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lwmutex_create PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lwmutex_create COMMAND lwmutex_create)

# start_gate: the console's start rule (a higher-priority child runs until it
# blocks, bounded) and PT_PROC_PARAM, the only place a title states its main
# thread's priority. Both are pure functions, so this links no guest memory, no
# PPU runtime and no VFS -- the same property the other header-driven suites
# keep, and the reason it can run on a machine with no game.
add_executable(start_gate tests/start_gate.cpp runtime/src/start_gate.cpp runtime/src/proc_param.cpp runtime/src/log.cpp runtime/src/host.cpp)
target_include_directories(start_gate PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(start_gate PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(start_gate PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME start_gate COMMAND start_gate)

add_executable(lwmutex_hle tests/lwmutex_hle.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(lwmutex_hle PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(lwmutex_hle PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(lwmutex_hle PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME lwmutex_hle COMMAND lwmutex_hle)

# A host memcpy into guest memory bypassed reservation invalidation entirely: the
# line version never moved, so a PPU thread holding lwarx on that line committed a
# value it never observed. This test measures both spellings -- the raw memcpy and
# the pgw funnel -- and the diagnostic page guard's read watch.
add_executable(host_write_invalidation tests/host_write_invalidation.cpp runtime/src/ppu_ops.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(host_write_invalidation PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(host_write_invalidation PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(host_write_invalidation PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME host_write_invalidation COMMAND host_write_invalidation)

add_executable(list_walk_guard tests/list_walk_guard.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(list_walk_guard PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(list_walk_guard PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(list_walk_guard PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME list_walk_guard COMMAND list_walk_guard)

# --- the cellGame / cellGameExec / cellSaveData / cellSysutilAvconfExt HLE ----
# Each target links ONLY what its subject needs: the three header-driven suites
# are pure host logic (no guest memory, no PPU runtime), and the one that does
# touch guest memory links the memory map and nothing else. That is what keeps
# them runnable on a machine with no game, no GPU and no network.
#
# cellgame: the PARAM.SFO CATEGORY rules, the parameter-id table and its
# per-mode permissions, the content-error dialog type set, and the SFO writer
# round-tripping through a parser. Same header the imports use.
add_executable(cellgame tests/cellgame.cpp)
target_include_directories(cellgame PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(cellgame PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME cellgame COMMAND cellgame)

# cellgameexec: the module's single import must NEVER return CELL_OK, and must
# leave the caller's buffer alone. Includes the module and calls the handler.
add_executable(cellgameexec tests/cellgameexec.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(cellgameexec PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(cellgameexec PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(cellgameexec PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME cellgameexec COMMAND cellgameexec)

# savedata: the delete-list argument checks (including the fileListMax == 0
# rule that exists ONLY for delete lists), the prefix and asterisk rules, the
# listing counts, the sort, and the per-file KiB rounding delete_save does.
add_executable(savedata tests/savedata.cpp)
target_include_directories(savedata PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(savedata PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME savedata COMMAND savedata)

# sysio: sys_io's keyboard and pad rules (hle/sysio_hle.h), driven through the
# same header the imports use. Pure host logic -- no guest memory, no PPU runtime
# -- because the subject is the return codes and the argument-check ORDER, which
# is where the previous handlers were wrong (cellKbInit AFTER the range test,
# cellPadInit clamped at 7, cellPadEnd and cellPadClearBuf answering CELL_OK
# unconditionally).
add_executable(sysio tests/sysio.cpp)
target_include_directories(sysio PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(sysio PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME sysio COMMAND sysio)

# commerce2: sceNpCommerce2's decision logic (hle/commerce2_hle.h), driven
# through the same header the handlers use. Pure host logic -- no guest memory,
# no network -- because the subject is the return codes, their ORDER, and the
# one thing this module must get right offline: the store is unavailable, so
# GETTING DATA fails with RPCS3's codes while CREATING a context and a session
# succeeds. Both halves are asserted, since a session that failed made this
# title abandon a save load (docs/loading-43/REPORT.md).
# error_codes: every code in runtime/kernel.h against RPCS3 ErrorCodes.h. A
# wrong code does not crash -- it makes the title take a branch meant for a
# different failure, which is how ENOTSUP became ENOLICDISC.
# fs_fcntl_e0000017: the lv2_file_e0000017 layout, built the way a title sends it.
# The first implementation read _x4 at +0x10 and _x8 at +0x20 (treating the values
# 0x10/0x20 as offsets), so its own validation failed and the guest got EINVAL.
add_executable(fs_fcntl_e0000017 tests/fs_fcntl_e0000017.cpp)
target_include_directories(fs_fcntl_e0000017 PRIVATE runtime/include)
target_compile_options(fs_fcntl_e0000017 PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME fs_fcntl_e0000017 COMMAND fs_fcntl_e0000017)

add_executable(error_codes tests/error_codes.cpp)
target_include_directories(error_codes PRIVATE runtime/include)
target_compile_options(error_codes PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME error_codes COMMAND error_codes)

add_executable(commerce2 tests/commerce2.cpp)
target_include_directories(commerce2 PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(commerce2 PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME commerce2 COMMAND commerce2)

# commerce2_hle: the 26 HANDLERS included whole, called the way the import
# dispatcher calls them, with a real guest context and real guest memory. It
# checks the registration set (the 26 imported names, and NOT the 26 that would
# otherwise fabricate answers), the values that land in r3 and in the caller's
# structures, and the five arguments of the session-done callback. Nothing here
# executes guest code.
# The module source is compiled INTO the test: the 26 HLE_FUNC registrations only
# exist if sceNpCommerce2.cpp is part of the executable, and the test's first
# assertion is that all 26 imported names are registered. Without it the registry
# is empty and the two registration checks fail while everything else passes --
# which reads like a module defect and is a missing translation unit.
add_executable(commerce2_hle tests/commerce2_hle.cpp
    runtime/src/hle/sceNpCommerce2.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(commerce2_hle PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(commerce2_hle PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(commerce2_hle PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME commerce2_hle COMMAND commerce2_hle)

# netctl: the cellNetCtl decision table (hle/netctl_hle.h) plus the ten handlers,
# called with a real guest context and real guest memory through the same header
# the imports use. Every answer is the console's OFFLINE answer, which is the
# point: RPCS3 answers ETHER_ADDR and the whole of GetNatInfo without a network,
# and refuses the rest with CELL_NET_CTL_ERROR_NOT_CONNECTED. Links the memory
# map and nothing else, so it runs with no game, no GPU and no network.
add_executable(netctl tests/cellNetCtl.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(netctl PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(netctl PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(netctl PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME netctl COMMAND netctl)
# np: sceNp's decision rules (hle/np_hle.h), driven through the same header the
# 47 imports in runtime/src/hle/np.cpp run. Pure host logic -- the header has no
# guest memory and no PPU context, so this target links nothing at all and runs
# with no game, no GPU and no network. The subject is the ORDER of the checks
# and the codes RPCS3 returns offline, which is where the previous handlers were
# wrong: GetChatRestrictionFlag answering 1, BasicGetEvent answering EXCEEDS_MAX
# instead of NO_EVENT, LookupInit answering 0 without a state, and ScoreInit
# writing through an argument it never had.
add_executable(np tests/np.cpp)
target_include_directories(np PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(np PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME np COMMAND np)

# nptrophy: sceNpTrophy, the twelve functions Red Dead Redemption imports. The
# suite includes the module and drives the real handlers with a real ppu::Context
# and real guest memory; hle::Reg, hle::call_guest and hle::sysutil_post are the
# only seams stubbed (no import table, no PPU scheduler, no callback-queue
# thread), and the test asserts what the module HANDS them. The trophy files are
# built by the test -- a synthetic TROPHY.TRP -- so it needs no game data, no GPU
# and no network.
add_executable(nptrophy tests/nptrophy.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp runtime/src/vfs.cpp)
target_include_directories(nptrophy PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(nptrophy PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(nptrophy PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME nptrophy COMMAND nptrophy)

# avconf: cellVideoOutSetGamma's argument order and its three return codes.
# The identity case is a true CELL_OK; every other in-range gamma is refused
# with NOT_IMPLEMENTED rather than a success that changed no pixel.
add_executable(avconf tests/avconf.cpp)
target_include_directories(avconf PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(avconf PRIVATE ${PS3BASE_ARCH_FLAGS})
add_test(NAME avconf COMMAND avconf)

# sysutil: cellSysutil's audio-out, video-out, system-parameter, BGM, cache, OSK
# and progress-bar surface. Pure host logic plus guest memory: the header the
# imports use (runtime/src/hle/sysutil_hle.h) is compiled into the test and its
# return codes and structure offsets are read back out of guest memory. Runs on
# a machine with no game, no GPU and no network.
add_executable(sysutil tests/sysutil.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(sysutil PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src")
target_compile_options(sysutil PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(sysutil PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME sysutil COMMAND sysutil)

# sysutil_handlers: the SAME handlers the imports run (cellSysutil.cpp is
# included), called with a real ppu::Context, so the argument order and the
# value that lands in r3 are checked -- not only the rules in the header. The
# services it calls (msgdialog, osk, vfs, cbwatch) are stubbed, so this runs with
# no game, no GPU and no threads.
add_executable(sysutil_handlers tests/sysutil_handlers.cpp runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(sysutil_handlers PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(sysutil_handlers PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(sysutil_handlers PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME sysutil_handlers COMMAND sysutil_handlers)



# voice: cellVoice (libvoice), the module Red Dead Redemption imports 13
# functions from. The suite includes the module and drives the real handlers
# with a real ppu::Context and real guest memory; LV2's queue create/send are
# the only thing stubbed, so the service's events are checked without a
# scheduler. No game, no GPU, no network.
add_executable(voice tests/voice.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(voice PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(voice PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(voice PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME voice COMMAND voice)

# cellAudio: libaudio against refs/.../Modules/cellAudio.cpp. Drives the state
# machine (runtime/src/hle/audio_hle.h) with real guest memory and a stub queue,
# so the layout of CellAudioPortConfig, the exact portSize, the advancing read
# index and the per-period CELL_AUDIO_EVENT_MIX events are all measured, without
# a game, a GPU or an audio device.
add_executable(cellAudio tests/cellAudio.cpp
    runtime/src/memory.cpp runtime/src/regions.cpp runtime/src/mem.cpp
    runtime/src/reservation.cpp runtime/src/codewatch.cpp runtime/src/backtrace_port.cpp
    runtime/src/host.cpp runtime/src/log.cpp)
target_include_directories(cellAudio PRIVATE runtime/include "${CMAKE_SOURCE_DIR}/runtime/src" "${CMAKE_SOURCE_DIR}/runtime/src/hle")
target_compile_options(cellAudio PRIVATE ${PS3BASE_ARCH_FLAGS})
target_link_libraries(cellAudio PRIVATE ${CMAKE_DL_LIBS} pthread)
add_test(NAME cellAudio COMMAND cellAudio)

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