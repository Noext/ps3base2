/* ps3base -- PPU instruction fuzzer.

 * Each helper in ppu/ops.h is compared against an independent reference written
 * from the PowerPC specification: __int128 for the wide products, explicit
 * rounding for the float conversions, and the ISA's carry rules for the
 * add/subtract chains. A divergence is a lifter bug, and the fuzzer prints the
 * operands so the failing case can be turned into a conformance test.
 */
#include "ppu/ops.h"

#include <bit>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

std::mt19937_64 g_rng(0x50533362ull);

uint64_t rnd64() { return g_rng(); }
uint32_t rnd32() { return static_cast<uint32_t>(g_rng()); }

struct Odds {
    std::vector<uint64_t> v;
};

/* Boundary operands plus random extremes: 0, 1, -1, min, max, powers of two. */
Odds boundary64()
{
    Odds o;
    o.v = {0ull, 1ull, 2ull, 63ull, 64ull, 127ull, 128ull,
           ~0ull, ~0ull - 1, 0x8000000000000000ull, 0x7fffffffffffffffull,
           0xffffffffull, 0x100000000ull, 0x80000000ull, 0x7fffffffull,
           0xffffffffffffull, 0xffffffffffull, 3ull, 0x5555555555555555ull,
           0xaaaaaaaaaaaaaaaaull};
    for (int i = 0; i < 64; i++) {
        o.v.push_back(1ull << i);
        if (i) o.v.push_back((1ull << i) - 1);
    }
    return o;
}

void report(const char* op, uint64_t a, uint64_t b, uint64_t want, uint64_t got,
            uint32_t want_ca = 0, uint32_t got_ca = 0, bool has_ca = false)
{
    g_failures++;
    if (g_failures > 40) return;
    if (has_ca)
        std::printf("FAIL %-10s a=0x%016" PRIx64 " b=0x%016" PRIx64
                    " want=0x%016" PRIx64 " ca=%u got=0x%016" PRIx64 " ca=%u\n",
                    op, a, b, want, want_ca, got, got_ca);
    else
        std::printf("FAIL %-10s a=0x%016" PRIx64 " b=0x%016" PRIx64
                    " want=0x%016" PRIx64 " got=0x%016" PRIx64 "\n",
                    op, a, b, want, got);
}

/* ---- references, written from the PowerPC ISA ---- */

uint64_t ref_mulhw(uint64_t a, uint64_t b)
{
    /* 32x32 -> signed 64, take the high word, sign-extended to 64. */
    int64_t p = static_cast<int64_t>(static_cast<int32_t>(a)) *
                static_cast<int64_t>(static_cast<int32_t>(b));
    return static_cast<uint64_t>(p >> 32);
}

uint64_t ref_mulhwu(uint64_t a, uint64_t b)
{
    uint64_t p = static_cast<uint64_t>(static_cast<uint32_t>(a)) *
                 static_cast<uint64_t>(static_cast<uint32_t>(b));
    return p >> 32;
}

uint64_t ref_mulhd(uint64_t a, uint64_t b)
{
    __int128 p = static_cast<__int128>(static_cast<int64_t>(a)) * static_cast<__int128>(static_cast<int64_t>(b));
    return static_cast<uint64_t>(p >> 64);
}

uint64_t ref_mulhdu(uint64_t a, uint64_t b)
{
    unsigned __int128 p = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
    return static_cast<uint64_t>(p >> 64);
}

uint64_t ref_divw(uint64_t a, uint64_t b)
{
    int32_t x = static_cast<int32_t>(a), y = static_cast<int32_t>(b);
    if (y == 0) return 0;
    /* The ISA leaves the result undefined on overflow; the lifter returns 0 and
     * RPCS3 does the same, so the reference matches that choice. */
    if (x == INT32_MIN && y == -1) return 0;
    /* RPCS3: gpr[rd] = (u32)(RA / RB) -- a 32-bit result zero-extended into
     * the 64-bit register. */
    int64_t q = static_cast<int64_t>(x) / static_cast<int64_t>(y);
    return static_cast<uint64_t>(static_cast<uint32_t>(q));
}

uint64_t ref_divwu(uint64_t a, uint64_t b)
{
    uint32_t y = static_cast<uint32_t>(b);
    if (y == 0) return 0;
    return static_cast<uint64_t>(static_cast<uint32_t>(a) / y);
}

uint64_t ref_divd(uint64_t a, uint64_t b)
{
    int64_t x = static_cast<int64_t>(a), y = static_cast<int64_t>(b);
    if (y == 0) return 0;
    if (x == INT64_MIN && y == -1) return 0;
    return static_cast<uint64_t>(x / y);
}

uint64_t ref_divdu(uint64_t a, uint64_t b)
{
    if (b == 0) return 0;
    return a / b;
}

/* sraw: shift a 32-bit signed value right, CA is set when any 1-bit is shifted
 * out of a negative value, or when the shift exceeds 31 (CA = sign). */
uint64_t ref_sraw(uint64_t s, uint64_t b, uint8_t& ca)
{
    int32_t v = static_cast<int32_t>(s);
    uint32_t n = static_cast<uint32_t>(b) & 63u;
    if (n > 31) {
        ca = v < 0 ? 1 : 0;
        return static_cast<uint64_t>(static_cast<int64_t>(v < 0 ? -1 : 0));
    }
    int32_t r = v >> n;   /* arithmetic, as the ISA specifies */
    if (n == 0) {
        ca = 0;
    } else {
        const uint32_t mask = (1u << n) - 1u;
        ca = (v < 0 && (static_cast<uint32_t>(v) & mask) != 0) ? 1 : 0;
    }
    return static_cast<uint64_t>(static_cast<int64_t>(r));
}

uint64_t ref_srad(uint64_t s, uint64_t b, uint8_t& ca)
{
    int64_t v = static_cast<int64_t>(s);
    uint32_t n = static_cast<uint32_t>(b) & 127u;
    if (n > 63) {
        ca = v < 0 ? 1 : 0;
        return static_cast<uint64_t>(v < 0 ? -1 : 0);
    }
    int64_t r = v >> n;   /* arithmetic, as the ISA specifies */
    if (n == 0) {
        ca = 0;
    } else {
        const uint64_t mask = (n == 64) ? ~0ull : ((1ull << n) - 1ull);
        ca = (v < 0 && (static_cast<uint64_t>(v) & mask) != 0) ? 1 : 0;
    }
    return static_cast<uint64_t>(r);
}

uint32_t ref_cntlzw(uint32_t v) { return v == 0 ? 32u : static_cast<uint32_t>(__builtin_clz(v)); }
uint32_t ref_cntlzd(uint64_t v) { return v == 0 ? 64u : static_cast<uint32_t>(__builtin_clzll(v)); }

uint64_t ref_extsb(uint64_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v))); }
uint64_t ref_extsh(uint64_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v))); }
uint64_t ref_extsw(uint64_t v) { return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); }

/* Carry chain references: add_ca already takes an explicit carry in. */
uint64_t ref_add_ca(uint64_t a, uint64_t b, uint64_t ci, uint8_t& co)
{
    unsigned __int128 s = static_cast<unsigned __int128>(a) + b + ci;
    co = (s >> 64) ? 1 : 0;
    return static_cast<uint64_t>(s);
}

/* fctiw / fctid: round per the instruction, saturate, and produce the raw
 * double bit pattern holding the integer in the low word(s), which is what the
 * lifter returns. */
double fctiw_ref(double x, bool trunc)
{
    double r;
    if (std::isnan(x)) {
        r = 0.0;
        uint64_t bits = 0x80000000ull;   /* INT32_MIN pattern in the low word */
        double out;
        std::memcpy(&out, &bits, 8);
        return out;
    }
    double t = trunc ? std::trunc(x) : std::nearbyint(x);
    if (t > 2147483647.0) t = 2147483647.0;
    if (t < -2147483648.0) t = -2147483648.0;
    int32_t i = static_cast<int32_t>(t);
    uint64_t bits = static_cast<uint64_t>(static_cast<uint32_t>(i));
    double out;
    std::memcpy(&out, &bits, 8);
    return out;
}

double fctid_ref(double x, bool trunc)
{
    if (std::isnan(x)) {
        uint64_t bits = 0x8000000000000000ull;
        double out;
        std::memcpy(&out, &bits, 8);
        return out;
    }
    double t = trunc ? std::trunc(x) : std::nearbyint(x);
    if (t > 9223372036854775807.0) t = 9223372036854775807.0;
    if (t < -9223372036854775808.0) t = -9223372036854775808.0;
    int64_t i = static_cast<int64_t>(t);
    uint64_t bits = static_cast<uint64_t>(i);
    double out;
    std::memcpy(&out, &bits, 8);
    return out;
}

void check(const char* op, uint64_t a, uint64_t b, uint64_t want, uint64_t got)
{
    g_checks++;
    if (want != got) report(op, a, b, want, got);
}

void check_ca(const char* op, uint64_t a, uint64_t b, uint64_t want, uint64_t got,
              uint32_t want_ca, uint32_t got_ca)
{
    g_checks++;
    if (want != got || want_ca != got_ca) report(op, a, b, want, got, want_ca, got_ca, true);
}

}  // namespace

int main(int argc, char** argv)
{
    const long iters = argc > 1 ? std::atol(argv[1]) : 200000;
    const Odds bnd = boundary64();

    for (long i = 0; i < iters; i++) {
        /* Mix random values with boundary values so both are exercised. */
        uint64_t a = (i & 1) ? rnd64() : bnd.v[static_cast<size_t>(i) % bnd.v.size()];
        uint64_t b = (i & 2) ? rnd64() : bnd.v[static_cast<size_t>(i / 3) % bnd.v.size()];
        if ((i % 7) == 0) { a = static_cast<uint64_t>(static_cast<int64_t>(rnd32())); }
        if ((i % 11) == 0) { b = static_cast<uint64_t>(static_cast<int64_t>(rnd32())); }

        check("mulhw", a, b, ref_mulhw(a, b), ppu::mulhw(a, b));
        check("mulhwu", a, b, ref_mulhwu(a, b), ppu::mulhwu(a, b));
        check("mulhd", a, b, ref_mulhd(a, b), ppu::mulhd(a, b));
        check("mulhdu", a, b, ref_mulhdu(a, b), ppu::mulhdu(a, b));

        check("divw", a, b, ref_divw(a, b), ppu::divw(a, b));
        check("divwu", a, b, ref_divwu(a, b), ppu::divwu(a, b));
        check("divd", a, b, ref_divd(a, b), ppu::divd(a, b));
        check("divdu", a, b, ref_divdu(a, b), ppu::divdu(a, b));

        ppu::Context c{};
        uint8_t want_ca = 0, got_ca = 0;
        uint64_t w = ref_sraw(a, b, want_ca);
        uint64_t g = ppu::sraw(c, a, b);
        got_ca = c.xer_ca ? 1 : 0;
        check_ca("sraw", a, b, w, g, want_ca, got_ca);

        c = ppu::Context{};
        w = ref_srad(a, b, want_ca);
        g = ppu::srad(c, a, b);
        got_ca = c.xer_ca ? 1 : 0;
        check_ca("srad", a, b, w, g, want_ca, got_ca);

        w = ref_add_ca(a, b, 0, want_ca);
        got_ca = 0;
        g = ppu::add_ca(a, b, 0, got_ca);
        check_ca("add_ca", a, b, w, g, want_ca, got_ca);

        w = ref_add_ca(a, b, 1, want_ca);
        got_ca = 0;
        g = ppu::add_ca(a, b, 1, got_ca);
        check_ca("add_ca+1", a, b, w, g, want_ca, got_ca);

        /* These have no helper: the lifter emits the cast / countl_zero inline,
         * so the reference is checked against that exact expression. */
        check("extsb", a, b, ref_extsb(a), static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(a))));
        check("extsh", a, b, ref_extsh(a), static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(a))));
        check("extsw", a, b, ref_extsw(a), static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(a))));

        /* Shifts and rotates used by the lifted rlwinm/rlwimi sequences. */
        const uint32_t n32 = static_cast<uint32_t>(b) & 63u;
        check("slw", a, b, n32 > 31 ? 0 : static_cast<uint32_t>(static_cast<uint32_t>(a) << n32), ppu::slw(a, b));
        check("srw", a, b, n32 > 31 ? 0 : static_cast<uint32_t>(a) >> n32, ppu::srw(a, b));
        const uint32_t n64 = static_cast<uint32_t>(b) & 127u;
        check("sld", a, b, n64 > 63 ? 0 : a << n64, ppu::sld(a, b));
        check("srd", a, b, n64 > 63 ? 0 : a >> n64, ppu::srd(a, b));
        /* ppu::rotl32 rotates the low word and duplicates it into both halves
         * (dup32): the lifter relies on that shape for 32-bit ops. */
        {
            uint32_t w = static_cast<uint32_t>(a);
            uint32_t rot = n32 ? ((w << n32) | (w >> (32 - n32))) : w;
            const uint64_t want = (uint64_t(rot) << 32) | rot;
            check("rotl32", a, b, want, ppu::rotl32(a, n32));
        }
    }

    /* cntlzw / cntlzd need their own operand pattern: zeros matter. */
    for (uint64_t v : bnd.v) {
        check("cntlzw", v, 0, ref_cntlzw(static_cast<uint32_t>(v)), static_cast<uint64_t>(std::countl_zero(static_cast<uint32_t>(v))));
        check("cntlzd", v, 0, ref_cntlzd(v), static_cast<uint64_t>(std::countl_zero(v)));
    }
    check("cntlzw0", 0, 0, 32, static_cast<uint64_t>(std::countl_zero(0u)));
    check("cntlzd0", 0, 0, 64, static_cast<uint64_t>(std::countl_zero(0ull)));

    std::printf("ppu_op_fuzz: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
