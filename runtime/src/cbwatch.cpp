#include "runtime/cbwatch.h"

#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/thread.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace cbwatch {

namespace {

struct ImportStat {
    uint64_t n = 0;
    double first = 0, last = 0;
    uint64_t last_ret = 0;
    uint32_t last_lr = 0;
    uint64_t ret_xor = 0;      /* order-independent digest of the return values */
    uint64_t nonzero = 0;      /* how many returns were != 0 */
    bool lle = false;
};

struct CbStat {
    std::string kind;
    uint32_t arg = 0;
    uint64_t n = 0;
    double first = 0, last = 0;
    uint64_t last_a1 = 0;
};

std::mutex g_lock;
std::map<std::string, ImportStat> g_imports;
std::map<uint32_t, CbStat> g_cbs;
double g_t0 = 0;
bool g_init = false;
int g_dumped = 0;

double now() { return logging::uptime(); }

void init_locked() {
    if (g_init) return;
    g_init = true;
    g_t0 = now();
}

}  // namespace

bool enabled() {
    static const bool on = std::getenv("PS3BASE_CENSUS") != nullptr;
    return on;
}

void note_import(const char* module, const char* name, uint64_t ret, uint32_t lr, bool lle) {
    if (!enabled()) return;
    const guest::Thread* t = guest::current();
    /* Keyed by thread AND import: "the server thread called X 400 times and got
     * 0 every time" is the sentence that names a leaf, and a global counter
     * cannot say it. */
    char key[192];
    std::snprintf(key, sizeof key, "%s|%s::%s", t ? t->name.c_str() : "?", module, name);
    std::lock_guard lk(g_lock);
    init_locked();
    ImportStat& s = g_imports[key];
    s.n++;
    const double e = now() - g_t0;
    if (!s.first) s.first = e;
    s.last = e;
    s.last_ret = ret;
    s.last_lr = lr;
    s.ret_xor ^= ret;
    if (ret) s.nonzero++;
    s.lle = lle;
    if (std::getenv("PS3BASE_CENSUS_CALLS") && s.n <= 40)
        LOG_INFO("census", "imp t=%.2f %s %s::%s -> 0x%llx lr=0x%08x", e,
                 t ? t->name.c_str() : "?", module, name,
                 static_cast<unsigned long long>(ret), lr);
}

void note_register(const char* kind, uint32_t opd, uint32_t arg) {
    if (!enabled()) return;
    std::lock_guard lk(g_lock);
    init_locked();
    CbStat& s = g_cbs[opd];
    s.kind = kind;
    s.arg = arg;
    s.first = now() - g_t0;
    /* A registration is not a delivery: n stays 0 until note_call. */
}

void note_call(const char* from, uint32_t opd, uint64_t arg1) {
    if (!enabled()) return;
    std::lock_guard lk(g_lock);
    init_locked();
    auto it = g_cbs.find(opd);
    if (it == g_cbs.end()) {
        /* A callback called without a registration we saw: record it anyway,
         * with the registering site named "unregistered". */
        CbStat& s = g_cbs[opd];
        s.kind = "unregistered";
        it = g_cbs.find(opd);
        s.first = now() - g_t0;
    }
    it->second.n++;
    it->second.last = now() - g_t0;
    it->second.last_a1 = arg1;
    if (std::getenv("PS3BASE_CENSUS_CALLS"))
        LOG_INFO("census", "cb-call t=%.2f from=%s opd=0x%08x a0=0x%llx",
                 now() - g_t0, from, opd, static_cast<unsigned long long>(arg1));
}

void dump(const char* why) {
    if (!enabled()) return;
    std::vector<std::pair<std::string, ImportStat>> imports;
    std::vector<std::pair<uint32_t, CbStat>> cbs;
    double uptime_now = 0;
    {
        std::lock_guard lk(g_lock);
        init_locked();
        imports.assign(g_imports.begin(), g_imports.end());
        cbs.assign(g_cbs.begin(), g_cbs.end());
        uptime_now = now() - g_t0;
    }
    LOG_INFO("census", "=== import inventory (%s) t=%.1f ===", why, uptime_now);
    for (const auto& [key, s] : imports) {
        LOG_INFO("census", "imp %-56s n=%-7llu ret_last=0x%-16llx ret_xor=0x%-16llx nz=%-7llu lle=%d lr=0x%08x t=[%.1f..%.1f]",
                 key.c_str(), static_cast<unsigned long long>(s.n),
                 static_cast<unsigned long long>(s.last_ret),
                 static_cast<unsigned long long>(s.ret_xor),
                 static_cast<unsigned long long>(s.nonzero), int(s.lle), s.last_lr,
                 s.first, s.last);
    }
    LOG_INFO("census", "=== callback inventory (%s) ===", why);
    for (const auto& [opd, s] : cbs) {
        LOG_INFO("census", "cb opd=0x%08x kind=%-16s arg=0x%08x deliveries=%-7llu first=%.2f last=%.2f last_a1=0x%llx",
                 opd, s.kind.c_str(), s.arg, static_cast<unsigned long long>(s.n),
                 s.first, s.last, static_cast<unsigned long long>(s.last_a1));
    }
    LOG_INFO("census", "=== end census (%s) ===", why);
    g_dumped++;
}

}  // namespace cbwatch
