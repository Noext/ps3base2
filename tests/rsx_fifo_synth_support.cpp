/* Symbols the runtime references that a single-purpose harness does not pull
 * in. None of them is exercised by the FIFO path; they exist so tests/rsx_fifo_
 * synth.cpp can link the REAL runtime/src/lv2/rsx.cpp -- the FIFO reader and the
 * method handlers -- instead of a copy of it. Every one of these is a no-op or a
 * trivial answer, and each says which property it stands in for.
 */
#include "ppu/context.h"
#include "runtime/image.h"
#include "runtime/kernel.h"
#include "runtime/log.h"
#include "runtime/memory.h"

#include <cstdio>

/* hle::gcm_flip_handler: the guest function cellGcmSetFlipHandler stored. The real
 * definition is runtime/src/hle/cellGcmSys.cpp, which this harness does not link --
 * it would drag in the whole HLE module table for one word. Returning 0 is the
 * honest answer here: no guest code registered a handler in a synthetic FIFO run, so
 * flip_done() must take its "no handler" branch. This is exactly what the game did
 * before commit 521670c fixed the registration, so the test still exercises the
 * correct path. */
namespace hle {
uint32_t gcm_flip_handler() { return 0; }
}  // namespace hle

/* force: the RDR save/load poke injection. Not part of the RSX path. */
namespace force {
void on_return(uint32_t, uint32_t, uint64_t&) {}
void on_call(uint32_t) {}
void tick() {}
void notify_save_done() {}
}  // namespace force

/* guest: no PPU threads run in this harness, so "the current thread" is none.
 * syscall() checks for null and skips its per-thread bookkeeping. */
namespace guest {
struct Thread;
Thread* current() { return nullptr; }
}  // namespace guest

namespace stacks {
void note_sp(uint32_t, uint32_t) {}
}  // namespace stacks

/* The store probe is a diagnostic hook on rsv::store_range that watches four
 * hard-coded God-of-War addresses (runtime/src/ps3base_wasm_stubs.cpp:111). Not
 * this title, not this test: inert. */
extern "C" void ps3base_store_probe(uint32_t, const void*, size_t) {}

namespace cbwatch {
void note_import(const char*, const char*, uint64_t, uint32_t, bool) {}
}  // namespace cbwatch

namespace loader {
bool image_loaded(int) { return false; }
}  // namespace loader

/* The image table: this harness has no lifted guest code, so the table is empty.
 * ppu_runtime keeps its dispatch empty and every syscall still routes by number,
 * which is the only part this test uses. */
namespace image {
const ImageEntry images[1] = {};
const FuncEntry functions[1] = {};
const ImportEntry imports[1] = {};
const ExportEntry exports[1] = {};
const uint32_t image_count = 0;
const uint32_t function_count = 0;
const uint32_t import_count = 0;
const uint32_t export_count = 0;
}  // namespace image

/* crash: the FIFO thread registers a stack sampler only when GOWA_PROFILE_FIFO
 * is set, which getenv makes impossible under wasm. */
namespace crash {
std::string sample_stack(void*, int) { return {}; }
void profile_thread(void*, const char*) {}
}  // namespace crash

