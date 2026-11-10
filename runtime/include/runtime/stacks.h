#pragma once

/* Guest stack inventory and guard pages.
 *
 * The hypothesis under test: a crash node in the heap has its +28 field holding
 * an address INSIDE a function (0x0048f640, ~24 bytes past the nearest lifted
 * entry), which is what a return address looks like. A word shaped like a return
 * address landing in a heap node is what a stack overflow or a stack/heap
 * overlap produces -- a frame's return slot written past the end of a stack and
 * into a node, or a stack placed over memory the heap also hands out.
 *
 * So: record every guest stack (requested size, allocated size, range), put a
 * PAGE_GUARD of exact PROT_NONE memory under each one, and report the lowest
 * guess r1 ever reaches per thread. If a guard ever fires, that names the thread
 * and the instruction that ran off the stack.
 */
#include <cstddef>
#include <cstdint>

namespace stacks {

struct Entry {
    uint32_t id;          /* guest thread id */
    char name[32];
    uint32_t requested;   /* what the title asked for */
    uint32_t allocated;   /* what we actually reserved */
    uint32_t lo;          /* first usable address */
    uint32_t hi;          /* one past the last usable byte */
    uint32_t guard_lo;    /* PROT_NONE page(s) below lo */
    uint32_t guard_size;
    uint32_t r1_min;      /* lowest r1 seen for this thread */
    uint32_t calls;
};

/* Split ONE contiguous allocation into [guard 64 KB][stack], mark the guard
 * inaccessible and record the entry. Returns the stack base, or 0 on failure.
 *
 * The allocation must be contiguous: reserving the guard separately lands it
 * ABOVE the stack (vm::alloc skips past the previous block), leaving a hole the
 * overflow simply walks through. Measured on the first version of this file:
 * stack d0020000..d0028000, guard d0030000..d0040000, 0x8000 of nothing between. */
uint32_t split(uint32_t id, const char* name, uint32_t requested, uint32_t block_base, uint32_t block_size);

/* Drop the entry and release its guard. */
void remove(uint32_t id);

/* Note a guest r1 for a thread (cheap; called from the scheduler). */
void note_sp(uint32_t id, uint32_t r1);

/* True if the address falls inside a guest stack; fills out the owner name. */
bool contains(uint32_t ea, char* name, size_t n);

/* Log the whole inventory with the lowest r1 per thread. */
void dump();

}  // namespace stacks
