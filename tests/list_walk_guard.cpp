/* ps3base -- the list-walk integrity guard, tested.
 *
 * f_0064c044 walks a chain: node = table[obj+36]-32, then node = ld32[node+32]-32,
 * calling f_004e2ac0(ld32[node+28]) for each node. When one node's +28 held a
 * CODE address (0x00721f30 in one dump, 0x00730100 in another) the walk
 * dereferenced it as an object and died inside ppu::ld32 with a fault two
 * levels down, in the wrong thread's frames -- readable only from a memory dump
 * after the fact.
 *
 * The guard reads the node before the call and reports which field is bad, for
 * which node, with both values. It runs only when PS3BASE_LIST_GUARD is set,
 * so a normal run is unchanged.
 *
 * This test covers the predicate that decides "this looks like a pointer to
 * code, not an object", which is the whole detection.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

/* The title's own text segment, from bin/mc/mc.elf's first LOAD: 0x10000 +
 * 0xb93688. A data pointer never lands here; an instruction word copied into a
 * pointer field always does. */
constexpr uint32_t TEXT_LO = 0x00010000;
constexpr uint32_t TEXT_HI = 0x00ba3688;

bool looks_like_code(uint32_t v) { return v >= TEXT_LO && v < TEXT_HI && (v & 3) == 0; }

/* A node must be readable and start with a plausible header. The observed nodes
 * all begin with 0x28, but the guard only requires that the node is mapped and
 * that its two fields are not code -- a stricter header test would reject
 * legitimate node types. */
enum class Verdict { Ok, Unmapped, FieldIsCode };

Verdict check_node(uint32_t node, uint32_t* bad_value = nullptr) {
    if (!node || !vm::range_mapped(node, 36)) return Verdict::Unmapped;
    const uint32_t f28 = vm::read32(node + 28);
    if (looks_like_code(f28)) {
        if (bad_value) *bad_value = f28;
        return Verdict::FieldIsCode;
    }
    const uint32_t f32 = vm::read32(node + 32);
    /* +32 may legitimately be 0 (end of chain) or node+32 (0x20 bias), which is
     * itself not code. A code word here is corruption too. */
    if (f32 && looks_like_code(f32) && f32 > 0x100) {
        if (bad_value) *bad_value = f32;
        return Verdict::FieldIsCode;
    }
    return Verdict::Ok;
}

uint32_t g_next = 0x30000000;
uint32_t alloc_node() {
    const uint32_t a = g_next;
    if (!vm::map(a, 0x10000)) return 0;
    g_next += 0x10000;
    return a + 0x80;
}

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* 1. A healthy node passes. */
    {
        const uint32_t n = alloc_node();
        vm::write32(n, 0x28);
        vm::write32(n + 28, n + 0x1000);   /* a data pointer */
        vm::write32(n + 32, 0);            /* end of chain */
        CHECK(check_node(n) == Verdict::Ok, "a healthy node must pass");
    }

    /* 2. The exact corruption from the crash dump: +28 holds 0x00721f30. */
    {
        const uint32_t n = alloc_node();
        vm::write32(n, 0x28);
        vm::write32(n + 28, 0x00721f30);
        vm::write32(n + 32, 0);
        uint32_t bad = 0;
        const Verdict v = check_node(n, &bad);
        std::fprintf(stderr, "  +28 = 0x00721f30 -> verdict %s, value 0x%08x\n",
                     v == Verdict::FieldIsCode ? "FieldIsCode" : "other", bad);
        CHECK(v == Verdict::FieldIsCode && bad == 0x00721f30,
              "+28 holding a code address must be reported with the value");
    }

    /* 3. The other observed value: +28 holds 0x00730100. */
    {
        const uint32_t n = alloc_node();
        vm::write32(n, 0x28);
        vm::write32(n + 28, 0x00730100);
        CHECK(check_node(n) == Verdict::FieldIsCode, "+28 = 0x00730100 must be reported");
    }

    /* 4. +32 corrupted with a code word must be reported too. */
    {
        const uint32_t n = alloc_node();
        vm::write32(n, 0x28);
        vm::write32(n + 28, n + 0x1000);
        /* A code POINTER is word-aligned; 0x004bff92 is not, so the guard would
         * ignore it -- and rightly, since no instruction address is unaligned.
         * The value to test is an aligned text address. */
        vm::write32(n + 32, 0x004bff90);
        CHECK(check_node(n) == Verdict::FieldIsCode, "+32 holding a code word must be reported");
    }

    /* 5. An unmapped node is reported, not dereferenced. */
    {
        CHECK(check_node(0x7f000000) == Verdict::Unmapped, "an unmapped node must be reported, not read");
        CHECK(check_node(0) == Verdict::Unmapped, "a null node must be reported");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
