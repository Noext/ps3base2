#pragma once
#include <cstdint>
namespace force {
void on_return(uint32_t fn_addr, uint32_t lr_addr, uint64_t& r3);
void on_call(uint32_t fn_addr);
}
