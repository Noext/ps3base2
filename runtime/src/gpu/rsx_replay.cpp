/* T1 -- THE REPLAY: the ring consumer, on the page's MAIN thread.
 *
 * THE ONE RULE THIS FILE EXISTS TO ENFORCE
 * Nothing here may run on a pthread. Measured on this tree: with the renderer
 * issuing wgpu::draw from the RSX pthread the FIFO froze at n_method=378
 * (busy 99.8%, methods 0.1%); with those calls silenced the same build reached
 * 42 379 draws and 503 327 methods (methods 93.4%, 119k/s). Every entry of the
 * emdawnwebgpu port is __proxy:'sync', so each WebGPU call from a pthread is a
 * blocking round trip to the realm that owns the device. A frame rate cannot be
 * built out of those.
 *
 * So the RSX thread only RECORDS (runtime/src/gpu/rsx_ring.cpp) and this file
 * REPLAYS, from the thread the browser gave us. Every entry point asserts it.
 *
 * WHAT IT CONSUMES
 * runtime/include/gpu/rsx_ring.h defines eight record kinds. This consumer keeps a
 * store keyed by CONTENT FINGERPRINT, because the producer sends each object once
 * and then names it (kRef):
 *
 *   kStateBatch  method/arg pairs -> a shadow copy of the RSX register file
 *   kBuffer      vertex/index/inline bytes -> store by hash
 *   kTexture     source bytes -> decode -> store by hash (RGBA8 + layout)
 *   kShader      ucode words -> emit WGSL -> store by hash
 *   kDraw        fingerprints + primitive -> assemble DrawInputs -> wgpu::draw
 *   kClear       mask/colour -> wgpu::clear
 *   kFlip        -> wgpu::end_frame()
 *   kRef         an object already in the store, by fingerprint
 *
 * It never blocks: pump() pops what is available and returns. The page calls it
 * once per animation frame.
 */
#include "gpu/rsx_replay.h"

#include "gpu/rsx_ring.h"
#include "gpu/wgpu_backend.h"
#include "gpu/texture_decode.h"
#include "runtime/log.h"
#include "runtime/wgsl_emit.h"
#include "fp_decompiler.h"

#include <atomic>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#include <emscripten/emscripten.h>
#endif

/* The decode seam, defined beside the tables in rsx_renderer.cpp so there is ONE
 * definition of each conversion. Retyping them here would drift silently. */
namespace gpu::rsx {
bool t1_decode_vertices(uint32_t type, uint32_t size, const uint8_t* src, uint32_t stride,
                        uint32_t count, float* out);
uint32_t t1_attribute_bytes(uint32_t type, uint32_t size);
uint64_t t1_decode_texture(const uint8_t* src, const gpu::TextureRegs& regs,
                           const gpu::TextureLayout& layout, uint8_t* dst);
}

namespace gpu {
namespace replay {

namespace {

std::atomic<uint32_t> g_owner_tid{0};
std::atomic<uint64_t> g_off_thread_calls{0};
bool g_device_ready = false;
uint32_t g_target_width = 1280;
uint32_t g_target_height = 720;

uint32_t this_thread_id() {
#if defined(__EMSCRIPTEN__)
    return static_cast<uint32_t>(emscripten_main_browser_thread_id());
#else
    return 0;
#endif
}

bool on_owner_thread() {
    const uint32_t owner = g_owner_tid.load(std::memory_order_acquire);
    return owner != 0 && this_thread_id() == owner;
}

/* Guard for every entry point that touches the device. A mismatch is not
 * recoverable -- it means the architecture was violated -- so it is counted and
 * the call is REFUSED rather than performed on the wrong realm, where it would
 * block or return undefined and be very hard to attribute. */
bool enter(const char* who) {
    if (!g_device_ready) return false;
    if (!on_owner_thread()) {
        const uint64_t n = g_off_thread_calls.fetch_add(1, std::memory_order_relaxed);
        if (n < 5) {
            LOG_ERROR("replay",
                      "%s called OFF the main thread (tid %u, owner %u) -- REFUSED; WebGPU must "
                      "never be called from a pthread",
                      who, this_thread_id(), g_owner_tid.load());
        }
        return false;
    }
    return true;
}

Stats g_stats{};

struct StoredBuffer {
    std::vector<uint8_t> bytes;
    uint32_t stride = 0;
    uint32_t type = 0;
};

struct StoredTexture {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t levels = 1;
    uint32_t out_bytes = 4;
    int vk_format = 0;
    bool complete = false;
    uint32_t regs_format = 0;
    struct Mip { uint32_t level; uint64_t offset; uint32_t width; uint32_t height; };
    std::vector<Mip> mips;
};

struct StoredShader {
    std::string wgsl;
    uint32_t stage = 0;
    uint32_t input_mask = 0;
    uint32_t texture_mask = 0;
    bool valid = false;
    /* The ucode, kept so the WGSL can be emitted AT DRAW TIME.
     *
     * The fragment program's key is derived from the TEXTURE registers (control,
     * texture_dims, unnormalized, shadow), and those arrive in state batches that
     * may follow the shader record. Emitting at record time therefore used a key
     * built from registers that were still zero -- measured: "fragment shader
     * 0x4ff2819e: emitter refused (128 words)" for every program, so every draw
     * reported no_shader and draws_ready stayed 0. */
    std::vector<uint32_t> ucode;
    uint32_t start_slot = 0;
    bool emitted = false;
};

std::map<uint32_t, StoredBuffer> g_buffers;
std::map<uint32_t, StoredTexture> g_textures;
std::map<uint32_t, StoredShader> g_shaders;

constexpr uint32_t kRegWords = 0x2000 / 4;
uint32_t g_regs[kRegWords] = {};
float g_vp_constants[468 * 4] = {};

inline uint32_t R(uint32_t reg) {
    const uint32_t i = reg >> 2;
    return i < kRegWords ? g_regs[i] : 0u;
}
inline void setR(uint32_t reg, uint32_t v) {
    const uint32_t i = reg >> 2;
    if (i < kRegWords) g_regs[i] = v;
}

enum : uint32_t {
    TRANSFORM_CONSTANT_LOAD = 0x1EFC,
    TRANSFORM_CONSTANT = 0x1F00,
    TEXTURE_BASE = 0x1A00,
    SHADER_CONTROL = 0x1D60,
    TRANSFORM_PROGRAM_START = 0x1EA0,
};

struct PendingTexture {
    std::vector<uint8_t> src;
    uint32_t expected = 0;
    uint32_t seen = 0;
    rsxring::TextureRecord hdr{};
};
std::map<uint32_t, PendingTexture> g_pending_tex;

/* ---------------------------------------------------------- shader emission */

void store_vertex_shader(const rsxring::ShaderRecord& hdr, const uint8_t* payload) {
    /* hdr.words is a BYTE count: the producer sends 1024 and copies sizeof(g_vp).
     * Treating it as words read four times past the payload. */
    const uint32_t bytes = hdr.words;
    if (!bytes || bytes > 1024) return;
    const uint32_t words = bytes / 4;
    StoredShader& s = g_shaders[hdr.hash];
    s.stage = 0;
    s.ucode.assign(words, 0);
    std::memcpy(s.ucode.data(), payload, bytes);
    s.start_slot = R(TRANSFORM_PROGRAM_START);
    s.emitted = false;
    g_stats.shaders++;
}

void store_fragment_shader(const rsxring::ShaderRecord& hdr, const uint8_t* payload) {
    /* BYTE count, same as the vertex path. */
    const uint32_t bytes = hdr.words;
    if (!bytes || bytes > 512) return;
    const uint32_t words = bytes / 4;
    StoredShader& s = g_shaders[hdr.hash];
    s.stage = 1;
    s.ucode.assign(words, 0);
    std::memcpy(s.ucode.data(), payload, bytes);
    s.emitted = false;
    g_stats.shaders++;
}

/* --------------------------------------------------------- emit at draw time
 *
 * The WGSL is emitted HERE, not when the record arrived, because the fragment
 * program's key is derived from the TEXTURE registers and those arrive in state
 * batches that may follow the shader record. Emitting at record time used a key
 * built from registers still zero, and every fragment program was refused.
 *
 * The result is cached on the entry: the key only changes when the state does, and
 * a program is re-emitted at most once per distinct key. */
bool emit_shader(StoredShader& s) {
    if (s.emitted) return s.valid;
    s.emitted = true;
    if (s.ucode.empty()) { s.valid = false; return false; }
    if (s.stage == 0) {
        WgslShader w = emit_vertex_wgsl(s.ucode.data(), s.start_slot);
        s.valid = w.valid;
        s.input_mask = w.input_mask;
        s.texture_mask = w.texture_mask;
        if (w.valid) s.wgsl = std::move(w.source);
    } else {
        FragmentProgramKey key;
        key.control = R(SHADER_CONTROL) & 0xCE;
        uint32_t dims = 0;
        for (uint32_t u = 0; u < 16; u++) {
            const uint32_t f = R(TEXTURE_BASE + 4 + u * 0x20);
            const uint32_t dim = (f >> 4) & 0xF;
            const uint32_t v = dim == 1 ? 0 : dim == 3 ? 3 : (f & 4) ? 2 : 1;
            dims |= v << (u * 2);
        }
        key.texture_dims = dims;
        /* THE HALFWORD SWAP, which is what made every fragment program unreadable.
         *
         * rsx_renderer.cpp:1104 builds its reader as
         *     uint32_t v = vm::read32(ea + i * 4);
         *     return (v << 16) | (v >> 16);
         * -- the guest stores each instruction as two big-endian halfwords, and the
         * analyser expects them swapped. Feeding the raw words made layout() return
         * invalid, so analyse_fragment_program refused every program: measured
         * no_shader=1715 against draws_ready=1102, and the emitter logged
         * "emitter refused (128 words)" for all of them. */
        const std::vector<uint32_t>& ucode = s.ucode;
        FpWordReader read = [&ucode](uint32_t i) {
            const uint32_t v = i < ucode.size() ? ucode[i] : 0u;
            return (v << 16) | (v >> 16);
        };
        WgslShader w = emit_fragment_wgsl(read, key);
        s.valid = w.valid;
        s.texture_mask = w.texture_mask;
        if (w.valid) s.wgsl = std::move(w.source);
    }
    if (!s.valid && g_stats.shaders <= 12) {
        /* Name what the analyser saw: an invalid layout means the ucode words are
         * not a fragment program, which is a different bug from an emitter refusal. */
        const uint32_t w0 = s.ucode.empty() ? 0 : s.ucode[0];
        const uint32_t w1 = s.ucode.size() > 1 ? s.ucode[1] : 0;
        LOG_WARN("replay", "shader stage %u: ucode %zu words, w0 %08x w1 %08x (emitter refused)",
                 s.stage, s.ucode.size(), w0, w1);
    }
    if (!s.valid && g_stats.shaders <= 12)
        LOG_WARN("replay", "shader 0x%08x stage %u: emitter refused (%zu words)",
                 uint32_t(s.ucode.size()), s.stage, s.ucode.size());
    return s.valid;
}

/* --------------------------------------------------------- texture decoding */

void finish_texture(uint32_t hash, PendingTexture& p) {
    TextureRegs regs;
    regs.offset = p.hdr.ea;
    regs.format = p.hdr.format;
    regs.rect = (p.hdr.width << 16) | (p.hdr.height & 0xFFFF);
    regs.control3 = p.hdr.pitch;
    TextureLayout layout;
    if (!describe_texture(regs, layout)) {
        g_stats.textures_refused++;
        return;
    }
    StoredTexture& t = g_textures[hash];
    t.width = layout.width;
    t.height = layout.height;
    t.levels = layout.levels;
    t.out_bytes = layout.out_bytes;
    t.vk_format = int(layout.format);
    t.rgba.assign(size_t(layout.decoded_size), 0);
    if (!rsx::t1_decode_texture(p.src.data(), regs, layout, t.rgba.data())) {
        g_stats.textures_refused++;
        return;
    }
    t.mips.clear();
    for (const TextureRegion& r : layout.regions) {
        t.mips.push_back({r.level, r.offset, r.width, r.height});
    }
    t.regs_format = p.hdr.format;
    t.complete = true;
    g_stats.textures++;
    g_stats.texture_bytes += layout.decoded_size;
}

}  // namespace

/* ============================================================== public API */

bool adopt_device(void* wgpu_device, int color_format, uint32_t width, uint32_t height) {
    g_owner_tid.store(this_thread_id(), std::memory_order_release);
    if (!wgpu_device) {
        LOG_ERROR("replay", "adopt_device: null device");
        return false;
    }
    /* Tell the BACKEND which thread owns the device, before init(). Every backend
     * entry that would touch the device checks this and refuses a call from any
     * other thread -- the enforcement of the one rule this architecture rests on. */
    wgpu::set_owner_thread(this_thread_id());
    if (!wgpu::init(width, height)) {
        LOG_ERROR("replay", "adopt_device: backend init refused");
        return false;
    }
    (void)color_format;
    g_target_width = width;
    g_target_height = height;
    g_device_ready = true;
    LOG_INFO("replay", "device adopted on tid %u, target %ux%u", this_thread_id(), width, height);
    return true;
}

void pump() {
    if (!enter("pump")) return;
    g_stats.frames++;

    /* A TIME BUDGET, not a record count.
     *
     * A count cannot hold a frame rate: the cost of one record varies by two orders
     * of magnitude (a state batch is a memcpy, a texture is a full decode), so 1 500
     * records could be 1 ms or 40 ms. Measured with a count budget: the ring sat at
     * 100% (peak 16 776 688 / 16 777 216) and popped never caught up with recs.
     *
     * 8 ms per animation frame leaves the browser the rest of a 16.7 ms frame to
     * paint, answer CDP and run the capture. Whatever does not fit stays in the ring
     * for the next frame; the loop stops on the CLOCK, not on a counter. */
    const double deadline = emscripten_get_now() + 8.0;
    for (;;) {
        if (emscripten_get_now() >= deadline) break;
        const uint32_t kind = rsxring::poll();
        if (!kind) break;
        const uint8_t* payload = rsxring::poll_bytes();
        const uint32_t size = rsxring::poll_size();
        g_stats.popped++;
        if (kind < 9) g_stats.by_kind[kind]++;
        g_stats.bytes += size;

        switch (kind) {
        case rsxring::kStateBatch: {
            const uint32_t n = size / sizeof(rsxring::StateWord);
            const rsxring::StateWord* w = reinterpret_cast<const rsxring::StateWord*>(payload);
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t method = w[i].method, arg = w[i].arg;
                setR(method, arg);
                if (method >= TRANSFORM_CONSTANT && method < TRANSFORM_CONSTANT + 128) {
                    const uint32_t idx = R(TRANSFORM_CONSTANT_LOAD) * 4 + ((method - TRANSFORM_CONSTANT) >> 2);
                    if (idx < 468 * 4) std::memcpy(&g_vp_constants[idx], &arg, 4);
                }
                g_stats.state_words++;
            }
            break;
        }
        case rsxring::kBuffer: {
            if (size < sizeof(rsxring::BufferRecord)) break;
            rsxring::BufferRecord hdr;
            std::memcpy(&hdr, payload, sizeof hdr);
            const uint8_t* bytes = payload + sizeof hdr;
            const uint32_t len = size - uint32_t(sizeof hdr);
            StoredBuffer& b = g_buffers[hdr.hash];
            b.bytes.assign(bytes, bytes + len);
            b.stride = hdr.stride;
            b.type = hdr.type;
            g_stats.buffers++;
            break;
        }
        case rsxring::kTexture: {
            if (size < sizeof(rsxring::TextureRecord)) break;
            rsxring::TextureRecord hdr;
            std::memcpy(&hdr, payload, sizeof hdr);
            const uint8_t* bytes = payload + sizeof hdr;
            const uint32_t len = size - uint32_t(sizeof hdr);
            PendingTexture& p = g_pending_tex[hdr.hash];
            if (p.expected == 0) {
                p.expected = hdr.src_total;
                p.hdr = hdr;
                p.src.resize(hdr.src_total);
            }
            const uint64_t off = uint64_t(hdr.chunk) * hdr.src_size;
            if (off + len <= p.src.size()) {
                std::memcpy(p.src.data() + off, bytes, len);
                p.seen += len;
            }
            if (p.seen >= p.expected) {
                finish_texture(hdr.hash, p);
                g_pending_tex.erase(hdr.hash);
            }
            break;
        }
        case rsxring::kShader: {
            if (size < sizeof(rsxring::ShaderRecord)) break;
            rsxring::ShaderRecord hdr;
            std::memcpy(&hdr, payload, sizeof hdr);
            const uint8_t* words = payload + sizeof hdr;
            if (hdr.stage == 0) store_vertex_shader(hdr, words);
            else store_fragment_shader(hdr, words);
            break;
        }
        case rsxring::kRef: {
            if (size < sizeof(rsxring::RefRecord)) break;
            g_stats.refs++;
            break;
        }
        case rsxring::kClear: {
            if (size < sizeof(rsxring::ClearRecord)) break;
            rsxring::ClearRecord c;
            std::memcpy(&c, payload, sizeof c);
            const uint32_t rgba = c.color;
            const float col[4] = {
                float(rgba & 0xFF) / 255.0f, float((rgba >> 8) & 0xFF) / 255.0f,
                float((rgba >> 16) & 0xFF) / 255.0f, float((rgba >> 24) & 0xFF) / 255.0f};
            const float depth = float(c.zstencil & 0xFFFFFF) / float(0xFFFFFF);
            wgpu::clear(c.mask, col, depth, uint32_t(c.zstencil >> 24));
            g_stats.clears++;
            break;
        }
        case rsxring::kFlip: {
            /* T1 DIAGNOSTIC: once, after a few frames, read the backend's OWN target
             * back and report how many pixels are non-black. If this is high the
             * rendering works and only the canvas presentation is broken; if it is 0
             * the draws themselves produce nothing. */
            if (g_stats.flips == 30) {
                std::vector<uint8_t> probe;
                uint32_t pw = 0, ph = 0;
                if (wgpu::capture_ppm("/t1probe.ppm", 0, 0)) {
                    LOG_INFO("replay", "target readback succeeded at flip 30");
                } else {
                    LOG_WARN("replay", "target readback FAILED at flip 30");
                }
                (void)probe; (void)pw; (void)ph;
            }
            /* The renderer flipped 1081 times in a measured run while this counter
             * stayed 0, so the flip path is instrumented: a record that arrives and
             * is refused must not look like a record that never arrived. */
            if (g_stats.flips < 3)
                LOG_INFO("replay", "kFlip arrived (record %llu)", (unsigned long long)g_stats.flips);
            wgpu::end_frame();
            g_stats.flips++;

            break;
        }
        case rsxring::kDraw: {
            if (size < sizeof(rsxring::DrawRecord)) break;
            rsxring::DrawRecord d;
            std::memcpy(&d, payload, sizeof d);
            g_stats.draws_seen++;
            /* T1 DIAGNOSTIC: the record's own numbers. Measured draws of
             * DrawIndexed(0, 1, 0, 0, 0) with all-zero vertices mean the record
             * carries a count of 1 and no vertex data; this names which field is
             * empty instead of guessing. */
            if (g_stats.draws_seen <= 8) {
                LOG_INFO("replay", "kDraw prim %u first %u count %u indexed %u mask 0x%x idx_hash %08x vp %08x fp %08x",
                         d.primitive, d.first, d.count, d.indexed, d.array_mask,
                         d.index_hash, d.shader_hash[0], d.shader_hash[1]);
                for (uint32_t i = 0; i < 4; i++)
                    if (d.array_mask & (1u << i))
                        LOG_INFO("replay", "  array %u fmt 0x%08x stride %u count %u len %u hash %08x",
                                 i, d.array_format[i], d.array_stride[i], d.array_count[i],
                                 d.array_len[i], d.array_hash[i]);
            }

            wgpu::DrawInputs in;
            auto vit = g_shaders.find(d.shader_hash[0]);
            auto fit = g_shaders.find(d.shader_hash[1]);
            /* Emit now, with the register state this draw sees. */
            if (vit != g_shaders.end()) emit_shader(vit->second);
            if (fit != g_shaders.end()) emit_shader(fit->second);
            if (vit == g_shaders.end() || fit == g_shaders.end() || !vit->second.valid || !fit->second.valid) {
                g_stats.draws_no_shader++;
                if (g_stats.draws_no_shader <= 6)
                    LOG_WARN("replay", "draw without programs: vp 0x%08x %s fp 0x%08x %s (store has %zu)",
                             d.shader_hash[0], vit == g_shaders.end() ? "MISSING" : (vit->second.valid ? "ok" : "INVALID"),
                             d.shader_hash[1], fit == g_shaders.end() ? "MISSING" : (fit->second.valid ? "ok" : "INVALID"),
                             g_shaders.size());
                break;
            }
            in.vs_wgsl = vit->second.wgsl;
            in.fs_wgsl = fit->second.wgsl;
            in.vp_hash = d.shader_hash[0];
            in.fp_hash = d.shader_hash[1];
            in.vertex_inputs = vit->second.input_mask;

            std::vector<std::vector<float>> decoded(16);
            uint32_t vcount = 0;
            for (uint32_t i = 0; i < 16; i++) {
                if (!(d.array_mask & (1u << i))) continue;
                auto bit = g_buffers.find(d.array_hash[i]);
                if (bit == g_buffers.end()) { g_stats.draws_no_vertex++; continue; }
                const StoredBuffer& b = bit->second;
                const uint32_t fmt = d.array_format[i];
                const uint32_t type = fmt & 7u;
                const uint32_t asize = (fmt >> 4) & 0xFu;
                const uint32_t stride = d.array_stride[i] ? d.array_stride[i] : b.stride;
                uint32_t n = d.array_count[i];
                if (!n && stride) n = uint32_t(b.bytes.size() / stride);
                if (!n || !asize) continue;
                decoded[i].assign(size_t(n) * 4, 0.0f);
                if (!rsx::t1_decode_vertices(type, asize, b.bytes.data(), stride, n, decoded[i].data())) continue;
                in.vertex[i] = decoded[i].data();
                if (n > vcount) vcount = n;
            }
            /* THE INLINE VERTEX BLOCK, which is bit 31 of array_mask.
             *
             * An immediate-mode draw carries its vertices inline: the producer sets
             * array_mask bit 31 and puts the block's fingerprint in array_hash[0]
             * (rsx_ring.cpp:519-521). The loop above only walks slots 0..15, so this
             * block was never decoded and the draw ran with the slot-0 array instead --
             * measured: the full-screen triangle arrived as three vertices of
             * (0,0,0,1), zero area, nothing rasterised, no error.
             *
             * The inline bytes are raw float4s already (the producer copies the guest's
             * inline words verbatim), so they need no attribute decode. */
            if (d.array_mask & 0x80000000u) {
                auto nit = g_buffers.find(d.array_hash[0]);
                if (nit != g_buffers.end() && nit->second.bytes.size() >= 16) {
                    const size_t n = nit->second.bytes.size() / 16;
                    decoded[0].assign(n * 4, 0.0f);
                    std::memcpy(decoded[0].data(), nit->second.bytes.data(), n * 16);
                    in.vertex[0] = decoded[0].data();
                    if (n > vcount) vcount = uint32_t(n);
                    if (g_stats.draws_seen <= 6)
                        LOG_INFO("replay", "  inline block: %zu vertices, v0 %.3f %.3f %.3f %.3f",
                                 n, decoded[0][0], decoded[0][1], decoded[0][2], decoded[0][3]);
                } else if (g_stats.draws_seen <= 6) {
                    LOG_WARN("replay", "  inline block MISSING for hash %08x", d.array_hash[0]);
                }
            }
            in.vertex_count = vcount;
            /* T1 DIAGNOSTIC: the decoded vertices and the index count, so a
             * degenerate draw names its own cause. Measured: DrawIndexed(0, 1, ...)
             * with all-zero vertices, which is either an empty index block or an
             * empty vertex stream -- and the two need different fixes. */
            if (g_stats.draws_seen <= 6) {
                LOG_INFO("replay", "  assembled: vcount %u index_count %u mask 0x%x",
                         vcount, in.index_count, in.texture_mask);
                for (uint32_t i = 0; i < 2; i++) {
                    if (!in.vertex[i] || !vcount) continue;
                    const float* v = in.vertex[i];
                    LOG_INFO("replay", "    v[%u][0] %.3f %.3f %.3f %.3f  v[%u][1] %.3f %.3f %.3f %.3f",
                             i, v[0], v[1], v[2], v[3], i, v[4], v[5], v[6], v[7]);
                }
            }

            std::vector<uint32_t> indices;
            if (d.indexed && d.index_hash) {
                auto iit = g_buffers.find(d.index_hash);
                if (g_stats.draws_seen <= 6)
                    LOG_INFO("replay", "  index lookup %08x -> %s (%zu buffers, size %zu)",
                             d.index_hash, iit == g_buffers.end() ? "MISSING" : "found",
                             g_buffers.size(), iit == g_buffers.end() ? size_t(0) : iit->second.bytes.size());
                if (iit != g_buffers.end() && iit->second.bytes.size() >= 2) {
                    const size_t n = iit->second.bytes.size() / 2;
                    indices.resize(n);
                    const uint8_t* p = iit->second.bytes.data();
                    for (size_t k = 0; k < n; k++) indices[k] = uint32_t(p[k * 2]) | (uint32_t(p[k * 2 + 1]) << 8);
                    in.indices = indices.data();
                    in.index_count = uint32_t(n);
                }
            }

            in.uniform[0] = g_vp_constants;
            in.uniform_size[0] = sizeof g_vp_constants;

            for (uint32_t u = 0; u < 16; u++) {
                const uint32_t b = TEXTURE_BASE + u * 0x20;
                const uint32_t fmt = R(b + 0x04);
                if (!fmt) continue;
                for (auto& kv : g_textures) {
                    const StoredTexture& t = kv.second;
                    if (!t.complete || t.regs_format != fmt) continue;
                    in.texture[u].pixels = t.rgba.data();
                    in.texture[u].width = t.width;
                    in.texture[u].height = t.height;
                    in.texture[u].mip_levels = t.levels;
                    in.texture[u].vk_format = t.vk_format;
                    in.texture[u].bytes_per_texel = t.out_bytes;
                    in.texture[u].mip_count = uint32_t(t.mips.size() < 16 ? t.mips.size() : 16);
                    for (uint32_t m = 0; m < in.texture[u].mip_count; m++) {
                        in.texture[u].mips[m].level = t.mips[m].level;
                        in.texture[u].mips[m].offset = t.mips[m].offset;
                        in.texture[u].mips[m].width = t.mips[m].width;
                        in.texture[u].mips[m].height = t.mips[m].height;
                    }
                    in.texture_mask |= 1u << u;
                    break;
                }
            }

            in.topology = 2;
            in.color_format = uint32_t(wgpu::format_for(23));
            in.target_width = g_target_width;
            in.target_height = g_target_height;
            in.viewport[2] = float(g_target_width);
            in.viewport[3] = float(g_target_height);
            in.scissor[2] = int32_t(g_target_width);
            in.scissor[3] = int32_t(g_target_height);

            if (g_stats.draws_ready <= 6)
                LOG_INFO("replay", "  -> wgpu::draw index_count %u vertex_count %u indices %p",
                         in.index_count, in.vertex_count, (const void*)in.indices);
            wgpu::draw(in);
            g_stats.draws_ready++;
            break;
        }
        default:
            break;
        }
    }

    const uint32_t used = rsxring::control_word(0);
    if (used > g_stats.peak_used) g_stats.peak_used = used;
    g_stats.capacity = rsxring::control_word(4);
}

const Stats& stats() { return g_stats; }

void reset_stats() {
    const uint64_t popped = g_stats.popped;
    g_stats = Stats{};
    g_stats.popped = popped;
}

uint32_t target_width() { return g_target_width; }
uint32_t target_height() { return g_target_height; }

void set_target_size(uint32_t w, uint32_t h) {
    g_target_width = w;
    g_target_height = h;
}

}  // namespace replay
}  // namespace gpu
