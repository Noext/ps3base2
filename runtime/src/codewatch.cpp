#include "runtime/codewatch.h"

#include "runtime/host.h"
#include "runtime/image.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/syncwatch.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#include <dlfcn.h>
#include "runtime/backtrace_port.h"

namespace cw {

std::atomic<uint32_t> g_lo{0x00010200u};
std::atomic<uint32_t> g_hi{0x008aa674u};
std::atomic<bool> g_on{false};
std::atomic<uint32_t> g_line{0xFFFFFFFFu};
std::atomic<bool> g_any{false};
std::atomic<double> g_arm_at{0.0};
std::atomic<uint32_t> g_cap{60};
std::atomic<bool> g_note_on{false};

namespace {

}  // namespace

Context (*current_context)() = nullptr;

namespace {

std::atomic<uint32_t> g_reports{0};
constexpr uint32_t kMaxReports = 60;

/* A store that writes a code word into data fires again whenever the corrupt
 * word is copied around. Report the first at each address, then count, so a
 * single root event stays visible in a long log. */
uint32_t g_seen[512];
int g_seen_n = 0;
std::atomic<uint32_t> g_dup{0};

bool seen(uint32_t a) {
    for (int i = 0; i < g_seen_n; i++)
        if (g_seen[i] == a) return true;
    if (g_seen_n < 512) g_seen[g_seen_n++] = a;
    return false;
}

std::string frame(void* p) {
    Dl_info info{};
    char b[80];
    if (dladdr(p, &info) && info.dli_sname)
        std::snprintf(b, sizeof b, " %s+0x%llx", info.dli_sname,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(info.dli_saddr)));
    else if (info.dli_fname)
        std::snprintf(b, sizeof b, " %s+0x%llx", info.dli_fname,
                      static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(info.dli_fbase)));
    else
        std::snprintf(b, sizeof b, " %p", p);
    return b;
}

std::string bt(int depth) {
    void* pcs[40];
    int n = backtrace_port::capture(pcs, depth < 40 ? depth : 40);
    std::string s;
    for (int i = 1; i < n; i++) s += " <-" + frame(pcs[i]);
    return s;
}

std::string hexdump(uint32_t ea, uint32_t len) {
    std::string s;
    char b[12];
    for (uint32_t i = 0; i < len; i += 4) {
        if (!vm::is_mapped(ea + i)) {
            s += " ??";
            break;
        }
        std::snprintf(b, sizeof b, " %08x", vm::read32(ea + i));
        s += b;
    }
    return s;
}

}  // namespace

bool is_code(uint32_t v) {
    if ((v & 3) != 0) return false;
    return v >= g_lo.load(std::memory_order_relaxed) && v < g_hi.load(std::memory_order_relaxed);
}

namespace {
std::vector<uint32_t> g_entries;
}

void set_known_entries(const uint32_t* addrs, uint32_t count) {
    g_entries.assign(addrs, addrs + count);
    std::sort(g_entries.begin(), g_entries.end());
    LOG_INFO("codewatch", "%u legal call targets registered: a text word that is one of them is a function pointer, not corruption", count);
}

const std::vector<uint32_t>& entries() { return g_entries; }

bool known_entry(uint32_t v) {
    if (g_entries.empty()) return false;
    return std::binary_search(g_entries.begin(), g_entries.end(), v);
}

/* Only the main heap. Measured at boot: the guest stacks (0xD0000000) are full
 * of return addresses and function pointers by construction, and the first run
 * with stacks included produced 338 hits before the first frame. A code word in
 * a *stack slot* is a return address; a code word in a *heap object field* is
 * the corruption. */
static bool heap_only = true;
static bool in_guest_data(uint32_t a) {
    if (!heap_only) return true;
    return a >= 0x30000000u && a < 0x3D000000u;
}

bool heap_pointer(uint32_t v) {
    return (v >= 0x30000000u && v < 0x3D000000u) || (v >= 0x10000000u && v < 0x14000000u) ||
           (v >= 0x20000000u && v < 0x21000000u);
}

void check_word(uint32_t addr, uint32_t value, const char* how) {
    if (logging::uptime() < g_arm_at.load(std::memory_order_relaxed)) return;
    /* A code address: inside the title's executable range and instruction
     * aligned. 0x00721f30 and 0x00730100 both are; 0x0003fe10 (a colour word
     * seen in the first run) is not 4-aligned, and 0x00000033 is out of range. */
    if ((value & 3u) != 0) return;
    if (value < g_lo.load(std::memory_order_relaxed) || value >= g_hi.load(std::memory_order_relaxed)) return;
    /* A legal call target is a function pointer, not corruption. */
    if (known_entry(value)) return;
    if (!in_guest_data(addr)) return;
    if (!g_any.load(std::memory_order_relaxed)) {
        /* The store replaces a HEAP POINTER with code: that is the signature.
         * check_word runs before the store, so the old word is still there. */
        if (!vm::range_mapped(addr, 4)) return;
        if (!heap_pointer(vm::read32(addr))) return;
    }
    report(addr, value, how);
}

void report(uint32_t addr, uint32_t value, const char* how) {
    if (known_entry(value)) return;
    if (!in_guest_data(addr)) return;
    const uint32_t cap = g_cap.load(std::memory_order_relaxed);
    if (g_reports.load(std::memory_order_relaxed) >= cap) return;
    if (seen(addr)) {
        g_dup.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    uint32_t n = g_reports.fetch_add(1, std::memory_order_relaxed);
    if (n >= cap) return;
    Context c{0, 0, nullptr};
    if (current_context) c = current_context();
    LOG_ERROR("codewatch", "#%u t=%.2f %s put CODE WORD 0x%08x at 0x%08x (guest lr 0x%08x) thread %s",
              n, logging::uptime(), how, value, addr, c.lr, c.thread ? c.thread : host::thread_name().c_str());
    const uint32_t base = (addr - 32) & ~3u;
    LOG_ERROR("codewatch", "   around 0x%08x:%s", base, hexdump(base, 80).c_str());
    if (n < 12) LOG_ERROR("codewatch", "   host:%s", bt(26).c_str());
}

/* Line watch: a bitmap over the whole 4 GB guest space at 128-byte lines (4 M
 * bits = 512 KB). The test on the store path is one array read, so arming a
 * whole list's worth of node lines costs nothing at run time. */
namespace {
std::atomic<uint64_t> g_note_bits[(0x100000000ull >> 7) / 64];
}

void note_line(uint32_t ea) {
    const uint32_t line = ea & ~127u;
    const uint32_t k = line >> 7;
    const uint64_t bit = uint64_t(1) << (k & 63);
    uint64_t cur = g_note_bits[k >> 6].load(std::memory_order_relaxed);
    if (cur & bit) return;
    g_note_bits[k >> 6].store(cur | bit, std::memory_order_relaxed);
    g_note_on.store(true, std::memory_order_relaxed);
    static std::atomic<uint32_t> armed{0};
    if (armed.fetch_add(1, std::memory_order_relaxed) < 80)
        LOG_INFO("codewatch", "line watch armed on 0x%08x (t=%.2f)", line, logging::uptime());
}


void note_hit(uint32_t addr, uint32_t size, const char* how, uint32_t value) {
    if (!g_note_on.load(std::memory_order_relaxed)) return;
    const uint32_t a0 = addr & ~127u;
    const uint32_t a1 = (addr + (size ? size : 1) - 1) & ~127u;
    const uint32_t k0 = a0 >> 7, k1 = a1 >> 7;
    bool hit = (g_note_bits[k0 >> 6].load(std::memory_order_relaxed) >> (k0 & 63)) & 1;
    if (!hit) hit = (g_note_bits[k1 >> 6].load(std::memory_order_relaxed) >> (k1 & 63)) & 1;
    if (!hit) return;
    static std::atomic<uint32_t> n{0};
    if (n.fetch_add(1, std::memory_order_relaxed) >= 2000) return;
    Context c{0, 0, nullptr};
    if (current_context) c = current_context();
    /* prev is what the field held BEFORE this store. The useful comparison is
     * "was a heap pointer, is now a code word"; a legal function pointer
     * replacing a zero is ordinary, and prev is what separates the two. */
    const uint32_t prev = vm::range_mapped(addr, 4) ? vm::read32(addr) : 0;
    LOG_INFO("codewatch", "line-write 0x%08x+%u prev 0x%08x -> 0x%08x by %s (guest lr 0x%08x) thread %s held %s",
             addr, size, prev, value, how, c.lr, c.thread ? c.thread : host::thread_name().c_str(),
             syncwatch::list().c_str());
}

/* ------------------------------------------------------------------------
 * Block writes: who writes the guest block at PS3BASE_BLOCKWATCH=lo-hi.
 * ---------------------------------------------------------------------- */
std::atomic<bool> g_block_on{false};
namespace {
std::atomic<uint32_t> g_block_lo{0}, g_block_hi{0};
std::atomic<uint64_t> g_block_hits{0};
/* Every distinct (offset, width, value, caller) is reported once and counted.
 * A block like 0x0177cc00 is read and re-read a thousand times a second, so an
 * unfiltered log is unreadable; a repeat of the same write from the same PC is
 * the same fact, not a new one. */
struct BlockKey { uint32_t off, size, val, lr, pc; };
BlockKey g_block_seen[512];
uint64_t g_block_counts[512];
int g_block_n = 0;
}

namespace {
/* Host frames, symbolised: the only thing that names a writer whose guest lr is
 * unusable. Depth 12 is enough to get out of rsv::store_range -> the lifted
 * ppu::st* inline -> the lifted f_XXXX body, which is where the answer is. */
std::string host_stack() {
    void* pcs[16];
    const int n = backtrace_port::capture(pcs, 16);
    std::string s;
    char b[160];
    for (int i = 1; i < n; i++) {
        Dl_info info{};
        if (dladdr(pcs[i], &info) && info.dli_sname)
            std::snprintf(b, sizeof b, " %s+0x%llx", info.dli_sname,
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pcs[i]) - reinterpret_cast<uintptr_t>(info.dli_saddr)));
        else if (info.dli_fname)
            std::snprintf(b, sizeof b, " %s+0x%llx", info.dli_fname,
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(pcs[i]) - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        else
            std::snprintf(b, sizeof b, " %p", pcs[i]);
        s += b;
    }
    return s;
}

/* Name a guest PC without pulling in the dispatch table: codewatch.cpp is also
 * linked into the small unit-test binaries, which have no generated table, so
 * lv2::describe_address() would be an undefined symbol there. The span table is
 * weak and absent in exactly those binaries, which this handles. */
const char* where(uint32_t pc) {
    static thread_local char buf[128];
    const char* image_name = nullptr;
    uint32_t image_base = 0;
    if (&image::image_count) {
        for (uint32_t i = 0; i < image::image_count; i++) {
            if (pc >= image::images[i].text_lo && pc < image::images[i].text_hi) {
                image_name = image::images[i].name;
                image_base = image::images[i].text_lo;
                break;
            }
        }
    }
    uint32_t entry = 0;
    if (&image::function_span_count) {
        uint32_t l = 0, h = image::function_span_count;
        while (l < h) {
            const uint32_t mid = l + (h - l) / 2;
            const image::FunctionSpan& s = image::function_spans[mid];
            if (pc < s.lo) h = mid;
            else if (pc > s.hi + 3) l = mid + 1;
            else { entry = s.entry; break; }
        }
    }
    if (image_name)
        std::snprintf(buf, sizeof buf, "%s+0x%x f_%08x+0x%x", image_name, pc - image_base, entry, entry ? pc - entry : 0);
    else
        std::snprintf(buf, sizeof buf, "0x%08x (no image)", pc);
    return buf;
}
}

void blockwatch_arm(uint32_t lo, uint32_t hi) {
    g_block_lo.store(lo);
    g_block_hi.store(hi);
    g_block_on.store(true, std::memory_order_relaxed);
    LOG_INFO("blockwatch", "watching guest 0x%08x..0x%08x from t=0; every write into it is logged with offset, width, value and caller", lo, hi);
    /* Arm-time contents, so a field that is already zero when the watch starts
     * is distinguishable from one the title zeroes later. */
    for (uint32_t a = lo; a < hi; a += 16) {
        if (!vm::range_mapped(a, 16)) break;
        LOG_INFO("blockwatch", "  arm-time 0x%08x: %08x %08x %08x %08x", a, vm::read32(a), vm::read32(a + 4), vm::read32(a + 8), vm::read32(a + 12));
    }
}

bool blockwatch_hit(uint32_t addr, uint32_t size, const uint8_t* data, const char* how) {
    const uint32_t lo = g_block_lo.load(std::memory_order_relaxed);
    const uint32_t hi = g_block_hi.load(std::memory_order_relaxed);
    const uint32_t n = size ? size : 1;
    if (addr + n <= lo || addr >= hi) return false;
    g_block_hits.fetch_add(1, std::memory_order_relaxed);
    const uint32_t first = (addr > lo) ? addr : lo;
    /* The word that actually landed at the first overlapping 4-byte slot, big-
     * endian the way the guest sees it. */
    uint32_t val = 0;
    const uint32_t w0 = first & ~3u;
    if (w0 >= addr && w0 + 4 <= addr + n) {
        std::memcpy(&val, data + (w0 - addr), 4);
        val = __builtin_bswap32(val);
    } else {
        val = vm::range_mapped(w0, 4) ? vm::read32(w0) : 0;
    }
    Context c{0, 0, nullptr};
    if (current_context) c = current_context();
    const uint32_t prev = vm::range_mapped(w0, 4) ? vm::read32(w0) : 0;
    int slot = -1;
    for (int i = 0; i < g_block_n; i++) {
        const BlockKey& k = g_block_seen[i];
        if (k.off == w0 && k.size == n && k.val == val && k.lr == c.lr) { slot = i; break; }
    }
    if (slot < 0 && g_block_n < 512) {
        slot = g_block_n++;
        g_block_seen[slot] = BlockKey{w0, n, val, c.lr, c.cia};
    }
    if (slot >= 0) g_block_counts[slot]++;
    if (slot >= 0 && g_block_counts[slot] > 1) return true;
    LOG_INFO("blockwatch", "WRITE +0x%03x (0x%08x) w%u prev 0x%08x -> 0x%08x by %s | guest lr 0x%08x %s | thread %s",
             w0 - lo, w0, n, prev, val, how, c.lr, where(c.lr),
             c.thread ? c.thread : host::thread_name().c_str());
    /* The guest lr is a RETURN ADDRESS and can be stale: inside a constructor
     * that was inlined into a caller it is the caller's lr, and when the store
     * happens inside the runtime's own memcpy it can be anything at all -- the
     * burst that writes this block reported lr 0x0177cde0, which is inside the
     * BSS being written. The host stack is what actually names the writer, so it
     * is captured for the first observation of each distinct site. */
    LOG_INFO("blockwatch", "  host:%s", host_stack().c_str());
    return true;
}

void init() {
    if (const char* b = std::getenv("PS3BASE_BLOCKWATCH")) {
        const char* dash = std::strchr(b, '-');
        const uint32_t lo = static_cast<uint32_t>(std::strtoul(b, nullptr, 16));
        const uint32_t hi = dash ? static_cast<uint32_t>(std::strtoul(dash + 1, nullptr, 16)) : lo + 0x100;
        blockwatch_arm(lo, hi);
    }
    if (!std::getenv("PS3BASE_CODEWATCH")) return;
    if (const char* r = std::getenv("PS3BASE_CODEWATCH_RANGES")) {
        uint32_t lo = static_cast<uint32_t>(std::strtoul(r, nullptr, 16));
        const char* dash = std::strchr(r, '-');
        uint32_t hi = dash ? static_cast<uint32_t>(std::strtoul(dash + 1, nullptr, 16)) : lo + 0x10000;
        g_lo.store(lo);
        g_hi.store(hi);
    }
    if (std::getenv("PS3BASE_CODEWATCH_ALL_REGIONS")) heap_only = false;
    if (std::getenv("PS3BASE_CODEWATCH_ANY")) g_any.store(true);
    if (const char* a = std::getenv("PS3BASE_CODEWATCH_AT")) g_arm_at.store(std::atof(a));
    if (const char* m = std::getenv("PS3BASE_CODEWATCH_MAX")) g_cap.store(static_cast<uint32_t>(std::atoi(m)));
    if (const char* l = std::getenv("PS3BASE_CODEWATCH_ADDR"))
        g_line.store(static_cast<uint32_t>(std::strtoul(l, nullptr, 16)) & ~127u);
    g_on.store(true, std::memory_order_relaxed);
    LOG_INFO("codewatch", "armed: a store of 0x%08x..0x%08x into guest data is reported with thread, guest lr and host stack%s",
             g_lo.load(), g_hi.load(),
             g_line.load() == 0xFFFFFFFFu ? "" : " (filtered to one 128-byte line)");
    if (const char* nl = std::getenv("PS3BASE_CODEWATCH_NOTE")) note_line(static_cast<uint32_t>(std::strtoul(nl, nullptr, 16)));
    if (std::getenv("PS3BASE_CODEWATCH_SCAN")) start_scan();
}

/* ------------------------------------------------------------------------
 * fill_pattern(): does the pattern of CODE WORDS the dump shows occur often
 * enough to be a signature rather than a coin toss?
 *
 * The corrupt field held 0x00721f30, and the words just before it in the same
 * object are 0x00000028, 0x00000028, 0x00000028, 0x0000000b, 0x0000000f -- i.e.
 * a run of small integers ending in a code pointer. Scanned over the whole heap
 * in blackbox/memory_0.bin: X small <=0x10000 followed by a text value 0x10000..0xb93688.
 * Measures how specific the shape is, so a future run can be judged against a
 * number instead of a feeling.
 * ---------------------------------------------------------------------- */
void measure_pattern() {
    std::thread([] {
        for (;;) {
            host::sleep_ms(5000);
            if (!g_on.load()) return;
            uint32_t hits = 0, run3 = 0;
            for (uint64_t a = 0x30000000ull; a < 0x3D000000ull; a += 4) {
                uint32_t v = vm::read32(static_cast<uint32_t>(a));
                if (v != 0x00721f30u) continue;
                const uint32_t w1 = vm::read32(static_cast<uint32_t>(a) - 4);
                const uint32_t w2 = vm::read32(static_cast<uint32_t>(a) - 8);
                const uint32_t w3 = vm::read32(static_cast<uint32_t>(a) - 12);
                if (w1 <= 0x10000 && w2 <= 0x10000 && w3 <= 0x10000) run3++;
                hits++;
            }
            LOG_INFO("codewatch", "pattern t=%.1f: %u occurrences of 0x00721f30 in the heap, %u preceded by three small ints", logging::uptime(), hits, run3);
        }
    }).detach();
}

/* ------------------------------------------------------------------------
 * Sweep. Some stores do not cross rsv::store_range at all: direct
 * memcpy(vm::ptr(...)) in HLE handlers, the RSX download path, the loader. A
 * periodic sweep of the heap for words that look like text addresses finds the
 * corruption even when the writer was missed, and the sweep is also what
 * proves the writer-side watcher is not blind.
 * ---------------------------------------------------------------------- */
/* Addresses that already held a signature match when the scan started. The heap
 * legitimately contains ~900k words that fall inside the text range at boot; what
 * matters is a NEW one appearing, at an address that did not hold one before. The
 * baseline is a sorted vector of addresses, 4 bytes each.
 *
 * This is the "recherche valeur" the investigation asked for, done so it can run
 * during a live session instead of only after a memory dump: a full sweep of the
 * heap at 4 bytes is 50 M reads, which costs about 0.2 s and runs every 5 s. */
static std::vector<uint32_t> g_baseline;

/* A word of the title's code, as opposed to a word that merely falls inside the
 * text range. Measured on blackbox/memory_0.bin: of 1 637 146 four-aligned words
 * in 0x10200..0x8aa674 inside the heap, 728 000 are UTF-16 text ("de", "st",
 * "ab") whose halves are both printable ASCII. The two real corrupt values,
 * 0x00721f30 and 0x00730100, have a printable high half (every code address
 * 0x00xxxxxx does) and a NON-printable low half -- 0x1f30 and 0x0100. Requiring
 * the low half not to be printable ASCII removes the text and keeps the code. */
static bool text_halves(uint32_t v) {
    const uint32_t hi = v >> 16, lo = v & 0xFFFF;
    auto printable = [](uint32_t h) { return h >= 0x20 && h <= 0x7E; };
    return (hi == 0 || printable(hi)) && (lo == 0 || printable(lo));
}

/* The discriminant that actually separates the corruption from ordinary data,
 * measured on blackbox/memory_0.bin over the whole 0x30000000 heap:
 *
 *   aligned word inside the text range                      1 637 146
 *   ... minus lifted function entries                       1 632 591
 *   ... minus UTF-16 text (both halves printable)             461 711
 *   ... minus words within 0x20000 of the previous entry           68
 *
 * The corrupt value 0x00721f30 is 85 496 bytes past the entry at 0x0070d138 --
 * deep inside a function, where no legitimate pointer target or switch-table
 * entry ever lands, because 0x710000..0x720000 contains no lifted function at
 * all. "A code address far from any function start" is 68 words in a 200 MB heap,
 * which is quiet enough to run for a whole session. */
constexpr uint32_t kMinGap = 0x20000;

static uint32_t entry_before(uint32_t v) {
    /* First entry strictly less than v. */
    extern const std::vector<uint32_t>& entries();
    const std::vector<uint32_t>& e = entries();
    auto it = std::lower_bound(e.begin(), e.end(), v);
    return it == e.begin() ? 0u : *(it - 1);
}

static bool signature_at(uint32_t a, uint32_t* out) {
    if (!vm::range_mapped(a, 4)) return false;
    const uint32_t v = vm::read32(a);
    if ((v & 3u) != 0) return false;
    if (v < g_lo.load() || v >= g_hi.load()) return false;
    if (known_entry(v)) return false;
    if (text_halves(v)) return false;
    if (v - entry_before(v) < kMinGap) return false;
    if (out) *out = v;
    return true;
}

void start_scan() {
    std::thread([] {
        const uint32_t lo = g_lo.load();
        const uint32_t hi = g_hi.load();
        for (;;) {
            host::sleep_ms(5000);
            if (!g_on.load(std::memory_order_relaxed)) return;
            if (g_baseline.empty()) {
                /* First pass after arming: this is the baseline, not a result. */
                for (uint64_t a = 0x30000000ull; a < 0x3D000000ull; a += 4) {
                    uint32_t v;
                    if (!signature_at(static_cast<uint32_t>(a), &v)) continue;
                    g_baseline.push_back(static_cast<uint32_t>(a));
                }
                std::sort(g_baseline.begin(), g_baseline.end());
                LOG_INFO("codewatch", "sweep baseline t=%.1f: %zu addresses already hold a text word at boot; only NEW ones are reported",
                         logging::uptime(), g_baseline.size());
                continue;
            }
            uint32_t found = 0;
            for (uint64_t a = 0x30000000ull; a < 0x3D000000ull; a += 4) {
                uint32_t v;
                if (!signature_at(static_cast<uint32_t>(a), &v)) continue;
                if (std::binary_search(g_baseline.begin(), g_baseline.end(), static_cast<uint32_t>(a))) continue;
                if (++found > 8) break;
                const uint32_t prev = vm::read32(static_cast<uint32_t>(a) - 4);
                const uint32_t next = vm::read32(static_cast<uint32_t>(a) + 4);
                LOG_WARN("codewatch", "sweep t=%.1f: NEW text word 0x%08x in the heap at 0x%08x (prev 0x%08x next 0x%08x)",
                         logging::uptime(), v, static_cast<uint32_t>(a), prev, next);
                note_line(static_cast<uint32_t>(a));
            }
        }
    }).detach();
}

}  // namespace cw