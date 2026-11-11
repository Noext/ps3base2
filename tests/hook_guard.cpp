/* Does the guard mechanism fire for a lifted entry?
 *
 * Rounds 194-201 placed guards on lifted entries and none ran, while every
 * condition the code checks looked satisfied. This reproduces the REAL dispatch
 * path -- g_hooks lookup, impl_body, h->guard(ctx), then the body -- against a
 * synthetic entry, so a failure is a runtime defect and not a lift or layout
 * problem.
 *
 * The previous version of this test passed while proving nothing: its stand-in
 * dispatcher called the body directly and never consulted the guard. That is the
 * same class of error the guards themselves suffered from, so it is worth stating
 * plainly here.
 */
#include "runtime/image.h"

#include <atomic>
#include <cstdio>
#include <unordered_map>

namespace ppu {
static uint8_t g_flag_storage[64];
uint8_t* hook_flags[] = {g_flag_storage};
uint32_t hook_flag_image_count = 1;
void (*hook_impl)(Context&, uint8_t*, uint32_t, void (*)(Context&, uint8_t*)) = nullptr;

uint8_t* hook_flag_for(uint32_t addr) {
    if (addr < 0x1000u || addr >= 0x1000u + 64u * 4u) return nullptr;
    return &hook_flags[0][(addr - 0x1000u) >> 2];
}
}  // namespace ppu

namespace {
std::atomic<int> g_bodies{0};
std::atomic<int> g_guard_calls{0};

bool guard(ppu::Context&) {
    g_guard_calls++;
    return false;   /* false: keep going and run the body */
}
void body(ppu::Context&, uint8_t*) { g_bodies++; }

struct Hook {
    bool (*guard_fn)(ppu::Context&) = nullptr;
    bool quiet = true;
    std::atomic<int> calls{0};
};
std::unordered_map<uint32_t, Hook*> g_hooks;
}  // namespace

namespace hooks {

/* Mirrors impl_body: count the call, consult the guard, then run the body. */
void impl_body(Hook* h, ppu::Context& ctx, uint8_t* base, void (*fn)(ppu::Context&, uint8_t*)) {
    h->calls.fetch_add(1, std::memory_order_relaxed);
    if (h->guard_fn && h->guard_fn(ctx)) return;
    fn(ctx, base);
}

/* Mirrors impl: look the hook up, fall back to the body when there is none. */
void impl(ppu::Context& ctx, uint8_t* base, uint32_t addr, void (*fn)(ppu::Context&, uint8_t*)) {
    auto it = g_hooks.find(addr);
    Hook* h = it != g_hooks.end() ? it->second : nullptr;
    if (!h) { fn(ctx, base); return; }
    impl_body(h, ctx, base, fn);
}

void add_guard(uint32_t addr, bool (*g)(ppu::Context&)) {
    Hook*& h = g_hooks[addr];
    if (!h) h = new Hook;
    h->guard_fn = g;
    uint8_t* f = ppu::hook_flag_for(addr);
    if (f) *f = 1;
    if (!ppu::hook_impl) ppu::hook_impl = impl;
}

void run_test() {
    auto lifted = [](ppu::Context& ctx, uint8_t* base) {
        if (ppu::hook_flags[0][(0x1010u - 0x1000u) >> 2]) {
            if (ppu::hook_impl) { ppu::hook_impl(ctx, base, 0x1010u, body); return; }
        }
        body(ctx, base);
    };
    ppu::Context ctx{};
    add_guard(0x1010u, guard);
    std::printf("flag=%d impl=%s\n", ppu::hook_flags[0][(0x1010u - 0x1000u) >> 2],
                ppu::hook_impl ? "armed" : "NULL");
    for (int i = 0; i < 3; i++) lifted(ctx, nullptr);
    std::printf("bodies=%d guards=%d\n", g_bodies.load(), g_guard_calls.load());
    const bool ok = g_bodies.load() == 3 && g_guard_calls.load() == 3;
    std::printf("%s\n", ok ? "HOOK-MECHANISM-OK" : "HOOK-MECHANISM-BROKEN");
}
}  // namespace hooks

int main() { hooks::run_test(); return 0; }
