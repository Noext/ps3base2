#pragma once

/* Guest memory backing, behind one interface so the runtime can be built
 * without mprotect and without POSIX signals.
 *
 * Two implementations:
 *   os      -- the original: mmap(PROT_NONE) + mprotect per page. Linux/Windows.
 *   bitset  -- no system protection at all: one flat buffer plus a bitmap of
 *              committed pages. This is what a WebAssembly build must use,
 *              because wasm has no mprotect and no per-page protection.
 *
 * Selected with PS3BASE_MEMORY=os|bitset; default os, so nothing changes unless
 * asked.
 *
 * The bitset mode is not merely a fallback. It makes the commit state explicit
 * and inspectable, which is what lets the same tests run against both and prove
 * the abstraction is faithful.
 */
#include <cstddef>
#include <cstdint>

namespace mem {

enum class Mode { Os, Bitset };

/* Chosen once from PS3BASE_MEMORY; everything else reads it. */
Mode mode();
const char* mode_name();

/* Reserve the whole guest address space. Returns the base, or null. */
uint8_t* reserve(size_t size);

/* Commit / decommit a range. In bitset mode these only flip bitmap bits; the
 * memory is already mapped and zero-filled on first reserve. */
bool commit(uint8_t* base, size_t offset, size_t size);
bool decommit(uint8_t* base, size_t offset, size_t size);

/* Change protection. In bitset mode only Prot::None matters: it records that the
 * range must not be touched, and the software guard checks it. */
enum class Prot { None, Read, ReadWrite };
bool protect(uint8_t* base, size_t offset, size_t size, Prot prot);

/* Bitset-mode query: is this page committed? Always true in os mode, where the
 * kernel is the authority and a fault answers instead. */
bool page_committed(size_t page_index);

/* Number of committed bytes, for the run-end summary. */
size_t committed_bytes();

/* Guard ranges. In os mode the CPU enforces Prot::None; in bitset mode nothing
 * does, so the ranges are kept here and checked by check_guard() on every guest
 * stack push region we can observe. */
void register_guard(size_t offset, size_t size);
/* True if the offset falls in a guard. Fills *guard_start when it does. */
bool in_guard(size_t offset, size_t* guard_start);

/* Reset for tests. */
void reset_for_tests();

}  // namespace mem
