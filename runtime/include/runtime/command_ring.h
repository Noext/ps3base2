#pragma once

/* The command ring between the emulation worker and the page thread.
 *
 * Milestone 4 runs the emulator in a Worker and keeps the WebGPU device on the
 * page. The worker cannot touch the device, so it posts the RSX command stream
 * and the page replays it -- single producer, single consumer, one shared
 * buffer, no locks on the fast path.
 *
 * This is NOT runtime/src/gpu/ring.cpp: that one is a Vulkan GPU buffer for
 * vertex/index data. This one carries commands across a thread boundary, and the
 * two must not be confused.
 *
 * Design constraints, each of which is a way the naive version breaks:
 *
 *  - Wrap-around. The producer must not overwrite unread bytes. A record that
 *    does not fit in the tail goes to the head with a padding marker, so a
 *    consumer never reassembles half a command.
 *  - Back-pressure. When the ring is full the producer must wait, not drop:
 *    a dropped RSX command is a wrong picture with no error.
 *  - Memory ordering. The producer publishes bytes before it publishes the head;
 *    the consumer reads the head before it reads the bytes. Release/acquire, not
 *    relaxed.
 *  - Alignment. Records are 4-byte aligned because RSX methods are words; an
 *    unaligned read would be a trap in the wasm build.
 *
 * Sizes are explicit uint32_t so the layout is identical whether the ring is
 * mapped from JavaScript, from shared wasm memory, or from a plain mmap.
 */
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace gpu {

/* One record: a length, a kind, and a payload that follows in the ring. The
 * length includes the header, and is a multiple of 4. */
struct CommandRecord {
    uint32_t size;   /* bytes, header included, multiple of 4, never 0 */
    uint32_t kind;   /* consumer-defined opcode */
};

enum : uint32_t {
    /* A padding record: the producer ran out of room at the tail. The consumer
     * skips it and resumes at the head. Its size is the padding length. */
    kCommandPad = 0xFFFFFFFFu,
};

class CommandRing {
public:
    /* capacity must be a power of two so the modulo is a mask. */
    CommandRing(uint8_t* memory, uint32_t capacity);

    /* Producer side. Returns false when the record does not fit even after a
     * wrap; the caller must drain and retry. Never partially writes. */
    bool push(uint32_t kind, const void* payload, uint32_t payload_size);

    /* Consumer side. Returns the record size on success and 0 when empty or when
     * the record spans a wrap and needs another look after skip_padding(). */
    uint32_t peek_padding() const;
    void skip_padding();

    /* Reads one record into out (capacity out_capacity). Returns false when empty
     * or when out is too small. On success, advance() must be called. */
    bool peek(CommandRecord* hdr, void* out, uint32_t out_capacity) const;
    void advance(uint32_t size);

    uint32_t used() const;
    uint32_t free_space() const;
    /* The three state words, for a consumer that lives in another language (the
     * page's JavaScript) and can therefore only reach them as addresses in the
     * shared linear memory. Single consumer: the caller must be that consumer. */
    uint32_t* head_word() { return reinterpret_cast<uint32_t*>(&head_); }
    uint32_t* tail_word() { return reinterpret_cast<uint32_t*>(&tail_); }
    uint32_t* used_word() { return reinterpret_cast<uint32_t*>(&used_); }

    uint64_t pushed() const { return pushed_.load(std::memory_order_relaxed); }
    uint64_t popped() const { return popped_.load(std::memory_order_relaxed); }
    uint64_t wrapped() const { return wrapped_.load(std::memory_order_relaxed); }

private:
    uint8_t* mem_;
    uint32_t capacity_;
    alignas(64) std::atomic<uint32_t> head_;   /* write position, producer only */
    alignas(64) std::atomic<uint32_t> tail_;   /* read position, consumer only */
    /* Readable bytes. Full/empty cannot be decided by comparing head and tail:
     * after a wrap head can equal tail while the ring is full, which every
     * position-only version of this code got wrong. */
    std::atomic<uint32_t> used_{0};
    /* Bytes the producer has COMMITTED to but not yet published as readable.
     * Without it the free-space check is a load-then-store on used_, which races
     * the consumer's own subtraction: measured on the S2 page, used_ wrapped to
     * 4 294 966 960 (> capacity) and the ring then reported itself permanently
     * full while the consumer had already advanced. used_ is now only ever moved
     * by an atomic RMW on both sides, and reserved_ is the producer's private
     * reservation so it can never over-commit while a push is in flight. */
    std::atomic<uint32_t> reserved_{0};
    std::atomic<uint64_t> pushed_{0};
    std::atomic<uint64_t> popped_{0};
    std::atomic<uint64_t> wrapped_{0};
};

}  // namespace gpu