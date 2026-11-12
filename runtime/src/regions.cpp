#include "runtime/regions.h"

#include "runtime/log.h"

#include <cstdlib>
#include <cstring>

namespace vm {

namespace {

RegionBases choose() {
    /* Its own variable. PS3BASE_MEMORY selects the memory BACKEND (os|bitset);
     * the region LAYOUT is a separate concern and must not fight over one name. */
    const char* e = std::getenv("PS3BASE_REGIONS");
    RegionBases b{};
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    /* The sparse map spans exactly 4 GiB, which leaves no room for code, stack or
     * tables in wasm32 linear memory. Compact is mandatory there. */
    if (!e || std::strcmp(e, "compact") != 0)
        e = "compact";
#endif
    if (e && std::strcmp(e, "compact") == 0) {
        /* Close the 1792 MB hole: video/stack/spu follow the user regions.
         *
         * The video base is 0x58000000, NOT 0x50000000, and that number is
         * measured, not chosen for looks. With video at 0x50000000 the user
         * region ends at 0x50000000 and every 256 MB slot below it is already
         * taken (region 1, the libsysutil shared block, the images, region 2),
         * so Region::RsxContext has nowhere to go: vm::alloc returns 0 for it,
         * sys_rsx_context_allocate answers CELL_ENOMEM, and the LLE
         * _cellGcmInitBody -- the body the native reference executes -- returns
         * 0x80010004 and never installs the FIFO callback. Measured in
         * docs/explore/G1/RESULTS.md 6.1.
         *
         * 0x58000000 leaves exactly one free 128 MB slot at 0x50000000, which is
         * the address the native sparse layout gives the RSX context, so the
         * context, driver and report blocks land on the SAME addresses as the
         * native reference (0x50100000 / 0x50200000 / 0x50300000). The video and
         * stack bases differ from native only because the browser layout cannot
         * hold 0xC0000000/0xD0000000 (G5). */
        b.video = 0x58000000u;
        b.stack = 0x68000000u;
        b.spu = 0x78000000u;
        b.spu_size = 0x20000000u;
        b.user_bound = 0x58000000u;
        b.compact = true;
    } else {
        b.video = 0xC0000000u;
        b.stack = 0xD0000000u;
        b.spu = 0xE0000000u;
        b.spu_size = 0x20000000u;
        b.user_bound = 0xC0000000u;
        b.compact = false;
    }
    return b;
}

}  // namespace

const RegionBases& region_bases() {
    static const RegionBases b = choose();
    return b;
}

uint64_t map_span() {
    const RegionBases& b = region_bases();
    return uint64_t(b.spu) + b.spu_size;
}

}  // namespace vm
