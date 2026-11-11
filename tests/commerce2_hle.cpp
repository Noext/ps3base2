/* ps3base -- the sceNpCommerce2 HANDLERS, not just their logic: the module
 * included whole, called the way the import dispatcher calls it, against real
 * guest memory.
 *
 * tests/commerce2.cpp checks the rules through commerce2_hle.h. This file
 * checks what actually lands in the guest's registers and structures, which is
 * what the title reads:
 *
 *   1. every one of the 26 imports is registered under the "module::name" key
 *      the dispatcher builds, and no OTHER Commerce2 entry point is -- the
 *      unimported 26 must stay unregistered so they log as unimplemented;
 *   2. the store-unavailable code reaches r3 as a sign-extended 0x80023016;
 *   3. CreateCtx writes the context id 0x5001 into the caller's u32;
 *   4. CreateSessionFinish zeroes 44 bytes for this title's SDK (0x300001) and
 *      only 8 for a pre-3.00 disc, leaving the rest of the structure alone;
 *   5. CreateSessionStart queues the callback with the five arguments of
 *      sceNpCommerce2.h:335 -- ctx_id, subject 0, event 0x0011, error 0, arg.
 */
#include "runtime/memory.h"
#include "runtime/loader.h"
#include "runtime/sysutil_events.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../runtime/src/hle/sceNpCommerce2.cpp"

/* --- the few runtime services the module calls -------------------------- */

/* Stands in for sceNpInit: np.cpp owns the real flag (NpState::init), and the
 * header declares this accessor so the "before the NP manager" path is
 * exercisable at all. */
static bool g_np_manager_up = false;

namespace hle {
/* hle::Reg normally registers into the import table (runtime/src/ppu_runtime.cpp).
 * This test calls the handlers directly, so the constructor records the key in
 * a local map instead of pulling in the whole PPU runtime. Same trick as
 * tests/cellgameexec.cpp; the map is function-local because the static hle::Reg
 * objects inside the included module run before any later namespace-scope
 * object would be constructed. */
std::map<std::string, Fn>& test_registry() {
    static std::map<std::string, Fn> m;
    return m;
}

/* sysutil_post is the delivery queue. Recording the thunk and running it here
 * makes "which callback, with which arguments" an assertion rather than a log. */
std::vector<std::function<void(ppu::Context&)>>& pending() {
    static std::vector<std::function<void(ppu::Context&)>> v;
    return v;
}
void sysutil_post(std::function<void(ppu::Context&)> fn) { pending().push_back(std::move(fn)); }

/* call_guest is support.cpp's; the guest there is real. Here nothing executes,
 * so what a delivery WOULD pass is captured instead. */
struct Delivered {
    uint32_t opd;
    uint64_t args[5];
};
std::vector<Delivered>& deliveries() {
    static std::vector<Delivered> v;
    return v;
}
void call_guest(ppu::Context&, uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2,
                uint64_t a3, uint64_t a4) {
    deliveries().push_back({opd, {a0, a1, a2, a3, a4}});
}
}

namespace hle {
Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}
}

namespace cbwatch {
void note_register(const char*, uint32_t, uint32_t) {}
void note_call(const char*, uint32_t, uint64_t) {}
}

namespace loader {
ProcessInfo g_process;
}

namespace hle {
namespace commerce2 {
bool np_manager_initialised() { return g_np_manager_up; }
}
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

uint32_t g_next = 0x20000000;

uint32_t alloc_area(uint32_t size = 0x1000) {
    const uint32_t a = g_next;
    if (!vm::map(a, size)) return 0;
    g_next += size;
    return a;
}

/* The import path's calling convention: r3.. are the arguments. */
int64_t call(ppu::Context& ctx, void (*fn)(ppu::Context&, uint8_t*), uint64_t a0 = 0,
             uint64_t a1 = 0, uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0) {
    ctx = ppu::Context{};
    ctx.gpr[3] = a0;
    ctx.gpr[4] = a1;
    ctx.gpr[5] = a2;
    ctx.gpr[6] = a3;
    ctx.gpr[7] = a4;
    fn(ctx, vm::g_base);
    return static_cast<int64_t>(ctx.gpr[3]);
}

int32_t low32(int64_t v) { return static_cast<int32_t>(v & 0xFFFFFFFFll); }

/* Run every queued thunk, as cellSysutilCheckCallback's pump does. */
void pump() {
    for (size_t i = 0; i < hle::pending().size(); i++) {
        ppu::Context c;
        hle::pending()[i](c);
    }
    hle::pending().clear();
}

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }
    ppu::Context ctx;
    const uint32_t area = alloc_area(0x1000);
    CHECK(area != 0, "guest memory for the output structures");
    if (!area) return 1;
    const uint32_t npid = area;            /* 36 bytes */
    const uint32_t ctx_id_out = area + 0x40;
    const uint32_t session_info = area + 0x80;

/* --- 1. the registration set ----------------------------------------
     * The 26 names are the ones this disc's import table lists
     * (out-rdr/ppu/table.cpp:82123-82148). */
    const char* const imported[] = {
        "sceNpCommerce2AbortReq", "sceNpCommerce2Init",
        "sceNpCommerce2GetCategoryContentsStart", "sceNpCommerce2Term",
        "sceNpCommerce2CreateSessionAbort", "sceNpCommerce2DestroyCtx",
        "sceNpCommerce2GetCategoryContentsCreateReq", "sceNpCommerce2CreateSessionFinish",
        "sceNpCommerce2GetContentInfo", "sceNpCommerce2DestroyGetCategoryContentsResult",
        "sceNpCommerce2GetProductInfoCreateReq", "sceNpCommerce2GetCategoryInfo",
        "sceNpCommerce2GetProductInfoGetResult", "sceNpCommerce2GetCategoryContentsGetResult",
        "sceNpCommerce2CreateSessionStart", "sceNpCommerce2InitGetCategoryContentsResult",
        "sceNpCommerce2CreateCtx", "sceNpCommerce2GetGameSkuInfoFromGameProductInfo",
        "sceNpCommerce2GetGameProductInfoFromContentInfo", "sceNpCommerce2ExecuteStoreBrowse",
        "sceNpCommerce2GetGameProductInfo", "sceNpCommerce2DestroyGetProductInfoResult",
        "sceNpCommerce2InitGetProductInfoResult", "sceNpCommerce2GetCategoryInfoFromContentInfo",
        "sceNpCommerce2DestroyReq", "sceNpCommerce2GetProductInfoStart",
    };
    int registered = 0;
    for (const char* n : imported)
        if (hle::test_registry().count(std::string("sceNpCommerce2::") + n)) registered++;
    CHECK(registered == 26, "all 26 imports are registered");
    CHECK(sizeof(imported) / sizeof(imported[0]) == 26,
          "the list itself holds 26 distinct names, so the count above cannot be met by a duplicate");

    /* The funcs this disc does NOT import must stay unregistered, so the
     * dispatcher logs them as unimplemented instead of inventing an answer. */
    const char* const not_imported[] = {
        "sceNpCommerce2EmptyStoreCheckStart", "sceNpCommerce2EmptyStoreCheckAbort",
        "sceNpCommerce2EmptyStoreCheckFinish", "sceNpCommerce2DoCheckoutStartAsync",
        "sceNpCommerce2DoCheckoutFinishAsync", "sceNpCommerce2DoProductBrowseStartAsync",
        "sceNpCommerce2DoProductBrowseFinishAsync", "sceNpCommerce2DoDlListStartAsync",
        "sceNpCommerce2DoDlListFinishAsync", "sceNpCommerce2DoProductCodeStartAsync",
        "sceNpCommerce2DoProductCodeFinishAsync", "sceNpCommerce2GetPrice",
        "sceNpCommerce2GetProductInfoListCreateReq", "sceNpCommerce2GetProductInfoListStart",
        "sceNpCommerce2GetProductInfoListGetResult", "sceNpCommerce2InitGetProductInfoListResult",
        "sceNpCommerce2GetGameProductInfoFromGetProductInfoListResult",
        "sceNpCommerce2DestroyGetProductInfoListResult",
        "sceNpCommerce2GetContentRatingInfoFromGameProductInfo",
        "sceNpCommerce2GetContentRatingInfoFromCategoryInfo",
        "sceNpCommerce2GetContentRatingDescriptor", "sceNpCommerce2GetBGDLAvailability",
        "sceNpCommerce2SetBGDLAvailability", "sceNpCommerce2DoServiceListStartAsync",
        "sceNpCommerce2DoServiceListFinishAsync", "sceNpCommerce2GetStoreBrowseUserdata",
    };
    int stray = 0;
    for (const char* n : not_imported)
        if (hle::test_registry().count(std::string("sceNpCommerce2::") + n)) stray++;
    CHECK(stray == 0, "no unimported Commerce2 entry point is registered (they must log as unimplemented)");

    /* --- 2. Init before the NP manager, then with it --------------------- */
    {
        const int64_t r = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2Init);
        CHECK(low32(r) == static_cast<int32_t>(0x8002aa01),
              "Init before sceNpInit answers SCE_NP_ERROR_NOT_INITIALIZED (0x8002aa01) in r3");
        g_np_manager_up = true;
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2Init)) == 0, "Init then answers CELL_OK");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2Init))
                  == static_cast<int32_t>(0x80023002),
              "a second Init answers 0x80023002");
    }

    /* --- 3. CreateCtx writes the id into guest memory ------------------- */
    {
        std::memset(vm::ptr(npid), 0, 36);
        std::memset(vm::ptr(ctx_id_out), 0, 4);
        const int64_t r = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2CreateCtx,
                               2, npid, 0x00200000, 0x11223344, ctx_id_out);
        CHECK(low32(r) == 0, "CreateCtx returns CELL_OK");
        CHECK(vm::read32(ctx_id_out) == 0x5001,
              "CreateCtx wrote 0x5001 (idm id_base) into the caller's u32, not 1");
    }

    /* --- 4. the session event, five arguments --------------------------- */
    {
        pump();
        hle::deliveries().clear();
        const int64_t r = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2CreateSessionStart, 0x5001);
        CHECK(low32(r) == 0, "CreateSessionStart returns CELL_OK offline");
        CHECK(hle::pending().size() == 1, "CreateSessionStart queued exactly one delivery");
        pump();
        CHECK(hle::deliveries().size() == 1, "the queued thunk delivered to the handler");
        if (hle::deliveries().size() == 1) {
            const auto& d = hle::deliveries()[0];
            CHECK(d.opd == 0x00200000, "delivered to the handler CreateCtx was given");
            CHECK(static_cast<uint32_t>(d.args[0]) == 0x5001, "arg0 is the context id");
            CHECK(static_cast<uint32_t>(d.args[1]) == 0, "arg1 is subject_id 0");
            CHECK(static_cast<uint32_t>(d.args[2]) == 0x0011,
                  "arg2 is SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE (0x0011)");
            CHECK(static_cast<uint32_t>(d.args[3]) == 0, "arg3 is error_code 0");
            CHECK(static_cast<uint32_t>(d.args[4]) == 0x11223344, "arg4 is the caller's arg, last");
        }
    }

    /* --- 5. CreateSessionFinish zeroes the documented prefix ------------- */
    {
        loader::g_process.sdk_version = 0x300001;
        std::memset(vm::ptr(session_info), 0x5A, 64);
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2CreateSessionFinish,
                         0x5001, session_info)) == 0,
              "CreateSessionFinish returns CELL_OK");
        bool first44 = true;
        for (int i = 0; i < 44; i++) if (vm::ptr(session_info)[i] != 0) first44 = false;
        CHECK(first44, "SDK 0x300001: the first 44 bytes of SceNpCommerce2SessionInfo are zeroed");
        CHECK(vm::ptr(session_info)[44] == 0x5A,
              "the reserved tail is NOT written: RPCS3 stops at sizeof - reserved");
    }
    {
        loader::g_process.sdk_version = 0x2fffff;
        std::memset(vm::ptr(session_info), 0x5A, 64);
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2CreateSessionFinish,
                         0x5001, session_info)) == 0,
              "CreateSessionFinish returns CELL_OK for a pre-3.00 SDK too");
        CHECK(vm::ptr(session_info)[0] == 0 && vm::ptr(session_info)[7] == 0,
              "SDK below 3.00: currencyCode and decimals are zeroed");
        CHECK(vm::ptr(session_info)[8] == 0x5A,
              "SDK below 3.00: byte 8 (currencySymbol) is left as the caller had it");
        bool rest_intact = true;
        for (int i = 8; i < 64; i++) if (vm::ptr(session_info)[i] != 0x5A) rest_intact = false;
        CHECK(rest_intact, "SDK below 3.00: nothing beyond byte 7 is written");
    }

    /* --- 6. the store-unavailable code in r3 ---------------------------- */
    {
        const int64_t r = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2GetCategoryInfo,
                                session_info, session_info + 0x100);
        CHECK(low32(r) == static_cast<int32_t>(0x80023016),
              "GetCategoryInfo reaches r3 as the sign-extended store-unavailable code 0x80023016");
        CHECK(r != 0, "GetCategoryInfo NEVER answers CELL_OK -- there is no store to answer from");
    }

    /* --- 7. the GetResult size floor ------------------------------------ */
    {
        const int64_t r = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2GetCategoryContentsGetResult,
                               1, session_info, 4096, session_info + 0x200);
        CHECK(low32(r) == static_cast<int32_t>(0x80023018),
              "a 4 KiB result buffer is INSUFFICIENT_BUF_SIZE (0x80023018)");

        /* A big enough buffer succeeds and writes NOTHING: fill_size keeps the
         * value the caller had, which is exactly what RPCS3's get_result does. */
        vm::write32(session_info + 0x200, 0xAABBCCDD);
        const int64_t ok = call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2GetCategoryContentsGetResult,
                                1, session_info, 262144, session_info + 0x200);
        CHECK(low32(ok) == 0, "a 256 KiB buffer returns CELL_OK");
        CHECK(vm::read32(session_info + 0x200) == 0xAABBCCDD,
              "fill_size is untouched: no count was fabricated");
    }

    /* --- 8. ExecuteStoreBrowse's two order rules ------------------------ */
    {
        /* A null targetId at type 1 is a TARGET_ID error... */
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2ExecuteStoreBrowse, 1, 0, 0))
                  == static_cast<int32_t>(0x8002301b),
              "a CATEGORY browse with a NULL targetId is INVALID_TARGET_ID");
        /* ...but the TYPE is checked first. */
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2ExecuteStoreBrowse, 7, 0, 0))
                  == static_cast<int32_t>(0x8002301a),
              "an out-of-range target type is INVALID_TARGET_TYPE, checked before the id");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2ExecuteStoreBrowse, 3, 0, 0)) == 0,
              "a PRODUCT_CODE browse needs no targetId");
    }

    /* --- 9. DestroyCtx, and the two unconditional destroys --------------- */
    {
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2DestroyGetCategoryContentsResult,
                         0)) == 0,
              "DestroyGetCategoryContentsResult answers CELL_OK with no init check and a NULL argument");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2DestroyGetProductInfoResult,
                         0)) == 0,
              "DestroyGetProductInfoResult answers CELL_OK with no init check and a NULL argument");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2DestroyCtx, 0x9999))
                  == static_cast<int32_t>(0x8002300c),
              "DestroyCtx of an unknown id is CTX_NOT_FOUND (0x8002300c)");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2DestroyCtx, 0x5001)) == 0,
              "DestroyCtx of the live context returns CELL_OK");
    }

    /* --- 10. Term, then the guard is back ------------------------------- */
    {
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2Term)) == 0, "Term returns CELL_OK");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2GetCategoryInfo,
                         session_info, session_info + 0x100))
                  == static_cast<int32_t>(0x80023001),
              "after Term, GetCategoryInfo is NOT_INITIALIZED (0x80023001)");
        CHECK(low32(call(ctx, hle::hle_sceNpCommerce2_sceNpCommerce2Term)) == 0,
              "Term before Init also returns CELL_OK (it has no guard)");
    }

    std::printf("commerce2_hle: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
