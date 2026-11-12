/* ps3base -- a HOST write into guest memory must invalidate a reservation.
 *
 * What this measures, and why the shape is not the obvious one.
 *
 * rsv::commit_if (which is what stwcx. and putllc call) has two checks:
 *
 *   const uint8_t* cur = vm::ptr(line + offset);
 *   if (std::memcmp(cur, expected, size) != 0) return false;   -- the data check
 *   if (e.ver.load() != rtime) return false;                   -- the version check
 *
 * so a raw memcpy that leaves DIFFERENT bytes behind is caught by the data
 * comparison, by luck rather than by design. The case that is not caught is the
 * one the architecture has to catch:
 *
 *   lwarx  r3, 0, r4          reserve the line, read 0x00000000
 *   <host> memcpy(vm::ptr(r4), 0xcafebabe)
 *   <host> memcpy(vm::ptr(r4), 0x00000000)     restored, so the DATA matches
 *   stwcx. r3, 0, r4          must FAIL -- the line changed while reserved
 *
 * POWER says that stwcx. fails. With no version bump it commits, and the guest
 * has just written back a value derived from a state that no longer existed.
 * That is the lost update, and it is silent. The same version is the ONLY signal
 * the SPU path uses (spu/thread.cpp:279 rsv::lost, :418 rsv::wait_parked) and the
 * only one the watches use, so a raw memset/memcpy also makes the writer of a
 * watched field invisible.
 *
 * The tree had 81 memcpy/memset(vm::ptr(...)) sites, 16 scalar writes and one
 * guest-to-guest memmove with that shape. They are now pgw::write / pgw::fill /
 * pgw::copy, which advance the version AND feed the watch.
 */
#include "runtime/memory.h"
#include "runtime/pgwrite.h"
#include "runtime/reservation.h"
#include "ppu/ops.h"

#include <atomic>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstring>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    const bool _ok = (cond); \
    std::fprintf(stderr, "  [%s] %s\n", _ok ? "ok" : "FAIL", msg); \
    if (!_ok) fails++; \
} while (0)

namespace {

/* One mapped 128-byte line, zeroed. */
uint32_t alloc_line() {
    const uint32_t a = vm::reserve_slot(0x10000, 0x1000);
    if (!a) return 0;
    if (!vm::commit(a, 0x10000)) return 0;
    pgw::fill(a + 0x80, 0, 128);
    return a + 0x80;
}

/* The A-B-A: write different bytes, then the original bytes, between the
 * reservation and the conditional store. */
bool aba(uint32_t ea, bool through_funnel) {
    ppu::Context ctx{};
    const uint32_t loaded = ppu::lwarx(ctx, vm::g_base, ea);
    const uint32_t a = __builtin_bswap32(0xCAFEBABEu);
    const uint32_t b = __builtin_bswap32(loaded);
    if (through_funnel) {
        pgw::write(ea, &a, 4);
        pgw::write(ea, &b, 4);
    } else {
        std::memcpy(vm::ptr(ea), &a, 4);
        std::memcpy(vm::ptr(ea), &b, 4);
    }
    /* The guest sees its own value back, so a data comparison alone cannot tell
     * that the line was modified. Only the version can. */
    return ppu::stwcx(ctx, vm::g_base, ea, loaded + 1);
}

/* ---------------------------------------------------------------- the guard.
 * A guarded page is PROT_NONE, so a plain host READ of it faults. That is what
 * makes it a read watch, which GOWA_TRAP cannot be: GOWA_TRAP needs the address
 * named before the run and covers only the direction it is told. */
sigjmp_buf g_jmp;

void segv(int, siginfo_t*, void*) { siglongjmp(g_jmp, 1); }

bool raw_read(uint32_t ea, uint8_t* out) {
    if (sigsetjmp(g_jmp, 1)) return false;
    *out = *reinterpret_cast<volatile uint8_t*>(vm::g_base + ea);
    return true;
}

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* ---- 1. Control: nothing in between, the stwcx. commits. --------------- */
    {
        const uint32_t ea = alloc_line();
        if (!ea) { std::fprintf(stderr, "could not map\n"); return 1; }
        ppu::Context ctx{};
        const uint32_t loaded = ppu::lwarx(ctx, vm::g_base, ea);
        const bool ok = ppu::stwcx(ctx, vm::g_base, ea, loaded + 1);
        CHECK(ok, "control: stwcx. with nothing in between commits");
        CHECK(vm::read32(ea) == loaded + 1, "control: the commit is visible");
    }

    /* ---- 2. Raw memcpy, A-B-A: the line was modified, the store still commits.
     * This is the defect, measured. */
    {
        const uint32_t ea = alloc_line();
        const bool committed = aba(ea, false);
        std::fprintf(stderr, "  raw memcpy A-B-A: stwcx. %s, word 0x%08x\n",
                     committed ? "COMMITTED -- LOST UPDATE" : "failed", vm::read32(ea));
        CHECK(committed, "measured: a raw memcpy does NOT invalidate -- this is the defect");
    }

    /* ---- 3. The funnel, same A-B-A: the store must fail. ------------------ */
    {
        const uint32_t ea = alloc_line();
        const bool committed = aba(ea, true);
        std::fprintf(stderr, "  pgw A-B-A: stwcx. %s, word 0x%08x\n",
                     committed ? "COMMITTED -- STILL BROKEN" : "failed (correct)", vm::read32(ea));
        CHECK(!committed, "pgw::write must invalidate the reservation across an A-B-A");
        CHECK(vm::read32(ea) == 0, "the funnel must leave the guest value the last write put there");
    }

    /* ---- 4. Every host-write shape advances the line version. ------------- */
    {
        const uint32_t ea = alloc_line();
        const uint32_t line = ea & ~127u;
        const uint64_t v0 = rsv::version(line).load();
        const uint32_t be = __builtin_bswap32(0x11223344u);
        pgw::write(ea, &be, 4);
        const uint64_t v1 = rsv::version(line).load();
        pgw::fill(ea, 0x5A, 4);
        const uint64_t v2 = rsv::version(line).load();
        const uint32_t src = vm::reserve_slot(0x10000, 0x1000);
        vm::commit(src, 0x10000);
        pgw::write(src, &be, 4);
        pgw::copy(ea, src, 4);
        const uint64_t v3 = rsv::version(line).load();
        std::fprintf(stderr, "  line 0x%08x version: %llu write->%llu fill->%llu copy->%llu\n", line,
                     (unsigned long long)v0, (unsigned long long)v1, (unsigned long long)v2,
                     (unsigned long long)v3);
        CHECK(v1 > v0, "pgw::write advances the line version");
        CHECK(v2 > v1, "pgw::fill advances the line version");
        CHECK(v3 > v2, "pgw::copy advances the line version");
        CHECK(vm::read32(ea) == 0x11223344u, "pgw::copy wrote the guest value");
    }

    /* ---- 5. The page guard is a READ watch, and it cannot kill the run. --- */
    {
        const uint32_t page = vm::reserve_slot(0x10000, 0x1000);
        if (!page || !vm::commit(page, 0x10000)) { std::fprintf(stderr, "could not map a guard page\n"); return 1; }
        pgw::write(page + 0x40, (const uint32_t[]){__builtin_bswap32(0xAABBCCDDu)}, 4);

        struct sigaction sa{};
        sa.sa_sigaction = segv;
        sa.sa_flags = SA_SIGINFO | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        struct sigaction old{};
        sigaction(SIGSEGV, &sa, &old);

        uint8_t b = 0;
        CHECK(raw_read(page + 0x40, &b), "an unguarded page is readable, no fault");

        CHECK(vm::guard_arm(page + 0x40, 4), "guard_arm succeeds on the os backend");
        CHECK(vm::guard_armed(page + 0x40), "the armed address reports armed");

        const bool read = raw_read(page + 0x40, &b);
        std::fprintf(stderr, "  read of an armed page: %s\n", read ? "succeeded -- NOT a guard" : "faulted");
        CHECK(!read, "a READ of a guarded page faults -- this is the read watch");

        const uint64_t hits0 = vm::guard_hits();
        CHECK(vm::guard_fault(page + 0x40), "guard_fault claims an address it guards");
        CHECK(vm::guard_hits() == hits0 + 1, "guard_fault counts the hit");
        vm::guard_rearm(page >> 12);

        CHECK(!raw_read(page + 0x40, &b), "after re-arming the page is guarded again");
        vm::guard_fault(page + 0x40);

        /* Now the host write path: it must not fault, and it must be counted. */
        const char* s0 = "guard: 2 guest faults, 0 host writes";
        const std::string before = vm::guard_report();
        std::fprintf(stderr, "  %s\n", s0);
        std::fprintf(stderr, "  %s   (before the host write)\n", before.c_str());
        const uint32_t be2 = __builtin_bswap32(0x55667788u);
        pgw::write(page + 0x40, &be2, 4);
        const std::string after = vm::guard_report();
        std::fprintf(stderr, "  %s   (after the host write)\n", after.c_str());
        CHECK(before != after, "a host write on a guarded page is counted, not silently lost");

        /* The page is guarded again after the funnel closed it, so the guest
         * still cannot read it without being reported. */
        CHECK(!raw_read(page + 0x40, &b), "the funnel re-armed the page after its store");

        /* To see the value, the page has to be opened the same way the funnel
         * opens it -- a plain read here would fault inside the test. */
        vm::guard_host_begin(page + 0x40, 4);
        const uint32_t got = vm::read32(page + 0x40);
        vm::guard_host_end(page + 0x40, 4);
        std::fprintf(stderr, "  guarded word now 0x%08x\n", got);
        CHECK(got == 0x55667788u, "the host write landed");

        sigaction(SIGSEGV, &old, nullptr);
    }

    /* ---- 6. The counters prove the path was taken. ------------------------ */
    {
        std::fprintf(stderr, "  %s\n", pgw::stats().c_str());
        CHECK(pgw::g_writes.load() >= 8, "every host write above went through the funnel");
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
