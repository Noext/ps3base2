#pragma once

#include <cstdint>

namespace lv2 {

int64_t event_flag_set_bits(uint32_t id, uint64_t bits);
int64_t event_flag_set_bits_traced(uint32_t id, uint64_t bits, uint32_t from_lr, const char* who);

}
