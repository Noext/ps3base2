#include "gpu/rsx_ring.h"

#include "runtime/command_ring.h"
#include "runtime/log.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif

namespace gpu {
namespace rsxring {

/* Declared here, at namespace scope, so the tee below can call the same consumer
 * the page does. Definitions are further down. */
uint32_t poll(void);
uint32_t poll_size(void);
const uint8_t* poll_bytes(void);
/* Internal helpers used by on_method()'s periodic report, declared here so the
 * anonymous-namespace definitions below can be reached without a second, shadowing
 * declaration inside it (which made the call ambiguous). */
void tee_drain();
void report_stats();

namespace {

/* 4 MB. Measured state traffic is ~1.27 M methods/s (H7 smoke: methods=89 023 195
 * in 69.6 s); at 8 bytes per method and a 128-word batch that is ~10 MB/s
 * batched, so the ring holds a burst much longer than one page drain interval.
 * A static array so its address is stable: the page views it directly. */
/* 16 MB. Measured with 4 MB: a 4 KB/s-of-state plus ~5 MB/s-of-texture stream
 * overflowed the ring between two drains and 3248 records (639 MB) were refused.
 * The page drains every animation frame, not every N methods, so the size is the
 * margin for a hitch. */
constexpr uint32_t kRingCapacity = 16u << 20;
alignas(64) uint8_t g_ring_mem[kRingCapacity];

/* Payload assembly. A record is pushed as ONE contiguous block, so the variable
 * part (ucode, texture chunk, vertex block) is gathered here first. */
constexpr uint32_t kScratchCapacity = 1u << 20;
alignas(64) uint8_t g_scratch[kScratchCapacity];

/* Consumer-side copy target. Separate from g_scratch on purpose: the producer
 * writes g_scratch while pushing, and the consumer may run on another thread. */
constexpr uint32_t kPollCapacity = 1u << 20;
alignas(64) uint8_t g_poll[kPollCapacity];
uint32_t g_poll_size = 0;

uint8_t* g_ring_base = g_ring_mem;

/* Report switch, OFF by default.
 *
 * Measured: at ~1.27 M methods/s a report every 1024 methods is ~1200 log lines a
 * second. That is right for a headless measurement run and wrong for the page build
 * and for the host test, which both want silence. It is turned on with the tee. */
bool g_reporting = false;

CommandRing& ring() {
    static CommandRing r(g_ring_base, kRingCapacity);
    return r;
}

Hooks g_hooks{};

struct Counters {
    std::atomic<uint64_t> records{0}, bytes{0};
    std::atomic<uint64_t> state_methods{0}, state_bytes{0};
    std::atomic<uint64_t> vtx_bytes{0}, idx_bytes{0};
    std::atomic<uint64_t> tex_bytes{0}, tex_objects{0};
    std::atomic<uint64_t> shader_bytes{0}, shader_objects{0};
    std::atomic<uint64_t> draws{0}, clears{0}, flips{0};
    std::atomic<uint64_t> refused{0}, refused_bytes{0};
    std::atomic<uint64_t> geometry_unmapped{0}, texture_refused{0};
    std::atomic<uint64_t> shader_refused{0};
    std::atomic<uint64_t> tex_unchanged{0};
    /* WHICH THREADS produce. The ownership claim for milestone 4 is that the page
     * thread is the only one that ever touches the WebGPU device and that the
     * emulation worker alone produces RSX records. The second half of that is
     * measurable here: the set of thread ids that entered on_method. A single
     * distinct id is the evidence; two would be a producer the claim does not
     * cover. Counted, not assumed. */
    std::atomic<uint64_t> producer_threads{0};
    std::atomic<uint64_t> producer_thread_id{0};
};
Counters g_c;

/* ---- state the producer tracks for TRANSPORT ------------------------------
 * gcm_fifo executes only the methods that carry synchronisation; the drawing
 * methods are counters there. To attach the right payload to a draw the producer
 * keeps the registers a draw depends on. This is not a second command processor:
 * it never writes guest memory and never interprets a method's meaning beyond
 * "this register is part of the draw's state". */
StateWord g_state[kStateBatchWords];
uint32_t g_state_n = 0;

uint32_t g_array_format[16] = {};
uint32_t g_array_offset[16] = {};
uint32_t g_base_offset = 0;
uint32_t g_primitive = 0;

uint32_t g_clear_color = 0;
uint32_t g_clear_zstencil = 0;

/* [unit][offset fmt address ctrl0 ctrl1 filter rect ctrl3 border] */
uint32_t g_tex_regs[16][9] = {};
uint32_t g_tex_seq = 0;
/* Last pushed texture per unit, by CONTENT key (address, format, rect). A title
 * re-writes the whole unit register file per draw; pushing on every address write
 * measured 1485 uploads of 24.6 MB in 75 s and filled the ring (55 685 refused
 * records, 1.28 GB refused). The page only needs a texture when it CHANGES. */
uint32_t g_tex_key[16] = {};

uint32_t g_vp[1024] = {};          /* vertex program, load-position addressed */
uint32_t g_vp_load = 0;
uint32_t g_vp_hash = 0;

uint32_t g_fp[512] = {};           /* fragment ucode snapshot */
uint32_t g_fp_hash = 0;
uint32_t g_fp_ea = 0;

uint32_t g_index_offset = 0;
bool g_index_16bit = false;

uint32_t g_inline[4096];
uint32_t g_inline_n = 0;
/* An immediate-mode draw is one BEGIN_END...INLINE_ARRAY...BEGIN_END(0) block;
 * the draw record is emitted at the closing zero. A block that used DRAW_ARRAYS
 * instead must NOT emit a second, empty draw at the close -- measured: doing so
 * put a zero-array draw after every real one. */
bool g_primitive_had_draw = false;

uint64_t g_last_report_bytes = 0;

/* TEE consumer.
 *
 * The page is the real consumer, but a page is not available in a Node run, and
 * the Node run is where the guest actually executes. The tee drains the ring in
 * the emulation thread purely to MEASURE the transport end to end (what came out,
 * in what quantities); it is off unless PS3BASE_RSX_TEE=1, so a page build keeps
 * the records for the page. It never changes what is pushed: the same bytes are
 * written whether the tee is on or off. */
struct Tee {
    bool enabled = false;
    uint64_t records = 0, bytes = 0;
    uint64_t state = 0, buffers = 0, textures = 0, shaders = 0;
    uint64_t draws = 0, clears = 0, flips = 0;
    uint64_t vtx = 0, idx = 0, tex_bytes = 0, shader_bytes = 0;
} g_tee;

// (poll()/poll_size()/poll_bytes() are declared in the header and defined below.)

uint32_t fnv1a(const uint32_t* w, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&w[i]);
        for (int b = 0; b < 4; b++) {
            h ^= p[b];
            h *= 16777619u;
        }
    }
    return h;
}

void note_producer_thread() {
    uint64_t id = 0;
#if defined(__EMSCRIPTEN__)
    id = static_cast<uint64_t>(pthread_self());
#endif
    if (!id) id = 1;
    uint64_t expected = 0;
    if (g_c.producer_thread_id.compare_exchange_strong(expected, id, std::memory_order_relaxed)) {
        g_c.producer_threads.store(1, std::memory_order_relaxed);
        return;
    }
    if (expected != id) {
        /* A SECOND producer thread. This is the case the ownership claim excludes,
         * so it is counted rather than silently tolerated. */
        g_c.producer_threads.fetch_add(1, std::memory_order_relaxed);
        g_c.producer_thread_id.store(id, std::memory_order_relaxed);
    }
}

bool push_record(uint32_t kind, const void* payload, uint32_t size) {
    CommandRing& r = ring();
    if (r.push(kind, payload, size)) {
        g_c.records.fetch_add(1, std::memory_order_relaxed);
        g_c.bytes.fetch_add(size, std::memory_order_relaxed);
        return true;
    }
    /* Full, or the record is bigger than the ring. The emulation thread must not
     * wait on the page, so it is counted and dropped, never half-written. */
    g_c.refused.fetch_add(1, std::memory_order_relaxed);
    g_c.refused_bytes.fetch_add(size, std::memory_order_relaxed);
    return false;
}

void flush_state() {
    if (!g_state_n) return;
    const uint32_t bytes = g_state_n * sizeof(StateWord);
    if (push_record(Kind::kStateBatch, g_state, bytes))
        g_c.state_bytes.fetch_add(bytes, std::memory_order_relaxed);
    g_c.state_methods.fetch_add(g_state_n, std::memory_order_relaxed);
    g_state_n = 0;
}

void add_state(uint32_t method, uint32_t arg) {
    if (g_state_n == kStateBatchWords) flush_state();
    g_state[g_state_n].method = method;
    g_state[g_state_n].arg = arg;
    g_state_n++;
}

void push_vertex_shader_if_changed() {
    const uint32_t hash = fnv1a(g_vp, 1024);
    if (hash == g_vp_hash) return;
    g_vp_hash = hash;
    ShaderRecord hdr{};
    hdr.stage = 0;
    hdr.words = 1024;
    hdr.hash = hash;
    hdr.ea = 0;
    std::memcpy(g_scratch, &hdr, sizeof hdr);
    std::memcpy(g_scratch + sizeof hdr, g_vp, sizeof g_vp);
    if (push_record(Kind::kShader, g_scratch, sizeof hdr + sizeof g_vp)) {
        g_c.shader_objects.fetch_add(1, std::memory_order_relaxed);
        g_c.shader_bytes.fetch_add(sizeof g_vp, std::memory_order_relaxed);
    }
}

/* One vertex array: copy stride*count bytes from the guest, or count the miss.
 * Capped so a bogus format cannot ask for the whole ring. */
uint32_t push_vertex_array(uint32_t slot, uint32_t count, DrawRecord& draw) {
    const uint32_t fmt = g_array_format[slot];
    const uint32_t size = (fmt >> 4) & 0xFu;
    const uint32_t stride = (fmt >> 8) & 0xFFu;
    if (!size || !stride || !count) return 0;
    uint32_t len = stride * count;
    if (len > 256u * 1024u) len = 256u * 1024u;   /* one array, one cap */
    const uint32_t io = g_array_offset[slot] + g_base_offset;
    const uint32_t ea = g_hooks.io_to_ea ? g_hooks.io_to_ea(io) : 0;
    const uint8_t* src = (ea && g_hooks.guest_ptr) ? g_hooks.guest_ptr(ea) : nullptr;
    if (!src) {
        const uint64_t n = g_c.geometry_unmapped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n <= 6)
            LOG_INFO("rsxring", "geometry miss: slot=%u io=0x%08x ea=0x%08x fmt=0x%08x stride=%u count=%u",
                     slot, io, ea, fmt, stride, count);
        return 0;
    }
    if (sizeof(BufferRecord) + len > kScratchCapacity) return 0;
    BufferRecord hdr{};
    hdr.type = kVertex;
    hdr.slot = slot;
    hdr.ea = ea;
    hdr.len = len;
    hdr.stride = stride;
    std::memcpy(g_scratch, &hdr, sizeof hdr);
    std::memcpy(g_scratch + sizeof hdr, src, len);
    if (!push_record(Kind::kBuffer, g_scratch, sizeof hdr + len)) return 0;
    g_c.vtx_bytes.fetch_add(len, std::memory_order_relaxed);
    draw.array_len[slot] = len;
    draw.array_count[slot] = count;
    draw.array_stride[slot] = stride;
    draw.array_format[slot] = fmt;
    return len;
}

void push_inline_vertices() {
    if (!g_inline_n) return;
    const uint32_t len = g_inline_n * 4;
    BufferRecord hdr{};
    hdr.type = kInlineVertex;
    hdr.slot = 0;
    hdr.ea = 0;
    hdr.len = len;
    hdr.stride = 0;
    if (sizeof hdr + len <= kScratchCapacity) {
        std::memcpy(g_scratch, &hdr, sizeof hdr);
        std::memcpy(g_scratch + sizeof hdr, g_inline, len);
        if (push_record(Kind::kBuffer, g_scratch, sizeof hdr + len))
            g_c.vtx_bytes.fetch_add(len, std::memory_order_relaxed);
    }
    g_inline_n = 0;
}

void push_index_buffer(uint32_t first, uint32_t count) {
    /* SET_INDEX_ARRAY_ADDRESS 0x181C + SET_INDEX_ARRAY_DMA 0x1820 hold the index
     * block's address and 16/32-bit selector. The producer tracks both; if the
     * address is absent there is nothing to carry and the draw says so via
     * array_mask bit 16 staying clear. */
    const uint32_t io = g_index_offset + g_base_offset;
    const uint32_t ea = g_hooks.io_to_ea ? g_hooks.io_to_ea(io) : 0;
    const uint8_t* src = (ea && g_hooks.guest_ptr) ? g_hooks.guest_ptr(ea) : nullptr;
    const uint32_t sz = g_index_16bit ? 2u : 4u;
    uint32_t len = (first + count) * sz;
    if (!src || !count || len == 0) return;
    if (len > 256u * 1024u) len = 256u * 1024u;
    if (sizeof(BufferRecord) + len > kScratchCapacity) return;
    BufferRecord hdr{};
    hdr.type = kIndex;
    hdr.slot = 0;
    hdr.ea = ea;
    hdr.len = len;
    hdr.stride = sz;
    std::memcpy(g_scratch, &hdr, sizeof hdr);
    std::memcpy(g_scratch + sizeof hdr, src, len);
    if (push_record(Kind::kBuffer, g_scratch, sizeof hdr + len))
        g_c.idx_bytes.fetch_add(len, std::memory_order_relaxed);
}

void push_draw(uint32_t arg, uint32_t indexed) {
    flush_state();
    push_vertex_shader_if_changed();
    push_inline_vertices();
    DrawRecord draw{};
    draw.primitive = g_primitive;
    draw.first = arg & 0x00FFFFFFu;
    draw.count = (arg >> 24) & 0xFFu;
    draw.indexed = indexed;
    draw.shader_hash[0] = g_vp_hash;
    draw.shader_hash[1] = g_fp_hash;
    if (indexed) push_index_buffer(draw.first, draw.count);
    for (uint32_t i = 0; i < 16; i++) {
        if (!((g_array_format[i] >> 4) & 0xFu)) continue;
        /* An indexed draw's vertex count is the index count, not the vertex
         * count: cap at 2048 vertices per array per draw so one draw cannot ask
         * for the whole ring. */
        uint32_t n = draw.count ? draw.count : 0;
        if (n > 2048) n = 2048;
        if (push_vertex_array(i, n, draw)) draw.array_mask |= (1u << i);
    }
    if (push_record(Kind::kDraw, &draw, sizeof draw))
        g_c.draws.fetch_add(1, std::memory_order_relaxed);
}

void push_texture(uint32_t unit) {
    if (unit >= 16) return;
    const uint32_t* regs = g_tex_regs[unit];
    if (!regs[2] || !regs[1]) return;   /* no DMA address or no format: unbound */
    uint32_t width = 0, height = 0, pitch = 0, src_size = 0;
    if (!g_hooks.describe_texture ||
        !g_hooks.describe_texture(regs, &width, &height, &pitch, &src_size)) {
        g_c.texture_refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!src_size || src_size > 4u * 1024u * 1024u) {
        g_c.texture_refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const uint32_t ea = g_hooks.io_to_ea ? g_hooks.io_to_ea(regs[2]) : 0;
    const uint8_t* src = (ea && g_hooks.guest_ptr) ? g_hooks.guest_ptr(ea) : nullptr;
    if (!src) {
        g_c.texture_refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    {
        /* Content key: address, format and rect. Same key twice in a row means the
         * page already has these bytes. */
        const uint32_t key = ea ^ (regs[1] * 2654435761u) ^ (regs[6] * 40503u);
        if (key == g_tex_key[unit]) {
            g_c.tex_unchanged.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_tex_key[unit] = key;
    }
    /* Chunked so a 4 MB source cannot monopolise the ring; the page reassembles
     * by (unit, seq, ea). */
    constexpr uint32_t kChunk = 512u * 1024u;
    uint32_t sent = 0;
    while (sent < src_size) {
        uint32_t n = src_size - sent;
        if (n > kChunk) n = kChunk;
        TextureRecord hdr{};
        hdr.unit = unit;
        hdr.ea = ea + sent;
        hdr.format = regs[1];
        hdr.width = width;
        hdr.height = height;
        hdr.pitch = pitch;
        hdr.src_size = n;
        hdr.seq = g_tex_seq;
        std::memcpy(g_scratch, &hdr, sizeof hdr);
        std::memcpy(g_scratch + sizeof hdr, src + sent, n);
        if (!push_record(Kind::kTexture, g_scratch, sizeof hdr + n)) break;
        g_c.tex_bytes.fetch_add(n, std::memory_order_relaxed);
        sent += n;
    }
    if (sent) {
        g_c.tex_objects.fetch_add(1, std::memory_order_relaxed);
        g_tex_seq++;
    }
}

void push_fragment_shader(uint32_t ea) {
    const uint8_t* src = (ea && g_hooks.guest_ptr) ? g_hooks.guest_ptr(ea) : nullptr;
    if (!src) {
        g_c.shader_refused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::memcpy(g_fp, src, sizeof g_fp);
    const uint32_t hash = fnv1a(g_fp, 512);
    if (hash == g_fp_hash) return;
    g_fp_hash = hash;
    g_fp_ea = ea;
    ShaderRecord hdr{};
    hdr.stage = 1;
    hdr.words = 512;
    hdr.hash = hash;
    hdr.ea = ea;
    std::memcpy(g_scratch, &hdr, sizeof hdr);
    std::memcpy(g_scratch + sizeof hdr, g_fp, sizeof g_fp);
    if (push_record(Kind::kShader, g_scratch, sizeof hdr + sizeof g_fp)) {
        g_c.shader_objects.fetch_add(1, std::memory_order_relaxed);
        g_c.shader_bytes.fetch_add(sizeof g_fp, std::memory_order_relaxed);
    }
}

/* Texture unit register file: 0x1A00 + unit*0x20 + field.
 *
 * The stride is 0x20, NOT 0x10: measured in runtime/src/gpu/rsx_renderer.cpp:761
 * ("TEXTURE_BASE + 4 + u * 0x20"), :1518 (b = TEXTURE_BASE + unit * 0x20) and
 * :2973. With a 0x10 stride the field->slot map folded units 1..7 onto unit 0 and
 * no address write was ever recognised as the completion of a texture, which is
 * exactly the "tex_objects=0" this producer reported for a whole run. */
bool texture_method(uint32_t method, uint32_t& unit, uint32_t& field) {
    if (method < 0x1A00 || method >= 0x1A00 + 0x20 * 16) return false;
    const uint32_t rel = method - 0x1A00;
    unit = rel / 0x20;
    field = rel % 0x20;
    return field <= 0x1C;
}

}  // namespace

void install(const Hooks& hooks) { g_hooks = hooks; }

void set_reporting(bool on) { g_reporting = on; }

void set_tee(bool on) {
    /* Opt-in, never default: in a page build the records must reach the PAGE, and
     * a tee that consumed them here would starve it.
     *
     * Measured: getenv() is NOT a way to opt in under Emscripten. Its getenv reads
     * Module.ENV, which is populated from a compile-time table and not from the
     * process environment, so PS3BASE_RSX_TEE=1 on the command line left the tee
     * disabled for a whole run (0 consumed lines, producer counters silent). The
     * measurement build therefore sets it with a compile-time flag, and the runtime
     * setter below stays available for the page. */
#if defined(PS3BASE_RSX_TEE_BUILD) && PS3BASE_RSX_TEE_BUILD
    (void)on;
    g_tee.enabled = true;
    g_reporting = true;   /* a measurement run wants the counters in the log */
#else
    g_tee.enabled = on;
    g_reporting = on;
#endif
}

void tee_drain() {
    if (!g_tee.enabled) return;
    for (;;) {
        const uint32_t kind = poll();
        if (!kind) break;
        const uint32_t size = poll_size();
        g_tee.records++;
        g_tee.bytes += size;
        const uint8_t* p = poll_bytes();
        uint32_t w[8] = {};
        const uint32_t n = size >= sizeof w ? sizeof w / 4 : size / 4;
        std::memcpy(w, p, n * 4);
        switch (kind) {
        case Kind::kStateBatch: g_tee.state += size / sizeof(StateWord); break;
        case Kind::kBuffer:
            g_tee.buffers++;
            if (w[0] == kVertex || w[0] == kInlineVertex) g_tee.vtx += w[3];
            else g_tee.idx += w[3];
            break;
        case Kind::kTexture: g_tee.textures++; g_tee.tex_bytes += w[6]; break;
        case Kind::kShader: g_tee.shaders++; g_tee.shader_bytes += w[1] * 4u; break;
        case Kind::kDraw: g_tee.draws++; break;
        case Kind::kClear: g_tee.clears++; break;
        case Kind::kFlip: g_tee.flips++; break;
        default: break;
        }
    }
}

void reset_for_test() {
    g_state_n = 0;
    g_inline_n = 0;
    g_vp_hash = 0;
    g_fp_hash = 0;
    g_tex_seq = 0;
    g_last_report_bytes = 0;
    std::memset(g_array_format, 0, sizeof g_array_format);
    std::memset(g_array_offset, 0, sizeof g_array_offset);
    std::memset(g_tex_regs, 0, sizeof g_tex_regs);
    std::memset(g_tex_key, 0, sizeof g_tex_key);
    std::memset(g_vp, 0, sizeof g_vp);
    std::memset(g_fp, 0, sizeof g_fp);
}

void on_method(uint32_t method, uint32_t arg, uint32_t src_io) {
    /* Periodic report + tee drain, keyed to METHODS rather than to a drain-end
     * hook. Measured: the LLE walker in lv2/rsx.cpp only reaches on_drain_end()
     * at an idle self-jump, and a busy frame never takes that path -- the counters
     * stayed silent for a whole run with the hook in place. Counting here makes the
     * report independent of which decoder happens to be live. */
    {
        static std::atomic<uint64_t> n{0};
        const uint64_t k = n.fetch_add(1, std::memory_order_relaxed) + 1;
        if (g_reporting && (k & 0x3FF) == 0) {
            flush_state();
            tee_drain();
            report_stats();
        }
    }
    /* First call, once: proves the seam is wired into the walker that actually
     * runs, not only into one that exists. A silent zero is otherwise ambiguous
     * between "no methods" and "not called". */
    static std::atomic<int> first{0};
    if (first.fetch_add(1, std::memory_order_relaxed) == 0)
        LOG_INFO("rsxring", "first method through the seam: 0x%04x arg=0x%08x io=0x%08x",
                 method, arg, src_io);
    static thread_local bool noted = false;
    if (!noted) { noted = true; note_producer_thread(); }
    uint32_t unit = 0, field = 0;
    if (texture_method(method, unit, field)) {
        g_tex_regs[unit][field / 4] = arg;
        add_state(method, arg);
        if (field == 0x08) {   /* TEXTURE_ADDRESS: unit is now complete */
            flush_state();
            push_texture(unit);
        }
        return;
    }

    switch (method) {
    case 0x1D90: g_clear_color = arg; add_state(method, arg); return;
    case 0x1D8C: g_clear_zstencil = arg; add_state(method, arg); return;
    case 0x1D94: {
        flush_state();
        ClearRecord rec{arg, g_clear_color, g_clear_zstencil};
        if (push_record(Kind::kClear, &rec, sizeof rec))
            g_c.clears.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    case 0x1808:   /* BEGIN_END */
        if ((arg & 0xFFu) == 0) {
            /* End of the primitive: the immediate vertices are complete, and the
             * draw is emitted here because an immediate draw has no DRAW_ARRAYS
             * packet to trigger it. */
            if (!g_primitive_had_draw) push_draw(0, 0);
            g_primitive = 0;
            g_inline_n = 0;
            g_primitive_had_draw = false;
            add_state(method, arg);
            return;
        }
        flush_state();
        g_primitive = arg & 0xFFu;
        g_primitive_had_draw = false;
        add_state(method, arg);
        return;
    case 0x1814:   /* DRAW_ARRAYS */
        push_draw(arg, 0);
        g_primitive_had_draw = true;
        return;
    case 0x1824:   /* DRAW_INDEX_ARRAY */
        push_draw(arg, 1);
        g_primitive_had_draw = true;
        return;
    case 0x1818: { /* INLINE_ARRAY: one vertex word per method call */
        if (g_inline_n == 4096) push_inline_vertices();
        g_inline[g_inline_n++] = arg;
        return;
    }
    case 0x181C: g_index_offset = arg; add_state(method, arg); return;
    case 0x1820: g_index_16bit = (arg & 0xF0u) == 0x10u; add_state(method, arg); return;
    case 0x1738: g_base_offset = arg; add_state(method, arg); return;
    case 0x1E9C: g_vp_load = arg; add_state(method, arg); return;
    case 0x08E4: {  /* SET_SHADER_PROGRAM: fragment ucode at this DMA offset */
        add_state(method, arg);
        flush_state();
        const uint32_t ea = g_hooks.io_to_ea ? g_hooks.io_to_ea(arg & ~3u) : 0;
        push_fragment_shader(ea);
        return;
    }
    case 0xE102:   /* the title's presentation request (context attribute 0x102).
                    * Real method number on this build's stream; see the call site in
                    * lv2/rsx.cpp. Counted as a flip record. */
    case 0xFEAC: {  /* GCM_FLIP_COMMAND: the flip itself, not the head select */
        flush_state();
        FlipRecord rec{0, src_io};
        if (push_record(Kind::kFlip, &rec, sizeof rec))
            g_c.flips.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    default: break;
    }

    /* Vertex array offsets 0x1680..0x16BC, formats 0x1740..0x177C: sixteen each,
     * four bytes apart (one register per array, as RPCS3's
     * NV4097_SET_VERTEX_DATA_ARRAY_OFFSET + index decoder has it). */
    if (method >= 0x1680 && method < 0x16C0) {
        g_array_offset[(method - 0x1680) / 4] = arg;
        add_state(method, arg);
        return;
    }
    if (method >= 0x1740 && method < 0x1780) {
        g_array_format[(method - 0x1740) / 4] = arg;
        add_state(method, arg);
        return;
    }
    /* SET_TRANSFORM_PROGRAM: 0x0B80..0x0BFC, addressed by the load position. */
    if (method >= 0x0B80 && method < 0x0C00) {
        const uint32_t index = method - 0x0B80;
        g_vp[(g_vp_load * 4 + (index % 4)) & 1023] = arg;
        add_state(method, arg);
        return;
    }

    /* Everything else is program state: batched, not interpreted. */
    add_state(method, arg);
}

void on_drain_end() {
    flush_state();
    tee_drain();
}

uint32_t control_word(uint32_t which) {
    CommandRing& r = ring();
    switch (which) {
    case 0: return r.used();
    case 1: return static_cast<uint32_t>(r.pushed());
    case 2: return static_cast<uint32_t>(r.popped());
    case 3: return static_cast<uint32_t>(r.wrapped());
    case 4: return kRingCapacity;
    default: return 0;
    }
}

uint32_t poll() {
    CommandRing& r = ring();
    for (;;) {
        if (r.peek_padding()) { r.skip_padding(); continue; }
        CommandRecord hdr{};
        if (!r.peek(&hdr, g_poll, kPollCapacity)) return 0;
        g_poll_size = hdr.size - static_cast<uint32_t>(sizeof(CommandRecord));
        r.advance(hdr.size);
        return hdr.kind;
    }
}

uint32_t ring_address() { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_ring_mem)); }
uint32_t ring_capacity() { return kRingCapacity; }

/* Addresses of the ring's three state words, as plain linear-memory addresses.
 *
 * Why addresses and not values: the page must DRAIN the ring from its own thread,
 * and it can only do that if it can perform the release/acquire protocol itself
 * (Atomics.load/store) on the same words the producer writes. Returning a value
 * through a function call would need the worker's thread to run, which it is not
 * doing while the page is draining.
 *
 * The words are 32-bit and the ring is single-consumer, so the page's
 * tail/used updates are the same two operations CommandRing::advance performs.
 * rsx_ring_control() still answers the counter VALUES for reporting. */
uint32_t ring_word_address(uint32_t which) {
    CommandRing& r = ring();
    switch (which) {
    case 0: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.head_word()));
    case 1: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.tail_word()));
    case 2: return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(r.used_word()));
    default: return 0;
    }
}

uint32_t poll_size() { return g_poll_size; }
const uint8_t* poll_bytes() { return g_poll; }

void stats(Stats* out) {
    if (!out) return;
    out->records = g_c.records.load(std::memory_order_relaxed);
    out->bytes = g_c.bytes.load(std::memory_order_relaxed);
    out->state_methods = g_c.state_methods.load(std::memory_order_relaxed);
    out->state_bytes = g_c.state_bytes.load(std::memory_order_relaxed);
    out->vtx_bytes = g_c.vtx_bytes.load(std::memory_order_relaxed);
    out->idx_bytes = g_c.idx_bytes.load(std::memory_order_relaxed);
    out->tex_bytes = g_c.tex_bytes.load(std::memory_order_relaxed);
    out->tex_objects = g_c.tex_objects.load(std::memory_order_relaxed);
    out->shader_bytes = g_c.shader_bytes.load(std::memory_order_relaxed);
    out->shader_objects = g_c.shader_objects.load(std::memory_order_relaxed);
    out->draws = g_c.draws.load(std::memory_order_relaxed);
    out->clears = g_c.clears.load(std::memory_order_relaxed);
    out->flips = g_c.flips.load(std::memory_order_relaxed);
    out->refused = g_c.refused.load(std::memory_order_relaxed);
    out->refused_bytes = g_c.refused_bytes.load(std::memory_order_relaxed);
    out->geometry_unmapped = g_c.geometry_unmapped.load(std::memory_order_relaxed);
    out->texture_refused = g_c.texture_refused.load(std::memory_order_relaxed);
    out->producer_threads = g_c.producer_threads.load(std::memory_order_relaxed);
    out->tex_unchanged = g_c.tex_unchanged.load(std::memory_order_relaxed);
}

void report_stats() {
    const uint64_t bytes = g_c.bytes.load(std::memory_order_relaxed);
    if (bytes == g_last_report_bytes) return;
    g_last_report_bytes = bytes;
    Stats s{};
    stats(&s);
    /* name=value so "grep -oE 'name=[0-9]+' log | tail -1" extracts a value. */
    LOG_INFO("rsxring",
             "pushed=%llu bytes=%llu state_methods=%llu state_bytes=%llu vtx_bytes=%llu "
             "idx_bytes=%llu tex_bytes=%llu tex_objects=%llu shader_bytes=%llu "
             "shader_objects=%llu draws=%llu clears=%llu flips=%llu refused=%llu "
             "refused_bytes=%llu geom_unmapped=%llu tex_refused=%llu tex_unchanged=%llu threads=%llu used=%u",
             (unsigned long long)s.records, (unsigned long long)s.bytes,
             (unsigned long long)s.state_methods, (unsigned long long)s.state_bytes,
             (unsigned long long)s.vtx_bytes, (unsigned long long)s.idx_bytes,
             (unsigned long long)s.tex_bytes, (unsigned long long)s.tex_objects,
             (unsigned long long)s.shader_bytes, (unsigned long long)s.shader_objects,
             (unsigned long long)s.draws, (unsigned long long)s.clears,
             (unsigned long long)s.flips, (unsigned long long)s.refused,
             (unsigned long long)s.refused_bytes, (unsigned long long)s.geometry_unmapped,
             (unsigned long long)s.texture_refused, (unsigned long long)s.tex_unchanged,
             (unsigned long long)g_c.producer_threads.load(std::memory_order_relaxed), ring().used());
    if (g_tee.enabled) {
        LOG_INFO("rsxring",
                 "consumed_records=%llu consumed_bytes=%llu c_state=%llu c_buffers=%llu "
                 "c_textures=%llu c_shaders=%llu c_draws=%llu c_clears=%llu c_flips=%llu "
                 "c_vtx=%llu c_idx=%llu c_tex=%llu c_shader=%llu",
                 (unsigned long long)g_tee.records, (unsigned long long)g_tee.bytes,
                 (unsigned long long)g_tee.state, (unsigned long long)g_tee.buffers,
                 (unsigned long long)g_tee.textures, (unsigned long long)g_tee.shaders,
                 (unsigned long long)g_tee.draws, (unsigned long long)g_tee.clears,
                 (unsigned long long)g_tee.flips, (unsigned long long)g_tee.vtx,
                 (unsigned long long)g_tee.idx, (unsigned long long)g_tee.tex_bytes,
                 (unsigned long long)g_tee.shader_bytes);
    }
}

}  // namespace rsxring
}  // namespace gpu

/* ---- the page's entry points ------------------------------------------------
 * They return LINEAR-MEMORY ADDRESSES: in wasm32 a pointer is an offset into the
 * single linear memory, which is exactly what an Int32Array view over the
 * module's SharedArrayBuffer needs. The page never learns the C++ layout: it
 * reads the ring's own head/tail/used words through these, and the record
 * layouts through rsx_ring.h's structs (written out byte-for-byte in worker.js).
 */
extern "C" {

/* Address of the ring's byte storage in linear memory. The JS consumer builds a
 * typed-array view over the module's heap at this offset; it never learns the
 * C++ layout of the control block below it. */
uint32_t rsx_ring_address(void) { return gpu::rsxring::ring_address(); }

uint32_t rsx_ring_capacity(void) { return gpu::rsxring::ring_capacity(); }

/* The CommandRing's control words, so the page drains the SAME instance the
 * producer pushes into. which: 0 = used bytes, 1 = head, 2 = tail, 3 = pushed
 * record count, 4 = wrapped-padding count. */
uint32_t rsx_ring_control(uint32_t which) { return gpu::rsxring::control_word(which); }

uint32_t rsx_ring_word(uint32_t which) { return gpu::rsxring::ring_word_address(which); }

/* Turn the in-process consumer on or off from JavaScript. A page leaves it off so
 * the records reach the page; a headless harness turns it on to measure the
 * transport end to end. */
void rsx_ring_tee(uint32_t on) { gpu::rsxring::set_tee(on != 0); }
uint32_t rsx_ring_poll(void) { return gpu::rsxring::poll(); }
uint32_t rsx_ring_poll_size(void) { return gpu::rsxring::poll_size(); }
uint32_t rsx_ring_poll_data(void) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gpu::rsxring::poll_bytes()));
}

/* Stats fields in declaration order (0 = records, 1 = bytes, ...). */
uint32_t rsx_ring_stat(uint32_t which) {
    gpu::rsxring::Stats s{};
    gpu::rsxring::stats(&s);
    switch (which) {
    case 0: return static_cast<uint32_t>(s.records);
    case 1: return static_cast<uint32_t>(s.bytes);
    case 2: return static_cast<uint32_t>(s.state_methods);
    case 3: return static_cast<uint32_t>(s.state_bytes);
    case 4: return static_cast<uint32_t>(s.vtx_bytes);
    case 5: return static_cast<uint32_t>(s.idx_bytes);
    case 6: return static_cast<uint32_t>(s.tex_bytes);
    case 7: return static_cast<uint32_t>(s.tex_objects);
    case 8: return static_cast<uint32_t>(s.shader_bytes);
    case 9: return static_cast<uint32_t>(s.shader_objects);
    case 10: return static_cast<uint32_t>(s.draws);
    case 11: return static_cast<uint32_t>(s.clears);
    case 12: return static_cast<uint32_t>(s.flips);
    case 13: return static_cast<uint32_t>(s.refused);
    case 14: return static_cast<uint32_t>(s.refused_bytes);
    case 15: return static_cast<uint32_t>(s.geometry_unmapped);
    case 16: return static_cast<uint32_t>(s.texture_refused);
    case 17: return static_cast<uint32_t>(s.producer_threads);
    case 18: return static_cast<uint32_t>(s.tex_unchanged);
    default: return 0;
    }
}
}