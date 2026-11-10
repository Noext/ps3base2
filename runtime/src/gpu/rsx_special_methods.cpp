#include "runtime/log.h"

#include <array>
#include <cstdint>

/* The RSX "special method" table: which registers the command processor treats as
 * data rather than as state.
 *
 * This lived inside gpu/rsx_renderer.cpp, which is the Vulkan renderer -- 4,626
 * lines, hundreds of Vk* calls, and no meaning without a GPU. The guest calls
 * special_methods() during RSX init, *before* any draw, so a build with no display
 * still needs this table. Extracting it lets the wasm build satisfy the call
 * without pulling the renderer in.
 *
 * The register values are copied verbatim from rsx_renderer.cpp's anonymous enum
 * (lines 115-146); keeping them here rather than including that file is the whole
 * point of the split.
 */

namespace gpu {
namespace rsx {

/* The RSX register file for a build with no renderer.
 *
 * gpu/rsx_renderer.cpp keeps its own `g_regs` inside an anonymous namespace, so
 * the two never collide at link time. A GPU-less build gets this one: the guest
 * writes registers during RSX init and reads them back before any draw, which is
 * all the boot needs. */
/* The renderer owns registers() when it is linked. This file is compiled into
 * both builds, so the definition is guarded: a graphical build gets the
 * renderer's, the no-display build gets this one. PS3BASE_NO_RENDERER is set by
 * the wasm link, which has no Vulkan at all. */
#if defined(PS3BASE_NO_RENDERER)
namespace {
uint32_t g_regs_nogpu[0x4000];
}
uint32_t* registers() { return g_regs_nogpu; }
#endif

namespace {

/* Register numbers, from rsx_renderer.cpp:115-146. Only the ones this table uses. */
constexpr uint32_t TRANSFORM_PROGRAM = 0x0B80;
constexpr uint32_t BEGIN_END = 0x1808;
constexpr uint32_t ARRAY_ELEMENT16 = 0x180C;
constexpr uint32_t ARRAY_ELEMENT32 = 0x1810;
constexpr uint32_t DRAW_ARRAYS = 0x1814;
constexpr uint32_t INLINE_ARRAY = 0x1818;
constexpr uint32_t DRAW_INDEX_ARRAY = 0x1824;
constexpr uint32_t CLEAR_SURFACE = 0x1D94;
constexpr uint32_t TRANSFORM_PROGRAM_LOAD = 0x1E9C;
constexpr uint32_t TRANSFORM_CONSTANT_LOAD = 0x1EFC;
constexpr uint32_t TRANSFORM_CONSTANT = 0x1F00;

}  // namespace

const uint8_t* special_methods() {
    static const auto table = [] {
        std::array<uint8_t, 0x4000> t{};
        /* A register is "special" when its payload is data, so the command
         * processor must not try to interpret it as state. */
        auto mark = [&](uint32_t from, uint32_t to) {
            for (uint32_t r = from; r < to; r += 4) t[(r >> 2) & 0x3FFF] = 1;
        };
        mark(TRANSFORM_PROGRAM, TRANSFORM_PROGRAM + 128);
        mark(TRANSFORM_CONSTANT, TRANSFORM_CONSTANT + 128);
        mark(0x0A80, 0x0B00);
        mark(0x1500, 0x1600);
        mark(0x1880, 0x1A00);
        mark(0x1C00, 0x1D00);
        mark(0x1E40, 0x1E80);
        mark(0xA400, 0xC000);
        for (uint32_t r : {TRANSFORM_PROGRAM_LOAD, TRANSFORM_CONSTANT_LOAD, BEGIN_END, ARRAY_ELEMENT16,
                           ARRAY_ELEMENT32, DRAW_ARRAYS, INLINE_ARRAY, DRAW_INDEX_ARRAY, CLEAR_SURFACE,
                           0x17C8u, 0x2328u, 0xC40Cu})
            mark(r, r + 4);
        return t;
    }();
    return table.data();
}

}  // namespace rsx
}  // namespace gpu
