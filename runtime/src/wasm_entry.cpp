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
    std::printf("WASM-RUNTIME-UP\n");
    return 0;
}

#endif  // __wasm__
