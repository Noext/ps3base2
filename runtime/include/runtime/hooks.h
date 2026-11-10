#pragma once

#include <cstdint>
#include <string>

namespace ppu {
struct Context;
}

namespace hooks {

void init();
const std::string& name_of(uint32_t addr);
void capture(const char* reason);
void tick_patches();

/* Fire `guard` at the entry of the lifted function at `addr`. Returns false to
 * continue normally. Used by diagnostics that need the live context of a specific
 * function: which arguments the guest passed and who called it. */
void add_guard(uint32_t addr, bool (*guard)(ppu::Context&));
bool fps_unlock_active();
double fps_unlock_target();
bool fps_unlock_timestep(ppu::Context& ctx);

}
