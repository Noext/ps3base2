/* ps3base -- _sys_lwmutex_create, driven through the REAL syscall handler.
 *
 * The subject is runtime/src/lv2/lwmutex.cpp. The handler is not re-implemented
 * here: the test calls it through ppu::Context, exactly as the dispatcher does,
 * so what passes is the code that runs.
 *
 * The three properties are the ones RPCS3's
 * refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_lwmutex.cpp fixes in its first twenty lines,
 * and each was wrong here:
 *
 *   1. PROTOCOL. An unknown protocol is refused with CELL_EINVAL and no object
 *      is minted (RPCS3 :31-36). We minted one and returned CELL_OK, telling the
 *      caller its mutex existed while nothing would honour the policy.
 *      SYS_SYNC_FIFO 0x1, SYS_SYNC_PRIORITY 0x2, SYS_SYNC_RETRY 0x4
 *      (refs/.../lv2/sys_sync.h:11-17). NOTE 0x3 (PRIORITY_INHERIT) is NOT one
 *      of them for an lwmutex; RPCS3 rejects it.
 *
 *   2. HAS_NAME. RPCS3 :41 is "if (!(has_name < 0)) name = 0;" -- the name is
 *      kept only when has_name is NEGATIVE. Our old code kept it for every
 *      non-zero value. RDR passes 0x80000001, which is negative, so the bug was
 *      invisible on this title; a caller passing +1 would have had a name it
 *      asked to be ignored.
 *
 *   3. The ID is written to *out and the control block is NOT touched. The
 *      guest structure belongs to the firmware wrapper (it writes +0,+4,+8,+12,
 *      +16 after the sc); a syscall that also wrote it would race the wrapper.
 *
 * The regression this guards is not hypothetical: the whole reason the task was
 * opened is that the guest's +16 must be written ONCE, by the wrapper, and any
 * belief that the syscall does it leads to "fixing" the wrong function.
 */
#include "runtime/memory.h"
#include "runtime/kernel.h"
#include "lv2/lwsync.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

/* lwmutex.cpp asks guest::current() for the waiter's thread id and registers its
 * syscalls at static-init over the object table. Linking the whole thread
 * subsystem would drag in the PPU, so both are satisfied here. */
namespace guest {
struct Thread;
Thread* current() { return nullptr; }
}

namespace lv2 {
SyscallReg::SyscallReg(unsigned, const char*, SyscallFn) {}
}

/* The handler is static inside lwmutex.cpp and reached only through its
 * registration; the test therefore re-declares nothing and instead calls the
 * behaviour through the same three fields the dispatcher sets. Because the
 * handler is static, this test drives it via the registration-free entry:
 * lv2::lwmutex_syscall_create(), a one-line forwarder declared in lwsync.h and
 * defined beside the handler. */


static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

uint32_t g_next_area = 0x30000000;

/* A mapped 24-byte control block, zeroed: the same state a BSS sys_lwmutex_t
 * has before anybody creates it. */
uint32_t alloc_control() {
    const uint32_t a = g_next_area;
    if (!vm::map(a, 0x10000)) return 0;
    g_next_area += 0x10000;
    std::memset(vm::ptr(a), 0, 24);
    return a;
}

struct Result {
    int64_t rc;
    uint32_t out_id;
    uint32_t control[6];
};

/* One _sys_lwmutex_create through the real handler. */
Result do_create(uint32_t protocol, uint32_t control, int32_t has_name, uint64_t name) {
    ppu::Context ctx{};
    const uint32_t out = alloc_control();
    ctx.gpr[3] = out;
    ctx.gpr[4] = protocol;
    ctx.gpr[5] = control;
    ctx.gpr[6] = static_cast<uint32_t>(has_name);
    ctx.gpr[7] = name;
    const int64_t rc = lv2::lwmutex_syscall_create(ctx, nullptr);
    Result r{};
    r.rc = rc;
    r.out_id = vm::range_mapped(out, 4) ? vm::read32(out) : 0;
    for (int i = 0; i < 6; i++)
        r.control[i] = (control && vm::range_mapped(control + i * 4, 4)) ? vm::read32(control + i * 4) : 0;
    return r;
}

}  // namespace

int main()
{
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* 1. PROTOCOL VALIDATION. 0x1/0x2/0x4 are the three an lwmutex accepts. */
    {
        const uint32_t c = alloc_control();
        for (uint32_t p : {0x1u, 0x2u, 0x4u}) {
            const Result r = do_create(p, c, -1, 0x0102030405060708ull);
            std::fprintf(stderr, "  protocol 0x%x -> rc 0x%llx id 0x%08x\n", p,
                         static_cast<unsigned long long>(r.rc), r.out_id);
            CHECK(r.rc == 0, "a valid protocol must return CELL_OK");
            CHECK((r.out_id >> 24) == 0x95, "the minted id must be an lwmutex id (0x95......)");
        }
        /* 0x3 is SYS_SYNC_PRIORITY_INHERIT, which is a MUTEX protocol and not an
         * lwmutex one; 0x0, 0x5 and 0xff are simply unknown. All must be EINVAL
         * and must mint nothing. */
        for (uint32_t p : {0x0u, 0x3u, 0x5u, 0xffu}) {
            const Result r = do_create(p, c, -1, 0);
            std::fprintf(stderr, "  protocol 0x%x -> rc 0x%llx out 0x%08x\n", p,
                         static_cast<unsigned long long>(r.rc), r.out_id);
            CHECK(r.rc == static_cast<int32_t>(0x80010002),
                  "an unknown lwmutex protocol must return CELL_EINVAL");
            CHECK(r.out_id == 0, "a refused create must not write an id to *out");
        }
    }

    /* 2. HAS_NAME. Negative keeps the name, non-negative zeroes it. */
    {
        const uint32_t c = alloc_control();
        constexpr uint64_t kName = 0x6e616d6531323334ull;  /* "name1234" */
        const Result neg = do_create(0x1, c, -1, kName);
        CHECK(neg.rc == 0, "create with has_name<0 must succeed");
        const Result pos = do_create(0x1, c, 1, kName);
        CHECK(pos.rc == 0, "create with has_name>0 must succeed");
        const Result zero = do_create(0x1, c, 0, kName);
        CHECK(zero.rc == 0, "create with has_name==0 must succeed");
        /* RPCS3 sys_lwmutex.cpp:41 keeps the name only when has_name is negative. */
        const std::string n_neg = lv2::lwmutex_name_of(neg.out_id);
        const std::string n_pos = lv2::lwmutex_name_of(pos.out_id);
        const std::string n_zero = lv2::lwmutex_name_of(zero.out_id);
        std::fprintf(stderr, "  has_name<0 name='%s' has_name>0 name='%s' has_name=0 name='%s'\n",
                     n_neg.c_str(), n_pos.c_str(), n_zero.c_str());
        CHECK(n_neg == "name1234", "has_name<0 must KEEP the name (RPCS3 sys_lwmutex.cpp:41)");
        CHECK(n_pos.empty(), "has_name>0 must DROP the name (RPCS3 sys_lwmutex.cpp:41)");
        CHECK(n_zero.empty(), "has_name==0 must DROP the name (RPCS3 sys_lwmutex.cpp:41)");
        /* The ids must all differ: three creates mint three objects. */
        CHECK(neg.out_id != pos.out_id && pos.out_id != zero.out_id && neg.out_id != zero.out_id,
              "each create must mint a distinct id");
    }

    /* 3. THE CONTROL BLOCK IS THE WRAPPER'S. The syscall writes *out and nothing
     *    into the guest structure; a syscall that also wrote it would race the
     *    firmware wrapper that does so after the sc. */
    {
        const uint32_t c = alloc_control();
        const uint32_t before[6] = {0xAAAAAAAAu, 0xBBBBBBBBu, 0xCCCCCCCCu,
                                    0xDDDDDDDDu, 0xEEEEEEEEu, 0xFFFFFFFFu};
        for (int i = 0; i < 6; i++) vm::write32(c + i * 4, before[i]);
        const Result r = do_create(0x1, c, -1, 0);
        CHECK(r.rc == 0, "create over a pre-filled control block must succeed");
        for (int i = 0; i < 6; i++) {
            std::fprintf(stderr, "  control +0x%02x: 0x%08x -> 0x%08x\n", i * 4, before[i], r.control[i]);
            CHECK(r.control[i] == before[i],
                  "_sys_lwmutex_create must not write the guest control block");
        }
    }

    std::printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
