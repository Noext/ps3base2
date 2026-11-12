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
 *     observed, because the line version never moved. Measured by
 *     tests/host_write_invalidation.cpp: the raw form COMMITS across a
 *     write-then-restore, the funnel fails the store, as POWER requires;
 *   - the block/line/code watches never report the write, so the writer of a
 *     watched field is invisible;
 *   - a diagnostic page guard (vm::guard_arm) never learns the page changed,
 *     because a host store cannot fault.
 *
 * There were 81 memcpy/memset(vm::ptr(...)) sites, 16 scalar
 * vm::ptr(a)[i] = v sites and one guest-to-guest memmove with that shape. They
 * are now all here, so there is a single place that can be audited and a single
 * place that invalidates and feeds.
 *
 * The funnel does, in this order:
 *
 *   1. vm::host_write(addr, size) -- tell the page machinery that a HOST write
 *      is about to land. A guarded page cannot fault on a host store, so it is
 *      counted here instead; a watched page is unprotected before the store.
 *   2. vm::guard_host_begin/end around the store when a guarded page is
 *      involved, because a guarded page is PROT_NONE and the host store would
 *      otherwise fault inside the runtime's own memcpy, where there is no guest
 *      context to report.
 *   3. rsv::store_range(addr, data, size) -- lock every 128-byte group, mark
 *      every line version, memcpy, publish, wake sleepers, and call
 *      cw::block_store / cw::check_store / cw::note_hit.
 *
 * Aliasing: a few old sites read from guest memory (the RSX blit rows) and a
 * guest store may run concurrently with such a read, so a source overlapping the
 * destination is staged first -- the same thing the overlap branch in
 * rsx_renderer.cpp already did by hand.
 *
 * Header-only on purpose. Twenty test binaries link memory.cpp and
 * reservation.cpp directly and would each need this translation unit added to
 * their link line; a header-only funnel is one definition-complete header that
 * every one of them already includes through memory.h.
 */

#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/reservation.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace pgw {

/* Host writes that went through the funnel, bytes written, and sources that had
 * to be staged for overlap. The counters exist so a run can prove the funnel is
 * the path actually taken: a missed site shows up as "no host writes at all" for
 * its range, which is what a pre-change binary measures. Printed at the end of a
 * run by main.cpp. */
inline std::atomic<uint64_t> g_writes{0};
inline std::atomic<uint64_t> g_bytes{0};
inline std::atomic<uint64_t> g_staged{0};

namespace detail {

/* A source overlapping the destination must be read before it is written, and
 * guest stores can race those reads, so it is copied out first. thread_local so
 * two guest threads cannot share the buffer. */
inline thread_local std::vector<uint8_t> t_stage;

inline bool in_guest(const void* p, uint32_t size) {
    const uintptr_t b = reinterpret_cast<uintptr_t>(vm::g_base);
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!vm::g_base || a < b) return false;
    const uintptr_t off = a - b;
    return off < 0x100000000ull && off + size <= 0x100000000ull;
}

/* Both operands are guest addresses and the ranges overlap in guest terms. */
inline bool overlaps(uint32_t dst, uint32_t src, uint32_t size) {
    return uint64_t(src) < uint64_t(dst) + size && uint64_t(dst) < uint64_t(src) + size;
}

inline bool trace_on() {
    static const bool on = std::getenv("PS3BASE_PGW_TRACE") != nullptr;
    return on;
}

/* PS3BASE_PGW_WATCH=<ea>[,<ea>...] logs every funnel write that touches one of
 * these 128-byte lines. That is the audit trail for "which host path wrote this
 * field", now that all host paths share one funnel. */
inline void trace_write(uint32_t addr, uint32_t size, const char* how) {
    if (!trace_on()) return;
    static const std::vector<uint32_t> lines = [] {
        std::vector<uint32_t> v;
        if (const char* e = std::getenv("PS3BASE_PGW_WATCH")) {
            std::string s = e;
            size_t pos = 0;
            while (pos < s.size()) {
                size_t end = s.find(',', pos);
                if (end == std::string::npos) end = s.size();
                v.push_back(static_cast<uint32_t>(std::strtoul(s.substr(pos, end - pos).c_str(), nullptr, 16)) & ~127u);
                pos = end + 1;
            }
        }
        return v;
    }();
    if (lines.empty()) return;
    for (uint32_t l : lines) {
        if (addr < l + 128 && l < addr + size) {
            LOG_INFO("pgw", "%s host write 0x%08x+0x%x", how, addr, size);
            return;
        }
    }
}

}  // namespace detail

/* Copy size guest bytes from host memory src to guest address addr.
 * Replaces memcpy(vm::ptr(addr), src, size). */
inline void write(uint32_t addr, const void* src, uint32_t size) {
    if (!size) return;
    vm::host_write(addr, size);
    const bool guarded = vm::guard_host_begin(addr, size);
    const uint8_t* p = static_cast<const uint8_t*>(src);
    if (detail::in_guest(src, size) &&
        detail::overlaps(addr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src) - reinterpret_cast<uintptr_t>(vm::g_base)), size)) {
        detail::t_stage.assign(p, p + size);
        p = detail::t_stage.data();
        g_staged.fetch_add(1, std::memory_order_relaxed);
    }
    detail::trace_write(addr, size, "pgw::write");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    rsv::store_range(addr, p, size);
    if (guarded) vm::guard_host_end(addr, size);
}

/* Fill size guest bytes with value. Replaces memset(vm::ptr(addr), value, size). */
inline void fill(uint32_t addr, int value, uint32_t size) {
    if (!size) return;
    vm::host_write(addr, size);
    const bool guarded = vm::guard_host_begin(addr, size);
    detail::trace_write(addr, size, "pgw::fill");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    /* rsv::store_range wants the bytes, not a length, so the fill is built in
     * chunks rather than reimplementing the group locking. 512 bytes covers every
     * structure-sized memset in the tree; a larger one is streamed. */
    uint8_t buf[512];
    std::memset(buf, value, sizeof buf);
    uint32_t done = 0;
    while (done < size) {
        const uint32_t n = (size - done) < sizeof buf ? (size - done) : static_cast<uint32_t>(sizeof buf);
        rsv::store_range(addr + done, buf, n);
        done += n;
    }
    if (guarded) vm::guard_host_end(addr, size);
}

/* Guest-to-guest move, overlap safe. Replaces memcpy/memmove between two
 * vm::ptr() operands. */
inline void copy(uint32_t dst, uint32_t src, uint32_t size) {
    if (!size) return;
    vm::host_write(dst, size);
    const bool guarded = vm::guard_host_begin(dst, size);
    detail::trace_write(dst, size, "pgw::copy");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    const uint8_t* p = vm::ptr(src);
    if (detail::overlaps(dst, src, size)) {
        detail::t_stage.assign(p, p + size);
        p = detail::t_stage.data();
        g_staged.fetch_add(1, std::memory_order_relaxed);
    }
    rsv::store_range(dst, p, size);
    if (guarded) vm::guard_host_end(dst, size);
}

/* One byte, for the scalar vm::ptr(a)[i] = v sites. */
inline void write8(uint32_t addr, uint8_t v) { write(addr, &v, 1); }

inline std::string stats() {
    char b[160];
    std::snprintf(b, sizeof b, "pgw: %llu host writes, %llu bytes, %llu staged for overlap",
                  static_cast<unsigned long long>(g_writes.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(g_bytes.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(g_staged.load(std::memory_order_relaxed)));
    return b;
}

}  // namespace pgw
