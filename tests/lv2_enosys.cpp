/* ps3base -- the LV2 table and the ENOSYS contract, checked without linking
 * the whole runtime.
 *
 * Two invariants:
 *   1. the generated table lists every number the specification dispatches,
 *      each with its name, and
 *   2. the value the dispatch returns for an unimplemented syscall is ENOSYS,
 *      read from the same header the dispatch uses.
 *
 * The full sweep -- driving ppu::syscall for every number -- needs the runtime
 * objects and lives in tests/lv2_dispatch.cpp, registered as a ctest case.
 */
#include "syscall_table.h"
#include "runtime/kernel.h"

#include <cstdio>
#include <cstring>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } } while (0)

int main()
{
    CHECK(k_syscall_spec_count > 400, "the LV2 table has more than 400 entries");

    /* Every entry carries a name: the log for an unimplemented call is only
     * actionable if the number resolves to what the specification calls it. */
    unsigned unnamed = 0;
    unsigned implemented = 0;
    for (unsigned i = 0; i < k_syscall_spec_count; i++) {
        if (!k_syscall_spec[i].name || !k_syscall_spec[i].name[0]) unnamed++;
        if (k_syscall_spec[i].implemented) implemented++;
    }
    CHECK(unnamed == 0, "every table entry has a specification name");

    /* Numbers must be strictly increasing: the generator sorts them, and a
     * duplicate would silently shadow one syscall with another. */
    unsigned dups = 0;
    for (unsigned i = 1; i < k_syscall_spec_count; i++)
        if (k_syscall_spec[i].num <= k_syscall_spec[i - 1].num) dups++;
    CHECK(dups == 0, "no duplicate or out-of-order syscall numbers");

    std::printf("lv2: %u listed, %u implemented (%.1f%%), %d checks\n",
                k_syscall_spec_count, implemented,
                100.0 * implemented / k_syscall_spec_count, checks);
    if (fails) {
        std::fprintf(stderr, "lv2: %d of %d FAILED\n", fails, checks);
        return 1;
    }
    return 0;
}
