#pragma once

/* H4 -- the RSX command ring's PRODUCER: what the emulation worker hands the page.
 *
 * The transport is not written here. runtime/include/runtime/command_ring.h is a
 * single-producer / single-consumer byte ring (tests/command_ring.cpp green), and
 * runtime/src/gpu/gcm_fifo.cpp is the command processor that walks the guest's
 * NV47 stream. This file is the missing half: it classifies every method the
 * command processor executes into one of seven kinds, copies the variable
 * payload (vertex words, index words, texture source bytes, shader ucode) into
 * the ring, and counts bytes and objects per kind.
 *
 * Why a new producer when two already exist on other branches:
 *
 *   - piste G (commit cdb43e5, runtime/src/gpu/webgpu_bridge.cpp) pushes ONE
 *     record kind, only for NV4097_CLEAR_SURFACE, six words, and re-implements
 *     the label window because it predates gcm_fifo. It carries no payload bytes.
 *   - piste X4 (branch explore/xX4, runtime/src/gpu/webgpu_ring.cpp) publishes
 *     three record kinds into a sequence-word block, with no payload bytes at
 *     all: no vertex data, no texture bytes, no ucode.
 *
 * H4 must CARRY state, vertex/index data, textures and shaders, not merely
 * announce them, so neither can serve. The seam shape (on_method() called from
 * gcm_fifo's execute()) is X4's, cited; the ring is the existing CommandRing,
 * and the decode stays in gcm_fifo. Nothing here re-walks the guest ring.
 *
 * Ordering: state is batched (128 method/arg pairs per record) and the batch is
 * flushed BEFORE any record that depends on it, so a consumer applying records in
 * order sees the state the command processor had at that point.
 *
 * Back-pressure: the emulation thread must never block on the page, so a record
 * that does not fit is COUNTED and dropped (refused/refused_bytes), never
 * partially written -- CommandRing::push is all-or-nothing.
 *
 * The record layouts live here because the page's JS reads the same numbers the
 * producer wrote: one definition, no parallel struct on the JS side.
 */
#include <cstdint>

namespace gpu {
namespace rsxring {

enum Kind : uint32_t {
    kStateBatch = 1,  /* StateWord[]: batched method/arg pairs */
    kBuffer = 2,      /* BufferRecord + len payload bytes */
    kTexture = 3,     /* TextureRecord + src_size payload bytes */
    kShader = 4,      /* ShaderRecord + words*4 payload bytes */
    kDraw = 5,        /* DrawRecord: state snapshot at the draw */
    kClear = 6,       /* ClearRecord */
    kFlip = 7,        /* FlipRecord */
    kRef = 8,         /* RefRecord: an object the page ALREADY has, by fingerprint */
};

/* 128 pairs = 1 KB per record. Measured state traffic is ~1.27 M methods/s, so
 * batching is what keeps the ring from being one record per word. */
constexpr uint32_t kStateBatchWords = 128;

enum BufferType : uint32_t { kVertex = 0, kIndex = 1, kInlineVertex = 2 };

struct StateWord {
    uint32_t method;
    uint32_t arg;
};

struct BufferRecord {
    uint32_t type;    /* BufferType */
    uint32_t slot;    /* array index, 0 for the inline block */
    uint32_t ea;      /* guest EA the bytes came from, 0 for inline */
    uint32_t len;     /* payload bytes that follow */
    uint32_t stride;  /* per-vertex stride, 0 when not applicable */
    uint32_t hash;    /* CONTENT fingerprint of the payload (FNV-1a over the bytes) */
};

struct TextureRecord {
    uint32_t unit;
    uint32_t ea;
    uint32_t format;    /* the RSX format word the guest wrote */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t src_size;  /* payload bytes that follow */
    uint32_t seq;       /* producer sequence, for the page's log */
    uint32_t chunks;    /* how many kTexture records this object is split into */
    uint32_t hash;      /* CONTENT fingerprint over the WHOLE source image */
};

struct ShaderRecord {
    uint32_t stage;  /* 0 = vertex, 1 = fragment */
    uint32_t words;
    uint32_t hash;   /* FNV-1a over the ucode words */
    uint32_t ea;
};

/* A DRAW CARRIES REFERENCES, NOT BYTES.
 *
 * Every array, index block and shader the draw needs has already been sent once,
 * under the fingerprint (content hash) in the fields below. The draw record names
 * those fingerprints; the page looks them up in its own by-fingerprint store.
 * `array_len` is kept because it is what the page sizes its bind range from, and
 * it is 0 for an array that was never sent (array_mask bit clear).
 */
struct DrawRecord {
    uint32_t primitive;
    uint32_t first;
    uint32_t count;
    uint32_t indexed;
    uint32_t shader_hash[2];
    uint32_t array_mask;      /* bit i: arrays[i] was carried for this draw */
    uint32_t array_count[16];
    uint32_t array_stride[16];
    uint32_t array_format[16];
    uint32_t array_len[16];   /* bytes carried for that array, 0 if none */
    uint32_t array_hash[16];  /* CONTENT fingerprint the page binds by */
    uint32_t index_hash;      /* CONTENT fingerprint of the index block, 0 if none */
};

/* An object the page already has. Sent INSTEAD of the payload when the content
 * fingerprint was seen before, so a title that re-issues the same vertex block,
 * index block, texture or ucode every frame costs 16 bytes per announcement
 * instead of the payload. The `hash` is the same value a full send carries in
 * its own record header, which is what makes the two interchangeable. */
struct RefRecord {
    uint32_t type;    /* BufferType for kBuffer refs; kRefShader/kRefTexture below */
    uint32_t slot;    /* array slot, unit, or 0 */
    uint32_t hash;    /* content fingerprint */
    uint32_t len;     /* bytes the page already holds for it */
};

enum RefType : uint32_t {
    kRefVertex = 0,
    kRefIndex = 1,
    kRefInline = 2,
    kRefTexture = 3,
    kRefShaderVp = 4,
    kRefShaderFp = 5,
};

struct ClearRecord {
    uint32_t mask;
    uint32_t color;
    uint32_t zstencil;
};

struct FlipRecord {
    uint32_t buffer;
    uint32_t io;   /* io offset of the flip packet inside the guest's own ring */
};

/* Everything the producer needs from the rest of the runtime, injected so this
 * file builds and tests on the host without Vulkan and without the guest map.
 * The wasm build installs the real ones in gpu/rsx_ring_glue.cpp. */
struct Hooks {
    uint32_t (*io_to_ea)(uint32_t io);
    uint8_t* (*guest_ptr)(uint32_t ea);   /* nullptr when unmapped */
    bool (*describe_texture)(const uint32_t regs[9], uint32_t* width, uint32_t* height,
                             uint32_t* pitch, uint32_t* src_size);
};
void install(const Hooks& hooks);
/* Binds the three hooks to the runtime (gcm_fifo's io table, the guest map, and
 * texture_decode's describe_texture). Implemented in gpu/rsx_ring_glue.cpp, so
 * the producer itself stays free of runtime and Vulkan dependencies. */
void install_runtime_hooks();
/* Enable the in-process tee consumer (PS3BASE_RSX_TEE=1). Off by default: a page
 * build must leave the records for the page. */
void set_tee(bool on);
/* Turn the periodic LOG_INFO report on or off. Off in tests, on in a real run. */
void set_reporting(bool on);

/* The seam: gcm_fifo's execute() calls this for EVERY method it decodes. */
void on_method(uint32_t method, uint32_t arg, uint32_t src_io);
/* Called when the command processor reaches get == put: flushes the last batch so
 * the consumer never waits a frame for it. */
void on_drain_end();

struct Stats {
    uint64_t records;
    uint64_t bytes;
    uint64_t state_methods;
    uint64_t state_bytes;
    uint64_t vtx_bytes;
    uint64_t idx_bytes;
    uint64_t tex_bytes;
    uint64_t tex_objects;
    uint64_t shader_bytes;
    uint64_t shader_objects;
    uint64_t draws;
    uint64_t clears;
    uint64_t flips;
    uint64_t refused;
    uint64_t refused_bytes;
    uint64_t geometry_unmapped;
    uint64_t texture_refused;
    uint64_t producer_threads;   /* distinct threads that called on_method: must be 1 */
    uint64_t tex_unchanged;      /* texture writes skipped: content already sent */
    uint64_t refs;               /* objects announced by fingerprint only */
    uint64_t distinct_objects;   /* fingerprints sent with a full payload */
    uint64_t bytes_saved;        /* payload bytes NOT sent because of a ref */
    uint64_t shader_alternations;/* shader switches whose ucode was already sent */
};
void stats(Stats* out);
void report_stats();
void reset_for_test();

/* The CommandRing's counters, for the page. 0 = used bytes, 1 = records pushed,
 * 2 = records popped, 3 = wrap paddings, 4 = capacity. Values, not addresses:
 * the ring's atomics stay private, and the page reaches the consumer path
 * through poll() so there is exactly one consumer either way. */
uint32_t control_word(uint32_t which);

/* One-step consumer, used by the page (through the extern "C" wrappers) and by
 * tests. Returns the record kind, or 0 when the ring is empty. The payload is
 * copied into a stable address the caller can read (poll_payload_size /
 * poll_payload). Padding records are skipped internally, so the caller never
 * sees one. */
uint32_t ring_address();
uint32_t ring_word_address(uint32_t which);
uint32_t ring_capacity();
uint32_t poll(void);
uint32_t poll_size(void);
/* The payload of the last polled record. A pointer, not a uint32_t: the host
 * test is a 64-bit process, and truncating an address there is how the first
 * version of tests/rsx_ring.cpp segfaulted. The wasm32 build narrows it in the
 * extern "C" wrappers below, where 32 bits is the whole address space. */
const uint8_t* poll_bytes(void);

}  // namespace rsxring
}  // namespace gpu