#pragma once

/* WebAssembly has no FCSR: rounding is fixed and there are no FP exception flags.
 * These helpers keep the save/modify/restore pattern compiling and make the wasm
 * divergence explicit rather than scattering #ifdefs through the body. */
#include <cstdint>
#if !defined(__wasm__) && !defined(__EMSCRIPTEN__)
#include <immintrin.h>
#endif
inline unsigned gowa_csr_read() {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    return 0u;
#else
    return _mm_getcsr();
#endif
}
inline void gowa_csr_write(unsigned v) {
#if defined(__wasm__) || defined(__EMSCRIPTEN__)
    (void)v;
#else
    _mm_setcsr(v);
#endif
}

#include "renjob_ops.h"
#include "runtime/jobsys.h"
#include "spu/context.h"

#include <xmmintrin.h>

namespace renjob::wh {
void pm_gettag(renjob::xo::V* R, uint8_t* ls, renjob::xo::Ctl& ctl);
void pm_releasetag(renjob::xo::V* R, uint8_t* ls, renjob::xo::Ctl& ctl);
}

namespace jobsys::jw {

extern thread_local spu::Context* t_ctx;

bool hle_call(renjob::xo::V* R, uint8_t* ls, renjob::xo::Ctl& ctl, uint32_t pc);
void pm_getbuffer(renjob::xo::V* R, uint8_t* ls, renjob::xo::Ctl& ctl);

struct Frame {
    static constexpr unsigned kWant = 0x6000u | 0x8040u;
    unsigned old;
    renjob::xo::Ctl ctl;
    renjob::xo::V* R;
    spu::Context* prev;
    Frame(uint8_t*, spu::Context& c, renjob::ChanHost& h) : old(gowa_csr_read()), R(reinterpret_cast<renjob::xo::V*>(c.gpr)), prev(t_ctx) {
        if ((old & kWant) != kWant) gowa_csr_write(old | kWant);
        ctl.host = &h;
        ctl.ie = &c.interrupts_enabled;
        t_ctx = &c;
    }
    jobsys::WholeResult finish() {
        if (!ctl.fail && (renjob::xo::pref(R[0]) & 0x3fffc) != jobsys::kJobReturn) {
            ctl.fail = true;
            ctl.fail_pc = renjob::xo::pref(R[0]) & 0x3fffc;
        }
        if ((old & kWant) != kWant) gowa_csr_write(old);
        t_ctx = prev;
        jobsys::WholeResult r;
        r.done = !ctl.fail;
        r.fail_pc = ctl.fail_pc;
        r.icall_miss = ctl.icall_miss;
        return r;
    }
};

}
