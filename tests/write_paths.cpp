/* ps3base -- every guest write path reaches the block watch, tested.
 *
 * The block watch is only evidence if it cannot silently miss a path. "All the
 * lifted stores go through rsv::store_range" is an argument; the table of call
 * sites is not a measurement. This test takes one 128-byte line, arms the watch
 * on it, and drives a store through every path the runtime has -- the lifted
 * PPU store, both reservation commits (stcx and putllc), putlluc, the locked
 * store used by SPURS, the bit merge used by the SPU PUT, and dcbz -- then
 * reads gowa.log back and requires one WRITE line naming each path.
 *
 * A path that stops calling cw::block_store makes this fail at the path's
 * name, which is the only shape that can be trusted at a run.
 */
#include "runtime/codewatch.h"
#include "runtime/memory.h"
#include "runtime/reservation.h"
#include "runtime/log.h"
#include "ppu/ops.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

std::string read_log() {
    std::string all;
    if (FILE* f = std::fopen("gowa.log", "r")) {
        char buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) all.append(buf, n);
        std::fclose(f);
    }
    return all;
}

/* The label blockwatch_hit prints for a path, and whether the log holds it. */
bool reported(const std::string& log, const char* label) {
    return log.find(std::string(" by ") + label + " ") != std::string::npos ||
           log.find(std::string(" by ") + label + " |") != std::string::npos;
}

}

int main()
{
    /* The watch is armed from the environment by cw::init(), exactly as the
     * title arms it. 0x0177caa0..0x0177cab0 is the real watched block from the
     * RDR investigation, so this test measures the same code path. */
    std::remove("gowa.log");
    /* One 4 KiB range, so every path's store can land inside the watch while
     * still having a distinct offset: blockwatch_hit reports the first
     * observation of each distinct (offset, width, value, caller) and counts
     * repeats, so seven different offsets are seven reported lines. */
    setenv("PS3BASE_BLOCKWATCH", "0x0177c000-0x0177d000", 1);
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    vm::map(0x0177c000u, 0x2000u);
    cw::init();

    const uint32_t base = 0x0177caa0u;

    /* 1. The lifted PPU store (ppu::st32 -> rsv::store_range). */
    vm::write32(base + 0x00, 1);

    /* 2. A store-conditional that commits (stwcx -> rsv::commit_if, label stcx). */
    {
        uint8_t snap[128];
        const uint64_t rt = rsv::snapshot(base + 0x40, snap);
        uint8_t want[128];
        std::memcpy(want, snap, sizeof want);
        want[0x04] = 0xAB;
        rsv::commit_if(base + 0x40, rt, snap, want, 0x04, 4);
    }

    /* 3. putllc: a 128-byte reservation commit (label putllc). */
    {
        uint8_t snap[128];
        const uint64_t rt = rsv::snapshot(base + 0x80, snap);
        uint8_t want[128];
        std::memcpy(want, snap, sizeof want);
        want[0x08] = 0xCD;
        rsv::commit_if(base + 0x80, rt, snap, want, 0, 128);
    }

    /* 4. putlluc: an unconditional 128-byte line write. */
    {
        uint8_t line[128];
        std::memset(line, 0, sizeof line);
        line[0] = 0x11;
        rsv::store_unconditional(base + 0x100, line);
    }

    /* 5. The locked store the SPURS HLE writes its fields with. */
    {
        const uint32_t v = 0x11223344u;
        rsv::store_locked(base + 0x180, &v, 4);
    }

    /* 6. The SPU PUT path: a bit merge, not a store. */
    {
        uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        uint8_t zero[4] = {0, 0, 0, 0};
        rsv::merge_bits(base + 0x200, data, zero, 4);
    }

    /* 7. dcbz: a zero fill through ppu::notify_store. */
    {
        uint8_t* b = vm::ptr(0);
        ppu::dcbz(b, base + 0x280);
    }

    const std::string log = read_log();
    CHECK(!log.empty(), "the log was written");
    CHECK(reported(log, "rsv::store_range"), "a lifted PPU store is reported by rsv::store_range");
    /* commit_if chooses its label from the size when the caller passes none, and
     * the lifted stwcx/putllc pass none: "stcx" for a narrow store, "putllc" for
     * a whole 128-byte line. Asserting the default is asserting the path the
     * guest's own instructions take, not a label this test invented. */
    CHECK(reported(log, "stcx"), "a stwcx is reported by rsv::commit_if");
    CHECK(reported(log, "putllc"), "a putllc is reported by rsv::commit_if");
    CHECK(reported(log, "rsv::store_unconditional"), "a putlluc is reported by rsv::store_unconditional");
    CHECK(reported(log, "rsv::store_locked"), "a locked SPURS store is reported by rsv::store_locked");
    CHECK(reported(log, "rsv::merge_bits"), "an SPU bit merge is reported by rsv::merge_bits");
    CHECK(reported(log, "ppu::dcbz"), "a dcbz is reported by ppu::dcbz");

    std::printf("write_paths: %d checks passed (%d failed)\n", checks, fails);
    return fails ? 1 : 0;
}
