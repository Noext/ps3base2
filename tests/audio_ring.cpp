/* The audio ring must never block the emulation thread and never lose samples
 * silently. Both matter: a blocking ring stalls the guest, an unaccounted drop
 * is a click nobody can explain.
 */
#include "runtime/audio_ring.h"

#include <cstdio>
#include <cmath>
#include <vector>

using namespace gpu;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { std::printf("FAIL %s\n", what); failures++; }
}

int main() {
    std::vector<float> buf(2 * 64);
    AudioRing r(buf.data(), 64);

    check(r.available() == 0, "starts empty");
    check(r.free_frames() == 63, "one frame is reserved so full differs from empty");

    /* Round trip, stereo interleaved. */
    {
        float in[2 * 8];
        for (int i = 0; i < 8; i++) { in[2*i] = float(i); in[2*i+1] = float(i) + 0.5f; }
        check(r.write(in, 8) == 8, "writes 8 frames");
        check(r.available() == 8, "8 available");
        float out[2 * 8] = {};
        check(r.read(out, 8) == 8, "reads 8 frames");
        bool same = true;
        for (int i = 0; i < 16; i++) if (out[i] != in[i]) same = false;
        check(same, "samples preserved exactly, both channels");
        check(r.dropped_frames() == 0, "nothing dropped");
        check(r.written_frames() == 8 && r.read_frames() == 8, "counters agree");
    }

    /* A full ring DROPS and counts; it never blocks and never overwrites. */
    {
        std::vector<float> b2(2 * 8);
        AudioRing c(b2.data(), 8);
        float block[2 * 4] = {1,1,2,2,3,3,4,4};
        uint32_t accepted = 0;
        for (int i = 0; i < 10; i++) accepted += c.write(block, 4);
        check(accepted < 40, "the ring refused some writes");
        check(c.dropped_frames() == 40 - accepted, "every refused frame is accounted as dropped");
        check(c.available() <= 7, "never exceeds capacity minus one");
        /* What was accepted must still be the OLDEST data, unbroken. */
        float out[2 * 4] = {};
        check(c.read(out, 4) == 4, "reads the oldest block");
        check(out[0] == 1 && out[7] == 4, "the oldest block is intact");
    }

    /* Wrap-around both directions, with no loss when the consumer keeps up. */
    {
        std::vector<float> b3(2 * 16);
        AudioRing c(b3.data(), 16);
        uint32_t written = 0, readn = 0;
        float block[2 * 5];
        for (int i = 0; i < 5; i++) { block[2*i] = float(written + i); block[2*i+1] = float(written + i); }
        for (int round = 0; round < 200; round++) {
            for (int i = 0; i < 5; i++) { block[2*i] = float(round); block[2*i+1] = float(round); }
            const uint32_t w = c.write(block, 5);
            written += w;
            float out[2 * 16] = {};
            const uint32_t got = c.read(out, 5);
            readn += got;
            check(w == 5 || c.available() > 0, "a short write only happens when the ring is not empty");
        }
        check(c.written_frames() == written, "written counter matches");
        check(c.read_frames() == readn, "read counter matches");
        check(c.dropped_frames() == written - readn || c.dropped_frames() == 0,
              "drops are either zero or exactly what did not fit");
    }

    /* The consumer reading more than is there gets what exists, not garbage. */
    {
        std::vector<float> b4(2 * 32);
        AudioRing c(b4.data(), 32);
        float in[2 * 3] = {7,7,8,8,9,9};
        c.write(in, 3);
        float out[2 * 10];
        for (float& f : out) f = -1.0f;
        const uint32_t got = c.read(out, 10);
        check(got == 3, "a short read returns what exists");
        check(out[0] == 7 && out[5] == 9, "the returned samples are the real ones");
        check(out[6] == -1.0f, "the caller's buffer past the read is untouched, so it can zero it");
    }

    std::printf("audio_ring: %d failures\n", failures);
    return failures ? 1 : 0;
}
