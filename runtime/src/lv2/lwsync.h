#pragma once

#include "object.h"

#include <list>
#include <string>

namespace lv2 {

struct LwWaiter {
    uint32_t tid;
    bool woken = false;
    int64_t result = CELL_OK;
};

struct LwMutex : Object {
    std::mutex m;
    std::condition_variable cv;
    int signaled = 0;
    std::list<LwWaiter*> sleepers;
    uint32_t control = 0;
    uint32_t protocol = 0;
    int32_t lwcond_waiters = 0;

    int64_t sleep(std::unique_lock<std::mutex>& lk, LwWaiter& w, uint64_t timeout);
    void release(int64_t result);
};

/* The _sys_lwmutex_* failure totals (ESRCH per syscall, and EPERM), printed at a
 * status dump and at exit. Always counted; only formatted when the probe or a
 * report asks. Defined in lwmutex.cpp. */
void lwmutex_probe_report(const char* where);

/* TEST ENTRY. The syscall handlers are static to lwmutex.cpp; a test that copies
 * a handler's body proves nothing about the body. These two call straight into
 * the real handler and the real object, so tests/lwmutex_create.cpp exercises the
 * shipped code. */
int64_t lwmutex_syscall_create(ppu::Context& ctx, uint8_t* base);
std::string lwmutex_name_of(uint32_t id);


}
