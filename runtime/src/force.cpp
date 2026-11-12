/* ps3base -- forced values, driven by PS3BASE_FORCE.
 *
 * Each forcing is a debt: it makes the title proceed past a place where our
 * emulation is wrong, so one blocker can be passed to reach the next. Every
 * application is logged once, with the address and both values, so the list can
 * be emptied deliberately later (see docs/forcings.md).
 *
 * Syntax, ';' separated:
 *   f<hexaddr>=<value>      override r3 on return from lifted function at addr
 *   w<hexaddr>:<n>=<value>  write n bytes at guest address on every hit
 *   lr<hexaddr>=<value>     override r3 on return when the caller lr matches
 */
#include "runtime/force.h"
#include "runtime/pgwrite.h"

#include "lv2/sync.h"
#include "runtime/log.h"
#include "runtime/memory.h"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace force {
namespace {

struct Rule {
    enum Kind { Func, Write, Lr, Flag } kind = Func;
    uint32_t addr = 0;
    uint32_t size = 4;
    uint64_t value = 0;
    bool logged = false;
};

std::vector<Rule> g_rules;
bool g_loaded = false;
std::mutex g_lock;
int g_flag_delay_ms = 5000;
std::atomic<bool> g_save_done{false};
std::chrono::steady_clock::time_point g_save_at;

void load() {
    if (g_loaded) return;
    g_loaded = true;
    const char* env = std::getenv("PS3BASE_FORCE");
    if (!env || !*env) return;
    std::string s(env);
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find(';', pos);
        if (end == std::string::npos) end = s.size();
        std::string item = s.substr(pos, end - pos);
        pos = end + 1;
        if (item.empty()) continue;
        const size_t eq = item.find('=');
        const bool is_flag = item.size() > 1 && item[0] == 'e';
        if (eq == std::string::npos && !is_flag) {
            LOG_WARN("force", "rule '%s' has no '='", item.c_str());
            continue;
        }
        const std::string lhs = is_flag ? item : item.substr(0, eq);
        const uint64_t value = is_flag ? 0 : std::strtoull(item.c_str() + eq + 1, nullptr, 0);
        Rule r;
        r.value = value;
        if (is_flag) {
            /* e<id>:<bits>:<delay_ms> -- set an event flag once, delay_ms after
             * the first UserAutoSave completes. */
            r.kind = Rule::Flag;
            std::string a = lhs.substr(1);
            size_t c1 = a.find(':');
            size_t c2 = c1 == std::string::npos ? std::string::npos : a.find(':', c1 + 1);
            r.addr = static_cast<uint32_t>(std::strtoul(a.substr(0, c1).c_str(), nullptr, 16));
            r.size = static_cast<uint32_t>(std::strtoul(a.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 16));
            g_flag_delay_ms = c2 == std::string::npos ? 5000 : std::atoi(a.c_str() + c2 + 1);
        } else if (lhs.size() > 2 && lhs[0] == 'l' && lhs[1] == 'r') {
            r.kind = Rule::Lr;
            r.addr = static_cast<uint32_t>(std::strtoul(lhs.c_str() + 2, nullptr, 16));
        } else if (lhs.size() > 1 && lhs[0] == 'w') {
            r.kind = Rule::Write;
            std::string a = lhs.substr(1);
            const size_t colon = a.find(':');
            if (colon != std::string::npos) {
                r.size = static_cast<uint32_t>(std::strtoul(a.c_str() + colon + 1, nullptr, 0));
                a = a.substr(0, colon);
            }
            r.addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
        } else {
            r.kind = Rule::Func;
            r.addr = static_cast<uint32_t>(std::strtoul(lhs.c_str() + (lhs[0] == 'f' ? 1 : 0), nullptr, 16));
        }
        g_rules.push_back(r);
    }
    for (const Rule& r : g_rules)
        LOG_INFO("force", "rule %s 0x%08x = 0x%llx", r.kind == Rule::Func ? "func" :
                 r.kind == Rule::Lr ? "lr" : r.kind == Rule::Flag ? "flag" : "write", r.addr,
                 static_cast<unsigned long long>(r.value));
}

}  // namespace

void on_return(uint32_t fn_addr, uint32_t lr_addr, uint64_t& r3) {
    load();
    if (g_rules.empty()) return;
    std::lock_guard lk(g_lock);
    for (Rule& r : g_rules) {
        bool hit = false;
        if (r.kind == Rule::Func) hit = r.addr == fn_addr;
        else if (r.kind == Rule::Lr) hit = r.addr == lr_addr;
        else if (r.kind == Rule::Write) {
            if (vm::range_mapped(r.addr, r.size)) {
                switch (r.size) {
                case 1: pgw::write8(r.addr + (0), static_cast<uint8_t>(static_cast<uint8_t>(r.value))); break;
                case 2: vm::write16(r.addr, static_cast<uint16_t>(r.value)); break;
                case 8: vm::write64(r.addr, r.value); break;
                default: vm::write32(r.addr, static_cast<uint32_t>(r.value)); break;
                }
                if (!r.logged) {
                    r.logged = true;
                    LOG_INFO("force", "wrote 0x%llx at 0x%08x (%u bytes)", 
                             static_cast<unsigned long long>(r.value), r.addr, r.size);
                }
            }
            continue;
        }
        if (!hit) continue;
        const uint64_t before = r3;
        r3 = r.value;
        if (!r.logged) {
            r.logged = true;
            LOG_INFO("force", "APPLIED %s 0x%08x: r3 0x%llx -> 0x%llx", 
                     r.kind == Rule::Func ? "func" : "lr", r.addr,
                     static_cast<unsigned long long>(before),
                     static_cast<unsigned long long>(r.value));
        }
        return;
    }
}

void notify_save_done() {
    if (g_save_done.exchange(true)) return;
    g_save_at = std::chrono::steady_clock::now();
    LOG_INFO("force", "save completed; flag rule armed");
}

void tick() {
    load();
    if (!g_save_done.load(std::memory_order_relaxed)) return;
    static std::atomic<bool> fired{false};
    if (fired.load(std::memory_order_relaxed)) return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_save_at).count();
    if (elapsed < g_flag_delay_ms) return;
    if (fired.exchange(true)) return;
    std::lock_guard lk(g_lock);
    for (Rule& r : g_rules) {
        if (r.kind != Rule::Flag || r.logged) continue;
        const int64_t rc = lv2::event_flag_set_bits(r.addr, r.size);
        r.logged = true;
        LOG_INFO("force", "APPLIED flag 0x%08x bits=0x%x -> 0x%llx (%lld ms after save)",
                 r.addr, r.size, static_cast<unsigned long long>(rc),
                 static_cast<long long>(elapsed));
    }
}

void on_call(uint32_t fn_addr) {
    load();
    if (g_rules.empty()) return;
    std::lock_guard lk(g_lock);
    for (Rule& r : g_rules) {
        if (r.kind != Rule::Write || r.addr == 0 || r.size != 1) continue;
        /* byte writes are applied here so they land before the function reads */
        if (vm::range_mapped(r.addr, 1)) pgw::write8(r.addr + (0), static_cast<uint8_t>(static_cast<uint8_t>(r.value)));
    }
}

}  // namespace force
