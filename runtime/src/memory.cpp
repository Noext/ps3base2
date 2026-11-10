#include <atomic>
#include "runtime/memory.h"
#include "runtime/mem.h"
#include "runtime/regions.h"
#include "runtime/log.h"

#include <algorithm>
#include <cstdio>
#include <vector>
#include <map>
#include <mutex>
#include <cstdlib>
#include <string>

#include "runtime/host.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace vm {

uint8_t* g_base = nullptr;

namespace {

struct RegionInfo {
    uint32_t start;
    uint32_t size;
    uint32_t page;
};

/* Built from vm::region_bases() so a wasm build can compact the map without
 * editing this table. The sparse default keeps the native path identical. */
RegionInfo* region_table() {
    static RegionInfo t[7] = {};
    static bool init = false;
    if (!init) {
        const vm::RegionBases& b = vm::region_bases();
        t[0] = {0x00010000, 0x0FFF0000, 0x10000};
        t[1] = {0, 0x10000000, 0x10000};
        t[2] = {0, 0x10000000, 0x100000};
        t[3] = {0, 0x10000000, 0x100000};
        t[4] = {b.video, 0x10000000, 0x100000};
        t[5] = {b.stack, 0x10000000, 0x10000};
        t[6] = {b.spu, b.spu_size, 0x10000};
        init = true;
    }
    return t;
}
#define kRegions region_table()

std::mutex g_lock;
std::map<uint32_t, uint32_t> g_slots = {{0x00000000, 0x10000000}};

uint32_t reserve_slot_locked(uint32_t size, uint32_t align) {
    if (!align) align = 0x10000000;
    for (uint64_t a = 0x10000000; a + size <= vm::region_bases().user_bound; a += align) {
        bool clash = false;
        for (auto& [b, n] : g_slots) {
            if (a < uint64_t(b) + n && b < a + size) {
                clash = true;
                break;
            }
        }
        if (!clash) {
            g_slots[uint32_t(a)] = size;
            return uint32_t(a);
        }
    }
    return 0;
}
std::map<uint32_t, uint32_t> g_blocks;
uint32_t g_used[static_cast<size_t>(Region::Count)] = {};

/* Callers inside this file already hold g_lock, so this must not take it. */
bool commit_locked(uint32_t addr, uint32_t size) {
    watch_reset(addr, size);
    return mem::commit(g_base, addr, size);
}

bool overlaps(uint32_t addr, uint32_t size) {
    auto it = g_blocks.upper_bound(addr);
    if (it != g_blocks.begin()) {
        auto prev = std::prev(it);
        if (prev->first + prev->second > addr) return true;
    }
    return it != g_blocks.end() && it->first < addr + size;
}

}

uint8_t g_pagemap[0x100000];

namespace {

std::atomic<uint32_t> g_page_epoch[0x100000];
std::atomic<uint8_t> g_page_watched[0x100000];
std::mutex g_watch_locks[4096];

std::mutex& watch_lock(uint32_t page) { return g_watch_locks[page & 4095]; }
std::atomic<uint64_t> g_watch_faults{0};
std::atomic<uint64_t> g_stale_repairs{0};
std::atomic<uint16_t> g_fault_pages[0x100000];

}

uint64_t watch_faults() { return g_watch_faults.load(std::memory_order_relaxed); }
uint64_t watch_stale_repairs() { return g_stale_repairs.load(std::memory_order_relaxed); }

std::string watch_top_pages() {
    std::vector<std::pair<uint32_t, uint32_t>> v;
    for (uint32_t i = 0; i < 0x100000; i++) {
        uint32_t n = g_fault_pages[i].exchange(0, std::memory_order_relaxed);
        if (n) v.push_back({n, i});
    }
    std::sort(v.rbegin(), v.rend());
    std::string out = std::to_string(v.size()) + " pages:";
    char b[32];
    for (size_t i = 0; i < v.size() && i < 12; i++) {
        std::snprintf(b, sizeof b, " %05x000x%u", v[i].second, v[i].first);
        out += b;
    }
    return out;
}

bool watch_enabled() {
    static const bool on = [] {
        const char* e = std::getenv("GOWA_WRITE_WATCH");
        return !e || std::string(e) != "0";
    }();
    return on;
}

void watch_reset(uint32_t addr, uint64_t size) {
    for (uint64_t a = addr & ~0xFFFull; a < uint64_t(addr) + size && a < 0x100000000ull; a += 0x1000) {
        uint32_t page = uint32_t(a >> 12);
        watch_lock(page).lock();
        if (g_page_watched[page].load(std::memory_order_relaxed)) {
            mem::protect(g_base, a, 0x1000, mem::Prot::ReadWrite);
            g_page_watched[page].store(0, std::memory_order_relaxed);
            static std::atomic<int> logged{0};
            if (logged.fetch_add(1) < 4) LOG_WARN("vm", "remapped watched page 0x%08llx", static_cast<unsigned long long>(a));
        }
        g_page_epoch[page].fetch_add(1, std::memory_order_acq_rel);
        watch_lock(page).unlock();
    }
}

uint64_t watch_range(uint32_t addr, uint32_t size, bool& ok) {
    ok = true;
    uint64_t end = std::min<uint64_t>(uint64_t(addr) + size, 0x100000000ull);
    uint32_t first = addr >> 12, last = uint32_t((end + 0xFFF) >> 12);
    for (uint32_t page = first; page < last;) {
        if (g_page_watched[page].load(std::memory_order_acquire)) {
            page++;
            continue;
        }
        uint32_t run = page;
        while (run < last && run - page < 1024 && !g_page_watched[run].load(std::memory_order_acquire)) run++;
        for (uint32_t q = page; q < run; q++) watch_lock(q).lock();
        if (mem::protect(g_base, (uint64_t(page) << 12), uint64_t(run - page) << 12, mem::Prot::Read)) {
            for (uint32_t q = page; q < run; q++) g_page_watched[q].store(1, std::memory_order_release);
        } else {
            for (uint32_t q = page; q < run; q++) {
                if (mem::protect(g_base, (uint64_t(q) << 12), 0x1000, mem::Prot::Read)) g_page_watched[q].store(1, std::memory_order_release);
                else ok = false;
            }
        }
        for (uint32_t q = page; q < run; q++) watch_lock(q).unlock();
        page = run;
    }
    uint64_t sum = 0;
    for (uint32_t page = first; page < last; page++) sum += g_page_epoch[page].load(std::memory_order_acquire);
    return sum;
}

uint64_t range_epoch(uint32_t addr, uint32_t size) {
    uint64_t sum = 0;
    for (uint64_t a = addr & ~0xFFFull; a < uint64_t(addr) + size && a < 0x100000000ull; a += 0x1000) sum += g_page_epoch[a >> 12].load(std::memory_order_acquire);
    return sum;
}

bool watch_fault(uint32_t addr, bool repair_stale) {
    uint32_t page = addr >> 12;
    watch_lock(page).lock();
    bool handled = false;
    if (g_page_watched[page].load(std::memory_order_relaxed)) {
        mem::protect(g_base, (uint64_t(page) << 12), 0x1000, mem::Prot::ReadWrite);
        g_page_watched[page].store(0, std::memory_order_release);
        g_page_epoch[page].fetch_add(1, std::memory_order_acq_rel);
        handled = true;
        g_watch_faults.fetch_add(1, std::memory_order_relaxed);
        g_fault_pages[page].fetch_add(1, std::memory_order_relaxed);
    } else {
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION mi;
        if (VirtualQuery(g_base + (uint64_t(page) << 12), &mi, sizeof mi) && mi.State == MEM_COMMIT) {
            if (mi.Protect == PAGE_READWRITE) {
                handled = true;
            } else if (repair_stale && mi.Protect == PAGE_READONLY) {
                DWORD old;
                VirtualProtect(g_base + (uint64_t(page) << 12), 0x1000, PAGE_READWRITE, &old);
                g_page_epoch[page].fetch_add(1, std::memory_order_acq_rel);
                g_stale_repairs.fetch_add(1, std::memory_order_relaxed);
                handled = true;
            }
        }
#else
        /* A page that belongs to the guest must be writable. If a fault lands
         * on a mapped page that is not currently watched, the protection is
         * stale -- left read-only by an earlier watch_range -- and letting the
         * fault through kills the guest thread for a page it owns. Repair it
         * regardless of whether other watches exist; the old repair_stale
         * condition (no watches at all) left this window open for as long as
         * any watch was active, which is most of the run. */
        if (is_mapped(addr)) {
            mem::protect(g_base, (uint64_t(page) << 12), 0x1000, mem::Prot::ReadWrite);
            g_page_epoch[page].fetch_add(1, std::memory_order_acq_rel);
            g_stale_repairs.fetch_add(1, std::memory_order_relaxed);
            handled = true;
        } else if (repair_stale) {
            handled = false;
        }
#endif
    }
    watch_lock(page).unlock();
    return handled;
}

void host_write(uint32_t addr, uint64_t size) {
    if (!size) return;
    uint64_t end = std::min<uint64_t>(uint64_t(addr) + size, 0x100000000ull);
    uint32_t first = addr >> 12, last = uint32_t((end + 0xFFF) >> 12);
    for (uint32_t page = first; page < last;) {
        if (!g_page_watched[page].load(std::memory_order_acquire)) {
            page++;
            continue;
        }
        uint32_t run = page;
        while (run < last && run - page < 1024 && g_page_watched[run].load(std::memory_order_acquire)) run++;
        for (uint32_t q = page; q < run; q++) watch_lock(q).lock();
        mem::protect(g_base, (uint64_t(page) << 12), uint64_t(run - page) << 12, mem::Prot::ReadWrite);
        for (uint32_t q = page; q < run; q++) {
            if (g_page_watched[q].load(std::memory_order_relaxed)) {
                g_page_watched[q].store(0, std::memory_order_release);
                g_page_epoch[q].fetch_add(1, std::memory_order_acq_rel);
            }
        }
        for (uint32_t q = page; q < run; q++) watch_lock(q).unlock();
        page = run;
    }
}

bool block_covers_locked(uint32_t addr) {
    auto it = g_blocks.upper_bound(addr);
    if (it == g_blocks.begin()) return false;
    auto prev = std::prev(it);
    return addr < prev->first + prev->second;
}

void refresh_pages_locked(uint32_t addr, uint64_t size) {
    for (uint64_t a = addr & ~0xFFFull; a < uint64_t(addr) + size && a < 0x100000000ull; a += 0x1000) {
        std::atomic_ref<uint8_t>(g_pagemap[a >> 12]).store(block_covers_locked(uint32_t(a)) ? 1 : 0, std::memory_order_release);
    }
}

bool init() {
    /* 8 GB of address space, reserved through the backend so a WebAssembly build
     * can use a flat bitmap instead of mmap+mprotect. */
    /* Ask for what the map can actually reach, not a fixed 8 GiB. Under
     * PS3BASE_REGIONS=compact that is 2.25 GiB, which fits wasm32; the sparse map
     * would ask for 4 GiB and leave nothing for code and stack.
     * mem::reserve rounds up, and the region placement below is what uses it. */
    /* Under wasm the reservation must be exactly what the map spans: linear memory
     * cannot promise 8 GiB when the wasm32 ceiling is 4, and the compact map only
     * needs 2.25 GiB. On the native path keep the generous 8 GiB as before. */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    const uint64_t need = vm::map_span();
#else
    const uint64_t need = std::max<uint64_t>(vm::map_span(), 0x200000000ull);
#endif
    void* p = mem::reserve(need);
    if (!p) {
        LOG_ERROR("vm", "failed to reserve guest address space");
        return false;
    }
    g_base = static_cast<uint8_t*>(p);
    LOG_INFO("vm", "guest base %p (backing: %s)", p, mem::mode_name());
    return true;
}

bool map(uint32_t addr, uint32_t size) {
    std::lock_guard lk(g_lock);
    uint32_t a = addr & ~0xFFFFu;
    uint32_t e = (addr + size + 0xFFFF) & ~0xFFFFu;
    if (!commit_locked(a, e - a)) return false;
    g_blocks[a] = e - a;
    refresh_pages_locked(a, uint64_t(e - a));
    return true;
}

uint32_t reserve_slot(uint32_t size, uint32_t align) {
    std::lock_guard lk(g_lock);
    return reserve_slot_locked(size, align);
}

void release_slot(uint32_t addr) {
    std::lock_guard lk(g_lock);
    g_slots.erase(addr);
}

bool commit(uint32_t addr, uint32_t size) {
    std::lock_guard lk(g_lock);
    return commit_locked(addr, size);
}

uint32_t alloc(Region region, uint32_t size, uint32_t align) {
    std::lock_guard lk(g_lock);
    RegionInfo& r = kRegions[static_cast<size_t>(region)];
    if (!r.start) {
        r.start = reserve_slot_locked(r.size, 0x10000000);
        if (!r.start) return 0;
        LOG_INFO("vm", "region %u placed at 0x%08x", static_cast<unsigned>(region), r.start);
    }
    if (align < r.page) align = r.page;
    size = (size + r.page - 1) & ~(r.page - 1);
    const uint64_t end = uint64_t(r.start) + r.size;
    uint64_t a = (uint64_t(r.start) + align - 1) & ~uint64_t(align - 1);
    while (a + size <= end) {
        auto it = g_blocks.upper_bound(uint32_t(a + size - 1));
        if (it != g_blocks.begin()) {
            auto prev = std::prev(it);
            uint64_t prev_end = uint64_t(prev->first) + prev->second;
            if (prev_end > a) {
                a = (prev_end + align - 1) & ~uint64_t(align - 1);
                continue;
            }
        }
        if (!commit_locked(uint32_t(a), size)) return 0;
        std::memset(g_base + a, 0, size);
        g_blocks[uint32_t(a)] = size;
        refresh_pages_locked(uint32_t(a), size);
        g_used[static_cast<size_t>(region)] += size;
        return uint32_t(a);
    }
    return 0;
}

bool dealloc(uint32_t addr) {
    std::lock_guard lk(g_lock);
    auto it = g_blocks.find(addr);
    if (it == g_blocks.end()) return false;
    for (size_t i = 0; i < static_cast<size_t>(Region::Count); i++) {
        if (kRegions[i].start && addr >= kRegions[i].start && addr < kRegions[i].start + kRegions[i].size) {
            g_used[i] -= it->second;
        }
    }
    watch_reset(addr, it->second);
    mem::decommit(g_base, addr, it->second);
    uint32_t freed = it->second;
    g_blocks.erase(it);
    refresh_pages_locked(addr, freed);
    return true;
}

uint32_t region_free(Region region) {
    std::lock_guard lk(g_lock);
    return kRegions[static_cast<size_t>(region)].size - g_used[static_cast<size_t>(region)];
}

uint32_t region_size(Region region) { return kRegions[static_cast<size_t>(region)].size; }


void snapshot_range(uint32_t addr, uint32_t size, const char* tag)
{
    char name[128];
    std::snprintf(name, sizeof name, "snap_%s_%08x.bin", tag, addr);
    if (FILE* f = std::fopen(name, "wb")) {
        const size_t n = std::fwrite(g_base + addr, 1, size, f);
        std::fclose(f);
        LOG_INFO("snap", "%s addr 0x%x size 0x%x wrote %zu mapped %d",
                 name, addr, size, n, is_mapped(addr) ? 1 : 0);
    } else {
        LOG_WARN("snap", "%s: cannot open", name);
    }
}

void tex_snapshot_tick(int seconds)
{
    static const std::string list = [] {
        const char* e = std::getenv("PS3BASE_TEX_SNAPSHOT");
        return std::string(e ? e : "");
    }();
    if (list.empty()) return;
    char tag[16];
    std::snprintf(tag, sizeof tag, "%03d", seconds);
    size_t pos = 0;
    while (pos < list.size()) {
        size_t end = list.find(',', pos);
        if (end == std::string::npos) end = list.size();
        const std::string item = list.substr(pos, end - pos);
        pos = end + 1;
        if (item.empty()) continue;
        uint32_t a = 0, n = 0x100000;
        if (std::sscanf(item.c_str(), "%x:%x", &a, &n) == 1) n = 0x100000;
        if (a && g_base) snapshot_range(a, n, tag);
    }
}

bool is_mapped(uint32_t addr) {
    return std::atomic_ref<uint8_t>(g_pagemap[addr >> 12]).load(std::memory_order_acquire) != 0;
}

struct Immutable {
    uint32_t lo;
    uint32_t hi;
};
Immutable g_immutable[64];
std::atomic<uint32_t> g_immutable_count{0};

void mark_immutable(uint32_t addr, uint32_t size) {
    uint32_t n = g_immutable_count.load();
    if (n >= 64) return;
    g_immutable[n] = {addr, addr + size};
    g_immutable_count.store(n + 1, std::memory_order_release);
}

bool is_immutable(uint32_t addr, uint32_t size) {
    uint32_t n = g_immutable_count.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; i++) {
        if (addr >= g_immutable[i].lo && uint64_t(addr) + size <= g_immutable[i].hi) return true;
    }
    return false;
}

bool range_mapped(uint32_t addr, uint64_t size) {
    if (!size) return true;
    uint64_t end = uint64_t(addr) + size;
    if (end > 0x100000000ull) return false;
    for (uint64_t a = addr & ~0xFFFull; a < end; a += 0x1000) {
        if (!std::atomic_ref<uint8_t>(g_pagemap[a >> 12]).load(std::memory_order_relaxed)) return false;
    }
    return true;
}

}
