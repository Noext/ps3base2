/* The WebGPU device that runs BESIDE the Vulkan renderer.
 *
 * The header says why it exists and what it consumes. This file is the device
 * plumbing: instance, adapter, device, and the objects a draw needs.
 *
 * Choices that were forced, not chosen:
 *
 *  - ONE instance, created with ShaderSourceSPIRV required. wgpu-native only
 *    accepts a SPIR-V shader module when the INSTANCE asked for that feature;
 *    there is no device-level equivalent, and without it module creation fails
 *    at the first draw rather than at setup.
 *
 *  - An uncaptured-error callback ALWAYS installed. wgpu-native panics the
 *    process on an uncaptured error, so the first invalid combination would
 *    kill the run instead of reporting a bad format. Capturing turns a Rust
 *    panic into a counted, named failure.
 *
 *  - The depth attachment is ALWAYS present in the pass. WebGPU rejects a
 *    setPipeline whose pipeline declares a depth format when the pass has no
 *    depth attachment, and it drops the whole command buffer rather than
 *    drawing without depth -- a black frame with no exception. Vulkan only
 *    warns. This is the single place the two backends differ in shape.
 */
#include "gpu/wgpu_backend.h"

#include <webgpu/webgpu.h>
#if defined(__EMSCRIPTEN__)
/* The browser takes WGSL, not SPIR-V, and has no <webgpu/wgpu.h>. The shim
 * supplies the one symbol the port lacks (wgpuDevicePoll) and leaves the C API
 * identical, so the rest of this file is the same code on both targets. */
#include "gpu/wgpu_web_shim.h"
#else
#include <webgpu/wgpu.h>
#endif

#include "runtime/log.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif
#include "ring.h"   /* gpu::frame_serial(): the per-frame counter the canvas re-import keys on */

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>
#include <string>
#include <vector>

/* Implemented in web/s1-bridge.js. Declared at FILE scope, before the anonymous
 * namespace: a declaration inside an anonymous namespace is a different entity,
 * the linker then sees a fresh undefined symbol, and the wasm import aborts on
 * the first call. (Measured in this workspace before: the same mistake on the
 * ring's io hook produced "RuntimeError: unreachable" on the first texture.) */
#if defined(__EMSCRIPTEN__)
extern "C" {
/* The bridge entry that turns a WGPUTexture pointer into its JS object. Declared
 * here so the wasm IMPORTS it: a library function nothing imports is tree-shaken
 * out of game.js (measured twice on this track). */
void* t1_get_js_texture(uint32_t id);
/* Configure the canvas for the adopted device and size, once per size change. */
int s1_canvas_configure(uint32_t w, uint32_t h);
/* The current frame's canvas texture, imported as a WGPUTexture. */
/* The canvas's WGPUTextureFormat, from the browser's preferred format. */
int s1_canvas_format();
/* The device the page created and this module adopted, obtained on the realm
 * that owns it. See web/s1-bridge.js: s1_adopt_device() wraps the port's
 * emscripten_webgpu_get_device() and is __proxy:'sync', because calling that
 * from a guest pthread fills a per-realm table the pthread cannot use. */
WGPUDevice s1_adopt_device();
}
#endif

namespace gpu {
namespace wgpu {

namespace {

struct StatsStore {
    uint64_t last_fp = 0;
    uint32_t last_mask = 0;
    uint64_t draws = 0;
    uint64_t pipelines = 0;
    uint64_t textures = 0;
    uint64_t upload_bytes = 0;
    uint64_t skipped = 0;
    std::string first_error;
};

struct Device {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    bool ready = false;
    /* True on the browser target: the device was adopted from JS and the colour
     * target is the CANVAS texture, which is re-imported every frame because a
     * canvas texture is invalidated at the next getCurrentTexture(). */
    bool web = false;
    uint32_t width = 0;
    uint32_t height = 0;

    WGPUTexture color = nullptr;
    /* T1 -- THE PERSISTENT OFFSCREEN TARGET.
     *
     * A canvas texture is destroyed by the browser after it is composed. Importing
     * one in end_frame() for the NEXT frame therefore hands every draw a dead
     * texture: measured, 91 magenta clears issued into a valid view, 0 validation
     * errors, and a black canvas. Nothing may be kept across animation callbacks.
     *
     * So all rendering goes into THIS texture, which we own and which never expires,
     * and the canvas texture is acquired, written and released inside the single
     * callback that presents. */
    WGPUTexture offscreen = nullptr;
    WGPUTextureView offscreen_view = nullptr;
    WGPUTextureView color_view = nullptr;
    WGPUTexture depth = nullptr;
    WGPUTextureView depth_view = nullptr;
    WGPUTextureFormat color_format = WGPUTextureFormat_BGRA8Unorm;
    WGPUTextureFormat depth_format = WGPUTextureFormat_Depth24PlusStencil8;

    WGPUCommandEncoder encoder = nullptr;
    WGPURenderPassEncoder pass = nullptr;

    WGPUBindGroupLayout bind_layout = nullptr;
    WGPUPipelineLayout pipe_layout = nullptr;
    /* WebGPU has no sampler-anisotropy FEATURE and no limits entry for it: the
     * descriptor field is always accepted, so the RSX anisotropy level is
     * honoured unconditionally. The flag stays so a backend that grows the
     * feature later has one place to consult. */
    bool aniso = true;

    /* Shader modules keyed by the hash the Vulkan path already computes, so a
     * program is translated once however many draws name it. */
    std::map<uint64_t, WGPUShaderModule> vs_modules;
    std::map<uint64_t, WGPUShaderModule> fs_modules;

    struct PipeKey {
        uint64_t vs = 0, fs = 0;
        uint32_t color_format = 0, depth_format = 0;
        uint32_t topology = 0, cull = 0;
        uint32_t blend_enable = 0, sfactor = 0, dfactor = 0, equation = 0;
        uint32_t sfactor_alpha = 0, dfactor_alpha = 0, equation_alpha = 0;
        uint32_t write_mask = 0;
        uint32_t depth_test = 0, depth_write = 0, depth_compare = 0;
        uint32_t cw = 0;
        uint32_t inputs = 0;
        bool operator<(const PipeKey& o) const { return std::memcmp(this, &o, sizeof *this) < 0; }
    };
    std::map<PipeKey, WGPURenderPipeline> pipelines;

    /* One vertex/index/uniform buffer set per draw, released when the frame is
     * submitted. A ring allocator is what a real port would use; this backend
     * exists to be measured, and hiding the allocation cost would hide the
     * number this measurement is about. */
    std::vector<WGPUBuffer> frame_buffers;
    int errors = 0;
    char first_error[512] = {0};
};

Device g;
StatsStore s;

void on_uncaptured(WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void* u1, void*) {
    auto* d = static_cast<Device*>(u1);
    const int len = int(msg.length < 400 ? msg.length : 400);
    if (!d->first_error[0]) std::snprintf(d->first_error, sizeof d->first_error, "%.*s", len, msg.data ? msg.data : "");
    d->errors++;
    if (s.first_error.empty()) {
        s.first_error = std::string(msg.data ? msg.data : "", size_t(len));
    }
    /* Every error, up to a few, so a chain is not hidden by its first link. */
    static int shown = 0;
    if (shown++ < 6) std::fprintf(stderr, "wgpu: ERROR: %.*s\n", len, msg.data ? msg.data : "");
    (void)type;
}

struct AdapterResult {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView msg, void* u1, void*) {
    auto* r = static_cast<AdapterResult*>(u1);
    if (status != WGPURequestAdapterStatus_Success) {
        std::fprintf(stderr, "wgpu: adapter request failed: %.*s\n", int(msg.length), msg.data ? msg.data : "");
    } else {
        r->adapter = adapter;
    }
    r->done = true;
}

struct DeviceResult {
    WGPUDevice device = nullptr;
    bool done = false;
};

void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView msg, void* u1, void*) {
    auto* r = static_cast<DeviceResult*>(u1);
    if (status != WGPURequestDeviceStatus_Success) {
        std::fprintf(stderr, "wgpu: device request failed: %.*s\n", int(msg.length), msg.data ? msg.data : "");
    } else {
        r->device = device;
    }
    r->done = true;
}

/* Poll until an asynchronous request resolves.
 *
 * wgpu-native v29.0.1.1 does not implement wgpuInstanceWaitAny at ALL: with a
 * non-zero timeout it reaches src/unimplemented.rs and panics the process, and
 * with WGPUInstanceFeatureName_TimedWaitAny requested it still does. With a zero
 * timeout it returns, but only ever reports completed for a future whose callback
 * has ALREADY run -- so it cannot be used to wait for anything.
 *
 * wgpuInstanceProcessEvents does implement the callbacks (webgpu.h: "fire inside\n * a call to wgpuInstanceProcessEvents if the asynchronous operation is complete"),
 * so the wait is a poll of that plus a short sleep. Measured, not assumed: the
 * three WaitAny variants were each reproduced in a standalone program before this
 * was changed.
 *
 * `done` is the callback's flag for the request, not the future: the future is
 * only useful to WaitAny, which does not work.
 */
bool poll_until(const bool& done) {
    for (int i = 0; i < 20000; i++) {
        /* THE INSTANCE IS NULL ON THE WEB PATH, and that is why every map failed.
         *
         * g.instance is created only in the NATIVE init(); the wasm init adopts a
         * device the page made and never creates an instance. wgpuInstanceProcessEvents
         * on a null instance does nothing, so the map callback never ran and readback
         * reported "map failed" -- measured at flip 30 of a run that had replayed 59
         * draws with 0 GPU errors. On the web the device drives its own futures through
         * the browser's event loop, so wgpuDevicePoll is what pumps them. */
        if (g.instance) wgpuInstanceProcessEvents(g.instance);
        else if (g.device) {
            /* wait=true: the device BLOCKS until its futures have advanced. With
             * wait=false the poll returns immediately and the sleep that follows blocks
             * the JS thread, so the map callback never gets a task boundary -- measured:
             * "PROBE map failed" on every attempt. */
            wgpuDevicePoll(g.device, true, nullptr);
        }
        if (done) return true;
        if (!g.device) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return done;
}

WGPUShaderModule module_from_spirv(const uint32_t* words, size_t count) {
    WGPUShaderSourceSPIRV spirv = WGPU_SHADER_SOURCE_SPIRV_INIT;
    spirv.codeSize = uint32_t(count);
    spirv.code = words;
    WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    d.nextInChain = &spirv.chain;
    return wgpuDeviceCreateShaderModule(g.device, &d);
}

/* The browser's shader source. The RSX program was emitted as WGSL by
 * runtime/src/gpu/wgsl_emit.cpp (track H3: 11/11 real Minecraft programs valid
 * in naga and in Chromium), so there is no GLSL and no SPIR-V in between. */
WGPUShaderModule module_from_wgsl(const std::string& code) {
    if (code.empty()) return nullptr;
    WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
    wg.code = {code.c_str(), code.size()};
    WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    d.nextInChain = &wg.chain;
    return wgpuDeviceCreateShaderModule(g.device, &d);
}

/* One shader module for a draw, from whichever source this target can take.
 *
 * Native: the renderer's own SPIR-V, passed through unchanged, so the two
 * backends differ by the API and nothing else (that is what makes their pixel
 * comparison mean anything).
 * Web: the WGSL emitted for the same program, because Chromium has no SPIR-V
 * device feature at all.
 * A refused module is reported through g.errors, which the pipeline path uses to
 * count a skip instead of drawing with a broken pipeline and losing the frame. */
WGPUShaderModule shader_module(const std::string& wgsl, const uint32_t* spirv, size_t words) {
#if defined(__EMSCRIPTEN__)
    (void)spirv; (void)words;
    return module_from_wgsl(wgsl);
#else
    (void)wgsl;
    return module_from_spirv(spirv, words);
#endif
}

WGPUBuffer make_buffer(WGPUBufferUsage usage, uint64_t size, const void* data) {
    WGPUBufferDescriptor d = WGPU_BUFFER_DESCRIPTOR_INIT;
    d.usage = usage;
    d.size = size;
    d.mappedAtCreation = data != nullptr;
    WGPUBuffer b = wgpuDeviceCreateBuffer(g.device, &d);
    if (!b) return nullptr;
    if (data) {
        if (void* dst = wgpuBufferGetMappedRange(b, 0, size)) std::memcpy(dst, data, size_t(size));
        wgpuBufferUnmap(b);
    }
    g.frame_buffers.push_back(b);
    return b;
}

void release_frame_buffers() {
    for (WGPUBuffer b : g.frame_buffers) wgpuBufferRelease(b);
    g.frame_buffers.clear();
}

void end_pass();

/* Build the target and report whether it exists. The callers need the verdict:
 * a null offscreen texture is a target failure, not a frame to submit. */
/* T1 -- what the PAGE needs to present: the id of the texture the C++ rendered
 * into, and how many frames it has finished. The page reads the JS texture with
 * WebGPU.getJsObject(id) and copies it into the canvas itself. */
std::atomic<uint32_t> g_debug_mode{0};
std::atomic<uint32_t> g_exported_texture_id{0};
std::atomic<uint64_t> g_frames_done{0};

bool make_targets_ok(uint32_t width, uint32_t height);

void make_targets(uint32_t width, uint32_t height) {
#if defined(__EMSCRIPTEN__)
    /* THE PERSISTENT OFFSCREEN TARGET, not the canvas texture.
     *
     * The canvas texture is destroyed by the browser once it has been composed, so a
     * view imported for the next animation frame is a view of a dead texture. That is
     * exactly what the bisection measured: 91 magenta clears issued into a view that
     * WebGPU accepted without error, and a canvas that stayed black.
     *
     * Everything therefore draws into this texture, which we own and which never
     * expires. The canvas texture is acquired, written and released inside the single
     * callback that presents (see end_frame). */
    if (!g.offscreen || g.width != width || g.height != height) {
        if (g.offscreen_view) { wgpuTextureViewRelease(g.offscreen_view); g.offscreen_view = nullptr; }
        if (g.offscreen) { wgpuTextureRelease(g.offscreen); g.offscreen = nullptr; }
        WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D;
        td.size = {width, height, 1};
        td.format = g.color_format;
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        g.offscreen = wgpuDeviceCreateTexture(g.device, &td);
        if (g.offscreen) g.offscreen_view = wgpuTextureCreateView(g.offscreen, nullptr);
        {
            static std::atomic<uint32_t> n{0};
            if (n.fetch_add(1) < 4)
                LOG_INFO("wgpu", "offscreen target %ux%u tex %p view %p", width, height,
                         (void*)g.offscreen, (void*)g.offscreen_view);
        }
    }
    if (!g.depth || g.width != width || g.height != height) {
        if (g.depth_view) { wgpuTextureViewRelease(g.depth_view); g.depth_view = nullptr; }
        if (g.depth) { wgpuTextureRelease(g.depth); g.depth = nullptr; }
        WGPUTextureDescriptor dz = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dz.usage = WGPUTextureUsage_RenderAttachment;
        dz.dimension = WGPUTextureDimension_2D;
        dz.size = {width, height, 1};
        dz.format = g.depth_format;
        dz.mipLevelCount = 1;
        dz.sampleCount = 1;
        g.depth = wgpuDeviceCreateTexture(g.device, &dz);
        if (g.depth) g.depth_view = wgpuTextureCreateView(g.depth, nullptr);
    }
    g.width = width;
    g.height = height;
    return;
#else
    static int resizes = 0;
    if (g.color && g.width == width && g.height == height) return;
    if (resizes++ < 6) std::fprintf(stderr, "wgpu: target resize %d -> %ux%u (had %ux%u) encoder %p\n",
                             resizes, width, height, g.width, g.height, (void*)g.encoder);
    /* A resize destroys the old views. If an encoder already references
     * them the submit is rejected ("TextureView with ... is invalid"), so
     * the recorded frame is dropped rather than resized underneath. The
     * renderer resizes at a surface change, which is a frame boundary. */
    if (g.encoder) {
        end_pass();
        wgpuCommandEncoderRelease(g.encoder);
        g.encoder = nullptr;
        release_frame_buffers();
    }
    if (g.color) {
        wgpuTextureViewRelease(g.color_view);
        wgpuTextureRelease(g.color);
        g.color = nullptr;
        g.color_view = nullptr;
    }
    if (g.depth) {
        wgpuTextureViewRelease(g.depth_view);
        wgpuTextureRelease(g.depth);
        g.depth = nullptr;
        g.depth_view = nullptr;
    }
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {width, height, 1};
    td.format = g.color_format;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    g.color = wgpuDeviceCreateTexture(g.device, &td);
    if (g.color) g.color_view = wgpuTextureCreateView(g.color, nullptr);

    WGPUTextureDescriptor dz = WGPU_TEXTURE_DESCRIPTOR_INIT;
    dz.usage = WGPUTextureUsage_RenderAttachment;
    dz.dimension = WGPUTextureDimension_2D;
    dz.size = {width, height, 1};
    dz.format = g.depth_format;
    dz.mipLevelCount = 1;
    dz.sampleCount = 1;
    g.depth = wgpuDeviceCreateTexture(g.device, &dz);
    if (g.depth) g.depth_view = wgpuTextureCreateView(g.depth, nullptr);
    g.width = width;
    g.height = height;
    /* Clear both attachments once. A newly created texture has undefined
     * contents and WebGPU REJECTS a pass that LOADs it ("Attachment without\n     * load"), which it reports at submit and then aborts on. Clearing here
     * gives the pass something to load and matches what the Vulkan path does
     * before its first draw. */
    {
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ca.view = g.color_view;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        da.view = g.depth_view;
        da.depthLoadOp = WGPULoadOp_Clear;
        da.depthStoreOp = WGPUStoreOp_Store;
        da.stencilLoadOp = WGPULoadOp_Clear;
        da.stencilStoreOp = WGPUStoreOp_Store;
        da.stencilClearValue = 0;
        da.depthClearValue = 1.0f;
        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        rp.depthStencilAttachment = &da;
        WGPURenderPassEncoder p = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderEnd(p);
        wgpuRenderPassEncoderRelease(p);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuCommandEncoderRelease(enc);
        wgpuQueueSubmit(g.queue, 1, &cb);
        wgpuCommandBufferRelease(cb);
    }
#endif  // __EMSCRIPTEN__
}

bool make_targets_ok(uint32_t width, uint32_t height) {
    make_targets(width, height);
#if defined(__EMSCRIPTEN__)
    if (!g.offscreen || !g.offscreen_view) return false;
    /* The id the PAGE presents with. WebGPU.getJsObject(id) returns this exact
     * texture object, so the page copies the texture the C++ actually rendered
     * into -- no import, no internal allocator, no second object table. */
    g_exported_texture_id.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g.offscreen)),
                       std::memory_order_release);
    return true;
#else
    return g.color != nullptr;
#endif
}


void begin_pass() {
    if (g.pass) return;
    if (!g.encoder) g.encoder = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = g.offscreen_view;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    /* On the web this is a fresh canvas texture every frame: WebGPU requires the
     * attachment to be CLEARED, because a canvas texture's contents at
     * getCurrentTexture() are undefined. Loading it is the native behaviour and
     * would be reading undefined memory here. */
    ca.loadOp = g.web ? WGPULoadOp_Clear : WGPULoadOp_Load;
    if (g.web) ca.clearValue = {0.0, 0.0, 0.0, 1.0};
    /* MODE 3 clears to magenta, so a visible background proves the pass ran even if
     * the game's triangle is off-screen or degenerate. */
    if (g_debug_mode.load() == 3) ca.clearValue = {1.0, 0.0, 1.0, 1.0};
    ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    da.view = g.depth_view;
    da.depthLoadOp = WGPULoadOp_Load;
    da.depthStoreOp = WGPUStoreOp_Store;
    da.stencilLoadOp = WGPULoadOp_Load;
    da.stencilStoreOp = WGPUStoreOp_Store;
    /* WebGPU validates depthClearValue EVEN WHEN depthLoadOp is Load, and
     * WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT leaves it zero-initialised --
     * which is a valid 0.0f, but the struct is then copied through the port's
     * descriptor reader and a field never written can arrive non-finite. Measured:
     * "Failed to read the 'depthClearValue' property ... The provided float value is
     * non-finite", and the whole command buffer was dropped: no frame was ever
     * submitted, so the canvas stayed black with draws_ready climbing. Set it
     * explicitly on every path. */
    da.depthClearValue = 1.0f;
    da.stencilClearValue = 0;
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    static const bool nd = std::getenv("PS3BASE_WGPU_NO_DEPTH") != nullptr;
    rp.depthStencilAttachment = nd ? nullptr : &da;
    g.pass = wgpuCommandEncoderBeginRenderPass(g.encoder, &rp);
    static uint64_t passes = 0;
    passes++;
    if (passes <= 6) std::fprintf(stderr, "wgpu: begin pass %llu (encoder %d)\n", (unsigned long long)passes, g.encoder != nullptr);
}

void end_pass() {
    if (!g.pass) return;
    { static uint64_t ends = 0; ends++; if (ends <= 6) std::fprintf(stderr, "wgpu: end pass %llu\n", (unsigned long long)ends); }
    wgpuRenderPassEncoderEnd(g.pass);
    wgpuRenderPassEncoderRelease(g.pass);
    g.pass = nullptr;
}

WGPURenderPipeline pipeline_for(const DrawInputs& in) {
    /* MODE 3: the game's vertex program with OUR fixed-red fragment shader.
     *
     * This isolates the vertex stage. If the triangle appears, the positions the
     * game's vertex shader produced are on screen and the vertex path is correct; if
     * it does not, the problem is in those positions (transform constants, viewport,
     * scissor, clip-space depth) and not in the fragment program or the textures. */
    const uint32_t dbg_mode = g_debug_mode.load();
    if (dbg_mode == 3 && !in.vs_wgsl.empty()) {
        static WGPURenderPipeline red_pipe = nullptr;
        static uint64_t red_vp = 0;
        if (!red_pipe || red_vp != in.vp_hash) {
            red_vp = in.vp_hash;
            WGPUShaderModule vs = module_from_wgsl(in.vs_wgsl);
            WGPUShaderModule fs = module_from_wgsl(
                "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }");
            if (vs && fs) {
                WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
                pld.bindGroupLayoutCount = 0;
                pd.layout = wgpuDeviceCreatePipelineLayout(g.device, &pld);
                pd.vertex.module = vs;
                pd.vertex.entryPoint = {"vs", 2};
                pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
                pd.primitive.cullMode = WGPUCullMode_None;
                pd.primitive.frontFace = WGPUFrontFace_CCW;
                pd.multisample.count = 1;
                WGPUFragmentState fsn = WGPU_FRAGMENT_STATE_INIT;
                fsn.module = fs;
                fsn.entryPoint = {"fs", 2};
                WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
                ct.format = g.color_format;
                ct.writeMask = WGPUColorWriteMask_All;
                fsn.targetCount = 1;
                fsn.targets = &ct;
                pd.fragment = &fsn;
                red_pipe = wgpuDeviceCreateRenderPipeline(g.device, &pd);
                static uint32_t nrep = 0;
                if (nrep++ < 3) std::fprintf(stderr, "wgpu: MODE3 pipeline %s (vp %016llx)\n",
                                             red_pipe ? "built" : "FAILED", (unsigned long long)in.vp_hash);
            }
        }
        if (red_pipe) return red_pipe;
    }
    Device::PipeKey k;
    k.vs = in.vp_hash;
    k.fs = in.fp_hash;
    k.color_format = in.color_format ? in.color_format : uint32_t(g.color_format);
    k.depth_format = in.depth_format ? in.depth_format : uint32_t(g.depth_format);
    k.topology = in.topology;
    k.cull = in.cull;
    k.blend_enable = in.blend_enable;
    k.sfactor = in.sfactor;
    k.dfactor = in.dfactor;
    k.sfactor_alpha = in.sfactor_alpha;
    k.dfactor_alpha = in.dfactor_alpha;
    k.equation_alpha = in.equation_alpha;
    k.equation = in.equation;
    k.write_mask = in.write_mask;
    k.depth_test = in.depth_test ? 1u : 0u;
    k.depth_write = in.depth_write ? 1u : 0u;
    k.depth_compare = in.depth_compare;
    k.cw = in.front_face_cw ? 1u : 0u;
    /* The declared-input mask, from the shader, NOT inferred from which pointers
     * happen to be non-null: a declared slot with no data still has to appear in
     * the pipeline, or the draw is rejected.
     */
    k.inputs = in.vertex_inputs;
    auto it = g.pipelines.find(k);
    if (it != g.pipelines.end()) return it->second;

    WGPUShaderModule vs = nullptr, fs = nullptr;
    auto vit = g.vs_modules.find(in.vp_hash);
    if (vit != g.vs_modules.end()) {
        vs = vit->second;
    } else {
        const int before = g.errors;
        vs = shader_module(in.vs_wgsl, in.vs_spirv, in.vs_words);
        wgpuDevicePoll(g.device, true, nullptr);
        if (!vs || g.errors != before) {
            s.skipped++;
            return nullptr;
        }
        g.vs_modules[in.vp_hash] = vs;
    }
    /* PS3BASE_WGPU_TRIVIAL_FS: keep the real pipeline KEY (blend, depth, masks,
     * vertex inputs) but swap in a trivial yellow fragment shader. If pixels
     * appear, the pipeline state is fine and the RSX fragment PROGRAM is what
     * produces nothing; if they do not, the state is the problem. */
    static const bool trivial_fs = std::getenv("PS3BASE_WGPU_TRIVIAL_FS") != nullptr;
    if (trivial_fs) {
        static WGPUShaderModule tfm = nullptr;
        if (!tfm) {
            static const char* kWGSL =
                "@group(0) @binding(32) var t0: texture_2d<f32>;"                "@group(0) @binding(96) var s0: sampler;"                "@fragment fn main(@builtin(position) p: vec4f) -> @location(0) vec4f {"                "  var uv = p.xy / vec2f(1280.0, 720.0);"                "  return vec4f(textureSample(t0, s0, uv).rgb, 1.0);"                "}";
                /* The sampler and texture are separate bindings, so they are
                 * declared here exactly as the RSX shaders use them. */
            WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
            wg.code = {kWGSL, strlen(kWGSL)};
            WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
            sd.nextInChain = &wg.chain;
            tfm = wgpuDeviceCreateShaderModule(g.device, &sd);
        }
        fs = tfm;
    } else {
        auto fit = g.fs_modules.find(in.fp_hash);
        if (fit != g.fs_modules.end()) {
            fs = fit->second;
        } else {
            const int before = g.errors;
            fs = shader_module(in.fs_wgsl, in.fs_spirv, in.fs_words);
            wgpuDevicePoll(g.device, true, nullptr);
            if (!fs || g.errors != before) {
                s.skipped++;
                return nullptr;
            }
            g.fs_modules[in.fp_hash] = fs;
        }
    }

    /* The vertex buffer layout array index IS the slot number, so the array must
     * be dense from 0 to the highest declared slot. Holes get an empty layout
     * (attributeCount 0): WebGPU still requires the draw to bind those slots, and
     * the draw loop binds every one of them. */
    uint32_t highest = 0;
    for (uint32_t i = 0; i < 16; i++) if (k.inputs & (1u << i)) highest = i + 1;
    std::vector<WGPUVertexBufferLayout> vbl(highest);
    std::vector<WGPUVertexAttribute> attribs(highest);
    for (uint32_t i = 0; i < highest; i++) {
        if (!(k.inputs & (1u << i))) {
            vbl[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
            vbl[i].arrayStride = 0;
            vbl[i].stepMode = WGPUVertexStepMode_Vertex;
            vbl[i].attributeCount = 0;
            vbl[i].attributes = nullptr;
            continue;
        }
        attribs[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        attribs[i].format = WGPUVertexFormat_Float32x4;
        attribs[i].offset = 0;
        attribs[i].shaderLocation = i;
        vbl[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vbl[i].arrayStride = 16;
        vbl[i].stepMode = WGPUVertexStepMode_Vertex;
        vbl[i].attributeCount = 1;
        vbl[i].attributes = &attribs[i];
    }
    WGPUVertexState vstate = WGPU_VERTEX_STATE_INIT;
    vstate.module = vs;
    vstate.entryPoint = {"main", 4};
    vstate.bufferCount = highest;
    vstate.buffers = highest ? vbl.data() : nullptr;

    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = WGPUTextureFormat(k.color_format);
    ct.writeMask = WGPUColorWriteMask(k.write_mask ? k.write_mask : 0xF);
    WGPUBlendState bs = WGPU_BLEND_STATE_INIT;
    bs.color.operation = WGPUBlendOperation(k.equation & 0xFFFF);
    /* The alpha OPERATION is derived from its own half of the register and
     * converted through the same table; reading it as a raw enumerant is the
     * same mistake the factors had. */
    bs.alpha.operation = WGPUBlendOperation(k.equation_alpha ? k.equation_alpha : (k.equation & 0xFFFF));
    bs.color.srcFactor = WGPUBlendFactor(k.sfactor & 0xFFFF);
    bs.color.dstFactor = WGPUBlendFactor(k.dfactor & 0xFFFF);
    /* The alpha factors are CONVERTED values too, not the upper half of the
     * colour ones. Taking them from the raw RSX register before conversion put
     * a Vulkan enumerant into a WebGPU enum -- measured as the logo's red coming
     * out (134,36,31) instead of (152,53,47). */
    bs.alpha.srcFactor = WGPUBlendFactor(k.sfactor_alpha ? k.sfactor_alpha : (k.sfactor & 0xFFFF));
    bs.alpha.dstFactor = WGPUBlendFactor(k.dfactor_alpha ? k.dfactor_alpha : (k.dfactor & 0xFFFF));
    ct.blend = k.blend_enable ? &bs : nullptr;

    WGPUFragmentState fstate = WGPU_FRAGMENT_STATE_INIT;
    fstate.module = fs;
    fstate.entryPoint = {"main", 4};
    fstate.targetCount = 1;
    fstate.targets = &ct;

    WGPUPrimitiveState ps = WGPU_PRIMITIVE_STATE_INIT;
    ps.topology = k.topology == 0 ? WGPUPrimitiveTopology_PointList : k.topology == 1 ? WGPUPrimitiveTopology_LineList : WGPUPrimitiveTopology_TriangleList;
    ps.cullMode = WGPUCullMode(k.cull);
    /* Front face stays as the Vulkan path computed it.
     *
     * The mirror flips NDC Y for WebGPU, which does reverse winding, so the
     * opposite front face was the obvious next step -- and it is WRONG, measured:
     * inverting it took the RMS from 10.57 to 38.02. These draws cull NOTHING
     * (cull_for returns None for VK_CULL_MODE_NONE and for FRONT_AND_BACK, both
     * of which the title uses), so winding never decides a pixel here. Leaving
     * the value identical to Vulkan's is the honest choice: the flip only
     * matters if a future title actually culls. */
    ps.frontFace = k.cw ? WGPUFrontFace_CW : WGPUFrontFace_CCW;

    static const bool no_depth = std::getenv("PS3BASE_WGPU_NO_DEPTH") != nullptr;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    ds.format = WGPUTextureFormat(k.depth_format);
    ds.depthWriteEnabled = k.depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    ds.depthCompare = WGPUCompareFunction(k.depth_compare);
    /* A "test disabled" draw is expressed as compare Always with no write; the
     * pipeline still DECLARES the depth format because the pass carries the
     * attachment. Baking it per draw is what PipelineKey already does for
     * Vulkan, where the same fields are dynamic. */
    if (!k.depth_test) ds.depthCompare = WGPUCompareFunction_Always;
    /* PS3BASE_WGPU_DEPTH_ALWAYS: keep every other fragment state but make the
     * depth test unconditional, which tells a depth rejection apart from a
     * fragment-shader or alpha-test rejection. */
    if (std::getenv("PS3BASE_WGPU_DEPTH_ALWAYS")) ds.depthCompare = WGPUCompareFunction_Always;
    if (std::getenv("PS3BASE_WGPU_CULL_NONE")) ps.cullMode = WGPUCullMode_None;
    if (std::getenv("PS3BASE_WGPU_MASK_ALL")) ct.writeMask = WGPUColorWriteMask_All;
    if (std::getenv("PS3BASE_WGPU_NO_BLEND")) ct.blend = nullptr;

    WGPURenderPipelineDescriptor rp = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rp.layout = g.pipe_layout;
    rp.vertex = vstate;
    rp.primitive = ps;
    rp.depthStencil = no_depth ? nullptr : &ds;
    rp.fragment = &fstate;
    const int before = g.errors;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(g.device, &rp);
    wgpuDevicePoll(g.device, true, nullptr);
    if (p && g.errors == before) {
        g.pipelines[k] = p;
        s.pipelines++;
    } else {
        s.skipped++;
        static int logged = 0;
        if (logged++ < 5) std::fprintf(stderr, "wgpu: pipeline rejected: %s\n", s.first_error.empty() ? g.first_error : s.first_error.c_str());
        return nullptr;
    }
    return p;
}

}  // namespace

/* Vulkan -> WebGPU conversions.
 *
 * The values are the Vulkan enums the renderer has already computed, not RSX
 * codes: the renderer's own color_format()/blend_factor()/compare_op() did that
 * translation, and doing it again here would introduce a second, divergent
 * front end. The tables are written from the Vulkan enumerant values so they do
 * not need the Vulkan headers (which this file deliberately does not include:
 * it must be buildable and testable without a Vulkan device).
 */
namespace {

constexpr int VK_FORMAT_UNDEFINED_ = 0;
constexpr int VK_FORMAT_B8G8R8A8_UNORM_ = 44;
constexpr int VK_FORMAT_B8G8R8A8_SRGB_ = 50;
constexpr int VK_FORMAT_R8G8B8A8_UNORM_ = 37;
constexpr int VK_FORMAT_R8G8B8A8_SRGB_ = 43;
constexpr int VK_FORMAT_R8_UNORM_ = 9;
constexpr int VK_FORMAT_R8G8_UNORM_ = 16;
constexpr int VK_FORMAT_R16G16B16A16_SFLOAT_ = 97;
constexpr int VK_FORMAT_R32G32B32A32_SFLOAT_ = 109;
constexpr int VK_FORMAT_R32_SFLOAT_ = 100;
constexpr int VK_FORMAT_D16_UNORM_ = 124;
constexpr int VK_FORMAT_D24_UNORM_S8_UINT_ = 129;
constexpr int VK_FORMAT_D32_SFLOAT_ = 126;
constexpr int VK_FORMAT_D32_SFLOAT_S8_UINT_ = 130;
constexpr int VK_FORMAT_A1R5G5B5_UNORM_PACK16_ = 1;
constexpr int VK_FORMAT_R5G6B5_UNORM_PACK16_ = 4;
constexpr int VK_FORMAT_R16_UNORM_ = 70;
constexpr int VK_FORMAT_R16G16_UNORM_ = 77;
constexpr int VK_FORMAT_R16G16_SFLOAT_ = 81;
constexpr int VK_FORMAT_R5G5B5A1_UNORM_PACK16_ = 86;
constexpr int VK_FORMAT_R4G4B4A4_UNORM_PACK16_ = 87;

}  // namespace

uint32_t format_for(int vk_format) {
    switch (vk_format) {
    case VK_FORMAT_B8G8R8A8_UNORM_: return uint32_t(WGPUTextureFormat_BGRA8Unorm);
    case VK_FORMAT_B8G8R8A8_SRGB_: return uint32_t(WGPUTextureFormat_BGRA8UnormSrgb);
    case VK_FORMAT_R8G8B8A8_UNORM_: return uint32_t(WGPUTextureFormat_RGBA8Unorm);
    case VK_FORMAT_R8G8B8A8_SRGB_: return uint32_t(WGPUTextureFormat_RGBA8UnormSrgb);
    case VK_FORMAT_R8_UNORM_: return uint32_t(WGPUTextureFormat_R8Unorm);
    case VK_FORMAT_R8G8_UNORM_: return uint32_t(WGPUTextureFormat_RG8Unorm);
    case VK_FORMAT_R16G16B16A16_SFLOAT_: return uint32_t(WGPUTextureFormat_RGBA16Float);
    /* The three formats WebGPU has no 16-bit packed counterpart for. wgpu_map
     * classifies them Expand: they widen to RGBA8, at twice the bytes per texel.
     * Doing it here rather than refusing keeps the menu drawable, and the extra
     * bandwidth is the honest cost of the format. */
    case VK_FORMAT_R32G32B32A32_SFLOAT_: return uint32_t(WGPUTextureFormat_RGBA32Float);
    case VK_FORMAT_R32_SFLOAT_: return uint32_t(WGPUTextureFormat_R32Float);
    case VK_FORMAT_D16_UNORM_: return uint32_t(WGPUTextureFormat_Depth16Unorm);
    case VK_FORMAT_D24_UNORM_S8_UINT_: return uint32_t(WGPUTextureFormat_Depth24PlusStencil8);
    case VK_FORMAT_D32_SFLOAT_: return uint32_t(WGPUTextureFormat_Depth32Float);
    case VK_FORMAT_D32_SFLOAT_S8_UINT_: return uint32_t(WGPUTextureFormat_Depth32FloatStencil8);
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16_:
    case VK_FORMAT_R5G6B5_UNORM_PACK16_: return uint32_t(WGPUTextureFormat_RGBA8Unorm);
    case VK_FORMAT_UNDEFINED_: return 0;
    default: return 0;
    }
}

uint32_t compare_for(int op) {
    /* Vulkan numbers the compare ops from 0 (NEVER); WebGPU has a leading
     * Undefined = 0 and starts at Never = 1. The mapping is therefore value + 1,
     * NOT the identity: an identity mapping turned RSX LESS (Vulkan 1) into WebGPU
     * 1 = Never, which rejects every fragment and yields a black frame with no
     * validation error at all. */
    const uint32_t v = uint32_t(op);
    if (v > 7) return uint32_t(WGPUCompareFunction_Always);
    return v + 1;
}

uint32_t blend_for(int vk_factor) {
    /* NOT v+1, even though both lists start one apart.
     *
     * The two enumerations order the factors DIFFERENTLY, and the difference
     * starts at the third member:
     *
     *   VK   0 ZERO 1 ONE 2 SRC_COLOR 3 1-SRC_COLOR 4 DST_COLOR 5 1-DST_COLOR
     *        6 SRC_ALPHA 7 1-SRC_ALPHA 8 DST_ALPHA 9 1-DST_ALPHA
     *       10 CONSTANT_COLOR 11 1-CONSTANT_COLOR 12 CONSTANT_ALPHA
     *       13 1-CONSTANT_ALPHA 14 SRC_ALPHA_SATURATE 15 SRC1_COLOR ...
     *
     *   WGPU 1 ZERO 2 ONE 3 Src 4 1-Src 5 SrcAlpha 6 1-SrcAlpha 7 Dst 8 1-Dst
     *        9 DstAlpha 10 1-DstAlpha 11 SrcAlphaSaturated 12 Constant
     *       13 1-Constant 14 Src1 15 1-Src1 16 Src1Alpha 17 1-Src1Alpha
     *
     * So v+1 turns VK SRC_ALPHA (6) into WebGPU Dst (7) and VK ONE_MINUS_SRC_ALPHA
     * (7) into OneMinusDst (8): the alpha blend silently became a destination
     * blend. That is the same class of silent error as the compare-op offset the
     * H2 report documents, on a table it had assumed was shifted uniformly, and
     * it is why the logo's red came out light (184,96,89) instead of (152,53,47). */
    switch (vk_factor) {
    case 0: return uint32_t(WGPUBlendFactor_Zero);
    case 1: return uint32_t(WGPUBlendFactor_One);
    case 2: return uint32_t(WGPUBlendFactor_Src);
    case 3: return uint32_t(WGPUBlendFactor_OneMinusSrc);
    case 4: return uint32_t(WGPUBlendFactor_Dst);
    case 5: return uint32_t(WGPUBlendFactor_OneMinusDst);
    case 6: return uint32_t(WGPUBlendFactor_SrcAlpha);
    case 7: return uint32_t(WGPUBlendFactor_OneMinusSrcAlpha);
    case 8: return uint32_t(WGPUBlendFactor_DstAlpha);
    case 9: return uint32_t(WGPUBlendFactor_OneMinusDstAlpha);
    /* Constant colour and constant alpha are ONE member in WebGPU (the constant
     * is a single clear value), so both Vulkan members land on it. */
    case 10:
    case 12: return uint32_t(WGPUBlendFactor_Constant);
    case 11:
    case 13: return uint32_t(WGPUBlendFactor_OneMinusConstant);
    case 14: return uint32_t(WGPUBlendFactor_SrcAlphaSaturated);
    case 15: return uint32_t(WGPUBlendFactor_Src1);
    case 16: return uint32_t(WGPUBlendFactor_OneMinusSrc1);
    case 17: return uint32_t(WGPUBlendFactor_Src1Alpha);
    case 18: return uint32_t(WGPUBlendFactor_OneMinusSrc1Alpha);
    default: return uint32_t(WGPUBlendFactor_One);
    }
}

uint32_t equation_for(int vk_op) {
    /* Same +1 offset: VK_BLEND_OP_ADD = 0, WGPUBlendOperation_Add = 1. */
    const uint32_t v = uint32_t(vk_op);
    if (v > 4) return uint32_t(WGPUBlendOperation_Add);
    return v + 1;
}
uint32_t cull_for(int vk_flags) {
    /* VkCullModeFlags: NONE 0, FRONT 1, BACK 2, FRONT_AND_BACK 3. WebGPU has no
     * "both" -- wgpu_map classifies it Emulate, expressed by discarding every
     * triangle in the vertex stage rather than by a cull mode, which this
     * backend does not do. Returning None for it is the closest honest answer:
     * the geometry still draws, and the difference shows up in the comparison
     * instead of being hidden. */
    switch (vk_flags) {
    case 1: return uint32_t(WGPUCullMode_Front);
    case 2: return uint32_t(WGPUCullMode_Back);
    default: return uint32_t(WGPUCullMode_None);
    }
}

/* Texture upload for the mirror.
 *
 * The pixels come from the renderer's own decode (Texture::host_pixels), so both
 * APIs sample the same bytes; this file only moves them to the second device.
 *
 * Two facts drive the shape:
 *  - the decoded form is always RGBA8 regardless of the RSX format (the decoder
 *    widens 0x81/0x85 to RGBA at the same size, which is why the mirror's texture
 *    format is fixed at RGBA8Unorm and only the SWIZZLE differs);
 *  - a texture is re-decoded into the same host vector each frame, so the pointer
 *    alone is not an identity. The upload is keyed on (pointer, size, and a
 *    content hash) so an unchanged texture is skipped and a changed one is
 *    re-uploaded.
 */
namespace {

/* The stand-in a bind group uses for a unit the draw does not sample.
 *
 * It has to exist because WebGPU requires an entry for every binding the layout
 * declares; it must be 1x1 and fully defined so the sampler read is well-formed
 * even if a shader samples a unit the renderer did not set. Created once: the
 * descriptors never change.
 */
WGPUTextureView dummy_view(WGPUTextureViewDimension dim) {
    static std::map<uint32_t, WGPUTextureView> cache;
    auto it = cache.find(uint32_t(dim));
    if (it != cache.end()) return it->second;
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    /* A 1D texture has height 1 by definition; the extent is what distinguishes
     * the two cases at creation. */
    td.dimension = dim == WGPUTextureViewDimension_1D ? WGPUTextureDimension_1D : WGPUTextureDimension_2D;
    td.size = {1, 1, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
    if (!t) return nullptr;
    /* Magenta, not transparent black: a black dummy is indistinguishable from a
     * correctly bound texture whose content happens to be black. */
    const uint8_t white[4] = {0, 0, 0, 0};
    WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    dst.texture = t;
    dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;
    WGPUExtent3D size = {1, 1, 1};
    wgpuQueueWriteTexture(g.queue, &dst, white, 4, &layout, &size);
    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    vd.format = WGPUTextureFormat_RGBA8Unorm;
    vd.dimension = dim;
    vd.mipLevelCount = 1;
    vd.arrayLayerCount = 1;
    WGPUTextureView view = wgpuTextureCreateView(t, &vd);
    cache[uint32_t(dim)] = view;
    return view;
}

/* The stand-in sampler, for units a draw does not sample at all (the vertex
 * slots, which the mirror does not mirror). Units that ARE sampled get the
 * sampler derived from their own RSX registers -- see sampler_for() below.
 *
 * PS3BASE_WGPU_SAMPLER_LINEAR=1 still forces this dummy to LINEAR; it is a
 * leftover A/B switch and no longer affects a sampled unit. */
WGPUSampler dummy_sampler() {
    static WGPUSampler sampler = nullptr;
    if (sampler) return sampler;
    const bool linear = std::getenv("PS3BASE_WGPU_SAMPLER_LINEAR") != nullptr;
    WGPUSamplerDescriptor sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    sd.addressModeU = WGPUAddressMode_ClampToEdge;
    sd.addressModeV = WGPUAddressMode_ClampToEdge;
    sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.minFilter = linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    sd.maxAnisotropy = 1;
    sampler = wgpuDeviceCreateSampler(g.device, &sd);
    return sampler;
}

/* WGPUAddressMode for one RSX wrap code.
 *
 * The RSX codes are NOT Vulkan's: 1 repeat, 2 mirrored repeat, 4 clamp to
 * border, 6/7/8 mirror-clamp-to-edge, everything else clamp-to-edge (the
 * renderer wrap_mode() table is the authority and this is the same table in
 * WebGPU terms). WebGPU has no clamp-to-border addressing: the border colour is
 * a Vulkan-only sampler field, and WebGPU drops it. ClampToEdge is the closest
 * defined behaviour, and it differs from Vulkan only OUTSIDE [0,1] where the
 * border would have shown -- recorded here rather than hidden. */
WGPUAddressMode address_for(uint32_t code) {
    switch (code) {
    case 1: return WGPUAddressMode_Repeat;
    case 2: return WGPUAddressMode_MirrorRepeat;
    /* 6/7/8 are mirror-clamp-to-edge in Vulkan; WebGPU's nearest defined mode is
     * MirrorRepeat, but MirrorRepeat is wrong inside the last half texel. These
     * codes mean "clamp mirrored", so ClampToEdge is the behaviour that keeps
     * the inside-right; the mirror is only visible in the outer half texel of
     * the reversed edge, and getting the interior right matters more. */
    case 6: case 7: case 8: return WGPUAddressMode_ClampToEdge;
    /* 4 is clamp-to-border in Vulkan. WebGPU has no border colour. */
    case 4: return WGPUAddressMode_ClampToEdge;
    default: return WGPUAddressMode_ClampToEdge;
    }
}

/* WGPUCompareFunction for the RSX shadow-compare function.
 *
 * The RSX encodes the comparison in TEXTURE_ADDRESS bits 28..30 and its values
 * are Vulkan's VkCompareOp, which is ALREADY what the renderer passes to
 * reversed_compare() -- so this is the same table, in WebGPU's numbering (which
 * is NOT Vulkan's: WebGPU puts NotEqual at 6 where Vulkan has GreaterEqual, and
 * Always at 8 where Vulkan has 7). Reading the renderer's reversed_compare()
 * output would be reading a Vulkan enum, which is the trap this file exists to
 * avoid; the RSX code is the input here. */
WGPUCompareFunction compare_fn_for(uint32_t rsx) {
    switch (rsx & 7) {
    case 0: return WGPUCompareFunction_Never;
    case 1: return WGPUCompareFunction_Greater;
    case 2: return WGPUCompareFunction_Equal;
    case 3: return WGPUCompareFunction_GreaterEqual;
    case 4: return WGPUCompareFunction_Less;
    case 5: return WGPUCompareFunction_NotEqual;
    case 6: return WGPUCompareFunction_LessEqual;
    default: return WGPUCompareFunction_Always;
    }
}

/* The sampler for one unit, derived from that unit's RSX registers.
 *
 * This is sampler_slow()'s derivation, expressed in WebGPU terms. Two of its
 * inputs WebGPU does not have at all, and neither is silently dropped:
 *   - a BORDER COLOUR (address code 4). WebGPU has no border colour on a
 *     sampler, so the unit gets ClampToEdge.
 *   - a per-sampler LOD BIAS. WebGPU has no mipLodBias. It is carried in the
 *     descriptor struct below but does not enter the sampler; the RSX shader
 *     adds it through textureLod where it matters, and the field is logged so
 *     a bias-driven difference can be told from a filtering one.
 * Address codes 6/7/8 are mirror-clamp-to-edge in Vulkan and have no WebGPU
 * equivalent; they take ClampToEdge. */
/* The descriptor derived from the registers, kept separate from the created
 * object so it can be logged field by field: a sampler that is created and
 * never inspected is indistinguishable from the dummy it replaced. */
struct SamplerDesc {
    WGPUSamplerDescriptor sd;
    bool shadow;
    float lod_bias;
};

SamplerDesc derive_sampler(const DrawInputs::Texture::RsxSampler& rs) {
    SamplerDesc out{};
    WGPUSamplerDescriptor& sd = out.sd;
    sd = WGPU_SAMPLER_DESCRIPTOR_INIT;
    const uint32_t minf = (rs.filter >> 16) & 7;
    const uint32_t magf = (rs.filter >> 24) & 7;
    const bool mips = minf >= 3 && minf <= 6 && rs.levels > 1;
    /* Mag is NEAREST only for code 1; every other code is LINEAR. Min is
     * NEAREST for 1, 3, 5 and LINEAR otherwise -- the same split sampler_slow
     * makes, and the codes are the RSX ones, not Vulkan's filter enum. */
    sd.magFilter = magf == 1 ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    if (std::getenv("PS3BASE_WGPU_SAMPLER_LINEAR")) {
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
    } else {
        sd.minFilter = (minf == 1 || minf == 3 || minf == 5) ? WGPUFilterMode_Nearest : WGPUFilterMode_Linear;
    }
    sd.mipmapFilter = (minf == 5 || minf == 6) ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
    /* A/B diagnostics, because "the sampler improved it" is a claim that has to
     * be attributable. Each mode changes exactly one axis of the derivation:
     *   nearest : mag and min NEAREST, mip NEAREST  (what the old dummy did)
     *   linear  : mag and min LINEAR,  mip NEAREST  (the refuted SAMPLER_LINEAR)
     *   lod0    : the derived filters but LOD clamped to level 0
     *   nomip   : the derived filters with mip filtering off (lodMaxClamp 0)
     * Unset = the derivation above, which matches the Vulkan sampler field for
     * field (logged by the renderer as "vksampler"). */
    static const std::string mode = std::getenv("PS3BASE_WGPU_SAMPLER_MODE") ? std::getenv("PS3BASE_WGPU_SAMPLER_MODE") : "";
    if (mode == "mag_near" || mode == "mag_lin") {
        sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Linear;
    }
    if (mode == "nearest") {
        sd.magFilter = WGPUFilterMode_Nearest;
        sd.minFilter = WGPUFilterMode_Nearest;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    } else if (mode == "linear") {
        sd.magFilter = WGPUFilterMode_Linear;
        sd.minFilter = WGPUFilterMode_Linear;
        sd.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    } else if (mode == "mag_near") {
        sd.magFilter = WGPUFilterMode_Nearest;
    } else if (mode == "mag_lin") {
        sd.magFilter = WGPUFilterMode_Linear;
    } else if (mode == "lod0" || mode == "nomip") {
        sd.lodMinClamp = 0.0f;
        sd.lodMaxClamp = 0.0f;
    }
    sd.addressModeU = address_for(rs.address & 0xF);
    sd.addressModeV = address_for((rs.address >> 8) & 0xF);
    sd.addressModeW = address_for((rs.address >> 16) & 0xF);
    /* LOD clamps: identical arithmetic to sampler_slow, and clamped to the mip
     * count so WebGPU does not sample a level the texture does not have. */
    const float min_lod = float(((rs.control0 >> 19) & 0xFFF) >> 6) / 4.0f;
    const float max_lod = float((((rs.control0 >> 7) & 0xFFF) + 63) >> 6) / 4.0f;
    sd.lodMinClamp = mips ? std::min(min_lod, float(rs.levels - 1)) : 0.0f;
    sd.lodMaxClamp = mips ? std::min(max_lod, float(rs.levels - 1)) : 0.0f;
    static const uint16_t aniso_levels[8] = {1, 2, 4, 6, 8, 10, 12, 16};
    const uint32_t aniso = (rs.control0 >> 4) & 7;
    sd.maxAnisotropy = aniso ? aniso_levels[aniso] : 1;
    if (sd.maxAnisotropy > 1) {
        /* Anisotropic filtering is an OPTIONAL feature in WebGPU. If the device
         * did not grant it the sampler must not request it: the descriptor
         * would be rejected and the whole bind group with it. The mirror falls
         * back to trilinear, which is what the Vulkan device does too when
         * samplerAnisotropy is absent. */
        if (!g.aniso) sd.maxAnisotropy = 1;
    }
    const int32_t bias = static_cast<int32_t>(rs.filter << 19) >> 19;
    out.lod_bias = float((bias + 16) >> 5) / 8.0f;
    out.shadow = rs.shadow;
    if (rs.shadow) {
        /* A shadow unit samples with a COMPARISON, and WebGPU takes the compare
         * function from the sampler itself. The binding TYPE must then be
         * Comparison in the layout, and the layout here declares Filtering for
         * every sampler, so a real shadow draw would be rejected. Nothing on
         * this title's screens (logo, title, menu) is a shadow unit; the case is
         * named rather than papered over, and the compare is not set so a
         * Filtering binding stays legal. */
        static bool warned = false;
        if (!warned) {
            std::fprintf(stderr, "wgpu: shadow sampler requested (unit compare 0x%x); the bind layout declares Filtering, so compare is not set\n",
                         (rs.address >> 28) & 7);
            warned = true;
        }
        (void)compare_fn_for;
    }
    return out;
}

WGPUSampler sampler_for(const DrawInputs::Texture::RsxSampler& rs, uint32_t unit) {
    /* Samplers are created per distinct register set and reused, exactly as the
     * Vulkan path memoises them: a draw rebuilds its bind group anyway, so
     * creating one sampler per unit per draw would leak an object per draw. */
    struct Key {
        uint32_t address, control0, filter, levels;
        bool shadow;
        bool operator==(const Key& o) const {
            return address == o.address && control0 == o.control0 && filter == o.filter && levels == o.levels && shadow == o.shadow;
        }
    };
    static std::vector<std::pair<Key, WGPUSampler>> cache;
    const Key key{rs.address, rs.control0, rs.filter, rs.levels, rs.shadow};
    for (const auto& kv : cache) {
        if (kv.first == key) return kv.second;
    }
    const SamplerDesc d = derive_sampler(rs);
    WGPUSampler s = wgpuDeviceCreateSampler(g.device, &d.sd);
    if (!s) {
        std::fprintf(stderr, "wgpu: sampler rejected for unit %u (addr 0x%x c0 0x%x filter 0x%x levels %u shadow %d)\n", unit, rs.address, rs.control0, rs.filter, rs.levels,
                     int(rs.shadow));
        s = dummy_sampler();
    }
    cache.emplace_back(key, s);
    return s;
}

/* One decoded texel, converted to the float4 WebGPU will sample.
 *
 * The renderer decodes into the format's REAL width (R8 is one byte, the packed
 * 16-bit formats two, the float and 32-bit ones four, the block formats eight or
 * sixteen). The mirror used to assume four and read width*height*4 bytes, which
 * is a read past the end of host_pixels -- it was a SIGSEGV in this function,
 * not a wrong colour, that killed the run once the logo frame came up.
 */
float half_to_float(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000) << 16;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t man = h & 0x3FF;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;  /* signed zero */
        } else {
            /* Subnormal half: renormalise into a float. */
            uint32_t e = 113;
            uint32_t m = man;
            while (!(m & 0x400)) {
                m <<= 1;
                e--;
            }
            bits = sign | (e << 23) | ((m & 0x3FF) << 13);
        }
    } else if (exp == 0x1F) {
        bits = sign | 0x7F800000u | (man << 13);  /* inf / nan */
    } else {
        bits = sign | ((exp + 112) << 23) | (man << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void texel_to_float(const uint8_t* p, int vk_format, float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (vk_format) {
    /* 8-bit unorm, one channel. The RSX B8 intensity texture lives here; the
     * component mapping decides which of the four gets the value. */
    case VK_FORMAT_R8_UNORM_: out[0] = out[1] = out[2] = p[0] / 255.0f; break;
    case VK_FORMAT_R8G8_UNORM_:
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = 0.0f;
        break;
    /* The 16-bit packed formats the decoder widens to RGBA at the same size
     * (out_bytes 2 in the table but 4 after conversion): read the bytes the
     * decoder produced, which are already RGBA8 in the format's own order. */
    case VK_FORMAT_A1R5G5B5_UNORM_PACK16_:
    case VK_FORMAT_R5G6B5_UNORM_PACK16_:
    case VK_FORMAT_R5G5B5A1_UNORM_PACK16_:
    case VK_FORMAT_R4G4B4A4_UNORM_PACK16_:
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = p[3] / 255.0f;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM_:
    case VK_FORMAT_B8G8R8A8_SRGB_:
        out[0] = p[2] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[0] / 255.0f;
        out[3] = p[3] / 255.0f;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM_:
    case VK_FORMAT_R8G8B8A8_SRGB_:
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = p[3] / 255.0f;
        break;
    case VK_FORMAT_R16_UNORM_:
        out[0] = out[1] = out[2] = ((p[1] << 8) | p[0]) / 65535.0f;
        break;
    case VK_FORMAT_R16G16_UNORM_:
        out[0] = ((p[1] << 8) | p[0]) / 65535.0f;
        out[1] = ((p[3] << 8) | p[2]) / 65535.0f;
        break;
    case VK_FORMAT_R16G16_SFLOAT_: {
        const uint16_t h0 = uint16_t(p[0] | (p[1] << 8)), h1 = uint16_t(p[2] | (p[3] << 8));
        out[0] = half_to_float(h0);
        out[1] = half_to_float(h1);
        break;
    }
    case VK_FORMAT_R16G16B16A16_SFLOAT_: {
        for (int c = 0; c < 4; c++) {
            const uint32_t b = uint32_t(p[c * 2]) | (uint32_t(p[c * 2 + 1]) << 8);
            out[c] = half_to_float(uint16_t(b));
        }
        break;
    }
    case VK_FORMAT_R32_SFLOAT_:
        std::memcpy(&out[0], p, 4);
        out[1] = out[2] = out[0];
        break;
    case VK_FORMAT_R32G32B32A32_SFLOAT_:
        std::memcpy(out, p, 16);
        break;
    default:
        /* An unknown format takes the 4-byte RGBA reading: that is what every
         * format the renderer actually reports falls back to, and guessing
         * anything else would be worse than a documented default. */
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = p[3] / 255.0f;
        break;
    }
}

/* Apply the VkComponentMapping the Vulkan IMAGE VIEW applies.
 *
 * VkComponentSwizzle values are IDENTITY 0, ZERO 1, ONE 2, R 3, G 4, B 5, A 6.
 * WebGPU has no per-view swizzle in this backend's version, so it is folded in
 * here, per texel. Doing it in the shader instead would mean editing every
 * fragment program the decompiler emits, which is a second divergence.
 */
void apply_mapping(float v[4], const int m[4]) {
    const float src[4] = {v[0], v[1], v[2], v[3]};
    for (int c = 0; c < 4; c++) {
        switch (m[c]) {
        case 0: break;                 /* IDENTITY: keep */
        case 1: v[c] = 0.0f; break;    /* ZERO */
        case 2: v[c] = 1.0f; break;    /* ONE */
        case 3: v[c] = src[0]; break;  /* R */
        case 4: v[c] = src[1]; break;  /* G */
        case 5: v[c] = src[2]; break;  /* B */
        case 6: v[c] = src[3]; break;  /* A */
        default: break;
        }
    }
}

WGPUTexture upload_texture(const DrawInputs::Texture& t, uint32_t unit, WGPUTextureDimension dim) {
    if (!t.pixels || !t.width || !t.height) return nullptr;
    static std::map<std::pair<const void*, uint64_t>, WGPUTexture> cache;
    const uint32_t bpt = t.bytes_per_texel ? t.bytes_per_texel : 4u;
    const size_t src_bytes = size_t(t.width) * t.height * bpt;

    /* Key the cache on the SOURCE bytes and the whole sampling state, so the
     * conversion below only runs on a miss. Hashing the converted image instead
     * would force a full per-draw conversion of every texture -- 1280x720 x 118
     * draws is 106 million texels, and it would hide the very cost the
     * comparison is trying to attribute. */
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < src_bytes; i += 97) {  /* sampled: a full hash per texture per draw is the wrong trade */
        h ^= t.pixels[i];
        h *= 1099511628211ull;
    }
    const auto key = std::make_pair(static_cast<const void*>(t.pixels),
                                    h ^ (uint64_t(t.width) << 32) ^ t.height ^ (uint64_t(dim) << 56) ^ (uint64_t(t.mapping[0]) << 48) ^
                                        (uint64_t(t.mapping[1]) << 44) ^ (uint64_t(t.mapping[2]) << 40) ^ (uint64_t(t.mapping[3]) << 36) ^
                                        (uint64_t(t.vk_format) << 24) ^ uint64_t(bpt) ^ (uint64_t(t.mip_count) << 60));
    /* The mip count is part of the key: the same bytes with a different chain
     * produce a different texture object, and reusing the level-0-only one for a
     * draw that samples the chain is a silent wrong answer. */
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    /* Decode EVERY mip level to RGBA8 with the component mapping folded in.
     * RGBA8Unorm is what the backend already bound, and it keeps the texture
     * format independent of the RSX one; the sampling arithmetic is done at
     * 8-bit precision, which is the precision the decoded image has anyway.
     *
     * Level 0 is the whole image at offset 0 ONLY when the decoder's region list
     * says so. Each level lives at its own offset inside the decoded buffer
     * (regions[].offset), with its own dimensions, and the Vulkan path copies
     * each one with a separate vkCmdCopyBufferToImage. Reading level 0 as the
     * first width*height texels is right for the common case and wrong for a
     * chain, which is exactly the case a sampler with mip filtering uses. */
    uint32_t levels = 1;
    if (t.mip_count) {
        levels = std::min<uint32_t>(t.mip_count, 16u);
        /* Level 0 must be the first region and at offset 0; anything else means
         * the renderer is describing a chain this code cannot index. */
        if (t.mips[0].offset != 0 || t.mips[0].level != 0) levels = 1;
    }
    std::vector<uint8_t> rgba;
    const size_t level0_bytes = size_t(t.width) * t.height * 4;
    rgba.resize(level0_bytes);
    for (uint32_t lv = 0; lv < levels; lv++) {
    const uint32_t lw = levels > 1 ? t.mips[lv].width : t.width;
    const uint32_t lh = levels > 1 ? t.mips[lv].height : t.height;
    const uint64_t loff = levels > 1 ? t.mips[lv].offset : 0;
    if (lv > 0) rgba.resize(rgba.size() + size_t(lw) * lh * 4);
    uint8_t* out = &rgba[lv > 0 ? rgba.size() - size_t(lw) * lh * 4 : 0];
    for (uint32_t y = 0; y < lh; y++) {
        for (uint32_t x = 0; x < lw; x++) {
            const size_t si = size_t(loff) + (size_t(y) * lw + x) * bpt;
            float v[4];
            texel_to_float(t.pixels + si, t.vk_format, v);
            /* Diagnostics: PS3BASE_WGPU_NO_MAPPING drops the view remap, and
             * PS3BASE_WGPU_RGBA_RAW reads the decoded bytes as RGBA regardless
             * of the format. Together they tell a channel-order error apart from
             * a mapping error without touching either code path. */
            static const bool no_map = std::getenv("PS3BASE_WGPU_NO_MAPPING") != nullptr;
            static const bool raw_rgba = std::getenv("PS3BASE_WGPU_RGBA_RAW") != nullptr;
            if (raw_rgba) {
                v[0] = t.pixels[si] / 255.0f;
                v[1] = t.pixels[si + 1] / 255.0f;
                v[2] = t.pixels[si + 2] / 255.0f;
                v[3] = t.pixels[si + 3] / 255.0f;
            }
            if (!no_map) apply_mapping(v, t.mapping);
            uint8_t* d = &out[(size_t(y) * lw + x) * 4];
            for (int c = 0; c < 4; c++) {
                const float f = std::clamp(v[c], 0.0f, 1.0f);
                d[c] = uint8_t(f * 255.0f + 0.5f);
            }
        }
    }
    }  /* mip level */

    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = dim;
    td.size = dim == WGPUTextureDimension_1D ? WGPUExtent3D{t.width, 1, 1} : WGPUExtent3D{t.width, t.height, 1};
    /* PS3BASE_WGPU_TEX_SRGB=1 uploads the mirror's textures as
     * RGBA8UnormSrgb, i.e. requests the same automatic linearisation the Vulkan
     * path gets from its *_SRGB image view. Diagnostic until it is derived from
     * the texture registers like the Vulkan path does. */
    static const bool tex_srgb = std::getenv("PS3BASE_WGPU_TEX_SRGB") != nullptr;
    td.format = tex_srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    /* EVERY level the decoder produced, not just level 0. A sampler whose RSX
     * registers ask for LOD up to 10 (which this title's logo draw does) samples
     * the small levels; with only level 0 present WebGPU clamps the LOD to the
     * single available level, and the result is a magnified 1x1 or a wrong
     * minification -- which is why deriving the sampler without this made the
     * image worse than the dummy it replaced. */
    td.mipLevelCount = levels;
    td.sampleCount = 1;
    WGPUTexture tex = wgpuDeviceCreateTexture(g.device, &td);
    if (!tex) return nullptr;
    const bool one_d = dim == WGPUTextureDimension_1D;
    size_t level_off = 0;
    for (uint32_t lv = 0; lv < levels; lv++) {
        const uint32_t lw = levels > 1 ? t.mips[lv].width : t.width;
        const uint32_t lh = levels > 1 ? t.mips[lv].height : t.height;
        WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        dst.texture = tex;
        dst.mipLevel = lv;
        dst.origin = {0, 0, 0};
        dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        /* A 1D texture is written as one row of width*height texels, exactly as
         * the Vulkan path lays it out; a 2D level is lw per row for lh rows. */
        layout.bytesPerRow = (one_d ? lw * lh : lw) * 4;
        layout.rowsPerImage = one_d ? 1 : lh;
        WGPUExtent3D size = {one_d ? lw * lh : lw, one_d ? 1u : lh, 1};
        const size_t bytes = size_t(lw) * lh * 4;
        wgpuQueueWriteTexture(g.queue, &dst, &rgba[level_off], bytes, &layout, &size);
        level_off += bytes;
    }
    s.upload_bytes += rgba.size();
    s.textures++;
    cache[key] = tex;
    (void)unit;
    return tex;
}

}  // namespace
namespace {
void report_at_exit() {
    if (!g.ready) return;
    std::fprintf(stderr,
                 "wgpu: draws %llu pipelines %llu textures %llu upload %.1f MiB skipped %llu errors %d\n",
                 (unsigned long long)s.draws, (unsigned long long)s.pipelines,
                 (unsigned long long)s.textures, double(s.upload_bytes) / (1024.0 * 1024.0),
                 (unsigned long long)s.skipped, g.errors);
    if (!s.first_error.empty()) std::fprintf(stderr, "wgpu: first error: %s\n", s.first_error.c_str());
}
}  // namespace

/* T1 -- THE THREAD GUARD, in the backend itself.
 *
 * The rule this build is built on: NO WebGPU call from a pthread. Measured on this
 * tree, with the renderer issuing wgpu::draw from the RSX pthread, the FIFO froze
 * at n_method=378 (busy 99.8%, methods 0.1%); with those calls silenced the same
 * build reached 42 379 draws and 503 327 methods (93.4%, 119k/s). Every entry of
 * the emdawnwebgpu port is __proxy:'sync', so each call from a pthread is a
 * blocking round trip to the realm that owns the device.
 *
 * The guard lives HERE rather than at each call site in rsx_renderer.cpp for two
 * reasons: there is exactly one place to audit, and a call site added later cannot
 * forget it. The owner thread is recorded by the page when it adopts the device;
 * every entry point that would touch the device checks it first and REFUSES, with
 * a counter, when it arrives on any other thread.
 */
std::atomic<uint32_t> g_owner_thread{0};
std::atomic<uint64_t> g_off_thread_refused{0};

/* T1 -- THE BISECTION MODE.
 *
 * A black canvas with draws that report no error admits several causes at once:
 * the target, the pass, the pipeline, the vertex shader's output, culling, depth,
 * blending, the fragment shader, the textures. Guessing between them is what the
 * last four turns did. This switch makes each one a separate, visible step:
 *
 *   0  the game's own draws (normal)
 *   1  ONE magenta clear, no draw at all          -> proves the target and the submit
 *   2  magenta clear + OUR full-screen triangle   -> proves the pass and the pipeline
 *   3  magenta clear + the GAME's Draw(3) vertices with the game's vertex shader and
 *      OUR fixed-red fragment shader             -> proves the vertex stage
 *   4  mode 3 with culling enabled
 *   5  mode 4 with the depth test enabled
 *   6  mode 5 with blending enabled
 *   7  mode 6 with the game's real fragment shader
 *   8  mode 7 with textures
 *
 * Set from the page with ?dbg=N; the page passes it to t1_set_debug_mode().
 */

std::atomic<uint32_t> g_scope_errors{0};


uint32_t backend_thread_id() {
#if defined(__EMSCRIPTEN__)
    return static_cast<uint32_t>(emscripten_main_browser_thread_id());
#else
    return 0;
#endif
}

/* Called by the replay on the page's thread, once, when it adopts the device. */
void set_owner_thread(uint32_t tid) { g_owner_thread.store(tid, std::memory_order_release); }

uint64_t off_thread_refusals() { return g_off_thread_refused.load(std::memory_order_relaxed); }

/* True when this call may touch the device. On a native build there is no browser
 * thread to compare against, so the guard is inert. */
bool on_owner_thread(const char* who) {
#if defined(__EMSCRIPTEN__)
    const uint32_t owner = g_owner_thread.load(std::memory_order_acquire);
    if (!owner) {
        /* No owner recorded yet: the renderer's own init runs before the page
         * adopts. Nothing may touch the device in that window either. */
        return false;
    }
    if (backend_thread_id() != owner) {
        const uint64_t n = g_off_thread_refused.fetch_add(1, std::memory_order_relaxed);
        if (n < 5) {
            LOG_ERROR("wgpu", "%s REFUSED: called from tid %u, device is owned by tid %u -- "
                      "WebGPU must never be called from a pthread", who, backend_thread_id(), owner);
        }
        return false;
    }
    return true;
#else
    (void)who;
    return true;
#endif
}

bool enabled() {
#if defined(__EMSCRIPTEN__)
    /* On by DEFAULT in the browser: there is no Vulkan there at all, so the
     * "mirror" is not an optional second path, it is the only one. Gating it
     * behind an env var would make the module link, flip and paint nothing with
     * no line saying why -- which is exactly the failure this track exists to
     * remove. PS3BASE_WGPU=0 turns it off for an A/B. */
    static const bool on = [] {
        const char* v = std::getenv("PS3BASE_WGPU");
        return !(v && v[0] == '0');
    }();
    return on;
#else
    static const bool on = [] { return std::getenv("PS3BASE_WGPU") != nullptr; }();
    return on;
#endif
}

#if defined(__EMSCRIPTEN__)
/* The JS side owns the device and this function ADOPTS it.
 *
 * Measured (docs/explore/S1/RESULTS.md sec. 3): a wasm module can take a WebGPU
 * device that JS created in its own realm, through the port's
 * preinitializedWebGPUDevice. The native path below cannot: a JS promise does
 * not resolve while the wasm occupies the thread, so the adapter callback never
 * fires. That measurement is why R3 put the device in the page; what it missed
 * is that adoption needs no request at all.
 *
 * The colour target is the CANVAS itself (make_targets below), so a drawn frame
 * reaches the visible canvas rather than a private mirror.
 */
/* The bind group and pipeline layouts, built ONCE for whichever init path runs.
 *
 * These used to be built only in the NATIVE init(), and the wasm init() returned
 * before reaching them. Measured: every draw failed with "createBindGroup: Failed
 * to read the 'layout' property ... Required member is undefined", because
 * g.bind_layout was never created on the web path. Extracting the block is what
 * makes the two paths share one definition instead of one of them silently
 * missing it. */
bool build_bind_layouts() {
    if (g.bind_layout) return true;
    /* Bindings mirror the Vulkan layout so the same SPIR-V is valid in both:
     * 0..3 uniform buffers, 16..19 vertex samplers, 32..47 fragment samplers.
     * Texture and sampler are SEPARATE bindings, the sampler 64 above its
     * texture, because a combined sampler compiles to OpTypeSampledImage and
     * naga rejects that ("invalid id"). */
    std::vector<WGPUBindGroupLayoutEntry> entries;
    for (uint32_t i = 0; i < 4; i++) {
        WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        e.binding = i;
        e.visibility = i < 2 ? WGPUShaderStage_Vertex : WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform;
        entries.push_back(e);
    }
    for (uint32_t i = 0; i < 4; i++) {
        WGPUBindGroupLayoutEntry t = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        t.binding = 16 + i;
        t.visibility = WGPUShaderStage_Vertex;
        t.texture.sampleType = WGPUTextureSampleType_Float;
        t.texture.viewDimension = WGPUTextureViewDimension_2D;
        entries.push_back(t);
        WGPUBindGroupLayoutEntry sm = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        sm.binding = 16 + i + 64;
        sm.visibility = WGPUShaderStage_Vertex;
        sm.sampler.type = WGPUSamplerBindingType_Filtering;
        entries.push_back(sm);
    }
    for (uint32_t i = 0; i < 16; i++) {
        WGPUBindGroupLayoutEntry t = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        t.binding = 32 + i;
        t.visibility = WGPUShaderStage_Fragment;
        t.texture.sampleType = WGPUTextureSampleType_Float;
        /* The dimension is per unit and follows the RSX sampler: unit 7 is the
         * 1D one the menu binds, and declaring D2 for it made every pipeline
         * using tex7 fail with "View dimension D1 does not match ... D2". */
        t.texture.viewDimension = i == 7 ? WGPUTextureViewDimension_1D : WGPUTextureViewDimension_2D;
        entries.push_back(t);
        WGPUBindGroupLayoutEntry sm = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        sm.binding = 32 + i + 64;
        sm.visibility = WGPUShaderStage_Fragment;
        sm.sampler.type = WGPUSamplerBindingType_Filtering;
        entries.push_back(sm);
    }
    WGPUBindGroupLayoutDescriptor bl = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bl.entryCount = entries.size();
    bl.entries = entries.data();
    g.bind_layout = wgpuDeviceCreateBindGroupLayout(g.device, &bl);
    if (!g.bind_layout) {
        std::fprintf(stderr, "wgpu: bind group layout failed\n");
        return false;
    }
    WGPUPipelineLayoutDescriptor pl = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pl.bindGroupLayoutCount = 1;
    pl.bindGroupLayouts = &g.bind_layout;
    g.pipe_layout = wgpuDeviceCreatePipelineLayout(g.device, &pl);
    return g.pipe_layout != nullptr;
}

bool init(uint32_t width_hint, uint32_t height_hint) {
    if (g.ready) return true;
    g.device = s1_adopt_device();
    if (!g.device) { std::fprintf(stderr, "wgpu: web: no adopted device\n"); return false; }
    g.queue = wgpuDeviceGetQueue(g.device);
    if (!g.queue) { std::fprintf(stderr, "wgpu: web: no queue\n"); return false; }
    /* The canvas format is the browser's preferred one, not a constant: the same
     * binary must work on a BGRA8 and an RGBA8 surface. A pipeline whose
     * colourTarget.format disagrees with the canvas is rejected at draw time and
     * the whole command buffer is dropped. */
    const int fmt = s1_canvas_format();
    if (fmt > 0) g.color_format = static_cast<WGPUTextureFormat>(fmt);
    g.width = width_hint;
    g.height = height_hint;
    g.web = true;
    g.ready = true;
    /* T1: MAKE THE CANVAS THE TARGET HERE, as the native init() does at its own
     * end. Without this the wasm path adopted the device, reported ready, and left
     * g.color_view null -- so every draw() returned at its first line
     * ("if (!g.ready || !g.color_view) return;") while the renderer's own counters
     * showed 153 942 draws and no skip. The two numbers disagreed because they
     * measure different things, and the one that mattered was the silent one.
     *
     * Measured before this call existed: "s1: canvas configured" never printed,
     * the page's readback was a single colour, and the backend's summary read
     * "0 draws replayed, 0 pipelines, 0 textures". */
    if (!build_bind_layouts()) {
        std::fprintf(stderr, "wgpu: web: bind layouts failed\n");
        return false;
    }
    /* THE CANVAS MUST STILL BE CONFIGURED, even though nothing draws into it until
     * presentation: getCurrentTexture() returns an error on an unconfigured canvas.
     * The configuration is what binds the canvas to this device and format. */
    if (!s1_canvas_configure(width_hint, height_hint)) {
        std::fprintf(stderr, "wgpu: web: canvas configure failed\n");
        return false;
    }
    make_targets(width_hint, height_hint);
    std::fprintf(stderr, "wgpu: web: adopted device %p queue %p colourFormat %d offscreen %ux%u view %p\n",
                 (void*)g.device, (void*)g.queue, int(g.color_format),
                 g.width, g.height, (void*)g.offscreen_view);
    return true;
}
#else
bool init(uint32_t width, uint32_t height) {
    if (g.ready) return true;
    if (!enabled()) return false;

    /* TWO instance features, and both are load-bearing:
     *
     *  - ShaderSourceSPIRV, because the renderer already produces SPIR-V and
     *    there is no device-level way to accept it.
     *  - TimedWaitAny, because wgpuInstanceWaitAny with a timeout > 0 PANICS
     *    the process ("not implemented", src/unimplemented.rs) when the
     *    instance did not ask for it. Measured: the same call with a zero
     *    timeout returns, and the panic reproduces in a 20-line program that
     *    requests only ShaderSourceSPIRV.
     */
    static WGPUInstanceFeatureName features[] = {WGPUInstanceFeatureName_ShaderSourceSPIRV, WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor idesc = WGPU_INSTANCE_DESCRIPTOR_INIT;
    idesc.requiredFeatureCount = 2;
    idesc.requiredFeatures = features;
    g.instance = wgpuCreateInstance(&idesc);
    if (!g.instance) {
        std::fprintf(stderr, "wgpu: instance creation refused (ShaderSourceSPIRV unavailable)\n");
        return false;
    }

    AdapterResult ar;
    WGPURequestAdapterOptions opts = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    opts.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo aci = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    aci.mode = WGPUCallbackMode_AllowProcessEvents;
    aci.callback = on_adapter;
    aci.userdata1 = &ar;
    wgpuInstanceRequestAdapter(g.instance, &opts, aci);
    if (!poll_until(ar.done) || !ar.adapter) {
        std::fprintf(stderr, "wgpu: no adapter\n");
        return false;
    }
    g.adapter = ar.adapter;

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(g.adapter, &info) == WGPUStatus_Success) {
        std::fprintf(stderr, "wgpu: adapter '%.*s' vendor '%.*s' arch '%.*s'\n", int(info.device.length), info.device.data ? info.device.data : "", int(info.vendor.length),
                     info.vendor.data ? info.vendor.data : "", int(info.architecture.length), info.architecture.data ? info.architecture.data : "");
        wgpuAdapterInfoFreeMembers(info);
    }

    DeviceResult dr;
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    /* The renderer binds one vertex buffer per RSX attribute slot, up to 16,
     * which is twice the WebGPU default of 8. Asking for it here is what makes
     * setVertexBuffer(slot 8..15) legal; without it the submit is rejected with
     * "Vertex buffer index 8 is greater than ... max_vertex_buffers limit 8". */
    WGPULimits limits = WGPU_LIMITS_INIT;
    limits.maxVertexBuffers = 16;
    limits.maxVertexAttributes = 16;
    limits.maxBindGroups = 4;
    dd.requiredLimits = &limits;
    WGPUUncapturedErrorCallbackInfo ec = WGPU_UNCAPTURED_ERROR_CALLBACK_INFO_INIT;
    ec.callback = on_uncaptured;
    ec.userdata1 = &g;
    dd.uncapturedErrorCallbackInfo = ec;
    WGPURequestDeviceCallbackInfo dci = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dci.mode = WGPUCallbackMode_AllowProcessEvents;
    dci.callback = on_device;
    dci.userdata1 = &dr;
    wgpuAdapterRequestDevice(g.adapter, &dd, dci);
    if (!poll_until(dr.done) || !dr.device) {
        std::fprintf(stderr, "wgpu: no device\n");
        return false;
    }
    g.device = dr.device;
    g.queue = wgpuDeviceGetQueue(g.device);

    if (!build_bind_layouts()) return false;

    make_targets(width, height);
    g.ready = true;
    return true;
}
#endif  // __EMSCRIPTEN__

namespace {

/* Draw one full-screen triangle with a trivial pipeline, read the target back and
 * report the coverage. This exists to localise a black frame: everything here is
 * this file own code and no RSX state, so a failure points at the backend and a
 * success points at the inputs the renderer supplies. */
bool readback(std::vector<uint8_t>& out, uint32_t& width, uint32_t& height);
/* The known-good pipeline: a full-screen triangle, no bindings, no depth. Shared
 * by the start-up self test and by PS3BASE_WGPU_PROBE_MAIN, which draws it
 * through the MAIN pass so the pass machinery can be judged on its own. */
/* Real vertex shader (from the renderer SPIR-V) + trivial fragment shader. Used
 * by PS3BASE_WGPU_PROBE_VS to exercise the vertex path in isolation. */
WGPURenderPipeline vs_pipeline(const DrawInputs& in) {
    static std::map<uint64_t, WGPURenderPipeline> cache;
    auto it = cache.find(in.vp_hash);
    if (it != cache.end()) return it->second;
    WGPUShaderModule vs = nullptr;
    auto vit = g.vs_modules.find(in.vp_hash);
    if (vit != g.vs_modules.end()) {
        vs = vit->second;
    } else {
        vs = module_from_spirv(in.vs_spirv, in.vs_words);
        if (!vs) return nullptr;
        g.vs_modules[in.vp_hash] = vs;
    }
    static const char* kWGSL =
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 1.0, 0.0, 1.0); }";
    WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
    wg.code = {kWGSL, strlen(kWGSL)};
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &wg.chain;
    WGPUShaderModule fm = wgpuDeviceCreateShaderModule(g.device, &sd);
    if (!fm) return nullptr;
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = g.color_format;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = fm;
    fs.entryPoint = {"fs", 2};
    fs.targetCount = 1;
    fs.targets = &ct;
    uint32_t hi = 0;
    for (uint32_t i = 0; i < 16; i++) if (in.vertex_inputs & (1u << i)) hi = i + 1;
    std::vector<WGPUVertexBufferLayout> vbl(hi);
    std::vector<WGPUVertexAttribute> attrs(hi);
    for (uint32_t i = 0; i < hi; i++) {
        vbl[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        if (in.vertex_inputs & (1u << i)) {
            attrs[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
            attrs[i].format = WGPUVertexFormat_Float32x4;
            attrs[i].offset = 0;
            attrs[i].shaderLocation = i;
            vbl[i].arrayStride = 16;
            vbl[i].attributeCount = 1;
            vbl[i].attributes = &attrs[i];
        } else {
            vbl[i].arrayStride = 0;
            vbl[i].attributeCount = 0;
        }
        vbl[i].stepMode = WGPUVertexStepMode_Vertex;
    }
    WGPUVertexState vsst = WGPU_VERTEX_STATE_INIT;
    vsst.module = vs;
    vsst.entryPoint = {"main", 4};
    vsst.bufferCount = hi;
    vsst.buffers = hi ? vbl.data() : nullptr;
    WGPUPipelineLayoutDescriptor pl = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    /* The real layout, because the real vertex shader reads binding 0 and 1. */
    pl.bindGroupLayoutCount = 1;
    pl.bindGroupLayouts = &g.bind_layout;
    WGPURenderPipelineDescriptor rp = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rp.layout = wgpuDeviceCreatePipelineLayout(g.device, &pl);
    rp.vertex = vsst;
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp.fragment = &fs;
    WGPUDepthStencilState pds = WGPU_DEPTH_STENCIL_STATE_INIT;
    pds.format = g.depth_format;
    pds.depthWriteEnabled = WGPUOptionalBool_False;
    pds.depthCompare = WGPUCompareFunction_Always;
    rp.depthStencil = &pds;
    const int before = g.errors;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(g.device, &rp);
    wgpuDevicePoll(g.device, true, nullptr);
    if (p && g.errors == before) {
        cache[in.vp_hash] = p;
        return p;
    }
    std::fprintf(stderr, "wgpu: vs_pipeline refused: %s\n", g.first_error);
    return nullptr;
}
/* Fill and bind group 0 for one draw. Split out of draw() so the PROBE_VS path
 * can use exactly the same bindings; the probe previously returned before this,
 * so its real vertex shader read no uniforms and collapsed to nothing. */
void bind_for(const DrawInputs& in) {
    if (!g.pass) return;
    /* One bind group per draw. EVERY binding of the layout is filled: WebGPU
     * rejects a group that leaves a declared binding empty, which is what made
     * the first version invalid ("BindGroup with ... is invalid") and aborted the
     * process, leaving every capture black. Unused units get a 1x1 dummy rather
     * than being omitted. */
    {
        std::vector<WGPUBindGroupEntry> be;
        be.reserve(4 + 8 + 32);
        static uint8_t zeros[8192] = {};
        for (uint32_t bi = 0; bi < 4; bi++) {
            const uint32_t size = in.uniform[bi] && in.uniform_size[bi] ? in.uniform_size[bi] : 8160u;
            WGPUBuffer b = make_buffer(WGPUBufferUsage_Uniform, size, in.uniform[bi] ? in.uniform[bi] : zeros);
            WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
            e.binding = bi;
            e.buffer = b;
            e.offset = 0;
            e.size = size;
            be.push_back(e);
        }
        WGPUTextureView dummy = dummy_view(WGPUTextureViewDimension_2D);
        WGPUSampler dsampler = dummy_sampler();
        auto add_pair = [&](uint32_t binding, uint32_t unit) {
            /* The dummy has to match the dimension the layout declares for this
             * binding: unit 7 is 1D, the rest 2D. A mismatched view makes the
             * whole bind group invalid, and the failure names the group, not the
             * binding. */
            const WGPUTextureViewDimension dim = unit == 7 ? WGPUTextureViewDimension_1D : WGPUTextureViewDimension_2D;
            WGPUTextureView view = dummy_view(dim);

            /* PS3BASE_WGPU_DUMMY_UNITS=7: replace the listed units with the
             * 1x1 dummy. It tells which UNIT a residual colour comes from
             * without touching the sampler derivation or the decode. */
            static const uint32_t dummy_units = [] {
                const char* e = std::getenv("PS3BASE_WGPU_DUMMY_UNITS");
                if (!e) return 0u;
                uint32_t m = 0;
                const char* p = e;
                while (*p) {
                    const unsigned long v = std::strtoul(p, const_cast<char**>(&p), 0);
                    if (v < 16) m |= 1u << v;
                    while (*p == ',' || *p == ' ') p++;
                }
                return m;
            }();
            if ((in.texture_mask & (1u << unit)) && in.texture[unit].pixels && !(dummy_units & (1u << unit))) {
                if (WGPUTexture t = upload_texture(in.texture[unit], unit, dim == WGPUTextureViewDimension_1D ? WGPUTextureDimension_1D : WGPUTextureDimension_2D)) {
                    /* The view must be the dimension the layout declares for this
                     * binding, not the texture native one: a 2D view on the 1D slot
                     * makes the whole bind group invalid, and the message names the
                     * group rather than the binding. */
                    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    vd.format = std::getenv("PS3BASE_WGPU_TEX_SRGB") ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
                    vd.dimension = dim;
                    /* The view must expose the WHOLE chain. With
                     * mipLevelCount = 1 the texture holds all 11 levels but the
                     * view declares one, so WebGPU clamps every LOD to 0 no
                     * matter what the sampler says -- the sampler derivation was
                     * correct and unobservable at the same time. */
                    vd.mipLevelCount = in.texture[unit].mip_count > 1 ? std::min<uint32_t>(in.texture[unit].mip_count, 16u) : 1u;
                    vd.arrayLayerCount = 1;
                    view = wgpuTextureCreateView(t, &vd);
                    {
                        static uint64_t nv = 0;
                        if (nv++ < 6) std::fprintf(stderr, "wgpu: unit %u texture %p view %p dim %d %ux%u\n",
                                             unit, (void*)t, (void*)view, int(dim), in.texture[unit].width, in.texture[unit].height);
                    }
                    if (!view) view = dummy_view(dim);
                }
            }
            {
                static uint64_t nv2 = 0;
                if (nv2++ < 8 && unit < 16) std::fprintf(stderr, "wgpu: pair binding %u unit %u -> view %p (mask 0x%x, pixels %p)\n",
                                         binding, unit, (void*)view, in.texture_mask, (const void*)in.texture[unit].pixels);
            }
            WGPUBindGroupEntry te = WGPU_BIND_GROUP_ENTRY_INIT;
            te.binding = binding;
            te.textureView = view;
            be.push_back(te);
            WGPUBindGroupEntry se = WGPU_BIND_GROUP_ENTRY_INIT;
            se.binding = binding + 64;
            /* The unit's OWN sampler, derived from its RSX registers; the dummy
             * only for a slot nothing samples. One dummy for every unit was the
             * defect: it threw away filtering, mip mode, wrap and LOD clamp. */
            const bool sampled = (in.texture_mask & (1u << unit)) && in.texture[unit].pixels;
            /* PS3BASE_WGPU_DUMMY_SAMPLER=1 restores the old one-sampler-for-every-
             * unit behaviour WITHOUT touching the mip chain, so the two halves of
             * this fix can be attributed separately instead of as one number. */
            static const bool force_dummy = std::getenv("PS3BASE_WGPU_DUMMY_SAMPLER") != nullptr;
            WGPUSampler unit_sampler = (sampled && !force_dummy) ? sampler_for(in.texture[unit].sampler, unit) : dsampler;
            se.sampler = unit_sampler;
            be.push_back(se);
            if (sampled) {
                /* One line per distinct (unit, register set) so the derivation is
                 * auditable from the log rather than from the source alone. */
                static std::vector<uint64_t> seen;
                const auto& rs = in.texture[unit].sampler;
                const uint64_t k = (uint64_t(rs.address) << 32) ^ rs.control0 ^ (uint64_t(rs.filter) << 16) ^ (uint64_t(rs.levels) << 48) ^ (rs.shadow ? 1ull << 60 : 0) ^
                                   (uint64_t(unit) << 56);
                bool have = false;
                for (uint64_t v : seen) if (v == k) { have = true; break; }
                if (!have && seen.size() < 48) {
                    seen.push_back(k);
                    const SamplerDesc d = derive_sampler(rs);
                    std::fprintf(stderr,
                                 "wgpu: sampler unit %u addr 0x%x c0 0x%x filt 0x%x levels %u shadow %d -> mag %u min %u mip %u wrap %u/%u/%u lod %.3f..%.3f aniso %u bias %.3f\n",
                                 unit, rs.address, rs.control0, rs.filter, rs.levels, int(rs.shadow),
                                 uint32_t(d.sd.magFilter), uint32_t(d.sd.minFilter), uint32_t(d.sd.mipmapFilter),
                                 uint32_t(d.sd.addressModeU), uint32_t(d.sd.addressModeV), uint32_t(d.sd.addressModeW),
                                 double(d.sd.lodMinClamp), double(d.sd.lodMaxClamp), uint32_t(d.sd.maxAnisotropy), double(d.lod_bias));
                }
            }
        };
        /* Vertex-stage units 0..3 at binding 16, fragment units 0..15 at 32. The
         * vertex slots are declared by the layout even when a draw does not use
         * them, so they get the dummy and the layout stays satisfied. */
        /* Vertex-stage units 0..3. They are NOT read from in.texture (which has 16
         * entries for the FRAGMENT units): indexing 16..19 read past the array and
         * put garbage views in the group. The mirror does not mirror vertex
         * textures, so those slots get the dummy. */
        for (uint32_t v = 0; v < 4; v++) {
            WGPUBindGroupEntry te = WGPU_BIND_GROUP_ENTRY_INIT;
            te.binding = 16 + v;
            te.textureView = dummy_view(WGPUTextureViewDimension_2D);
            be.push_back(te);
            WGPUBindGroupEntry se = WGPU_BIND_GROUP_ENTRY_INIT;
            se.binding = 16 + v + 64;
            se.sampler = dsampler;
            be.push_back(se);
        }
        for (uint32_t tu = 0; tu < 16; tu++) add_pair(32 + tu, tu);
        {
            static uint64_t reported = 0;
            if (reported++ < 4) {
                uint32_t real = 0;
                for (uint32_t tu = 0; tu < 16; tu++) if ((in.texture_mask & (1u << tu)) && in.texture[tu].pixels) real++;
                std::fprintf(stderr, "wgpu: bind group: mask 0x%x, %u real textures, fp %016llx\n",
                             in.texture_mask, real, (unsigned long long)in.fp_hash);
            }
        }
        WGPUBindGroupDescriptor bg = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bg.layout = g.bind_layout;
        bg.entryCount = be.size();
        bg.entries = be.data();
        /* Scoped so a rejection reports WHY instead of aborting on the next
         * setBindGroup. An invalid bind group is created successfully and only
         * fails when it is used, so the check has to be around creation. */
        const int before = g.errors;
        wgpuDevicePushErrorScope(g.device, WGPUErrorFilter_Validation);
        WGPUBindGroup group = wgpuDeviceCreateBindGroup(g.device, &bg);
        wgpuDevicePoll(g.device, true, nullptr);
        /* PopErrorScope takes a callback INFO; passing a zeroed one is an "invalid\n         * callback" panic inside wgpu. The result arrives through a real callback,
         * and the wait that follows is what makes it fire. */
        WGPUPopErrorScopeCallbackInfo pci = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        pci.mode = WGPUCallbackMode_AllowProcessEvents;
        pci.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView msg, void* u1, void*) {
            auto* out = static_cast<std::pair<int, std::string>*>(u1);
            out->first = int(type);
            if (status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError) {
                out->second.assign(msg.data ? msg.data : "", size_t(msg.length));
            }
        };
        std::pair<int, std::string> scope{WGPUErrorType_NoError, {}};
        pci.userdata1 = &scope;
        wgpuDevicePopErrorScope(g.device, pci);
        for (int i = 0; i < 200 && scope.first == WGPUErrorType_NoError && scope.second.empty(); i++) {
            wgpuInstanceProcessEvents(g.instance);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (!scope.second.empty()) {
            static int logged = 0;
            if (logged++ < 3) std::fprintf(stderr, "wgpu: bind group rejected: %s\n", scope.second.c_str());
            s.skipped++;
            return;
        }
        if (group) wgpuRenderPassEncoderSetBindGroup(g.pass, 0, group, 0, nullptr);
    }
}
WGPURenderPipeline simple_pipeline() {
    static WGPURenderPipeline cached = nullptr;
    if (cached) return cached;
    static const char* kWGSL =
        "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
        "  var p = array<vec2f,3>(vec2f(-1.0,-1.0), vec2f(3.0,-1.0), vec2f(-1.0,3.0));"
        "  return vec4f(p[i], 0.0, 1.0);"
        "}"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 1.0, 1.0); }";
    WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
    wg.code = {kWGSL, strlen(kWGSL)};
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &wg.chain;
    WGPUShaderModule m = wgpuDeviceCreateShaderModule(g.device, &sd);
    if (!m) return nullptr;
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = g.color_format;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = m;
    fs.entryPoint = {"fs", 2};
    fs.targetCount = 1;
    fs.targets = &ct;
    WGPUVertexState vs = WGPU_VERTEX_STATE_INIT;
    vs.module = m;
    vs.entryPoint = {"vs", 2};
    WGPUPipelineLayoutDescriptor pl = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPURenderPipelineDescriptor rp = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rp.layout = wgpuDeviceCreatePipelineLayout(g.device, &pl);
    rp.vertex = vs;
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp.fragment = &fs;
    /* The same depth format the pass carries. A pipeline without it cannot be
     * bound to a pass that has a depth attachment ("Incompatible depth-stencil
     * attachment format ... uses an attachment with format None"). */
    WGPUDepthStencilState pds = WGPU_DEPTH_STENCIL_STATE_INIT;
    pds.format = g.depth_format;
    pds.depthWriteEnabled = WGPUOptionalBool_False;
    pds.depthCompare = WGPUCompareFunction_Always;
    rp.depthStencil = &pds;
    cached = wgpuDeviceCreateRenderPipeline(g.device, &rp);
    wgpuDevicePoll(g.device, true, nullptr);
    return cached;
}

void self_test() {
    static const char* kWGSL =
        "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
        "  var p = array<vec2f,3>(vec2f(-1.0,-1.0), vec2f(3.0,-1.0), vec2f(-1.0,3.0));"
        "  return vec4f(p[i], 0.0, 1.0);"
        "}"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(0.0, 1.0, 0.0, 1.0); }";
    WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
    wg.code = {kWGSL, strlen(kWGSL)};
    WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    sd.nextInChain = &wg.chain;
    WGPUShaderModule m = wgpuDeviceCreateShaderModule(g.device, &sd);
    if (!m) { std::fprintf(stderr, "wgpu: self-test shader refused\n"); return; }
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = g.color_format;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = m;
    fs.entryPoint = {"fs", 2};
    fs.targetCount = 1;
    fs.targets = &ct;
    WGPUVertexState vs = WGPU_VERTEX_STATE_INIT;
    vs.module = m;
    vs.entryPoint = {"vs", 2};
    WGPURenderPipelineDescriptor rp = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    /* An EMPTY layout: the probe uses no bindings, so it needs no bind group.
     * Reusing g.pipe_layout made the draw illegal ("expects a BindGroup to be
     * set at index 0") and aborted the run. */
    WGPUPipelineLayoutDescriptor pl = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pl.bindGroupLayoutCount = 0;
    pl.bindGroupLayouts = nullptr;
    WGPUPipelineLayout empty_layout = wgpuDeviceCreatePipelineLayout(g.device, &pl);
    rp.layout = empty_layout;
    rp.vertex = vs;
    rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rp.fragment = &fs;
    const int before = g.errors;
    WGPURenderPipeline p = wgpuDeviceCreateRenderPipeline(g.device, &rp);
    wgpuDevicePoll(g.device, true, nullptr);
    if (!p || g.errors != before) { std::fprintf(stderr, "wgpu: self-test pipeline refused: %s\n", g.first_error); return; }

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = g.offscreen_view;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {1.0, 0.0, 1.0, 1.0};   /* magenta: unmistakable */
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &ca;
    WGPURenderPassEncoder pe = wgpuCommandEncoderBeginRenderPass(enc, &desc);
    wgpuRenderPassEncoderSetPipeline(pe, p);
    wgpuRenderPassEncoderDraw(pe, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pe);
    wgpuRenderPassEncoderRelease(pe);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuCommandEncoderRelease(enc);
    wgpuQueueSubmit(g.queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuDevicePoll(g.device, true, nullptr);
    std::fprintf(stderr, "wgpu: self-test drew a magenta full-screen triangle (errors %d)\n", g.errors);
    /* Read the target back with a copy of its own, on a fresh encoder: this runs
     * before any renderer code and is the only way to tell a broken readback from
     * a broken draw. The shared readback() cannot be used here because it finishes
     * g.encoder, which this probe never touched. */
    {
        const uint64_t pitch = ((uint64_t(g.width) * 4) + 255) & ~255ull;
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        bd.size = pitch * g.height;
        WGPUBuffer rbuf = wgpuDeviceCreateBuffer(g.device, &bd);
        WGPUCommandEncoder e2 = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = g.color;
        src.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo dstb = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dstb.buffer = rbuf;
        dstb.layout.bytesPerRow = uint32_t(pitch);
        dstb.layout.rowsPerImage = g.height;
        WGPUExtent3D csz = {g.width, g.height, 1};
        wgpuCommandEncoderCopyTextureToBuffer(e2, &src, &dstb, &csz);
        WGPUCommandBuffer cb2 = wgpuCommandEncoderFinish(e2, nullptr);
        wgpuCommandEncoderRelease(e2);
        wgpuQueueSubmit(g.queue, 1, &cb2);
        wgpuCommandBufferRelease(cb2);
        bool mapped = false;
        WGPUBufferMapCallbackInfo mci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mci.mode = WGPUCallbackMode_AllowProcessEvents;
        mci.callback = [](WGPUMapAsyncStatus st, WGPUStringView, void* u1, void*) { *static_cast<bool*>(u1) = st == WGPUMapAsyncStatus_Success; };
        mci.userdata1 = &mapped;
        wgpuBufferMapAsync(rbuf, WGPUMapMode_Read, 0, pitch * g.height, mci);
        for (int i = 0; i < 20000 && !mapped; i++) {
            wgpuInstanceProcessEvents(g.instance);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        if (mapped) {
            const uint8_t* px = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(rbuf, 0, pitch * g.height));
            size_t nonblack = 0;
            for (uint32_t y = 0; y < g.height; y++) {
                const uint8_t* row = px + y * pitch;
                for (uint32_t x = 0; x < g.width; x++) if (row[x*4] || row[x*4+1] || row[x*4+2]) nonblack++;
            }
            std::fprintf(stderr, "wgpu: self-test readback nonblack %zu/%zu\n", nonblack, size_t(g.width) * g.height);
            wgpuBufferUnmap(rbuf);
        } else {
            std::fprintf(stderr, "wgpu: self-test readback could not map\n");
        }
        wgpuBufferRelease(rbuf);
    }
}

}  // namespace
/* =======================================================================
 * THE BISECTION RENDERER
 *
 * One function that draws exactly one of the test steps, with its own pipeline
 * built from a WGSL literal. It touches nothing the game's path uses: its own
 * shader module, its own pipeline, its own vertex buffer. That is deliberate --
 * if it renders and the game's draws do not, the difference is in the game's
 * inputs, not in the target or the submit.
 *
 * The full-screen triangle is (-1,-1) (3,-1) (-1,3) in clip space, which covers
 * the whole viewport with one primitive and needs no index buffer.
 */
namespace {

const char* kFullscreenWgsl =
    "@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {"
    "  let x = f32(i32(i) - 1) * 3.0;"
    "  let y = f32(i32(i & 1u) * 2 - 1) * 3.0;"
    "  return vec4f(x, y, 0.0, 1.0);"
    "}"
    "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }";

/* The game's vertex program with OUR fragment colour, so the vertex stage is
 * tested on its own. The fragment shader ignores whatever the vertex stage passes
 * on, which is the point: if the triangle appears, the positions the game's vertex
 * shader produced are on screen. */
const char* kFixedRedFsWgsl =
    "@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.0, 0.0, 1.0); }";

WGPURenderPipeline g_diag_pipe = nullptr;
WGPUShaderModule g_diag_vs = nullptr;
WGPUShaderModule g_diag_fs = nullptr;

WGPUShaderModule diag_module(const char* code) {
    WGPUShaderSourceWGSL wg = WGPU_SHADER_SOURCE_WGSL_INIT;
    wg.code = {code, strlen(code)};
    WGPUShaderModuleDescriptor d = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    d.nextInChain = &wg.chain;
    return wgpuDeviceCreateShaderModule(g.device, &d);
}

/* Build the fixed-red pipeline, optionally with culling / depth / blending, so
 * each of those states can be turned on one at a time. */
WGPURenderPipeline diag_pipeline(bool cull, bool depth, bool blend) {
    if (!g_diag_vs) g_diag_vs = diag_module(kFullscreenWgsl);
    WGPUShaderModule fs = g_diag_fs ? g_diag_fs : (g_diag_fs = diag_module(kFullscreenWgsl));
    if (!g_diag_vs || !fs) return nullptr;
    WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    /* AN EXPLICIT EMPTY LAYOUT for the diagnostic pipeline.
     *
     * Reusing g.pipe_layout declares four uniform buffers and 32 samplers, and WebGPU
     * then requires a bind group for it at draw time. The test triangle binds nothing,
     * so the draw is dropped -- silently: 30 draws issued, 0 validation errors, and a
     * black canvas. An empty layout matches what this pipeline actually uses. */
    {
        static WGPUPipelineLayout empty_layout = nullptr;
        if (!empty_layout) {
            WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            pld.bindGroupLayoutCount = 0;
            pld.bindGroupLayouts = nullptr;
            empty_layout = wgpuDeviceCreatePipelineLayout(g.device, &pld);
        }
        pd.layout = empty_layout;
    }
    pd.vertex.module = g_diag_vs;
    pd.vertex.entryPoint = {"vs", 2};
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.cullMode = cull ? WGPUCullMode_Back : WGPUCullMode_None;
    pd.primitive.frontFace = WGPUFrontFace_CCW;
    pd.multisample.count = 1;
    WGPUFragmentState fsn = WGPU_FRAGMENT_STATE_INIT;
    fsn.module = fs;
    fsn.entryPoint = {"fs", 2};
    WGPUColorTargetState ct = WGPU_COLOR_TARGET_STATE_INIT;
    ct.format = g.color_format;
    ct.writeMask = WGPUColorWriteMask_All;
    WGPUBlendState bs = WGPU_BLEND_STATE_INIT;
    if (blend) {
        bs.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        bs.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bs.color.operation = WGPUBlendOperation_Add;
        bs.alpha.srcFactor = WGPUBlendFactor_One;
        bs.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bs.alpha.operation = WGPUBlendOperation_Add;
        ct.blend = &bs;
    }
    fsn.targetCount = 1;
    fsn.targets = &ct;
    pd.fragment = &fsn;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (depth) {
        ds.format = g.depth_format;
        ds.depthWriteEnabled = WGPUOptionalBool_True;
        ds.depthCompare = WGPUCompareFunction_LessEqual;
        pd.depthStencil = &ds;
    }
    return wgpuDeviceCreateRenderPipeline(g.device, &pd);
}

}  // namespace

void draw(const DrawInputs& in) {
    if (!on_owner_thread("draw")) return;
    /* THE BISECTION MODES OWN THE FRAME. In modes 1..6 the diagnostic renderer
     * draws instead of the game, so the game's draws must not also run. */
    const uint32_t dbg = g_debug_mode.load();
    /* Mode 3 lets the GAME's draws through, with our fixed-red fragment shader
     * substituted in pipeline_for. Modes 1, 2 and 4..6 are the diagnostic renderer's
     * alone. */
    if (dbg >= 1 && dbg <= 6 && dbg != 3) return;
    /* T1 BISECT: the renderer counted 30 204 draws and the backend reported none.
     * These three counters say WHICH early return ate them, so "the draw never
     * arrived" is distinguishable from "it arrived and the pipeline was refused". */
    if (!g.ready) return;
    /* T1: A NULL VIEW HERE IS THE NORMAL START OF A FRAME, NOT A REFUSAL.
     *
     * end_frame() releases the imported canvas texture and nulls the view after
     * every submit, because a canvas texture is only valid between
     * getCurrentTexture() and the next one -- a cached handle is a stale handle.
     * The next frame therefore has to IMPORT A NEW ONE before it can draw.
     *
     * Nothing did: make_targets() was only called from init() and from the resize
     * path, so the sequence was init -> view created -> first present -> view
     * nulled -> every later draw refused at this line. Measured: make_targets ran
     * exactly ONCE per run, the view it built was valid (0x9bc97260), and 30 204
     * draws were then dropped with "color_view=0" on the same thread that had just
     * created it.
     *
     * Re-importing here is the correct place: it happens on the FIFO thread that
     * owns the frame, before begin_pass() opens the pass that references the view,
     * and s1_canvas_texture() is proxied so the import runs on the realm that owns
     * the device. */
    if (!g.offscreen_view) {
        /* end_frame() imports the next canvas texture right after each submit, so a
         * null view here means the import failed, not that this is a new frame. The
         * refusal is counted so it cannot be mistaken for "the draw never arrived". */
        static std::atomic<uint32_t> no_view{0};
        if (no_view.fetch_add(1) < 3)
            LOG_WARN("wgpu", "draw refused: no canvas view (the per-frame import failed)");
        return;
    }
    /* PS3BASE_WGPU_SKIP_UNTEXTURED: a draw that samples nothing paints the whole
     * surface with the dummy texture, so the last one of those hides every draw
     * before it. Skipping them shows what the textured draws produced. */
    if (std::getenv("PS3BASE_WGPU_SKIP_UNTEXTURED") && in.texture_mask == 0) return;

    /* The target size comes from the RENDERER, not from init(). The mirror used
     * to keep the 1280x720 it was created with while the Vulkan path drew into a
     * 1280x480 surface and let the presenter upscale, so every draw was
     * stretched differently and the two images could not converge below the
     * resolution difference. Resizing here is safe because begin_pass() below
     * ends the previous pass first; the resize happens before any encoder that
     * references the new views is opened.
     *
     * The size is the target the DRAW names. When the renderer does not supply
     * one (target_width 0), the init size stands. */
    static const bool no_resize = std::getenv("PS3BASE_WGPU_NO_RESIZE") != nullptr;
    if (!no_resize && in.target_width && in.target_height && (in.target_width != g.width || in.target_height != g.height)) {
        end_pass();
        if (g.encoder) {
            /* An open encoder holds references to the old views. FINISH AND SUBMIT
             * it before swapping the targets -- the previous code submitted an EMPTY
             * command list (wgpuQueueSubmit(g.queue, 0, nullptr)) and then released the
             * encoder, which DISCARDED every draw recorded in it. Measured: 19 pipelines
             * built, draws issued with real counts, 0 GPU errors, and a canvas of one
             * colour, because the frame was thrown away at every resize. */
            WGPUCommandBuffer old_cb = wgpuCommandEncoderFinish(g.encoder, nullptr);
            if (old_cb) {
                wgpuQueueSubmit(g.queue, 1, &old_cb);
                wgpuCommandBufferRelease(old_cb);
            }
            wgpuCommandEncoderRelease(g.encoder);
            g.encoder = nullptr;
            release_frame_buffers();
        }
        make_targets(in.target_width, in.target_height);
        g.encoder = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    }
    WGPURenderPipeline p = pipeline_for(in);
    if (!p) {
        static std::atomic<uint32_t> np{0};
        if (np.fetch_add(1) < 5)
            LOG_WARN("wgpu", "draw refused: pipeline_for returned null (vp %016llx fp %016llx vs_wgsl %zu fs_wgsl %zu errors %d)",
                     (unsigned long long)in.vp_hash, (unsigned long long)in.fp_hash,
                     in.vs_wgsl.size(), in.fs_wgsl.size(), g.errors);
        return;
    }
    begin_pass();
    if (!g.pass) {
        static std::atomic<uint32_t> npass{0};
        if (npass.fetch_add(1) < 5) LOG_WARN("wgpu", "draw refused: begin_pass produced no pass");
        return;
    }
    wgpuRenderPassEncoderSetPipeline(g.pass, p);

    /* EVERY slot the pipeline declares is bound, with a stand-in when the draw
     * carries no data for it.
     *
     * Two measured reasons. WebGPU rejects a draw whose pipeline declares a
     * vertex buffer the pass has not set ("requires vertex buffer 1 to be set"),
     * where Vulkan tolerates it. And the renderer can legitimately leave vdata[]
     * null for a declared input: the edge-compute path hands the expanded stream
     * to the GPU and sets vdata[i] to nullptr, so "declared but not bound" is a
     * real state, not a bug in the caller.
     *
     * The stand-in is one float4 of zeros, which is what the Vulkan path binds
     * for a missing attribute too. */
    /* Every slot the pipeline declares is bound. The layout array is dense, so a
     * hole (a slot with no attribute) still occupies a buffer index and WebGPU
     * rejects the draw if it is not set. */
    uint32_t vhighest = 0;
    for (uint32_t i = 0; i < 16; i++) if (in.vertex_inputs & (1u << i)) vhighest = i + 1;
    static float zero_vertex[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (uint32_t i = 0; i < 16; i++) {
        if (i >= vhighest) break;
        const uint64_t bytes = uint64_t(in.vertex_count) * 16;
        WGPUBuffer vb = in.vertex[i] ? make_buffer(WGPUBufferUsage_Vertex, bytes, in.vertex[i])
                                     : make_buffer(WGPUBufferUsage_Vertex, 16, zero_vertex);
        if (!vb) return;
        wgpuRenderPassEncoderSetVertexBuffer(g.pass, i, vb, 0, in.vertex[i] ? bytes : 16);
        if (in.vertex[i]) s.upload_bytes += bytes;
    }
    WGPUBuffer ib = make_buffer(WGPUBufferUsage_Index, uint64_t(in.index_count) * 4, in.indices);
    if (!ib) return;
    wgpuRenderPassEncoderSetIndexBuffer(g.pass, ib, WGPUIndexFormat_Uint32, 0, uint64_t(in.index_count) * 4);

    bind_for(in);

    /* Viewport, scissor and blend constant are DYNAMIC in the Vulkan path
     * (VK_DYNAMIC_STATE_*), so the pipeline does not carry them and the mirror
     * has to set them per draw. None of the three was set before: the viewport
     * and scissor happened to match the whole target on these screens, which is
     * why their absence was invisible, and the blend constant did not -- a draw
     * using CONSTANT_COLOR or CONSTANT_ALPHA blended against (0,0,0,0) with no
     * error and no log line. */
    {
        /* The BLEND CONSTANT is set; the viewport and scissor are NOT, and that
         * is a measured decision rather than an omission.
         *
         * The renderer's viewport and scissor are expressed in the RENDER
         * SURFACE's space, which on this title's logo frame is 1280x480, while
         * the mirror's colour target is the 1280x720 presented image. Setting
         * the 480-tall viewport on the 720-tall target put the whole frame in
         * the top two thirds and took RMS from 12.09 to 155.20 (measured);
         * leaving WebGPU's default of "the whole target" is what reproduces the
         * reference, because the presenter is what scales 480 to 720 and the
         * mirror has no presenter. Reproducing the scale belongs in the mirror's
         * target management, not in a per-draw viewport, so the default stands
         * and the difference is recorded instead of being papered over. */
        WGPUColor bc = {in.blend_constant[0], in.blend_constant[1], in.blend_constant[2], in.blend_constant[3]};
        wgpuRenderPassEncoderSetBlendConstant(g.pass, &bc);
    }

    /* INDEXED OR NOT, decided by the record -- and getting this wrong rendered
     * NOTHING for the most important draw on screen.
     *
     * This always called DrawIndexed. A non-indexed draw carries no index block, so
     * index_count was 0 and the call became DrawIndexed(0, 1, ...): zero indices,
     * zero primitives, no error, no pixel. Measured on the logo frame: the draws
     * alternated between "index_count 0 vertex_count 3" (a full-screen triangle,
     * NON-indexed) and "index_count 38 vertex_count 38" (indexed). The 3-vertex one
     * is the one that covers the screen, and it was the one being dropped.
     *
     * in.indices being null is the signal: the replay sets it only when the record
     * was indexed AND its index block was in the store. */
    static uint64_t issued = 0;
    if (in.indices && in.index_count) {
        wgpuRenderPassEncoderDrawIndexed(g.pass, in.index_count, 1, 0, in.base_vertex, 0);
        if (issued++ < 6) std::fprintf(stderr, "wgpu: issued DrawIndexed(%u, 1, 0, %d, 0)\n", in.index_count, in.base_vertex);
    } else {
        wgpuRenderPassEncoderDraw(g.pass, in.vertex_count, 1, 0, 0);
        if (issued++ < 6) std::fprintf(stderr, "wgpu: issued Draw(%u, 1, 0, 0)\n", in.vertex_count);
    }
    s.draws++;
    s.last_fp = in.fp_hash;
    s.last_mask = in.texture_mask;
    if (s.draws <= 4 || s.draws % 100 == 0) {
        std::fprintf(stderr, "wgpu: draw %llu idx %u base %d vtx[0]=%p vtx[1]=%p nv %u vp %016llx fp %016llx\n",
                     (unsigned long long)s.draws, in.index_count, in.base_vertex,
                     (const void*)in.vertex[0], (const void*)in.vertex[1], in.vertex_count,
                     (unsigned long long)in.vp_hash, (unsigned long long)in.fp_hash);
        std::fprintf(stderr, "wgpu:   vp %.1f %.1f %.1f %.1f scissor %d %d %dx%d inputs 0x%x\n",
                     in.viewport[0], in.viewport[1], in.viewport[2], in.viewport[3],
                     in.scissor[0], in.scissor[1], in.scissor[2], in.scissor[3], in.vertex_inputs);
        if (in.uniform[3]) {
            const uint32_t* u3 = static_cast<const uint32_t*>(in.uniform[3]);
            std::fprintf(stderr, "wgpu:   fs window %.3f %.3f %.3f %.3f fog %.3f %.3f flags 0x%x 0x%x 0x%x 0x%x size %u\n",
                         ((const float*)u3)[0], ((const float*)u3)[1], ((const float*)u3)[2], ((const float*)u3)[3],
                         ((const float*)u3)[4], ((const float*)u3)[5], u3[8], u3[9], u3[10], u3[11], in.uniform_size[3]);
        }
        if (in.uniform[1]) {
            const float* u = static_cast<const float*>(in.uniform[1]);
            std::fprintf(stderr, "wgpu:   vp_scale %.3f %.3f %.3f %.3f | vp_offset %.1f %.1f %.1f %.1f | surface %.1f %.1f %.4f %.4f\n",
                         u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7], u[8], u[9], u[10], u[11]);
        }
        if (in.vertex[0]) std::fprintf(stderr, "wgpu:   v0 %.4f %.4f %.4f %.4f | v1 %.4f %.4f %.4f %.4f\n",
                     in.vertex[0][0], in.vertex[0][1], in.vertex[0][2], in.vertex[0][3],
                     in.vertex[0][4], in.vertex[0][5], in.vertex[0][6], in.vertex[0][7]);
    }
}

void clear(uint32_t mask, const float color[4], float depth, uint32_t) {
    if (!on_owner_thread("clear")) return;
    static uint64_t nclears = 0;
    nclears++;
    if (nclears <= 4) std::fprintf(stderr, "wgpu: clear %llu mask 0x%x color %.2f %.2f %.2f %.2f depth %.2f (draws %llu)\n",
                        (unsigned long long)nclears, mask, color[0], color[1], color[2], color[3], depth, (unsigned long long)s.draws);
    if (!g.ready) return;
    /* A CLEAR WITH NO VIEW IMPORTS ONE, instead of returning and losing the frame.
     *
     * The view is created by make_targets(), which end_frame() calls AFTER the
     * submit. A frame whose first command is a clear therefore found g.color_view
     * null and returned here -- measured: 82 690 records popped, 161 flips, and the
     * clear body ran FOUR times, so nothing was ever recorded into a frame. The
     * cycle (clear needs a view, the view is made at the end of a frame, a frame
     * needs a clear) is broken by importing the target here when it is missing. */
    if (!g.offscreen_view) make_targets(g.width, g.height);
    if (!g.offscreen_view) return;
    end_pass();
    /* Keep the frame's encoder: it already holds the draws before this
     * clear, and releasing it would discard them. A clear is just another
     * render pass in the same encoder, which WebGPU allows. */
    if (!g.encoder) g.encoder = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = g.offscreen_view;
    ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    /* The RSX mask says WHICH attachments to clear: bits 0xF0 are colour, 0x3
     * depth and stencil. Clearing colour on a depth-only clear erased every
     * draw already recorded -- a black frame with no validation error, which
     * is exactly what the readback showed. */
    ca.loadOp = (mask & 0xF0) ? WGPULoadOp_Clear : WGPULoadOp_Load;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {color[0], color[1], color[2], color[3]};
    /* MODE 1: a MAGENTA clear and nothing else. If this PNG is not magenta, the
     * defect is in the target or the submit, and nothing downstream can be trusted. */
    if (g_debug_mode.load() >= 1) {
        ca.loadOp = WGPULoadOp_Clear;
        ca.clearValue = {1.0, 0.0, 1.0, 1.0};
    }
    WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    da.view = g.depth_view;
    da.depthLoadOp = (mask & 0x1) ? WGPULoadOp_Clear : WGPULoadOp_Load;
    da.depthStoreOp = WGPUStoreOp_Store;
    da.stencilLoadOp = (mask & 0x2) ? WGPULoadOp_Clear : WGPULoadOp_Load;
    da.stencilStoreOp = WGPUStoreOp_Store;
    da.stencilClearValue = 0;
    /* WebGPU REJECTS a non-finite depthClearValue and drops the whole command
     * buffer -- measured: "Failed to read the 'depthClearValue' property ... The
     * provided float value is non-finite", after which no frame was ever submitted.
     * A register the guest left in an unexpected state must degrade to a valid
     * clear, not destroy the frame. */
    da.depthClearValue = (depth >= 0.0f && depth <= 1.0f) ? depth : 1.0f;
    WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &ca;
    rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder clear_pass = wgpuCommandEncoderBeginRenderPass(g.encoder, &rp);
    wgpuRenderPassEncoderEnd(clear_pass);
    wgpuRenderPassEncoderRelease(clear_pass);
}

/* Submit what the frame recorded and read the colour target back. The readback
 * is what every number in the comparison section is computed from, so it is
 * part of the backend rather than a separate tool. */
namespace {

bool readback(std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
    std::fprintf(stderr, "wgpu: readback now: last draw fp %016llx mask 0x%x (total draws %llu)\n",
                 (unsigned long long)s.last_fp, s.last_mask, (unsigned long long)s.draws);
    /* The pass MUST be closed before the encoder is finished: an encoder with
     * an unfinished render pass is discarded as a whole, which is what made the
     * capture black while the draw counter kept climbing. */
    end_pass();
    if (!g.encoder) {
        /* Not an error in the self-test path, which owns its own encoder. In the
         * main path it means the frame had no commands at all. */
        std::fprintf(stderr, "wgpu: readback: no encoder (draws %llu)\n", (unsigned long long)s.draws);
        return false;
    }
    width = g.width;
    height = g.height;
    const uint64_t bytes_per_row = uint64_t(width) * 4;
    /* CopyTextureToBuffer requires the row pitch to be a multiple of 256. */
    const uint64_t pitch = (bytes_per_row + 255) & ~255ull;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bd.size = pitch * height;
    WGPUBuffer rb = wgpuDeviceCreateBuffer(g.device, &bd);
    if (!rb) { std::fprintf(stderr, "wgpu: readback: no buffer\n"); return false; }
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = g.color;
    src.mipLevel = 0;
    src.origin = {0, 0, 0};
    src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = rb;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = uint32_t(pitch);
    dst.layout.rowsPerImage = height;
    WGPUExtent3D size = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(g.encoder, &src, &dst, &size);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(g.encoder, nullptr);
    wgpuCommandEncoderRelease(g.encoder);
    g.encoder = nullptr;
    wgpuQueueSubmit(g.queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    /* The frame is closed. A new one starts at the next clear(). */

    bool ok = false;
    WGPUBufferMapCallbackInfo mci = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mci.mode = WGPUCallbackMode_AllowProcessEvents;
    mci.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
        *static_cast<bool*>(u1) = status == WGPUMapAsyncStatus_Success;
    };
    mci.userdata1 = &ok;
    wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, pitch * height, mci);
    /* Same reason as poll_until: WaitAny does not work in this wgpu-native, so
     * the map completion is awaited by driving ProcessEvents. */
    poll_until(ok);
    if (ok) wgpuDevicePoll(g.device, true, nullptr);
    if (!ok) { std::fprintf(stderr, "wgpu: readback: map failed\n"); wgpuBufferRelease(rb); return false; }
    const uint8_t* p = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(rb, 0, pitch * height));
    if (!p) { std::fprintf(stderr, "wgpu: readback: no mapped range\n");
        wgpuBufferRelease(rb);
        return false;
    }
    out.resize(size_t(width) * height * 4);
    for (uint32_t y = 0; y < height; y++) std::memcpy(&out[size_t(y) * width * 4], p + y * pitch, size_t(width) * 4);
    {
        size_t nonblack = 0;
        for (size_t i = 0; i < size_t(width) * height; i++) {
            if (out[i * 4] || out[i * 4 + 1] || out[i * 4 + 2]) nonblack++;
        }
        std::fprintf(stderr, "wgpu: readback %ux%u nonblack %zu/%zu (%.2f%%)\n", width, height, nonblack,
                     size_t(width) * height, 100.0 * double(nonblack) / double(size_t(width) * height));
    }
    wgpuBufferUnmap(rb);
    wgpuBufferRelease(rb);
    release_frame_buffers();
    return true;
}

}  // namespace

/* Submit the recorded frame and present it.
 *
 * On the web the colour view IS the canvas texture, so finishing the encoder and
 * submitting is the whole of "present": the browser composites the canvas at the
 * end of the task. The texture itself is re-imported at the next make_targets,
 * because a canvas texture does not survive a presentation.
 *
 * Nothing is read back: a readback would stall the renderer on the GPU once per
 * frame, and the capture harness reads the canvas through its own hook instead. */
void end_frame() {
    if (!on_owner_thread("end_frame")) return;
    if (!g.ready) return;
    wgpuDevicePushErrorScope(g.device, WGPUErrorFilter_Validation);
    const uint32_t dbg = g_debug_mode.load();
    end_pass();

    /* THE C++ NEVER TOUCHES THE CANVAS.
     *
     * It renders into its OWN persistent texture and exports that texture's id. The
     * PAGE presents it: it reads the JS texture with WebGPU.getJsObject(id) and copies
     * it into canvasCtx.getCurrentTexture().
     *
     * Why the previous design could not work: the C++ imported the canvas texture
     * through the port's INTERNAL allocator (_emwgpuCreateTexture + jsObjectInsert).
     * The import produced a handle WebGPU accepted, the pass recorded into it, the
     * submit reported no error -- and the canvas stayed black, because the handle was
     * not the object the presentation reads. Measured: 30 renders straight into the
     * canvas texture, 0 validation errors, every PNG srgba(0,0,0,0), while a
     * page-owned clear into the SAME canvas produced srgb(255,0,255) immediately. */
    /* ALWAYS go through make_targets_ok: it is what publishes the texture id the
     * page presents with. Guarding it with "if (!g.offscreen)" meant the id stayed 0
     * for the whole run -- measured: "frame 1 done, texture id 0x00000000" on every
     * frame, and the page's presentation block never ran because it skips a zero id. */
    if (!make_targets_ok(g.width, g.height)) {
        wgpuDevicePopErrorScope(g.device, WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT);
        return;
    }

    if (!g.encoder) g.encoder = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    if (!g.encoder) {
        wgpuDevicePopErrorScope(g.device, WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT);
        return;
    }

    if (dbg >= 1 && dbg != 3) {
        /* MODE 1, 2, 4..6: our own pass, opened and closed here.
         *
         * MODE 3 IS DELIBERATELY EXCLUDED: its draws are the GAME's, they go through
         * draw() -> begin_pass(), and begin_pass returns immediately when g.pass is
         * already set. Opening a pass here therefore recorded the clear and left the
         * game's draws with nowhere to go -- measured: ?dbg=3 showed the magenta clear
         * and no red at all. Mode 3's clear comes from begin_pass() instead. */
        WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        ca.view = g.offscreen_view;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {1.0, 0.0, 1.0, 1.0};
        WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;
        WGPURenderPassEncoder cp = wgpuCommandEncoderBeginRenderPass(g.encoder, &rp);
        if (cp) {
            /* Mode 3 is the GAME's triangle, drawn by the game's own draw() path with
             * our fixed-red fragment shader substituted. It must not use the diag
             * triangle, or its red would be step 2's red and would prove nothing. */
            if ((dbg >= 2 && dbg <= 6) && dbg != 3) {
                WGPURenderPipeline dp = diag_pipeline(dbg >= 4, false, dbg >= 6);
                if (dp) {
                    wgpuRenderPassEncoderSetPipeline(cp, dp);
                    wgpuRenderPassEncoderDraw(cp, 3, 1, 0, 0);
                    wgpuRenderPipelineRelease(dp);
                }
            }
            wgpuRenderPassEncoderEnd(cp);
            wgpuRenderPassEncoderRelease(cp);
            static uint32_t ncl = 0;
            if (ncl++ < 200) std::fprintf(stderr, "wgpu: diag mode %u drew into OUR texture\n", dbg);
        }
    }

    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(g.encoder, nullptr);
    wgpuCommandEncoderRelease(g.encoder);
    g.encoder = nullptr;
    if (cb) {
        wgpuQueueSubmit(g.queue, 1, &cb);
        wgpuCommandBufferRelease(cb);
        g_frames_done.fetch_add(1, std::memory_order_release);
        /* Resolve the texture to its JS object HERE, on the frame path, so the call
         * is reachable and the wasm actually imports the bridge entry. A declaration
         * alone does not create an import: Emscripten tree-shakes a library function
         * nothing calls, which is why t1_get_js_texture was absent from game.js on the
         * first attempt. */
        resolve_texture_for_page();
        static std::atomic<uint32_t> np{0};
        if (np.fetch_add(1) < 200)
            std::fprintf(stderr, "wgpu: frame %u done, texture id 0x%08x\n",
                         np.load(), g_exported_texture_id.load());
    }
    release_frame_buffers();
    {
        struct ScopeOut { std::atomic<uint32_t>* count; char* msg; size_t cap; };
        static char scope_msg[512] = {0};
        static ScopeOut out{&g_scope_errors, scope_msg, sizeof scope_msg};
        WGPUPopErrorScopeCallbackInfo pci = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        pci.mode = WGPUCallbackMode_AllowProcessEvents;
        pci.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView msg, void* u1, void*) {
            auto* o = static_cast<ScopeOut*>(u1);
            if (status == WGPUPopErrorScopeStatus_Success && type != WGPUErrorType_NoError) {
                const uint32_t n = o->count->fetch_add(1);
                if (n < 5 && msg.data) {
                    const size_t len = msg.length < (o->cap - 1) ? msg.length : (o->cap - 1);
                    std::memcpy(o->msg, msg.data, len);
                    o->msg[len] = 0;
                    std::fprintf(stderr, "wgpu: FRAME VALIDATION ERROR: %s\n", o->msg);
                }
            }
        };
        pci.userdata1 = &out;
        wgpuDevicePopErrorScope(g.device, pci);
        wgpuDevicePoll(g.device, false, nullptr);
    }
}

/* T1 -- capture the CURRENT TARGET into a buffer the PAGE can read.
 *
 * capture_ppm() writes through std::fopen into MEMFS, which the page cannot fetch.
 * This leaves the RGBA bytes in a static buffer and returns its heap address.
 *
 * THE COPY IS DEFERRED TO THE FRAME BOUNDARY, and that is load-bearing. Measured:
 * calling it from the page returned 0 every time, because end_frame() had already
 * finished the encoder and released the canvas view -- there was nothing left to
 * copy. The request is therefore recorded here and performed by end_frame(), which
 * is the last moment the target texture is still valid. */
/* Ask for a capture at the next frame boundary. Returns 1 when a previous request
 * has produced pixels. */
/* Take the capture NOW, synchronously, from the persistent texture. Called by the
 * replay immediately after end_frame(), on the main thread, outside the render
 * callback -- which is where an async map can actually complete. */



bool capture_ppm(const std::string& path, uint32_t, uint32_t) {
    if (!g.ready) return false;
    std::fprintf(stderr, "wgpu: capture (draws %llu, pipelines %llu, textures %llu, errors %d, encoder %d, pass %d)\n",
                 (unsigned long long)s.draws, (unsigned long long)s.pipelines, (unsigned long long)s.textures,
                 g.errors, g.encoder != nullptr, g.pass != nullptr);
    if (!g.first_error[0] == false && g.first_error[0]) std::fprintf(stderr, "wgpu: last error: %s\n", g.first_error);
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!readback(rgba, w, h)) return false;
    /* The target is BGRA8Unorm (the RSX surface format), so the channels come
     * back swapped relative to the RGB order a PPM stores. */
    const bool bgra = g.color_format == WGPUTextureFormat_BGRA8Unorm;
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for (size_t i = 0; i < size_t(w) * h; i++) {
        const uint8_t* px = &rgba[i * 4];
        rgb[i * 3 + 0] = bgra ? px[2] : px[0];
        rgb[i * 3 + 1] = px[1];
        rgb[i * 3 + 2] = bgra ? px[0] : px[2];
    }
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", w, h);
    std::fwrite(rgb.data(), 1, rgb.size(), f);
    std::fclose(f);
    return true;
}

/* T1 -- the bisection mode and the per-frame validation error count. */
/* Resolve the exported texture to its JS object through the bridge, so the page can
 * copy it. Returns 1 on success; the object is left in Module.t1JsTexture. This is
 * what makes the wasm IMPORT the bridge entry -- a library function nothing calls is
 * tree-shaken out of game.js. */
uint32_t resolve_texture_for_page() {
    const uint32_t id = g_exported_texture_id.load(std::memory_order_acquire);
    if (!id) return 0;
    return t1_get_js_texture(id) ? 1u : 0u;
}

uint32_t texture_id() { return g_exported_texture_id.load(std::memory_order_acquire); }
uint64_t frames_done() { return g_frames_done.load(std::memory_order_acquire); }

void set_debug_mode(uint32_t mode) {
    g_debug_mode.store(mode, std::memory_order_release);
    std::fprintf(stderr, "wgpu: debug mode set to %u\n", mode);
}

uint32_t frame_error_count() { return g_scope_errors.load(std::memory_order_relaxed); }

void shutdown() {
    if (!g.ready) return;
    report_at_exit();
    end_pass();
    release_frame_buffers();
    for (auto& kv : g.pipelines) wgpuRenderPipelineRelease(kv.second);
    g.pipelines.clear();
    for (auto& kv : g.vs_modules) wgpuShaderModuleRelease(kv.second);
    g.vs_modules.clear();
    for (auto& kv : g.fs_modules) wgpuShaderModuleRelease(kv.second);
    g.fs_modules.clear();
    if (g.color_view) wgpuTextureViewRelease(g.color_view);
    if (g.color) wgpuTextureRelease(g.color);
    if (g.depth_view) wgpuTextureViewRelease(g.depth_view);
    if (g.depth) wgpuTextureRelease(g.depth);
    g.color = nullptr;
    g.depth = nullptr;
    g.color_view = nullptr;
    g.depth_view = nullptr;
    g.ready = false;
}

Stats stats() {
    Stats out;
    out.draws = s.draws;
    out.pipelines = s.pipelines;
    out.textures = s.textures;
    out.upload_bytes = s.upload_bytes;
    out.skipped = s.skipped;
    out.first_error = s.first_error;
    return out;
}

}  // namespace wgpu
}  // namespace gpu
