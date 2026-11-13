#include <cstdint>
#include <cstddef>
#include <cstdio>

/* One symbol the generic ps3base runtime needs and no God of War module provides.
 *
 * spu/thread.cpp reads edge::gpu::g_active before routing a DMA transfer through
 * GOWA's edge accelerator. spu/edge/edge_gpu.cpp defines it, but that file also
 * pulls the whole edge GPU state (g_mode, the descriptor tables) and belongs to a
 * title we are not running, so it stays out of this build.
 *
 * The flag lives here instead, alone, in its own translation unit: earlier
 * attempts to add it to gowa_stubs.cpp collided with a parallel port that adds and
 * removes its own copy there, and the definitions fought each other.
 *
 * false means the transfer takes the ordinary path, which is what this title
 * wants -- it has no edge GPU to accelerate anything with.
 */
/* The WASM build only. On a native build spu/edge/edge_gpu.cpp defines
 * g_active (as `const bool`), so defining it here as well is a duplicate
 * symbol and a type mismatch; under emscripten that file is not compiled. */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace edge {
namespace gpu {
bool g_active = false;
}  // namespace gpu
}  // namespace edge
#endif

/* Diagnostic only: counts the first reads of an address that no mapped region
 * covers. Built with -DPS3BASE_LD_TRAP=1; never enabled in a release build. */
#if PS3BASE_LD_TRAP
#include <cstdio>
#include "runtime/memory.h"
extern "C" int ps3base_ld_probe(uint32_t ea) {
    static int n = 0;
    if (n > 12) return 0;
    if (!vm::is_mapped(ea)) {
        n++;
        std::fprintf(stderr, "[unmapped] ld32 at 0x%08x (unmapped read #%d)\n", ea, n);
    }
    return 0;
}
#endif

#if PS3BASE_BLOCK_TRACE
/* Reports control-flow decisions made while the video-initialisation function is
 * running. The lr identifies the caller; the address range narrows it to
 * f_002da36c (mc_028, 0x002da36c..0x002dac00) whose labels decide whether
 * cellVideoOutConfigure is reached. */
#include <cstdio>
/* The trace is global -- every branch of every lifted function passes through
 * bc_cond. What matters is the decisions taken while ONE function runs, so the
 * window is opened by a guard on its entry and closed by the next entry of any
 * other function. Without the window the output is tens of thousands of lines
 * that say nothing about the function under study. */
#include "runtime/image.h"
extern bool g_bt_on;
extern "C" void* g_bt_ctx;

extern "C" void ps3base_block_trace_at(uint32_t pc, uint32_t bo, uint32_t bi, bool taken) {
    static int n = 0;
    /* Filter here, not at the source: touching the dispatch path to open a window
     * broke the boot (round 214), so the trace stays global and the sink decides.
     * The caller is identified by the thread's current lr, which the runtime
     * publishes for this purpose. */
    extern uint32_t g_bt_lr;
    /* Count every branch, and separately every bi==1, so "the filter sees nothing"
     * and "the branch is not executed" stop being the same observation. */
    {
        extern uint32_t g_bt_all, g_bt_bi1;
        g_bt_all++;
        if (bi == 1) g_bt_bi1++;
        if (g_bt_all == 1000 || g_bt_all == 10000 || g_bt_all == 50000)
            std::fprintf(stderr, "[btall] branches=%u bi1=%u\n", g_bt_all, g_bt_bi1);
    }
    /* Attribute by the BLOCK ADDRESS now that the lifter emits it. The lr was the
     * caller of the dispatcher, so every callee entered from the watched range
     * passed the filter and the trace mixed functions. */
    /* Round 223 established that every dependency of _cellGcmInitBody succeeds,
     * so 0x80010004 is computed inside the body. Watch the body's own branches:
     * the path that reaches the error return names the check that fails. */
    /* f_330005ec is the callee that writes [r30+0]/[r30+4]. Watch ITS branches and
     * print r30: a guard on its entry breaks the boot (three times measured), but
     * bc_cond_at fires inside it without touching the dispatch path. */
    const bool kWant = pc >= 0x330005ecu && pc < 0x3300070cu;
    if (kWant && n <= 60) {
        /* The first five branches form a loop. If one of its counters never
         * advances, the loop cannot end and branch 6 is never reached. Print the
         * registers those branches use and increment. */
        const uint64_t* g = static_cast<const uint64_t*>(g_bt_ctx);
        std::fprintf(stderr, "[regs] n=%d pc=0x%x bo=%u bi=%u t=%d r30=0x%llx\n",
                     n, pc, bo, bi, taken ? 1 : 0, (unsigned long long)g[30]);
    }
    if (!kWant || n > 400) return;
    n++;
    /* The ordinal, not an address: the lifted code sets ctx.cia only before a
     * syscall, so a branch has no address to report. What the comparison with
     * native needs is the SEQUENCE of decisions -- the first place the two
     * diverge names the faulty condition by its position. */
    std::fprintf(stderr, "[blk] seq=%d bo=%u bi=%u taken=%d\n", n, bo, bi, taken ? 1 : 0);
}
#endif

#if PS3BASE_BLOCK_TRACE
bool g_bt_on = false;
#endif

#if PS3BASE_BLOCK_TRACE
extern "C" uint32_t g_bt_all = 0;
extern "C" uint32_t g_bt_bi1 = 0;
extern "C" void ps3base_bt_report() {
    std::fprintf(stderr, "[btall] branches=%u bi1=%u\n", g_bt_all, g_bt_bi1);
}
#endif

/* Every guest store goes through rsv::store_range. Watching one address there says
 * whether anything at all writes it -- which is the last open question about the
 * null field at 0x00d72800. */
__attribute__((weak)) extern "C" void ps3base_store_probe(uint32_t ea, const void* data, size_t size) {
    /* The GCM context at 0xd72800 has callback filled but begin/end/current null.
     * Watch the three null fields specifically: knowing whether anything writes
     * them separates "the code was never lifted" from "our runtime provides no
     * path that does it". */
    /* The body's context argument (r6) is 0x40100000, and its callee writes
     * [r30+0] and [r30+4]. Watch the io buffer as well as the game structure. */
    /* The crashing load is ld32(0xd72800 + 10240) = ld32(0xd75000). That address was
     * never watched: rounds 220-238 observed 0xd72800 instead. */
    /* The crash path reads ld32(r31+12) = callback, then ld32(callback+0) as the
     * call target. callback is 0x00bcf278, so watch that too. */
    if (!((ea >= 0x00bcf270u && ea < 0x00bcf2b0u) ||
          (ea >= 0x00d72800u && ea < 0x00d72840u) ||
          (ea >= 0x00d74f00u && ea < 0x00d75100u) ||
          (ea >= 0x40100000u && ea < 0x40100040u))) return;
    static int n = 0;
    if (n > 40) return;
    n++;
    const uint8_t* d = static_cast<const uint8_t*>(data);
    uint32_t v = 0;
    for (size_t i = 0; i < size && i < 4; i++) v |= uint32_t(d[i]) << (8 * i);
    std::fprintf(stderr, "[d7store] ea=0x%08x size=%zu v=0x%08x\n", ea, size, v);
}


/* The linker turns every undefined symbol into an aborting trampoline, which
 * surfaces as a bare "unreachable" with no name -- the trap documented in
 * docs/webassembly. These are God-of-War-only hooks the Minecraft port links but
 * never calls usefully; provide inert definitions so a missing one cannot abort.
 * Signature-agnostic where the type does not matter. */
/* ppu::Context is named in the signatures below, so its header has to be
 * included rather than assumed: without it the native build fails with
 * "use of undeclared identifier ppu" on every hook. */
#include "ppu/context.h"
/* WASM ONLY. On a native build jobsys/edge_consumer.cpp, jobsys/sched.cpp,
 * jobsys/wws.cpp, jobsys/jobs_graph.cpp and jobsys/jobs_inplace.cpp define
 * every symbol below, so defining them here as well is a duplicate symbol at
 * link time. Under emscripten those files are not compiled and these inert
 * definitions are the only ones -- which is the whole point of this file,
 * and the reason the linker otherwise turns each missing symbol into an
 * aborting trampoline.
 */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace jsedge {
bool enabled() { return false; }
/* Arrow points the SPU reader at this hook; returning false means "no work was
 * donated", which is what an inert port must say. */
bool spu_idle(unsigned) { return false; }
void init() {}
void set_prev_guard(bool (*)(ppu::Context&)) {}
bool on_wait_previous(ppu::Context&) { return false; }
bool on_set_priorities(ppu::Context&) { return false; }
}  // namespace jsedge
namespace jobsys {
bool spu_idle_donate(unsigned) { return false; }
namespace wws {
using ppu_hook = bool (*)(ppu::Context&);
using installer = void (*)(unsigned, ppu_hook);
void install(installer) {}
}  // namespace wws
}  // namespace jobsys
namespace wj {
using ppu_hook = bool (*)(ppu::Context&);
using installer = void (*)(unsigned, ppu_hook);
void install(installer) {}
void install_inplace() {}
}  // namespace wj
#endif
/* Native builds already have these: spu/edge/edge_gpu.cpp defines
 * edge::gpu::on_put and runtime/src/crash_posix.cpp defines the crash
 * hooks, so defining them here as well is a duplicate symbol. Under
 * emscripten those two files are not linked and the stubs are the only
 * definitions -- which is what this file is for. */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
namespace edge {
namespace gpu { void on_put(unsigned char*, unsigned, unsigned, unsigned) {} }
namespace ovl {
enum class Counter : int;
unsigned long long count(Counter, unsigned long long) { return 0; }
}
}  // namespace edge
namespace crash {
void profile_thread(void*, const char*) {}
void log_here(const char*) {}
}  // namespace crash
#endif
extern "C" {
}
