#pragma once
/* cellPngDec output conversion, shared by the HLE handler and its unit test.

 * The handler used to write RGBA for every request. The conversion lives here so
 * the test exercises the same code the emulator runs, rather than a copy that
 * can drift.
 */
#include <cstdint>

namespace pngdec {

struct Format {
    int components = 4;
    int bpp = 4;
    bool alpha = true;
    bool argb = false;
    bool palette = false;
};

inline bool space_has_alpha(int32_t space)
{
    return space == 9 || space == 10 || space == 20;
}

inline Format format_for(int32_t space)
{
    Format f;
    switch (space) {
    case 1:  f.components = 1; f.bpp = 1; f.alpha = false; break;              /* GRAYSCALE */
    case 2:  f.components = 3; f.bpp = 3; f.alpha = false; break;              /* RGB */
    case 4:  f.components = 1; f.bpp = 1; f.alpha = false; f.palette = true; break; /* PALETTE */
    case 9:  f.components = 2; f.bpp = 2; f.alpha = true; break;               /* GRAYSCALE_ALPHA */
    case 20: f.components = 4; f.bpp = 4; f.alpha = true; f.argb = true; break; /* ARGB */
    case 10:
    default: f.components = 4; f.bpp = 4; f.alpha = true; break;               /* RGBA */
    }
    return f;
}

/* Convert one RGBA8 image into the requested layout.
 *   src      width*height*4 bytes, R,G,B,A order
 *   dst      written with `pitch` bytes per row; padding is left untouched
 *   alpha_fill  used for the alpha component when the source has 3 channels
 */
inline void convert(const uint8_t* src, int width, int height, int src_channels,
                    uint8_t* dst, uint32_t pitch, int32_t space, uint8_t alpha_fill)
{
    const Format fmt = format_for(space);
    for (int y = 0; y < height; y++) {
        uint8_t* row = dst + uint64_t(y) * pitch;
        const uint8_t* s = src + uint64_t(y) * width * 4;
        for (int x = 0; x < width; x++, s += 4) {
            const uint8_t r = s[0], g = s[1], b = s[2], a = s[3];
            const uint8_t gray = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
            uint8_t* d = row + uint64_t(x) * fmt.bpp;
            switch (space) {
            case 1:  d[0] = gray; break;                      /* GRAYSCALE */
            case 4:  d[0] = r; break;                         /* PALETTE index */
            case 2:  d[0] = r; d[1] = g; d[2] = b; break;     /* RGB */
            case 9:  d[0] = gray;                             /* GRAYSCALE_ALPHA */
                     d[1] = src_channels < 4 ? alpha_fill : a; break;
            case 20: d[0] = a; d[1] = r; d[2] = g; d[3] = b; break; /* ARGB */
            default: d[0] = r; d[1] = g; d[2] = b; d[3] = a; break; /* RGBA */
            }
        }
    }
}

}  // namespace pngdec
