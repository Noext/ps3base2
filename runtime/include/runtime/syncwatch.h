#pragma once

#include <cstdint>
#include <cstring>
#include <string>

/* PS3BASE_SYNCWATCH: what synchronisation does a thread hold when it writes a
 * watched word?
 *
 * The f_0064c044 crash question is not "which store is wrong" but "is the field
 * written while the guest's own lock is held". Answering it needs, at the moment
 * of a store, the set of LV2 objects the writing guest thread currently owns.
 * Every lock primitive we emulate already knows that; this header collects it in
 * a per-thread stack, which a reporting site can drain in a few instructions.
 *
 * Cheap by construction: a push/pop is a couple of stores into a thread_local
 * array, and nothing is formatted unless a watch actually fires.
 */
namespace syncwatch {

enum Kind : uint8_t {
    K_MUTEX = 1,
    K_LWMUTEX = 2,
    K_RWLOCK_W = 3,
    K_RWLOCK_R = 4,
    K_SEMAPHORE = 5,   /* not ownership; tracked because a semaphore can be the
                        * only thing serialising two writers in a title that
                        * uses counting semaphores as its mutex */
    K_EVENT = 6,
};

constexpr int kMaxHeld = 32;

struct Entry {
    uint8_t kind;
    uint32_t id;
};

inline Entry* stack() {
    static thread_local Entry s[kMaxHeld];
    return s;
}

inline int& depth() {
    static thread_local int d = 0;
    return d;
}

inline void push(uint8_t kind, uint32_t id) {
    int& d = depth();
    if (d >= kMaxHeld) return;
    stack()[d].kind = kind;
    stack()[d].id = id;
    d++;
}

/* Removes the most recent entry of this kind and id. A lock can be released on
 * a path that never took it (refused unlock, destroyed object), so removal is
 * by identity and never pops a different object. */
inline void pop(uint8_t kind, uint32_t id) {
    int& d = depth();
    for (int i = d - 1; i >= 0; i--) {
        if (stack()[i].kind != kind || stack()[i].id != id) continue;
        for (int j = i; j + 1 < d; j++) stack()[j] = stack()[j + 1];
        d--;
        return;
    }
}

inline bool holds(uint8_t kind, uint32_t id) {
    const int d = depth();
    for (int i = 0; i < d; i++)
        if (stack()[i].kind == kind && stack()[i].id == id) return true;
    return false;
}

inline int count() { return depth(); }

/* "lwmutex:0x96001234,mutex:0x85000005" or "-" when the thread owns nothing. */
inline std::string list() {
    const int d = depth();
    if (!d) return "-";
    std::string s;
    char b[40];
    for (int i = 0; i < d; i++) {
        const char* k = "?";
        switch (stack()[i].kind) {
        case K_MUTEX: k = "mutex"; break;
        case K_LWMUTEX: k = "lwmutex"; break;
        case K_RWLOCK_W: k = "rwlock_w"; break;
        case K_RWLOCK_R: k = "rwlock_r"; break;
        case K_SEMAPHORE: k = "sem"; break;
        case K_EVENT: k = "event"; break;
        }
        std::snprintf(b, sizeof b, "%s%s:0x%x", s.empty() ? "" : ",", k, stack()[i].id);
        s += b;
    }
    return s;
}

}  // namespace syncwatch
