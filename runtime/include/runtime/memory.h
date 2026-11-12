#pragma once

#include <cstdint>
#include <cstring>
#include "runtime/reservation.h"
#include <string>

namespace vm {

enum class Region : uint32_t {
    Main,
    User64K,
    User1M,
    RsxContext,
    Video,
    Stack,
    Spu,
    Count,
};

extern uint8_t* g_base;

bool init();
bool map(uint32_t addr, uint32_t size);
uint32_t alloc(Region region, uint32_t size, uint32_t align);
/* Make an already reserved range touchable. */
bool commit(uint32_t addr, uint32_t size);
uint32_t reserve_slot(uint32_t size, uint32_t align);
void release_slot(uint32_t addr);
bool dealloc(uint32_t addr);
uint32_t region_free(Region region);
uint32_t region_size(Region region);
bool is_mapped(uint32_t addr);
bool range_mapped(uint32_t addr, uint64_t size);
void mark_immutable(uint32_t addr, uint32_t size);
bool is_immutable(uint32_t addr, uint32_t size);
bool watch_enabled();
uint64_t watch_faults();
std::string watch_top_pages();
uint64_t watch_range(uint32_t addr, uint32_t size, bool& ok);
uint64_t range_epoch(uint32_t addr, uint32_t size);
bool watch_fault(uint32_t addr, bool repair_stale);
uint64_t watch_stale_repairs();
void host_write(uint32_t addr, uint64_t size);
void watch_reset(uint32_t addr, uint64_t size);

/* ---------------------------------------------------------------------------
 * Non-fatal page guard. The existing write watch (watch_range/watch_fault)
 * serves the renderer's texture cache: the fault means "the guest wrote a page
 * I was reading", and the page is repaired and the read redone. This is the
 * diagnostic counterpart, for tracing ANY address:
 *
 *   PS3BASE_GUARD=lo-hi[,lo-hi...]   arm at startup, PROT_NONE
 *   vm::guard_arm(addr, size)        arm at run time
 *   vm::guard_report()               counts
 *
 * A guest load or store into a guarded page takes a fault that is REPAIRED and
 * the instruction is RETRIED (write: the page is made writable for one
 * instruction; read: the page is made readable for one instruction), so the
 * guest never dies for the privilege of being watched. The first
 * PS3BASE_GUARD_REPORT<n> hits per range are logged with the host stack, the
 * guest thread and the LR. That is the difference from GOWA_TRAP: this one does
 * not require naming the address in advance, does not depend on the page being
 * remapped by a caller, and cannot abort the run.
 *
 * Host writes do NOT fault. They are reported by host_write(), which is why the
 * pgwrite funnel calls it.
 * ------------------------------------------------------------------------- */

/* Dump snapshot ranges at the given second (PS3BASE_TEX_SNAPSHOT). */
void tex_snapshot_tick(int seconds);

/* Non-fatal diagnostic page guard, see the comment block above. */
bool guard_arm(uint32_t addr, uint32_t size);
bool guard_armed(uint32_t addr);
uint64_t guard_hits();
std::string guard_report();
void guard_init_from_env();

/* Fault-handler side. guard_fault() opens the page for one instruction and
 * returns false when the page is not guarded (so the write watch can have it);
 * guard_rearm() closes it again after the single step; guard_wants_log() applies
 * the per-range hit cap. */
bool guard_fault(uint32_t addr);
void guard_rearm(uint32_t page);
bool guard_wants_log(uint32_t addr);

/* host_write() calls this: a host store cannot fault, so a guarded page learns
 * about it here instead. */
void guard_note_host_write(uint32_t addr);

/* A host store to a guarded page must not fault -- it would fault inside the
 * runtime's own memcpy, where the guest context is meaningless. So the funnel
 * opens the guarded pages before it stores and re-arms them afterwards:
 *
 *   bool any = guard_host_begin(addr, size);
 *   rsv::store_range(...);
 *   if (any) guard_host_end(addr, size);
 *
 * Either call is a no-op when nothing is armed. */
bool guard_host_begin(uint32_t addr, uint64_t size);
void guard_host_end(uint32_t addr, uint64_t size);

inline uint8_t* ptr(uint32_t addr) { return g_base + addr; }

inline uint32_t read32(uint32_t addr) { uint32_t v; std::memcpy(&v, g_base + addr, 4); return __builtin_bswap32(v); }
inline uint64_t read64(uint32_t addr) { uint64_t v; std::memcpy(&v, g_base + addr, 8); return __builtin_bswap64(v); }
inline uint16_t read16(uint32_t addr) { uint16_t v; std::memcpy(&v, g_base + addr, 2); return __builtin_bswap16(v); }
inline void write32(uint32_t addr, uint32_t v) {
    v = __builtin_bswap32(v);
    rsv::store_range(addr, reinterpret_cast<const uint8_t*>(&v), 4);
}
inline void write64(uint32_t addr, uint64_t v) {
    v = __builtin_bswap64(v);
    rsv::store_range(addr, reinterpret_cast<const uint8_t*>(&v), 8);
}
inline void write16(uint32_t addr, uint16_t v) {
    v = __builtin_bswap16(v);
    rsv::store_range(addr, reinterpret_cast<const uint8_t*>(&v), 2);
}
inline void write8(uint32_t addr, uint8_t v) { rsv::store_range(addr, &v, 1); }

}
