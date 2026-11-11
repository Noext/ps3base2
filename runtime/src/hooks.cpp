#include "ppu/ops.h"
#include "runtime/codewatch.h"
#include "runtime/host.h"
#include "runtime/force.h"
#include "runtime/thread.h"
#include "runtime/critpath.h"
#include "runtime/hooks.h"
#include "runtime/game_profile.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/jobsys.h"
#include "jobsys/jobs_graph.h"
#include "runtime/image.h"
#include "runtime/reservation.h"
#include "runtime/trace.h"
#include "jobsys/edge_consumer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>

namespace spu {
void status_dump();
std::string brief();
}

namespace lv2 {
void rsx_status();
}

namespace guest {
void status_dump();
}

namespace hooks {

namespace {

/* Set an image's flag byte for an address. The one place that writes the flags,
 * so a bad address is reported once, loudly, instead of silently indexing past
 * an array. Returns false when the address belongs to no image. */
static bool set_hook_flag(uint32_t addr) {
    uint8_t* f = ppu::hook_flag_for(addr);
    if (!f) {
        LOG_WARN("hook", "no image owns 0x%08x: hook flag NOT set (this is a lift/layout bug, not a game bug)", addr);
        return false;
    }
    *f = 1;
    return true;
}

struct Dump {
    int reg;
    uint32_t len;
    uint32_t ptr = 0;
    int derefs = 1;
};

struct Hook {
    uint32_t addr = 0;
    bool ring = false;
    bool panic = false;
    uint8_t cp = 0;
    bool quiet = false;
    bool (*guard)(ppu::Context&) = nullptr;
    std::vector<Dump> dumps;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint32_t> logged{0};
};

std::unordered_map<uint32_t, Hook*> g_hooks;
std::unordered_map<uint32_t, std::string> g_names;
uint32_t g_max = 40;
double g_at = 0.0;
bool g_count_only = false;
thread_local int t_depth = 0;

struct RingEntry {
    double t;
    uint32_t addr;
    uint32_t lr;
    uint32_t tid;
    uint32_t dump_ea;
    uint64_t r[6];
    uint32_t data[16];
};

constexpr uint32_t RING_SIZE = 8192;
RingEntry g_ring[RING_SIZE];
std::atomic<uint64_t> g_ring_pos{0};
std::atomic<int> g_panics{0};
std::mutex g_capture_lock;

uint32_t dump_target(const Dump& d, const ppu::Context& ctx) {
    if (d.reg >= 0) return static_cast<uint32_t>(ctx.gpr[d.reg]);
    uint32_t ea = d.ptr;
    for (int k = 0; k < d.derefs && ea; k++) ea = vm::is_mapped(ea) ? vm::read32(ea) : 0;
    return ea;
}

void record(const Hook* h, const ppu::Context& ctx) {
    uint64_t i = g_ring_pos.fetch_add(1, std::memory_order_relaxed);
    RingEntry& e = g_ring[i % RING_SIZE];
    e.t = logging::uptime();
    e.addr = h->addr;
    e.lr = static_cast<uint32_t>(ctx.lr);
    e.tid = host::tid();
    for (int r = 0; r < 6; r++) e.r[r] = ctx.gpr[3 + r];
    e.dump_ea = 0;
    if (!h->dumps.empty()) {
        uint32_t ea = dump_target(h->dumps[0], ctx);
        if (ea && vm::range_mapped(ea, 64)) {
            e.dump_ea = ea;
            for (int k = 0; k < 16; k++) e.data[k] = vm::read32(ea + k * 4);
        }
    }
}

std::string guest_string(uint32_t ea) {
    std::string out;
    while (ea && out.size() < 200 && vm::is_mapped(ea)) {
        char ch = static_cast<char>(vm::g_base[ea++]);
        if (!ch) break;
        out += ch;
    }
    return out;
}

void guest_backtrace(const ppu::Context& ctx) {
    std::string bt;
    char b[32];
    std::snprintf(b, sizeof b, " lr %08x", static_cast<uint32_t>(ctx.lr));
    bt += b;
    uint32_t sp = static_cast<uint32_t>(ctx.gpr[1]);
    for (int i = 0; i < 32 && sp && vm::range_mapped(sp, 24); i++) {
        uint32_t next = static_cast<uint32_t>(vm::read64(sp));
        if (!next || next <= sp || !vm::range_mapped(next, 24)) break;
        uint32_t lr = static_cast<uint32_t>(vm::read64(next + 16));
        std::snprintf(b, sizeof b, " %08x", lr);
        bt += b;
        sp = next;
    }
    LOG_INFO("blackbox", "guest backtrace:%s", bt.c_str());
}

std::string thread_name() {
    return host::thread_name();
}

std::string hex_dump(uint32_t ea, uint32_t len) {
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

void load_names(const char* path) {
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string a, n;
        if (!(ss >> a >> n) || a.empty() || a[0] == '#') continue;
        g_names[static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16))] = n;
    }
}

bool benign_game_error(const std::string& msg) { return msg.rfind("BATCHJOB: AddJob: waiting for room", 0) == 0; }


/* Generic lifted-call tracer for ONE thread: every entry and exit with depth,
 * r3 in, r3 out and duration. PS3BASE_CALL_TRACE=<thread name>. The tree is
 * what shows which branch a worker actually took, and what made it leave. */
namespace calltrace {
struct Frame {
    uint32_t addr;
    uint64_t r3_in;
    uint64_t t0;
};
thread_local std::vector<Frame> t_stack;

bool enabled_for_current() {
    static const std::string want = std::getenv("PS3BASE_CALL_TRACE")
                                        ? std::getenv("PS3BASE_CALL_TRACE")
                                        : std::string();
    if (want.empty()) return false;
    guest::Thread* t = guest::current();
    return t && t->name == want;
}

void enter(uint32_t addr, uint64_t r3) {
    const int depth = static_cast<int>(t_stack.size());
    static std::atomic<uint64_t> n{0};
    const uint64_t k = n.fetch_add(1) + 1;
    if (k <= 20000)
        LOG_INFO("calltree", "%*s-> f_%08x #%llu r3=0x%llx", depth * 2, "", addr,
                 static_cast<unsigned long long>(k), static_cast<unsigned long long>(r3));
    t_stack.push_back({addr, r3, ppu::timebase()});
}

void peek(uint32_t addr, ppu::Context& ctx) {
    if (addr != 0x188428u) return;
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    if (!vm::range_mapped(obj + 28, 1)) return;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1);
    {
        /* Decode the two head-of-loop exit tests exactly.
         * L1084: bc_cond(12,2) with cr0.eq = (byte==0) -> exits when byte==0
         * L1088: bc_cond(4,2)  -> exits when byte!=0                             */
        const unsigned b28 = vm::ptr(obj)[28];
        const unsigned g = vm::range_mapped(0xc82601, 1) ? vm::ptr(0xc82601)[0] : 0xff;
        static std::atomic<int> m{0};
        if (m.fetch_add(1) < 40)
            LOG_INFO("loopdec", "#%d obj=0x%x b28=%u -> L1084 %s ; global=%u -> L1088 %s",
                     m.load(), obj, b28, b28 == 0 ? "EXIT" : "continue",
                     g, g != 0 ? "EXIT" : "continue");
    }
    if (k == 0 && vm::range_mapped(obj, 0x200))
        LOG_INFO("looppeek", "TRAPADDR obj=0x%x quit_ea=0x%x", obj, obj + 28);
    if (k < 60)
        LOG_INFO("looppeek", "#%d obj=0x%x bytes24..31=%02x%02x%02x%02x%02x%02x%02x%02x "
                 "b28=%u b29=%u global=0x%x",
                 k, obj,
                 vm::ptr(obj)[24], vm::ptr(obj)[25], vm::ptr(obj)[26], vm::ptr(obj)[27],
                 vm::ptr(obj)[28], vm::ptr(obj)[29], vm::ptr(obj)[30], vm::ptr(obj)[31],
                 static_cast<unsigned>(vm::ptr(obj)[28]),
                 static_cast<unsigned>(vm::ptr(obj)[29]),
                 vm::range_mapped(0xc82601, 1) ? static_cast<unsigned>(vm::ptr(0xc82601)[0]) : 0xff);
}

void leave(ppu::Context& ctx) {
    if (t_stack.empty()) return;
    Frame f = t_stack.back();
    t_stack.pop_back();
    const int depth = static_cast<int>(t_stack.size());
    const uint64_t us = (ppu::timebase() - f.t0) / 79.8;
    LOG_INFO("calltree", "%*s<- f_%08x r3_out=0x%llx %llu us", depth * 2, "", f.addr,
             static_cast<unsigned long long>(ctx.gpr[3]),
             static_cast<unsigned long long>(us));
}
}  // namespace calltrace

void impl_body(Hook* h, ppu::Context& ctx, uint8_t* base, uint32_t addr, ppu::BodyFn fn);

void impl(ppu::Context& ctx, uint8_t* base, uint32_t addr, ppu::BodyFn fn) {
    /* hook_call already showed impl is armed when f_005d2b44 is entered, yet the
     * dispatcher's own probe never fired. Print what addr actually arrives, and
     * whether the hook map holds it. */
    {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 10)
            std::fprintf(stderr, "[impl-entry] addr=0x%08x in_map=%d\n", addr,
                         g_hooks.count(addr) ? 1 : 0);
    }
    if (calltrace::enabled_for_current()) {
        calltrace::enter(addr, ctx.gpr[3]);
        calltrace::peek(addr, ctx);
        fn(ctx, base);
        calltrace::leave(ctx);
        return;
    }
    auto found = g_hooks.find(addr);
    Hook* h = found != g_hooks.end() ? found->second : nullptr;
    if (!h) {
        fn(ctx, base);
        return;
    }
    if (h->cp) {
        critpath::Call c;
        critpath::enter(h->cp, ctx, c);
        impl_body(h, ctx, base, addr, fn);
        critpath::leave(h->cp, ctx, c);
        return;
    }
    impl_body(h, ctx, base, addr, fn);
}

void impl_body(Hook* h, ppu::Context& ctx, uint8_t* base, uint32_t addr, ppu::BodyFn fn) {
    if (addr == 0x5d2b44u) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 6)
            std::fprintf(stderr, "[implbody] addr=0x%08x guard=%s quiet=%d\n", addr,
                         h->guard ? "set" : "NULL", h->quiet ? 1 : 0);
    }
    h->calls.fetch_add(1, std::memory_order_relaxed);
    if (h->guard && h->guard(ctx)) return;
    if (h->quiet) {
        fn(ctx, base);
        return;
    }
    if (h->panic) {
        record(h, ctx);
        std::string msg = guest_string(static_cast<uint32_t>(ctx.gpr[3]));
        LOG_WARN("blackbox", "game error call %s lr %08x: '%s' r4=%llx r5=%llx r6=%llx", name_of(addr).c_str(), static_cast<uint32_t>(ctx.lr), msg.c_str(), static_cast<unsigned long long>(ctx.gpr[4]),
                 static_cast<unsigned long long>(ctx.gpr[5]), static_cast<unsigned long long>(ctx.gpr[6]));
        guest_backtrace(ctx);
        if (!benign_game_error(msg) && g_panics.fetch_add(1) < 2) capture("game error");
        fn(ctx, base);
        return;
    }
    if (h->ring) {
        record(h, ctx);
        fn(ctx, base);
        return;
    }
    bool log = !g_count_only && logging::uptime() >= g_at && h->logged.load(std::memory_order_relaxed) < g_max;
    if (!log) {
        fn(ctx, base);
        return;
    }
    uint32_t n = h->logged.fetch_add(1, std::memory_order_relaxed);
    const std::string& name = name_of(addr);
    std::string args;
    char b[64];
    for (int r = 3; r <= 8; r++) {
        std::snprintf(b, sizeof b, " r%d=%llx", r, static_cast<unsigned long long>(ctx.gpr[r]));
        args += b;
    }
    std::snprintf(b, sizeof b, " f1=%g f2=%g", ctx.fpr[1], ctx.fpr[2]);
    args += b;
    LOG_INFO("hook", "%*s-> %s #%u lr %08x [%s]%s", t_depth * 2, "", name.c_str(), n, static_cast<uint32_t>(ctx.lr), thread_name().c_str(), args.c_str());
    for (const Dump& d : h->dumps) {
        if (d.reg < 0) {
            uint32_t ea = d.ptr;
            for (int k = 0; k < d.derefs && ea; k++) ea = vm::is_mapped(ea) ? vm::read32(ea) : 0;
            LOG_INFO("hook", "%*s   *%08x@%08x:%s", t_depth * 2, "", d.ptr, ea, ea && vm::range_mapped(ea, d.len) ? hex_dump(ea, d.len).c_str() : " unmapped");
            continue;
        }
        uint32_t ea = static_cast<uint32_t>(ctx.gpr[d.reg]);
        LOG_INFO("hook", "%*s   r%d@%08x:%s", t_depth * 2, "", d.reg, ea, hex_dump(ea, d.len).c_str());
    }
    uint64_t r1 = ctx.gpr[1];
    t_depth++;
    fn(ctx, base);
    t_depth--;
    LOG_INFO("hook", "%*s<- %s #%u r3=%llx f1=%g%s", t_depth * 2, "", name.c_str(), n, static_cast<unsigned long long>(ctx.gpr[3]), ctx.fpr[1], ctx.gpr[1] != r1 ? " (stack mismatch)" : "");
}



std::string batch_state(uint32_t obj) {
    std::string s;
    char b[160];
    std::snprintf(b, sizeof b, "added %u done %u cur %u lists %u mode %u per %u", vm::read32(obj + 0xcc), vm::read32(obj + 0xd0), vm::read32(obj + 0xd4), vm::read32(obj + 0xd8), vm::read32(obj + 0xdc), vm::read32(obj + 0xc8));
    s += b;
    uint32_t hdr = vm::read32(obj);
    if (hdr && vm::range_mapped(hdr, 0x80)) {
        for (uint32_t i = 0; i < 4; i++) {
            uint32_t st = hdr + 0x40 + i * 0x10;
            std::snprintf(b, sizeof b, " | L%u n%u st", i, vm::read32(obj + 0x2c + i * 0x14));
            s += b;
            for (uint32_t k = 0; k < 8; k++) {
                std::snprintf(b, sizeof b, " %04x", vm::read16(st + k * 2));
                s += b;
            }
        }
        std::snprintf(b, sizeof b, " | hdr %08x %08x %08x %08x", vm::read32(hdr), vm::read32(hdr + 4), vm::read32(hdr + 8), vm::read32(hdr + 12));
        s += b;
    }
    const uint32_t spurs = 0x3001ad00;
    uint32_t wid = vm::read32(obj + 8) & 31;
    std::snprintf(b, sizeof b, " | wid %u ready %u cont %02x pend %02x idling %02x ssm %02x sig %04x flagrecv %02x", wid, vm::g_base[spurs + (wid & 15) + (wid < 16 ? 0 : 0x10)], vm::g_base[spurs + 0x20 + (wid & 15)],
                  vm::g_base[spurs + 0x30 + (wid & 15)], vm::g_base[spurs + 0x73], vm::g_base[spurs + 0x72], vm::read16(spurs + 0x70), vm::g_base[spurs + 0x77]);
    s += b;
    return s;
}

void batch_diag_thread() {
    using clk = std::chrono::steady_clock;
    std::vector<uint8_t> last;
    auto since = clk::now();
    auto wait_since = clk::now();
    int wait_kind = 0;
    bool reported = false, wait_reported = false;
    int samples = 0;
    double thresh = 20.0;
    if (const char* e = std::getenv("GOWA_BATCH_DIAG")) thresh = (std::max)(1.0, std::atof(e));
    for (;;) {
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        uint32_t obj = vm::is_mapped(0x858c2c) ? vm::read32(0x858c2c) : 0;
        if (!obj || !vm::range_mapped(obj, 0xe0)) continue;
        uint32_t hdr = vm::read32(obj);
        if (!hdr || !vm::range_mapped(hdr, 0x80)) continue;
        uint32_t added = vm::read32(obj + 0xcc), done = vm::read32(obj + 0xd0), mode = vm::read32(obj + 0xdc);
        uint32_t per = vm::read32(obj + 0xc8), lists = vm::read32(obj + 0xd8);
        int kind = 0;
        if (mode == 2 && added != done) kind = 2;
        else if (mode == 1 && per && int32_t((lists + 4) * per - added) <= 0) kind = 1;
        auto now = clk::now();
        if (kind != wait_kind) {
            double ms = std::chrono::duration<double, std::milli>(now - wait_since).count();
            if (wait_kind && ms > thresh) LOG_WARN("batchdiag", "ppu %s wait ended after %.1f ms: %s", wait_kind == 2 ? "finish" : "addjob", ms, batch_state(obj).c_str());
            wait_kind = kind;
            wait_since = now;
            wait_reported = false;
            samples = 0;
        } else if (kind) {
            double ms = std::chrono::duration<double, std::milli>(now - wait_since).count();
            if (ms > thresh && samples < 8 && ms > thresh * (1 << samples)) {
                samples++;
                LOG_WARN("batchdiag", "ppu %s wait %.1f ms: %s |%s", kind == 2 ? "finish" : "addjob", ms, batch_state(obj).c_str(), spu::brief().c_str());
                if (!wait_reported) {
                    wait_reported = true;
                    static std::atomic<int> dumps{0};
                    if (dumps.fetch_add(1) < 4) spu::status_dump();
                }
            }
        }
        std::vector<uint8_t> sig(vm::g_base + hdr + 0x40, vm::g_base + hdr + 0x80);
        sig.push_back(uint8_t(added));
        sig.push_back(uint8_t(done));
        bool pending = added != done;
        if (!pending || sig != last) {
            if (reported) LOG_WARN("batchdiag", "resolved after %.1f ms: %s", std::chrono::duration<double, std::milli>(now - since).count(), batch_state(obj).c_str());
            reported = false;
            last = sig;
            since = now;
            continue;
        }
        double ms = std::chrono::duration<double, std::milli>(now - since).count();
        if (!reported && ms > thresh) {
            reported = true;
            LOG_WARN("batchdiag", "no progress for %.1f ms (mode %u): %s |%s", ms, mode, batch_state(obj).c_str(), spu::brief().c_str());
        }
    }
}

void summary_thread() {
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::string s;
        for (auto& [a, h] : g_hooks) {
            char b[96];
            std::snprintf(b, sizeof b, " %s=%llu", name_of(a).c_str(), static_cast<unsigned long long>(h->calls.load()));
            s += b;
        }
        LOG_INFO("hook", "calls:%s", s.c_str());
    }
}

}

const std::string& name_of(uint32_t addr) {
    static std::mutex m;
    static std::unordered_map<uint32_t, std::string> cache;
    std::lock_guard lk(m);
    auto it = g_names.find(addr);
    if (it != g_names.end()) return it->second;
    auto& c = cache[addr];
    if (c.empty()) {
        char b[16];
        std::snprintf(b, sizeof b, "f_%08x", addr);
        c = b;
    }
    return c;
}

void capture(const char* reason) {
    std::lock_guard lk(g_capture_lock);
    static int n = 0;
    LOG_WARN("blackbox", "capture #%d: %s", n, reason);
    uint64_t end = g_ring_pos.load();
    uint64_t start = end > RING_SIZE ? end - RING_SIZE : 0;
    for (uint64_t i = start; i < end; i++) {
        const RingEntry& e = g_ring[i % RING_SIZE];
        std::string data;
        if (e.dump_ea) {
            char b[12];
            data = " @";
            std::snprintf(b, sizeof b, "%08x:", e.dump_ea);
            data += b;
            for (int k = 0; k < 16; k++) {
                std::snprintf(b, sizeof b, " %08x", e.data[k]);
                data += b;
            }
        }
        LOG_INFO("blackbox", "ring %.6f %s lr %08x tid %u r3=%llx r4=%llx r5=%llx r6=%llx r7=%llx r8=%llx%s", e.t, name_of(e.addr).c_str(), e.lr, e.tid, static_cast<unsigned long long>(e.r[0]),
                 static_cast<unsigned long long>(e.r[1]), static_cast<unsigned long long>(e.r[2]), static_cast<unsigned long long>(e.r[3]), static_cast<unsigned long long>(e.r[4]),
                 static_cast<unsigned long long>(e.r[5]), data.c_str());
    }
    spu::status_dump();
    guest::status_dump();
    lv2::rsx_status();
    static const bool no_mem = std::getenv("GOWA_BLACKBOX_NOMEM") != nullptr;
    if (!no_mem && n < 2) {
        std::filesystem::create_directories("blackbox");
        char path[64];
        std::snprintf(path, sizeof path, "blackbox/memory_%d.bin", n);
        trace::dump_memory(path);
    }
    n++;
    LOG_WARN("blackbox", "capture done");
}

bool skip_repeated_mip_eviction(ppu::Context& ctx) {
    uint32_t slot = static_cast<uint32_t>(ctx.gpr[4]);
    if (!vm::range_mapped(slot, 0x48)) return false;
    uint32_t record = vm::read32(slot + 0x44);
    if (!record || !vm::range_mapped(record, 0x20)) return false;
    uint32_t evict = vm::read32(slot + 0x1c);
    uint32_t size = vm::read32(record + 0xc);
    if (size >= evict) return false;
    static std::atomic<uint32_t> logged{0};
    if (logged.fetch_add(1) < 8) LOG_WARN("patch", "skipped repeated top-mip eviction of record 0x%08x (size 0x%x, evict 0x%x)", record, size, evict);
    return true;
}

bool wait_previous_edge_list_started(ppu::Context& ctx) {
    uint32_t cur = vm::read32(0xa0c13c);
    if (cur > 2) return false;
    uint32_t prev = (cur + 2) % 3;
    if (!*vm::ptr(0xa0c12b + prev)) return false;
    uint32_t list = vm::read32(0xa0c11c + prev * 4);
    if (!list || !vm::range_mapped(list, 0x30)) return false;
    uint32_t state = vm::read32(list);
    if (!state || !vm::range_mapped(state + 0x70, 2)) return false;
    if (vm::read16(state + 0x70)) return false;
    auto start = std::chrono::steady_clock::now();
    uint64_t cp0 = critpath::g_on ? __rdtsc() : 0;
    for (uint32_t i = 0; !vm::read16(state + 0x70); i++) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(200)) {
            LOG_WARN("patch", "edge list %u did not start within 200 ms", prev);
            return false;
        }
        if (i < 256) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (cp0) critpath::emit_wait(critpath::K_EDGE_GUARD, cp0, __rdtsc(), prev, list, 0);
    static std::atomic<uint32_t> logged{0};
    if (logged.fetch_add(1) < 8) {
        LOG_WARN("patch", "waited %.3f ms for edge list %u to pass its MeshPrep barrier", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(), prev);
    }
    return false;
}

bool restore_post_pass_keys(ppu::Context&) {
    if (const char* e = std::getenv("GOWA_POST_KEYS"); e && std::string(e) == "0") return false;
    uint64_t base = vm::read32(0x83d004) ? 0 : vm::read64(0x397400);
    struct Slot {
        uint32_t addr;
        uint64_t pass;
    };
    static const Slot slots[] = {{0x9abfb8, 4}, {0x9abfa0, 6}};
    static const bool blur = [] {
        const char* e = std::getenv("GOWA_MOTION_BLUR");
        return !(e && std::string(e) == "0");
    }();
    for (const Slot& s : slots) {
        if (s.pass == 4 && !blur) continue;
        if (vm::read64(s.addr)) continue;
        uint64_t key = (base & 0x0c3ffffffffffff0ull) | 0x9000000000000000ull | (uint64_t(5) << 54) | s.pass;
        vm::write64(s.addr, key);
        LOG_INFO("patch", "render pass key 0x%08x = 0x%016llx", s.addr, static_cast<unsigned long long>(key));
    }
    return false;
}

void tick_patches() {
    static const bool off = [] {
        const char* e = std::getenv("GOWA_GAME_PATCHES");
        return e && std::string(e) == "0";
    }();
    struct Boost {
        uint32_t wid;
        uint8_t prio[8];
        bool done;
    };
    static std::vector<Boost> boosts = [] {
        std::vector<Boost> v;
        std::string spec = std::getenv("GOWA_SPURS_PRIO") ? std::getenv("GOWA_SPURS_PRIO") : "";
        size_t pos = 0;
        while (pos < spec.size()) {
            size_t end = spec.find(';', pos);
            if (end == std::string::npos) end = spec.size();
            std::string item = spec.substr(pos, end - pos);
            size_t colon = item.find(':');
            if (colon != std::string::npos) {
                Boost b{static_cast<uint32_t>(std::atoi(item.substr(0, colon).c_str())), {}, false};
                std::string rest = item.substr(colon + 1);
                size_t q = 0;
                for (int i = 0; i < 8 && q <= rest.size(); i++) {
                    size_t c = rest.find(',', q);
                    if (c == std::string::npos) c = rest.size();
                    b.prio[i] = static_cast<uint8_t>(std::atoi(rest.substr(q, c - q).c_str()));
                    q = c + 1;
                }
                v.push_back(b);
            }
            pos = end + 1;
        }
        return v;
    }();
    if (off) return;
    const uint32_t spurs = 0x3001ad00;
    if (!vm::range_mapped(spurs, 0x1100)) return;
    bool changed = false;
    for (Boost& b : boosts) {
        if (b.done || b.wid >= 32) continue;
        if ((vm::read32(spurs + 0xB0) & (0x80000000u >> b.wid)) == 0) continue;
        uint32_t info = spurs + (b.wid < 16 ? 0xB00 : 0x1000) + (b.wid & 15) * 0x20;
        if (!vm::read32(info + 4)) continue;
        uint64_t prio = vm::read64(info + 0x18);
        if (!prio) continue;
        uint8_t old[8];
        for (int i = 0; i < 8; i++) old[i] = uint8_t(prio >> (56 - 8 * i));
        bool ok = true;
        for (int i = 0; i < 8; i++) {
            if ((old[i] == 0) != (b.prio[i] == 0)) ok = false;
        }
        b.done = true;
        if (!ok) {
            LOG_WARN("patch", "SPURS workload %u priority layout differs, not changed", b.wid);
            continue;
        }
        rsv::store_locked(info + 0x18, b.prio, 8);
        changed = true;
        LOG_WARN("patch", "SPURS workload %u priority %u,%u,%u,%u,%u,%u -> %u,%u,%u,%u,%u,%u", b.wid, old[0], old[1], old[2], old[3], old[4], old[5], b.prio[0], b.prio[1], b.prio[2], b.prio[3], b.prio[4], b.prio[5]);
    }
    if (changed) {
        uint8_t ff = 0xFF;
        rsv::store_locked(spurs + 0xBD, &ff, 1);
        rsv::store_locked(spurs + 0x72, &ff, 1);
    }
}

void install_critpath() {
    if (!critpath::g_on) return;
    int n = 0;
    const critpath::Hook* t = critpath::hook_table(n);
    for (int i = 0; i < n; i++) {
        Hook*& h = g_hooks[t[i].addr];
        if (!h) {
            h = new Hook;
            h->addr = t[i].addr;
            h->quiet = true;
        }
        h->cp = t[i].kind;
        set_hook_flag(t[i].addr);
    }
}

void install_patches() {
    if (const char* e = std::getenv("GOWA_GAME_PATCHES"); e && std::string(e) == "0") return;
    struct Patch {
        uint32_t addr;
        bool (*guard)(ppu::Context&);
    };
    static const Patch patches[] = {{0x39ea58, skip_repeated_mip_eviction}, {0x41c480, wait_previous_edge_list_started}, {0x2d0258, fps_unlock_timestep}, {0x397540, restore_post_pass_keys}};
    for (const Patch& p : patches) {
        Hook*& h = g_hooks[p.addr];
        if (!h) {
            h = new Hook;
            h->addr = p.addr;
            h->ring = true;
        }
        h->guard = p.guard;
        set_hook_flag(p.addr);
    }
}

void add_guard(uint32_t addr, bool (*guard)(ppu::Context&)) {
    Hook*& h = g_hooks[addr];
    if (!h) {
        h = new Hook;
        h->addr = addr;
        h->quiet = true;
    }
    if (h->guard) LOG_WARN("hook", "guard at 0x%08x replaced by jobsys", addr);
    h->guard = guard;
    set_hook_flag(addr);
    /* MEASURED TRAP, guarded here rather than at each call site: a guard added
     * after hooks::init() left hook_impl null, and the lifted code only consults
     * hook_flags when hook_impl is set, so the guard was registered and never
     * called. Setting it here makes add_guard self-sufficient. */
    if (!ppu::hook_impl) ppu::hook_impl = impl;
}

void install_ppujobs() {
    jobsys::wws::install(add_guard);
    wj::install(add_guard);
}

void install_jobsys_edge() {
    if (!jsedge::enabled()) return;
    jsedge::init();
    Hook*& h = g_hooks[0x41cbdc];
    if (!h) {
        h = new Hook;
        h->addr = 0x41cbdc;
        h->quiet = true;
    }
    jsedge::set_prev_guard(h->guard);
    h->guard = jsedge::on_wait_previous;
    set_hook_flag(0x41cbdc);
    Hook*& sp = g_hooks[0x49cd6c];
    if (!sp) {
        sp = new Hook;
        sp->addr = 0x49cd6c;
        sp->quiet = true;
    }
    sp->guard = jsedge::on_set_priorities;
    set_hook_flag(0x49cd6c);
}

void hog_thread(int lp, int on_ms, int off_ms, double from) {
    host::set_affinity({lp});
    host::set_thread_name("test hog");
    while (logging::uptime() < from) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_WARN("hog", "normal-priority hog on lp %d (%d ms on, %d ms off)", lp, on_ms, off_ms);
    volatile uint64_t x = 0;
    for (;;) {
        auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(on_ms);
        while (std::chrono::steady_clock::now() < end) {
            for (int i = 0; i < 1000; i++) x = x + 1;
        }
        if (off_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(off_ms));
    }
}

void start_hogs(const char* spec) {
    double from = std::getenv("GOWA_TEST_HOG_FROM") ? std::atof(std::getenv("GOWA_TEST_HOG_FROM")) : 90.0;
    std::string s = spec;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t end = s.find(';', pos);
        if (end == std::string::npos) end = s.size();
        int lp = -1, on = 1000, off = 0;
        std::sscanf(s.substr(pos, end - pos).c_str(), "%d:%d:%d", &lp, &on, &off);
        if (lp >= 0 && lp < 64) std::thread(hog_thread, lp, on, off, from).detach();
        pos = end + 1;
    }
}

/* PS3BASE_MORTON_TRACE=1: when the Morton copy for the glyph atlas runs, log the
 * object and the source pointer it reads. The writer of that buffer is the
 * rasteriser, and this finds it without guessing. */
static void morton_trace(ppu::Context& ctx)
{
    static const bool on = std::getenv("PS3BASE_MORTON_TRACE") != nullptr;
    if (!on) return;
    static std::atomic<uint32_t> n{0};
    if (n.fetch_add(1) > 60) return;
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    LOG_INFO("morton", "obj 0x%08x  [obj+24]=0x%08x [obj+28]=0x%08x  [obj+0]=0x%08x",
             obj,
             obj ? vm::read32(obj + 24) : 0,
             obj ? vm::read32(obj + 28) : 0,
             obj ? vm::read32(obj + 0) : 0);
}

/* PS3BASE_LIST_GUARD=1: before f_0064c044 passes a node to f_004e2ac0, check
 * that the node is mapped and that its +28/+32 fields are not pointers into the
 * title's own text. When one of them IS, the walk has left the structure: the
 * crash that follows is a SIGSEGV two levels down inside ppu::ld32, in the
 * wrong thread's frames, readable only from a memory dump taken afterwards.
 * This names the node and both values at the moment it happens. Off by default.
 *
 * The title's text runs 0x10000..0xba3688 (bin/mc/mc.elf, first LOAD). A data
 * pointer never lands there; an instruction word copied into a pointer field
 * always does. That is exactly what the two dumps show: +28 held 0x00721f30
 * ('stw r3,0(r26)') in one and 0x00730100 in the other. */
/* The guard also ARMS the line watch. Every node the walk reaches is a line of
 * memory whose writer matters, and arming it here means the writer is already
 * being recorded before the corruption is seen -- the log then contains the
 * exact store, with its thread, guest lr and value. This is the "permanent watch
 * on the word" the investigation needed; it is free at run time (one bit set per
 * new line, then one array read on each store). */
static bool list_guard(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_LIST_GUARD") != nullptr;
    if (!on) return false;
    /* Entry census. The walk is NOT called during ordinary play -- measured:
     * 0 invocations in 400 s of a healthy run -- so arming lines from it can
     * never pre-empt the crash. Naming *when* it runs is what makes the watch
     * usable: the corruption happens around a save or a chunk update, so the
     * window to instrument is that call, not the whole session. */
    static std::atomic<uint32_t> calls{0};
    {
        const uint32_t n = calls.fetch_add(1, std::memory_order_relaxed);
        if (n < 40) {
            guest::Thread* t = guest::current();
            LOG_INFO("listguard", "call #%u t=%.2f thread %s r30=0x%08x r29=0x%08x lr=0x%08x",
                     n, logging::uptime(), t ? t->name.c_str() : "?",
                     static_cast<uint32_t>(ctx.gpr[30]), static_cast<uint32_t>(ctx.gpr[29]),
                     static_cast<uint32_t>(ctx.lr));
        }
    }
    /* THE NODE IS NOT IN r30 AT ENTRY. Measured, and it invalidated the guard:
     *
     *   f_0064c044_body:  save r29, save r31, r29 = r3, r31 = r4,
     *                     ld32[r29+40], SAVE r30, ld32[r29+52],
     *                     r30 = ld32[r29+36]      <-- the node, this late
     *
     *   f_004e2ac0_body:  r30 = r3, r31 = r4, ld32[r30+0] ...
     *
     * so at entry r30 still holds the caller's saved r30, and r29 holds the
     * *caller's* frame pointer (0xd0xxxxxx = a guest stack address), not the
     * object. The guard read both at entry and therefore inspected unrelated
     * stack memory. That is why it "never fired" in the earlier runs and why,
     * once it finally ran, it reported 8 nodes whose +28/+32 were UTF-16 text
     * ("\0.\00", "\0i\0\0"): a stack frame, not a list node.
     *
     * The object IS available at entry: it is r3, and the walk starts from
     * ld32[obj+52] + ld32[obj+36]*4. Reading the first node from there is what
     * the lifted code does four instructions later, and it is exact. */
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    if (!obj || !vm::range_mapped(obj, 56)) return false;
    const uint32_t count = vm::read32(obj + 36);
    const uint32_t table = vm::read32(obj + 52);
    if (!table || !vm::range_mapped(table + count * 4, 4)) return false;
    const uint32_t head = vm::read32(table + count * 4);
    const uint32_t node = head ? head - 32 : 0;
    cw::note_line(obj);
    cw::note_line(table);
    if (!node) return false;
    if (vm::range_mapped(node, 40)) {
        cw::note_line(node);
        cw::note_line(node + 32);
    }
    if (!vm::range_mapped(node, 36)) {
        LOG_ERROR("listguard", "node 0x%08x (head 0x%08x) is not mapped (obj=0x%08x table=0x%08x count=%u)",
                  node, head, obj, table, count);
        return false;
    }
    const uint32_t f28 = vm::read32(node + 28);
    const uint32_t f32 = vm::read32(node + 32);
    /* A word of the title's code is 4-aligned, inside the executable range, and
     * NOT bit-identical to UTF-16 text. 0x002e0030 (".0") and 0x0046006c ("lF")
     * are text; 0x00721f30 is an instruction. Requiring the low half not to be
     * printable ASCII is what separates them -- measured on all seven dumps. */
    auto is_code = [](uint32_t v) {
        if ((v & 3) != 0) return false;
        if (v < 0x00010200u || v >= 0x008aa674u) return false;
        const uint32_t hi = v >> 16, lo = v & 0xFFFF;
        auto printable = [](uint32_t h) { return h >= 0x20 && h <= 0x7E; };
        if ((hi == 0 || printable(hi)) && (lo == 0 || printable(lo))) return false;
        return true;
    };
    if (is_code(f28)) {
        LOG_ERROR("listguard", "node 0x%08x [28]=0x%08x is a CODE WORD, not an object (obj=0x%08x next=0x%08x)",
                  node, f28, obj, f32);
    }
    if (is_code(f32)) {
        LOG_ERROR("listguard", "node 0x%08x [32]=0x%08x is a CODE WORD, not a next pointer (obj=0x%08x obj28=0x%08x)",
                  node, f32, obj, f28);
    }
    return false;   /* observe only: this must never change what the guest sees */
}

/* piste 43 -- the load-state probe.
 *
 * f_002eee64 is the arbiter of the world-load path: it dispatches on
 * obj+2924 and, in state 5 ONLY, computes the OPD 0xbc0000-6152 = 0xbbe7f8
 * and calls f_002e8ae8. That OPD is the record for f_002eeae4, the sole
 * writer of the thumbnail size/pointer at obj+168/+172 -- so if the state
 * never reaches 5, the thumbnail is never registered and f_002e8ae8 takes
 * its hard-coded (0,0) branch. This probe reports the state and the four
 * fields the state-5 arm reads, without changing anything the guest sees.
 *
 * Environment: PS3BASE_LOAD_PROBE=1 */
/* PS3BASE_THREAD_PROBE: is the RAGE worker entry actually executed, and how far
 * does it get?
 *
 * Red Dead Redemption creates its pool threads through the OPD table at
 * 0x01356420, well above the title text_hi (0x010d9f74). call_opd reads the pair
 * and lookup() finds f_00533064, so the machinery should work -- but the thread
 * exits within a millisecond through f_005331e0 -> _sys_ppu_thread_exit(0xdead
 * 0xc0de), and a constant search finds nothing because the title builds that
 * value with lis+oris. This probe answers the question by measurement instead of
 * by reading: both entry and exit are hooked, and the register state at entry is
 * printed, so a missing work item is visible as a null queue pointer. */
static bool thread_entry_probe(ppu::Context& ctx) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 16) {
        const uint32_t ws = static_cast<uint32_t>(ctx.gpr[3]);
        char buf[640];
        int off = 0;
        off += std::snprintf(buf + off, sizeof(buf) - off,
                 "ENTRY 0x533064 #%d th=0x%x r3=0x%08x lr=0x%08x r2=0x%08x t=%.6f",
                 k, ctx.thread_id, ws, static_cast<uint32_t>(ctx.lr),
                 static_cast<uint32_t>(ctx.gpr[2]), ppu::timebase() / 79.8e6);
        if (ws && vm::range_mapped(ws, 0x2c)) {
            off += std::snprintf(buf + off, sizeof(buf) - off,
                     " ws[]=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                     vm::read32(ws), vm::read32(ws + 4), vm::read32(ws + 8), vm::read32(ws + 12),
                     vm::read32(ws + 16), vm::read32(ws + 20), vm::read32(ws + 24),
                     vm::read32(ws + 28), vm::read32(ws + 32), vm::read32(ws + 36),
                     vm::read32(ws + 40));
            /* 0x5331a4: r5 = ws[0]; lwz r4,0(r5); mtctr r4; bctrl -- the work
             * function is the FIRST WORD of the object ws[0] points at. */
            const uint32_t obj = vm::read32(ws);
            if (obj && vm::range_mapped(obj, 16))
                off += std::snprintf(buf + off, sizeof(buf) - off,
                         " ws0=0x%08x ws0[0]=0x%08x (work fn) ws0[4]=0x%08x ws0[8]=0x%08x",
                         obj, vm::read32(obj), vm::read32(obj + 4), vm::read32(obj + 8));
            else
                off += std::snprintf(buf + off, sizeof(buf) - off, " ws0=0x%08x UNMAPPED", obj);
        }
        LOG_INFO("threadprobe", "%s", buf);
    }
    return false;
}

/* The three work functions the worker entry dispatches to through
 * f_00533064_body's call_indirect at 0x5331ac, named by the ledger above. If
 * they are entered, the beq at 0x533148 was NOT taken and the work ran. */
static bool work_probe(ppu::Context& ctx, const char* what) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 24) {
        const uint32_t a = static_cast<uint32_t>(ctx.gpr[3]);
        char buf[512];
        int off = 0;
        off += std::snprintf(buf + off, sizeof(buf) - off, "WORK %s #%d th=0x%x r3=0x%08x t=%.6f",
                             what, k, ctx.thread_id, a, ppu::timebase() / 79.8e6);
        if (vm::range_mapped(0x165d460u, 8))
            off += std::snprintf(buf + off, sizeof(buf) - off, " LIST count=%u head=0x%08x",
                     vm::read32(0x165d460u), vm::read32(0x165d464u));
        if (a && vm::range_mapped(a, 0x2c)) {
            const unsigned b32 = vm::ptr(a + 32)[0];
            off += std::snprintf(buf + off, sizeof(buf) - off,
                     " [32]=0x%02x bit7=%u a[]=%08x %08x %08x %08x %08x %08x %08x %08x",
                     b32, unsigned(b32 >> 7 & 1),
                     vm::read32(a), vm::read32(a + 4), vm::read32(a + 8), vm::read32(a + 12),
                     vm::read32(a + 16), vm::read32(a + 20), vm::read32(a + 24), vm::read32(a + 28));
        }
        /* The exact location the gate tests. f_00533064 spilled the low 32 bits
         * of its own r3 to r1+0xd8 at 0x5330b4 and reloads them at 0x533140;
         * the work function pushes its own frame, so when this guard runs r1 is
         * still the worker frame and r1+0xd8 is untouched since the spill. */
        const uint32_t slot_ea = static_cast<uint32_t>(ctx.gpr[1]) + 0xd8u;
        if (vm::range_mapped(slot_ea, 4))
            off += std::snprintf(buf + off, sizeof(buf) - off,
                     " GATE_SLOT[r1+0xd8=0x%08x]=0x%08x", slot_ea, vm::read32(slot_ea));
        LOG_INFO("threadprobe", "%s", buf);
    }
    return false;
}
static bool work_27e564(ppu::Context& ctx) { return work_probe(ctx, "f_0027e564"); }
static bool work_545d54(ppu::Context& ctx) { return work_probe(ctx, "f_00545d54"); }
static bool work_546724(ppu::Context& ctx) { return work_probe(ctx, "f_00546724"); }
/* f_0054b67c is LoadThread's work. It reads three global flags and, if ANY is
 * set, parks forever in li r3,1000 / sys_timer_usleep at 0x54b6dc. If all
 * three are clear it takes the real path at 0x54b6e8 and calls f_005339a8.
 * Reading the three bytes and hooking f_005339a8 separates the two outcomes
 * with values instead of an inference. */
static bool work_54b67c(ppu::Context& ctx) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 8) {
        auto b = [](uint32_t ea) { return vm::range_mapped(ea, 1) ? unsigned(vm::ptr(ea)[0]) : 0xffu; };
        const unsigned f0 = b(0x1669dccu), f1 = b(0x17197f8u), f2 = b(0x17197f9u);
        LOG_INFO("threadgate",
                 "LOADTHREAD work f_0054b67c #%d th=0x%x flag[0x1669dcc]=%u flag[0x17197f8]=%u flag[0x17197f9]=%u -> %s t=%.6f",
                 k, ctx.thread_id, f0, f1, f2, (f0 || f1 || f2) ? "PARK (sleep loop)" : "REAL WORK at 0x54b6e8",
                 ppu::timebase() / 79.8e6);
    }
    return false;
}
static bool loadthread_real(ppu::Context& ctx) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 8)
        LOG_INFO("threadgate", "LOADTHREAD REAL PATH f_005339a8 #%d th=0x%x t=%.6f", k, ctx.thread_id,
                 ppu::timebase() / 79.8e6);
    return false;
}
static bool work_54a450(ppu::Context& ctx) { return work_probe(ctx, "f_0054a450"); }
static bool work_54b360(ppu::Context& ctx) { return work_probe(ctx, "f_0054b360"); }

/* The decisive site. f_00533064_body:
 *   0x5330ac clrldi r3,r31,32   ; r31 = the entry's r3 argument (the work struct)
 *   0x5330b4 stw    r3,0xd8(r1) ; spill its low 32 bits
 *   0x533140 lwz    r6,0xd8(r1) ; reload
 *   0x533144 cmpwi  r6,0
 *   0x533148 beq    0x53316c    ; skip the block when r6 == 0
 *   0x53314c lis r7,0x166 / addi r7,-0x33e0 -> 0x165cc20
 *   0x533154 lwz r8,0x844(r7)   ; [0x165d464] = list head
 *   0x533158 stw r8,0(r6)       ; ws->next = head
 *   0x533160 stw r6,0x844(r7)   ; head = ws
 *   0x533164 lwz r8,0x840(r7)   ; [0x165d460] = count
 *   0x533168 stw r8+1,0x840(r7) ; count++
 *
 * So the branch guards a LINKED-LIST REGISTRATION of the work structure, not the
 * work itself -- the work is the call_indirect at 0x5331ac, which is after the
 * rejoin at 0x53316c. Reaching 0x53314c proves the beq was not taken, and the
 * printed r6 is the exact value the test compared. */
static bool thread_gate_probe(ppu::Context& ctx) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 16) {
        const uint32_t r6 = static_cast<uint32_t>(ctx.gpr[6]);
        const uint32_t head_before = vm::range_mapped(0x165d464u, 4) ? vm::read32(0x165d464u) : 0;
        const uint32_t cnt = vm::range_mapped(0x165d460u, 4) ? vm::read32(0x165d460u) : 0;
        LOG_INFO("threadgate", "#%d th=0x%x r6=0x%08x (test value) r7=0x%08x head_before=0x%08x count=%u",
                 k, ctx.thread_id, r6, static_cast<uint32_t>(ctx.gpr[7]), head_before, cnt);
    }
    return false;
}

/* A direct experiment on the meaning of the beq at 0x533148.
 *
 * The tested value is not a field. It is the worker entry's own r3 argument,
 * copied through 0x5330ac (clrldi r3,r31,32) and 0x5330b4 (stw r3,0xd8(r1)) and
 * reloaded at 0x533140 (lwz r6,0xd8(r1)). It is therefore the ADDRESS of the
 * per-worker structure the thread was created with (0x165cc20 / 0x165cc4c),
 * and it is never zero in this title -- a thread created with a null argument
 * would be the only way to take the branch.
 *
 * PS3BASE_THREAD_ZERO_ARG=1 proves that reading by forcing the value: it zeroes
 * ws[0] instead, which is the field the call_indirect at 0x5331a4 reads
 * (lwz r5,0(r5); lwz r4,0(r5); mtctr r4). With it set, the work function is
 * skipped and the thread must fall through to _sys_ppu_thread_exit. That is the
 * difference between the branch guarding the work and it guarding a list link. */
static bool thread_exit_probe(ppu::Context& ctx) {
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 8)
        LOG_INFO("threadprobe", "EXIT  0x5331e0 #%d th=0x%x r3=0x%08x r31=0x%08x lr=0x%08x",
                 k, ctx.thread_id, static_cast<uint32_t>(ctx.gpr[3]),
                 static_cast<uint32_t>(ctx.gpr[31]), static_cast<uint32_t>(ctx.lr));
    return false;
}
static bool load_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_LOAD_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 200) return false;
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    if (!obj || !vm::range_mapped(obj + 2924, 16)) {
        LOG_INFO("loadprobe", "obj 0x%08x unmapped lr=0x%08x", obj, static_cast<uint32_t>(ctx.lr));
        return false;
    }
    auto rd = [&](uint32_t off) { return vm::range_mapped(obj + off, 4) ? vm::read32(obj + off) : 0xdeadbeefu; };
    LOG_INFO("loadprobe", "#%d lr=0x%08x obj=0x%08x state=%u cnt=%u lim=%u thumb_size=%u thumb_ptr=0x%08x [324]=%u",
             k, static_cast<uint32_t>(ctx.lr), obj, rd(2924), rd(2928), rd(2900),
             rd(168), rd(172), rd(324));
    return false;   /* observe only */
}

/* piste 43 -- the two candidates named by piste 32, observed.
 *
 * f_002e4120 (mc_028.cpp:31505) is the thunk that calls f_002e8ae8 with the
 * hard-coded object 0xbc0000-11016 = 0xd0d4f8. f_002eeb78 is the state
 * setter called just before UserAutoLoad in thread_autoload (mc_029.cpp:4190,
 * 4215), which stores its argument into obj+324 -- the very field the state-5
 * arm of f_002eee64 tests first. If +324 is 0, the state-5 arm exits at once
 * and f_002eeae4 is never reached. Environment: PS3BASE_LOAD_PROBE=1 */
static bool load_arm_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_LOAD_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k < 60)
        LOG_INFO("loadprobe", "ARM f_002e4120 #%d lr=0x%08x r3=0x%08x", k,
                 static_cast<uint32_t>(ctx.lr), static_cast<unsigned>(ctx.gpr[3]));
    return false;
}

static bool load_setter_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_LOAD_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    const uint32_t val = static_cast<uint32_t>(ctx.gpr[4]);
    if (k < 60)
        LOG_INFO("loadprobe", "SETTER f_002eeb78 #%d lr=0x%08x obj=0x%08x arg=%u (state <- %u)",
                 k, static_cast<uint32_t>(ctx.lr), obj, val, val);
    return false;
}

/* piste 43 -- the ONLY writer that can put obj+2924 into state 5.
 *
 * f_002ea564 reaches L_2ea500, which stores 5 into obj+2924 -- but only when
 * gpr30 (loaded from obj+16 by the preceding call) is ZERO. If gpr30 is
 * non-zero the same block stores 8 or 9 instead, and state 5 is unreachable,
 * so f_002eee64 can never take the arm that materialises OPD 0xbbe7f8 and
 * calls f_002eeae4. This probe reports which one happens and the gate value.
 * Environment: PS3BASE_LOAD_PROBE=1 */
static bool load_advance_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_LOAD_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 40) return false;
    const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
    auto rd = [&](uint32_t off) { return vm::range_mapped(obj + off, 4) ? vm::read32(obj + off) : 0xdeadbeefu; };
    LOG_INFO("loadprobe", "ADVANCE f_002ea564 #%d lr=0x%08x obj=0x%08x state=%u [16]=0x%08x lim=%u cnt=%u",
             k, static_cast<uint32_t>(ctx.lr), obj, rd(2924), rd(16), rd(2900), rd(2928));
    return false;
}

/* PS3BASE_USLEEP_PROBE=1: WHO asks for a one-second sleep, forever?
 *
 * The title's main thread parks in sys_timer_usleep(1000000) with lr
 * 0x002a9180, i.e. inside f_002a9168 -- an eight-instruction wrapper that
 * multiplies its argument by 1000 and issues syscall 141. The wrapper has 70
 * call sites in the lifted tree, so the address of the wrapper says nothing
 * about which loop is spinning. This probe reports the GUEST LR (the site's
 * return address, unique per call site) and the guest thread, and separates the
 * 1000 ms requests from the 1/10/16/100 ms noise.
 *
 * It also reads, on each such request, the guest word the loop is about to
 * test, so the loop's gate is measured rather than inferred: PS3BASE_USLEEP_EA
 * names that address.
 *
 * Environment: PS3BASE_USLEEP_PROBE=1 (optional PS3BASE_USLEEP_EA=0x...) */
static uint32_t usleep_ea() {
    static const uint32_t ea = [] {
        const char* e = std::getenv("PS3BASE_USLEEP_EA");
        return e ? static_cast<uint32_t>(std::strtoul(e, nullptr, 16)) : 0u;
    }();
    return ea;
}

static bool usleep_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_USLEEP_PROBE") != nullptr;
    if (!on) return false;
    const uint32_t ms = static_cast<uint32_t>(ctx.gpr[3]);
    /* Only the long sleeps: the 1 ms ticks are irrelevant noise. */
    if (ms < 500) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 240) return false;
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);
    const guest::Thread* t = guest::current();
    const uint32_t ea = usleep_ea();
    std::string held;
    if (ea) {
        char b[96];
        if (vm::range_mapped(ea, 4))
            std::snprintf(b, sizeof b, " [0x%08x]=0x%08x (%u)", ea, vm::read32(ea), vm::read32(ea));
        else
            std::snprintf(b, sizeof b, " [0x%08x]=UNMAPPED", ea);
        held = b;
    }
    LOG_INFO("usleepprobe", "#%d t=%.3f th=0x%x '%s' ms=%u lr=0x%08x r1=0x%08x%s",
             k, logging::uptime(), t ? t->id : 0u, t ? t->name.c_str() : "?",
             ms, lr, static_cast<uint32_t>(ctx.gpr[1]), held.c_str());
    return false;
}

/* PS3BASE_USLEEP_EA + this probe: the leaf of the 1 s idle loop.
 *
 * main_thread parks in f_00550b34 (rdr_053.cpp:24883), whose L_550c10 is an
 * UNCONDITIONAL "sleep 1000 ms; goto L_550c10" -- there is no memory word in
 * that back-edge at all. The only test that decides whether the thread enters
 * it is the return code of import 210 (sys_fs/cellFsFsync) at L_550ba0:
 *   == 0x8001002A (CELL_FS_EBADF) -> L_550bc4 -> log "simulating infinite retry"
 *                                    -> L_550c10 -> the loop
 *   == 0x8001002B                -> L_550c1c -> return normally
 * This probe reports the fd handed to cellFsFsync and the code it came back
 * with, so the leaf is a measured number rather than a reading of the branch.
 *
 * Environment: PS3BASE_FSYNC_PROBE=1 */
static bool fsync_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    const uint32_t fd = static_cast<uint32_t>(ctx.gpr[3]);
    const guest::Thread* t = guest::current();
    LOG_INFO("fsyncprobe", "cellFsFsync(fd=%u) called from lr=0x%08x th=0x%x '%s'",
             fd, static_cast<uint32_t>(ctx.lr), t ? t->id : 0u, t ? t->name.c_str() : "?");
    /* Observe only: the syscall still runs and its own return code is what the
     * caller sees. Reading it here would mean re-implementing the handler. */
    return false;
}

/* PS3BASE_FSYNC_PROBE=1: read the return code the loop's own branch consumes.
 *
 * f_00550b34 (rdr_053.cpp:24883) is reached with 5 arguments. It calls import
 * 210 at L_550ba0, then compares the code against 0x8001002A and 0x8001002B:
 * either value falls into L_550bc4, which logs and hangs in L_550c10 forever.
 * Only a code that equals NEITHER returns. Guarding 0x550bc4 (the error arm)
 * prints the exact code that trapped this thread -- the leaf, measured. */
static bool loop_arm_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 16) return false;
    const guest::Thread* t = guest::current();
    LOG_INFO("loopprobe", "#%d L_550bc4 (error arm) r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x th=0x%x '%s'",
             k, static_cast<uint32_t>(ctx.gpr[3]), static_cast<uint32_t>(ctx.gpr[4]),
             static_cast<uint32_t>(ctx.gpr[5]), static_cast<uint32_t>(ctx.gpr[6]),
             static_cast<uint32_t>(ctx.gpr[7]), t ? t->id : 0u, t ? t->name.c_str() : "?");
    return false;
}

static bool loop_import_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 16) return false;
    const guest::Thread* t = guest::current();
    LOG_INFO("loopprobe", "#%d L_550ba4 import-210 return r3=0x%08x th=0x%x '%s'",
             k, static_cast<uint32_t>(ctx.gpr[3]), t ? t->id : 0u, t ? t->name.c_str() : "?");
    return false;
}

/* PS3BASE_FSYNC_PROBE=1: the gate byte the loop function tests on entry, and
 * the code cellFsFsync actually handed back. Hooked at the caller's own return
 * address so the value read is the one the branch consumes. */
static bool fsync_result_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 32) return false;
    const uint32_t rc = static_cast<uint32_t>(ctx.gpr[3]);
    const uint32_t gate_ea = usleep_ea() ? usleep_ea() : 0x1669dccu;
    const unsigned gate = vm::range_mapped(gate_ea, 1) ? vm::ptr(gate_ea)[0] : 0xff;
    const guest::Thread* t = guest::current();
    LOG_INFO("fsyncprobe", "cellFsFsync -> 0x%08x %s (gate[0x%08x]=%u, ld8 at f_00550b34 entry) th=0x%x '%s'",
             rc, rc == 0x8001002au ? "CELL_FS_EBADF" : rc == 0x8001002bu ? "CELL_FS_EPERM?" : "",
             gate_ea, gate, t ? t->id : 0u, t ? t->name.c_str() : "?");
    return false;
}

/* PS3BASE_FSYNC_PROBE=1: the two files of the failure, read as guest strings.
 *
 * f_34010b28 is the LLE body of cellFsAllocateFileAreaWithoutZeroFill; r3 is
 * the guest path. This names the file the title wanted to allocate before it
 * started writing, so "the cache file" is a string from the title, not an
 * inference from the log line above it.
 *
 * Environment: PS3BASE_FSYNC_PROBE=1 */
static bool alloc_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    if (n.fetch_add(1) >= 8) return false;
    const uint32_t p = static_cast<uint32_t>(ctx.gpr[3]);
    std::string s;
    if (vm::range_mapped(p, 1))
        for (uint32_t i = 0; i < 256 && vm::range_mapped(p + i, 1) && vm::ptr(p)[i]; i++) s += char(vm::ptr(p)[i]);
    LOG_INFO("allocprobe", "cellFsAllocateFileAreaWithoutZeroFill(path='%s', size=0x%llx) lr=0x%08x",
             s.c_str(), static_cast<unsigned long long>(ctx.gpr[4]), static_cast<uint32_t>(ctx.lr));
    return false;
}

/* PS3BASE_FSYNC_PROBE=1: the loop function's own entry, with its arguments and
 * the byte it tests before doing anything.
 *
 * f_00550b34 (rdr_053.cpp:24883) is the function main_thread parks in. Its very
 * first act is `ld8 r7, -25140(r8)` with r8 = 0x1670000, i.e. the byte at
 * 0x01669dcc, and `cmpwi r7, 0`: non-zero goes to L_550b80 (the real work),
 * zero hangs in L_550b74 at 1000 ms per turn. Reporting r3..r7 AND that byte at
 * entry is what says which arm the thread took and with what arguments.
 *
 * Environment: PS3BASE_FSYNC_PROBE=1 */
static bool loop_entry_probe(ppu::Context& ctx) {
    static const bool on = std::getenv("PS3BASE_FSYNC_PROBE") != nullptr;
    if (!on) return false;
    static std::atomic<int> n{0};
    const int k = n.fetch_add(1, std::memory_order_relaxed);
    if (k >= 12) return false;
    const uint32_t gate_ea = 0x01669dcc;
    const unsigned gate = vm::range_mapped(gate_ea, 1) ? vm::ptr(gate_ea)[0] : 0xff;
    const guest::Thread* t = guest::current();
    LOG_INFO("loopentry", "#%d f_00550b34 th=0x%x '%s' r3=0x%08x r4=0x%08x r5=0x%08x r6=0x%08x r7=0x%08x gate[0x%08x]=%u",
             k, t ? t->id : 0u, t ? t->name.c_str() : "?",
             static_cast<uint32_t>(ctx.gpr[3]), static_cast<uint32_t>(ctx.gpr[4]),
             static_cast<uint32_t>(ctx.gpr[5]), static_cast<uint32_t>(ctx.gpr[6]),
             static_cast<uint32_t>(ctx.gpr[7]), gate_ea, gate);
    return false;
}

void init() {
    /* Everything below is title-specific: patches by guest address, job
     * intercepts, the jobsys edge, the critical-path hooks. A profile with
     * nothing to declare gets none of it, and its guest runs on the generic
     * runtime alone. This is the whole point of the seam -- the runtime must
     * not know a game name, and a foreign disc must not inherit one. */
    const game::Profile& profile = game::current();
    for (const game::AddressHook& h : profile.address_hooks()) {
        LOG_INFO("hooks", "profile '%s': hook %s at 0x%08x",
                 profile.name(), h.what, h.guest_addr);
        /* The map holds Hook* values; an address hook is a named watchpoint
         * with no code of its own, so it registers with a null Hook and the
         * name is only for the log. */
        g_hooks[h.guest_addr] = nullptr;
    }
    profile.install_ppu_shims();
    /* Force and call-trace modes need every lifted call to funnel through impl
     * so a return value can be overridden or recorded. They must still honour
     * GOWA_HOOK specs, so the flags are filled and the parse continues. */
    const bool route_all = std::getenv("PS3BASE_FORCE") || std::getenv("PS3BASE_CALL_TRACE");
    if (route_all) {
        for (uint32_t i = 0; i < ppu::hook_flag_image_count; i++) {
            const auto& im = image::images[i];
            std::memset(ppu::hook_flags[i], 1, (im.text_hi - im.text_lo + 3) / 4);
        }
        ppu::hook_impl = impl;
    }
    /* hook_impl must be non-null for hook_call to dispatch anything. Assign it
     * here AND again after the diagnostics below register their hooks: a guard
     * added after this line would otherwise never be reached, which is exactly
     * how PS3BASE_LIST_GUARD ended up inert. */
    if (!g_hooks.empty() && !ppu::hook_impl) ppu::hook_impl = impl;
    /* The list guard is generic: it observes the node pointer a walk is about to
     * pass, and needs no title-specific address beyond the text range it tests
     * against. Installed before the early return below so it also works on a run
     * that does not enable the title's other hooks.
     *
     * MEASURED BUG, fixed here: add_guard sets a hook_flags bit, but the lifted
     * code only consults hook_flags when ppu::hook_impl is non-null. hook_impl
     * was assigned ABOVE this block, while g_hooks was still empty, so with
     * PS3BASE_GAME_HOOKS unset -- which is the documented way to run this guard
     * -- the guard was installed, logged "guard installed", and never called.
     * That is why earlier runs reported "0 false alarms": it was not watching.
     * Assign it again after the guard is registered. */
    if (std::getenv("PS3BASE_THREAD_PROBE")) {
        add_guard(0x533064, thread_entry_probe);
        add_guard(0x5331e0, thread_exit_probe);
        /* The call_indirect at 0x5331ac uses ctr = [ws[0]] (the work object's
         * first word). Hook the three targets the measured runs use, so
         * "the work never ran" and "the work ran and returned" are different
         * lines rather than an inference. */
        add_guard(0x27e564, work_27e564);
        add_guard(0x545d54, work_545d54);
        add_guard(0x546724, work_546724);
        add_guard(0x5339a8, loadthread_real);
        add_guard(0x54b67c, work_54b67c);
        add_guard(0x54a450, work_54a450);
        add_guard(0x54b360, work_54b360);
    }
    if (std::getenv("PS3BASE_LOAD_PROBE")) {
        add_guard(0x2eee64, load_probe);
        add_guard(0x2e4120, load_arm_probe);
        add_guard(0x2eeb78, load_setter_probe);
        if (!ppu::hook_impl) ppu::hook_impl = impl;
        for (uint32_t a : {0x2eee64u, 0x2e4120u, 0x2eeb78u}) set_hook_flag(a);
        add_guard(0x2ea564, load_advance_probe);
        set_hook_flag(0x2ea564);
        LOG_INFO("loadprobe", "probes installed: f_002eee64 (arbiter), f_002e4120 (arm), f_002eeb78 (setter), f_002ea564 (state-5 writer)");
    }
    if (std::getenv("PS3BASE_USLEEP_PROBE")) {
        /* 0x2a9168 is the title's own usleep(ms) wrapper: it multiplies r3 by
         * 1000 and issues syscall 141. Guarding the WRAPPER rather than the
         * syscall keeps this a lifted-code hook, so ctx.lr is the guest return
         * address of the site that asked for the sleep. */
        add_guard(0x2a9168, usleep_probe);
        if (!ppu::hook_impl) ppu::hook_impl = impl;
        set_hook_flag(0x2a9168);
        LOG_INFO("usleepprobe", "guard installed on f_002a9168 (usleep wrapper), hook_impl=%s flag=%u",
                 ppu::hook_impl ? "set" : "NULL",
                 unsigned(ppu::hook_flag_for(0x2a9168) ? *ppu::hook_flag_for(0x2a9168) : 0));
    }
    if (std::getenv("PS3BASE_FSYNC_PROBE")) {
        /* f_3400144c is the lifted cellFsFsync: it sets r11=820 and issues the
         * syscall. f_00550b34 is the loop function; guarding it reports the fd
         * and rc at the site that branches on them. */
        add_guard(0x550b34, loop_entry_probe);
        set_hook_flag(0x550b34);
        add_guard(0x34010b28, alloc_probe);
        set_hook_flag(0x34010b28);
        add_guard(0x550ba4, loop_import_probe);
        add_guard(0x550bc4, loop_arm_probe);
        if (!ppu::hook_impl) ppu::hook_impl = impl;
        set_hook_flag(0x550ba4);
        set_hook_flag(0x550bc4);
        LOG_INFO("fsyncprobe", "guards installed: f_3400144c (cellFsFsync) and 0x550ba4 (its return site), hook_impl=%s",
                 ppu::hook_impl ? "set" : "NULL");
    }
    if (std::getenv("PS3BASE_LIST_GUARD")) {
        add_guard(0x64c044, list_guard);
        if (!ppu::hook_impl) ppu::hook_impl = impl;
        set_hook_flag(0x64c044);
        /* Prove the wiring, do not assume it: a guard that is never called and a
         * guard that is called and finds nothing are opposite results. */
        LOG_INFO("listguard", "guard installed on f_0064c044: hook_impl=%s flag=%u, node +28/+32 are checked against the text range 0x00010000..0x00ba3688",
                 ppu::hook_impl ? "set" : "NULL",
                 unsigned(ppu::hook_flag_for(0x64c044) ? *ppu::hook_flag_for(0x64c044) : 0));
    }
    /* Any hook or guard registered above needs the dispatcher, whether or not
     * the title-specific hook set is enabled. */
    if (ppu::hook_impl == nullptr) {
        bool any = !g_hooks.empty();
        for (uint32_t i = 0; i < ppu::hook_flag_image_count && !any; i++)
            for (uint32_t a = 0; a < (image::images[i].text_hi - image::images[i].text_lo + 3) / 4 && !any; a++)
                any = ppu::hook_flags[i][a] != 0;
        if (any) ppu::hook_impl = impl;
    }
    if (std::getenv("PS3BASE_GAME_HOOKS") == nullptr) return;
    if (std::getenv("GOWA_BATCH_DIAG")) std::thread(batch_diag_thread).detach();
    if (const char* h = std::getenv("GOWA_TEST_HOG")) start_hogs(h);
    if (const char* n = std::getenv("GOWA_SYMBOLS")) load_names(n);
    else load_names("../docs/re/symbols.txt");
    std::string combined = std::getenv("GOWA_HOOK") ? std::getenv("GOWA_HOOK") : "";
    const char* bb = std::getenv("GOWA_BLACKBOX");
    if (!bb) bb = "52bd7c:panic,3a2adc:ring,3a2690:ring,3a12a0:ring,3a0000:ring:p9b26ac*=0x40,39f3c4:ring,39f06c:ring,39b6f4:ring:d3=0x40";
    if (*bb && std::string(bb) != "0") combined += (combined.empty() ? "" : ",") + std::string(bb);
    const char* spec = combined.c_str();
    if (!*spec) {
        install_patches();
        install_ppujobs();
        install_jobsys_edge();
        install_critpath();
        if (!g_hooks.empty()) ppu::hook_impl = impl;
        return;
    }
    if (const char* m = std::getenv("GOWA_HOOK_MAX")) g_max = static_cast<uint32_t>(std::atoi(m));
    if (const char* a = std::getenv("GOWA_HOOK_AT")) g_at = std::atof(a);
    g_count_only = std::getenv("GOWA_HOOK_COUNT") != nullptr;
    std::string all(spec);
    size_t pos = 0;
    while (pos <= all.size()) {
        size_t comma = all.find(',', pos);
        std::string item = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? all.size() + 1 : comma + 1;
        if (item.empty()) continue;
        std::vector<std::string> parts;
        size_t p = 0;
        while (true) {
            size_t c = item.find(':', p);
            parts.push_back(item.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (c == std::string::npos) break;
            p = c + 1;
        }
        uint32_t addr = 0;
        for (auto& [a, nm] : g_names) {
            if (nm == parts[0]) addr = a;
        }
        if (!addr) addr = static_cast<uint32_t>(std::strtoul(parts[0].c_str() + (parts[0].rfind("f_", 0) == 0 ? 2 : 0), nullptr, 16));
        if (!addr || !ppu::hook_flag_for(addr)) {
            LOG_WARN("hook", "bad hook '%s'", item.c_str());
            continue;
        }
        if (g_hooks.count(addr)) continue;
        Hook* h = new Hook;
        h->addr = addr;
        for (size_t i = 1; i < parts.size(); i++) {
            if (parts[i] == "ring") h->ring = true;
            else if (parts[i] == "panic") h->panic = true;
            else if (parts[i].size() > 2 && parts[i][0] == 'd') {
                size_t eq = parts[i].find('=');
                int reg = std::atoi(parts[i].substr(1, eq - 1).c_str());
                uint32_t len = eq == std::string::npos ? 0x40 : static_cast<uint32_t>(std::strtoul(parts[i].c_str() + eq + 1, nullptr, 0));
                if (reg >= 0 && reg < 32) h->dumps.push_back({reg, len});
            } else if (parts[i].size() > 2 && parts[i][0] == 'p') {
                size_t eq = parts[i].find('=');
                std::string a = parts[i].substr(1, eq == std::string::npos ? std::string::npos : eq - 1);
                int derefs = 1;
                while (!a.empty() && a.back() == '*') {
                    derefs++;
                    a.pop_back();
                }
                uint32_t ptr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
                uint32_t len = eq == std::string::npos ? 0x40 : static_cast<uint32_t>(std::strtoul(parts[i].c_str() + eq + 1, nullptr, 0));
                h->dumps.push_back({-1, len, ptr, derefs});
            }
        }
        g_hooks[addr] = h;
        set_hook_flag(addr);
        if (!h->ring && !h->panic) LOG_INFO("hook", "hooked %s at 0x%08x", name_of(addr).c_str(), addr);
    }
    install_patches();
    install_ppujobs();
    install_jobsys_edge();
    install_critpath();
    if (g_hooks.empty()) return;
    ppu::hook_impl = impl;
    if (g_count_only) std::thread(summary_thread).detach();
}

}
