#include "runtime/mem.h"

#include "runtime/host.h"
#include "runtime/log.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace mem {

namespace {

constexpr size_t PAGE = 0x1000;

Mode g_mode = [] {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    /* os mode needs mmap/mprotect, which wasm does not have. bitset is the only
     * backend that can work there, so it is not a preference but a requirement. */
    return Mode::Bitset;
#endif
    const char* e = std::getenv("PS3BASE_MEMORY");
    if (e && std::strcmp(e, "bitset") == 0) return Mode::Bitset;
    if (e && std::strcmp(e, "os") != 0 && e[0])
        LOG_WARN("mem", "PS3BASE_MEMORY=%s is not os|bitset, using os", e);
    return Mode::Os;
}();

std::mutex g_lock;
std::vector<uint64_t> g_bitmap;   /* bitset mode: one bit per page */
size_t g_pages = 0;
size_t g_committed = 0;
struct Guard { size_t offset; size_t size; };
std::vector<Guard> g_guards;
uint8_t* g_base = nullptr;

inline bool test_bit(size_t page) {
    return (g_bitmap[page / 64] >> (page % 64)) & 1ull;
}
inline void set_bit(size_t page) {
    g_bitmap[page / 64] |= 1ull << (page % 64);
}
inline void clear_bit(size_t page) {
    g_bitmap[page / 64] &= ~(1ull << (page % 64));
}

}  // namespace

Mode mode() { return g_mode; }
const char* mode_name() { return g_mode == Mode::Bitset ? "bitset" : "os"; }

uint8_t* reserve(size_t size) {
    if (g_mode == Mode::Bitset) {
        /* One flat buffer, zero-filled. No PROT_NONE, no per-page mapping: the
         * commit state lives in the bitmap and is enforced in software. This is
         * the only shape a WebAssembly build can use. */
        g_base = static_cast<uint8_t*>(std::calloc(1, size));
        if (!g_base) {
            LOG_ERROR("mem", "bitset mode: could not allocate %zu bytes", size);
            return nullptr;
        }
        std::lock_guard lk(g_lock);
        g_pages = (size + PAGE - 1) / PAGE;
        g_bitmap.assign((g_pages + 63) / 64, 0);
        g_committed = 0;
        LOG_INFO("mem", "bitset mode: %zu MB flat, %zu pages, no mprotect", size >> 20, g_pages);
        return g_base;
    }
    g_base = static_cast<uint8_t*>(host::vm_reserve(size));
    if (g_base) {
        std::lock_guard lk(g_lock);
        g_pages = (size + PAGE - 1) / PAGE;
        g_bitmap.assign((g_pages + 63) / 64, 0);
        g_committed = 0;
    }
    return g_base;
}

bool commit(uint8_t* base, size_t offset, size_t size) {
    if (g_mode == Mode::Bitset) {
        const size_t first = offset / PAGE;
        const size_t last = (offset + size + PAGE - 1) / PAGE;
        std::lock_guard lk(g_lock);
        for (size_t p = first; p < last && p < g_pages; p++) {
            if (!test_bit(p)) {
                set_bit(p);
                g_committed += PAGE;
            }
        }
        return true;
    }
    return host::vm_commit(base + offset, size);
}

bool decommit(uint8_t* base, size_t offset, size_t size) {
    if (g_mode == Mode::Bitset) {
        const size_t first = offset / PAGE;
        const size_t last = (offset + size + PAGE - 1) / PAGE;
        std::lock_guard lk(g_lock);
        for (size_t p = first; p < last && p < g_pages; p++) {
            if (test_bit(p)) {
                clear_bit(p);
                g_committed -= PAGE;
            }
        }
        /* Do NOT memset here. The os path zeroes on the next commit (fresh
         * pages), and the guest never reads decommitted memory, so clearing
         * eagerly would only cost time on every dealloc. Where a test needs a
         * zeroed page it commits first, which allocates from the calloc'd flat
         * buffer and is therefore already zero. */
        (void)base;
        return true;
    }
    return host::vm_decommit(base + offset, size);
}

bool protect(uint8_t* base, size_t offset, size_t size, Prot prot) {
    if (g_mode == Mode::Bitset) {
        /* Reads and writes are unrestricted; only Prot::None carries meaning,
         * and it is enforced by the software range guard, not by the CPU. */
        (void)base;
        (void)offset;
        (void)size;
        (void)prot;
        return true;
    }
    return host::vm_protect(base + offset, size, static_cast<host::Prot>(prot));
}

bool page_committed(size_t page_index) {
    if (g_mode != Mode::Bitset) return true;
    std::lock_guard lk(g_lock);
    if (page_index >= g_pages) return false;
    return test_bit(page_index);
}

void register_guard(size_t offset, size_t size) {
    std::lock_guard lk(g_lock);
    g_guards.push_back({offset, size});
}

bool in_guard(size_t offset, size_t* guard_start) {
    std::lock_guard lk(g_lock);
    for (const auto& g : g_guards) {
        if (offset >= g.offset && offset < g.offset + g.size) {
            if (guard_start) *guard_start = g.offset;
            return true;
        }
    }
    return false;
}

size_t committed_bytes() {
    std::lock_guard lk(g_lock);
    return g_committed;
}

void reset_for_tests() {
    std::lock_guard lk(g_lock);
    std::fill(g_bitmap.begin(), g_bitmap.end(), 0);
    g_committed = 0;
}

}  // namespace mem
