/* The three syscalls Red Dead Redemption reaches after the filesystem is
 * satisfied, and why each is answered the way it is.
 *
 * Reached in order: 834 sys_fs_chmod, 815 sys_fs_utime, then these. Each ENOSYS
 * moved the title one step and stopped there, which is how they were found --
 * the log names the syscall, the caller and the argument.
 */
#include "object.h"

#include "runtime/memory.h"
#include "runtime/log.h"

#include <cstdio>

namespace lv2 {
namespace {

/* sys_vm_touch (306): advise the host that a mapped page is about to be touched.
 *
 * This is a hint with no observable effect on a host that has no PS3 memory
 * model: every mapped page is already backed here, and the guest reads the same
 * bytes afterwards either way. Returning CELL_OK is truthful -- the page is
 * genuinely usable -- unlike a stubbed syscall whose work the caller depends on.
 *
 * The bounds are still checked, because an out-of-range touch means the caller is
 * confused about its own mapping and should hear about it. */
int32_t vm_touch_impl(uint64_t start, uint64_t size) {
    if (!size || (start & 0xFFF) || (size & 0xFFF)) return CELL_EINVAL;
    if (!vm::range_mapped(static_cast<uint32_t>(start), size)) return CELL_EFAULT;
    return CELL_OK;
}

}  // namespace

LV2_SYSCALL(306, sys_vm_touch) {
    return vm_touch_impl(u64arg(ctx, 0), u64arg(ctx, 1));
}

/* sys_net_infoctl (722) and sys_net_bnet_sysctl (725): configure the network
 * stack, which this runtime does not provide.
 *
 * Both are refused with CELL_ENOTSUP rather than answered CELL_OK. The title's
 * netThreadPool is what calls them; a fake success would tell it the socket
 * layer is configured and it would then wait for data that never arrives, with
 * nothing in the log to say why. ENOTSUP names the reason at the call site and
 * lets the title skip its network paths -- which is what it does, and the game
 * reaches its renderer without them.
 *
 * The discarded commands are logged once each so the surface stays visible. */
LV2_SYSCALL(722, sys_net_infoctl) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) == 0)
        LOG_WARN("net", "sys_net_infoctl cmd %u refused: no network stack (further calls quiet)",
                 static_cast<unsigned>(u32arg(ctx, 0)));
    return CELL_ENOTSUP;
}

LV2_SYSCALL(725, sys_net_bnet_sysctl) {
    static std::atomic<int> n{0};
    if (n.fetch_add(1) == 0)
        LOG_WARN("net", "sys_net_bnet_sysctl refused: no network stack (further calls quiet)");
    return CELL_ENOTSUP;
}

}  // namespace lv2
