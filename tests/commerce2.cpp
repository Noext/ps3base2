/* ps3base -- sceNpCommerce2, the 26 imports Red Dead Redemption takes
 * (ticket RDR-HLE-04), driven through the SAME header the handlers use
 * (runtime/src/hle/commerce2_hle.h).
 *
 * No game, no GPU, no network. The subject is the module's rules, and every
 * check is a property a wrong implementation would break:
 *
 *   1. the return codes are RPCS3's, byte for byte, including the two the old
 *      np.cpp handlers had WRONG (NOT_INITIALIZED was 0x8002b701, the real one
 *      is 0x80023001; the session-done event was 0x4, the real one 0x0011);
 *   2. the ORDER of the checks is RPCS3's: version before npId in CreateCtx,
 *      buffer size before pointers in GetResult, target type before target id
 *      in ExecuteStoreBrowse;
 *   3. Init answers SCE_NP_ERROR_NOT_INITIALIZED (the manager's code, not
 *      Commerce2's) when sceNpInit has not run, and ALREADY_INITIALIZED for a
 *      second Init;
 *   4. CreateCtx / CreateSessionStart / CreateSessionFinish SUCCEED -- that is
 *      what the console answers -- and the context id is 0x5001, idm's
 *      id_base, not 1;
 *   5. GetCategoryInfo is the store-unavailable answer,
 *      SCE_NP_COMMERCE2_ERROR_SERVER_MAINTENANCE, and every Init*Result is
 *      INVALID_RESULT_DATA because there is no result to parse.
 *
 * Citations name the reference line for each group.
 */
#include "hle/commerce2_hle.h"

#include <cstdio>

namespace com2 = hle::commerce2;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

int main() {
    /* --- 1. the constants, refs/.../sceNpCommerce2.h:8-35, :57 -------------- */
    CHECK(com2::ERR_NOT_INITIALIZED == static_cast<int32_t>(0x80023001),
          "ERR_NOT_INITIALIZED is 0x80023001 (np.cpp had 0x8002b701)");
    CHECK(com2::ERR_ALREADY_INITIALIZED == static_cast<int32_t>(0x80023002),
          "ERR_ALREADY_INITIALIZED is 0x80023002 (np.cpp had 0x8002b702)");
    CHECK(com2::ERR_INVALID_ARGUMENT == static_cast<int32_t>(0x80023003),
          "ERR_INVALID_ARGUMENT is 0x80023003");
    CHECK(com2::ERR_UNSUPPORTED_VERSION == static_cast<int32_t>(0x80023004),
          "ERR_UNSUPPORTED_VERSION is 0x80023004");
    CHECK(com2::ERR_CTX_NOT_FOUND == static_cast<int32_t>(0x8002300c),
          "ERR_CTX_NOT_FOUND is 0x8002300c (np.cpp had 0x8002b70b)");
    CHECK(com2::ERR_INVALID_RESULT_DATA == static_cast<int32_t>(0x80023014),
          "ERR_INVALID_RESULT_DATA is 0x80023014");
    CHECK(com2::ERR_SERVER_MAINTENANCE == static_cast<int32_t>(0x80023016),
          "the store-unavailable code is 0x80023016");
    CHECK(com2::ERR_INSUFFICIENT_BUF_SIZE == static_cast<int32_t>(0x80023018),
          "ERR_INSUFFICIENT_BUF_SIZE is 0x80023018");
    CHECK(com2::NP_ERR_NOT_INITIALIZED == static_cast<int32_t>(0x8002aa01),
          "the manager's NOT_INITIALIZED is 0x8002aa01 (sceNp.h:57)");
    CHECK(com2::EVENT_CREATE_SESSION_DONE == 0x0011,
          "EVENT_CREATE_SESSION_DONE is 0x0011 (np.cpp delivered 0x4)");
    CHECK(com2::VERSION == 2, "SCE_NP_COMMERCE2_VERSION is 2");
    CHECK(com2::CTX_MAX == 1, "SCE_NP_COMMERCE2_CTX_MAX is 1");
    CHECK(com2::CTX_ID_BASE == 0x5001, "commerce2_ctx id_base is 0x5001");
    CHECK(com2::RECV_BUF_SIZE == 262144, "SCE_NP_COMMERCE2_RECV_BUF_SIZE is 0x40000");
    CHECK(com2::GETCAT_MAX_COUNT == 60, "SCE_NP_COMMERCE2_GETCAT_MAX_COUNT is 60");

    /* --- 2. the handler's argument order, refs/.../sceNpCommerce2.h:335 ---
     * void(ctx_id, subject_id, event, error_code, arg). */
    {
        const com2::Event ev = com2::session_done_event(0x5001);
        CHECK(ev.ctx_id == 0x5001, "the event carries the context id");
        CHECK(ev.subject_id == 0, "subject_id is 0 (sceNpCommerce2.cpp:282)");
        CHECK(ev.event == 0x0011, "event is CREATE_SESSION_DONE");
        CHECK(ev.error_code == 0, "error_code is 0 on success");
    }

    /* --- 3. Init's three answers, sceNpCommerce2.cpp:129-144 --------------- */
    {
        com2::Module m;
        CHECK(m.init(false) == com2::NP_ERR_NOT_INITIALIZED,
              "Init before sceNpInit answers SCE_NP_ERROR_NOT_INITIALIZED, not a Commerce2 code");
        CHECK(!m.initialized(), "a refused Init leaves the module uninitialised");
        CHECK(m.init(true) == 0, "Init with the manager up returns CELL_OK");
        CHECK(m.initialized(), "Init sets the flag");
        CHECK(m.init(true) == com2::ERR_ALREADY_INITIALIZED,
              "a second Init answers ALREADY_INITIALIZED, even with the manager up");
        /* Term has NO NOT_INITIALIZED check (:146-155). */
        CHECK(m.term() == 0, "Term returns CELL_OK");
        CHECK(!m.initialized(), "Term clears the flag");
        CHECK(m.term() == 0, "Term on a module that is already down still returns CELL_OK");
    }

    /* --- 4. the not-initialised guard, in EVERY function that has one ----- */
    {
        com2::Module m;
        uint32_t out = 0;
        bool deliver = false;
        CHECK(m.create_ctx(2, true, 0x1000, 0, 0, true, &out) == com2::ERR_NOT_INITIALIZED,
              "CreateCtx before Init is NOT_INITIALIZED");
        CHECK(m.destroy_ctx(0x5001) == com2::ERR_NOT_INITIALIZED, "DestroyCtx before Init");
        CHECK(m.create_session_start(0x5001, &deliver) == com2::ERR_NOT_INITIALIZED,
              "CreateSessionStart before Init");
        CHECK(m.create_session_abort(0x5001) == com2::ERR_NOT_INITIALIZED, "CreateSessionAbort before Init");
        CHECK(m.create_session_finish(0x5001) == com2::ERR_NOT_INITIALIZED, "CreateSessionFinish before Init");
        CHECK(m.get_category_contents_create_req(true) == com2::ERR_NOT_INITIALIZED,
              "GetCategoryContentsCreateReq before Init");
        CHECK(m.get_category_contents_start(true, 1) == com2::ERR_NOT_INITIALIZED,
              "GetCategoryContentsStart before Init");
        CHECK(m.get_category_info(true, true) == com2::ERR_NOT_INITIALIZED, "GetCategoryInfo before Init");
        CHECK(m.execute_store_browse(1, true, true) == com2::ERR_NOT_INITIALIZED,
              "ExecuteStoreBrowse before Init");
        CHECK(m.abort_req() == com2::ERR_NOT_INITIALIZED, "AbortReq before Init");
        CHECK(m.destroy_req() == com2::ERR_NOT_INITIALIZED, "DestroyReq before Init");
        /* The two Destroy*Result functions have NO initialisation check at all
         * (sceNpCommerce2.cpp:498-502, :591-595), unlike (say) AbortReq. That
         * asymmetry is RPCS3's and is reproduced, not tidied. */
        CHECK(com2::Module::destroy_result() == 0,
              "DestroyGetCategoryContentsResult / DestroyGetProductInfoResult answer CELL_OK with no init check");
    }

    /* --- 5. CreateCtx, sceNpCommerce2.cpp:157-181 -------------------------- */
    {
        com2::Module m;
        m.init(true);
        uint32_t out = 0;
        /* The version check comes BEFORE the npId check. */
        CHECK(m.create_ctx(3, false, 0, 0, 0, true, &out) == com2::ERR_UNSUPPORTED_VERSION,
              "version > 2 is UNSUPPORTED_VERSION, and it wins over a NULL npId");
        CHECK(out == 0, "a refused CreateCtx writes no context id");
        CHECK(m.create_ctx(2, false, 0, 0, 0, true, &out) == com2::ERR_INVALID_ARGUMENT,
              "a NULL npId is INVALID_ARGUMENT");
        CHECK(m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &out) == 0,
              "a valid CreateCtx returns CELL_OK -- the console creates the context offline");
        CHECK(out == 0x5001, "the first context id is 0x5001 (idm id_base), not 1");
        CHECK(m.find_ctx(0x5001) != nullptr, "the context is findable by that id");
        CHECK(m.find_ctx(0x5001) && m.find_ctx(0x5001)->handler == 0x2000,
              "the context stores the handler");
        CHECK(m.find_ctx(0x5001) && m.find_ctx(0x5001)->arg == 0x3000,
              "the context stores the caller's arg");

        /* A NULL ctx_id pointer is a warning, not an error (:175-178). */
        CHECK(m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, false, nullptr) == 0,
              "a NULL ctx_id pointer still returns CELL_OK");

        /* CTX_MAX is 1 and RPCS3's check is "if (false) // TODO" (:169-170), so
         * a second context does NOT fail; idm cannot allocate, and the returned
         * id is the id space's invalid value, 0. */
        CHECK(m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &out) == 0,
              "a second CreateCtx still returns CELL_OK (CTX_MAX is a TODO in RPCS3)");
        CHECK(out == 0, "with the single slot taken, the allocated id is invalid (0)");
    }

    /* --- 6. DestroyCtx, sceNpCommerce2.cpp:183-198 ------------------------ */
    {
        com2::Module m;
        m.init(true);
        uint32_t out = 0;
        m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &out);
        CHECK(m.destroy_ctx(0x9999) == com2::ERR_CTX_NOT_FOUND, "DestroyCtx of an unknown id is CTX_NOT_FOUND");
        CHECK(m.destroy_ctx(0x5001) == 0, "DestroyCtx of the live context returns CELL_OK");
        /* Term clears the flag and NOTHING else (sceNpCommerce2.cpp:146-155):
         * the context survives it, because RPCS3 keeps it in idm. */
        {
            com2::Module t;
            t.init(true);
            uint32_t tid = 0;
            t.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &tid);
            t.term();
            t.init(true);
            CHECK(t.find_ctx(tid) != nullptr, "Term does NOT destroy the context (RPCS3 only flips the flag)");
            CHECK(t.destroy_ctx(tid) == 0, "the surviving context is still destroyable after Term + Init");
        }
        CHECK(m.find_ctx(0x5001) == nullptr, "the destroyed context is gone");
        CHECK(m.destroy_ctx(0x5001) == com2::ERR_CTX_NOT_FOUND, "destroying it twice is CTX_NOT_FOUND");
    }

    /* --- 7. the session, sceNpCommerce2.cpp:262-357 ----------------------- */
    {
        com2::Module m;
        m.init(true);
        uint32_t out = 0;
        bool deliver = false;
        CHECK(m.create_session_start(0x5001, &deliver) == com2::ERR_CTX_NOT_FOUND,
              "CreateSessionStart with no context is CTX_NOT_FOUND");
        m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &out);
        CHECK(m.create_session_start(out, &deliver) == 0,
              "CreateSessionStart on a live context returns CELL_OK -- no network is involved");
        CHECK(deliver, "a registered handler is given CREATE_SESSION_DONE");
        CHECK(m.create_session_abort(out) == 0, "CreateSessionAbort on a live context returns CELL_OK");
        CHECK(m.create_session_finish(out) == 0, "CreateSessionFinish on a live context returns CELL_OK");
        CHECK(m.create_session_start(0x9999, &deliver) == com2::ERR_CTX_NOT_FOUND,
              "CreateSessionStart on an unknown context is CTX_NOT_FOUND");
    }

    /* --- 8. the session's zeroed prefix, sceNpCommerce2.cpp:322-329 ------ */
    {
        CHECK(com2::session_info_zero_bytes(0x2fffff) == 8,
              "an SDK below 3.00 clears currencyCode+decimals only (8 bytes)");
        CHECK(com2::session_info_zero_bytes(0x300000) == 44,
              "SDK 3.00 exactly takes the 44-byte branch");
        CHECK(com2::session_info_zero_bytes(0x300001) == 44,
              "this title's SDK (0x300001) clears 44 bytes: sizeof(SessionInfo) - reserved");
    }

    /* --- 9. the request/resource family, and its check order ------------- */
    {
        com2::Module m;
        m.init(true);
        uint32_t out = 0;
        m.create_ctx(2, true, 0x1000, 0x2000, 0x3000, true, &out);

        CHECK(m.get_category_contents_create_req(false) == com2::ERR_INVALID_ARGUMENT,
              "GetCategoryContentsCreateReq with a NULL req_id is INVALID_ARGUMENT");
        CHECK(m.get_category_contents_create_req(true) == 0,
              "GetCategoryContentsCreateReq does NOT require a context, unlike its product twin");

        CHECK(m.get_product_info_create_req(0x9999, true) == com2::ERR_CTX_NOT_FOUND,
              "GetProductInfoCreateReq requires a live context");
        CHECK(m.get_product_info_create_req(out, false) == com2::ERR_INVALID_ARGUMENT,
              "GetProductInfoCreateReq checks req_id before the context");
        CHECK(m.get_product_info_create_req(out, true) == 0, "GetProductInfoCreateReq with both is CELL_OK");

        /* categoryId is NOT checked here (sceNpCommerce2.cpp:532). */
        CHECK(m.get_product_info_start(true) == 0, "GetProductInfoStart accepts a productId and ignores categoryId");
        CHECK(m.get_product_info_start(false) == com2::ERR_INVALID_ARGUMENT,
              "GetProductInfoStart refuses a NULL productId");

        CHECK(m.get_category_contents_start(true, 60) == 0,
              "GetCategoryContentsStart accepts the maximum count (60)");
        CHECK(m.get_category_contents_start(true, 61) == com2::ERR_INVALID_ARGUMENT,
              "GetCategoryContentsStart refuses 61");
        CHECK(m.get_category_contents_start(false, 10) == com2::ERR_INVALID_ARGUMENT,
              "GetCategoryContentsStart refuses a NULL categoryId");
        CHECK(m.get_category_contents_start(false, 61) == com2::ERR_INVALID_ARGUMENT,
              "both bad is still INVALID_ARGUMENT");
    }

    /* --- 10. the GetResult size check comes FIRST, sceNpCommerce2.cpp:408 -
     * A buffer under 0x40000 is INSUFFICIENT_BUF_SIZE even when the pointers
     * are null, so the size error wins. */
    {
        com2::Module m;
        m.init(true);
        CHECK(m.get_category_contents_get_result(0, false, false) == com2::ERR_INSUFFICIENT_BUF_SIZE,
              "GetCategoryContentsGetResult checks buf_size before the pointers");
        CHECK(m.get_category_contents_get_result(0x3ffff, true, true) == com2::ERR_INSUFFICIENT_BUF_SIZE,
              "0x3ffff bytes is one short of the 0x40000 required");
        CHECK(m.get_category_contents_get_result(0x40000, false, true) == com2::ERR_INVALID_ARGUMENT,
              "with a big enough buffer, a NULL buf is INVALID_ARGUMENT");
        CHECK(m.get_category_contents_get_result(0x40000, true, false) == com2::ERR_INVALID_ARGUMENT,
              "with a big enough buffer, a NULL fill_size is INVALID_ARGUMENT");
        CHECK(m.get_category_contents_get_result(0x40000, true, true) == 0,
              "the valid case returns CELL_OK and writes NOTHING (fill_size stays 0)");
        CHECK(m.get_product_info_get_result(0x40000, true, true) == 0, "GetProductInfoGetResult behaves the same");
        CHECK(m.get_product_info_get_result(10, true, true) == com2::ERR_INSUFFICIENT_BUF_SIZE,
              "GetProductInfoGetResult enforces the same 0x40000 floor");
        CHECK(com2::Module::get_result(false, 0x40000, true) == com2::ERR_INVALID_ARGUMENT,
              "get_result's own NULL check, sceNpCommerce2.cpp:389-397");
        CHECK(com2::Module::get_result(true, 0, true) == com2::ERR_INVALID_ARGUMENT,
              "a zero buf_size is INVALID_ARGUMENT inside get_result too");
    }

    /* --- 11. the store-unavailable answers -------------------------------- */
    {
        com2::Module m;
        m.init(true);
        CHECK(m.get_category_info(true, true) == com2::ERR_SERVER_MAINTENANCE,
              "GetCategoryInfo answers SERVER_MAINTENANCE -- RPCS3's store-unavailable line");
        CHECK(m.get_category_info(false, true) == com2::ERR_INVALID_ARGUMENT,
              "a NULL result is still INVALID_ARGUMENT, not the maintenance code");

        /* check_and_get_data returns INVALID_RESULT_DATA for EVERY payload,
         * sceNpCommerce2.cpp:82-92; the 60-byte floor is only the first test. */
        CHECK(com2::Module::check_and_get_data(0) == com2::ERR_INVALID_RESULT_DATA, "no data at all");
        CHECK(com2::Module::check_and_get_data(59) == com2::ERR_INVALID_RESULT_DATA,
              "59 bytes is under the floor");
        CHECK(com2::Module::check_and_get_data(60) == com2::ERR_INVALID_RESULT_DATA,
              "60 bytes passes the floor and STILL fails: the extraction is a TODO");
        CHECK(com2::Module::check_and_get_data(262144) == com2::ERR_INVALID_RESULT_DATA,
              "a full-size response fails too -- there is no store data to parse");
        CHECK(m.init_get_category_contents_result(true, true, 262144) == com2::ERR_INVALID_RESULT_DATA,
              "InitGetCategoryContentsResult fails on real-looking data");
        CHECK(m.init_get_product_info_result(true, true, 262144) == com2::ERR_INVALID_RESULT_DATA,
              "InitGetProductInfoResult fails on real-looking data");
        CHECK(m.init_get_category_contents_result(true, false, 100) == com2::ERR_INVALID_ARGUMENT,
              "a NULL data pointer is INVALID_ARGUMENT before the data check");
    }

    /* --- 12. the accessor functions, sceNpCommerce2.cpp:453-496, 576-589,
     * 735-748 ------------------------------------------------------------- */
    {
        com2::Module m;
        m.init(true);
        CHECK(m.get_content_info(true, true) == 0, "GetContentInfo returns CELL_OK");
        CHECK(m.get_content_info(true, false) == com2::ERR_INVALID_ARGUMENT, "GetContentInfo refuses a NULL out");
        CHECK(m.get_category_info_from_content_info(true, true) == 0, "GetCategoryInfoFromContentInfo returns CELL_OK");
        CHECK(m.get_category_info_from_content_info(false, true) == com2::ERR_INVALID_ARGUMENT,
              "GetCategoryInfoFromContentInfo refuses a NULL in");
        CHECK(m.get_game_product_info_from_content_info(true, true) == 0,
              "GetGameProductInfoFromContentInfo returns CELL_OK");
        CHECK(m.get_game_product_info(true, true) == 0, "GetGameProductInfo returns CELL_OK");
        CHECK(m.get_game_product_info(true, false) == com2::ERR_INVALID_ARGUMENT,
              "GetGameProductInfo refuses a NULL out");
        CHECK(m.get_game_sku_info(true, true) == 0, "GetGameSkuInfoFromGameProductInfo returns CELL_OK");
        CHECK(m.get_game_sku_info(false, true) == com2::ERR_INVALID_ARGUMENT,
              "GetGameSkuInfoFromGameProductInfo refuses a NULL in");
    }

    /* --- 13. ExecuteStoreBrowse, sceNpCommerce2.cpp:94-110 ---------------- */
    {
        com2::Module m;
        m.init(true);
        CHECK(m.execute_store_browse(0, true, true) == com2::ERR_INVALID_TARGET_TYPE,
              "target type 0 is below CATEGORY and is INVALID_TARGET_TYPE");
        CHECK(m.execute_store_browse(4, true, true) == com2::ERR_INVALID_TARGET_TYPE,
              "target type 4 is above PRODUCT_CODE and is INVALID_TARGET_TYPE");
        /* The type wins over the id: a bad type with a missing id is a TYPE error. */
        CHECK(m.execute_store_browse(9, false, false) == com2::ERR_INVALID_TARGET_TYPE,
              "the target type is checked before the target id");
        CHECK(m.execute_store_browse(1, false, false) == com2::ERR_INVALID_TARGET_ID,
              "a CATEGORY browse with no id is INVALID_TARGET_ID");
        CHECK(m.execute_store_browse(2, false, false) == com2::ERR_INVALID_TARGET_ID,
              "a PRODUCT browse with no id is INVALID_TARGET_ID");
        CHECK(m.execute_store_browse(3, false, false) == 0,
              "a PRODUCT_CODE browse needs no id (sceNpCommerce2.cpp:106)");
        CHECK(m.execute_store_browse(1, true, false) == com2::ERR_INVALID_TARGET_ID,
              "an empty-string target id is INVALID_TARGET_ID, not a null-pointer error");
        CHECK(m.execute_store_browse(1, true, true) == 0, "a CATEGORY browse with an id is CELL_OK");
    }

    /* --- 14. AbortReq / DestroyReq ---------------------------------------- */
    {
        com2::Module m;
        m.init(true);
        CHECK(m.abort_req() == 0, "AbortReq returns CELL_OK once initialised");
        CHECK(m.destroy_req() == 0, "DestroyReq returns CELL_OK once initialised");
    }

    std::printf("commerce2: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
