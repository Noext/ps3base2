#include "runtime/command_ring.h"

#include <cstring>

namespace gpu {

namespace {
constexpr uint32_t kAlign = 4;
inline uint32_t align_up(uint32_t v) { return (v + (kAlign - 1)) & ~(kAlign - 1); }
}  // namespace

CommandRing::CommandRing(uint8_t* memory, uint32_t capacity)
    : mem_(memory), capacity_(capacity) {
    /* Power of two: the wrap is a mask, and a modulus here would be a division
     * in the per-command path. */
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
}

uint32_t CommandRing::used() const { return used_.load(std::memory_order_acquire); }

uint32_t CommandRing::free_space() const {
    /* One slot stays empty so head == tail means empty, never full. */
    return capacity_ - 1 - used();
}

/* Full/empty is decided by an explicit BYTE COUNTER, not by comparing head and
 * tail. The position comparison cannot distinguish "empty" from "full when head
 * wrapped back to tail", and every earlier version of this file broke on exactly
 * that case: either it overwrote unread data, or it refused to ever wrap.
 *
 * used_ is the number of readable bytes. head_/tail_ remain for the contiguous
 * copy only.
 */
bool CommandRing::push(uint32_t kind, const void* payload, uint32_t payload_size) {
    const uint32_t need = align_up(static_cast<uint32_t>(sizeof(CommandRecord)) + payload_size);
    if (need > capacity_ - 1) return false;     /* never fits */

    const uint32_t head = head_.load(std::memory_order_relaxed);
    const uint32_t tail = tail_.load(std::memory_order_acquire);
    const uint32_t used = used_.load(std::memory_order_acquire);
    const uint32_t tail_room = capacity_ - head;

    /* If the record does not fit before the end, a padding marker consumes the
     * rest and the record starts at 0. */
    uint32_t start = head;
    uint32_t pad = 0;
    uint32_t cost = need;
    if (need > tail_room) {
        pad = tail_room;
        cost = need + pad;
        start = 0;
        if (used != 0) {
            /* Wrapping while data is outstanding is legal: the padding makes the
             * consumer jump to 0 and the data behind it is still contiguous from
             * the tail. But the bytes at [head, capacity_) must not hold unread
             * data, which they cannot: unread data lies AFTER the tail, and the
             * tail is at or before the head only in the wrapped case. Clear the
             * case where the tail is inside the gap by demanding room up to the
             * tail, handled by the counter check below. */
        }
    }
    /* RESERVE atomically, never load-then-store: the consumer moves used_ too, and
     * a plain store here silently threw away its subtraction (measured: used_
     * wrapped past capacity on the S2 page). reserved_ holds the in-flight bytes so
     * two pushes cannot both pass the check against the same free space. */
    const uint32_t already = reserved_.fetch_add(cost, std::memory_order_acq_rel);
    /* Free space = capacity-1 - (readable used_ + in-flight reserved_). Reading used_
     * here can only be STALE LOW (the consumer only ever subtracts), so a race makes
     * this check refuse a record that would have fit -- never accept one that does
     * not. That is the safe direction: a false refusal is counted and recovered, an
     * over-commit corrupts the stream. */
    if (used + already + cost > capacity_ - 1) {
        reserved_.fetch_sub(cost, std::memory_order_acq_rel);
        return false;   /* full: caller drains */
    }

    if (pad) {
        CommandRecord ph{pad, kCommandPad};
        std::memcpy(mem_ + head, &ph, sizeof ph);
        if (pad > sizeof ph) std::memset(mem_ + head + sizeof ph, 0, pad - sizeof ph);
        wrapped_.fetch_add(1, std::memory_order_relaxed);
    }

    CommandRecord hdr{need, kind};
    std::memcpy(mem_ + start, &hdr, sizeof hdr);
    if (payload_size && payload) std::memcpy(mem_ + start + sizeof hdr, payload, payload_size);
    const uint32_t written = static_cast<uint32_t>(sizeof hdr) + payload_size;
    if (need > written) std::memset(mem_ + start + written, 0, need - written);

    uint32_t new_head = start + need;
    if (new_head >= capacity_) new_head = 0;
    /* Publish: the bytes are in place, so the size becomes readable. Release pairs
     * with the consumer's acquire load of used_. */
    used_.fetch_add(cost, std::memory_order_release);
    head_.store(new_head, std::memory_order_release);
    pushed_.fetch_add(1, std::memory_order_relaxed);
    reserved_.fetch_sub(cost, std::memory_order_acq_rel);
    (void)used;
    (void)tail;
    return true;
}

uint32_t CommandRing::peek_padding() const {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    if (t == h) return 0;
    CommandRecord hdr{};
    std::memcpy(&hdr, mem_ + t, sizeof hdr);
    return (hdr.kind == kCommandPad) ? hdr.size : 0;
}

void CommandRing::skip_padding() {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    CommandRecord hdr{};
    std::memcpy(&hdr, mem_ + t, sizeof hdr);
    if (hdr.kind != kCommandPad) return;
    uint32_t nt = t + hdr.size;
    if (nt >= capacity_) nt -= capacity_;
    used_.fetch_sub(hdr.size, std::memory_order_acq_rel);
    tail_.store(nt, std::memory_order_release);
}

bool CommandRing::peek(CommandRecord* hdr, void* out, uint32_t out_capacity) const {
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    if (t == h) return false;
    CommandRecord rh{};
    std::memcpy(&rh, mem_ + t, sizeof rh);
    if (rh.kind == kCommandPad) return false;   /* caller skips first */
    if (rh.size == 0 || rh.size > capacity_) return false;
    const uint32_t payload = rh.size - static_cast<uint32_t>(sizeof rh);
    if (payload > out_capacity) return false;
    if (payload && out) std::memcpy(out, mem_ + t + sizeof rh, payload);
    if (hdr) *hdr = rh;
    return true;
}

void CommandRing::advance(uint32_t size) {
    if (size == 0) return;
    const uint32_t t = tail_.load(std::memory_order_relaxed);
    uint32_t nt = t + size;
    if (nt >= capacity_) nt -= capacity_;
    used_.fetch_sub(size, std::memory_order_acq_rel);
    tail_.store(nt, std::memory_order_release);
    popped_.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace gpu
