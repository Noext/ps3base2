#pragma once

/* pgw -- the one funnel for HOST writes into guest memory.
 *
 * Motivation, measured. rsv::store_range is what invalidates a lwarx/stwcx
 * reservation (it bumps the 128-byte line version), what feeds the block watch,
 * the code watch and the line watch, and what wakes a parked SPU waiter. Every
 * lifted ppu::st* helper and every vm::write* goes through it.
 *
 * A memcpy(vm::ptr(ea), ...) does not. It writes the guest bytes and nothing
 * else sees them:
 *
 *   - a PPU thread holding a reservation on that line commits a value it never
 *     observed, because the line version never moved;
 *   - the block/line/code watches never report the write, so the writer of a
 *     watched field is invisible;
 *   - a diagnostic page guard (vm::guard_arm) never learns the page changed,
 *     because no fault can happen -- the write is a plain host store.
 *
 * There were 81 memcpy/memset(vm::ptr(...)) sites, 16 scalar
 * vm::ptr(a)[i] = v sites and one memmove between two guest ranges. They are
 * now all here, so there is a single place that can be audited and a single
 * place that invalidates and feeds.
 *
 * The funnel does, in this order:
 *
 *   1. vm::host_write(addr, size) -- tell the page machinery that a HOST write
 *      is about to land. A guarded page reports it here (a fault cannot: the
 *      write is a host store), and a watched page is unprotected before the
 *      store so the store itself cannot fault. Must be first, because a
 *      guarded page is PROT_NONE.
 *   2. rsv::store_range(addr, data, size) -- lock every 128-byte group, mark
 *      every line version, memcpy, publish, wake sleepers, and call
 *      cw::block_store / cw::check_store / cw::note_hit.
 *
 * Aliasing: a few old sites read from guest memory (the RSX blit rows) and a
 * guest store may run concurrently with such a read, so a source overlapping
 * the destination is staged first -- the same thing the overlap branch in
 * rsx_renderer.cpp already did by hand.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace pgw {

/* Host writes that went through the funnel, bytes written, and sources that had
 * to be staged for overlap. The counters exist so a run can prove the funnel is
 * the path actually taken: a missed site shows up as "no host writes at all"
 * for its range, which is what the pre-change binary measures. */
extern std::atomic<uint64_t> g_writes;
extern std::atomic<uint64_t> g_bytes;
extern std::atomic<uint64_t> g_staged;

/* Copy size guest bytes from host memory src to guest address addr.
 * Replaces memcpy(vm::ptr(addr), src, size). */
void write(uint32_t addr, const void* src, uint32_t size);

/* Fill size guest bytes with value. Replaces memset(vm::ptr(addr), value, size). */
void fill(uint32_t addr, int value, uint32_t size);

/* Guest-to-guest move, overlap safe. Replaces memcpy/memmove between two
 * vm::ptr() operands. */
void copy(uint32_t dst, uint32_t src, uint32_t size);

/* One byte, for the scalar vm::ptr(a)[i] = v sites. */
inline void write8(uint32_t addr, uint8_t v) { write(addr, &v, 1); }

std::string stats();

}  // namespace pgw
