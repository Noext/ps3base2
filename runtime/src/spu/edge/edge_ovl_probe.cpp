#include <cstdlib>

/* edge::ovl::stats_on() -- a pure predicate, extracted for the same reason
 * gpu::rsx::special_methods() was: the guest calls it during SPU thread startup,
 * long before any draw or job, but its definition lives in edge_ovl.cpp, which
 * includes edge_ovl_gen.h. That header is produced by tests/edge/ovl_cgen.py from
 * a *different title's* ELF (bin/01.12/GOWA.patched.elf), which this checkout does
 * not and should not contain.
 *
 * Being a getenv test with no dependency on the generated data, it can stand
 * alone. In wasm getenv returns null (see docs/webassembly/M3-NULL-VTABLE.md), so
 * the default -- stats on -- is what a browser build sees.
 */

#if defined(PS3BASE_EDGE_NO_CODEGEN)
namespace edge {
namespace ovl {

bool stats_on() {
    static const bool on = [] {
        const char* e = std::getenv("GOWA_EDGE_STATS");
        return !(e && e[0] == '0');
    }();
    return on;
}

}  // namespace ovl
}  // namespace edge
#endif
