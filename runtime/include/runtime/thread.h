#pragma once

#include "ppu/context.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace guest {

struct Thread {
    uint32_t id = 0;
    std::string name;
    ppu::Context ctx{};
    uint32_t entry_opd = 0;
    uint32_t stack_addr = 0;
    uint32_t stack_size = 0;
    int32_t prio = 0;
    bool joinable = true;
    bool started = false;
    bool exited = false;
    bool joined = false;
    uint64_t exit_value = 0;
    std::mutex lock;
    std::condition_variable cv;
    std::thread host;
    std::atomic<uint32_t> in_syscall{0};
    std::atomic<uint64_t> syscalls{0};
    uint64_t syscalls_seen = 0;
    uint32_t syscall_hist[1024] = {};
    void* native = nullptr;
    /* The start gate (runtime/include/runtime/start_gate.h). `gate_armed` is set
     * by the creator that is about to wait for this thread; `gate_open` is set
     * by this thread the moment it stops running exclusively -- its first
     * blocking call, its first yield, or its exit. Nothing is stored when no
     * creator is waiting, because these are read on the syscall path. */
    std::atomic<bool> gate_armed{false};
    std::atomic<bool> gate_open{false};
    int32_t gate_creator_prio = 0;
    uint32_t gate_creator_id = 0;
};

Thread* current();
std::shared_ptr<Thread> find(uint32_t id);
std::shared_ptr<Thread> create(const std::string& name, uint32_t entry_opd, uint64_t arg0, uint64_t arg1, int32_t prio, uint32_t stack_size, uint32_t tls, bool joinable);
/* `creator_prio` is the priority of the caller of sys_ppu_thread_start, which is
 * the thread the console makes wait when the child is higher priority
 * (refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_ppu_thread.cpp:578-622). INT32_MIN means
 * "no creator" -- the main thread and host-spawned threads take this path and
 * are never gated. */
void start(const std::shared_ptr<Thread>& t, int32_t creator_prio = INT32_MIN, uint32_t creator_id = 0);

/* Called by a running thread at the first point where it no longer runs
 * exclusively: its first blocking call, its first yield, or its exit. Cheap
 * when no creator armed the gate -- one relaxed load. Inline so a test that
 * stubs guest::current() (tests/lwmutex_contention.cpp) still links. */
inline void open_start_gate(Thread* t) {
    if (t && t->gate_armed.load(std::memory_order_relaxed)) t->gate_open.store(true, std::memory_order_release);
}
std::shared_ptr<Thread> spawn_host(const std::string& name, int32_t prio, uint32_t stack_size, uint32_t tls, std::function<void(Thread&)> fn);
void join_host(const std::shared_ptr<Thread>& t);
void remove(uint32_t id);
void status_dump();
void call_opd(ppu::Context& ctx, uint8_t* base, uint32_t opd);

}
