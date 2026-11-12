#pragma once

#include <atomic>
#include <cstdint>

/* PS3BASE_CODEWATCH: catch the store that puts a word from the title's own
 * .text into memory that is not the loader.
 *
 * Measured motivation (docs/findings/f-0064c044-list-walk.md): the crash in
 * f_0064c044 is a linked-list node whose +28 or +32 holds an *instruction word*
 * (0x00721f30, 0x00730100 -- both inside mc.elf's first LOAD, 0x10000..0xba3688)
 * instead of a heap pointer. That is a 4-byte store of code into data, and the
 * writer is what has to be named.
 *
 * Every guest-visible write funnels through rsv::store_range (all the lifted
 * ppu::st* helpers, vm::write*, every SPU MFC put), through
 * rsv::store_unconditional (SPU putlluc) and through ppu::notify_store (dcbz).
 * The check is one branch on a hot path and it sees the store at the
 * instruction that performs it. Callers that memcpy into vm::ptr() directly are
 * not covered; PS3BASE_CODEWATCH_SCAN=1 covers those with a periodic sweep.
 *
 * Off unless PS3BASE_CODEWATCH is set. Defaults to the title's code range;
 * PS3BASE_CODEWATCH_RANGES=lo-hi overrides it, PS3BASE_CODEWATCH_ADDR=ea
 * restricts reports to one 128-byte line, PS3BASE_CODEWATCH_HEAP_ONLY=1 (the
 * default) ignores stores outside the guest heap and stacks.
 */

namespace cw {

/* Default range is the EXECUTABLE part of mc.elf's first LOAD, not the whole
 * LOAD: 0x10200..0x8aa674 is code, while 0x8aa674..0xba3688 is read-only data
 * (string tables, vtables' targets). A pointer to rodata in a heap object is
 * ordinary; a word that is *inside the code* is not. The two measured corrupt
 * values, 0x00721f30 and 0x00730100, are both in this code range. */
extern std::atomic<uint32_t> g_lo;
extern std::atomic<uint32_t> g_hi;
extern std::atomic<bool> g_on;
extern std::atomic<uint32_t> g_line;

/* Guest context for a report. Registered by the runtime so the watch itself
 * links into the small test binaries that only need reservation.cpp. */
struct Context { uint32_t lr; uint32_t cia; const char* thread; };
extern Context (*current_context)();

/* Legal call targets: the lifted function table. A data word that equals one of
 * these is a function pointer (vtable, callback, OPD), which is ordinary. A data
 * word that is inside .text but is NOT a function entry is the signature this
 * watch is for -- 0x721f30 and 0x730100 are both ~85 KB past the nearest entry. */
void set_known_entries(const uint32_t* addrs, uint32_t count);

void report(uint32_t addr, uint32_t value, const char* how);
bool is_code(uint32_t value);

/* PS3BASE_CODEWATCH_ANY=1: report every text value written into the heap,
 * whatever it replaces. Measured to be useless on its own -- a first run
 * produced 60 hits before the world loaded, all small integers and colour words
 * that merely fall inside the text range (0x0003fe10, 0x00330000, 0x000166dc).
 * Kept for the case where the target address is known in advance. */
extern std::atomic<bool> g_any;

/* PS3BASE_CODEWATCH_AT=<seconds>: arm only after the title has finished
 * booting. Measured: the first 60 s legitimately install ~60 mid-function code
 * pointers (switch/jump tables) into heap structures, which is indistinguishable
 * from the corruption by value alone. */
extern std::atomic<double> g_arm_at;
extern std::atomic<uint32_t> g_cap;

/* The reported signature is the one the crash dump shows: a field that held a
 * HEAP POINTER now holds a CODE ADDRESS. Requiring that combination is what
 * makes the watch usable in a long run -- a store into never-used memory has no
 * pointer to replace. */
bool heap_pointer(uint32_t v);

/* Called with the guest address and value; reads the old word itself, so callers
 * must invoke it BEFORE performing the store. */
void check_word(uint32_t addr, uint32_t guest_word, const char* how);

/* Check a range of guest bytes about to be written, 4-byte aligned windows. */
inline void check_store(uint32_t addr, uint32_t size, const uint8_t* data, const char* how) {
    if (!g_on.load(std::memory_order_relaxed)) return;
    for (uint32_t i = 0; i + 4 <= size; i += 4) {
        uint32_t v;
        __builtin_memcpy(&v, data + i, 4);
        check_word(addr + i, __builtin_bswap32(v), how);
    }
}

/* Line-level watch, independent of the text-range test. Every store that
 * crosses a watched 128-byte line is logged with its caller, so the writer of a
 * field can be read out even when the value it wrote is legal (0, a small
 * integer). PS3BASE_CODEWATCH_NOTE=<ea> arms it at startup; note_line() arms it
 * at run time, which is how a sweep that spots the corrupt word turns the whole
 * subsequent history of that line into a log. */
extern std::atomic<bool> g_note_on;
/* Arms the 128-byte line containing ea. Up to 524288 lines (4 GB / 128) can be
 * armed; the test is one array read. */
void note_line(uint32_t ea);
void note_hit(uint32_t addr, uint32_t size, const char* how, uint32_t value);

/* Byte-precise watch of ONE guest range, armed from PS3BASE_BLOCKWATCH=lo-hi.
 *
 * The 128-byte line watch above answers "did anything touch this line", which is
 * not enough to name the field: the RDR block at 0x0177cc00 has ten interesting
 * pointers inside two lines, and the question is which of them the title writes
 * and with what. This one logs the offset inside the range, the width, the value
 * and the caller for every write that overlaps it, and it is on from t=0.
 *
 * Costs one compare per store when disarmed (a single load of g_block_on). */
extern std::atomic<bool> g_block_on;
bool blockwatch_hit(uint32_t addr, uint32_t size, const uint8_t* data, const char* how);

/* Called before a store is applied: prev is still readable, which is what makes
 * "0 -> 0" (a redundant write) distinguishable from "0x0123456 -> 0". */
inline void block_store(uint32_t addr, uint32_t size, const uint8_t* data, const char* how) {
    if (!g_block_on.load(std::memory_order_relaxed)) return;
    blockwatch_hit(addr, size, data, how);
}
void blockwatch_arm(uint32_t lo, uint32_t hi);

void init();
void start_scan();
void measure_pattern();

}
