#pragma once

#include "object.h"

#include <deque>

namespace lv2 {

struct Event {
    uint64_t source;
    uint64_t data1;
    uint64_t data2;
    uint64_t data3;
};

struct EventQueue : Object {
    std::mutex m;
    std::condition_variable cv;
    std::deque<Event> events;
    uint32_t size = 127;
    int32_t type = 1;
    uint64_t key = 0;
    bool destroyed = false;
    uint32_t waiters = 0;
    /* N5b: how many events were pushed into this queue and how many were taken
     * out of it by a receive. A push without a matching receive leaves the queue
     * full and the sender's later sends return CELL_EBUSY; a receive count below
     * the push count is the difference between "the producer sent" and "the
     * consumer woke". Read only by the PS3BASE_QUEUE_COUNTS report. */
    uint64_t delivered = 0;
    uint64_t received = 0;
};

int64_t push_event(const std::shared_ptr<EventQueue>& q, const Event& e);
std::shared_ptr<EventQueue> queue_by_key(uint64_t key);

/* Create an event queue under a global key, as sys_event_queue_create does
 * (refs/rpcs3/rpcs3/Emu/Cell/lv2/sys_event.cpp). Returns 0 when the key is
 * already taken -- the caller's CELL_EEXIST -- and the new queue's guest id
 * otherwise. HLE modules that create their own notify queue need this, because
 * sys_event_queue_create is a guest-facing entry point and they are not guests.
 * cellVoice is its first user (refs/.../Modules/cellVoice.cpp:99). */
uint32_t create_queue_by_key(uint64_t key, uint32_t size, const char* name);

/* Deliver one event to the queue registered under key. False when no queue has
 * that key, which is lv2_event_queue::find returning null in RPCS3
 * (refs/.../Modules/cellVoice.cpp:252, :693, :754). */
bool send_event_by_key(uint64_t key, uint64_t source, uint64_t data1, uint64_t data2, uint64_t data3);

/* N5b: PS3BASE_QUEUE_COUNTS dumps, per queue, how many events were pushed into
 * it and how many a receive took out (runtime/src/lv2/event.cpp). A push count
 * above the receive count is the difference between "the producer sent an event"
 * and "the consumer woke and took it", which is the question a notification
 * queue exists to answer. Called from the process-exit path, because a title
 * that ends by stopping never reaches main()'s summary. */
void queue_counts_report(const char* where);

}
