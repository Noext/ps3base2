/* hook_flags: one array per image, and every address in bounds.
 *
 * The bug this guards: a single global uint8_t[0x800000] indexed by addr >> 2
 * covers addresses up to 0x2000000 only. Red Dead Redemption's liblv2 is placed
 * at 0x21000000, so its entry stub indexed 0x8405244 -- 124 MB past the end of
 * the array -- and the write faulted at a garbage address far from the cause.
 *
 * The generated table also carries every lifted function, so linking the real
 * table.cpp would drag in the whole game. This test therefore builds its own
 * image table at a HIGH address, which is exactly the case the old design could
 * not represent, and checks the lookup against it.
 */
#include "runtime/image.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ppu {
/* Stand-in for the generated arrays. Two images, the second above 0x20000000. */
constexpr uint32_t kImage0 = 4000;
constexpr uint32_t kImage1 = 900;
uint8_t storage_0[kImage0] = {};
uint8_t storage_1[kImage1] = {};
extern uint8_t* const hook_flags[] = {storage_0, storage_1};
extern const uint32_t hook_flag_image_count = 2;
}  // namespace ppu

namespace image {
const ImageEntry images[] = {
    {"game", "game.elf", false, 0x00010200, 0x00010200 + 4 * ppu::kImage0, 0, 0},
    {"liblv2", "liblv2.elf", true, 0x21000000, 0x21000000 + 4 * ppu::kImage1, 0, 0},
};
const uint32_t image_count = 2;
const uint32_t function_count = 0;
const uint32_t import_count = 0;
const uint32_t export_count = 0;
}  // namespace image

namespace ppu {

/* The real implementation, kept identical to ppu_ops.cpp's. Copied rather than
 * linked because linking ppu_ops.cpp would pull in the whole runtime. */
uint8_t* hook_flag_for(uint32_t addr) {
    for (uint32_t i = 0; i < image::image_count; i++) {
        const image::ImageEntry& im = image::images[i];
        if (addr >= im.text_lo && addr < im.text_hi) {
            const uint32_t idx = (addr - im.text_lo) >> 2;
            if (idx >= (im.text_hi - im.text_lo + 3) / 4) return nullptr;
            return &hook_flags[i][idx];
        }
    }
    return nullptr;
}

}  // namespace ppu

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

}  // namespace

int main() {
    /* 1. A high image resolves, and to its OWN array. This is the case that
     *    crashed: 0x21000000 indexed a flat array 124 MB past its end. */
    uint8_t* hi = ppu::hook_flag_for(0x21000000);
    check(hi != nullptr, "an image above 0x20000000 resolves");
    check(hi == ppu::storage_1, "the high image resolves to its own array, not the game's");

    /* 2. The index is relative to the image, so it is small even at 0x21000000. */
    if (hi) {
        const long idx = hi - ppu::storage_1;
        check(idx == 0, "the first word of a high image has index 0");
        check(idx >= 0 && idx < (long)ppu::kImage1, "the index stays inside the array");
        *hi = 7;
        check(ppu::storage_1[0] == 7, "a write through the lookup lands in the array");
        check(ppu::storage_0[0] == 0, "and not in the other image's array");
    }

    /* 3. The game image still works and does not alias the module. */
    uint8_t* lo = ppu::hook_flag_for(0x00010200);
    check(lo == ppu::storage_0, "the game resolves to the first array");
    if (lo) {
        *lo = 3;
        check(ppu::storage_0[0] == 3, "a write to the game lands in its array");
        check(ppu::storage_1[0] == 7, "and leaves the module's flag alone");
    }

    /* 4. One past the end of an image belongs to no image. */
    check(ppu::hook_flag_for(0x21000000 + 4 * ppu::kImage1) == nullptr, "one past the module resolves to nothing");
    check(ppu::hook_flag_for(0x21000000 + 4 * ppu::kImage1 + 0x1000) == nullptr, "well past the module resolves to nothing");
    check(ppu::hook_flag_for(0x18000000) == nullptr, "an address between images resolves to nothing");

    if (failures == 0) std::printf("hook_flags: %u images, high image above 0x20000000, all checks passed\n", image::image_count);
    else std::printf("hook_flags: %d FAILURES\n", failures);
    return failures ? 1 : 0;
}
