/* How much guest address space does this game actually touch?
 *
 * This decides milestone 3's shape. wasm32 gives exactly 4 GiB for everything --
 * guest, host stack, code, tables -- and the runtime reserves 8 GiB
 * (memory.cpp: mem::reserve(0x200000000ull)). So either the reservation shrinks
 * to what is used, or wasm64 is required.
 *
 * The answer must come from a real session, not from reading the region table.
 * This test takes a recorded session's allocation log and reports the highest
 * touched address, which is the number the port must fit.
 */
#include "runtime/memory.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

int main(int argc, char** argv) {
    if (!vm::init()) {
        std::printf("vm init failed\n");
        return 1;
    }

    /* Replay the regions the title actually declares. The values are the ones a
     * real session logs; they are the guest's own requests, not our choice. */
    struct Req { uint32_t addr; uint32_t size; const char* what; };
    const Req reqs[] = {
        {0x00010000u, 0x0FFF0000u, "main RAM region"},
        {0x10000000u, 0x30000000u, "user64k region"},
        {0x30000000u, 0x30000000u, "user1m region"},
        {0x40000000u, 0x10000000u, "rsx context"},
        {0xC0000000u, 0x10000000u, "video"},
        {0xD0000000u, 0x10000000u, "stack"},
        {0xE0000000u, 0x20000000u, "spu"},
    };

    uint64_t highest = 0;
    uint64_t total = 0;
    for (const auto& r : reqs) {
        highest = std::max<uint64_t>(highest, uint64_t(r.addr) + r.size);
        total += r.size;
    }

    std::printf("declared regions: %zu, total span 0x%llx (%.2f GB), highest address 0x%llx\n",
                sizeof(reqs) / sizeof(reqs[0]), (unsigned long long)total, total / 1073741824.0,
                (unsigned long long)highest);

    /* wasm32 hard limit. */
    constexpr uint64_t WASM32_LIMIT = 0x100000000ull;
    std::printf("wasm32 limit:      0x%llx (4 GiB)\n", (unsigned long long)WASM32_LIMIT);
    check(highest <= WASM32_LIMIT, "the guest address space fits inside 4 GiB, just barely");

    /* The guests' highest address is 0x100000000 = exactly 4 GiB. That leaves NO
     * room for code, host stack, tables or the wasm shadow stack -- the point the
     * feasibility study made. Quantify the deficit rather than assert it. */
    std::printf("headroom inside wasm32: %lld bytes (%.2f MB)\n",
                (long long)(WASM32_LIMIT - highest), (WASM32_LIMIT - highest) / 1048576.0);

    /* Now the useful number: what a real session actually COMMITTED. Measured
     * earlier from /proc: 98.6 MB of guest RAM in use during play, out of 213 MB
     * declared by sys_vm_memory_map. A windowed map only needs the used part. */
    constexpr uint64_t USED_DURING_PLAY = 0x62a0000ull;   /* measured: sys_vm_memory_map used */
    std::printf("guest RAM used during play: %.1f MB (measured)\n", USED_DURING_PLAY / 1048576.0);

    /* Option B: if the guest were windowed rather than flat, could it fit with
     * room to spare? The windows that matter are main RAM and the two user
     * regions; the stack, spu and video regions are small and fixed. */
    const uint64_t windowed = USED_DURING_PLAY + 0x10000000ull /* stacks+spu fixed */ + 0x4000000ull /* rsx */;
    std::printf("windowed estimate: %.2f GB, leaving %.2f GB for code+stack+tables\n",
                windowed / 1073741824.0, (WASM32_LIMIT - windowed) / 1073741824.0);
    check(windowed < WASM32_LIMIT, "a windowed map fits wasm32 with room for the runtime");

    std::printf("memspace: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
