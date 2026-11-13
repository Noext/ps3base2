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

/* Run one round-robin pass of the scheduler: run every Ready or Suspended
 * fibre until it yields or terminates. Returns the number of switches.
 *
 * This is the body RunFibers and JoinFiber share. The real library's JoinFiber
 * does exactly this while it waits (see below), so the two must not drift. */
static uint32_t run_runnable_fibers(ThreadState* st, ppu::Context& ctx, bool& stopped_at_guard) {
    uint32_t ran = 0;
    stopped_at_guard = false;
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
        /* The SCHEDULER's registers must survive the switch. MEASURED: without
         * this save/restore the title carried on after RunFibers with
         * r1=0xd0033070, i.e. still on the FIBRE's stack, and faulted at guest
         * 0x400 in its own code. */
        const ppu::Context scheduler_ctx = ctx;
        f.guest_ctx = ctx;
        swapcontext(&st->scheduler_ctx, &f.host_ctx);
        ctx = scheduler_ctx;
        st->current = -1;
        st->in_fiber = false;
        if (f.state != State::Terminated) f.state = State::Suspended;
        ran++;
        if (ran > 100000) {
            stopped_at_guard = true;
            break;
        }
    }
    return ran;
}

/* cellFiberPpuJoinFiber(fiber, status) -- the real library's behaviour, read
 * from the LIFTED code, not from memory.
 *
 * The export's stub is 0x3600e940, an .opd whose first word is the code address
 * 0x36003574 (out-rdr/ppu/libfiber_000.cpp, f_36003574_body). That body is not a
 * validate-and-return: it polls the fibre's state word at +772 in a loop
 * (L_36003704), and only when that word says terminated does it read the exit
 * status from +792 and store it to the caller's *status:
 *
 *     L_36003704:
 *         lwz   r0, 772(r30)        ; the fibre's state
 *         cmpwi r0, 0
 *         beq   L_36003a34          ; terminated -> fall out of the loop
 *         ...
 *         b     L_36003704          ; otherwise run and poll again
 *     L_36003854:
 *         lwz   r0, 792(r30)        ; the real exit status
 *         stw   r0, 0(r31)          ; *status = it
 *         stw   r9, 0(r30)          ; clear the fibre's state
 *
 * So the console BLOCKS here, running the other fibres while it waits. Our
 * previous version returned CELL_OK immediately with f.exit_status even when the
 * fibre had never run -- the log said so 20 times in three 900 s runs
 * ("JoinFiber #N: still in state 3 (not yet terminated)"), and the title then
 * proceeded as if the work were done.
 *
 * The wait is bounded by the same guard RunFibers uses, so a fibre that never
 * terminates cannot hang the process; when the guard trips we say so and return
 * the status we have, which is the honest answer at that point. */
HLE_FUNC(cellFiber, cellFiberPpuJoinFiber) {
    const uint32_t fiber_ea = a32(ctx, 0);
    const uint32_t status_ea = a32(ctx, 1);
    if (!fiber_ea || !status_ea) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    if (fiber_ea & (SCHEDULER_ALIGN - 1)) return ret(ctx, CELL_FIBER_ERROR_ALIGN);
    ThreadState* st = state_for_self();
    if (st) {
        uint32_t handle = 0;
        {
            std::lock_guard lk(g_lock);
            handle = vm::range_mapped(fiber_ea, 4) ? vm::read32(fiber_ea) : 0;
            if (!valid_handle(*st, handle) || st->fibers[handle].state == State::Free) handle = 0xFFFFFFFFu;
        }
        if (handle != 0xFFFFFFFFu) {
            /* Wait the way the real code waits: run the other fibres, re-check. */
            uint32_t passes = 0;
            bool guard = false;
            for (;;) {
                {
                    std::lock_guard lk(g_lock);
                    if (st->fibers[handle].state == State::Terminated) break;
                }
                st->live_ctx = &ctx;
                run_runnable_fibers(st, ctx, guard);
                passes++;
                if (guard) {
                    LOG_WARN("cellFiber",
                             "JoinFiber #%u: the fibre did not terminate after %u passes; "
                             "returning the status it has", handle, passes);
                    break;
                }
                bool any_runnable = false;
                {
                    std::lock_guard lk(g_lock);
                    for (const Fiber& f : st->fibers) {
                        if (f.state == State::Ready || f.state == State::Suspended) { any_runnable = true; break; }
                    }
                }
                /* Nothing left that could ever terminate it: stop rather than spin. */
                if (!any_runnable) break;
            }
            uint32_t status = 0;
            {
                std::lock_guard lk(g_lock);
                status = st->fibers[handle].exit_status;
            }
            if (vm::range_mapped(status_ea, 4)) vm::write32(status_ea, status);
            LOG_INFO("cellFiber", "HLE JoinFiber #%u -> status 0x%x after %u pass(es), CELL_OK",
                     handle, status, passes);
            return ret(ctx, 0);
        }
    }
    if (vm::range_mapped(status_ea, 4)) vm::write32(status_ea, 0);
    LOG_WARN("cellFiber", "JoinFiber on an unknown handle (fiber=0x%08x)", fiber_ea);
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

/* cellFiberPpuYield: inside a fibre, give the CPU back to the scheduler.
 *
 * The two refusals are DIFFERENT and the real library distinguishes them. Read
 * from the lifted code (stub 0x3600e930 -> code 0x360033dc,
 * out-rdr/ppu/libfiber_000.cpp f_360033dc_body):
 *
 *     lwz  r11, -32712(r2)      ; the per-thread fibre context
 *     lwz  r9,  0(r11)          ; its current fibre, -1 when there is none
 *     cmpwi r9, -1
 *     addi r0, r10, 32
 *     beq  ...                  ; -> returns 0x80760020 NOSYSINIT
 *     ...
 *     li   r3, 0x80760009       ; PERM, when the derived pointer is zero
 *
 * So "no scheduler on this thread" is NOSYSINIT (0x80760020) and "scheduler, but
 * this thread is not in a fibre" is PERM (0x80760009). Our previous version
 * returned PERM for both, which is a false report of state: the title cannot
 * tell a torn-down scheduler from a plain yield outside a fibre.
 *
 * The PERM arm is not a guess about the title's expectations -- it is what LLE
 * libfiber returned on all 24 439 calls the title made before any fibre
 * existed, so the title already handles it. */
HLE_FUNC(cellFiber, cellFiberPpuYield) {
    ThreadState* st = state_for_self();
    if (!st || !st->initialized) return ret(ctx, CELL_FIBER_ERROR_NOSYSINIT);
    if (st->current < 0) return ret(ctx, CELL_FIBER_ERROR_PERM);
    Fiber& f = st->fibers[static_cast<size_t>(st->current)];
    f.state = State::Suspended;
    suspend_guest(f);
    const int prev = st->current;
    st->current = -1;
    st->in_fiber = false;
    /* The fibre's registers are saved in f.guest_ctx; the scheduler resumes on
     * its own. Restore our own registers on the way back in, exactly as
     * RunFibers does, so the scheduler's r1 is its own. */
    const ppu::Context fibre_ctx = ctx;
    swapcontext(&f.host_ctx, &st->scheduler_ctx);
    ctx = fibre_ctx;
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

    bool guard = false;
    const uint32_t ran = run_runnable_fibers(st, ctx, guard);
    if (guard) {
        LOG_WARN("cellFiber", "RunFibers: 100000 switches without finishing; stopping");
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

/* These four used to be a bare `ret(ctx, 0)`, which is the fake success
 * AGENTS.md forbids: CELL_OK from something that did not run tells the caller its
 * state was created when it was not. RDR does not call any of them in the three
 * measured 900 s runs, so an explicit refusal costs nothing and is honest. */
HLE_FUNC(cellFiber, cellFiberPpuCheckStackLimit) {
    LOG_WARN("cellFiber", "CheckStackLimit is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

HLE_FUNC(cellFiber, cellFiberPpuSendSignal) {
    LOG_WARN("cellFiber", "SendSignal is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuWaitSignal) {
    LOG_WARN("cellFiber", "WaitSignal is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}

HLE_FUNC(cellFiber, cellFiberPpuWaitFlag) {
    const uint32_t flag = a32(ctx, 0);
    if (!flag) return ret(ctx, CELL_FIBER_ERROR_NULL_POINTER);
    LOG_WARN("cellFiber", "WaitFlag is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
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
HLE_FUNC(cellFiber, cellFiberPpuContextSelf) {
    LOG_WARN("cellFiber", "ContextSelf is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextReturnToThread) {
    LOG_WARN("cellFiber", "ContextReturnToThread is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
HLE_FUNC(cellFiber, cellFiberPpuContextCheckStackLimit) {
    LOG_WARN("cellFiber", "ContextCheckStackLimit is not implemented");
    ret(ctx, CELL_FIBER_ERROR_INVAL);
}
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
