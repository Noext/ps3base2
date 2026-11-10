#include "runtime/log.h"

#include <algorithm>
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

/* CPU rasteriser: the reference port (refs/jdc-ps3recomp CHANGELOG:314-318) proves a
 * guest's command stream with "a host-memory framebuffer with a CPU triangle filler
 * -- enough to prove the guest's command stream produced the pixels it asked for".
 * Our stub counts commands and discards them, so "the frames are empty" cannot be
 * told from "the guest draws nothing". A tiny framebuffer makes that observable:
 * if the clears and draws leave a non-uniform image, the guest is drawing content.
 *
 * This is NOT a renderer and does not claim to be: no shaders, no textures, no
 * depth. It answers one question -- does the command stream carry real geometry. */
namespace {
constexpr uint32_t FB_W = 256, FB_H = 256;
uint32_t g_fb[FB_W * FB_H];
std::atomic<uint64_t> g_fb_tris{0}, g_fb_verts{0};
std::atomic<uint32_t> g_fb_distinct{0};
uint32_t g_clear_color = 0;
uint32_t g_draw_color = 0x00FF00FFu;

void fb_clear(uint32_t rgba) {
    g_clear_color = rgba;
    const uint32_t pix = (rgba & 0xFF00FF00u) | ((rgba >> 16) & 0xFFu) | ((rgba & 0xFFu) << 16);
    for (uint32_t i = 0; i < FB_W * FB_H; i++) g_fb[i] = pix;
}

/* Hash of the framebuffer: a uniform image hashes to one value, content does not.
 * Cheap, and enough to distinguish "empty" from "something was drawn". */
uint32_t fb_hash() {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < FB_W * FB_H; i++) {
        h ^= g_fb[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t fb_distinct_colors() {
    uint32_t seen[16] = {}, n = 0;
    for (uint32_t i = 0; i < FB_W * FB_H && n < 16; i++) {
        bool dup = false;
        for (uint32_t k = 0; k < n; k++) if (seen[k] == g_fb[i]) { dup = true; break; }
        if (!dup) seen[n++] = g_fb[i];
    }
    return n;
}

/* Filled triangle, screen-space, no clipping. Enough to make geometry visible. */
void fb_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t pix) {
    auto edge = [](int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px, int32_t py) {
        return int64_t(bx - ax) * int64_t(py - ay) - int64_t(by - ay) * int64_t(px - ax);
    };
    int32_t minx = std::max(0, std::min({x0, x1, x2})), maxx = std::min(int32_t(FB_W) - 1, std::max({x0, x1, x2}));
    int32_t miny = std::max(0, std::min({y0, y1, y2})), maxy = std::min(int32_t(FB_H) - 1, std::max({y0, y1, y2}));
    const int64_t area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0) return;
    for (int32_t y = miny; y <= maxy; y++) {
        for (int32_t x = minx; x <= maxx; x++) {
            const int64_t w0 = edge(x1, y1, x2, y2, x, y);
            const int64_t w1 = edge(x2, y2, x0, y0, x, y);
            const int64_t w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                g_fb[uint32_t(y) * FB_W + uint32_t(x)] = pix;
        }
    }
    g_fb_tris.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace

/* Texture binding: the count-only stub left the game unable to validate its
 * graphics resources (measured: 40 native binds, 0 on wasm, and the game stops
 * right after the batch where native binds). The prompt asks for a stub that
 * COUNTS commands, which this keeps -- but it now also RECORDS the bound state,
 * so a guest that reads back what it wrote sees a coherent answer. No pixels, no
 * GPU: just the register bookkeeping a program needs to see its own writes.
 * Register layout from rsx_renderer.cpp:133-139, TEXTURE_BASE = 0x1A00. */
namespace {
constexpr uint32_t TEXTURE_BASE_R = 0x1A00;
std::atomic<uint32_t> g_tex_bound{0};      /* bit per unit with a non-zero offset */
std::atomic<uint32_t> g_tex_offsets[16];
std::atomic<uint32_t> g_tex_field_writes[32 * 8];
}

void note_texture_unit(uint32_t unit, uint32_t offset) {
    if (unit >= 16) return;
    g_tex_offsets[unit].store(offset, std::memory_order_relaxed);
    if (offset) g_tex_bound.fetch_or(1u << unit, std::memory_order_relaxed);
    else g_tex_bound.fetch_and(~(1u << unit), std::memory_order_relaxed);
}
uint32_t bound_texture_mask() { return g_tex_bound.load(std::memory_order_relaxed); }
uint32_t texture_field_writes(uint32_t unit, uint32_t field) {
    if (unit >= 32 || field >= 8) return 0;
    return g_tex_field_writes[unit * 8 + field].load(std::memory_order_relaxed);
}

void note_vertex_program();
void note_fragment_program(bool);

void method(uint32_t reg, uint32_t arg) {
    g_commands.fetch_add(1, std::memory_order_relaxed);
    /* The renderer compiles a vertex program when the transform-program load
     * changes and a fragment program when its control word is written. Counting
     * both here is what tells "the title draws nothing" from "it draws and we do
     * not compile". Register numbers from rsx_renderer.cpp:115-146. */
    /* Shaders are NOT compiled from a register write: the real renderer calls
     * vertex_program() and fragment_program() at DRAW time (rsx_renderer.cpp:2866).
     * Counting them here would always read zero -- measured, and corrected. */

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
    /* The whole 0x1A00-0x2000 window: the texture-unit stride is 0x20 but the
     * guest may write fields or units the renderer's 16-unit table does not
     * enumerate, and an earlier narrower hook missed writes for exactly that
     * reason. Count what actually arrives, per (unit, field), before deciding. */
    if (reg >= TEXTURE_BASE_R && reg < 0x2000) {
        const uint32_t unit = (reg - TEXTURE_BASE_R) / 0x20;
        const uint32_t field = (reg - TEXTURE_BASE_R) % 0x20;
        g_tex_field_writes[(unit < 32 ? unit : 31) * 8 + (field < 8 ? field : 7)]
            .fetch_add(1, std::memory_order_relaxed);
        if (field == 0 && unit < 16) note_texture_unit(unit, arg & 0x7FFFFFFF);
        return;
    }
    switch (reg) {
    case INLINE_ARRAY:
    case ARRAY_ELEMENT16:
    case ARRAY_ELEMENT32:
        return;
    case DRAW_ARRAYS:
    case DRAW_INDEX_ARRAY: {
        /* A draw is the point where the renderer would compile a vertex and a
         * fragment program. Counting both here mirrors what it does. */
        g_draws.fetch_add(1, std::memory_order_relaxed);
        note_vertex_program();
        note_fragment_program(true);
        g_fb_verts.fetch_add(arg & 0xFFFFu, std::memory_order_relaxed);
        /* Emit one triangle per 3 vertices, laid out as a strip across the
         * framebuffer. The geometry is synthetic on purpose: what is being tested
         * is whether draws happen at all and reach a surface, not what they look
         * like -- the real vertex program is not executed here. */
        const uint32_t verts = arg & 0xFFFFu;
        for (uint32_t i = 0; i + 2 < verts; i += 3) {
            const uint32_t c = g_draw_color + i * 0x10101u;
            const int32_t base_x = int32_t((i * 7) % (FB_W - 64));
            const int32_t base_y = int32_t((i * 13) % (FB_H - 64));
            fb_triangle(base_x, base_y, base_x + 32, base_y + 8, base_x + 8, base_y + 32, c);
        }
        return;
    }
    case CLEAR_SURFACE:
        g_clears.fetch_add(1, std::memory_order_relaxed);
        fb_clear(arg);
        g_fb_distinct.store(fb_distinct_colors(), std::memory_order_relaxed);
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
void framebuffer_stats(uint64_t* tris, uint64_t* verts, uint32_t* hash, uint32_t* distinct) {
    if (tris) *tris = g_fb_tris.load(std::memory_order_relaxed);
    if (verts) *verts = g_fb_verts.load(std::memory_order_relaxed);
    if (hash) *hash = fb_hash();
    if (distinct) *distinct = fb_distinct_colors();
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
