#pragma once
/* Sysutil event delivery, used by modules that raise system commands
 * (cellNetCtl's connect dialog being the one that matters here). */
#include <cstdint>
#include <functional>

namespace ppu {
struct Context;
}

namespace hle {

uint32_t callback_function(int slot);
uint32_t callback_arg(int slot);
void sysutil_post(std::function<void(ppu::Context&)> fn);

}  // namespace hle
