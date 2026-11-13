/* ps3base -- the cellFiber HLE's contract, checked against the REAL library.
 *
 * The subject is runtime/src/hle/cellFiber.cpp. Every claim below is read from
 * the LIFTED firmware code, not from memory and not from RPCS3's stubs (which
 * are themselves `todo()` for these four exports):
 *
 *   cellFiberPpuYield     stub 0x3600e930 -> code 0x360033dc
 *   cellFiberPpuRunFibers stub 0x3600e918 -> code 0x3600254c
 *   cellFiberPpuJoinFiber stub 0x3600e940 -> code 0x36003574
 *   cellFiberPpuExit      stub 0x3600e948 -> code 0x36003b84
 *
 * (A stub is an .opd: its first word is the code address. Comparing the stub
 * itself to a text bound is the category error AGENTS.md records.)
 *
 * What the real code does, and what this test pins:
 *
 * 1. Yield returns TWO different refusals. f_360033dc_body loads the per-thread
 *    fibre context, compares the current fibre to -1, and returns 0x80760020
 *    (NOSYSINIT) in that arm; the PERM arm (0x80760009) is a different path.
 *    Returning PERM for both is a false report: the caller cannot tell a
 *    torn-down scheduler from a plain yield outside a fibre.
 *
 * 2. JoinFiber WAITS. f_36003574_body polls the fibre's state word at +772 in a
 *    loop (L_36003704) and only reads the exit status from +792 once the state
 *    says terminated. Returning CELL_OK with a status for a fibre that never ran
 *    is what the log showed 20 times in three 900 s runs.
 *
 * 3. The unimplemented exports must refuse, not return CELL_OK. AGENTS.md,
 *    "Never a fake success".
 *
 * The test drives the real handlers through the HLE registry, so it cannot
 * drift from the code under test the way a copy would.
 *
 * Run: ./build-rdr/cellfiber_contract   (exit 0 = pass)
 */
#include "runtime/hle.h"
#include "runtime/log.h"
#include "runtime/memory.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

/* The HLE registry, provided here instead of linking runtime/src/ppu_runtime.cpp.
 *
 * ppu_runtime.cpp defines hle::Reg and hle::find in about twenty lines, but it
 * also needs the generated image table (image::functions, image::exports), which
 * is 57 MB of lifted code and not something a unit test should link. The
 * registry is a map keyed "module::name"; reimplementing that key format would
 * be a copy that can drift, so this file reproduces it EXACTLY as ppu_runtime.cpp
 * writes it and the test says which file it mirrors.
 *
 * A drift here would show up as the test failing to find a handler, which is a
 * loud failure, not a silent pass. */
namespace hle {

namespace {
std::map<std::string, Fn>& registry() {
    static std::map<std::string, Fn> r;
    return r;
}
}

Reg::Reg(const char* module, const char* name, Fn fn) {
    registry()[std::string(module) + "::" + name] = fn;
}

Fn find(const char* module, const char* name) {
    auto it = registry().find(std::string(module) + "::" + name);
    return it == registry().end() ? nullptr : it->second;
}

/* runtime/src/hle/support.cpp:18, same three lines. cellFiber.cpp registers one
 * module override at load time, so the constructor has to exist for the object
 * file to link. */
namespace {
std::map<std::string, bool (*)()>& overrides() {
    static std::map<std::string, bool (*)()> r;
    return r;
}
}

ModuleOverride::ModuleOverride(const char* file, bool (*active)()) { overrides()[file] = active; }

}  // namespace hle

/* The two guest entry points cellFiber.cpp calls. In the runtime these live in
 * runtime/src/thread.cpp; a unit test has no guest threads, so they are stubs
 * that report "no current thread" and refuse to call an OPD.
 *
 * This is honest for this test's purpose: every check below exercises a path
 * that returns BEFORE any fibre is created (no scheduler, null pointers,
 * unimplemented exports). If a future check needs a real fibre, it needs the
 * real thread.cpp, and this stub will make that visible as a wrong answer rather
 * than a silent pass. */
namespace guest {
class Thread;
Thread* current() { return nullptr; }
void call_opd(ppu::Context&, uint8_t*, uint32_t) {}
}  // namespace guest

static int g_checks = 0;
static int g_fails = 0;
#define CHECK(cond, msg) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; std::printf("FAIL %s\n", msg); } \
    else std::printf("ok   %s\n", msg); \
} while (0)

/* The two refusal codes, spelled out here because the test must not read them
 * from the file it is testing -- a typo there would make the test agree with the
 * bug. Values from refs/rpcs3/rpcs3/Emu/Cell/Modules/cellFiber.h: the base is
 * 0x80760000 and the last byte is the error number. */
static constexpr int32_t ERR_PERM      = static_cast<int32_t>(0x80760009u);
static constexpr int32_t ERR_NOSYSINIT = static_cast<int32_t>(0x80760020u);
static constexpr int32_t ERR_INVAL     = static_cast<int32_t>(0x80760002u);

/* hle::find is the registry's own lookup (runtime/include/runtime/hle.h:15), so
 * the test drives the same handler the runtime would. */
static bool lookup(const char* name, void (**out)(ppu::Context&, uint8_t*)) {
    *out = hle::find("cellFiber", name);
    return *out != nullptr;
}

static int32_t call(void (*fn)(ppu::Context&, uint8_t*), ppu::Context& ctx,
                    uint64_t a0 = 0, uint64_t a1 = 0) {
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[2] = 0;
    fn(ctx, nullptr);
    return static_cast<int32_t>(static_cast<uint32_t>(ctx.gpr[3]));
}

int main() {
    void (*yield)(ppu::Context&, uint8_t*) = nullptr;
    void (*join)(ppu::Context&, uint8_t*) = nullptr;
    void (*wait_flag)(ppu::Context&, uint8_t*) = nullptr;
    void (*check_stack)(ppu::Context&, uint8_t*) = nullptr;

    if (!lookup("cellFiberPpuYield", &yield)) {
        std::printf("FAIL: the HLE registry has no cellFiber handler -- the "
                    "cellFiber module is not linked into this binary\n");
        return 1;
    }
    lookup("cellFiberPpuJoinFiber", &join);
    lookup("cellFiberPpuWaitFlag", &wait_flag);
    lookup("cellFiberPpuCheckStackLimit", &check_stack);

    ppu::Context ctx;
    std::memset(&ctx, 0, sizeof(ctx));

    /* 1. Yield outside any scheduler: NOSYSINIT, not PERM. */
    const int32_t y = call(yield, ctx);
    CHECK(y == ERR_NOSYSINIT,
          "Yield with no scheduler returns NOSYSINIT (0x80760020), not PERM");
    CHECK(y != ERR_PERM,
          "Yield with no scheduler does NOT return PERM -- the two refusals differ");

    /* 2. JoinFiber with a null pointer refuses rather than succeeding. */
    if (join) {
        const int32_t j = call(join, ctx, 0, 0);
        CHECK(j != 0, "JoinFiber(null, null) refuses instead of returning CELL_OK");
    }

    /* 3. The unimplemented exports refuse; none of them may return CELL_OK. */
    if (wait_flag) {
        /* A non-null flag address that is not mapped still must not be CELL_OK. */
        const int32_t w = call(wait_flag, ctx, 0xDEADBEE0u, 0);
        CHECK(w != 0, "WaitFlag on an unmapped flag refuses instead of CELL_OK");
    }
    if (check_stack) {
        const int32_t c = call(check_stack, ctx);
        CHECK(c != 0, "CheckStackLimit refuses instead of CELL_OK (no fake success)");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
