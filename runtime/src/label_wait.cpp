#include "runtime/label_wait.h"

#include "runtime/host.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "ppu/ops.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/threading.h>
#endif

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>

namespace label_wait {

namespace {

/* One parked waiter: the guest address of the 4-byte word it waits on. Walking
 * this list is how the notifier finds the futex to wake; nothing else in the
 * runtime knows a thread is parked. */
struct Slot {
    std::atomic<uint32_t> ea{0};
    std::atomic<bool> active{false};
};
constexpr int kSlots = 64;
Slot g_slots[kSlots];

/* Fast path for rsv::store_*: how many slots are occupied. Every guest store
 * reads this once and returns; only a non-zero count walks the slots. */
std::atomic<int> g_parked{0};

std::atomic<uint64_t> g_sites{0};
std::atomic<uint64_t> g_parks{0};
std::atomic<uint64_t> g_wakes{0};
std::atomic<uint64_t> g_timeouts{0};

/* Cap for one Atomics.wait. A word nothing writes must not park its waiter
 * forever: the wait times out, the guest's loop re-reads, and the same decision
 * is made -- but the thread was BLOCKED instead of rescheduled onto a timer.
 *
 * A compile-time constant on purpose: getenv is inert under wasm (see
 * docs/webassembly/M3-PARK-FIX.md), so a runtime knob here would read as "the
 * knob does nothing" rather than "the feature is off". */
#ifndef PS3BASE_LABEL_WAIT_MS
#define PS3BASE_LABEL_WAIT_MS 2
#endif
constexpr uint32_t kParkMs = PS3BASE_LABEL_WAIT_MS;

/* Repeat threshold that turns "slept here again" into "polling here". A one-shot
 * delay is one call; a wait loop calls the same guest instruction forever. */
constexpr uint32_t kPollThreshold = 3;
/* How far past the syscall a poll's re-read may sit. The lifted loops put it at
 * +4; the window is generous so a cmpwi/li in between does not defeat discovery,
 * and stops at the branch that closes the loop. */
constexpr int kScanInstructions = 6;

struct SiteCache {
    uint32_t site = 0;     /* ctx.cia of the syscall instruction */
    uint32_t repeats = 0;
    uint32_t ea = 0;       /* decoded poll word, 0 = none found */
};
thread_local SiteCache t_site;

uint32_t read_word(uint32_t ea) {
    uint32_t v;
    std::memcpy(&v, vm::ptr(ea), 4);
    return v;
}

/* ---- guest instruction decode ------------------------------------------ */

/* The poll word is the operand of the first GUEST LOAD after the syscall. That
 * is the loop's own re-read -- exactly the word whose value decides whether the
 * loop repeats -- and it needs no per-title table and no instrumentation of the
 * load path (which would recompile every lifted translation unit).
 *
 * Only the load forms a wait loop uses are decoded. Anything unrecognised
 * returns 0, which makes the caller fall back to the guest's own sleep: a park
 * is never attempted on a guess. */
bool decode_load(uint32_t word, uint64_t* gpr, uint32_t* ea) {
    const uint32_t op = word >> 26;
    auto exts = [](uint32_t v, int bits) -> int32_t {
        const uint32_t m = 1u << (bits - 1);
        return int32_t((v ^ m) - m);
    };
    switch (op) {
    case 32:   /* lwz  D(RA) */
    case 34:   /* lbz */
    case 40:   /* lhz */
    case 42:   /* lha */
    case 48:   /* lfs */
    case 50:   /* lfd */
    case 58: { /* ld */
        const uint32_t ra = (word >> 16) & 0x1F;
        const int32_t d = exts(word & 0xFFFF, 16);
        const uint64_t base = ra ? gpr[ra] : 0;
        *ea = static_cast<uint32_t>(base) + static_cast<uint32_t>(d);
        return true;
    }
    case 31: { /* the X-form loads: lwzx 23, lbzx 87, lhzx 279, ldx 21 */
        const uint32_t xo = (word >> 1) & 0x3FF;
        if (xo != 23 && xo != 87 && xo != 279 && xo != 21) return false;
        const uint32_t ra = (word >> 16) & 0x1F;
        const uint32_t rb = (word >> 11) & 0x1F;
        const uint64_t base = ra ? gpr[ra] : 0;
        *ea = static_cast<uint32_t>(base + gpr[rb]);
        return true;
    }
    default:
        return false;
    }
}

/* A branch closes the loop body, so the re-read is never past one. */
bool is_branch(uint32_t word) {
    const uint32_t op = word >> 26;
    return op == 16 /* bc */ || op == 18 /* b */ || op == 19 /* bclr/bcctr */;
}

int claim_slot(uint32_t ea) {
    for (int i = 0; i < kSlots; i++) {
        bool expected = false;
        if (g_slots[i].active.compare_exchange_strong(expected, true, std::memory_order_seq_cst)) {
            g_slots[i].ea.store(ea, std::memory_order_seq_cst);
            g_parked.fetch_add(1, std::memory_order_seq_cst);
            return i;
        }
    }
    return -1;
}

void release_slot(int i) {
    if (i < 0) return;
    g_slots[i].ea.store(0, std::memory_order_seq_cst);
    g_slots[i].active.store(false, std::memory_order_seq_cst);
    g_parked.fetch_sub(1, std::memory_order_seq_cst);
}

/* Atomics.wait is illegal on a browser main thread (it throws), so parking is
 * refused there and the caller falls back to the guest's own sleep. Every guest
 * PPU thread is a worker in this build, but the check costs nothing and keeps a
 * no-pthreads build from aborting. */
bool can_block() {
#if defined(__EMSCRIPTEN__)
    return !emscripten_is_main_browser_thread();
#else
    return true;
#endif
}

}  // namespace

bool note_sleep_site(uint32_t site) {
    g_sites.fetch_add(1, std::memory_order_relaxed);
    if (site != t_site.site) {
        /* A previous site's decode cannot be reused: the guest left that loop. */
        t_site.site = site;
        t_site.repeats = 1;
        t_site.ea = 0;
        return false;
    }
    if (t_site.repeats < kPollThreshold) t_site.repeats++;
    return t_site.repeats >= kPollThreshold;
}

bool poll_target(uint8_t* base, const uint64_t* gpr, uint32_t cia, uint32_t* ea) {
    if (!ea) return false;
    if (t_site.ea) { *ea = t_site.ea; return true; }
    if (cia < 0x10000) return false;
    for (int i = 1; i <= kScanInstructions; i++) {
        const uint32_t at = cia + 4u * i;
        const uint32_t word = ppu::ld32(base, at);
        if (is_branch(word)) return false;
        uint32_t cand = 0;
        if (!decode_load(word, const_cast<uint64_t*>(gpr), &cand)) continue;
        /* The word has to be a mapped 4-byte slot: a load from a register that
         * holds a scalar, or from unmapped guest memory, is not a poll target. */
        const uint32_t a = cand & ~3u;
        if (!a || !vm::range_mapped(a, 4)) continue;
        t_site.ea = a;
        *ea = a;
        return true;
    }
    return false;
}

int park(uint32_t ea, uint32_t max_ms) {
    /* Atomics.wait needs 4-byte alignment; a misaligned or unmapped address falls
     * back to the guest's own timed poll rather than parking on a word no store
     * could wake. */
    if (!ea || (ea & 3) || !vm::range_mapped(ea, 4) || !can_block()) return 0;
    {
        /* Which word is parked on, said once: the measurement must name the word
         * rather than assert it. */
        static std::atomic<int> shown{0};
        if (shown.fetch_add(1, std::memory_order_relaxed) < 4) {
            std::printf("ps3base-wasm: labelwait park on ea=0x%08x value=0x%08x cap=%u ms\n",
                        ea, read_word(ea), max_ms);
            std::fflush(stdout);
        }
    }
    const int slot = claim_slot(ea);
    if (slot < 0) return 0;   /* every slot busy: fall through to the guest's poll */

    const uint32_t expect = read_word(ea);
    host::wait_on(vm::ptr(ea), &expect, 4, max_ms);
    int result = 0;
    if (read_word(ea) != expect) {
        g_wakes.fetch_add(1, std::memory_order_relaxed);
        result = 1;
    } else {
        g_timeouts.fetch_add(1, std::memory_order_relaxed);
    }
    release_slot(slot);
    g_parks.fetch_add(1, std::memory_order_relaxed);
    return result;
}

void notify(uint32_t addr, uint32_t size) {
    if (!size || g_parked.load(std::memory_order_seq_cst) == 0) return;
    const uint64_t lo = addr, hi = uint64_t(addr) + size;
    for (int i = 0; i < kSlots; i++) {
        if (!g_slots[i].active.load(std::memory_order_seq_cst)) continue;
        const uint32_t ea = g_slots[i].ea.load(std::memory_order_seq_cst);
        if (!ea) continue;
        if (uint64_t(ea) + 4 <= lo || uint64_t(ea) >= hi) continue;
        host::wake_all(vm::ptr(ea));
    }
}

void stats(Stats* out) {
    if (!out) return;
    out->sites = g_sites.load(std::memory_order_relaxed);
    out->parks = g_parks.load(std::memory_order_relaxed);
    out->wakes = g_wakes.load(std::memory_order_relaxed);
    out->timeouts = g_timeouts.load(std::memory_order_relaxed);
}

int selftest(uint32_t scratch_ea) {
    /* The proof word has to be in memory the runtime has actually allocated:
     * range_mapped() reflects the allocator's block list, and a bare address in
     * main RAM reads as unmapped even though the flat span covers it. MEASURED:
     * 0x00100200 was refused here, which is why the first version reported FAIL
     * without even reaching the wait. */
    if (!scratch_ea || (scratch_ea & 3) || !vm::range_mapped(scratch_ea, 4))
        scratch_ea = vm::alloc(vm::Region::Main, 0x1000, 0x1000);
    if (!scratch_ea || !vm::range_mapped(scratch_ea, 4)) return 0;
    vm::write32(scratch_ea, 0x11111111u);

    std::atomic<int> parked{0};
    std::atomic<int> woke{0};

    std::thread waiter([&] {
        parked.store(1, std::memory_order_seq_cst);
        const uint32_t expect = read_word(scratch_ea);
        host::wait_on(vm::ptr(scratch_ea), &expect, 4, 5000);
        woke.store(read_word(scratch_ea) != expect ? 1 : 0, std::memory_order_seq_cst);
    });

    while (!parked.load(std::memory_order_seq_cst)) host::sleep_ms(0);
    /* Give the waiter time to actually reach Atomics.wait before the write: a
     * write that lands first only proves the fast path, not the wake. */
    host::sleep_ms(50);
    /* Write through the PRODUCTION store path, so the measurement covers
     * rsv::store_range -> label_wait::notify -> futex wake, not a special case. */
    vm::write32(scratch_ea, 0x22222222u);
    waiter.join();
    const int ok = woke.load(std::memory_order_seq_cst);
    std::printf("ps3base-wasm: labelwait selftest ea=0x%08x woke=%d after=0x%08x mapped=%d can_block=%d\n",
                scratch_ea, ok, read_word(scratch_ea), vm::range_mapped(scratch_ea, 4) ? 1 : 0,
                can_block() ? 1 : 0);
    std::fflush(stdout);
    return ok;
}

}  // namespace label_wait
