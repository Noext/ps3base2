#include "common.h"
#include "lwmutex_hle.h"

/* sysPrxForUser: the userspace half of the synchronisation primitives.
 *
 * These are NOT the _sys_lwmutex_* syscalls. They take a pointer to a
 * cell_lwmutex structure in guest memory. The table, the ownership rules and
 * the destroy/reap protocol live in lwmutex_hle.h, so tests/lwmutex_hle.cpp
 * drives the identical code rather than a copy of it.
 *
 * The reference documents the trap this title falls into: a mutex that was
 * statically initialised in .data (attribute set, sleep_queue still 0) has never
 * been through the create import. Locking one must serialise the first use
 * rather than fail, which "entry_for" does -- registering on first use.
 */

#include "runtime/thread.h"

namespace hle {
namespace lwmutex {

uint32_t me() {
    guest::Thread* t = guest::current();
    return t ? t->id : 0;
}

}  // namespace lwmutex

/* Every import records its caller so PS3BASE_LWMUX_TRACE can name the guest
 * site; the lr is the only thing that ties an operation to its code path. */
HLE_FUNC(sysPrxForUser, sys_lwmutex_lock) {
    hle::lwmutex::caller_lr() = static_cast<uint32_t>(ctx.lr);
    ret(ctx, hle::lwmutex::lock(a32(ctx, 0)));
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_unlock) {
    hle::lwmutex::caller_lr() = static_cast<uint32_t>(ctx.lr);
    ret(ctx, hle::lwmutex::unlock(a32(ctx, 0)));
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_destroy) {
    hle::lwmutex::caller_lr() = static_cast<uint32_t>(ctx.lr);
    ret(ctx, hle::lwmutex::destroy(a32(ctx, 0)));
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_create) {
    ret(ctx, hle::lwmutex::create(a32(ctx, 0), a32(ctx, 1)));
}

HLE_FUNC(sysPrxForUser, sys_lwmutex_trylock) {
    ret(ctx, hle::lwmutex::trylock(a32(ctx, 0)));
}

}  // namespace hle
