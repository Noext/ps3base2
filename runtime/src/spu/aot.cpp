#include "spu/aot.h"
#include "runtime/log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spu::aot {

namespace {

struct Index {
    std::unordered_map<uint32_t, std::vector<const CompiledBlock*>> by_lsa;
    size_t count = 0;
    bool active = false;

    Index() {
        size_t n = 0;
        const CompiledBlock* blocks = compiled_blocks(n);
        for (size_t i = 0; i < n; i++) by_lsa[blocks[i].lsa].push_back(&blocks[i]);
        count = n;
        active = n > 0 && !std::getenv("GOWA_SPU_NO_AOT");
        if (n) LOG_INFO("spu", "%zu compiled SPU blocks%s", n, active ? "" : " (disabled)");
    }
};

const Index& index() {
    static const Index i;
    return i;
}

uint32_t load_word(const uint8_t* ls, uint32_t pc) {
    uint32_t w;
    std::memcpy(&w, ls + (pc & PC_MASK), 4);
    return __builtin_bswap32(w);
}

struct Recorder {
    std::mutex m;
    std::unordered_set<uint64_t> seen;
    std::vector<uint32_t> pending;
    const char* path = std::getenv("GOWA_SPU_RECORD");
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    uint64_t total = 0;
};

Recorder& recorder() {
    static Recorder r;
    return r;
}

void flush_locked(Recorder& r) {
    if (r.pending.empty() || !r.path) return;
    if (FILE* f = std::fopen(r.path, "ab")) {
        std::fwrite(r.pending.data(), 4, r.pending.size(), f);
        std::fclose(f);
    }
    r.pending.clear();
}

}

bool active() { return index().active; }

bool recording() { return recorder().path != nullptr; }

bool is_terminator(uint32_t w) {
    uint32_t op9 = w >> 23;
    if (op9 == 0x040 || op9 == 0x042 || op9 == 0x044 || op9 == 0x046 || op9 == 0x060 || op9 == 0x062 || op9 == 0x064 || op9 == 0x066) return true;
    uint32_t op11 = w >> 21;
    return op11 == 0x000 || op11 == 0x140 || (op11 >= 0x128 && op11 <= 0x12B) || (op11 >= 0x1A8 && op11 <= 0x1AB);
}

const CompiledBlock* find(const uint8_t* ls, uint32_t pc) {
    const Index& ix = index();
    auto it = ix.by_lsa.find(pc);
    if (it == ix.by_lsa.end()) return nullptr;
    for (const CompiledBlock* b : it->second) {
        if (pc + b->words * 4 > LS_SIZE) continue;
        if (std::memcmp(ls + pc, b->code, b->words * 4) == 0) return b;
    }
    return nullptr;
}

void record(const uint8_t* ls, uint32_t pc) {
    uint32_t n = 0;
    uint64_t h = 1469598103934665603ull ^ pc;
    while (n < MAX_BLOCK_WORDS && pc + n * 4 < LS_SIZE) {
        uint32_t w = load_word(ls, pc + n * 4);
        h = (h ^ w) * 1099511628211ull;
        n++;
        if (is_terminator(w)) break;
    }
    Recorder& r = recorder();
    std::lock_guard lk(r.m);
    if (!r.seen.insert(h ^ (uint64_t(n) << 56)).second) return;
    r.pending.push_back(pc);
    r.pending.push_back(n);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t raw;
        std::memcpy(&raw, ls + pc + i * 4, 4);
        r.pending.push_back(raw);
    }
    r.total++;
    auto now = std::chrono::steady_clock::now();
    if (now - r.last > std::chrono::seconds(5)) {
        r.last = now;
        flush_locked(r);
        LOG_INFO("spu", "recorded %llu SPU blocks", static_cast<unsigned long long>(r.total));
    }
}

namespace {
/* Blocks the interpreter has had to run, keyed by content hash so a block seen
 * at two addresses counts once. Deliberately hash-only: the question this
 * answers is "which pieces of code were never compiled", not "where". */
std::unordered_set<uint64_t> g_interpreted_hashes;
std::mutex g_interpreted_lock;
std::atomic<uint64_t> g_interpreted_count{0};
std::atomic<uint64_t> g_interpreted_insns{0};
std::atomic<bool> g_strict_seen{false};
}  // namespace

uint64_t note_interpreted(const uint8_t* ls, uint32_t pc) {
    static const bool strict = std::getenv("GOWA_SPU_STRICT") != nullptr;
    static const bool loud = strict || std::getenv("GOWA_SPU_LOUD_INTERP") != nullptr;
    /* Hash the same way record() does, so a capture and a warning name the same
     * block. Cheap enough at 60 Hz and only on the slow path. */
    uint32_t n = 0;
    uint64_t h = 1469598103934665603ull ^ pc;
    while (n < MAX_BLOCK_WORDS && pc + n * 4 < LS_SIZE) {
        uint32_t w = load_word(ls, pc + n * 4);
        h = (h ^ w) * 1099511628211ull;
        n++;
        if (is_terminator(w)) break;
    }
    const uint64_t key = h ^ (uint64_t(n) << 56);
    bool first = false;
    {
        std::lock_guard lk(g_interpreted_lock);
        first = g_interpreted_hashes.insert(key).second;
    }
    const uint64_t total = g_interpreted_count.fetch_add(first ? 1 : 0) + (first ? 1 : 0);
    if (first && loud) {
        LOG_WARN("spujob", "INTERPRETED BLOCK lsa %05x words %u hash %016llx (total distinct %llu)",
                 pc, n, static_cast<unsigned long long>(key),
                 static_cast<unsigned long long>(total));
    }
    if (strict && !g_strict_seen.exchange(true)) {
        LOG_ERROR("spujob", "STRICT: the interpreter ran a block (lsa %05x words %u hash %016llx) -- "
                            "100%% compiled is violated, failing the run",
                  pc, n, static_cast<unsigned long long>(key));
        /* A run that needed the interpreter cannot claim 100%. Exit loudly
         * rather than finish with a number that would be a lie. */
        std::fprintf(stderr, "GOWA_SPU_STRICT: interpreted block at lsa %05x\n", pc);
        std::fflush(stderr);
        std:: _Exit(70);
    }
    return total;
}

uint64_t interpreted_blocks() { return g_interpreted_count.load(); }
uint64_t interpreted_insns_total() { return g_interpreted_insns.load(); }

void flush() {
    Recorder& r = recorder();
    std::lock_guard lk(r.m);
    flush_locked(r);
}

}
