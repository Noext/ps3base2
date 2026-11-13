/* CommandRing -- the wrap path, forced to every gap size, with a CANARY.
 *
 * The failure this catches was MEASURED on the S2 page, not imagined. The old push()
 * wrote its padding marker with memcpy(mem_ + head, &ph, 8) whenever the record did
 * not fit before the end -- even when the room left at the end was 0..7 bytes. That
 * wrote PAST the end of the ring buffer. On the page the buffer is a static array, so
 * those 8 bytes landed in whatever followed it, and the effects were exactly what was
 * measured: used_ pinned at capacity-16, the consumer frozen at 16283 of 16398
 * records, and the producer refusing forever (refused=117568).
 *
 * So the ring here is placed inside a larger allocation with a CANARY on both sides.
 * A ring that overruns is caught by the canary even when the corruption happens to be
 * harmless to the heap, which is exactly the case the page hit.
 *
 * Each record is SELF-DESCRIBING: its kind carries its payload length and its payload
 * is a byte pattern derived from that length, so a record read back is checked against
 * itself. Payload lengths step by 4 because CommandRing aligns sizes to words.
 */
#include "runtime/command_ring.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gpu;

static int failures = 0;
static void check(bool ok, const char* what, uint64_t extra) {
    std::printf("%s %s (%llu observed)\n", ok ? "PASS" : "FAIL", what,
                (unsigned long long)extra);
    if (!ok) failures++;
}

static constexpr uint32_t kBase = 0x1000u;
static constexpr uint32_t kCanaryBytes = 64;
static constexpr uint8_t kCanary = 0xA5;

static void drain_all(CommandRing& ring, std::vector<uint8_t>& out, uint64_t& popped,
                      uint32_t& bad, uint32_t& torn, bool& printed) {
    for (;;) {
        if (ring.peek_padding()) { ring.skip_padding(); continue; }
        CommandRecord hdr{};
        if (!ring.peek(&hdr, out.data(), static_cast<uint32_t>(out.size()))) return;
        const uint32_t plen = hdr.size - static_cast<uint32_t>(sizeof hdr);
        if (hdr.size < sizeof hdr || hdr.size > out.size()) torn++;
        else if (hdr.kind < kBase || hdr.kind > kBase + 4096u) bad++;
        else {
            const uint32_t n = hdr.kind - kBase;
            bool ok = (n == plen);
            for (uint32_t b = 0; ok && b < plen; b++)
                if (out[b] != static_cast<uint8_t>(n * 7u + b)) ok = false;
            if (!ok) {
                if (!printed) {
                    std::printf("DBG kind=%u plen=%u n=%u size=%u\n", hdr.kind, plen, n, hdr.size);
                    printed = true;
                }
                bad++;
            }
        }
        popped++;
        ring.advance(hdr.size);
    }
}

int main() {
    constexpr uint32_t kCapacity = 4096;
    std::vector<uint8_t> store(kCapacity + 2 * kCanaryBytes);
    std::memset(store.data(), kCanary, store.size());
    uint8_t* ring_mem = store.data() + kCanaryBytes;
    CommandRing ring(ring_mem, kCapacity);

    uint64_t pushed = 0, popped = 0;
    uint32_t bad = 0, torn = 0, canary_hits = 0;
    bool printed = false;
    std::vector<uint8_t> out(kCapacity);

    auto canary_ok = [&]() {
        for (uint32_t i = 0; i < kCanaryBytes; i++) {
            if (store[i] != kCanary) return false;
            if (store[kCanaryBytes + kCapacity + i] != kCanary) return false;
        }
        return true;
    };

    for (uint32_t n = 0; n <= 96; n += 4) {
        std::vector<uint8_t> payload(n);
        for (uint32_t i = 0; i < n; i++) payload[i] = static_cast<uint8_t>(n * 7u + i);
        for (uint32_t i = 0; i < 4000; i++) {
            if (ring.push(kBase + n, payload.data(), n)) pushed++;
            drain_all(ring, out, popped, bad, torn, printed);
            if (!canary_ok()) { canary_hits++; std::memset(store.data(), kCanary, store.size()); }
        }
    }
    drain_all(ring, out, popped, bad, torn, printed);

    std::printf("INFO pushed=%llu popped=%llu wrapped=%llu used=%u\n",
                (unsigned long long)pushed, (unsigned long long)popped,
                (unsigned long long)ring.wrapped(), ring.used());
    check(canary_hits == 0, "push() never wrote past the end of the ring buffer", canary_hits);
    check(torn == 0, "no record with an impossible size", torn);
    check(bad == 0, "no record whose kind, length and payload disagree", bad);
    check(popped == pushed, "every pushed record came back out", popped - pushed);
    check(ring.wrapped() > 100, "the sweep really wrapped many times", (uint64_t)ring.wrapped());
    check(ring.used() == 0, "the ring ends empty", ring.used());
    std::printf("command_ring_wrap: %d failures\n", failures);
    return failures ? 1 : 0;
}
