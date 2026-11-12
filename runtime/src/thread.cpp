#include "runtime/placement.h"
#include "runtime/host.h"
#include "runtime/stacks.h"
#include "runtime/thread.h"
#include "runtime/kernel.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "ppu/ops.h"

#include <cstring>
#include <map>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace crash {
std::string sample_stack(void* native, int depth);
void profile_thread(void* native, const char* name);
}

namespace lv2 {
void lwmutex_debug(uint32_t id);
}

namespace guest {

namespace {

std::mutex g_lock;
std::map<uint32_t, std::shared_ptr<Thread>> g_threads;
uint32_t g_next_id = 0x01000000;
thread_local Thread* t_current = nullptr;

void body(std::shared_ptr<Thread> t) {
    t_current = t.get();
    t->native = host::current_native();
    host::set_thread_name("PPU " + t->name);
    placement::apply("ppu:" + t->name);
    if (const char* e = std::getenv("GOWA_PROFILE_PPU")) {
        if (t->name == e) {
            crash::profile_thread(host::current_native(), e);
        }
    }
    LOG_INFO("ppu", "thread 0x%x '%s' start entry opd 0x%08x", t->id, t->name.c_str(), t->entry_opd);
    uint64_t status = 0;
    try {
        call_opd(t->ctx, vm::g_base, t->entry_opd);
        status = t->ctx.gpr[3];
    } catch (const lv2::ThreadExit& e) {
        status = e.status;
        /* The lr names the lifted function that asked to exit, which is what
         * turns "thread X exited 0xdeadc0de" into "this function chose to stop".
         * The status alone cannot be attributed: the title builds 0xdeadc0de
         * with lis/oris at 0x005331c0, so it appears nowhere in a file. */
        LOG_INFO("ppu", "thread 0x%x '%s' sys_ppu_thread_exit(0x%llx) from lr=0x%08x %s",
                 t->id, t->name.c_str(), static_cast<unsigned long long>(e.status),
                 static_cast<uint32_t>(t->ctx.lr),
                 lv2::has_code_at(static_cast<uint32_t>(t->ctx.lr)) ? "(lifted)" : "(not lifted)");
    } catch (const lv2::ProcessExit& e) {
        /* Describe, do not classify. The lr is the return address of the call
         * that asked to exit, so it is almost never a function ENTRY -- which
         * is why has_code_at() answered false and the report said
         * "lr=0x3000c7e8 (not lifted)" for a PC inside f_3000c790, lifted and
         * running. describe_address() reports the image, the landmark label the
         * seed carried, and the containing function with the offset into it. */
        LOG_INFO("ppu", "process exit %d from thread 0x%x lr=0x%08x %s", e.status, t->id,
                 static_cast<uint32_t>(t->ctx.lr),
                 lv2::describe_address(static_cast<uint32_t>(t->ctx.lr)).c_str());
        std::fflush(nullptr);
        std::quick_exit(e.status);
    }
    LOG_INFO("ppu", "thread 0x%x '%s' exit 0x%llx lr=0x%08x", t->id, t->name.c_str(),
             static_cast<unsigned long long>(status), static_cast<uint32_t>(t->ctx.lr));
    {
        std::lock_guard lk(t->lock);
        t->exited = true;
        t->exit_value = status;
    }
    t->cv.notify_all();
}

}

Thread* current() { return t_current; }

void status_dump() {
    stacks::dump();
    std::lock_guard lk(g_lock);
    for (auto& [id, t] : g_threads) {
        if (t->exited) continue;
        uint64_t n = t->syscalls.load();
        uint32_t sc = t->in_syscall.load();
        LOG_INFO("status", "ppu 0x%x '%s' %s%u (r3 0x%llx r31 0x%llx) syscalls +%llu lr 0x%llx", id, t->name.c_str(), sc ? "blocked in " : "running, last ", sc ? sc - 1 : 0,
                 static_cast<unsigned long long>(t->ctx.gpr[3]), static_cast<unsigned long long>(t->ctx.gpr[31]), static_cast<unsigned long long>(n - t->syscalls_seen), static_cast<unsigned long long>(t->ctx.lr));
        t->syscalls_seen = n;
        {
            std::string top;
            for (int k = 0; k < 4; k++) {
                uint32_t best = 0, bi = 0;
                for (uint32_t i = 0; i < 1024; i++) if (t->syscall_hist[i] > best) { best = t->syscall_hist[i]; bi = i; }
                if (best < 100) break;
                top += " " + std::to_string(bi) + "x" + std::to_string(best);
                t->syscall_hist[bi] = 0;
            }
            std::memset(t->syscall_hist, 0, sizeof t->syscall_hist);
            if (!top.empty()) LOG_INFO("status", "   top syscalls%s", top.c_str());
        }
        if (!sc) {
            auto& g = t->ctx.gpr;
            LOG_INFO("status", "   regs r3 %llx r4 %llx r5 %llx r6 %llx r7 %llx r9 %llx r10 %llx r11 %llx", (unsigned long long)g[3], (unsigned long long)g[4], (unsigned long long)g[5],
                     (unsigned long long)g[6], (unsigned long long)g[7], (unsigned long long)g[9], (unsigned long long)g[10], (unsigned long long)g[11]);
        }
        if (sc == 98) lv2::lwmutex_debug(static_cast<uint32_t>(t->ctx.gpr[3]));
        if ((!sc || sc == 142 || t->name == "main_thread") && t->native && !host::native_is_current(t->native)) LOG_INFO("status", "   %s", crash::sample_stack(t->native, 14).c_str());
    }
}

std::shared_ptr<Thread> find(uint32_t id) {
    std::lock_guard lk(g_lock);
    auto it = g_threads.find(id);
    return it == g_threads.end() ? nullptr : it->second;
}

std::shared_ptr<Thread> create(const std::string& name, uint32_t entry_opd, uint64_t arg0, uint64_t arg1, int32_t prio, uint32_t stack_size, uint32_t tls, bool joinable) {
    stack_size = (stack_size + 0xFFF) & ~0xFFFu;
    if (stack_size < 0x4000) stack_size = 0x4000;
    /* One contiguous block: 64 KB of guard followed by the stack itself. The
     * guard MUST be part of the same reservation -- allocating it separately
     * puts it above the stack and the overflow walks through the hole. */
    constexpr uint32_t GUARD = 0x10000;
    uint32_t block = vm::alloc(vm::Region::Stack, stack_size + GUARD, 0x1000);
    if (!block) return nullptr;
    uint32_t stack = block + GUARD;
    auto t = std::make_shared<Thread>();
    t->name = name;
    t->entry_opd = entry_opd;
    t->stack_addr = stack;
    t->stack_size = stack_size;
    t->prio = prio;
    t->joinable = joinable;
    t->ctx.gpr[1] = stack + stack_size - 0x70;
    t->ctx.gpr[13] = tls;
    /* Record the stack and put an inaccessible 64 KB page below it, so a
     * downward overflow faults on the guard instead of silently corrupting a
     * heap node. */
    {
        std::lock_guard lk(g_lock);
        t->id = g_next_id++;
    }
    if (!stacks::split(t->id, name.c_str(), stack_size, block, stack_size + GUARD)) {
        LOG_ERROR("stacks", "thread '%s': guard could not be installed; its overflow would be silent", name.c_str());
    }
    t->ctx.gpr[3] = arg0;
    t->ctx.gpr[4] = arg1;
    t->ctx.vscr_nj = 1;
    {
        std::lock_guard lk(g_lock);
        g_threads[t->id] = t;
    }
    t->ctx.thread_id = t->id;
    return t;
}

void start(const std::shared_ptr<Thread>& t) {
    std::lock_guard lk(t->lock);
    if (t->started) return;
    t->started = true;
    t->host = std::thread(body, t);
    t->host.detach();
}

std::shared_ptr<Thread> spawn_host(const std::string& name, int32_t prio, uint32_t stack_size, uint32_t tls, std::function<void(Thread&)> fn) {
    auto t = create(name, 0, 0, 0, prio, stack_size, tls, true);
    if (!t) return nullptr;
    std::lock_guard lk(t->lock);
    t->started = true;
    t->host = std::thread([t, fn = std::move(fn)]() {
        t_current = t.get();
        t->native = host::current_native();
        host::set_thread_name("PPU " + t->name);
        placement::apply("ppu:" + t->name);
        LOG_INFO("ppu", "host thread 0x%x '%s' start", t->id, t->name.c_str());
        fn(*t);
        LOG_INFO("ppu", "host thread 0x%x '%s' exit", t->id, t->name.c_str());
        {
            std::lock_guard lk2(t->lock);
            t->exited = true;
        }
        t->cv.notify_all();
        t_current = nullptr;
    });
    return t;
}

void join_host(const std::shared_ptr<Thread>& t) {
    if (!t) return;
    if (t->host.joinable()) t->host.join();
    remove(t->id);
    vm::dealloc(t->stack_addr);
}

void remove(uint32_t id) {
    stacks::remove(id);
    std::lock_guard lk(g_lock);
    g_threads.erase(id);
}

void call_opd(ppu::Context& ctx, uint8_t* base, uint32_t opd) {
    uint32_t code = vm::read32(opd);
    ctx.gpr[2] = vm::read32(opd + 4);
    ctx.lr = 0;
    ppu::call_indirect(ctx, base, code);
}

}
