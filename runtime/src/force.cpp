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

#include "runtime/log.h"
#include "runtime/memory.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace force {
namespace {

struct Rule {
    enum Kind { Func, Write, Lr } kind = Func;
    uint32_t addr = 0;
    uint32_t size = 4;
    uint64_t value = 0;
    bool logged = false;
};

std::vector<Rule> g_rules;
bool g_loaded = false;
std::mutex g_lock;

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
        if (eq == std::string::npos) {
            LOG_WARN("force", "rule '%s' has no '='", item.c_str());
            continue;
        }
        const std::string lhs = item.substr(0, eq);
        const uint64_t value = std::strtoull(item.c_str() + eq + 1, nullptr, 0);
        Rule r;
        r.value = value;
        if (lhs.size() > 2 && lhs[0] == 'l' && lhs[1] == 'r') {
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
                 r.kind == Rule::Lr ? "lr" : "write", r.addr,
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
                case 1: vm::ptr(r.addr)[0] = static_cast<uint8_t>(r.value); break;
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

void on_call(uint32_t fn_addr) {
    load();
    if (g_rules.empty()) return;
    std::lock_guard lk(g_lock);
    for (Rule& r : g_rules) {
        if (r.kind != Rule::Write || r.addr == 0 || r.size != 1) continue;
        /* byte writes are applied here so they land before the function reads */
        if (vm::range_mapped(r.addr, 1)) vm::ptr(r.addr)[0] = static_cast<uint8_t>(r.value);
    }
}

}  // namespace force
