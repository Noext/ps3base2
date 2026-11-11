/* Every error code this runtime returns, checked against RPCS3's ErrorCodes.h.
 *
 * These numbers are the whole interface between the runtime and the guest: a
 * wrong code does not crash, it makes the title take a branch meant for a
 * different failure. That is how CELL_ENOTSUP ended up as 0x8001003D, which is
 * CELL_ENOLICDISC -- a title asking "is this supported" was told "your disc
 * licence is wrong" and the two were indistinguishable in the log.
 *
 * The expected values are transcribed once, here, from
 * refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h. Any change to runtime/kernel.h that
 * disagrees with this table fails the test.
 */
#include "runtime/kernel.h"

#include <cstdio>
#include <cstdint>

namespace {

int g_checks = 0;
int g_fail = 0;

void check(int32_t authority, int32_t ours, const char* name, int line) {
    ++g_checks;
    if (authority != ours) {
        ++g_fail;
        std::printf("[FAIL] %s: runtime has 0x%08X, RPCS3 ErrorCodes.h:%d says 0x%08X\n",
                    name, static_cast<uint32_t>(ours), line, static_cast<uint32_t>(authority));
    }
}

/* RPCS3 line | name | value, in the order they appear there. */
#define CASES(X) \
    X(16,  CELL_OK,        0x00000000) \
    X(12,  CELL_EINVAL,    0x80010002) \
    X(16,  CELL_ENOENT,    0x80010006) \
    X(111, CELL_EDEADLK,   0x80010008) \
    X(113, CELL_EBUSY,     0x8001000A) \
    X(167, CELL_EBADF,     0x8001002A) \
    X(23,  CELL_EFAULT,    0x8001000D) \
    X(26,  CELL_EEXIST,    0x80010014) \
    X(144, CELL_EACCES,    0x80010029) \
    X(158, CELL_ENOTSUP,   0x80010037) \
    X(164, CELL_ENOLICDISC,0x8001003D) \

}  // namespace

int main() {
    std::printf("error codes pinned to refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h\n");

#define CHECK_CASE(line, name, value) \
    { \
        constexpr int32_t authority = static_cast<int32_t>(value##u); \
        check(authority, static_cast<int32_t>(lv2::name), #name, line); \
    }
    CASES(CHECK_CASE)
#undef CHECK_CASE

    /* The two that were confused. Named separately so the diff is obvious when
     * one drifts back. */
    ++g_checks;
    if (static_cast<uint32_t>(lv2::CELL_ENOTSUP) == 0x8001003Du) {
        ++g_fail;
        std::printf("[FAIL] CELL_ENOTSUP is CELL_ENOLICDISC (0x8001003D) again\n");
    }

    std::printf("error_codes: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}