/* Two different blocks at the SAME local-store address must both be usable.
 *
 * On SPU this is normal, not pathological: several SPU tasks overlay their own
 * code at the same LSA, so the local store at address X holds different
 * instructions at different moments. A table keyed by address alone would keep
 * one and silently fall back to the interpreter for the other -- which is
 * exactly the failure this campaign chased for a whole turn at lsa 0a590.
 *
 * The table is a map from LSA to a LIST of blocks, and aot::find() compares the
 * bytes at that address to pick the right one. This test asserts both halves.
 */
#include "spu/aot.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace spu {
namespace gen {
bool block_a(Context&, Host&) { return true; }
bool block_b(Context&, Host&) { return true; }
}  // namespace gen

/* The registry normally comes from generated code; here we supply two blocks at
 * one address so the lookup path is exercised directly. */
static const uint32_t kCodeA[] = {0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u};
static const uint32_t kCodeB[] = {0x10000000u, 0x20000000u, 0x30000000u, 0x40000000u};

static const CompiledBlock kBlocks[] = {
    {0x2000u, 4u, kCodeA, gen::block_a, false, nullptr},
    {0x2000u, 4u, kCodeB, gen::block_b, false, nullptr},
    {0x2000u, 2u, kCodeA, gen::block_a, false, nullptr},  // a SHORTER block, same address
    {0x3000u, 4u, kCodeB, gen::block_b, false, nullptr},
};

const CompiledBlock* compiled_blocks(size_t& count) {
    count = sizeof(kBlocks) / sizeof(kBlocks[0]);
    return kBlocks;
}
}  // namespace spu

static int failures = 0;
static int checks = 0;

static void check(bool ok, const char* what) {
    checks++;
    if (!ok) {
        failures++;
        std::printf("FAIL: %s\n", what);
    }
}

int main() {
    std::vector<uint8_t> ls(spu::LS_SIZE, 0);

    // Both variants live at 0x2000 in turn.
    std::memcpy(ls.data() + 0x2000, spu::kCodeA, sizeof spu::kCodeA);
    const spu::CompiledBlock* a = spu::aot::find(ls.data(), 0x2000);
    check(a != nullptr, "variant A at a shared LSA is found");
    // find() returns the FIRST block whose bytes match, in table order. Both the
    // 4-word and the 2-word entry at this address are prefixes of A's code, so
    // the 4-word one (declared first) wins. That is a deliberate rule: a longer
    // compiled block covers more instructions, so first-match on declared order
    // gives the most code, and the caller's version check keeps it honest.
    check(a == &spu::kBlocks[0], "variant A resolves to the first declared match");
    check(a->code == spu::kCodeA, "variant A returns variant A's code");

    std::memcpy(ls.data() + 0x2000, spu::kCodeB, sizeof spu::kCodeB);
    const spu::CompiledBlock* b = spu::aot::find(ls.data(), 0x2000);
    check(b != nullptr, "variant B at the SAME LSA is found after A");
    check(b != nullptr && b->code == spu::kCodeB, "variant B returns variant B's code");
    check(a != b, "the two variants are distinct blocks, not one");

    // A third content at the same address matches nothing.
    uint32_t other[4] = {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu};
    std::memcpy(ls.data() + 0x2000, other, sizeof other);
    check(spu::aot::find(ls.data(), 0x2000) == nullptr, "unknown content at a known LSA matches nothing");

    // A different LSA with the same bytes must not alias.
    std::memcpy(ls.data() + 0x3000, spu::kCodeA, sizeof spu::kCodeA);
    check(spu::aot::find(ls.data(), 0x3000) == nullptr, "same bytes at another LSA do not alias");

    // Longer than the remaining local store: rejected, not read out of bounds.
    std::memcpy(ls.data() + spu::LS_SIZE - 8, spu::kCodeB, 8);
    check(spu::aot::find(ls.data(), spu::LS_SIZE - 8) == nullptr, "a block running past the end of LS is rejected");

    std::printf("spu_aot_variants: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
