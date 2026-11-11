#include "common.h"
#include "runtime/cbwatch.h"
#include "runtime/thread.h"

#include <map>

namespace hle {

namespace {

std::map<std::string, bool (*)()>& overrides() {
    static std::map<std::string, bool (*)()> m;
    return m;
}

}

ModuleOverride::ModuleOverride(const char* file, bool (*active)()) { overrides()[file] = active; }

bool module_overridden(const std::string& file) {
    auto it = overrides().find(file);
    return it != overrides().end() && it->second();
}

void call_guest(ppu::Context& ctx, uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2,
                uint64_t a3, uint64_t a4) {
    /* Every delivery of a guest callback passes through here, which makes this
     * the one place that can count them against the registrations. */
    cbwatch::note_call("hle", opd, a1);
    ppu::Context saved = ctx;
    ctx.gpr[1] = (ctx.gpr[1] - 0x200) & ~0xFull;
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[5] = a2;
    ctx.gpr[6] = a3;
    ctx.gpr[7] = a4;
    guest::call_opd(ctx, vm::g_base, opd);
    uint64_t r3 = ctx.gpr[3];
    ctx = saved;
    ctx.gpr[3] = r3;
}

}

/* DELIBERATELY ABSENT: an override forcing libsysutil.sprx to HLE.
 *
 * It was tried and refuted. With the override the title flips (2341 frames in
 * 40 s) but calls 230 exported C++ symbols the HLE does not provide --
 * cxml::Document, sysutil_cxmlutil::FixedMemory, cellSysutilPacketBegin/End --
 * so it renders nothing: flip 97.3%, draw 0.2%, black screen. libsysutil is a
 * library of 230 exports, not a dispatcher the HLE can stand in for.
 *
 * So the module stays LLE (D-001), and the deadlock that appears without it is
 * somewhere else. See docs/progress.md for where that search stands. */

