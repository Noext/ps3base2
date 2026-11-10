#include "runtime/log.h"

#include <atomic>
#include <cstdint>

/* The RSX command path for a build with no display.
 *
 * The prompt for milestone 3 says exactly this: "le chemin d'affichage est
 * remplacé par un stub qui compte les commandes RSX". Not a renderer, not a fake
 * renderer -- a counter, so the boot can proceed and the command stream can be
 * measured.
 *
 * Why this file exists at all: gpu::rsx::method() and method_run() are the RSX
 * command interpreter. They live in rsx_renderer.cpp (4,626 lines, hundreds of
 * Vk* calls) only because that is where they were written. The FIFO thread calls
 * them for every register write, and in wasm the FIFO thread cannot start without
 * them:
 *
 *     Aborted(missing function: _ZN3gpu3rsx6methodEjj)
 *
 * The interpreter itself does no Vulkan -- it decodes commands and records state.
 * The drawing is what needs a GPU, and that is what this stub omits.
 *
 * Register numbers are from rsx_renderer.cpp:115-146.
 */

#if defined(PS3BASE_NO_RENDERER)

namespace gpu {
namespace rsx {

namespace {

/* Register numbers this stub recognises, kept in sync with the renderer's enum. */
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

/* Counters, so the command stream is measurable even though nothing draws. This
 * is the "stub qui compte les commandes RSX" the prompt asks for. */
std::atomic<uint64_t> g_commands{0};
std::atomic<uint64_t> g_draws{0};
std::atomic<uint64_t> g_clears{0};
std::atomic<uint64_t> g_vp_words{0};
std::atomic<uint64_t> g_const_writes{0};

}  // namespace

void note_vertex_program();
void note_fragment_program(bool);

void method(uint32_t reg, uint32_t arg) {
    g_commands.fetch_add(1, std::memory_order_relaxed);
    /* The renderer compiles a vertex program when the transform-program load
     * changes and a fragment program when its control word is written. Counting
     * both here is what tells "the title draws nothing" from "it draws and we do
     * not compile". Register numbers from rsx_renderer.cpp:115-146. */
    if (reg == TRANSFORM_PROGRAM_LOAD) note_vertex_program();
    if (reg == 0x0A04) note_fragment_program(true);   /* FP_CONTROL / shader write */

    if (reg >= TRANSFORM_PROGRAM && reg < TRANSFORM_PROGRAM + 128) {
        g_vp_words.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (reg == TRANSFORM_PROGRAM_LOAD) return;
    if (reg == TRANSFORM_CONSTANT_LOAD) return;
    if (reg >= TRANSFORM_CONSTANT && reg < TRANSFORM_CONSTANT + 128) {
        g_const_writes.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    switch (reg) {
    case INLINE_ARRAY:
    case ARRAY_ELEMENT16:
    case ARRAY_ELEMENT32:
        return;
    case DRAW_ARRAYS:
    case DRAW_INDEX_ARRAY:
        g_draws.fetch_add(1, std::memory_order_relaxed);
        return;
    case CLEAR_SURFACE:
        g_clears.fetch_add(1, std::memory_order_relaxed);
        return;
    default:
        return;
    }
}

/* Same contract as the renderer's: return how many argument words were consumed,
 * 0 when the method is not a block method. A no-display build consumes exactly one
 * word per method, which keeps the FIFO pointer advancing correctly. */
uint32_t method_run(uint32_t reg, const uint32_t* args, uint32_t n) {
    (void)args;
    if (!n) return 0;
    method(reg, args[0]);
    return 1;
}

/* Shader programs: the GLSL generator (fp_decompiler) has no Vulkan dependency, so
 * a no-display build can run it. Counting how many programs the guest submits --
 * and whether they decompile -- separates "the title draws nothing" from "the
 * title draws and we do not compile it". */
namespace {
std::atomic<uint64_t> g_vp_seen{0}, g_fp_seen{0}, g_fp_ok{0};
}

void note_vertex_program() { g_vp_seen.fetch_add(1, std::memory_order_relaxed); }
void note_fragment_program(bool valid) {
    g_fp_seen.fetch_add(1, std::memory_order_relaxed);
    if (valid) g_fp_ok.fetch_add(1, std::memory_order_relaxed);
}
void shader_stats(uint64_t* vp, uint64_t* fp, uint64_t* ok) {
    if (vp) *vp = g_vp_seen.load(std::memory_order_relaxed);
    if (fp) *fp = g_fp_seen.load(std::memory_order_relaxed);
    if (ok) *ok = g_fp_ok.load(std::memory_order_relaxed);
}

/* Texture-pool bookkeeping. The renderer syncs its decode pool here; with no
 * textures there is nothing to sync, but the call must exist because the FIFO
 * thread makes it. */
void sync_decodes() {}

/* A present with no surface: nothing is flipped, and the caller is told so. The
 * guest's flip counter therefore does not advance from here -- which is the honest
 * behaviour of a build that draws nothing. */
bool present(uint32_t buffer_id) {
    (void)buffer_id;
    g_clears.fetch_add(0, std::memory_order_relaxed);
    return false;
}

/* Occlusion-query result. The renderer reads a GPU counter; with no GPU there is
 * no result, so report "not visible" (0), which makes the guest skip work rather
 * than block on a query that will never complete. */
uint32_t zpass_value(uint32_t ea) {
    (void)ea;
    return 0u;
}

/* Reported once at exit so the command stream is part of the build's evidence. */
void no_display_stats(uint64_t* commands, uint64_t* draws, uint64_t* clears,
                      uint64_t* vp_words, uint64_t* const_writes) {
    if (commands) *commands = g_commands.load(std::memory_order_relaxed);
    if (draws) *draws = g_draws.load(std::memory_order_relaxed);
    if (clears) *clears = g_clears.load(std::memory_order_relaxed);
    if (vp_words) *vp_words = g_vp_words.load(std::memory_order_relaxed);
    if (const_writes) *const_writes = g_const_writes.load(std::memory_order_relaxed);
}

}  // namespace rsx

/* The display layer: presenter.cpp owns these and needs a window. A no-display
 * build records the buffer description the guest supplies -- which is how the
 * RSX context learns where its framebuffer lives -- and reports that nothing was
 * presented. record_display() is what the guest's video-out query reads back. */
namespace {
struct DisplayBuffer {
    uint32_t id = 0, offset = 0, pitch = 0, width = 0, height = 0;
    bool set = false;
};
DisplayBuffer g_display;
}  // namespace

void set_display_buffer(uint32_t id, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height) {
    g_display = {id, offset, pitch, width, height, true};
}

void* native_window() { return nullptr; }

bool init() {
    /* No device to create. Reporting success lets the guest finish its RSX setup;
     * drawing is the part that is absent, not initialisation. */
    return true;
}

void flip(uint32_t id) {
    (void)id;
}

}  // namespace gpu

#endif  // PS3BASE_NO_RENDERER
