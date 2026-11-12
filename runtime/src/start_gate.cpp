#include "runtime/start_gate.h"

#include <chrono>
#include <cstdlib>

namespace start_gate {

bool enabled_value(const char* value) {
    if (!value || !*value) return true;
    return value[0] != '0';
}

bool enabled() {
    static const bool on = enabled_value(std::getenv("PS3BASE_PPU_START_GATE"));
    return on;
}

bool takes_precedence(int32_t child_prio, int32_t creator_prio) {
    return child_prio < creator_prio;
}

uint64_t budget_us() { return 5000; }

uint64_t clock_us() {
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

}
