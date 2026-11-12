#include "runtime/loader.h"
#include "runtime/log.h"

#include <cstring>

/* PT_PROC_PARAM (Elf64_Phdr p_type 0x60000001, "LOOS+0x1"). Kept apart from
 * loader.cpp on purpose: loader.cpp pulls in the image table, the VFS and the
 * guest memory map, and this is the one piece of it a test can drive from the
 * three titles' real section bytes with no game present. */
namespace loader {

namespace {

uint32_t be32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return __builtin_bswap32(v); }

}

bool decode_proc_param(const uint8_t* p, size_t n, ProcParam& out) {
    if (n < 4) return false;
    out = ProcParam{};
    out.size = be32(p);
    out.size_short = out.size < 32;
    if (n < 32) return true;   /* size only; every other field stays 0 */
    uint32_t f[8];
    std::memcpy(f, p, 32);
    for (uint32_t& v : f) v = __builtin_bswap32(v);
    out.magic = f[1];
    out.version = f[2];
    out.sdk_version = f[3];
    out.primary_prio = static_cast<int32_t>(f[4]);
    out.primary_stacksize = f[5];
    out.malloc_pagesize = f[6];
    out.ppc_seg = f[7];
    out.magic_ok = out.magic == 0x13bcc5f6u;
    return true;
}

void apply_proc_param(ProcParam& pp, ProcessInfo& info) {
    if (pp.size_short) {
        LOG_WARN("loader", "PT_PROC_PARAM size 0x%x is shorter than the 0x20 byte structure; only the fields that exist are used", pp.size);
    }
    if (!pp.magic_ok) {
        /* RPCS3 refuses the whole section on a bad magic (PPUModule.cpp:2205);
         * mirroring that is the point -- a misparsed section must not silently
         * become the title's priority, and the priority is what every child is
         * compared against by the start gate. */
        LOG_ERROR("loader", "PT_PROC_PARAM bad magic 0x%08x, expected 0x13bcc5f6; section ignored", pp.magic);
        return;
    }
    info.sdk_version = pp.sdk_version;
    /* refs/rpcs3/rpcs3/Emu/Cell/PPUModule.cpp:2210-2213. ps3base answers no
     * debug/root permission anywhere, so the floor is 0 and not -512. */
    if (pp.primary_prio < 3072 && pp.primary_prio >= 0) {
        info.primary_prio = pp.primary_prio;
        pp.prio_accepted = true;
    } else {
        LOG_WARN("loader", "PT_PROC_PARAM primary_prio %d is outside 0..3071; keeping %d", pp.primary_prio, info.primary_prio);
    }
    info.primary_stacksize = pp.primary_stacksize;
    info.malloc_pagesize = pp.malloc_pagesize;
    LOG_INFO("loader", "PT_PROC_PARAM size 0x%x version 0x%x sdk 0x%x primary_prio %d stack 0x%x malloc 0x%x ppc_seg 0x%x",
             pp.size, pp.version, pp.sdk_version, info.primary_prio, pp.primary_stacksize, pp.malloc_pagesize, pp.ppc_seg);
}

}
