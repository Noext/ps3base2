/* ps3base -- cellGameExec's single import, tested against the reference.
 *
 * The title imports exactly one function from the module:
 *   cellGameExec::cellGameGetHomeDataExportPath (NID 0x59B1EDE1)
 *
 * The property that matters is that it NEVER returns CELL_OK. The previous
 * state of this tree resolved the import nowhere, and the generic fallback
 * wrote r3 = 0, so the title was told an export path had been produced while
 * its buffer still held whatever was there before. RPCS3 returns
 * CELL_GAME_ERROR_NOAPP for every call, with the comment "PlayStation home is
 * defunct" (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGameExec.cpp:20-31).
 *
 * This test includes the module and calls the handler the import path calls,
 * with a real guest context and real guest memory, so the "not zero" property
 * is checked on the value that actually lands in r3.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../runtime/src/hle/cellGameExec.cpp"

/* hle::Reg normally registers into the import table (runtime/src/ppu_runtime.cpp).
 * This test calls the handler directly, so the constructor records the key in a
 * local map instead of pulling in the whole PPU runtime. Same trick as
 * tests/spurs.cpp, kept a map so the registration NAME is checked too -- the
 * dispatcher builds "module::name" and looks it up, so a typo there makes the
 * import resolve nowhere and the handler never run. */
namespace {
/* Function-local, not a namespace-scope object: the static hle::Reg instances
 * inside the included module run BEFORE any namespace-scope object declared
 * after it, so a file-scope map would be used before it was constructed --
 * measured as a segfault at startup. */
std::map<std::string, hle::Fn>& test_registry() {
    static std::map<std::string, hle::Fn> m;
    return m;
}
}

namespace hle {
Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

uint32_t g_next = 0x20000000;

uint32_t alloc_area(uint32_t size = 0x1000) {
    const uint32_t a = g_next;
    if (!vm::map(a, size)) return 0;
    g_next += size;
    return a;
}

/* Call the handler as the import path does: r3.. are the arguments. */
int64_t call(ppu::Context& ctx, void (*fn)(ppu::Context&, uint8_t*), uint64_t a0) {
    ctx = ppu::Context{};
    ctx.gpr[3] = a0;
    fn(ctx, vm::g_base);
    return static_cast<int64_t>(ctx.gpr[3]);
}

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    ppu::Context ctx;
    const uint32_t buf = alloc_area(128);
    CHECK(buf != 0, "guest memory for the export-path buffer");
    if (!buf) return 1;

    /* A cellGame error is a NEGATIVE s32, and the handler sign-extends it into
     * r3 (ret() takes int64_t; RPCS3's ppu_gpr_cast does the same). So the
     * comparison is on the low 32 bits as a signed value, not on a positive
     * 64-bit literal. */
    auto low32 = [](int64_t v) { return static_cast<int32_t>(v & 0xFFFFFFFFll); };

    /* A null pointer is PARAM, before anything else: cellGameExec.cpp:24-27. */
    const int64_t nul = call(ctx, hle::hle_cellGameExec_cellGameGetHomeDataExportPath, 0);
    CHECK(low32(nul) == static_cast<int32_t>(0x8002cb07),
          "a NULL exportPath returns CELL_GAME_ERROR_PARAM (0x8002cb07)");

    /* A real pointer is NOAPP, and the buffer is left ALONE. */
    std::memset(vm::ptr(buf), 0x5A, 128);
    const int64_t rc = call(ctx, hle::hle_cellGameExec_cellGameGetHomeDataExportPath, buf);
    CHECK(low32(rc) == static_cast<int32_t>(0x8002cb08),
          "a valid exportPath returns CELL_GAME_ERROR_NOAPP (0x8002cb08)");
    CHECK(rc != 0, "the handler NEVER returns CELL_OK -- Home is defunct, so there is no path");
    CHECK(vm::ptr(buf)[0] == 0x5A,
          "the caller's buffer is untouched: no fabricated path was written");

    /* The registration key is the one the dispatcher builds from the import
     * table (runtime/src/ppu_runtime.cpp:85-89, "module::name"), and the
     * function it points at is the one called above. */
    const auto& reg = test_registry();
    const auto it = reg.find("cellGameExec::cellGameGetHomeDataExportPath");
    CHECK(it != reg.end(), "the handler is registered as cellGameExec::cellGameGetHomeDataExportPath");
    CHECK(it != reg.end() && it->second == &hle::hle_cellGameExec_cellGameGetHomeDataExportPath,
          "the registered handler is this function, not another slot");

    std::printf("cellgameexec: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
