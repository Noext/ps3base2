#pragma once

/* A ring of the last cellFiber events, dumped by the crash handler.
 *
 * Why a ring and not a log: the crash this exists to explain takes 43 s to
 * reach, and logging every cellFiber import to the log file produced 50 000
 * lines in 36 s -- which both slowed the run and buried the events that matter.
 * The ring keeps the last 512 events in memory and the crash handler prints
 * them, so the lines immediately before the fault are the ones that survive.
 */
#include <cstdint>

namespace fibertrace {

enum class Kind : uint8_t {
    Enter = 0,   /* a cellFiber import was entered */
    Leave = 1,   /* it returned */
    Switch = 2,  /* the lifted libfiber context switch/restore was entered */
};

struct Event {
    double   t;        /* uptime, seconds */
    uint32_t thread;   /* guest thread id */
    uint32_t lr;       /* guest lr at the event (caller's site for an import) */
    uint32_t r1;       /* guest stack pointer at the event */
    uint64_t r3;       /* argument 0 / return value */
    uint32_t addr;     /* the guest address of the event */
    uint8_t  kind;
    bool     switched; /* Leave only: r1 at leave differs from r1 at enter */
    char     name[40];
};

/* True when PS3BASE_FIBER_TRACE is set. Read once. */
bool enabled();

/* Record one event. */
void note(Kind k, uint32_t thread, uint32_t addr, const char* name, uint32_t lr,
          uint32_t r1, uint64_t r3, bool switched = false);

/* This thread's current cellFiber import nesting depth, 0 when not inside one. */
int depth();

/* Write the whole ring to the log, oldest first. The first caller wins. Called
 * from the crash handler only. */
void dump(const char* why);

}  // namespace fibertrace
