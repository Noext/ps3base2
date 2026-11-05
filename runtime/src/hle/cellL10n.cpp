/* ps3base -- cellL10n, the localisation helpers.

 * A title converts its own strings with these. UTF16stoUTF8s is the one that
 * matters at boot: it turns the menu and title text into something it can draw.
 * Returning ENOSYS left the caller with an empty string, so the UI came up with
 * no labels at all -- not a crash, just a screen with nothing on it, which is
 * the hardest kind of missing feature to notice.
 */
#include "common.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace hle {

namespace {

/* Encode one code point as UTF-8. Returns the number of bytes written. */
size_t utf8_encode(uint32_t cp, uint8_t* out)
{
    if (cp < 0x80) { out[0] = static_cast<uint8_t>(cp); return 1; }
    if (cp < 0x800) {
        out[0] = static_cast<uint8_t>(0xC0 | (cp >> 6));
        out[1] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<uint8_t>(0xE0 | (cp >> 12));
        out[1] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<uint8_t>(0xF0 | (cp >> 18));
    out[1] = static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<uint8_t>(0x80 | (cp & 0x3F));
    return 4;
}

}  // namespace

/* cellL10n UTF16stoUTF8s( src, src_len, &dst, dst_len, &written ) --
 * src_len is a count of UTF-16 code units, dst_len a count of bytes. */
HLE_FUNC(cellL10n, UTF16stoUTF8s) {
    /* The signature is
     *   s32 UTF16stoUTF8s(cptr<u16> src, u32* src_len, ptr<u8> dst, u32* dst_len)
     * -- both lengths are POINTERS, in and out. Reading them as values made the
     * source length a guest address (0xD0040001) and the conversion then copied
     * across the whole address space.
     */
    const uint32_t src = a32(ctx, 0);
    const uint32_t src_len_ea = a32(ctx, 1);
    const uint32_t dst_ea = a32(ctx, 2);
    const uint32_t dst_len_ea = a32(ctx, 3);
    const uint32_t src_len = src_len_ea ? vm::read32(src_len_ea) : 0;
    const uint32_t dst_len = dst_len_ea ? vm::read32(dst_len_ea) : 0;
    const uint32_t written_ea = 0;

    uint8_t* dst = dst_ea ? vm::ptr(dst_ea) : nullptr;
    uint32_t produced = 0;

    for (uint32_t i = 0; i < src_len; i++) {
        uint32_t unit = vm::read16(src + i * 2);
        /* A high surrogate takes its low half with it. An unpaired one becomes
         * U+FFFD rather than silently truncating the string. */
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < src_len) {
            const uint32_t low = vm::read16(src + (i + 1) * 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i++;
            } else {
                unit = 0xFFFD;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            unit = 0xFFFD;
        }

        uint8_t enc[4];
        const size_t n = utf8_encode(unit, enc);
        if (dst && produced + n > dst_len) break;   /* never write past the buffer */
        if (dst) std::memcpy(dst + produced, enc, n);
        produced += static_cast<uint32_t>(n);
    }

    /* The bytes written go back through the dst_len pointer. */
    if (dst_len_ea) vm::write32(dst_len_ea, produced);
    LOG_TRACE("cellL10n", "UTF16stoUTF8s: %u code units -> %u bytes", src_len, produced);
    ret(ctx, 0);
}

/* The reverse direction, same shape. */
HLE_FUNC(cellL10n, UTF8stoUTF16s) {
    /* Same pointer-shaped lengths as the other direction. */
    const uint32_t src = a32(ctx, 0);
    const uint32_t src_len_ea = a32(ctx, 1);
    const uint32_t dst_ea = a32(ctx, 2);
    const uint32_t dst_len_ea = a32(ctx, 3);
    const uint32_t src_len = src_len_ea ? vm::read32(src_len_ea) : 0;
    const uint32_t dst_len = dst_len_ea ? vm::read32(dst_len_ea) : 0;
    const uint32_t written_ea = 0;

    uint32_t out_units = 0;

    for (uint32_t i = 0; i < src_len;) {
        const uint8_t b0 = vm::ptr(src)[i];
        uint32_t cp = 0;
        uint32_t adv = 1;
        if (b0 < 0x80) { cp = b0; }
        else if ((b0 & 0xE0) == 0xC0 && i + 1 < src_len) {
            cp = ((b0 & 0x1Fu) << 6) | (vm::ptr(src)[i + 1] & 0x3Fu); adv = 2;
        } else if ((b0 & 0xF0) == 0xE0 && i + 2 < src_len) {
            cp = ((b0 & 0x0Fu) << 12) | ((vm::ptr(src)[i + 1] & 0x3Fu) << 6) |
                 (vm::ptr(src)[i + 2] & 0x3Fu); adv = 3;
        } else if ((b0 & 0xF8) == 0xF0 && i + 3 < src_len) {
            cp = ((b0 & 0x07u) << 18) | ((vm::ptr(src)[i + 1] & 0x3Fu) << 12) |
                 ((vm::ptr(src)[i + 2] & 0x3Fu) << 6) | (vm::ptr(src)[i + 3] & 0x3Fu);
            adv = 4;
        } else { cp = 0xFFFD; }
        i += adv;

        if (cp >= 0x10000) {
            if (dst_ea && out_units + 2 > dst_len / 2) break;
            const uint32_t v = cp - 0x10000;
            if (dst_ea) {
                vm::write16(dst_ea + out_units * 2, static_cast<uint16_t>(0xD800 + (v >> 10)));
                vm::write16(dst_ea + (out_units + 1) * 2, static_cast<uint16_t>(0xDC00 + (v & 0x3FF)));
            }
            out_units += 2;
        } else {
            if (dst_ea && out_units + 1 > dst_len / 2) break;
            if (dst_ea) vm::write16(dst_ea + out_units * 2, static_cast<uint16_t>(cp));
            out_units++;
        }
    }

    if (dst_len_ea) vm::write32(dst_len_ea, out_units);
    ret(ctx, 0);
}

/* l10n_get_converter( s32 src_code, s32 dst_code, s32* conv_id ) -- a handle for
 * an encoding pair. This runtime has one conversion that matters (UTF-16 to
 * UTF-8), so every pair maps to the same id; the conversion itself is done by
 * UTF16stoUTF8s / UTF8stoUTF16s above. */
HLE_FUNC(cellL10n, l10n_get_converter) {
    /* The signature is (u32 src_code, u32 dst_code) and the return value IS the
     * converter: (src << 16) | dst. There is no out-parameter. Treating
     * argument 3 as one wrote through whatever happened to be there -- a null,
     * at offset 0x18. */
    const uint32_t src_code = a32(ctx, 0);
    const uint32_t dst_code = a32(ctx, 1);
    const uint32_t conv = (src_code << 16) | (dst_code & 0xFFFFu);
    LOG_INFO("cellL10n", "l10n_get_converter(%u, %u) -> 0x%x", src_code, dst_code, conv);
    ret(ctx, conv);
}

/* l10n_convert_str( conv_id, src, src_len, dst, dst_len, u32* written ) --
 * one entry point for whatever pair the converter named. UTF-16 in, UTF-8 out,
 * which is the only direction a PS3 title uses for its own UI text. */
HLE_FUNC(cellL10n, l10n_convert_str) {
    /* l10n_convert_str( s32 cd, cptr<void> src, cptr<u32> src_len,
     *                   ptr<void> dst, ptr<u32> dst_len )
     * -- five arguments, and BOTH lengths are pointers, in and out. Reading
     * them as values made the source length a guest pointer and the conversion
     * then read across the address space. */
    const uint32_t src_ea = a32(ctx, 1);
    const uint32_t src_len_ea = a32(ctx, 2);
    const uint32_t dst_ea = a32(ctx, 3);
    const uint32_t dst_len_ea = a32(ctx, 4);
    const uint32_t src_len = src_len_ea ? vm::read32(src_len_ea) : 0;
    const uint32_t dst_len = dst_len_ea ? vm::read32(dst_len_ea) : 0;
    const uint32_t written_ea = 0;

    uint8_t* dst = dst_ea ? vm::ptr(dst_ea) : nullptr;
    uint32_t produced = 0;
    for (uint32_t i = 0; i < src_len; i++) {
        uint32_t unit = vm::read16(src_ea + i * 2);
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < src_len) {
            const uint32_t low = vm::read16(src_ea + (i + 1) * 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i++;
            } else {
                unit = 0xFFFD;
            }
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            unit = 0xFFFD;
        }
        uint8_t enc[4];
        const size_t n = utf8_encode(unit, enc);
        if (dst && produced + n > dst_len) break;
        if (dst) std::memcpy(dst + produced, enc, n);
        produced += static_cast<uint32_t>(n);
    }
    if (dst_len_ea) vm::write32(dst_len_ea, produced);
    ret(ctx, 0);
}

}  // namespace hle
