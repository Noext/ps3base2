/* ps3base -- cellPngDec output conversion test.

 * Four-by-two RGBA image with hand-computed expectations, for every colour space
 * the title can ask for, including a destination pitch wider than the row.
 */
#include "runtime/pngdec_convert.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_failures = 0;

/* 4x2 source, R,G,B,A per pixel. The first four pixels are the ones named in the
 * task: black, white, transparent red, half-transparent blue. */
const uint8_t kSrc[4 * 2 * 4] = {
    /* row 0 */
    0,   0,   0,   255,   /* black, opaque */
    255, 255, 255, 255,   /* white, opaque */
    255, 0,   0,   0,     /* red, transparent */
    0,   0,   255, 128,   /* blue, half */
    /* row 1 */
    10,  20,  30,  255,
    200, 100, 50,  0,
    0,   255, 0,   64,
    255, 255, 0,   255,
};

uint8_t gray(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
}

void check(const char* what, int space, int x, int y, int comp, uint8_t want, uint8_t got)
{
    if (want == got) return;
    g_failures++;
    if (g_failures <= 30)
        std::printf("FAIL space %d pixel (%d,%d) comp %d: want %u got %u  [%s]\n",
                    space, x, y, comp, want, got, what);
}

/* Run one colour space, comparing every component against the hand calculation. */
void run(int space, uint32_t pitch)
{
    const pngdec::Format fmt = pngdec::format_for(space);
    std::vector<uint8_t> dst(uint64_t(pitch) * 2, 0xA5);   /* sentinel padding */
    pngdec::convert(kSrc, 4, 2, 4, dst.data(), pitch, space, 0xFF);

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            const uint8_t* s = kSrc + (y * 4 + x) * 4;
            const uint8_t r = s[0], g = s[1], b = s[2], a = s[3];
            const uint8_t* d = dst.data() + uint64_t(y) * pitch + uint64_t(x) * fmt.bpp;
            switch (space) {
            case 1:
                check("gray", space, x, y, 0, gray(r, g, b), d[0]);
                break;
            case 4:
                check("palette index", space, x, y, 0, r, d[0]);
                break;
            case 2:
                check("rgb.r", space, x, y, 0, r, d[0]);
                check("rgb.g", space, x, y, 1, g, d[1]);
                check("rgb.b", space, x, y, 2, b, d[2]);
                break;
            case 9:
                check("ga.gray", space, x, y, 0, gray(r, g, b), d[0]);
                check("ga.alpha", space, x, y, 1, a, d[1]);
                break;
            case 20:
                check("argb.a", space, x, y, 0, a, d[0]);
                check("argb.r", space, x, y, 1, r, d[1]);
                check("argb.g", space, x, y, 2, g, d[2]);
                check("argb.b", space, x, y, 3, b, d[3]);
                break;
            case 10:
            default:
                check("rgba.r", space, x, y, 0, r, d[0]);
                check("rgba.g", space, x, y, 1, g, d[1]);
                check("rgba.b", space, x, y, 2, b, d[2]);
                check("rgba.a", space, x, y, 3, a, d[3]);
                break;
            }
        }
        /* Padding past the row must be untouched. */
        const uint32_t used = 4u * fmt.bpp;
        for (uint32_t i = used; i < pitch; i++) {
            const uint8_t got = dst[uint64_t(y) * pitch + i];
            if (got != 0xA5) {
                g_failures++;
                if (g_failures <= 30)
                    std::printf("FAIL space %d row %d padding byte %u: want 0xA5 got 0x%02X\n",
                                space, y, i, got);
                break;
            }
        }
    }
}

}  // namespace

int main()
{
    /* Every colour space at its natural pitch, then with slack. */
    for (int space : {1, 2, 4, 9, 10, 20}) {
        const pngdec::Format fmt = pngdec::format_for(space);
        run(space, 4u * fmt.bpp);
        run(space, 4u * fmt.bpp + 8);   /* outputBytesPerLine wider than the row */
    }

    /* The specific case the task calls out: (0,0,0,255) must convert to 0, not
     * 255, in GRAYSCALE. Stated separately so a regression is unmistakable. */
    {
        uint8_t out[4] = {0xEE, 0xEE, 0xEE, 0xEE};
        const uint8_t black[4] = {0, 0, 0, 255};
        pngdec::convert(black, 1, 1, 4, out, 1, 1, 0xFF);
        if (out[0] != 0) {
            g_failures++;
            std::printf("FAIL black(0,0,0,255) in GRAYSCALE: want 0 got %u\n", out[0]);
        }
    }

    std::printf("pngdec_convert: %s (%d failures)\n", g_failures ? "FAIL" : "ok", g_failures);
    return g_failures ? 1 : 0;
}
