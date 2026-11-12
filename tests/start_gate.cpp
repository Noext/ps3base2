/* ps3base -- the start rule and the process parameter, pinned.
 *
 * Two subjects, both of which decide scheduling for EVERY title and both of
 * which used to be a literal in the runtime:
 *
 *   1. runtime/include/runtime/start_gate.h -- a thread created with a higher
 *      priority than its creator starts ahead of it and the creator waits until
 *      the child stops running (blocks, yields, or exits), bounded.
 *      Specification: refs/rpcs3/rpcs3/Emu/Cell/PPUThread.h:287 (smaller number
 *      = higher priority) and .../lv2/sys_ppu_thread.cpp:578-622 (the wait, and
 *      its 5000 us bound).
 *
 *   2. PT_PROC_PARAM decoding in runtime/src/loader.cpp -- the main thread's
 *      priority comes from the title's own section (p_type 0x60000001), whose
 *      layout is refs/rpcs3/.../PPUModule.cpp:2185-2192 and whose acceptance
 *      rule is at :2210-2213.
 *
 * The three expected decodes are the three titles' REAL section bytes, dumped
 * from their ELFs:
 *
 *   rdr 00000020 13bcc5f6 00009000 00300001 00000800 00040000 00100000 00000000  prio 2048
 *   gta 00000024 13bcc5f6 00330000 00475001 00000226 00040000 00100000 00000000  prio  550
 *   mc  00000024 13bcc5f6 00330000 00450001 000003e9 00010000 00100000 00000000  prio 1001
 *
 * No guest memory and no PPU runtime is linked: the two functions under test
 * are pure, which is why this runs on a machine with no game.
 */
#include "runtime/loader.h"
#include "runtime/start_gate.h"

#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

/* The real bytes, in the order they sit in the file. */
const uint8_t kRdr[32] = {
    0x00, 0x00, 0x00, 0x20, 0x13, 0xbc, 0xc5, 0xf6, 0x00, 0x00, 0x90, 0x00,
    0x00, 0x30, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kGta[32] = {
    0x00, 0x00, 0x00, 0x24, 0x13, 0xbc, 0xc5, 0xf6, 0x00, 0x33, 0x00, 0x00,
    0x00, 0x47, 0x50, 0x01, 0x00, 0x00, 0x02, 0x26, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t kMc[32] = {
    0x00, 0x00, 0x00, 0x24, 0x13, 0xbc, 0xc5, 0xf6, 0x00, 0x33, 0x00, 0x00,
    0x00, 0x45, 0x00, 0x01, 0x00, 0x00, 0x03, 0xe9, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void decode_one(const char* name, const uint8_t* bytes, uint32_t size, uint32_t sdk, int32_t prio, uint32_t stack, uint32_t malloc_page) {
    loader::ProcParam pp;
    CHECK(loader::decode_proc_param(bytes, 32, pp), "decode returns true for a 32 byte section");
    CHECK(pp.magic_ok, "magic 0x13bcc5f6 is recognised");
    CHECK(pp.magic == 0x13bcc5f6u, "magic is read big-endian");
    CHECK(pp.size == size, "size field");
    CHECK(!pp.size_short, "0x20/0x24 is not short");
    CHECK(pp.sdk_version == sdk, "sdk version field");
    CHECK(pp.primary_prio == prio, "primary priority field");
    CHECK(pp.primary_stacksize == stack, "primary stack size field");
    CHECK(pp.malloc_pagesize == malloc_page, "malloc page size field");
    CHECK(pp.ppc_seg == 0, "ppc_seg field");
    (void)name;
}

}

int main() {
    /* --- 1. the comparison itself --------------------------------------- */
    /* Smaller number = higher priority (RPCS3 PPUThread.h:287, ps3recomp
     * docs/SYSCALLS.md:112 "0 = highest, 3071 = lowest"). */
    CHECK(start_gate::takes_precedence(100, 200), "a smaller number starts ahead of a larger one");
    CHECK(!start_gate::takes_precedence(200, 100), "a larger number does not start ahead");
    CHECK(!start_gate::takes_precedence(100, 100), "equal priority does not take precedence (strict, as RPCS3's <)");
    /* RDR's real numbers: main 2048, the SPURS handlers 2016, the RAGE workers
     * 2047/2048. The handlers are the ones the console starts ahead of main. */
    CHECK(start_gate::takes_precedence(2016, 2048), "SPURS handler (2016) starts ahead of RDR main (2048)");
    CHECK(!start_gate::takes_precedence(2048, 2048), "a RAGE worker at 2048 does not start ahead of main at 2048");
    CHECK(start_gate::takes_precedence(1, 2048), "_gcm_intr_thread at 1 starts ahead of everything");
    /* gta main is 550 and mc main is 1001: the same child is ahead on one and
     * behind on the other, which is why the comparison is with the CREATOR and
     * not with a constant. */
    CHECK(start_gate::takes_precedence(500, 550), "500 is ahead of gta main 550");
    /* ... but the SAME child number is ahead of gta's 550 and behind nothing:
     * mc main is 1001, so 500 is still ahead of it. The comparison is a total
     * order on one number, and the interesting case is a child that is ahead on
     * one title and behind on the other -- a child at 1500: */
    CHECK(start_gate::takes_precedence(500, 1001), "500 is ahead of mc main 1001 too (smaller number wins)");
    CHECK(!start_gate::takes_precedence(1500, 1001), "1500 is behind mc main 1001");
    CHECK(start_gate::takes_precedence(1500, 2048), "and ahead of RDR main 2048 -- same child, opposite verdicts");

    /* --- 2. the option -------------------------------------------------- */
    CHECK(start_gate::enabled_value(nullptr), "unset means on");
    CHECK(start_gate::enabled_value(""), "empty means on");
    CHECK(start_gate::enabled_value("1"), "1 means on");
    CHECK(!start_gate::enabled_value("0"), "0 means off");
    CHECK(!start_gate::enabled_value("0x0"), "0x0 means off, the leading character decides like GOWA_ASYNC_SHADERS");
    CHECK(start_gate::budget_us() == 5000, "the budget is RPCS3's 5000 us");

    /* --- 3. the wait, with a clock that cannot run out ------------------ */
    {
        int polls = 0;
        auto ready = [&] { return ++polls >= 3; };
        auto clock = [] { return uint64_t(0); };
        CHECK(start_gate::wait_until_ready(ready, clock, 5000), "a predicate that answers is seen");
        CHECK(polls == 3, "the wait polls the predicate one step at a time");
    }
    /* --- 4. the bound --------------------------------------------------- */
    {
        uint64_t t = 0;
        auto ready = [] { return false; };
        auto clock = [&t] { return t += 1000; };
        CHECK(!start_gate::wait_until_ready(ready, clock, 5000), "a predicate that never answers gives up at the budget");
        CHECK(t <= 7000, "and it gives up near the budget, not after it");
    }
    /* --- 5. a real thread that blocks: the point of the rule ------------- */
    {
        /* The shape sys_ppu_thread_start now has: arm, spawn, wait for the child
         * to reach its first blocking point. A child that blocks is released
         * immediately; the creator does not wait for the child to finish. */
        std::atomic<bool> blocked{false};
        std::atomic<bool> done{false};
        std::thread child([&] {
            /* 1 ms, not 20: the budget is 5000 us, and the point of the case is
             * a child that blocks WITHIN it. A child that blocks after the
             * budget is case 6, and there the answer must be "budget expired". */
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            blocked.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            done.store(true);
        });
        const uint64_t t0 = start_gate::clock_us();
        const bool ok = start_gate::wait_until_ready([&] { return blocked.load(); }, start_gate::clock_us, start_gate::budget_us());
        const uint64_t elapsed = start_gate::clock_us() - t0;
        CHECK(ok, "a child that blocks releases its creator");
        CHECK(!done.load(), "and the creator resumes at the block, not at the child's exit");
        CHECK(elapsed < 100000, "the creator resumed long before the child finished");
        child.join();
    }
    /* --- 6. a real thread that never blocks: the budget, not a hang ------ */
    {
        std::atomic<bool> stop{false};
        std::thread child([&] {
            while (!stop.load()) std::this_thread::yield();
        });
        /* Do not let the child run: it spins, it never "blocks", so the gate
         * must expire rather than park the creator forever. */
        const uint64_t t0 = start_gate::clock_us();
        const bool ok = start_gate::wait_until_ready([&] { return false; }, start_gate::clock_us, start_gate::budget_us());
        const uint64_t elapsed = start_gate::clock_us() - t0;
        CHECK(!ok, "a child that never blocks leaves the budget to expire");
        CHECK(elapsed >= 4000 && elapsed < 200000, "the expiry is near 5000 us, not immediate and not unbounded");
        stop.store(true);
        child.join();
    }

    /* --- 7. the process parameter -------------------------------------- */
    decode_one("rdr", kRdr, 0x20, 0x300001, 2048, 0x40000, 0x100000);
    decode_one("gta", kGta, 0x24, 0x475001, 550, 0x40000, 0x100000);
    decode_one("mc", kMc, 0x24, 0x450001, 1001, 0x10000, 0x100000);

    /* A bad magic must be refused outright -- RPCS3 does not read the rest of
     * the section when it is wrong, and a misparse here changes the priority
     * every child is compared against. */
    {
        uint8_t bad[32];
        std::memcpy(bad, kRdr, 32);
        bad[4] = 0;   /* magic 0x00bcc5f6 */
        loader::ProcParam pp;
        CHECK(loader::decode_proc_param(bad, 32, pp), "a bad magic still decodes");
        CHECK(!pp.magic_ok, "but it is marked not ok");
        loader::ProcessInfo info;
        const int32_t before = info.primary_prio;
        loader::apply_proc_param(pp, info);
        CHECK(info.primary_prio == before, "and nothing from the section is applied");
    }
    /* The acceptance window is RPCS3's (PPUModule.cpp:2210-2213): below 3072
     * and at least 0 without debug permission. */
    {
        struct Case { int32_t prio; bool accepted; };
        const Case cases[] = {{0, true}, {1, true}, {2048, true}, {3071, true}, {3072, false}, {-1, false}, {-512, false}, {100000, false}};
        for (const Case& c : cases) {
            uint8_t bytes[32];
            std::memcpy(bytes, kRdr, 32);
            const uint32_t v = static_cast<uint32_t>(c.prio);
            bytes[16] = static_cast<uint8_t>(v >> 24);
            bytes[17] = static_cast<uint8_t>(v >> 16);
            bytes[18] = static_cast<uint8_t>(v >> 8);
            bytes[19] = static_cast<uint8_t>(v);
            loader::ProcParam pp;
            loader::decode_proc_param(bytes, 32, pp);
            loader::ProcessInfo info;
            info.primary_prio = 1000;   /* RPCS3's own default when nothing is applied */
            loader::apply_proc_param(pp, info);
            if (c.accepted) {
                checks++;
                if (info.primary_prio != c.prio) { fails++; std::fprintf(stderr, "[FAIL] prio %d should be accepted\n", c.prio); }
            } else {
                checks++;
                if (info.primary_prio != 1000) { fails++; std::fprintf(stderr, "[FAIL] prio %d should be refused\n", c.prio); }
            }
        }
    }
    /* A truncated section still yields a size and is reported short, rather
     * than reading past the program header. */
    {
        loader::ProcParam pp;
        CHECK(loader::decode_proc_param(kRdr, 2, pp) == false, "two bytes is not enough even for a size");
        CHECK(loader::decode_proc_param(kRdr, 4, pp), "four bytes gives the size");
        CHECK(pp.size == 0x20, "and the size is the real one");
        CHECK(pp.size_short == false, "0x20 is not short");
        uint8_t six[8];
        std::memcpy(six, kRdr, 8);
        six[3] = 0x10;   /* size 0x10 */
        loader::ProcParam p2;
        loader::decode_proc_param(six, 8, p2);
        CHECK(p2.size == 0x10, "a short section reports its size");
        CHECK(p2.size_short, "and is flagged short");
    }

    /* --- 8. the loader writes the priority the section states ----------- */
    /* The main thread's priority is what every child is compared against, so
     * the value the gate uses has to be the one in the file. */
    {
        loader::ProcessInfo info;
        loader::ProcParam pp;
        loader::decode_proc_param(kRdr, 32, pp);
        loader::apply_proc_param(pp, info);
        CHECK(info.primary_prio == 2048, "RDR main thread priority is 2048");
        CHECK(info.sdk_version == 0x300001, "RDR sdk version");
        CHECK(info.primary_stacksize == 0x40000, "RDR primary stack size");
        CHECK(pp.prio_accepted, "and it was accepted");
    }

    std::printf("start_gate: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
