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
namespace edge {
namespace gpu {
bool g_active = false;
}  // namespace gpu
}  // namespace edge

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
extern "C" void ps3base_block_trace(uint32_t bo, uint32_t bi, bool taken) {
    static int n = 0;
    if (!g_bt_on || n > 400) return;
    n++;
    std::fprintf(stderr, "[blk] bo=%u bi=%u taken=%d\n", bo, bi, taken ? 1 : 0);
}
#endif

#if PS3BASE_BLOCK_TRACE
bool g_bt_on = false;
#endif
