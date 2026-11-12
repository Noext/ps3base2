/* ps3base -- sceNpCommerce2, the PlayStation Store's in-game commerce API,
 * for Red Dead Redemption (BLES00680, ticket RDR-HLE-04).
 *
 * The decision logic is in commerce2_hle.h, where the unit test drives it
 * without guest memory. This file is the guest-memory skin: read the
 * arguments, call the logic, write back what the reference writes.
 *
 * THE TITLE IMPORTS EXACTLY 26 OF THIS MODULE'S 52 ENTRY POINTS, and only those
 * 26 are implemented here. The list is the generated import table the lifter
 * emits for this disc (out-rdr/ppu/table.cpp:82123-82148): Init, Term,
 * CreateCtx, DestroyCtx, CreateSessionStart/Abort/Finish,
 * GetCategoryContentsCreateReq/Start/GetResult, InitGetCategoryContentsResult,
 * GetCategoryInfo, GetContentInfo, GetCategoryInfoFromContentInfo,
 * GetGameProductInfoFromContentInfo, DestroyGetCategoryContentsResult,
 * GetProductInfoCreateReq/Start/GetResult, InitGetProductInfoResult,
 * GetGameProductInfo, DestroyGetProductInfoResult,
 * GetGameSkuInfoFromGameProductInfo, ExecuteStoreBrowse, AbortReq, DestroyReq.
 * The rest -- the EmptyStoreCheck pair, the DoCheckout/DoProductBrowse/
 * DoDlList/DoProductCode/DoServiceList async families, GetPrice, the
 * GetProductInfoList family, GetBGDLAvailability -- stay UNREGISTERED on
 * purpose: they log as unimplemented imports with their name, which is what
 * AGENTS.md asks for, rather than answering a fabricated success.
 *
 * THE STORE IS UNAVAILABLE. That is not a stub, it is the console's answer
 * when no store is reachable, and it is RPCS3's:
 *
 *   - Init / CreateCtx / CreateSessionStart / CreateSessionFinish are LOCAL
 *     objects in RPCS3 -- a flag, an id, and a queued callback -- and they
 *     return CELL_OK (sceNpCommerce2.cpp:129-144, :157-181, :262-288,
 *     :306-357). They must NOT fail: docs/loading-43/REPORT.md measured that a
 *     failed store session makes this title abandon a save load with
 *     "Connection to the server was lost".
 *   - everything that needs the store's DATA answers with an error:
 *     SCE_NP_COMMERCE2_ERROR_SERVER_MAINTENANCE from GetCategoryInfo
 *     (:449-450, "Hack to stop crashes in some games"), and
 *     SCE_NP_COMMERCE2_ERROR_INVALID_RESULT_DATA from every Init*Result,
 *     because check_and_get_data refuses every payload (:82-92).
 *
 * Every constant, every check and their ORDER is cited in commerce2_hle.h and
 * repeated inline below at the point of use, with the RPCS3 line.
 */
#include "common.h"
#include "runtime/pgwrite.h"
#include "commerce2_hle.h"
#include "runtime/cbwatch.h"
#include "runtime/loader.h"
#include "runtime/sysutil_events.h"

#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace hle {

namespace {

namespace com2 = commerce2;

/* One module object, not globals: RPCS3 keeps this in np_handler
 * (refs/.../np_handler.h:140 is_NP_Com2_init) and the contexts in idm
 * (refs/.../np_contexts.h:256-268). The lock is for the sysutil delivery
 * posting from a different thread than the caller. */
std::mutex g_lock;
com2::Module g_module;

/* SceNpId is 36 bytes: SceNpOnlineId handle (char data[16+1]; char dummy[3]),
 * u8 opt[8], u8 reserved[8] -- refs/.../sceNp.h:1266-1291. Only its address and
 * size matter here; the context stores the pointer RPCS3 copies from
 * (np_contexts.cpp:220-225 memcpy's sizeof(SceNpId) from it). */
constexpr uint32_t NP_ID_SIZE = 36;

/* PS3BASE_COMMERCE2_LOG reports every entry point and its return, so the
 * store-unavailable path is visible in a run rather than inferred. */
#define COM2_LOG(name, ...)                                                    \
    do {                                                                       \
        static const bool on = std::getenv("PS3BASE_COMMERCE2_LOG") != nullptr;\
        if (on) LOG_INFO("sceNpCommerce2", name " " __VA_ARGS__);               \
    } while (0)

/* The five-argument context handler, refs/.../sceNpCommerce2.h:335:
 *   void(u32 ctx_id, u32 subject_id, s32 event, s32 error_code, void* arg)
 * delivered exactly as sceNpCommerce2.cpp:282 calls it. */
void post_session_event(uint32_t cb, uint32_t arg, uint32_t ctx_id) {
    const com2::Event ev = com2::session_done_event(ctx_id);
    sysutil_post([cb, arg, ev](ppu::Context& c) {
        call_guest(c, cb, ev.ctx_id, ev.subject_id,
                   static_cast<uint32_t>(ev.event), static_cast<uint32_t>(ev.error_code), arg);
    });
}

}  // namespace

/* sceNpCommerce2Init(). refs/.../sceNpCommerce2.cpp:129-144. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2Init) {
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.init(com2::np_manager_initialised());
    COM2_LOG("Init", "-> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2Term(). refs/.../sceNpCommerce2.cpp:146-155 -- no
 * NOT_INITIALIZED check; Term before Init succeeds. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2Term) {
    std::lock_guard lk(g_lock);
    g_module.term();
    COM2_LOG("Term", "-> 0");
    ret(ctx, 0);
}

/* sceNpCommerce2CreateCtx(version, npId, handler, arg, ctx_id).
 * refs/.../sceNpCommerce2.cpp:157-181: the version check precedes the npId
 * check, and a null ctx_id is only a warning -- the call still succeeds. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateCtx) {
    const uint32_t version = a32(ctx, 0);
    const uint32_t np_id = a32(ctx, 1);
    const uint32_t handler = a32(ctx, 2);
    const uint32_t arg = a32(ctx, 3);
    const uint32_t ctx_id_out = a32(ctx, 4);
    std::lock_guard lk(g_lock);
    uint32_t out = 0;
    const int32_t r = g_module.create_ctx(version, np_id != 0, np_id, handler, arg,
                                          ctx_id_out != 0, &out);
    /* idm's allocation happens before the store, so a successful call writes
     * ctx_id even when the pointer is present (sceNpCommerce2.cpp:175-176). */
    if (r == 0 && ctx_id_out) vm::write32(ctx_id_out, out);
    if (handler) cbwatch::note_register("commerce2", handler, arg);
    COM2_LOG("CreateCtx", "version=%u npId=0x%08x handler=0x%08x arg=0x%08x out=0x%08x -> 0x%08x ctx=%u",
             version, np_id, handler, arg, ctx_id_out, static_cast<uint32_t>(r), out);
    ret(ctx, r);
}

/* sceNpCommerce2DestroyCtx(ctx_id). refs/.../sceNpCommerce2.cpp:183-198. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2DestroyCtx) {
    const uint32_t ctx_id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.destroy_ctx(ctx_id);
    COM2_LOG("DestroyCtx", "ctx=%u -> 0x%08x", ctx_id, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2CreateSessionStart(ctx_id). refs/.../sceNpCommerce2.cpp:262-288:
 * the session is a local object; the only effect is the queued
 * SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE for the context's handler. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionStart) {
    const uint32_t ctx_id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    bool deliver = false;
    const int32_t r = g_module.create_session_start(ctx_id, &deliver);
    if (r == 0 && deliver) {
        const com2::ContextRecord* c = g_module.find_ctx(ctx_id);
        if (c) post_session_event(c->handler, c->arg, ctx_id);
    }
    COM2_LOG("CreateSessionStart", "ctx=%u deliver=%d -> 0x%08x", ctx_id,
             deliver ? 1 : 0, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2CreateSessionAbort(ctx_id). refs/.../sceNpCommerce2.cpp:290-304:
 * validates the context and delivers NOTHING. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionAbort) {
    const uint32_t ctx_id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.create_session_abort(ctx_id);
    COM2_LOG("CreateSessionAbort", "ctx=%u -> 0x%08x", ctx_id, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2CreateSessionFinish(ctx_id, sessionInfo).
 * refs/.../sceNpCommerce2.cpp:306-357. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2CreateSessionFinish) {
    const uint32_t ctx_id = a32(ctx, 0);
    const uint32_t info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.create_session_finish(ctx_id);
    if (r == 0 && info && vm::range_mapped(info, com2::SESSION_INFO_BYTES_WIDE)) {
        /* sdk_ver from the process, refs/.../sys_process.cpp:308-313. This
         * title's SDK is 0x300001, so it takes the 44-byte branch; a pre-3.00
         * disc gets only currencyCode+decimals cleared (sceNpCommerce2.cpp:322
         * -329). The bytes are ZEROED, not filled: there is no store to read a
         * currency from, and RPCS3's USD block is behind "if (false)". */
        const uint32_t n = com2::session_info_zero_bytes(static_cast<int32_t>(loader::g_process.sdk_version));
        pgw::fill(info, 0, n);
        vm::host_write(info, n);
    }
    COM2_LOG("CreateSessionFinish", "ctx=%u info=0x%08x -> 0x%08x", ctx_id, info,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetCategoryContentsCreateReq(ctx_id, req_id).
 * refs/.../sceNpCommerce2.cpp:359-372. It writes NOTHING: the request id comes
 * from the store and there is no store. The caller's slot keeps whatever it
 * held, which is what the console does too. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetCategoryContentsCreateReq) {
    const uint32_t req_id = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_category_contents_create_req(req_id != 0);
    COM2_LOG("GetCategoryContentsCreateReq", "req_id=0x%08x -> 0x%08x", req_id,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetCategoryContentsStart(req_id, categoryId, startPosition,
 * maxCountOfResults). refs/.../sceNpCommerce2.cpp:374-387. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetCategoryContentsStart) {
    const uint32_t category_id = a32(ctx, 1);
    const uint32_t max_count = a32(ctx, 3);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_category_contents_start(category_id != 0, max_count);
    COM2_LOG("GetCategoryContentsStart", "categoryId=0x%08x max=%u -> 0x%08x", category_id,
             max_count, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetCategoryContentsGetResult(req_id, buf, buf_size, fill_size).
 * refs/.../sceNpCommerce2.cpp:399-412: buf_size is checked BEFORE the
 * pointers. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetCategoryContentsGetResult) {
    const uint32_t buf = a32(ctx, 1), buf_size = a32(ctx, 2), fill_size = a32(ctx, 3);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_category_contents_get_result(buf_size, buf != 0, fill_size != 0);
    COM2_LOG("GetCategoryContentsGetResult", "buf=0x%08x size=%u -> 0x%08x", buf, buf_size,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2InitGetCategoryContentsResult(result, data, data_size).
 * refs/.../sceNpCommerce2.cpp:414-435 -- always INVALID_RESULT_DATA, because
 * check_and_get_data is a TODO that returns it for every payload. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2InitGetCategoryContentsResult) {
    const uint32_t result = a32(ctx, 0), data = a32(ctx, 1), data_size = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.init_get_category_contents_result(result != 0, data != 0, data_size);
    COM2_LOG("InitGetCategoryContentsResult", "result=0x%08x data=0x%08x size=%u -> 0x%08x",
             result, data, data_size, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetCategoryInfo(result, categoryInfo).
 * refs/.../sceNpCommerce2.cpp:437-451 -- THIS is the store-unavailable answer:
 * SCE_NP_COMMERCE2_ERROR_SERVER_MAINTENANCE after the argument checks
 * (:449-450, "Hack to stop crashes in some games"). */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetCategoryInfo) {
    const uint32_t result = a32(ctx, 0), info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_category_info(result != 0, info != 0);
    COM2_LOG("GetCategoryInfo", "result=0x%08x info=0x%08x -> 0x%08x (store unavailable)",
             result, info, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetContentInfo(result, index, contentInfo).
 * refs/.../sceNpCommerce2.cpp:453-466. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetContentInfo) {
    const uint32_t result = a32(ctx, 0), index = a32(ctx, 1), info = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_content_info(result != 0, info != 0);
    COM2_LOG("GetContentInfo", "index=%u -> 0x%08x", index, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetCategoryInfoFromContentInfo(contentInfo, categoryInfo).
 * refs/.../sceNpCommerce2.cpp:468-481. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetCategoryInfoFromContentInfo) {
    const uint32_t content = a32(ctx, 0), info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_category_info_from_content_info(content != 0, info != 0);
    COM2_LOG("GetCategoryInfoFromContentInfo", "-> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetGameProductInfoFromContentInfo(contentInfo, gameProductInfo).
 * refs/.../sceNpCommerce2.cpp:483-496. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetGameProductInfoFromContentInfo) {
    const uint32_t content = a32(ctx, 0), info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_game_product_info_from_content_info(content != 0, info != 0);
    COM2_LOG("GetGameProductInfoFromContentInfo", "-> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2DestroyGetCategoryContentsResult(result).
 * refs/.../sceNpCommerce2.cpp:498-502: CELL_OK unconditionally, with NO
 * initialisation check and NO argument check. Kept as-is. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2DestroyGetCategoryContentsResult) {
    const uint32_t result = a32(ctx, 0);
    COM2_LOG("DestroyGetCategoryContentsResult", "result=0x%08x -> 0", result);
    ret(ctx, com2::Module::destroy_result());
}

/* sceNpCommerce2GetProductInfoCreateReq(ctx_id, req_id).
 * refs/.../sceNpCommerce2.cpp:504-521: unlike its category twin, this also
 * requires a live context. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetProductInfoCreateReq) {
    const uint32_t ctx_id = a32(ctx, 0), req_id = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_product_info_create_req(ctx_id, req_id != 0);
    COM2_LOG("GetProductInfoCreateReq", "ctx=%u req_id=0x%08x -> 0x%08x", ctx_id, req_id,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetProductInfoStart(req_id, categoryId, productId).
 * refs/.../sceNpCommerce2.cpp:523-536: categoryId is NOT checked. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetProductInfoStart) {
    const uint32_t product_id = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_product_info_start(product_id != 0);
    COM2_LOG("GetProductInfoStart", "productId=0x%08x -> 0x%08x", product_id,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetProductInfoGetResult(req_id, buf, buf_size, fill_size).
 * refs/.../sceNpCommerce2.cpp:538-551. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetProductInfoGetResult) {
    const uint32_t buf = a32(ctx, 1), buf_size = a32(ctx, 2), fill_size = a32(ctx, 3);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_product_info_get_result(buf_size, buf != 0, fill_size != 0);
    COM2_LOG("GetProductInfoGetResult", "buf=0x%08x size=%u -> 0x%08x", buf, buf_size,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2InitGetProductInfoResult(result, data, data_size).
 * refs/.../sceNpCommerce2.cpp:553-574. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2InitGetProductInfoResult) {
    const uint32_t result = a32(ctx, 0), data = a32(ctx, 1), data_size = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.init_get_product_info_result(result != 0, data != 0, data_size);
    COM2_LOG("InitGetProductInfoResult", "result=0x%08x data=0x%08x size=%u -> 0x%08x",
             result, data, data_size, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2GetGameProductInfo(result, gameProductInfo).
 * refs/.../sceNpCommerce2.cpp:576-589. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetGameProductInfo) {
    const uint32_t result = a32(ctx, 0), info = a32(ctx, 1);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_game_product_info(result != 0, info != 0);
    COM2_LOG("GetGameProductInfo", "-> 0x%08x", static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2DestroyGetProductInfoResult(result).
 * refs/.../sceNpCommerce2.cpp:591-595: CELL_OK unconditionally. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2DestroyGetProductInfoResult) {
    const uint32_t result = a32(ctx, 0);
    COM2_LOG("DestroyGetProductInfoResult", "result=0x%08x -> 0", result);
    ret(ctx, com2::Module::destroy_result());
}

/* sceNpCommerce2GetGameSkuInfoFromGameProductInfo(gameProductInfo, index,
 * gameSkuInfo). refs/.../sceNpCommerce2.cpp:735-748. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2GetGameSkuInfoFromGameProductInfo) {
    const uint32_t product_info = a32(ctx, 0), index = a32(ctx, 1), sku_info = a32(ctx, 2);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.get_game_sku_info(product_info != 0, sku_info != 0);
    COM2_LOG("GetGameSkuInfoFromGameProductInfo", "index=%u -> 0x%08x", index,
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2ExecuteStoreBrowse(targetType, targetId, userdata).
 * refs/.../sceNpCommerce2.cpp:94-110: the target TYPE is checked before the id,
 * and a PRODUCT_CODE browse legitimately carries no id. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2ExecuteStoreBrowse) {
    const int32_t target_type = s32(ctx, 0);
    const uint32_t target_id = a32(ctx, 1);
    const std::string id = gstr(target_id, 1024);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.execute_store_browse(target_type, target_id != 0, !id.empty());
    COM2_LOG("ExecuteStoreBrowse", "type=%d id='%s' -> 0x%08x", target_type, id.c_str(),
             static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2AbortReq(req_id). refs/.../sceNpCommerce2.cpp:1000-1010. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2AbortReq) {
    const uint32_t req_id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.abort_req();
    COM2_LOG("AbortReq", "req_id=%u -> 0x%08x", req_id, static_cast<uint32_t>(r));
    ret(ctx, r);
}

/* sceNpCommerce2DestroyReq(req_id). refs/.../sceNpCommerce2.cpp:1012-1022. */
HLE_FUNC(sceNpCommerce2, sceNpCommerce2DestroyReq) {
    const uint32_t req_id = a32(ctx, 0);
    std::lock_guard lk(g_lock);
    const int32_t r = g_module.destroy_req();
    COM2_LOG("DestroyReq", "req_id=%u -> 0x%08x", req_id, static_cast<uint32_t>(r));
    ret(ctx, r);
}

}  // namespace hle
