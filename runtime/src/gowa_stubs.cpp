// Generated: no-op stubs for the SPU job rewrites that exist only for this
// title. A different disc has no such handwritten natives, so their symbols
// are absent. Defining them as no-ops lets the CPU and kernel boot be
// measured; the SPU work they would perform is not done.
//
// Nothing already defined by the runtime or the generated code is redeclared:
// a duplicate would either fail the link or, for a TLS variable, corrupt it.
extern "C" int gowa_stub_0() __asm__("_ZN2pm5hooksERm");
extern "C" int gowa_stub_0() { return 0; }
extern "C" int gowa_stub_1() __asm__("_ZN2pm8run_hookERN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_1() { return 0; }
extern "C" int gowa_stub_2() __asm__("_ZN2pm9signatureEjRj");
extern "C" int gowa_stub_2() { return 0; }
extern "C" int gowa_stub_3() __asm__("_ZN2wj3runEiPhRN3spu7ContextERN6renjob8ChanHostEbPNS1_4HostE");
extern "C" int gowa_stub_3() { return 0; }
extern "C" int gowa_stub_4() __asm__("_ZN2wj4infoEi");
extern "C" int gowa_stub_4() { return 0; }
/* 64-bit unsigned, not unsigned long: under wasm32 a C++ `unsigned long long&`
 * mangles to Ry, while `unsigned long&` mangles to Rm. The stub carried the host
 * mangling, so the wasm link saw it as undefined and replaced it with an aborting
 * trampoline -- which the static initialisers called, producing a bare
 * "unreachable" before the first printf. */
/* BOTH manglings. `uint64_t` is `unsigned long long` under wasm32 (mangles to
 * Ry) and `unsigned long` under LP64 (mangles to Rm), and the real definition in
 * jobsys/jobs_whole.h is `uint64_t&`. Keeping only Ry links on wasm and fails the
 * native link with "undefined reference to wj::ext_stats(unsigned long&)";
 * keeping only Rm does the reverse. Two weak stubs cover both, and either is
 * dropped as soon as the real definition is linked. */
extern "C" int gowa_stub_5_ll() __asm__("_ZN2wj9ext_statsERy");
extern "C" int gowa_stub_5_ll() { return 0; }
extern "C" int gowa_stub_5_l() __asm__("_ZN2wj9ext_statsERm");
extern "C" int gowa_stub_5_l() { return 0; }
extern "C" int gowa_stub_6() __asm__("_ZN2wj9job_in_lsEPKh");
extern "C" int gowa_stub_6() { return 0; }
extern "C" int gowa_stub_7() __asm__("_ZN4anim5hooksERm");
extern "C" int gowa_stub_7() { return 0; }
extern "C" int gowa_stub_8() __asm__("_ZN4anim8run_hookERN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_8() { return 0; }
extern "C" int gowa_stub_9() __asm__("_ZN4anim9signatureEjRj");
extern "C" int gowa_stub_9() { return 0; }
extern "C" int gowa_stub_10() __asm__("_ZN4edge12image_loadedEPKh");
extern "C" int gowa_stub_10() { return 0; }
extern "C" int gowa_stub_11() __asm__("_ZN4edge3ovl10entry_nameEm");
extern "C" int gowa_stub_11() { return 0; }
extern "C" int gowa_stub_12() __asm__("_ZN4edge3ovl11entry_countEv");
extern "C" int gowa_stub_12() { return 0; }
extern "C" int gowa_stub_13() __asm__("_ZN4edge3ovl11note_interpEj");
extern "C" int gowa_stub_13() { return 0; }
extern "C" int gowa_stub_14() __asm__("_ZN4edge3ovl13count_declineEi");
extern "C" int gowa_stub_14() { return 0; }
extern "C" int gowa_stub_15() __asm__("_ZN4edge3ovl3runERN3spu7ContextERNS1_4HostEPKNS0_5EntryE");
extern "C" int gowa_stub_15() { return 0; }
extern "C" int gowa_stub_16() __asm__("_ZN4edge3ovl4findEPKhj");
extern "C" int gowa_stub_16() { return 0; }
extern "C" int gowa_stub_17() __asm__("_ZN4edge3ovl5countENS0_7CounterEm");
extern "C" int gowa_stub_17() { return 0; }
extern "C" int gowa_stub_18() __asm__("_ZN4edge3ovl5entryEm");
extern "C" int gowa_stub_18() { return 0; }
extern "C" int gowa_stub_19() __asm__("_ZN4edge3ovl8index_ofEPKNS0_5EntryE");
extern "C" int gowa_stub_19() { return 0; }
extern "C" int gowa_stub_20() __asm__("_ZN4edge3ovl8stats_onEv");
extern "C" int gowa_stub_20() { return 0; }
extern "C" int gowa_stub_21() __asm__("_ZN4edge8adaptersERm");
extern "C" int gowa_stub_21() { return 0; }
extern "C" int gowa_stub_22() __asm__("_ZN5cloth12image_loadedEPKh");
extern "C" int gowa_stub_22() { return 0; }
extern "C" int gowa_stub_23() __asm__("_ZN5cloth8adaptersERm");
extern "C" int gowa_stub_23() { return 0; }
extern "C" int gowa_stub_24() __asm__("_ZN5mpjob3runEiRN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_24() { return 0; }
extern "C" int gowa_stub_25() __asm__("_ZN5mpjob4nameEi");
extern "C" int gowa_stub_25() { return 0; }
extern "C" int gowa_stub_26() __asm__("_ZN5mpjob5hooksEiRm");
extern "C" int gowa_stub_26() { return 0; }
extern "C" int gowa_stub_27() __asm__("_ZN5mpjob9signatureEijRj");
extern "C" int gowa_stub_27() { return 0; }
extern "C" int gowa_stub_28() __asm__("_ZN5tasks6imagesERm");
extern "C" int gowa_stub_28() { return 0; }
extern "C" int gowa_stub_29() __asm__("_ZN6renjob12image_loadedEPKh");
extern "C" int gowa_stub_29() { return 0; }
extern "C" int gowa_stub_30() __asm__("_ZN6renjob8adaptersERm");
extern "C" int gowa_stub_30() { return 0; }
extern "C" int gowa_stub_31() __asm__("_ZN6renjob9run_wholeEPhRN3spu7ContextERNS_8ChanHostE");
extern "C" int gowa_stub_31() { return 0; }
extern "C" int gowa_stub_32() __asm__("_ZN6scream8adaptersERm");
extern "C" int gowa_stub_32() { return 0; }
extern "C" int gowa_stub_33() __asm__("_ZN7edgejob5hooksERm");
extern "C" int gowa_stub_33() { return 0; }
extern "C" int gowa_stub_34() __asm__("_ZN7edgejob8run_hookERN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_34() { return 0; }
extern "C" int gowa_stub_35() __asm__("_ZN7edgejob9signatureEjRj");
extern "C" int gowa_stub_35() { return 0; }
extern "C" int gowa_stub_36() __asm__("_ZN7shadows12image_loadedEPKh");
extern "C" int gowa_stub_36() { return 0; }
extern "C" int gowa_stub_37() __asm__("_ZN7shadows8adaptersERm");
extern "C" int gowa_stub_37() { return 0; }
extern "C" int gowa_stub_38() __asm__("_ZN8meshhier5hooksERm");
extern "C" int gowa_stub_38() { return 0; }
extern "C" int gowa_stub_39() __asm__("_ZN8meshhier8run_hookERN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_39() { return 0; }
extern "C" int gowa_stub_40() __asm__("_ZN8meshhier9signatureEjRj");
extern "C" int gowa_stub_40() { return 0; }
extern "C" int gowa_stub_41() __asm__("_ZN8meshprep8adaptersERm");
extern "C" int gowa_stub_41() { return 0; }
extern "C" int gowa_stub_42() __asm__("_ZN9screamjob6imagesERm");
extern "C" int gowa_stub_42() { return 0; }
extern "C" int gowa_stub_43() __asm__("_ZN9screamjob8run_hookERN3spu7ContextERNS0_4HostE");
extern "C" int gowa_stub_43() { return 0; }
extern "C" int gowa_stub_44() __asm__("_ZNSolsEi");
extern "C" int gowa_stub_44() { return 0; }
extern "C" int gowa_stub_45() __asm__("_ZNSolsEj");
extern "C" int gowa_stub_45() { return 0; }

/* edge::gpu::g_active is NOT stubbed here.
 *
 * spu/edge/edge_gpu.cpp:131 already defines it (`const bool g_active = ...`) and
 * edge_gpu.h:35 declares it extern, so a second definition in this file is a
 * duplicate: "multiple definition of edge::gpu::g_active" at link time. It was
 * added here on the assumption that no edge GPU module is linked, but
 * GOWA_EDGE_SOURCES globs runtime/src/spu/edge/*.cpp into the binary, so one
 * always is. */

/* compiled_blocks() is the AOT table: the SPU blocks compiled to native code for
 * this title. ps3base links none of them (they are produced by a generator that
 * needs the title's own block layout), so the table is empty and the interpreter
 * runs every block instead. aot.cpp already handles n == 0 -- it just leaves the
 * index inactive. */
#include "spu/aot.h"
namespace spu {
const CompiledBlock* compiled_blocks(size_t& count) {
    count = 0;
    return nullptr;
}
}  // namespace spu
