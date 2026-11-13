/* The 27 support symbols rsx_renderer.cpp needs in the wasm build, where the
 * modules that own them are left out on purpose.
 *
 * Why each is absent, and why a stub is the right answer rather than compiling its
 * owner:
 *
 *  - compile_glsl / load_cached_glsl: the GLSL->SPIR-V chain is glslang, a host
 *    library with no wasm build here. The web path does not use SPIR-V: the WGSL
 *    emitter (runtime/src/gpu/wgsl_emit.cpp) writes shaders directly. Returning
 *    false makes the renderer MARK the program un-compiled instead of pretending
 *    it has one, so "the shader did not translate" stays distinguishable from "the
 *    shader is wrong".
 *  - edge_compute::*: Edge of Tomorrow's GPU accelerator; this title has none.
 *  - mv::* (motion vectors), dlssg/dlssnr: DLSS input capture, needs a real GPU
 *    and NVIDIA extensions.
 *  - presenter.cpp (flip, enabled, set_display_buffer, display_buffer,
 *    frame_serial): owns the swapchain and a native window. There is neither.
 *  - rsx stats helpers: defined in rsx_no_display.cpp under PS3BASE_NO_RENDERER,
 *    which this build does NOT define any more -- the real renderer supplies the
 *    RSX entry points now, so those counters must come from somewhere else.
 *  - edge::gpu::find: the Edge GPU record cache.
 *
 * None of these draws anything. The renderer's DECODE -- vertex expansion, texture
 * decode, program decompile, uniform assembly -- is what this build exists to run,
 * and it does not touch any symbol below.
 */
#if defined(__EMSCRIPTEN__)

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "shader_compiler.h"
#include "../spu/edge/edge_gpu.h"
#include "ring.h"
#include "vk_context.h"

namespace gpu {

/* --- the GLSL/SPIR-V chain: honestly absent on the web ---------------------- */
bool compile_glsl(ShaderStage, const std::string&, std::vector<uint32_t>&, std::string& log) {
    log = "glslang is not part of the wasm build; the web path emits WGSL";
    return false;
}
bool load_cached_glsl(ShaderStage, const std::string&, std::vector<uint32_t>&) { return false; }

/* --- Edge of Tomorrow accelerator: not this title --------------------------- */
namespace edge_compute {
/* TRUE, and this is the one place a "yes" is not a pretend-Vulkan.
 *
 * gpu::enabled() is read by the RENDERER's present() as "may I finish a frame?"
 * (rsx_renderer.cpp:4519). With false, present() returns before end_pass() and
 * before every diagnostic it owns -- and the renderer's own counters (draws,
 * skipped, the per-line skip reasons, textures, surfaces) are printed ONLY there.
 * Measured with false: 1321 flips and not one line saying why nothing was drawn.
 *
 * With true, present() runs to the end; present_image() is the null stub, so no
 * Vulkan submission happens and nothing is displayed. What the true answer buys is
 * the frame boundary itself, which is what the decode path needs in order to
 * report what it decoded. */
bool enabled() { return true; }
bool verify() { return false; }
void expand(const edge::gpu::Record&, RingAlloc&) {}
void add_check(const float*, const float*, uint32_t, uint32_t) {}
void frame_complete() {}
}  // namespace edge_compute

/* --- DLSS / motion vectors: need a NVIDIA device ---------------------------- */
namespace mv {
bool enabled() { return false; }
void set_camera(const float*, size_t, uint32_t, uint32_t) {}
bool camera(uint32_t&, uint32_t&) { return false; }
int run(VkCommandBuffer, VkImage, VkImage, uint32_t, uint32_t, double) { return 0; }
std::string dump_base(double) { return std::string(); }
double elapsed() { return 0.0; }
void complete() {}
bool pending() { return false; }
VkImage output(VkImageLayout&) { return VK_NULL_HANDLE; }
const float* camera_consts() { return nullptr; }
}  // namespace mv

/* --- the presenter: no swapchain, no window ---------------------------------
 *
 * But NOT a black hole. The renderer asks display_buffer() for the guest's
 * framebuffer description at every present, and a false answer makes present()
 * return early -- which is also where the renderer's own counters (draws, skipped,
 * per-line skip reasons) are reported. Measured with a false answer: flips advanced
 * to 361 and the log said nothing at all about why nothing was drawn.
 *
 * So the guest's own description is RECORDED and answered truthfully. Nothing is
 * presented (there is no swapchain), and enabled() still says false so no Vulkan
 * submission is attempted. */
namespace {
struct DisplayBuffer {
    uint32_t offset = 0, pitch = 0, width = 0, height = 0;
    bool set = false;
};
DisplayBuffer g_display[8];
}  // namespace

/* TRUE, and this is the one place a "yes" does not pretend Vulkan works.
 *
 * gpu::enabled() is read by the RENDERER as "may I run a frame?" in two places
 * that gate the whole decode path:
 *   rsx_renderer.cpp:4386  method() returns immediately -- abandoning every RSX
 *                          register write, including BEGIN_END and its draw()
 *   rsx_renderer.cpp:4519  present() returns before end_pass()
 * Measured with false: 700 009 method() calls, 62 679 BEGIN_END pairs, and
 * enabled()==false on all 700 009 of them, so draws stayed 0 and not one pixel of
 * state was decoded. This is exactly the "nothing is drawn" that must not be
 * confused with "nothing is decoded".
 *
 * With true, the decode path runs to completion; present_image() is the null stub,
 * so no Vulkan submission happens and nothing is displayed. What the true answer
 * buys is the decode itself, which is the half the web port needs. */
bool enabled() { return true; }
void flip(uint32_t) {}
void set_display_buffer(uint32_t id, uint32_t offset, uint32_t pitch, uint32_t width, uint32_t height) {
    if (id >= 8) return;
    g_display[id] = {offset, pitch, width, height, true};
}
bool display_buffer(uint32_t id, uint32_t& offset, uint32_t& pitch, uint32_t& width, uint32_t& height) {
    if (id >= 8 || !g_display[id].set) return false;
    offset = g_display[id].offset;
    pitch = g_display[id].pitch;
    width = g_display[id].width;
    height = g_display[id].height;
    return true;
}
/* T1: this returned a CONSTANT 0, and that is a ring defect, not a neutral stub.
 *
 * gpu/ring.cpp rotates its two slots on frame_serial() changing:
 *
 *     if (serial != g_serial) { g_serial = serial; g_base = (serial % 2) * RING_SIZE; g_head = 0; }
 *
 * With a constant serial the branch never fires, so g_head only ever grows and the
 * ring is never reclaimed. Measured after the ring was made to fit: 46 528
 * "ring buffer exhausted in one frame (7488 bytes requested)" -- a 64 MB ring
 * refusing 7 KB, because "one frame" was the whole run. Every draw then skipped at
 * rsx_renderer.cpp:3509 and draws stayed 0.
 *
 * The serial has to advance once per presented frame. The presenter owns the real
 * counter (presenter.cpp:300 g_frame_serial, incremented at :491) but the wasm build
 * does not link presenter.cpp, so this file keeps its own, advanced by the flip the
 * guest performs. That is the same event the native counter follows. */
/* frame_serial() stays CONSTANT 0, as it was.
 *
 * Making it advance was tried and REVERTED: it is not only the ring's slot
 * selector. rsx_renderer.cpp uses it for surface retirement (1428, 1455), texture
 * last_used (2179, 2201), the draw-time serial (3244, 3506) and the writeback ring
 * index (4645). Advancing it from present() changed all of those at once, and the
 * measured result was the FIFO thread spinning at "busy 99.8%" with
 * "methods 0.1% (0k/s)" and n_method frozen at 249 -- a stall, not a picture.
 *
 * The ring's slot rotation is fixed where it belongs instead: ring.cpp rotates on
 * its own presented-frame counter, which is the only consumer that needs it. */
uint64_t frame_serial() { return 0; }

}  // namespace gpu

namespace gpu::rsx {
/* Defined in rsx_no_display.cpp only when PS3BASE_NO_RENDERER is set. This build
 * links the real renderer, so they are counted here as zero rather than left
 * undefined: a stats call is not a draw and must never be the reason a run stops. */
void shader_stats(uint64_t* vp, uint64_t* fp, uint64_t* ok) {
    if (vp) *vp = 0;
    if (fp) *fp = 0;
    if (ok) *ok = 0;
}
void low_reg_stats(uint32_t* v) { if (v) *v = 0; }
void draw_shape_stats(uint32_t* a, uint32_t* b) { if (a) *a = 0; if (b) *b = 0; }
void framebuffer_stats(uint64_t* tris, uint64_t* verts, uint32_t* hash, uint32_t* distinct) {
    if (tris) *tris = 0;
    if (verts) *verts = 0;
    if (hash) *hash = 0;
    if (distinct) *distinct = 0;
}
void surface_reg_stats(uint32_t* v) { if (v) *v = 0; }
uint32_t bound_texture_mask() { return 0; }
uint32_t texture_field_writes(uint32_t, uint32_t) { return 0; }
}  // namespace gpu::rsx

namespace edge::gpu {
std::shared_ptr<const Record> find(uint32_t) { return nullptr; }
}  // namespace edge::gpu

#endif  // __EMSCRIPTEN__
