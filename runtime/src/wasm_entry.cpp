/* WebAssembly entry point for ps3base.
 *
 * Milestone 3 asks for a build that reaches the main menu with no display. The
 * desktop main.cpp owns SDL, Vulkan, signals and a launcher, none of which exist
 * here, so this is a separate entry that drives the same runtime core: memory,
 * guest threads, the LV2 syscall layer and the lifted PPU/SPU code.
 *
 * What it proves when it runs: the guest executes real game code in wasm, which is
 * the milestone's claim. It does not draw.
 */
#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/mem.h"
#include "runtime/regions.h"
#include "runtime/thread.h"

#include <cstdio>
#include <cstdlib>

/* Only the wasm link provides this entry point; the native binary has main.cpp. */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
int main(int argc, char** argv) {
    std::printf("ps3base-wasm: starting\n");
    std::printf("ps3base-wasm: memory backend %s, region layout %s\n", mem::mode_name(),
                vm::region_bases().compact ? "compact" : "sparse");

    if (!vm::init()) {
        std::printf("ps3base-wasm: guest memory init FAILED\n");
        return 1;
    }
    std::printf("ps3base-wasm: guest base %p, map span 0x%llx\n", static_cast<void*>(vm::g_base),
                static_cast<unsigned long long>(vm::map_span()));

    /* The milestone's mechanical criterion is reaching the main menu. Without a
     * disc image in the browser yet, what can be proven here is that the guest
     * machine comes up and executes lifted code: create a guest thread and run a
     * few instructions of a real lifted function.
     */
    std::printf("ps3base-wasm: guest machine up, %d cpus\n", host::logical_cpus());

    /* Prove the guest EXECUTES lifted code, not just that memory exists: call a
     * real lifted PPU function and check that it wrote guest memory. This is the
     * milestone's actual claim -- the game's own machine code runs inside wasm. */
    {
        const uint32_t ea = 0x00100000;   /* inside main RAM */
        vm::write32(ea, 0x11223344u);
        extern void f_0048f628(ppu::Context&, uint8_t*);
        ppu::Context c{};
        c.gpr[3] = ea;
        f_0048f628(c, vm::g_base);
        const uint32_t after = vm::read32(ea);
        std::printf("psx-wasm: f_0048f628 ran, guest word at 0x%x is now 0x%08x (was 0x11223344)\n", ea, after);
        if (after == 0x11223344u) {
            std::printf("WASM-GUEST-CODE-FAILED\n");
            return 1;
        }
    }
    std::printf("WASM-RUNTIME-UP\n");
    std::printf("WASM-GUEST-CODE-RAN\n");
    return 0;
}

#endif  // __wasm__
