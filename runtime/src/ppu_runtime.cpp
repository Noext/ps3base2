#include "ppu/ops.h"
#include "runtime/critpath.h"
#include "runtime/hle.h"
#include "runtime/image.h"
#include "runtime/kernel.h"
#include "runtime/loader.h"
#include "runtime/log.h"
#include "runtime/cbwatch.h"
#include "runtime/force.h"
#include "runtime/trace.h"
#include <atomic>
#include "runtime/thread.h"
#include "runtime/memory.h"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "syscall_table.h"

namespace lv2 {

namespace {

struct SyscallSlot {
    const char* name = nullptr;
    SyscallFn fn = nullptr;
};

SyscallSlot* table() {
    static SyscallSlot slots[1024];
    return slots;
}

std::atomic<uint8_t> g_warned[1024];

}

SyscallReg::SyscallReg(uint32_t number, const char* name, SyscallFn fn) {
    table()[number & 1023] = {name, fn};
}

const char* syscall_name(uint32_t number) {
    const char* n = table()[number & 1023].name;
    return n ? n : "?";
}

const char* spec_name(uint32_t number) {
    /* The generated table is the LV2 specification: it lists every number the
     * console dispatches, with the name the specification gives it. An
     * unimplemented number still has a name here, which is what makes the
     * ENOSYS log actionable. */
    for (unsigned i = 0; i < k_syscall_spec_count; i++)
        if (k_syscall_spec[i].num == number) return k_syscall_spec[i].name;
    return nullptr;
}

}

namespace hle {

namespace {

std::map<std::string, Fn>& registry() {
    static std::map<std::string, Fn> r;
    return r;
}

}

Reg::Reg(const char* module, const char* name, Fn fn) {
    registry()[std::string(module) + "::" + name] = fn;
}

Fn find(const char* module, const char* name) {
    auto it = registry().find(std::string(module) + "::" + name);
    return it == registry().end() ? nullptr : it->second;
}

namespace {

std::map<std::string, Tap>& taps() {
    static std::map<std::string, Tap> r;
    return r;
}

}

TapReg::TapReg(const char* module, const char* name, Tap tap) {
    taps()[std::string(module) + "::" + name] = tap;
}

Tap find_tap(const char* module, const char* name) {
    auto it = taps().find(std::string(module) + "::" + name);
    return it == taps().end() ? nullptr : it->second;
}

}

namespace ppu {

namespace {

struct Range {
    uint32_t lo;
    uint32_t hi;
    std::vector<Func> table;
};

std::vector<Range> g_ranges;
std::once_flag g_dispatch_once;

void build_dispatch() {
    for (uint32_t i = 0; i < image::image_count; i++) {
        g_ranges.push_back({image::images[i].text_lo, image::images[i].text_hi, std::vector<Func>((image::images[i].text_hi - image::images[i].text_lo) / 4)});
    }
    for (uint32_t i = 0; i < image::function_count; i++) {
        uint32_t a = image::functions[i].addr;
        for (auto& r : g_ranges) {
            if (a >= r.lo && a < r.hi) {
                r.table[(a - r.lo) / 4] = image::functions[i].fn;
                break;
            }
        }
    }
}

Func lookup(uint32_t addr) {
    std::call_once(g_dispatch_once, build_dispatch);
    if (addr & 3) return nullptr;
    for (auto& r : g_ranges) {
        if (addr >= r.lo && addr < r.hi) return r.table[(addr - r.lo) / 4];
    }
    return nullptr;
}


enum : int32_t { kUnresolved = -3, kNone = -2, kHle = -1 };

struct ImportSlot {
    std::atomic<int32_t> state{kUnresolved};
    hle::Fn hle = nullptr;
    std::atomic<bool> warned{false};
    bool log_args = false;
    hle::Tap tap = nullptr;
};

std::vector<ImportSlot>& import_slots() {
    static std::vector<ImportSlot> v(image::import_count);
    return v;
}

int32_t resolve(uint32_t index, ImportSlot& slot) {
    const image::ImportEntry& imp = image::imports[index];
    if (const char* list = std::getenv("GOWA_IMPORT_ARGS")) {
        std::string l = std::string(",") + list + ",";
        if (imp.name && *imp.name && l.find(std::string(",") + imp.name + ",") != std::string::npos) slot.log_args = true;
    }
    if (imp.name && *imp.name) slot.tap = hle::find_tap(imp.module, imp.name);
    for (uint32_t i = 0; i < image::export_count; i++) {
        const image::ExportEntry& e = image::exports[i];
        if (e.nid == imp.nid && std::strcmp(e.module, imp.module) == 0 && loader::image_loaded(static_cast<int>(e.image))) {
            return static_cast<int32_t>(i);
        }
    }
    if (imp.name && *imp.name) {
        if (hle::Fn fn = hle::find(imp.module, imp.name)) {
            slot.hle = fn;
            return kHle;
        }
    }
    /* PS3BASE_RESOLVE_LOG: name the imports that resolve nowhere. A title that
     * faults through a bogus address usually has one of these behind it. */
    if (std::getenv("PS3BASE_RESOLVE_LOG")) {
        LOG_WARN("import", "unresolved %s::%s (0x%08x)", imp.module, *imp.name ? imp.name : "?", imp.nid);
    }
    return kNone;
}

}

bool has_code_at(uint32_t addr) { return lookup(addr) != nullptr; }

void call_indirect(Context& ctx, uint8_t* base, uint32_t addr) {
    force::tick();
    /* Log the vtable target and its owners just before dispatch from
     * f_0006ee9c (lr 0x6eec4): the target is what returns the decisive 0. */
    if (std::getenv("PS3BASE_VTARGET")) {
        const uint32_t lr0 = static_cast<uint32_t>(ctx.lr);
        if (lr0 == 0x749e8u || lr0 == 0x74a1cu || lr0 == 0x6eec4u)
            LOG_INFO("vchain", "lr=0x%08x -> ctr=0x%08x r3=0x%x r4=0x%x",
                     lr0, static_cast<uint32_t>(ctx.ctr),
                     static_cast<unsigned>(ctx.gpr[3]), static_cast<unsigned>(ctx.gpr[4]));
        /* The leaf f_00216258 reads one byte; capture its address and value. */
        if (lr0 == 0x749e8u) {
            const uint32_t obj = static_cast<uint32_t>(ctx.gpr[3]);
            const uint32_t ea = obj + 593u;
            LOG_INFO("vleaf", "obj=0x%08x ea=0x%08x mapped=%d value=0x%02x",
                     obj, ea, int(vm::range_mapped(ea, 1)),
                     vm::range_mapped(ea, 1) ? static_cast<unsigned>(vm::ptr(ea)[0]) : 0xff);
        }
    }
    if (std::getenv("PS3BASE_VTARGET") && static_cast<uint32_t>(ctx.lr) == 0x6eec4u) {
        const uint32_t g = vm::range_mapped(0xc00000u - 4596u, 4) ? vm::read32(0xc00000u - 4596u) : 0;
        const uint32_t o = g && vm::range_mapped(g, 4) ? vm::read32(g) : 0;
        const uint32_t s1 = o && vm::range_mapped(o + 72, 4) ? vm::read32(o + 72) : 0;
        LOG_INFO("vtarget", "global=0x%08x obj=0x%08x slot=0x%08x fn=0x%08x ctr=0x%08x r3=0x%x",
                 g, o, s1, addr, static_cast<uint32_t>(ctx.ctr),
                 static_cast<unsigned>(ctx.gpr[3]));
    }
    Func fn = lookup(addr);
    if (!fn) {
        LOG_FATAL("ppu", "unknown indirect target 0x%08x (lr=0x%08llx, thread 0x%x)", addr, static_cast<unsigned long long>(ctx.lr), ctx.thread_id);
    }
    force::on_call(addr);
    const uint32_t saved_lr = static_cast<uint32_t>(ctx.lr);
    fn(ctx, base);
    force::on_return(addr, saved_lr, ctx.gpr[3]);
}

void call_import_body(Context& ctx, uint8_t* base, uint32_t index, ImportSlot& slot, int32_t st, const image::ImportEntry& imp);

void call_import(Context& ctx, uint8_t* base, uint32_t index) {
    ImportSlot& slot = import_slots()[index];
    int32_t st = slot.state.load(std::memory_order_acquire);
    if (st == kUnresolved || st == kNone) {
        st = resolve(index, slot);
        slot.state.store(st, std::memory_order_release);
    }
    const image::ImportEntry& imp = image::imports[index];
    if (trace::on(trace::IMPORT)) {
        static std::atomic<uint32_t> names[16384];
        uint32_t id = names[index & 16383].load(std::memory_order_relaxed);
        if (!id) {
            id = trace::intern(std::string(imp.module) + "::" + (*imp.name ? imp.name : "?"));
            names[index & 16383].store(id, std::memory_order_relaxed);
        }
        uint64_t a0 = ctx.gpr[3], a1 = ctx.gpr[4], a2 = ctx.gpr[5];
        uint32_t lr = static_cast<uint32_t>(ctx.lr);
        uint64_t t0 = trace::now_ns();
        call_import_body(ctx, base, index, slot, st, imp);
        uint64_t us = (trace::now_ns() - t0) / 1000;
        trace::emit(trace::T_IMPORT, id, a0, a1, a2, ctx.gpr[3], (uint64_t(lr) << 32) | (us > 0xFFFFFFFFull ? 0xFFFFFFFFull : us));
        cbwatch::note_import(imp.module, *imp.name ? imp.name : "?", ctx.gpr[3], lr, st >= 0);
        return;
    }
    const uint32_t lr = static_cast<uint32_t>(ctx.lr);
    call_import_body(ctx, base, index, slot, st, imp);
    cbwatch::note_import(imp.module, *imp.name ? imp.name : "?", ctx.gpr[3], lr, st >= 0);
}

void call_import_inner(Context& ctx, uint8_t* base, uint32_t index, ImportSlot& slot, int32_t st, const image::ImportEntry& imp);

void call_import_body(Context& ctx, uint8_t* base, uint32_t index, ImportSlot& slot, int32_t st, const image::ImportEntry& imp) {
    if (!slot.tap) return call_import_inner(ctx, base, index, slot, st, imp);
    uint64_t a[8];
    for (int i = 0; i < 8; i++) a[i] = ctx.gpr[3 + i];
    call_import_inner(ctx, base, index, slot, st, imp);
    slot.tap(a, ctx.gpr[3]);
}

void call_import_inner(Context& ctx, uint8_t* base, uint32_t index, ImportSlot& slot, int32_t st, const image::ImportEntry& imp) {
    /* Arguments must be captured BEFORE the handler runs: afterwards r3 holds
     * the return value, which is how an earlier SPURS probe read 0. */
    if (imp.name && std::strstr(imp.name, "cellSpursInitialize") && std::getenv("PS3BASE_SPURS_TRACE")) {
        LOG_INFO("spurs", "Initialize args spurs=0x%08x attr=0x%08x",
                 static_cast<unsigned>(ctx.gpr[3]), static_cast<unsigned>(ctx.gpr[4]));
    }
    /* PS3BASE_IMPORT_THREAD=<id>: every HLE import called BY ONE THREAD, with
     * arguments and return value. A thread that exits early is diagnosed by
     * what it called, not by its exit code. */
    bool import_thread_match = false;
    {
        static const uint32_t only = std::getenv("PS3BASE_IMPORT_THREAD")
            ? static_cast<uint32_t>(std::strtoul(std::getenv("PS3BASE_IMPORT_THREAD"), nullptr, 0)) : 0;
        /* The thread id differs between runs, so a name filter is the stable
         * way to follow one worker. */
        static const std::string only_name = std::getenv("PS3BASE_IMPORT_NAME")
            ? std::getenv("PS3BASE_IMPORT_NAME") : std::string();
        if (only || !only_name.empty()) {
            guest::Thread* t = guest::current();
            import_thread_match = t && ((only && t->id == only) ||
                                        (!only_name.empty() && t->name == only_name));
            if (import_thread_match) {
                static std::atomic<uint64_t> n{0};
                LOG_INFO("impthr", "#%llu %s::%s(0x%llx,0x%llx,0x%llx,0x%llx) lr=0x%08x",
                         static_cast<unsigned long long>(n.fetch_add(1) + 1),
                         imp.module, imp.name,
                         static_cast<unsigned long long>(ctx.gpr[3]),
                         static_cast<unsigned long long>(ctx.gpr[4]),
                         static_cast<unsigned long long>(ctx.gpr[5]),
                         static_cast<unsigned long long>(ctx.gpr[6]),
                         static_cast<unsigned>(ctx.lr));
            }
        }
    }
    if (imp.name && std::strstr(imp.name, "ppu_thread_exit") && std::getenv("PS3BASE_EXIT_TRACE")) {
        guest::Thread* t = guest::current();
        const uint32_t desc = static_cast<uint32_t>(ctx.gpr[31]);
        uint32_t opd = 0, code = 0, name_ea = 0;
        if (vm::range_mapped(desc, 48)) {
            opd = vm::read32(desc + 4);
            name_ea = desc + 12;
            if (vm::range_mapped(opd, 4)) code = vm::read32(opd);
        }
        LOG_INFO("threadexit", "thread=%s(0x%x) t=%.6f lr=0x%08x desc=0x%08x opd=0x%08x entry=0x%08x "
                 "entry_ret=0x%x reason=0x%llx name='%s'",
                 t ? t->name.c_str() : "?", t ? t->id : 0,
                 double(ppu::timebase()) / 80000000.0,
                 static_cast<unsigned>(ctx.lr), desc, opd, code,
                 vm::range_mapped(desc + 80, 4) ? vm::read32(desc + 80) : 0xFFFFFFFFu,
                 static_cast<unsigned long long>(ctx.gpr[3]),
                 vm::range_mapped(name_ea, 32) ?
                     reinterpret_cast<const char*>(vm::ptr(name_ea)) : "");
    }
    if (slot.log_args) {
        LOG_INFO("import", "%s::%s(0x%llx, 0x%llx, 0x%llx, 0x%llx) lr 0x%llx", imp.module, imp.name, static_cast<unsigned long long>(ctx.gpr[3]),
                 static_cast<unsigned long long>(ctx.gpr[4]), static_cast<unsigned long long>(ctx.gpr[5]), static_cast<unsigned long long>(ctx.gpr[6]),
                 static_cast<unsigned long long>(ctx.lr));
    }
    /* A pointer argument below the guest's first mapped page is never valid.
     * Naming the caller and the import is what turns "fault at 0x38" into a
     * writer. */
    const bool ptr_taking = imp.name &&
        (std::strstr(imp.name, "lwmutex") || std::strstr(imp.name, "lwcond") ||
         std::strstr(imp.name, "event")   || std::strstr(imp.name, "semaphore"));
    if (ptr_taking && ctx.gpr[3] && ctx.gpr[3] < 0x10000 && std::getenv("PS3BASE_BADPTR_LOG")) {
        /* Capped: an uncapped log here produced a million lines and starved the
         * run to 224 flips in five minutes, which measures nothing. The guest
         * stack walker is deliberately absent until its frame layout is
         * validated against a known frame. */
        static std::atomic<int> budget{200};
        if (budget.fetch_sub(1) > 0) {
            LOG_WARN("import", "low-pointer arg r3=0x%llx to %s::%s lr=0x%llx r4=0x%llx r5=0x%llx",
                     static_cast<unsigned long long>(ctx.gpr[3]), imp.module, *imp.name ? imp.name : "?",
                     static_cast<unsigned long long>(ctx.lr),
                     static_cast<unsigned long long>(ctx.gpr[4]),
                     static_cast<unsigned long long>(ctx.gpr[5]));
        }
    }
    if (st >= 0) {
        const image::ExportEntry& e = image::exports[st];
        uint32_t sp = static_cast<uint32_t>(ctx.gpr[1]);
        st64(base, sp + 0x28, ctx.gpr[2]);
        uint32_t code = ld32(base, e.opd);
        ctx.gpr[2] = ld32(base, e.opd + 4);
        if (std::getenv("PS3BASE_LLE_TRACE") && imp.name && std::strstr(imp.name, "SpursEventFlagSet")) {
            static std::atomic<int> n{0};
            if (n.fetch_add(1) < 20)
                LOG_INFO("lle", "EventFlagSet opd=0x%08x -> code=0x%08x known=%d",
                         e.opd, code, int(lookup(code) != nullptr));
        }
        int wait_seq = -1;
        uint32_t wait_mask_ptr = 0;
        uint64_t wait_mask_in = 0;
        if (std::getenv("PS3BASE_LLE_TRACE") && imp.name && std::strstr(imp.name, "SpursEventFlagWait")) {
            static std::atomic<int> n{0};
            wait_seq = n.fetch_add(1) + 1;
            wait_mask_ptr = static_cast<uint32_t>(ctx.gpr[4]);
            wait_mask_in = wait_mask_ptr ? ld16(base, wait_mask_ptr) : 0;
            LOG_INFO("lle", "wait-in  #%d t=%.6f ef=0x%08x mask_ptr=0x%08x mask_in=0x%llx "
                     "wait_mode=0x%x lr=0x%08x",
                     wait_seq, ppu::timebase() / 79.8e6,
                     static_cast<unsigned>(ctx.gpr[3]), wait_mask_ptr,
                     static_cast<unsigned long long>(wait_mask_in),
                     static_cast<unsigned>(ctx.gpr[5]), static_cast<unsigned>(ctx.lr));
        }
        LOG_TRACE("import", "%s::%s -> LLE 0x%08x", imp.module, imp.name, code);
        call_indirect(ctx, base, code);
        ctx.gpr[2] = ld64(base, sp + 0x28);
        if (slot.log_args) LOG_INFO("import", "%s::%s returned 0x%llx", imp.module, imp.name, static_cast<unsigned long long>(ctx.gpr[3]));
        if (import_thread_match)
            LOG_INFO("impthr", "        -> ret=0x%llx", static_cast<unsigned long long>(ctx.gpr[3]));
        if (wait_seq > 0) {
            LOG_INFO("lle", "wait-out #%d t=%.6f ret=0x%llx mask_out=0x%llx",
                     wait_seq, ppu::timebase() / 79.8e6,
                     static_cast<unsigned long long>(ctx.gpr[3]),
                     static_cast<unsigned long long>(wait_mask_ptr ? ld16(base, wait_mask_ptr) : 0));
        }
    if (imp.name && std::strstr(imp.name, "cellSpurs") && std::getenv("PS3BASE_SPURS_TRACE")) {
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 20000)
            LOG_INFO("spurs", "%s::%s -> 0x%llx (lr 0x%08x)", imp.module, imp.name,
                     static_cast<unsigned long long>(ctx.gpr[3]),
                     static_cast<unsigned int>(ctx.lr));
    }
        return;
    }
    if (st == kHle) {
        LOG_TRACE("import", "%s::%s -> HLE", imp.module, imp.name);
        slot.hle(ctx, base);
        if (import_thread_match)
            LOG_INFO("impthr", "        -> ret=0x%llx", static_cast<unsigned long long>(ctx.gpr[3]));
        if (slot.log_args) LOG_INFO("import", "%s::%s returned 0x%llx", imp.module, imp.name, static_cast<unsigned long long>(ctx.gpr[3]));
        return;
    }
    if (!slot.warned.exchange(true)) {
        LOG_WARN("import", "unimplemented %s::%s (0x%08x) r3=0x%llx r4=0x%llx", imp.module, *imp.name ? imp.name : "?", imp.nid,
                 static_cast<unsigned long long>(ctx.gpr[3]), static_cast<unsigned long long>(ctx.gpr[4]));
    }
    ctx.gpr[3] = 0;
}

void syscall(Context& ctx, uint8_t* base) {
    uint32_t n = static_cast<uint32_t>(ctx.gpr[11]) & 1023;
    auto& slot = lv2::table()[n];
    /* PS3BASE_SYSCALL_LOG=<n>: log the first n syscalls with their number,
     * name and caller. A title that is looping on a timed wait shows the same
     * handful of numbers forever; which handful is the diagnosis. */
    {
        static const long limit = std::getenv("PS3BASE_SYSCALL_LOG")
                                      ? std::atol(std::getenv("PS3BASE_SYSCALL_LOG"))
                                      : 0;
        static long seen = 0;
        static const double after_s = std::getenv("PS3BASE_SYSCALL_AFTER")
                                          ? std::atof(std::getenv("PS3BASE_SYSCALL_AFTER"))
                                          : 0.0;
        static const double t0 = double(ppu::timebase()) / 80000000.0;
        const bool late = (double(ppu::timebase()) / 80000000.0 - t0) >= after_s;
        static const uint32_t only = std::getenv("PS3BASE_SYSCALL_THREAD")
                                          ? static_cast<uint32_t>(std::strtoul(std::getenv("PS3BASE_SYSCALL_THREAD"), nullptr, 0))
                                          : 0;
        if (!late || (only && ctx.thread_id != only)) { /* filtered out */ }
        else if (limit && seen < limit) {
            seen++;
            const char* spec = lv2::spec_name(n);
            LOG_INFO("sys", "%u %s lr=0x%08x r3=0x%llx r4=0x%llx", n,
                     spec ? spec : "?", static_cast<uint32_t>(ctx.cia),
                     static_cast<unsigned long long>(ctx.gpr[3]),
                     static_cast<unsigned long long>(ctx.gpr[4]));
        }
    }
    if (slot.fn) {
        LOG_TRACE("lv2", "%s(0x%llx, 0x%llx, 0x%llx, 0x%llx)", slot.name, static_cast<unsigned long long>(ctx.gpr[3]), static_cast<unsigned long long>(ctx.gpr[4]),
                  static_cast<unsigned long long>(ctx.gpr[5]), static_cast<unsigned long long>(ctx.gpr[6]));
        uint64_t cp0 = critpath::g_on ? __rdtsc() : 0;
        uint32_t cp_arg = static_cast<uint32_t>(ctx.gpr[3]);
        guest::Thread* t = guest::current();
        if (t) {
            t->in_syscall.store(n + 1, std::memory_order_relaxed);
            t->syscalls.fetch_add(1, std::memory_order_relaxed);
            t->syscall_hist[n]++;
        }
        if (trace::on(trace::SYSCALL)) {
            uint64_t a0 = ctx.gpr[3], a1 = ctx.gpr[4], a2 = ctx.gpr[5];
            uint32_t lr = static_cast<uint32_t>(ctx.lr);
            uint64_t t0 = trace::now_ns();
            ctx.gpr[3] = static_cast<uint64_t>(slot.fn(ctx, base));
            uint64_t us = (trace::now_ns() - t0) / 1000;
            trace::emit(trace::T_SYSCALL, n, a0, a1, a2, ctx.gpr[3], (uint64_t(lr) << 32) | (us > 0xFFFFFFFFull ? 0xFFFFFFFFull : us));
        } else {
            ctx.gpr[3] = static_cast<uint64_t>(slot.fn(ctx, base));
        }
        if (t) t->in_syscall.store(0, std::memory_order_relaxed);
        if (cp0) critpath::syscall_done(n, cp0, static_cast<uint32_t>(ctx.lr), cp_arg, static_cast<uint32_t>(ctx.gpr[1]));
        return;
    }
    /* A syscall nobody implemented returns ENOSYS, named, once. It does NOT
     * return 0: CELL_OK from something that did not run tells the caller its
     * state was created when it was not, and the failure then surfaces far
     * from here -- a title that believes a mutex exists, a heap was mapped, a
     * thread was started. The number and its specification name are both
     * logged so the gap is actionable rather than mysterious. */
    if (!lv2::g_warned[n].exchange(1)) {
        const char* spec_name = lv2::spec_name(n);
        LOG_WARN("lv2", "ENOSYS: syscall %u (%s) not implemented, lr=0x%08x r3=0x%llx",
                 n, spec_name ? spec_name : "not in the LV2 table",
                 static_cast<uint32_t>(ctx.cia),
                 static_cast<unsigned long long>(ctx.gpr[3]));
    }
    ctx.gpr[3] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(0x8001003Fu)));
}

}

namespace lv2 {
/* The declaration lives in kernel.h under lv2; the implementation belongs with
 * the dispatch table it asks about, under ppu. One forwarder keeps both true. */
bool has_code_at(uint32_t addr) { return ppu::has_code_at(addr); }
}  // namespace lv2
