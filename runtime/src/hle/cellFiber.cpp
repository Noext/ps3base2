/* cellFiber: PPU fibres, implemented in HLE on real host fibres.
 *
 * WHY THIS IS NOT LLE ANY MORE, with the measurement that forced it:
 *
 * libfiber.sprx is lifted and loads LLE. Every cellFiber import ran LLE and the
 * title reached its own save-prompt screen, and then at 39.5 s it faulted:
 *
 *   crash: signal 11 ... fault 0x...010 (guest 0x10) thread main_thread
 *   crash:   cellFiber import nesting depth on this thread: 2
 *   crash:   guest lr 0x0 r1 0xd0032d30
 *
 * Instrumenting every cellFiber import and keeping the last 512 events in a ring
 * (runtime/src/fibertrace.cpp) named the sequence, verbatim from the log:
 *
 *   t=39.514803 ENTER  cellFiberPpuCreateFiber  r1=0xd0025140 r3=0x165d580 lr=0x0053334c
 *   t=39.514902 LEAVE  cellFiberPpuCreateFiber  r1=0xd0025140 r3=0x0
 *   t=39.514903 ENTER  cellFiberPpuRunFibers    r1=0xd00252e0 r3=0x165d580 lr=0x00533498
 *   t=39.514955 ENTER  cellFiberPpuYield        r1=0xd0032d60 r3=0x1000000720bcbe0
 *   ... SIGSEGV at t=39.537142 with r1=0xd0032d30
 *
 * RunFibers has no LEAVE, nesting depth is 2 (RunFibers + the Yield inside it),
 * and r1 moved off the thread's stack (0xd004f440 on all 24 439 preceding
 * yields) onto 0xd0032d60. CreateFiber received eaStack=0xd0025490 and
 * sizeStack=0xdc70, so the fibre stack spans 0xd0025490..0xd0033100: BOTH r1
 * values are inside it, 0x3d0 and 0x3a0 below its top.
 *
 * So a fibre switch genuinely precedes the crash, and the crash executes on the
 * fibre's own guest stack. The faulting instruction is libfiber's own back-chain
 * epilogue -- ld64(base, gpr[9] + 16) with gpr[9] = 0, where gpr[9] was loaded by
 * ld64(base, gpr[1] + 0), i.e. the OUTERMOST frame of the fibre has no back
 * chain. That frame is laid down by libfiber's context initialiser, which is the
 * code being replaced here: an HLE fibre calls the entry with a back chain that
 * points at itself, so the epilogue is well defined.
 *
 * ABI: field order, sizes, error codes and return-value behaviour are taken from
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellFiber.{h,cpp} and
 * refs/jdc-ps3recomp/libs/spurs/cellFiber.{h,c} -- not from memory.
 */
#include "common.h"
#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "runtime/thread.h"

#include <ucontext.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace hle {

namespace {

/* refs/rpcs3/.../cellFiber.h:6-19. The SPURS base is NOT used here: cellFiber
 * has its own 0x807600xx base. */
constexpr int32_t FIBER_ERROR(int32_t n) { return static_cast<int32_t>(0x80760000u | static_cast<uint32_t>(n)); }
constexpr int32_t CELL_FIBER_ERROR_AGAIN = FIBER_ERROR(0x01);
constexpr int32_t CELL_FIBER_ERROR_INVAL = FIBER_ERROR(0x02);
constexpr int32_t CELL_FIBER_ERROR_NOMEM = FIBER_ERROR(0x04);
constexpr int32_t CELL_FIBER_ERROR_DEADLK = FIBER_ERROR(0x08);
constexpr int32_t CELL_FIBER_ERROR_PERM = FIBER_ERROR(0x09);
constexpr int32_t CELL_FIBER_ERROR_BUSY = FIBER_ERROR(0x0A);
constexpr int32_t CELL_FIBER_ERROR_ABORT = FIBER_ERROR(0x0C);
constexpr int32_t CELL_FIBER_ERROR_STAT = FIBER_ERROR(0x0F);
constexpr int32_t CELL_FIBER_ERROR_ALIGN = FIBER_ERROR(0x10);
constexpr int32_t CELL_FIBER_ERROR_NULL_POINTER = FIBER_ERROR(0x11);
constexpr int32_t CELL_FIBER_ERROR_NOSYSINIT = FIBER_ERROR(0x20);

/* refs/rpcs3/.../cellFiber.h:31-116, the guest-visible sizes. They are the REAL
 * guest layouts because the title allocates these objects itself. */
constexpr uint32_t SCHEDULER_SIZE = 512;
constexpr uint32_t SCHEDULER_ALIGN = 128;
constexpr uint32_t SCHEDULER_ATTR_SIZE = 256;
constexpr uint32_t FIBER_SIZE = 896;
constexpr uint32_t FIBER_ATTR_SIZE = 256;
constexpr uint32_t CONTEXT_SIZE = 640;
constexpr uint32_t CONTEXT_ALIGN = 16;
constexpr uint32_t CONTEXT_ATTR_SIZE = 128;

/* refs/rpcs3/.../cellFiber.cpp:36-40 -- the scheduler attribute's own fields.
 * Everything else in the 256 bytes stays zero. */
constexpr uint32_t SCHED_ATTR_FLAGS = 0x10;
constexpr uint32_t SCHED_ATTR_INTERVAL = 0x14;

enum class State : uint32_t { Free = 0, Ready, Running, Suspended, Terminated };

struct Fiber {
    State state = State::Free;
    uint32_t entry = 0;      /* guest function pointer (a code address, not an OPD) */
    uint64_t arg = 0;
    uint32_t stack_lo = 0;   /* the guest stack the TITLE supplied in CreateFiber */
    uint32_t stack_size = 0;
    uint32_t guest_fiber = 0;/* the guest CellFiberPpu object */
    uint32_t priority = 0;
    uint32_t exit_status = 0;/* what the entry returned; JoinFiber reports it */
    char name[32] = {};

    /* The host side. One host stack and one host context per fibre, because a
     * fibre has to be able to yield from the middle of a guest call chain and
     * come back into it: only a real context switch preserves the C++ frames
     * between the import and the guest entry. */
    ucontext_t host_ctx{};
    void* host_stack = nullptr;
    ppu::Context guest_ctx{};  /* the guest registers while this fibre is not running */
};

struct ThreadState {
    bool initialized = false;
    bool in_fiber = false;
    int current = -1;
    std::vector<Fiber> fibers;   /* index == guest CellFiberPpu handle, stable */
    ucontext_t scheduler_ctx{};
    ppu::Context* live_ctx = nullptr;  /* the thread's ctx, so we can save and restore */
    uint32_t scheduler_ea = 0;
};

/* One set of fibres per guest thread. A fibre belongs to the thread that created
 * it -- the console's fibres are per-PPU-thread cooperatively scheduled, and a
 * process-wide table would let one thread resume another's fibre. */
std::mutex g_lock;
std::vector<std::pair<uint32_t, ThreadState>> g_threads;

ThreadState* state_for(uint32_t tid) {
    for (auto& [id, st] : g_threads)
        if (id == tid) return &st;
    g_threads.emplace_back(tid, ThreadState{});
    return &g_threads.back().second;
}

ThreadState* state_for_self() {
    guest::Thread* t = guest::current();
    if (!t) return nullptr;
    std::lock_guard lk(g_lock);
    return state_for(t->id);
}

constexpr uint32_t kFiberStackReserve = 0x1000;  /* the self-referential back chain */

void fiber_trampoline();

/* makecontext takes a void(*)(void); the real entry is the same function. */
void fiber_to_trampoline() { fiber_trampoline(); }

/* Point the guest's r1 at the top of the fibre's own stack with a back chain
 * that is a real, readable address. This is the whole fix: libfiber's lifted
 * epilogue does ld64(base, gpr[9] + 16) where gpr[9] came from the outermost
 * frame's back-chain word, and a zero there is the fault at guest 0x10. A fibre
 * has no caller, so the chain points at itself and the read stays in its stack. */
void prepare_guest_stack(Fiber& f, ppu::Context& c) {
    const uint32_t top = (f.stack_lo + f.stack_size) & ~0xFull;
    const uint32_t sp = top - 0x100;
    c.gpr[1] = sp;
    vm::write64(sp, sp);   /* back chain: a valid pointer, not 0 */
    c.lr = 0;
}

void resume_guest(Fiber& f) {
    /* The guest registers live in the thread's own ppu::Context, and every
     * lifted frame holds a reference to that one object. So a switch saves and
     * restores that object; the host context is what preserves the C++ frames. */
    ThreadState* st = state_for_self();
    if (st && st->live_ctx) *st->live_ctx = f.guest_ctx;
}

void suspend_guest(Fiber& f) {
    ThreadState* st = state_for_self();
    if (st && st->live_ctx) f.guest_ctx = *st->live_ctx;
}

void fiber_trampoline() {
    ThreadState* st = state_for_self();
    if (!st || st->current < 0) return;
    Fiber& f = st->fibers[static_cast<size_t>(st->current)];
    f.state = State::Running;
    if (st->live_ctx) {
        prepare_guest_stack(f, *st->live_ctx);
        st->live_ctx->gpr[3] = f.arg;
        /* The entry is an OPD, NOT a code address. Measured: the title passes
         * entry=0x01385418, whose two words are 0x00b5294c 0x0139ebb0 -- code and
         * TOC -- and calling it as code aborted with
         *   ppu: unknown indirect target 0x01385418 -- (no lifted code)
         * Both call_opd and the OPD layout are the established ones here. */
        guest::call_opd(*st->live_ctx, vm::g_base, f.entry);
        f.exit_status = static_cast<uint32_t>(st->live_ctx->gpr[3]);
    }
    f.state = State::Terminated;
    st->in_fiber = false;
    st->current = -1;
    swapcontext(&f.host_ctx, &st->scheduler_ctx);
}

bool hle_enabled() {
    /* Default ON: the LLE path was measured to fault on the fibre's own frame at
     * 39.5 s (see the header). GOWA_FIBER_HLE=0 restores LLE libfiber, which is
     * what an A/B needs. */
    static const bool on = [] {
        const char* e = std::getenv("GOWA_FIBER_HLE");
        return !(e && *e && std::string(e) == "0");
    }();
    return on;
}

/* The guest address of a CellFiberPpu handle is the INDEX of the fibre: the
 * title never dereferences the CellFiberPpu object itself (it is 896 opaque
 * bytes, refs/rpcs3/.../cellFiber.h:53-58), so the handle is our own token. */
bool valid_handle(const ThreadState& st, uint32_t h) { return h < st.fibers.size(); }

}  // namespace

/* ------------------------------------------------------------------------- */

HLE_FUNC(cellFiber, _cellFiberPpuInitialize) {
    ThreadState* st = state_for_self();
    if (!st) return ret(ctx, CELL_FIBER_ERROR_NOSYSINIT);
    st->initialized = true;
    st->live_ctx = &ctx;
    LOG_INFO("cellFiber", "HLE _cellFiberPpuInitialize -> CELL_OK");
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, _cellFiberPpuSchedulerAttributeInitialize) {
    const uint32_t attr = a32(ctx, 0);
    if (!attr) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    /* The ALIGNMENT is the ATTRIBUTE's own (alignas(8), refs/rpcs3/.../cellFiber.h:33),
     * not the scheduler's 128. Checking 128 here rejected the title's real call:
     * it passes attr=0xd004f950, which is 8-aligned and not 128-aligned, and the
     * title reacted to 0x80760010 with "[FATAL] ... failed" and process_exit(1). */
    if (attr & 7) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    if (!vm::range_mapped(attr, SCHEDULER_ATTR_SIZE)) return ret(ctx, CELL_FIBER_ERROR_PERM);
    for (uint32_t o = 0; o < SCHEDULER_ATTR_SIZE; o += 8) vm::write64(attr + o, 0);
    /* refs/rpcs3/.../cellFiber.cpp:52-58: flags false, interval 0. */
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, _cellFiberPpuAttributeInitialize) {
    const uint32_t attr = a32(ctx, 0);
    if (!attr) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (attr & 7) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    if (!vm::range_mapped(attr, FIBER_ATTR_SIZE)) return ret(ctx, CELL_FIBER_ERROR_PERM);
    for (uint32_t o = 0; o < FIBER_ATTR_SIZE; o += 8) vm::write64(attr + o, 0);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, _cellFiberPpuContextAttributeInitialize) {
    const uint32_t attr = a32(ctx, 0);
    if (!attr) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (attr & 7) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    if (!vm::range_mapped(attr, CONTEXT_ATTR_SIZE)) return ret(ctx, CELL_FIBER_ERROR_PERM);
    for (uint32_t o = 0; o < CONTEXT_ATTR_SIZE; o += 8) vm::write64(attr + o, 0);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuInitializeScheduler) {
    const uint32_t scheduler = a32(ctx, 0);
    if (!scheduler) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (scheduler & (SCHEDULER_ALIGN - 1)) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    if (!vm::range_mapped(scheduler, SCHEDULER_SIZE)) return ret(ctx, CELL_FIBER_ERROR_PERM);
    ThreadState* st = state_for_self();
    if (!st) return ret(ctx, CELL_FIBER_ERROR_NOSYSINIT);
    st->scheduler_ea = scheduler;
    st->live_ctx = &ctx;
    LOG_INFO("cellFiber", "HLE InitializeScheduler(0x%08x) -> CELL_OK", scheduler);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuFinalizeScheduler) {
    const uint32_t scheduler = a32(ctx, 0);
    if (!scheduler) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (scheduler & (SCHEDULER_ALIGN - 1)) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    ret(ctx, 0);
}

/* cellFiberPpuCreateFiber(scheduler, fiber, entry, arg, priority, eaStack,
 * sizeStack, attr) -- refs/rpcs3/.../cellFiber.cpp:156. The argument order was
 * checked against the measured call, not assumed:
 *
 *   cellFiberPpuCreateFiber(0x165d580, 0xd0033100, 0x1385418, 0x73041300, 0x0,
 *                           0xd0025490, 0xdc70, 0xd00251b0)
 *   scheduler    fiber        entry      arg        prio eaStack    size  attr
 */
HLE_FUNC(cellFiber, cellFiberPpuCreateFiber) {
    const uint32_t scheduler = a32(ctx, 0);
    const uint32_t fiber_ea = a32(ctx, 1);
    const uint32_t entry = a32(ctx, 2);
    const uint64_t arg = a64(ctx, 3);
    const uint32_t priority = a32(ctx, 4);
    const uint32_t ea_stack = a32(ctx, 5);
    const uint32_t size_stack = a32(ctx, 6);
    if (!scheduler || !fiber_ea || !entry || !ea_stack) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if ((scheduler & (SCHEDULER_ALIGN - 1)) || (fiber_ea & (SCHEDULER_ALIGN - 1))) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    if (priority > 3) return ret(ctx, CELL_FIBER_ERROR_INVAL);
    if (!vm::range_mapped(ea_stack, size_stack)) {
        LOG_WARN("cellFiber", "CreateFiber: guest stack 0x%08x+0x%x is not mapped", ea_stack, size_stack);
        return ret(ctx, CELL_FIBER_ERROR_PERM);
    }
    if (size_stack < 0x1000) return ret(ctx, CELL_FIBER_ERROR_NOMEM);

    ThreadState* st = state_for_self();
    if (!st) return ret(ctx, CELL_FIBER_ERROR_NOSYSINIT);
    std::lock_guard lk(g_lock);

    uint32_t handle = 0;
    while (handle < st->fibers.size() && st->fibers[handle].state != State::Free) handle++;
    if (handle == st->fibers.size()) st->fibers.emplace_back();
    Fiber& f = st->fibers[handle];
    f = Fiber{};
    f.state = State::Ready;
    f.entry = entry;
    f.arg = arg;
    f.stack_lo = ea_stack;
    f.stack_size = size_stack;
    f.guest_fiber = fiber_ea;
    f.priority = priority;

    f.host_stack = std::malloc(256 * 1024);
    if (!f.host_stack) return ret(ctx, CELL_FIBER_ERROR_NOMEM);
    if (getcontext(&f.host_ctx) != 0) {
        std::free(f.host_stack);
        f.host_stack = nullptr;
        return ret(ctx, CELL_FIBER_ERROR_NOMEM);
    }
    f.host_ctx.uc_stack.ss_sp = f.host_stack;
    f.host_ctx.uc_stack.ss_size = 256 * 1024;
    f.host_ctx.uc_link = nullptr;
    /* The trampoline takes no arguments: it reads st->current, which RunFibers
     * sets immediately before switching. Passing the index through makecontext
     * would need it split into ints and is one more place to get wrong. */
    makecontext(&f.host_ctx, fiber_to_trampoline, 0);

    vm::write32(fiber_ea, handle);
    LOG_INFO("cellFiber", "HLE CreateFiber #%u entry=0x%08x arg=0x%llx stack=0x%08x+0x%x prio=%u -> CELL_OK",
             handle, entry, static_cast<unsigned long long>(arg), ea_stack, size_stack, priority);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuDeleteFiber) {
    LOG_WARN("cellFiber", "DeleteFiber is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

/* cellFiberPpuJoinFiber(fiber, status) -- refs/rpcs3/.../cellFiber.cpp:202.
 *
 * MEASURED why this cannot refuse: with RunFibers running the fibre for real,
 * the title's next call is JoinFiber, and the title treats an error as fatal:
 *
 *   cellFiber: HLE CreateFiber #0 ... -> CELL_OK
 *   cellFiber: HLE RunFibers(0x0165d580) ran 932 fibre switch(es) -> CELL_OK
 *   tty: [FATAL] cellFiberPpuJoinFiber(g_Fibers->m_Fiber, &exitCode) failed, code 80760002
 *   process: _sys_process_exit(0x1)
 *
 * So JoinFiber is on the path this game actually walks. RPCS3 validates and
 * returns CELL_OK; the console's fibre has finished by the time the scheduler
 * returns (RunFibers above ran it to completion), so the join is a status
 * report and not a wait. Same shape here: validate, write the exit status,
 * return CELL_OK. */
HLE_FUNC(cellFiber, cellFiberPpuJoinFiber) {
    const uint32_t fiber_ea = a32(ctx, 0);
    const uint32_t status_ea = a32(ctx, 1);
    if (!fiber_ea || !status_ea) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (fiber_ea & (SCHEDULER_ALIGN - 1)) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    ThreadState* st = state_for_self();
    if (st) {
        std::lock_guard lk(g_lock);
        /* The handle is what CreateFiber wrote into the CellFiberPpu object. */
        const uint32_t handle = vm::range_mapped(fiber_ea, 4) ? vm::read32(fiber_ea) : 0;
        if (valid_handle(*st, handle) && st->fibers[handle].state != State::Free) {
            const Fiber& f = st->fibers[handle];
            /* A fibre that has not finished is not joinable yet; the console
             * would block, but this title has already run it to completion. */
            if (f.state != State::Terminated) {
                LOG_WARN("cellFiber", "JoinFiber #%u: still in state %u (not yet terminated)", handle,
                         static_cast<unsigned>(f.state));
            }
            if (vm::range_mapped(status_ea, 4)) vm::write32(status_ea, f.exit_status);
            LOG_INFO("cellFiber", "HLE JoinFiber #%u -> status 0x%x, CELL_OK", handle, f.exit_status);
            return ret(ctx, 0);
        }
    }
    if (vm::range_mapped(status_ea, 4)) vm::write32(status_ea, 0);
    LOG_WARN("cellFiber", "JoinFiber on an unknown handle (fiber=0x%08x)", fiber_ea);
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

/* cellFiberPpuYield: inside a fibre, give the CPU back to the scheduler; outside
 * one, PERM. The PERM arm is not a guess -- it is what LLE libfiber returned on
 * all 24 439 calls the title made before any fibre existed, so the title already
 * handles it. */
HLE_FUNC(cellFiber, cellFiberPpuYield) {
    ThreadState* st = state_for_self();
    if (!st || !st->initialized || st->current < 0) return ret(ctx, CELL_FIBER_ERROR_PERM);
    Fiber& f = st->fibers[static_cast<size_t>(st->current)];
    f.state = State::Suspended;
    suspend_guest(f);
    const int prev = st->current;
    st->current = -1;
    st->in_fiber = false;
    swapcontext(&f.host_ctx, &st->scheduler_ctx);
    /* Resumed: our guest registers are back in the thread's context. */
    st->current = prev;
    st->in_fiber = true;
    f.state = State::Running;
    ret(ctx, 0);
}

/* cellFiberPpuRunFibers(scheduler): run every Ready fibre to completion or to a
 * yield, round-robin, and return when none is runnable. */
HLE_FUNC(cellFiber, cellFiberPpuRunFibers) {
    const uint32_t scheduler = a32(ctx, 0);
    if (!scheduler) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (scheduler & (SCHEDULER_ALIGN - 1)) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    ThreadState* st = state_for_self();
    if (!st) return ret(ctx, CELL_FIBER_ERROR_NOSYSINIT);
    st->initialized = true;
    st->live_ctx = &ctx;

    uint32_t ran = 0;
    for (;;) {
        int pick = -1;
        {
            std::lock_guard lk(g_lock);
            for (size_t i = 0; i < st->fibers.size(); i++) {
                if (st->fibers[i].state == State::Ready || st->fibers[i].state == State::Suspended) {
                    pick = static_cast<int>(i);
                    break;
                }
            }
        }
        if (pick < 0) break;
        Fiber& f = st->fibers[static_cast<size_t>(pick)];
        f.state = State::Running;
        st->current = pick;
        st->in_fiber = true;
        f.guest_ctx = ctx;          /* the scheduler's registers are the fibre's start */
        swapcontext(&st->scheduler_ctx, &f.host_ctx);
        st->current = -1;
        st->in_fiber = false;
        /* A terminated fibre stops being runnable; a suspended one is retried on
         * the next pass, which is what a round-robin scheduler does. */
        if (f.state != State::Terminated) f.state = State::Suspended;
        ran++;
        if (ran > 100000) {
            LOG_WARN("cellFiber", "RunFibers: 100000 switches without finishing; stopping");
            break;
        }
    }
    LOG_INFO("cellFiber", "HLE RunFibers(0x%08x) ran %u fibre switch(es) -> CELL_OK", scheduler, ran);
    ret(ctx, 0);
}

/* The remaining exports answer the console's refusals rather than a fake success
 * (AGENTS.md, "Never a fake success"): they are named in the log and return the
 * error the state implies. */
HLE_FUNC(cellFiber, cellFiberPpuExit) {
    ThreadState* st = state_for_self();
    if (!st || st->current < 0) return ret(ctx, CELL_FIBER_ERROR_PERM);
    Fiber& f = st->fibers[static_cast<size_t>(st->current)];
    f.state = State::Terminated;
    st->current = -1;
    st->in_fiber = false;
    swapcontext(&f.host_ctx, &st->scheduler_ctx);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuSelf) {
    ret(ctx, 0);   /* "zero for simple PPU thread" -- refs/rpcs3/.../cellFiber.cpp:221 */
}

HLE_FUNC(cellFiber, cellFiberPpuGetScheduler) {
    ThreadState* st = state_for_self();
    const uint32_t fiber_ea = a32(ctx, 0);
    const uint32_t out = a32(ctx, 1);
    if (!fiber_ea || !out) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (!vm::range_mapped(out, 4)) return ret(ctx, CELL_FIBER_ERROR_PERM);
    vm::write32(out, st ? st->scheduler_ea : 0);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuSetPriority) {
    const uint32_t priority = a32(ctx, 0);
    if (priority > 3) return ret(ctx, CELL_FIBER_ERROR_INVAL);
    ret(ctx, 0);
}

HLE_FUNC(cellFiber, cellFiberPpuCheckStackLimit) { ret(ctx, 0); }

HLE_FUNC(cellFiber, cellFiberPpuSendSignal) { ret(ctx, 0); }
HLE_FUNC(cellFiber, cellFiberPpuWaitSignal) { ret(ctx, 0); }

HLE_FUNC(cellFiber, cellFiberPpuWaitFlag) {
    const uint32_t flag = a32(ctx, 0);
    if (!flag) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    ret(ctx, 0);
}

/* The context-scheduler surface. These are the entries the title never called in
 * the measured run (the census in RESULTS-P3.md lists exactly four imports), so
 * rather than a guess they refuse explicitly. */
HLE_FUNC(cellFiber, cellFiberPpuContextInitialize) {
    LOG_WARN("cellFiber", "ContextInitialize is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextFinalize) {
    LOG_WARN("cellFiber", "ContextFinalize is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextRun) {
    LOG_WARN("cellFiber", "ContextRun is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextSwitch) {
    LOG_WARN("cellFiber", "ContextSwitch is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextSelf) { ret(ctx, 0); }
HLE_FUNC(cellFiber, cellFiberPpuContextReturnToThread) { ret(ctx, 0); }
HLE_FUNC(cellFiber, cellFiberPpuContextCheckStackLimit) { ret(ctx, 0); }
HLE_FUNC(cellFiber, cellFiberPpuContextRunScheduler) {
    LOG_WARN("cellFiber", "ContextRunScheduler is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextEnterScheduler) {
    LOG_WARN("cellFiber", "ContextEnterScheduler is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

static ModuleOverride g_fiber_override("libfiber.sprx", hle_enabled);

}  // namespace hle
