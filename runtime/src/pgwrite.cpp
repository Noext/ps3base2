
#include "runtime/log.h"
#include "runtime/pgwrite.h"
#include "runtime/memory.h"
#include "runtime/reservation.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace pgw {

std::atomic<uint64_t> g_writes{0};
std::atomic<uint64_t> g_bytes{0};
std::atomic<uint64_t> g_staged{0};

namespace {

/* A source overlapping the destination must be read before it is written, and
 * guest stores can race those reads, so it is copied out first. Sized once and
 * reused: the RSX blit is the only caller that hits this and it runs on one
 * thread at a time per frame, but a per-call vector would cost a malloc on the
 * same path. thread_local so two guest threads cannot share the buffer. */
thread_local std::vector<uint8_t> t_stage;

bool in_guest(const void* p, uint32_t size) {
    const uintptr_t b = reinterpret_cast<uintptr_t>(vm::g_base);
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (!vm::g_base || a < b) return false;
    const uintptr_t off = a - b;
    return off < 0x100000000ull && off + size <= 0x100000000ull;
}

/* Both operands are guest addresses and the ranges overlap in guest terms. */
bool overlaps(uint32_t dst, uint32_t src, uint32_t size) {
    return uint64_t(src) < uint64_t(dst) + size && uint64_t(dst) < uint64_t(src) + size;
}

bool trace_on() {
    static const bool on = std::getenv("PS3BASE_PGW_TRACE") != nullptr;
    return on;
}

/* PS3BASE_PGW_WATCH=<ea>[,<ea>...] logs every funnel write that touches one of
 * these 128-byte lines, with the host stack. That is the audit trail for
 * "which host path wrote this field", now that all of them share one funnel. */
void trace_write(uint32_t addr, uint32_t size, const char* how) {
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
    bool hit = false;
    for (uint32_t l : lines) {
        if (addr < l + 128 && l < addr + size) {
            hit = true;
            break;
        }
    }
    if (!hit) return;
    LOG_INFO("pgw", "%s host write 0x%08x+0x%x", how, addr, size);
}

}  // namespace

void write(uint32_t addr, const void* src, uint32_t size) {
    if (!size) return;
    /* A host write cannot fault, so the page machinery has to be told before the
     * bytes land: this is where a guarded page is reported and unprotected. It
     * must come first, because a guarded page is PROT_NONE. */
    vm::host_write(addr, size);
    /* A guarded page is PROT_NONE, and this store is a host store: it would fault
     * inside the runtime's own memcpy, where the guest context is gone. Open the
     * page for the store and close it again after. */
    const bool guarded = vm::guard_host_begin(addr, size);
    const uint8_t* p = static_cast<const uint8_t*>(src);
    if (in_guest(src, size) && overlaps(addr, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(src) - reinterpret_cast<uintptr_t>(vm::g_base)), size)) {
        t_stage.assign(p, p + size);
        p = t_stage.data();
        g_staged.fetch_add(1, std::memory_order_relaxed);
    }
    trace_write(addr, size, "pgw::write");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    rsv::store_range(addr, p, size);
    if (guarded) vm::guard_host_end(addr, size);
}

void fill(uint32_t addr, int value, uint32_t size) {
    if (!size) return;
    vm::host_write(addr, size);
    const bool guarded = vm::guard_host_begin(addr, size);
    trace_write(addr, size, "pgw::fill");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    /* rsv::store_range wants the bytes, not a length, so the fill is built in
     * chunks rather than reimplementing the group locking. 512 bytes keeps the
     * stack cost trivial and covers every structure-sized memset in the tree; a
     * larger one is streamed. */
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

void copy(uint32_t dst, uint32_t src, uint32_t size) {
    if (!size) return;
    vm::host_write(dst, size);
    const bool guarded = vm::guard_host_begin(dst, size);
    trace_write(dst, size, "pgw::copy");
    g_writes.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    const uint8_t* p = vm::ptr(src);
    if (overlaps(dst, src, size)) {
        t_stage.assign(p, p + size);
        p = t_stage.data();
        g_staged.fetch_add(1, std::memory_order_relaxed);
    }
    rsv::store_range(dst, p, size);
    if (guarded) vm::guard_host_end(dst, size);
}

std::string stats() {
    char b[160];
    std::snprintf(b, sizeof b, "pgw: %llu host writes, %llu bytes, %llu staged for overlap",
                  static_cast<unsigned long long>(g_writes.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(g_bytes.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(g_staged.load(std::memory_order_relaxed)));
    return b;
}

}  // namespace pgw
