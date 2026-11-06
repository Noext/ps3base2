#pragma once

#include "runtime/hle.h"
#include "runtime/log.h"
#include "runtime/memory.h"
#include "ppu/ops.h"

#include <cstring>
#include <string>

namespace hle {

inline uint32_t a32(ppu::Context& c, int n) { return static_cast<uint32_t>(c.gpr[3 + n]); }
inline uint64_t a64(ppu::Context& c, int n) { return c.gpr[3 + n]; }
inline int32_t s32(ppu::Context& c, int n) { return static_cast<int32_t>(c.gpr[3 + n]); }
inline void ret(ppu::Context& c, int64_t v) { c.gpr[3] = static_cast<uint64_t>(v); }

inline std::string gstr(uint32_t addr, size_t max = 1024) {
    if (!addr) return {};
    /* The title sometimes passes a pointer into a region it has not mapped yet,
     * and strnlen then walks off the end and faults. Walk only while the bytes
     * stay mapped. */
    std::string out;
    for (size_t i = 0; i < max; i++) {
        if (!vm::range_mapped(addr + static_cast<uint32_t>(i), 1)) break;
        const char c = static_cast<char>(vm::ptr(addr)[i]);
        if (!c) break;
        out.push_back(c);
    }
    return out;
}

inline void put_str(uint32_t addr, const std::string& s, size_t cap) {
    if (!addr || !cap) return;
    size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(vm::ptr(addr), s.data(), n);
    vm::ptr(addr)[n] = 0;
}

/* A guest callback with up to five arguments. The fifth is real: a trophy
 * status callback takes (context, status, completed, total, arg), and dropping
 * the last one handed the title a pointer it had not supplied. */
void call_guest(ppu::Context& ctx, uint32_t opd, uint64_t a0 = 0, uint64_t a1 = 0,
                uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0);

}
