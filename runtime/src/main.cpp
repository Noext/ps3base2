#include "runtime/placement.h"
#include "runtime/cbwatch.h"
#include "runtime/codewatch.h"
#include "runtime/host.h"
#include "runtime/image.h"
#include "runtime/loader.h"
#include "runtime/log.h"
#include "runtime/sysutil_events.h"
#include "hle/common.h"
#include "runtime/trace.h"
#include "runtime/critpath.h"
#include "runtime/hooks.h"
#include "runtime/memory.h"
#include "gpu/presenter.h"
#include "runtime/thread.h"
#include "runtime/game_profile.h"

#include "runtime/vfs.h"
#include "runtime/settings.h"
#include "runtime/session.h"
#ifdef GOWA_HAVE_LAUNCHER
#include "runtime/launcher.h"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace spu {
void status_dump();
void stack_dump();
void lowfps_tick(uint64_t flips);
}

namespace lv2 {
void rsx_status();
uint64_t rsx_flip_count();
}

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace {

uint32_t push_strings(uint32_t& sp, const char* const* list, int count) {
    uint32_t ptrs = 0;
    std::vector<uint32_t> addrs;
    for (int i = 0; i < count; i++) {
        uint32_t len = static_cast<uint32_t>(std::strlen(list[i])) + 1;
        sp -= (len + 15) & ~15u;
        std::memcpy(vm::ptr(sp), list[i], len);
        addrs.push_back(sp);
    }
    sp -= static_cast<uint32_t>((addrs.size() + 1) * 8 + 15) & ~15u;
    ptrs = sp;
    for (size_t i = 0; i < addrs.size(); i++) vm::write64(ptrs + static_cast<uint32_t>(i) * 8, addrs[i]);
    vm::write64(ptrs + static_cast<uint32_t>(addrs.size()) * 8, 0);
    return ptrs;
}

}

namespace crash {
void install();
void arm_watch();
void rearm_watch();
}


namespace {

struct CensusRow {
    double user, kernel;
    std::string name;
    unsigned long tid;
};

void census_log(std::vector<CensusRow>& rows, double total, int period) {
    std::sort(rows.begin(), rows.end(), [](const CensusRow& a, const CensusRow& b) { return a.user + a.kernel > b.user + b.kernel; });
    std::string line;
    char b[160];
    for (const CensusRow& r : rows) {
        std::snprintf(b, sizeof b, " [%s#%lu %.0f%%u %.0f%%k]", r.name.empty() ? "?" : r.name.c_str(), r.tid, r.user, r.kernel);
        line += b;
    }
    LOG_INFO("census", "process cpu %.0f%% of one core over %ds:%s", total, period, line.c_str());
}

#ifdef _WIN32
void thread_census(int period) {
    static std::map<DWORD, std::pair<uint64_t, uint64_t>> prev;
    static auto prev_wall = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(now - prev_wall).count();
    prev_wall = now;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{sizeof te};
    DWORD pid = GetCurrentProcessId();
    using Row = CensusRow;
    std::vector<Row> rows;
    std::map<DWORD, std::pair<uint64_t, uint64_t>> cur;
    double total = 0;
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid) continue;
        HANDLE h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
        if (!h) continue;
        FILETIME c, e, k, u;
        if (GetThreadTimes(h, &c, &e, &k, &u)) {
            uint64_t ku = (uint64_t(k.dwHighDateTime) << 32) | k.dwLowDateTime, uu = (uint64_t(u.dwHighDateTime) << 32) | u.dwLowDateTime;
            cur[te.th32ThreadID] = {uu, ku};
            auto it = prev.find(te.th32ThreadID);
            if (it != prev.end() && wall > 0) {
                Row r;
                r.user = (uu - it->second.first) / 1e7 / wall * 100.0;
                r.kernel = (ku - it->second.second) / 1e7 / wall * 100.0;
                r.tid = te.th32ThreadID;
                PWSTR desc = nullptr;
                if (SUCCEEDED(GetThreadDescription(h, &desc)) && desc) {
                    char b[128] = {};
                    WideCharToMultiByte(CP_UTF8, 0, desc, -1, b, sizeof b - 1, nullptr, nullptr);
                    r.name = b;
                    LocalFree(desc);
                }
                total += r.user + r.kernel;
                if (r.user + r.kernel >= 1.0) rows.push_back(r);
            }
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    prev.swap(cur);
    census_log(rows, total, period);
}
#else
void thread_census(int period) {
    static std::map<unsigned long, std::pair<uint64_t, uint64_t>> prev;
    static auto prev_wall = std::chrono::steady_clock::now();
    static const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
    auto now = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(now - prev_wall).count();
    prev_wall = now;
    std::vector<CensusRow> rows;
    std::map<unsigned long, std::pair<uint64_t, uint64_t>> cur;
    double total = 0;
    DIR* d = opendir("/proc/self/task");
    if (!d) return;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        unsigned long tid = std::strtoul(e->d_name, nullptr, 10);
        std::string base = std::string("/proc/self/task/") + e->d_name;
        std::ifstream sf(base + "/stat");
        std::string st;
        std::getline(sf, st);
        size_t rp = st.rfind(')');
        if (rp == std::string::npos) continue;
        unsigned long long uu = 0, ku = 0;
        if (std::sscanf(st.c_str() + rp + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &uu, &ku) != 2) continue;
        cur[tid] = {uu, ku};
        auto it = prev.find(tid);
        if (it == prev.end() || wall <= 0) continue;
        CensusRow r;
        r.user = (uu - it->second.first) / hz / wall * 100.0;
        r.kernel = (ku - it->second.second) / hz / wall * 100.0;
        r.tid = tid;
        std::ifstream cf(base + "/comm");
        std::getline(cf, r.name);
        total += r.user + r.kernel;
        if (r.user + r.kernel >= 1.0) rows.push_back(r);
    }
    closedir(d);
    prev.swap(cur);
    census_log(rows, total, period);
}
#endif

}

int main(int argc, char** argv) {
    settings::load();
#ifdef GOWA_HAVE_LAUNCHER
    if (launcher::wanted(argc, argv) && launcher::run() == launcher::Result::Quit) return 0;
#endif
    settings::apply_env();
    session::begin();
    crash::install();
    trace::init();
    cw::current_context = [] {
        cw::Context c{0, 0, nullptr};
        guest::Thread* t = guest::current();
        if (t) {
            c.lr = static_cast<uint32_t>(t->ctx.lr);
            c.cia = static_cast<uint32_t>(t->ctx.cia);
            c.thread = t->name.c_str();
        }
        return c;
    };
    {
        std::vector<uint32_t> entries;
        entries.reserve(image::function_count);
        for (uint32_t i = 0; i < image::function_count; i++) entries.push_back(image::functions[i].addr);
        cw::set_known_entries(entries.data(), static_cast<uint32_t>(entries.size()));
    }
    cw::init();
    critpath::init();
    hooks::init();
    host::init_timers();
    gpu::init();
    if (const char* root = std::getenv("GOWA_ROOT")) loader::g_root = root;
    if (const char* lvl = std::getenv("GOWA_LOG")) {
        if (!std::strcmp(lvl, "trace")) logging::set_level(logging::Level::Trace);
        else if (!std::strcmp(lvl, "debug")) logging::set_level(logging::Level::Debug);
    }
    LOG_INFO("main", "GOWA-pc: %u images, %u functions, %u imports, %u exports, root %s", image::image_count, image::function_count, image::import_count, image::export_count, loader::g_root.c_str());
    if (!vm::init()) return 1;
    /* The title image is whatever the profile names; a build with no profile
     * uses the first image in the manifest, which for a generated tree is the
     * game itself. The runtime never spells a game name. */
    /* A profile is selected by name before anything else looks at one. The
     * name comes from PS3BASE_PROFILE, or from the first image in the manifest
     * when the build ships exactly one. A build with no profiles compiled in,
     * or a name nothing registered, falls back to the empty profile and the
     * generic runtime, which is a supported configuration rather than an
     * error: that is how a foreign disc is measured. */
    {
        const char* want_profile = std::getenv("PS3BASE_PROFILE");
        const game::Profile* chosen = want_profile ? game::find(want_profile) : nullptr;
        if (!chosen && !want_profile && game::profiles_available() == 1)
            chosen = game::first_profile();
        game::set_profile(chosen);
        const char* active = game::current().name();
        LOG_INFO("main", "game profile: '%s'%s", active[0] ? active : "(none)",
                 chosen ? "" : " -- generic runtime only");
    }
    const game::Profile& profile = game::current();
    const char* want = profile.name()[0] ? profile.name() : nullptr;
    int game = want ? loader::find_image(want) : 0;
    int lv2 = loader::find_image("liblv2");
    if (game < 0 || lv2 < 0 || !loader::load_image(game) || !loader::load_image(lv2)) return 1;
    const loader::ProcessInfo& pi = loader::g_process;
    LOG_INFO("main", "sdk 0x%x prio %d stack 0x%x malloc page 0x%x tls 0x%x/0x%x/0x%x", pi.sdk_version, pi.primary_prio, pi.primary_stacksize, pi.malloc_pagesize, pi.tls_vaddr, pi.tls_filesz, pi.tls_memsz);
    uint32_t stack_size = pi.primary_stacksize;
    switch (stack_size) {
    case 0x10: stack_size = 32 * 1024; break;
    case 0x20: stack_size = 64 * 1024; break;
    case 0x30: stack_size = 96 * 1024; break;
    case 0x40: stack_size = 128 * 1024; break;
    case 0x50: stack_size = 256 * 1024; break;
    case 0x60: stack_size = 512 * 1024; break;
    case 0x70: stack_size = 1024 * 1024; break;
    default: stack_size = std::max<uint32_t>(0x10000, (stack_size + 0xFFF) & ~0xFFFu); break;
    }
    auto main_thread = guest::create("main_thread", image::images[lv2].entry, 0, 0, pi.primary_prio, stack_size, 0, true);
    if (!main_thread) return 1;
    uint32_t sp = main_thread->stack_addr + main_thread->stack_size;
    /* argv[0] for the CRT. A profile may override it; otherwise it is derived
     * from the disc metadata, which is what a generic boot wants. */
    std::string title_id = vfs::disc_sfo().str("TITLE_ID");
    std::string self_path = profile.self_path(title_id);
    if (self_path.empty()) {
        const std::string dir = vfs::patch_sfo().strings.empty()
            ? std::string("/dev_bdvd/PS3_GAME/USRDIR")
            : "/dev_hdd0/game/" + (title_id.empty() ? std::string("UNKNOWN") : title_id) + "/USRDIR";
        self_path = dir + "/" + (want ? want : "EBOOT") + ".SELF";
    }
    std::vector<std::string> arg_list = {self_path};
    if (const char* extra = std::getenv("GOWA_ARGS")) {
        std::string e = extra;
        size_t pos = 0;
        while (pos < e.size()) {
            size_t end = e.find(' ', pos);
            if (end == std::string::npos) end = e.size();
            if (end > pos) arg_list.push_back(e.substr(pos, end - pos));
            pos = end + 1;
        }
    }
    std::vector<const char*> args;
    for (const std::string& a : arg_list) args.push_back(a.c_str());
    LOG_INFO("main", "argc %zu%s%s", args.size(), args.size() > 1 ? " extra: " : "", std::getenv("GOWA_ARGS") ? std::getenv("GOWA_ARGS") : "");
    uint32_t argv_addr = push_strings(sp, args.data(), static_cast<int>(args.size()));
    uint32_t envp_addr = push_strings(sp, nullptr, 0);
    ppu::Context& c = main_thread->ctx;
    c.gpr[1] = (sp - 0x70) & ~15u;
    c.gpr[3] = args.size();
    c.gpr[4] = argv_addr;
    c.gpr[5] = envp_addr;
    c.gpr[6] = 0;
    c.gpr[7] = main_thread->id;
    c.gpr[8] = pi.tls_vaddr;
    c.gpr[9] = pi.tls_filesz;
    c.gpr[10] = pi.tls_memsz;
    c.gpr[11] = pi.entry_opd;
    c.gpr[12] = pi.malloc_pagesize;
    int watch_delay = std::getenv("GOWA_TRAP_AT") ? std::atoi(std::getenv("GOWA_TRAP_AT")) : std::getenv("GOWA_WATCH_DELAY") ? std::atoi(std::getenv("GOWA_WATCH_DELAY")) : 0;
    if (!watch_delay) crash::arm_watch();
    guest::start(main_thread);
    bool status = std::getenv("GOWA_STATUS") != nullptr;
    int memdump_at = std::getenv("GOWA_MEMDUMP_AT") ? std::atoi(std::getenv("GOWA_MEMDUMP_AT")) : -1;
    if (const char* e = std::getenv("GOWA_HITCH_DUMP_MS")) {
        int limit = std::atoi(e);
        std::thread([limit] {
            host::set_thread_name("hitch watchdog");
            uint64_t last = lv2::rsx_flip_count();
            int snap_sec = 0;
            auto since = std::chrono::steady_clock::now();
            bool dumped = false;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                uint64_t f = lv2::rsx_flip_count();
                auto now = std::chrono::steady_clock::now();
                if (f != last) {
                    last = f;
                    since = now;
                    dumped = false;
                    continue;
                }
                if (dumped || f < 60 || now - since < std::chrono::milliseconds(limit)) continue;
                dumped = true;
                LOG_INFO("hitch", "no flip for %d ms after flip %llu", limit, static_cast<unsigned long long>(f));
                guest::status_dump();
                spu::stack_dump();
                lv2::rsx_status();
            }
        }).detach();
    }
    /* PS3BASE_SYSUTIL_EVENTS: deliver the per-frame system events a real PS3
     * pushes to every registered sysutil callback (DRAWING_BEGIN/END and the
     * periodic system-command notification). The title registers three slots and
     * never calls CheckCallback itself in our environment, so without a producer
     * its callback queue stays empty forever. Field order matches the guest
     * handler: cb(status, param, userdata). */
    if (std::getenv("PS3BASE_SYSUTIL_EVENTS")) {
        std::thread([] {
            host::set_thread_name("sysutil events");
            uint64_t last = 0;
            for (;;) {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                const uint64_t now = lv2::rsx_flip_count();
                if (now == last) continue;
                last = now;
                hle::sysutil_post([](ppu::Context& c) {
                    for (int slot = 0; slot < 4; slot++) {
                        const uint32_t fn = hle::callback_function(slot);
                        if (fn) hle::call_guest(c, fn, 0x0121ull /* DRAWING_BEGIN */, 0, hle::callback_arg(slot));
                    }
                });
                hle::sysutil_post([](ppu::Context& c) {
                    for (int slot = 0; slot < 4; slot++) {
                        const uint32_t fn = hle::callback_function(slot);
                        if (fn) hle::call_guest(c, fn, 0x0122ull /* DRAWING_END */, 0, hle::callback_arg(slot));
                    }
                });
            }
        }).detach();
    }
    if (const char* sp = std::getenv("PS3BASE_SPURS_WATCH")) {
        const uint32_t base = static_cast<uint32_t>(std::strtoul(sp, nullptr, 0));
        std::thread([base] {
            host::set_thread_name("spurs watch");
            for (int i = 0; i < 400; i++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!vm::range_mapped(base, 0x100)) continue;
                const uint32_t s1 = vm::read32(base + 0x70);
                const uint32_t s2 = vm::read32(base + 0x78);
                const uint32_t idle = vm::ptr(base)[0x73];
                LOG_INFO("spurswatch", "t~%dms wklSignal1=0x%08x wklSignal2=0x%08x spuIdling=0x%02x",
                         i * 500, s1, s2, idle);
            }
        }).detach();
    }
    host::set_thread_name("Host main");
    placement::apply("host");
    static const int census = std::getenv("GOWA_THREAD_CPU") ? std::atoi(std::getenv("GOWA_THREAD_CPU")) : 0;
    std::vector<uint32_t> peeks;
    if (const char* e = std::getenv("GOWA_PEEK")) {
        for (const char* q = e; *q;) {
            char* end = nullptr;
            peeks.push_back(static_cast<uint32_t>(std::strtoul(q, &end, 16)));
            q = *end ? end + 1 : end;
        }
    }
    int cb_dump_at = std::getenv("PS3BASE_CENSUS_AT") ? std::atoi(std::getenv("PS3BASE_CENSUS_AT")) : 30;
    if (std::getenv("PS3BASE_CENSUS") && cb_dump_at > 0) {
        /* Periodic, not at exit: a run stopped by "timeout" gets SIGTERM and
         * never reaches a shutdown path, so the inventory has to be written
         * while the run is still alive. */
        std::thread([cb_dump_at] {
            host::set_thread_name("census dump");
            for (;;) {
                std::this_thread::sleep_for(std::chrono::seconds(cb_dump_at));
                cbwatch::dump("periodic");
            }
        }).detach();
    }
    for (int tick = 0;; tick++) {
        if (census > 0 && tick % census == 0) thread_census(census);
        hooks::tick_patches();
        {
            /* Snapshot the listed guest ranges at 2, 5 and 10 s, so the same
             * buffer can be compared across time. Identical striped content at
             * every point rules out a race. */
            static const auto t0 = std::chrono::steady_clock::now();
            static int done = 0;
            const int sec = int(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            for (int mark : {2, 10, 17, 19, 21}) {
                if (sec >= mark && !(done & (1 << mark))) {
                    done |= 1 << mark;
                    vm::tex_snapshot_tick(mark);
                }
            }
        }
        spu::lowfps_tick(lv2::rsx_flip_count());
        if (!peeks.empty()) {
            std::string line;
            for (uint32_t addr : peeks) {
                char buf[32];
                std::snprintf(buf, sizeof buf, " %x=%08x", addr, vm::read32(addr));
                line += buf;
            }
            LOG_INFO("peek", "%s", line.c_str());
        }
        if (watch_delay && tick == watch_delay) crash::arm_watch();
        else if (tick > watch_delay) crash::rearm_watch();
        if (tick == memdump_at) trace::dump_memory("memdump.bin");
        if (std::error_code ec; std::filesystem::exists("blackbox.req", ec)) {
            std::filesystem::remove("blackbox.req", ec);
            hooks::capture("external request");
            if (FILE* f = std::fopen("blackbox.done", "w")) std::fclose(f);
        }
        if (status && tick % 3 == 2) {
            spu::status_dump();
            guest::status_dump();
            lv2::rsx_status();
        }
        std::unique_lock lk(main_thread->lock);
        if (main_thread->cv.wait_for(lk, std::chrono::seconds(1), [&] { return main_thread->exited; })) break;
    }
    LOG_INFO("main", "main thread finished");
    trace::shutdown();
    return 0;
}
