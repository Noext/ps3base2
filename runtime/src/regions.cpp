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
        /* Close the 1792 MB hole: video/stack/spu follow the user regions. */
        b.video = 0x50000000u;
        b.stack = 0x60000000u;
        b.spu = 0x70000000u;
        b.spu_size = 0x20000000u;
        b.user_bound = 0x50000000u;
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
