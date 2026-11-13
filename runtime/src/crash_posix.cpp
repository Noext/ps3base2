/* The crash reporter walks x86 machine state: REG_RIP, si_addr, signal frames.
 * None of that exists in WebAssembly, and there are no POSIX signals there
 * either. Under wasm this file provides inert stubs so the callers link; the
 * bitset memory backend already reports overruns in software instead.
 */
#if defined(__wasm__) || defined(__EMSCRIPTEN__)

#include "runtime/log.h"

#include <string>

namespace crash {

void install() {}
std::string sample_stack(void* native, int depth) {
    (void)native;
    (void)depth;
    return "<no stack in wasm>";
}

}  // namespace crash

#else

#ifndef _WIN32
#include "runtime/codewatch.h"
#include "runtime/fibertrace.h"
#include "runtime/hooks.h"
#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/thread.h"
#include "runtime/trace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <cxxabi.h>
#include <dlfcn.h>
#include "runtime/backtrace_port.h"
#include "ppu/ops.h"
#include <pthread.h>
#include <sys/mman.h>
#include <ucontext.h>

namespace crash {

namespace {

std::string symbol_of(uintptr_t addr) {
    Dl_info info{};
    char out[512];
    if (dladdr(reinterpret_cast<void*>(addr), &info) && info.dli_sname) {
        int st = 0;
        char* dm = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &st);
        std::snprintf(out, sizeof out, "%s+0x%llx", st == 0 && dm ? dm : info.dli_sname, static_cast<unsigned long long>(addr - reinterpret_cast<uintptr_t>(info.dli_saddr)));
        std::free(dm);
        return out;
    }
    if (info.dli_fname) {
        const char* base = std::strrchr(info.dli_fname, '/');
        bool exe = base && std::strcmp(base + 1, program_invocation_short_name) == 0;
        std::snprintf(out, sizeof out, "%s+0x%llx", exe ? "exe" : base ? base + 1 : info.dli_fname, static_cast<unsigned long long>(addr - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        return out;
    }
    return "?";
}

int stack_from(const ucontext_t* uc, void** pcs, int depth) {
    void* raw[64];
    int n = backtrace_port::capture(raw, 64);
    uintptr_t rip = uc ? static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]) : 0;
    int start = 0;
    bool found = false;
    if (uc) {
        start = std::min(n, 2);
        for (int i = 0; i < n; i++) {
            if (reinterpret_cast<uintptr_t>(raw[i]) == rip) {
                start = i;
                found = true;
                break;
            }
        }
    }
    int out = 0;
    /* A rip of 0 is a NULL CODE POINTER being called, not a return address:
     * nothing in backtrace() will ever equal it, so the search above cannot
     * succeed. MEASURED: the old code then left start at 2 and printed the single
     * frame "#0 ?" -- the whole host stack was thrown away exactly when it was
     * most needed. RDR's rare crash (rip = 0) produced no frame to resolve for
     * that reason.
     *
     * So: always emit the faulting pc, and when it is not found in the captured
     * stack, keep the captured stack as well instead of skipping it. A caller
     * reading the dump then sees both "we jumped to 0" and the frames that made
     * the call. */
    if (uc) pcs[out++] = reinterpret_cast<void*>(rip);
    if (uc && !found && rip == 0) start = 0;   /* keep the whole captured stack */
    else if (uc && !found) start = std::min(n, 2);
    for (int i = start; i < n && out < depth; i++) pcs[out++] = raw[i];
    return out;
}

std::string stack_string(void* const* pcs, int n) {
    std::string line;
    for (int i = 0; i < n; i++) line += " <- " + symbol_of(reinterpret_cast<uintptr_t>(pcs[i]));
    return line;
}

struct Trap {
    uint32_t addr = 0;
    uint32_t len = 4;
    bool reads = false;
    bool has_val = false;
    bool dump = false;
    bool dumped = false;
    uint32_t val = 0;
    uint64_t hits = 0;
    uint32_t logged = 0;
};

struct PendingHit {
    int trap;
    uint32_t fault;
    bool write;
    /* Y1: the word AS IT WAS at fault time. The mprotect fault fires at the
     * instruction, before the store retires, so capturing here is the only way
     * to report "prev -> new" instead of just the value afterwards. */
    uint32_t prev;
    /* Y1: guest context at fault time. ctx.lr is the return address of the
     * caller that made the access, which names the site; ctx.cia is only
     * maintained for syscalls, so it is reported but not relied on. */
    uint32_t lr;
    uint32_t cia;
    /* Copied, not pointed at: the guest thread's name buffer can be rewritten or
     * freed between the fault and the single-step trap. */
    char thread[32];
    /* Y1: set when the faulting access was the block watch reading the watched
     * word for its "prev" field, not the guest. Reported with a SELF-READ tag
     * and never counted as a guest read. */
    bool self_read;
    void* pcs[12];
    int npcs;
};

std::vector<Trap> g_traps;
std::unordered_map<uint32_t, host::Prot> g_trap_pages;
int g_watch_count = 0;
uint32_t g_trap_max = 50;
/* Y1: how many mprotect faults were the watch reading its own watched word.
 * Printed with the trap summary, so "0 self reads" is a measurement. */
std::atomic<uint64_t> g_self_reads{0};
std::mutex g_trap_lock;
thread_local uint32_t t_step_page = 0;
/* Set when the single step belongs to the diagnostic page guard rather than to a
 * GOWA_TRAP: the two use the same trap flag, and only one of them re-arms. */
thread_local bool t_step_guard = false;
thread_local std::vector<PendingHit> t_hits;

/* Y1: continuity check. Every reported event carries the word as it was just
 * before the access, so consecutive events on the same address must chain:
 * event[n].prev == event[n-1].new. A break means an access was NOT reported,
 * i.e. it slipped through the step window (the page is left writable for the
 * one instruction being single-stepped, so a store from another thread in that
 * window is not trapped). Reporting the break is what makes the watch's own
 * coverage measurable instead of assumed. */
/* Per ADDRESS, not per trap: a trap may cover a range (the SPU descriptor is
 * 0x84 bytes), and two words in that range have unrelated values, so a
 * per-trap chain would report a gap on every alternating access. 256 words
 * tracked covers every address any of these watches uses. */
struct ChainState { uint32_t addr; uint32_t val; uint64_t events; uint64_t gaps; bool used; };
ChainState g_chain[256];

ChainState& chain_for(uint32_t addr) {
    for (int i = 0; i < 256; i++)
        if (g_chain[i].used && g_chain[i].addr == addr) return g_chain[i];
    for (int i = 0; i < 256; i++)
        if (!g_chain[i].used) {
            g_chain[i] = ChainState{addr, 0, 0, 0, true};
            return g_chain[i];
        }
    return g_chain[255];
}

void process_hit(const PendingHit& h) {
    Trap& t = g_traps[h.trap];
    /* Read AFTER the retried instruction retired (trap_handler runs on the
     * single-step trap), so for a write this is the NEW value and h.prev is the
     * OLD one. */
    uint32_t value = vm::read32(t.len <= 4 ? t.addr : (h.fault & ~3u));
    if (t.has_val && vm::read32(t.addr) != t.val) return;
    /* GOWA_TRAP_NONZERO: only report writes whose value is non-zero, so a
     * zero-filling writer cannot mask the one that carries the data. */
    if (const char* nz = std::getenv("GOWA_TRAP_NONZERO"); nz && *nz != '0' && h.write && value == 0) return;
    std::lock_guard lk(g_trap_lock);
    if (h.self_read) {
        /* The watch reading the word for its own "prev". It must be attributed,
         * not hidden: a guest read and a self read are the same mprotect fault,
         * and mistaking one for the other is how a watcher invents the event it
         * was built to find. Counted and logged separately, so the number of
         * self reads is in the log and a fabricated read cannot pass for a
         * guest one. */
        g_self_reads.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    t.hits++;
    uint32_t stack_id = 0;
    if (t.logged < g_trap_max) {
        t.logged++;
        std::string stack = stack_string(h.pcs, h.npcs);
        guest::Thread* g = guest::current();
        std::string who = g ? g->name : host::thread_name();
        /* Y1: prev -> new, guest lr, guest thread and the elapsed time, all in
         * one line, so a read and a write of the same word are comparable
         * within one run without a second log. */
        LOG_INFO("trap", "trap %d %s 0x%08x prev 0x%08x -> 0x%08x (guest lr 0x%08x) by %s%s",
                 h.trap, h.write ? "write" : "read", h.fault, h.prev, value, h.lr,
                 h.thread[0] ? h.thread : who.c_str(),
                 t.logged == g_trap_max ? " (further hits not logged)" : "");
        LOG_INFO("trap", "  %s", stack.c_str());
        if (trace::on(trace::TRAP)) stack_id = trace::intern(stack);
        /* Y1 continuity: if the word did not hold what the previous reported
         * event left there, an access went unreported -- a real gap in the
         * watch, printed as one rather than hidden. */
        const uint32_t word = t.len <= 4 ? t.addr : (h.fault & ~3u);
        ChainState& cs = chain_for(word);
        cs.events++;
        if (cs.events > 1 && h.prev != cs.val) {
            cs.gaps++;
            LOG_WARN("trap", "CHAIN GAP trap %d at 0x%08x: previous event left 0x%08x, this one starts from 0x%08x (%llu gaps so far) -- an access was NOT watched",
                     h.trap, word, cs.val, h.prev, static_cast<unsigned long long>(cs.gaps));
        }
        cs.val = value;
    }
    /* Y1: a running total, emitted from the event path so it appears even when
     * the title ends through _sys_process_exit (where atexit never runs). "0
     * self reads" is then a printed number, not an absence. */
    static std::atomic<uint64_t> g_reported{0};
    if (g_reported.fetch_add(1, std::memory_order_relaxed) % 200 == 0)
        LOG_INFO("trap", "watch totals: %llu guest events, %llu self reads dropped (the watch reading a watched word for its own prev)",
                 static_cast<unsigned long long>(g_reported.load()), static_cast<unsigned long long>(g_self_reads.load()));
    if (trace::on(trace::TRAP)) trace::emit(trace::T_TRAP, uint32_t(h.trap) | (uint32_t(h.write) << 8), h.fault, value, stack_id, t.len <= 4 ? 0 : vm::read32(t.addr));
    if (t.dump && !t.dumped) {
        t.dumped = true;
        char fn[64];
        std::snprintf(fn, sizeof fn, "trap_%08x.bin", t.addr);
        trace::dump_memory(fn);
    }
}

/* The diagnostic page guard. A guarded page is PROT_NONE, so both a load and a
 * store land here, which is what makes it a READ watch as well as a write one.
 * The page is opened, the trap flag is set, and the instruction is retried;
 * trap_handler() re-arms the page after that one instruction. Nothing here can
 * kill the run, and the report names the direction, the guest context and the
 * host stack. */
bool guard_fault(uint32_t g, bool is_write, ucontext_t* uc) {
    if (!vm::guard_armed(g)) return false;
    if (vm::guard_wants_log(g)) {
        void* pcs[12];
        const int n = stack_from(uc, pcs, 12);
        guest::Thread* t = guest::current();
        /* The value the guest sees, read through the opened page below; the
         * address and the direction are what matter for a read watch. */
        LOG_INFO("guard", "%s 0x%08x by %s%s", is_write ? "write" : "read", g,
                 t ? t->name.c_str() : host::thread_name().c_str(), n ? "" : " (no stack)");
        if (n) {
            std::string stack;
            for (int i = 0; i < n; i++) stack += " <- " + symbol_of(reinterpret_cast<uintptr_t>(pcs[i]));
            LOG_INFO("guard", "  %s", stack.c_str());
        }
    }
    vm::guard_fault(g);
    t_step_page = (g >> 12) + 1;
    t_step_guard = true;
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100;
    return true;
}

bool trap_fault(uint32_t g, bool is_write, ucontext_t* uc) {
    if (!g_trap_pages.count(g >> 12)) return false;
    /* Y1: the watch reads the watched word itself, to report "prev". When the
     * word sits on a trap page that read faults right here, and without this
     * guard it is published as a guest READ carrying the guest lr of whatever
     * store happened to be interrupted -- a fabricated event that mimics the
     * reader under investigation. Measured on RDR run B: 6 of 575 events on
     * 0x0177caa8 were the watch reading itself, and each one made the store it
     * interrupted look preceded by a read. The page is opened and the single
     * step is still required (the read must complete), so the fix is to record
     * the hit for the step but mark it self-inflicted and drop it in
     * process_hit. */
    const bool self_read = !is_write && cw::t_self_read_depth > 0;
    for (int i = 0; i < static_cast<int>(g_traps.size()); i++) {
        const Trap& t = g_traps[i];
        if (uint64_t(g) + 16 <= t.addr || uint64_t(g) >= uint64_t(t.addr) + t.len) continue;
        if (!is_write && !t.reads) continue;
        PendingHit h{};
        h.trap = i;
        h.fault = g;
        h.write = is_write;
        h.self_read = self_read;
        guest::Thread* th = guest::current();
        if (th) {
            h.lr = static_cast<uint32_t>(th->ctx.lr);
            h.cia = static_cast<uint32_t>(th->ctx.cia);
            std::snprintf(h.thread, sizeof h.thread, "%s", th->name.c_str());
        }
        h.npcs = stack_from(uc, h.pcs, 12);
        t_hits.push_back(h);
    }
    /* Y1: make the page accessible BEFORE reading the watched word. It is
     * currently read-only (write trap) or no-access (read/write trap), so a
     * read here would fault inside the fault handler. The watch's own reads and
     * the guest's reads of this page both go through this window, which is why
     * the page is left writable until the single-step trap re-protects it. */
    host::vm_protect(vm::g_base + (uint64_t(g >> 12) << 12), 4096, host::Prot::ReadWrite);
    /* The word before the store, and the guest context at the instant of the
     * access. The store has NOT executed yet -- it retries when this handler
     * returns -- so this prev is genuinely the pre-store value, which is what
     * makes "0 -> 1" readable rather than "the field is 1". */
    for (PendingHit& h : t_hits) {
        const Trap& t = g_traps[h.trap];
        h.prev = vm::range_mapped(t.addr, 4) ? vm::read32(t.addr) : 0;
    }
    t_step_page = (g >> 12) + 1;
    uc->uc_mcontext.gregs[REG_EFL] |= 0x100;
    return true;
}

struct sigaction g_old_segv, g_old_bus, g_old_ill, g_old_fpe;

void chain(int sig, siginfo_t* si, void* ctx) {
    struct sigaction* old = sig == SIGSEGV ? &g_old_segv : sig == SIGBUS ? &g_old_bus : sig == SIGILL ? &g_old_ill : &g_old_fpe;
    if (old->sa_flags & SA_SIGINFO) {
        if (old->sa_sigaction) return old->sa_sigaction(sig, si, ctx);
    } else if (old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) {
        return old->sa_handler(sig);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void fault_handler(int sig, siginfo_t* si, void* ctx) {
    auto* uc = static_cast<ucontext_t*>(ctx);
    auto* fault = static_cast<uint8_t*>(si->si_addr);
    bool guest = sig == SIGSEGV && vm::g_base && fault >= vm::g_base && fault < vm::g_base + 0x100000000ull;
    bool is_write = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
    if (guest) {
        uint32_t g = static_cast<uint32_t>(fault - vm::g_base);
        /* The diagnostic guard first: it is an explicit request by address, so
         * it outranks the texture watch's implicit repair. */
        if (vm::guard_armed(g) && guard_fault(g, is_write, uc)) return;
        /* Traps next. watch_fault would otherwise repair the page and clear its
         * watched bit, so a trap on the same page fired exactly once and the
         * writer of interest never appeared in the log. */
        if (g_watch_count && trap_fault(g, is_write, uc)) return;
        if (is_write && vm::watch_fault(g, g_watch_count == 0)) return;
    }
    static std::atomic<bool> once{false};
    if (once.exchange(true)) return chain(sig, si, ctx);
    guest::Thread* t = guest::current();
    uintptr_t rip = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    uint64_t base = reinterpret_cast<uint64_t>(vm::g_base);
    LOG_ERROR("crash", "signal %d at %p (%s) fault %p (guest 0x%llx)%s thread %s 0x%x", sig, reinterpret_cast<void*>(rip), symbol_of(rip).c_str(), si->si_addr,
              static_cast<unsigned long long>(reinterpret_cast<uint64_t>(si->si_addr) - base), is_write ? " write" : "", t ? t->name.c_str() : host::thread_name().c_str(), t ? t->id : 0);
    void* pcs[24];
    int n = stack_from(uc, pcs, 24);
    for (int i = 0; i < n; i++) LOG_ERROR("crash", "  #%d %s", i, symbol_of(reinterpret_cast<uintptr_t>(pcs[i])).c_str());
    /* The last indirect calls, oldest first. RDR's rare crash is rip = 0 -- a
     * NULL HOST CODE POINTER -- and the host stack alone cannot say which guest
     * call produced it, because the fault is in the dispatcher rather than in
     * guest code. This ring names the call that led there. */
    LOG_ERROR("crash", "  last indirect calls (oldest first): lr -> target (ctr, r3)");
    ppu::indirect_history([](uint32_t lr, uint32_t target, uint32_t ctr, uint32_t r3) {
        LOG_ERROR("crash", "    lr 0x%08x -> 0x%08x (ctr 0x%08x, r3 0x%08x)", lr, target, ctr, r3);
    });
    /* The cellFiber events that led here. Printed BEFORE the register dump so
     * the reader sees the sequence first: what the fault is a consequence of is
     * the last import that ran, not the address it faulted at. */
    fibertrace::dump("crash");
    LOG_ERROR("crash", "  cellFiber import nesting depth on this thread: %d%s", fibertrace::depth(),
              fibertrace::depth() > 0 ? " (the fault happened INSIDE a cellFiber import)" : "");
    if (t) {
        auto& c = t->ctx;
        LOG_ERROR("crash", "  guest lr 0x%llx r1 0x%llx r2 0x%llx r3 0x%llx r4 0x%llx r5 0x%llx", static_cast<unsigned long long>(c.lr), static_cast<unsigned long long>(c.gpr[1]),
                  static_cast<unsigned long long>(c.gpr[2]), static_cast<unsigned long long>(c.gpr[3]), static_cast<unsigned long long>(c.gpr[4]), static_cast<unsigned long long>(c.gpr[5]));
        /* Dump the objects the failing call was built from, and every general
         * register, so the null field and its loader can be read from the crash
         * instead of inferred from symbolised frames (inlining makes those
         * ambiguous). */
        char regs[512];
        int off = 0;
        for (int i = 0; i < 32 && off < 480; i++)
            off += std::snprintf(regs + off, sizeof regs - off, " r%d=0x%llx", i,
                                 static_cast<unsigned long long>(c.gpr[i]));
        LOG_ERROR("crash", "  guest regs:%s", regs);
        /* The whole TLS block table, not just this thread's window. A crash at
         * a small guest offset is a TLS fault until proven otherwise, and the
         * question is which thread's block the pointer names -- which the one
         * window cannot answer. */
        guest::tls_census("crash");
        {
            /* r13 is the thread control block; the liblv2 accessor reads a field
             * from it. Dump it whenever it is mapped. */
            const uint32_t tcb = static_cast<uint32_t>(c.gpr[13]);
            if (tcb && vm::is_mapped(tcb) ) {
                char w[520]; int o = 0;
                for (int i = -64; i < 64 && o < 480; i += 4) {
                    const uint32_t a = uint32_t(int64_t(tcb) + i);
                    if (!vm::is_mapped(a)) continue;
                    o += std::snprintf(w + o, sizeof w - o, " %08x", vm::read32(a));
                }
                LOG_ERROR("crash", "  tcb r13 (0x%08x):%s", tcb, w);
            }
        }
        {
            /* The faulting argument came from a TOC slot; dump the liblv2 TOC
             * around it so the runtime value can be compared with the file. */
            const uint32_t toc = static_cast<uint32_t>(c.gpr[2]);
            if (toc == 0x101c620u) {
                const uint32_t slot = toc - 32444u;   /* 0x1014764 */
                LOG_ERROR("crash", "  toc slot 0x%08x = 0x%08x (file value 0x01014a40)",
                          slot, vm::read32(slot));
                char w[520]; int o = 0;
                for (int i = -32; i < 64 && o < 460; i += 4) {
                    const uint32_t a = slot + uint32_t(int64_t(i));
                    o += std::snprintf(w + o, sizeof w - o, " %08x", vm::read32(a));
                }
                LOG_ERROR("crash", "  toc window @0x%08x:%s", slot - 32, w);
            }
        }
        for (int r = 3; r <= 7; r++) {
            const uint32_t ea = static_cast<uint32_t>(c.gpr[r]);
            if (!ea || !vm::is_mapped(ea) || !vm::range_mapped(ea, 256)) continue;
            char words[700];
            int o2 = 0;
            for (int i = 0; i < 64 && o2 < 660; i += 4)
                o2 += std::snprintf(words + o2, sizeof words - o2, " %08x", vm::read32(ea + i));
            LOG_ERROR("crash", "  object r%d (0x%08x):%s", r, ea, words);
        }
    }
    if (!std::getenv("GOWA_CRASH_NO_BLACKBOX")) hooks::capture("crash");
    std::fflush(nullptr);
    chain(sig, si, ctx);
}

void trap_handler(int, siginfo_t*, void* ctx) {
    auto* uc = static_cast<ucontext_t*>(ctx);
    if (!t_step_page) return;
    uint32_t page = t_step_page - 1;
    t_step_page = 0;
    uc->uc_mcontext.gregs[REG_EFL] &= ~0x100ll;
    const bool guard = t_step_guard;
    t_step_guard = false;
    std::vector<PendingHit> hits;
    hits.swap(t_hits);
    for (const PendingHit& h : hits) process_hit(h);
    /* Guarded pages go back to PROT_NONE so the next access faults too; a trap
     * page goes back to whatever GOWA_TRAP asked for. */
    if (guard) vm::guard_rearm(page);
    else host::vm_protect(vm::g_base + (uint64_t(page) << 12), 4096, g_trap_pages[page]);
}

const int kSampleSig = SIGRTMIN + 2;
std::mutex g_sample_lock;
std::atomic<int> g_sample_ready{0};
void* g_sample_pcs[32];
int g_sample_depth = 0;
int g_sample_n = 0;

void sample_handler(int, siginfo_t*, void* ctx) {
    g_sample_n = stack_from(static_cast<ucontext_t*>(ctx), g_sample_pcs, g_sample_depth);
    g_sample_ready.store(1, std::memory_order_release);
}

int sample_pcs(void* native, void** pcs, int depth) {
    std::lock_guard lk(g_sample_lock);
    g_sample_depth = std::min(depth, 32);
    g_sample_n = 0;
    g_sample_ready.store(0, std::memory_order_release);
    if (pthread_kill(reinterpret_cast<pthread_t>(native), kSampleSig) != 0) return 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (!g_sample_ready.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > end) return 0;
        std::this_thread::yield();
    }
    std::memcpy(pcs, g_sample_pcs, sizeof(void*) * g_sample_n);
    return g_sample_n;
}

void handle(int sig, void (*fn)(int, siginfo_t*, void*), struct sigaction* old) {
    struct sigaction sa{};
    sa.sa_sigaction = fn;
    /* SA_ONSTACK: the handler runs on the alternate stack installed by
     * install_alt_stack(). Without it a fault caused by a stack overflow cannot
     * be reported at all -- the handler has no room to run and the process dies
     * with a bare "Segmentation fault" and no log, which is exactly what the
     * renderer crash produced. */
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, old);
}

}

/* One alternate signal stack PER THREAD.
 *
 * sigaltstack() is per-thread: calling it once on the main thread leaves every
 * other thread -- the renderer, the PPU workers, the sysutil pump -- without a
 * stack for the handler, so their faults cannot be reported. This is called from
 * install() and again from each thread entry. */
void install_alt_stack() {
    static thread_local std::vector<uint8_t> stack;
    if (!stack.empty()) return;
    stack.resize(SIGSTKSZ < 64 * 1024 ? 64 * 1024 : SIGSTKSZ);
    stack_t ss{};
    ss.ss_sp = stack.data();
    ss.ss_size = stack.size();
    ss.ss_flags = 0;
    if (sigaltstack(&ss, nullptr) != 0)
        LOG_WARN("crash", "sigaltstack failed on this thread: no fault report here");
}

void install() {
    /* A WebAssembly build has no POSIX signals, and the bitset memory backend is
     * what such a build uses. Refuse to install on that path so the dependency is
     * gone rather than merely unused: measured by 'grep sigaction' on the binary.
     * PS3BASE_MEMORY=bitset means "no signals" by construction. */
    if (const char* e = std::getenv("PS3BASE_MEMORY"); e && std::strcmp(e, "bitset") == 0) {
        LOG_INFO("crash", "signals not installed: bitset memory backend has no fault path");
        return;
    }
    void* warm[4];
    backtrace_port::capture(warm, 4);
    install_alt_stack();
    handle(SIGSEGV, fault_handler, &g_old_segv);
    handle(SIGBUS, fault_handler, &g_old_bus);
    handle(SIGILL, fault_handler, &g_old_ill);
    handle(SIGFPE, fault_handler, &g_old_fpe);
    handle(SIGTRAP, trap_handler, nullptr);
    handle(kSampleSig, sample_handler, nullptr);
}

std::string sample_stack(void* native, int depth) {
    void* pcs[32];
    int n = sample_pcs(native, pcs, std::min(depth, 32));
    return stack_string(pcs, n);
}

void profile_thread(void* native, const char* name) {
    std::thread([native, name] {
        std::map<std::string, uint64_t> counts;
        uint64_t total = 0;
        auto last = std::chrono::steady_clock::now();
        static const int depth = std::getenv("GOWA_PROFILE_DEPTH") ? std::min(16, std::atoi(std::getenv("GOWA_PROFILE_DEPTH"))) : 3;
        static const size_t top_n = std::getenv("GOWA_PROFILE_TOP") ? size_t(std::atoi(std::getenv("GOWA_PROFILE_TOP"))) : size_t(12);
        for (;;) {
            host::sleep_ms(1);
            void* pcs[16];
            int n = sample_pcs(native, pcs, depth);
            if (!n) continue;
            std::string key;
            for (int i = 0; i < n; i++) {
                std::string sym = symbol_of(reinterpret_cast<uintptr_t>(pcs[i]));
                size_t plus = sym.rfind('+');
                if (plus != std::string::npos && sym.compare(0, plus, "exe") != 0) sym.resize(plus);
                key += (i ? " <- " : "") + sym;
            }
            counts[key]++;
            total++;
            auto now = std::chrono::steady_clock::now();
            if (now - last > std::chrono::seconds(5)) {
                last = now;
                std::vector<std::pair<uint64_t, std::string>> top;
                for (auto& [k, v] : counts) top.push_back({v, k});
                std::sort(top.rbegin(), top.rend());
                LOG_INFO("profile", "%s: %llu samples", name, static_cast<unsigned long long>(total));
                for (size_t i = 0; i < top.size() && i < top_n; i++) LOG_INFO("profile", "  %5.1f%% %s", 100.0 * top[i].first / total, top[i].second.c_str());
                counts.clear();
                total = 0;
            }
        }
    }).detach();
}

void log_here(const char* tag) {
    void* pcs[16];
    int n = stack_from(nullptr, pcs, 16);
    LOG_INFO("watch", "stack at %s", tag);
    LOG_INFO("watch", "  %s", stack_string(pcs, n).c_str());
}

void rearm_watch() {
    for (auto& [page, prot] : g_trap_pages) host::vm_protect(vm::g_base + (uint64_t(page) << 12), 4096, prot);
}

void arm_watch() {
    const char* env = std::getenv("GOWA_TRAP");
    if (!env) env = std::getenv("GOWA_WATCH");
    if (!env || g_watch_count) return;
    if (const char* m = std::getenv("GOWA_TRAP_MAX")) g_trap_max = static_cast<uint32_t>(std::atoi(m));
    std::string list = env;
    for (char& ch : list) if (ch == ';') ch = ',';
    size_t pos = 0;
    while (pos < list.size()) {
        size_t end = list.find(',', pos);
        if (end == std::string::npos) end = list.size();
        std::string item = list.substr(pos, end - pos);
        pos = end + 1;
        if (item.empty()) continue;
        Trap t;
        if (item[0] == 'r') {
            t.reads = true;
            item = item.substr(1);
        }
        size_t bang = item.find('!');
        if (bang != std::string::npos) {
            t.dump = item.substr(bang + 1) == "dump";
            item = item.substr(0, bang);
        }
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            t.has_val = true;
            t.val = static_cast<uint32_t>(std::strtoul(item.c_str() + eq + 1, nullptr, 16));
            item = item.substr(0, eq);
        }
        size_t plus = item.find('+');
        size_t dash = item.find('-');
        t.addr = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
        if (plus != std::string::npos) t.len = static_cast<uint32_t>(std::strtoul(item.c_str() + plus + 1, nullptr, 16));
        else if (dash != std::string::npos) t.len = static_cast<uint32_t>(std::strtoul(item.c_str() + dash + 1, nullptr, 16)) - t.addr;
        if (!t.len) t.len = 4;
        g_traps.push_back(t);
    }
    for (size_t i = 0; i < g_traps.size(); i++) {
        const Trap& t = g_traps[i];
        for (uint64_t p = t.addr >> 12; p <= (uint64_t(t.addr) + t.len - 1) >> 12; p++) {
            auto [it, fresh] = g_trap_pages.try_emplace(static_cast<uint32_t>(p), host::Prot::Read);
            if (t.reads) it->second = host::Prot::None;
        }
        /* Y1: arm-time value, so the state at the first guest instruction is a
         * measurement and not an inference. The loader has already run (and
         * zero-filled LOAD1's BSS), so this is what the guest inherits. */
        char armed[96];
        int o = 0;
        for (uint32_t a = t.addr, k = 0; k < 8 && a + 4 <= t.addr + t.len; a += 4, k++)
            o += std::snprintf(armed + o, sizeof armed - o, " %08x", vm::range_mapped(a, 4) ? vm::read32(a) : 0);
        LOG_INFO("trap", "trap %zu: %s 0x%08x+0x%x%s%s arm-time words:%s", i, t.reads ? "read/write" : "write", t.addr, t.len, t.has_val ? " value-match" : "", t.dump ? " dump" : "", armed);
    }
    g_watch_count = static_cast<int>(g_traps.size());
    /* The number of mprotect faults that were the watch's own read of a watched
     * word is reported at the same place the watch is armed, so a run states it
     * instead of leaving it to be inferred from a stack trace. */
    LOG_INFO("trap", "self-read guard armed: a fault from this thread's own block-watch read is counted, not reported as a guest access");
    /* The count is only useful if the run says it. The title ends through
     * _sys_process_exit, so main()'s summary never runs; an atexit hook does. */
    std::atexit([] {
        uint64_t total = 0;
        for (size_t i = 0; i < g_traps.size(); i++) total += g_traps[i].hits;
        LOG_INFO("trap", "watch totals: %llu guest events, %llu self reads dropped (%zu traps)", static_cast<unsigned long long>(total),
                 static_cast<unsigned long long>(g_self_reads.load()), g_traps.size());
    });
    rearm_watch();
}

}
#endif

#endif  // !__wasm__
