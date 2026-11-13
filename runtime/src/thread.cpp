#include "runtime/placement.h"
#include "runtime/host.h"
#include "runtime/start_gate.h"
#include "runtime/stacks.h"
#include "runtime/thread.h"
#include "runtime/kernel.h"
#include "runtime/loader.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "ppu/ops.h"
#include "lv2/event.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

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
void lwmutex_probe_report(const char* where);
void syscall_error_report(const char* where);
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
    /* A thread that starts and immediately exits has stopped running too, so a
     * creator waiting on it is released rather than parked for the whole budget. */
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
                 lv2::describe_address(static_cast<uint32_t>(t->ctx.lr)).c_str());
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
        /* quick_exit runs at_quick_exit handlers, NOT atexit ones, so the
         * lwmutex totals have to be printed here or a run that ends by
         * stopping (which is most of them) reports nothing. */
        lv2::lwmutex_probe_report("process exit");
        lv2::syscall_error_report("process exit");
        lv2::queue_counts_report("process exit");
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
    t->gate_open.store(true, std::memory_order_release);
    t->cv.notify_all();
}

}

Thread* current() { return t_current; }

/* The TLS block table, read out of the live VM.
 *
 * The PS3 thread pointer is not the block: RPCS3's sys_initialize_tls
 * (refs/rpcs3 .../sys_ppu_thread_.cpp:90) sets gpr[13] = block + 0x7000 + 0x30
 * and one thread's block is 0x30 (system area) + tls_memsz. So the block behind
 * an r13 is r13 - 0x7030, the .tdata image begins at block + 0x30, and the
 * .tbss tail is everything after the image up to memsz. Printing all three per
 * thread is what separates "the block is null" from "the block was reused" from
 * "the block is fine and the pointer is wrong", which are three different bugs
 * that all present as a fault near a TLS access. */
void tls_census(const char* why) {
    const loader::ProcessInfo& pi = loader::g_process;
    const uint32_t img = pi.tls_vaddr, file = pi.tls_filesz, mem = pi.tls_memsz;
    const uint32_t block_size = mem + 0x30;
    LOG_INFO("tls", "census (%s): image 0x%08x file 0x%x mem 0x%x -> block 0x%x (tbss 0x%x)", why ? why : "?", img, file, mem, block_size,
             mem > file ? mem - file : 0);
    const bool have_image = img && file && vm::range_mapped(img, file);
    std::vector<uint8_t> tmpl(have_image ? file : 0);
    if (have_image) std::memcpy(tmpl.data(), vm::ptr(img), file);
    std::lock_guard lk(g_lock);
    LOG_INFO("tls", "census: %zu thread(s) registered", g_threads.size());
    std::map<uint32_t, std::vector<uint32_t>> owners;
    int alive = 0, null_tp = 0, tdata_ok = 0, tdata_bad = 0, tbss_ok = 0, tbss_bad = 0;
    for (auto& [id, t] : g_threads) {
        const uint32_t tp = static_cast<uint32_t>(t->ctx.gpr[13]);
        if (!t->exited) alive++;
        if (!tp) {
            null_tp++;
            LOG_INFO("tls", "  0x%08x '%-28s' r13=00000000 -- NO TLS BLOCK", id, t->name.c_str());
            continue;
        }
        const uint32_t blk = tp - 0x7030;
        const bool mapped = vm::range_mapped(blk, block_size);
        owners[blk].push_back(id);
        bool d_ok = false, b_ok = false;
        if (mapped && have_image && vm::range_mapped(blk + 0x30, mem)) {
            d_ok = std::memcmp(vm::ptr(blk + 0x30), tmpl.data(), file) == 0;
            bool zero = true;
            const uint8_t* p = vm::ptr(blk + 0x30 + file);
            for (uint32_t i = 0; i < mem - file; i++) {
                if (p[i]) { zero = false; break; }
            }
            b_ok = zero;
        }
        (d_ok ? tdata_ok : tdata_bad)++;
        (b_ok ? tbss_ok : tbss_bad)++;
        LOG_INFO("tls", "  0x%08x '%-28s' r13=%08x block=%08x mapped=%d tdata=%s tbss=%s %s", id, t->name.c_str(), tp, blk,
                 mapped ? 1 : 0, d_ok ? "copied" : "NOT-COPIED", b_ok ? "zeroed" : "NOT-ZEROED", t->exited ? "exited" : "");
    }
    LOG_INFO("tls", "census: alive %d, null thread-pointer %d, tdata copied %d/not %d, tbss zeroed %d/not %d", alive, null_tp, tdata_ok,
             tdata_bad, tbss_ok, tbss_bad);
    for (auto& [blk, ids] : owners) {
        if (ids.size() < 2) continue;
        LOG_WARN("tls", "census: block 0x%08x SHARED by %zu threads:", blk, ids.size());
        for (uint32_t i : ids) LOG_WARN("tls", "    shared with 0x%08x", i);
    }
}

void status_dump() {
    stacks::dump();
    lv2::lwmutex_probe_report("status dump");
    lv2::syscall_error_report("status dump");
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

void start(const std::shared_ptr<Thread>& t, int32_t creator_prio, uint32_t creator_id) {
    {
        std::lock_guard lk(t->lock);
        if (t->started) return;
        t->started = true;
        /* Arm the gate BEFORE the host thread exists, so the child cannot run
         * its first syscall before the flag it clears is set. The decision is
         * made here, once, by the creator: whether this child starts ahead of
         * it (start_gate::takes_precedence). */
        if (start_gate::enabled() && start_gate::takes_precedence(t->prio, creator_prio)) {
            t->gate_armed.store(true, std::memory_order_relaxed);
            t->gate_creator_prio = creator_prio;
            t->gate_creator_id = creator_id;
        }
        t->host = std::thread(body, t);
        t->host.detach();
    }
    if (!t->gate_armed.load(std::memory_order_acquire)) return;
    /* The console's rule (refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_ppu_thread.cpp:608):
     * the creator does not return from sys_ppu_thread_start until the child has
     * started and then stopped running -- blocked or preempted. Bounded by the
     * same 5000 us budget RPCS3 uses, and the bound is REPORTED: a silent
     * timeout here would be indistinguishable from "the child blocked at once",
     * and the two mean different things about the title. */
    const bool ok = start_gate::wait_until_ready(
        [&] {
            if (t->gate_open.load(std::memory_order_acquire)) return true;
            std::lock_guard lk(t->lock);
            return t->exited;
        },
        start_gate::clock_us, start_gate::budget_us());
    static std::atomic<uint64_t> gates{0}, timeouts{0};
    static const bool trace = std::getenv("PS3BASE_PPU_START_GATE_TRACE") != nullptr;
    const uint64_t n = gates.fetch_add(1) + 1;
    if (!ok) timeouts.fetch_add(1);
    if (trace || !ok) {
        LOG_INFO("ppu", "start gate: '%s' 0x%x prio %d ahead of creator 0x%x prio %d -> %s (%llu gated, %llu timed out)",
                 t->name.c_str(), t->id, t->prio, creator_id, creator_prio,
                 ok ? "child reached a blocking point" : "budget exhausted, creator resumed",
                 static_cast<unsigned long long>(n), static_cast<unsigned long long>(timeouts.load()));
    }
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
