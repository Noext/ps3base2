/* The command ring carries the RSX stream across the worker boundary. Every way
 * a ring goes wrong is a wrong picture with no error, so each is pinned here:
 * ordering, wrap-around, back-pressure, and losing no record.
 */
#include "runtime/command_ring.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace gpu;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); failures++; }
}

int main() {
    std::vector<uint8_t> mem(256);
    CommandRing r(mem.data(), static_cast<uint32_t>(mem.size()));
    check(r.used() == 0, "starts empty");

    /* Round trip. */
    {
        uint32_t v[3] = {0x11223344u, 0x55667788u, 0x99AABBCCu};
        check(r.push(7, v, sizeof v), "push succeeds");
        check(r.used() > 0, "used grows");
        CommandRecord hdr{};
        uint32_t out[3] = {};
        check(r.peek(&hdr, out, sizeof out), "peek succeeds");
        check(hdr.kind == 7, "kind preserved");
        check(std::memcmp(v, out, sizeof v) == 0, "payload preserved byte for byte");
        r.advance(hdr.size);
        check(r.used() == 0, "empty again after advance");
        check(r.pushed() == 1 && r.popped() == 1, "counters agree");
    }

    /* Order is preserved across many records. */
    {
        std::vector<uint8_t> m2(1024);
        CommandRing c(m2.data(), static_cast<uint32_t>(m2.size()));
        for (uint32_t i = 0; i < 20; i++) check(c.push(100 + i, &i, sizeof i), "push a sequence");
        for (uint32_t i = 0; i < 20; i++) {
            CommandRecord hdr{};
            uint32_t got = 0xFFFFFFFFu;
            check(c.peek(&hdr, &got, sizeof got), "peek the sequence");
            check(hdr.kind == 100 + i, "kinds come back in order");
            check(got == i, "payloads come back in order");
            c.advance(hdr.size);
        }
        check(c.used() == 0, "drained");
    }

    /* Back-pressure: a full ring refuses rather than overwriting unread bytes. */
    {
        std::vector<uint8_t> m3(64);
        CommandRing c(m3.data(), static_cast<uint32_t>(m3.size()));
        uint32_t payload[4] = {1, 2, 3, 4};   /* 16 bytes + 8 header = 24 aligned */
        int pushed = 0;
        while (c.push(1, payload, sizeof payload)) pushed++;
        check(pushed > 0, "it accepted something");
        check(pushed < 10, "it stopped before filling 64 bytes ten times");
        /* A refused push must not have moved the head: the ring still holds
         * exactly what it held. */
        const uint32_t used_before = c.used();
        check(!c.push(1, payload, sizeof payload), "a full ring refuses");
        check(c.used() == used_before, "a refused push changes nothing");
    }

    /* Wrap-around: many rounds through a small ring, losing nothing and keeping
     * order. The consumer drains whenever the producer blocks, which is what the
     * page does between frames. */
    {
        std::vector<uint8_t> m4(64);
        CommandRing c(m4.data(), static_cast<uint32_t>(m4.size()));
        constexpr uint32_t kRounds = 500;
        uint32_t seen = 0;
        for (uint32_t round = 0; round < kRounds; round++) {
            const uint32_t v = round;
            bool pushed = c.push(9, &v, sizeof v);
            if (!pushed) {
                for (int attempt = 0; attempt < 64 && !pushed; attempt++) {
                    if (c.peek_padding()) { c.skip_padding(); continue; }
                    CommandRecord hdr{};
                    uint32_t out = 0;
                    if (c.peek(&hdr, &out, sizeof out)) {
                        check(out == seen, "drained value matches what was pushed");
                        seen++;
                        c.advance(hdr.size);
                    }
                    pushed = c.push(9, &v, sizeof v);
                }
            }
            check(pushed, "every record is eventually accepted");
        }
        for (;;) {
            if (c.peek_padding()) { c.skip_padding(); continue; }
            CommandRecord hdr{};
            uint32_t out = 0;
            if (!c.peek(&hdr, &out, sizeof out)) break;
            check(out == seen, "tail values ordered");
            seen++;
            c.advance(hdr.size);
        }
        check(seen == kRounds, "every pushed record was seen exactly once");
        check(c.used() == 0, "the ring is empty at the end");
        check(c.wrapped() > 0, "the ring actually wrapped");
    }

    /* A payload larger than the ring is refused, not truncated. */
    {
        std::vector<uint8_t> m5(32);
        CommandRing c(m5.data(), static_cast<uint32_t>(m5.size()));
        std::vector<uint8_t> big(64, 0xAB);
        check(!c.push(1, big.data(), static_cast<uint32_t>(big.size())), "oversized payload refused");
        check(c.used() == 0, "nothing was written");
    }

    std::printf("command_ring: %d failures\n", failures);
    return failures ? 1 : 0;
}
