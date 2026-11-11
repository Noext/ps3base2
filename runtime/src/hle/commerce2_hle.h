/* ps3base -- the decision logic of sceNpCommerce2, in a header so a test
 * drives the same code the imports run.
 *
 * Same reason as cellgame_hle.h: a test that re-implements the thing under test
 * proves nothing. Everything here is host-side and guest-memory-free: the
 * handlers in sceNpCommerce2.cpp read guest arguments, call these functions,
 * and write back exactly what the reference writes.
 *
 * Every constant, every check and its ORDER comes from RPCS3's module, by file
 * and line:
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:8-50    SceNpCommerce2Error
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:56-80   event ids
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:115-117 store browse types
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:150-181 versions, limits
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:205-217 SceNpCommerce2SessionInfo
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.h:335     SceNpCommerce2Handler
 *   refs/rpcs3/rpcs3/Emu/NP/np_contexts.h:256-268              commerce2_ctx id space
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpCommerce2.cpp:73-92 helper pair
 *   and one citation per function below.
 *
 * THE STORE IS UNAVAILABLE, AND THAT IS THE IMPLEMENTATION.
 *
 * RPCS3 answers this module without a network: Init/CreateCtx/CreateSession*
 * are LOCAL objects (flags, ids, a queued callback), and every call that would
 * need the store's DATA either answers "invalid result data" because there is
 * no result to parse, or answers SCE_NP_COMMERCE2_ERROR_SERVER_MAINTENANCE --
 * that one line, sceNpCommerce2.cpp:449-450 ("Hack to stop crashes in some
 * games"), is the store-unavailable answer, and it is what a console with no
 * reachable store returns to a title that asks for category data.
 *
 * Note what is NOT done here, deliberately: CreateCtx and CreateSessionStart do
 * NOT fail. RPCS3 returns CELL_OK from both, and docs/loading-43/REPORT.md
 * measured the consequence of failing them: Red Dead Redemption abandons a save
 * load with "Connection to the server was lost". AGENTS.md is explicit that an
 * implemented function answers what the console answers, success included.
 */
#ifndef PS3BASE_HLE_COMMERCE2_H
#define PS3BASE_HLE_COMMERCE2_H

#include <cstdint>

namespace hle {
namespace commerce2 {

/* refs/.../sceNpCommerce2.h:8-35. Only the codes reachable from the 26 imports
 * this title uses are listed; the rest of the enum is the server's vocabulary. */
constexpr int32_t ERR_NOT_INITIALIZED = static_cast<int32_t>(0x80023001);
constexpr int32_t ERR_ALREADY_INITIALIZED = static_cast<int32_t>(0x80023002);
constexpr int32_t ERR_INVALID_ARGUMENT = static_cast<int32_t>(0x80023003);
constexpr int32_t ERR_UNSUPPORTED_VERSION = static_cast<int32_t>(0x80023004);
constexpr int32_t ERR_CTX_NOT_FOUND = static_cast<int32_t>(0x8002300c);
constexpr int32_t ERR_INVALID_TARGET_TYPE = static_cast<int32_t>(0x8002301a);
constexpr int32_t ERR_INVALID_TARGET_ID = static_cast<int32_t>(0x8002301b);
constexpr int32_t ERR_INVALID_RESULT_DATA = static_cast<int32_t>(0x80023014);
constexpr int32_t ERR_SERVER_MAINTENANCE = static_cast<int32_t>(0x80023016);
constexpr int32_t ERR_INSUFFICIENT_BUF_SIZE = static_cast<int32_t>(0x80023018);

/* refs/.../sceNp.h:57 -- the NP manager's own "not initialised", which Init
 * returns before Commerce2 is up (not the Commerce2 code above). */
constexpr int32_t NP_ERR_NOT_INITIALIZED = static_cast<int32_t>(0x8002aa01);

/* refs/.../sceNpCommerce2.h:57. Delivered to the handler as the third
 * argument, with subject_id 0 and error 0 (sceNpCommerce2.cpp:282). */
constexpr uint32_t EVENT_CREATE_SESSION_DONE = 0x0011;

/* refs/.../sceNpCommerce2.h:150. Init refuses anything above it. */
constexpr uint32_t VERSION = 2;
/* refs/.../sceNpCommerce2.h:151. */
constexpr uint32_t CTX_MAX = 1;

/* refs/.../np_contexts.h:261-262: the context's id space is id_base 0x5001,
 * step 1, count SCE_NP_COMMERCE2_CTX_MAX (1). idm::make returns the allocated
 * id, so the FIRST context a title ever creates is 0x5001, not 1. */
constexpr uint32_t CTX_ID_BASE = 0x5001;

/* refs/.../sceNpCommerce2.h:170. Every GetResult call demands a buffer of at
 * least this size before it looks at pointers (sceNpCommerce2.cpp:408). */
constexpr uint32_t RECV_BUF_SIZE = 262144;
/* refs/.../sceNpCommerce2.h:174. */
constexpr uint32_t GETCAT_MAX_COUNT = 60;

/* check_and_get_data's floor, sceNpCommerce2.cpp:84. */
constexpr uint32_t RESULT_DATA_MIN = 60;

/* refs/.../sceNpCommerce2.h:115-117. */
constexpr int32_t STORE_BROWSE_TYPE_CATEGORY = 1;
constexpr int32_t STORE_BROWSE_TYPE_PRODUCT = 2;
constexpr int32_t STORE_BROWSE_TYPE_PRODUCT_CODE = 3;

/* sceNpCommerce2.cpp:322-329. An SDK older than 3.00 gets the first 8 bytes of
 * SceNpCommerce2SessionInfo cleared (currencyCode, decimals); from 3.00 the
 * first 44 are ("sizeof(SceNpCommerce2SessionInfo) - reserved"). */
constexpr int32_t SDK_SESSION_INFO_NARROW = 0x300000;
constexpr uint32_t SESSION_INFO_BYTES_NARROW = 8;
constexpr uint32_t SESSION_INFO_BYTES_WIDE = 44;

/* The handler's signature, refs/.../sceNpCommerce2.h:335:
 *   void(u32 ctx_id, u32 subject_id, s32 event, s32 error_code, void* arg)
 * Five arguments, the caller's pointer LAST. */
struct Event {
    uint32_t ctx_id;
    uint32_t subject_id;
    int32_t event;
    int32_t error_code;
};

inline Event session_done_event(uint32_t ctx_id) {
    return {ctx_id, 0, static_cast<int32_t>(EVENT_CREATE_SESSION_DONE), 0};
}

/* sceNpCommerce2.cpp:322-329, as a byte count. */
inline uint32_t session_info_zero_bytes(int32_t sdk_version) {
    return sdk_version < SDK_SESSION_INFO_NARROW ? SESSION_INFO_BYTES_NARROW
                                                 : SESSION_INFO_BYTES_WIDE;
}

/* One commerce2 context, refs/.../np_contexts.h:256-268 plus
 * np_contexts.cpp:220-225 (the constructor copies version and npId and stores
 * the callback pair). */
struct ContextRecord {
    bool used = false;
    uint32_t version = 0;
    uint32_t np_id = 0;
    uint32_t handler = 0;
    uint32_t arg = 0;
};

/* The module's state, and one function per imported entry point.
 *
 * The flags mirror RPCS3's np_handler: is_NP_init (the MANAGER, set by
 * sceNpInit) and is_NP_Com2_init (refs/.../np_handler.h:140). Both are needed:
 * Init answers SCE_NP_ERROR_NOT_INITIALIZED when the manager is down and
 * SCE_NP_COMMERCE2_ERROR_ALREADY_INITIALIZED when Commerce2 is already up, in
 * that order (sceNpCommerce2.cpp:135-141).
 */
class Module {
public:
    /* sceNpCommerce2Init, sceNpCommerce2.cpp:129-144. */
    int32_t init(bool np_manager_up) {
        if (com2_init_) return ERR_ALREADY_INITIALIZED;
        if (!np_manager_up) return NP_ERR_NOT_INITIALIZED;
        com2_init_ = true;
        return 0;
    }

    /* sceNpCommerce2Term, sceNpCommerce2.cpp:146-155. No NOT_INITIALIZED check:
     * Term on a module that was never initialised succeeds. And it does NOT
     * destroy the contexts -- RPCS3's Term only clears is_NP_Com2_init, while
     * the contexts live in idm and outlive it. Clearing them here would make a
     * DestroyCtx after Term answer CTX_NOT_FOUND where the console answers
     * CELL_OK. */
    int32_t term() {
        com2_init_ = false;
        return 0;
    }

    bool initialized() const { return com2_init_; }

    /* sceNpCommerce2CreateCtx, sceNpCommerce2.cpp:157-181. The version check
     * comes BEFORE the npId check, and a null ctx_id pointer is only a warning:
     * the call still succeeds, it just has nowhere to return the id. */
    int32_t create_ctx(uint32_t version, bool has_np_id, uint32_t np_id, uint32_t handler,
                       uint32_t arg, bool has_ctx_id_out, uint32_t* ctx_id_out) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (version > VERSION) return ERR_UNSUPPORTED_VERSION;
        /* sceNpCommerce2.cpp:169-170: the CTX_MAX check is "if (false) // TODO"
         * in RPCS3. Reaching CTX_MAX therefore does NOT fail here; what fails
         * is idm's allocation, and create_commerce2_context then returns the id
         * space's INVALID id, which CreateCtx stores without looking at it. For
         * id_base 0x5001 the invalid value is 0 (idm.h id_traits::invalid is
         * "-+!base"), so a second context comes back as id 0. */
        if (!has_np_id) return ERR_INVALID_ARGUMENT;

        /* sceNpCommerce2.cpp:175-178: create_commerce2_context is called ONLY
         * inside the if (ctx_id) branch. With a null ctx_id pointer RPCS3 warns
         * and creates NOTHING, then returns CELL_OK -- so a caller that passed
         * null has no context afterwards, and a later CreateSessionStart on any
         * id is CTX_NOT_FOUND. Allocating anyway would leave an invisible,
         * never destroyable context behind and consume the single CTX_MAX slot. */
        if (!has_ctx_id_out || !ctx_id_out) return 0;

        uint32_t id = 0;
        for (uint32_t i = 0; i < CTX_MAX; i++) {
            if (!contexts_[i].used) {
                contexts_[i].used = true;
                contexts_[i].version = version;
                contexts_[i].np_id = np_id;
                contexts_[i].handler = handler;
                contexts_[i].arg = arg;
                id = CTX_ID_BASE + i;
                break;
            }
        }
        *ctx_id_out = id;
        return 0;
    }

    /* sceNpCommerce2DestroyCtx, sceNpCommerce2.cpp:183-198. */
    int32_t destroy_ctx(uint32_t ctx_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        for (uint32_t i = 0; i < CTX_MAX; i++) {
            if (contexts_[i].used && CTX_ID_BASE + i == ctx_id) {
                contexts_[i] = ContextRecord{};
                return 0;
            }
        }
        return ERR_CTX_NOT_FOUND;
    }

    const ContextRecord* find_ctx(uint32_t ctx_id) const {
        for (uint32_t i = 0; i < CTX_MAX; i++)
            if (contexts_[i].used && CTX_ID_BASE + i == ctx_id) return &contexts_[i];
        return nullptr;
    }

    /* sceNpCommerce2CreateSessionStart, sceNpCommerce2.cpp:262-288. Local
     * object: the only effect is queuing SCE_NP_COMMERCE2_EVENT_CREATE_SESSION_DONE
     * for the context's handler. No network, and CELL_OK. */
    int32_t create_session_start(uint32_t ctx_id, bool* deliver_event) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        const ContextRecord* c = find_ctx(ctx_id);
        if (!c) return ERR_CTX_NOT_FOUND;
        *deliver_event = c->handler != 0;
        return 0;
    }

    /* sceNpCommerce2CreateSessionAbort, sceNpCommerce2.cpp:290-304. Abort
     * validates the context and delivers NOTHING -- not even ABORTED. */
    int32_t create_session_abort(uint32_t ctx_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!find_ctx(ctx_id)) return ERR_CTX_NOT_FOUND;
        return 0;
    }

    /* sceNpCommerce2CreateSessionFinish, sceNpCommerce2.cpp:306-357. */
    int32_t create_session_finish(uint32_t ctx_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!find_ctx(ctx_id)) return ERR_CTX_NOT_FOUND;
        return 0;
    }

    /* sceNpCommerce2EmptyStoreCheckStart ends at :231, not imported here. */

    /* sceNpCommerce2GetCategoryContentsCreateReq, sceNpCommerce2.cpp:359-372.
     * It checks req_id and then writes NOTHING: the request id comes from the
     * store, and there is no store. */
    int32_t get_category_contents_create_req(bool has_req_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_req_id) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetCategoryContentsStart, sceNpCommerce2.cpp:374-387. */
    int32_t get_category_contents_start(bool has_category_id, uint32_t max_count) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_category_id || max_count > GETCAT_MAX_COUNT) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetCategoryContentsGetResult, sceNpCommerce2.cpp:399-412.
     * The buffer size is checked BEFORE the pointers: a 0x40000-byte demand is
     * an INSUFFICIENT_BUF_SIZE error even when buf and fill_size are null. */
    int32_t get_category_contents_get_result(uint32_t buf_size, bool has_buf, bool has_fill_size) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (buf_size < RECV_BUF_SIZE) return ERR_INSUFFICIENT_BUF_SIZE;
        return get_result(has_buf, buf_size, has_fill_size);
    }

    /* sceNpCommerce2InitGetCategoryContentsResult, sceNpCommerce2.cpp:414-435.
     * It always fails: check_and_get_data refuses every size (see below). */
    int32_t init_get_category_contents_result(bool has_result, bool has_data, uint32_t data_size) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_result || !has_data || !data_size) return ERR_INVALID_ARGUMENT;
        return check_and_get_data(data_size);
    }

    /* sceNpCommerce2GetCategoryInfo, sceNpCommerce2.cpp:437-451.
     *
     * THIS is RPCS3's "store unavailable" answer: after the argument checks it
     * returns SCE_NP_COMMERCE2_ERROR_SERVER_MAINTENANCE with the comment
     * "Hack to stop crashes in some games". A title that asks the store for
     * category data with no store reachable gets exactly this. */
    int32_t get_category_info(bool has_result, bool has_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_result || !has_info) return ERR_INVALID_ARGUMENT;
        return ERR_SERVER_MAINTENANCE;
    }

    /* sceNpCommerce2GetContentInfo, sceNpCommerce2.cpp:453-466. */
    int32_t get_content_info(bool has_result, bool has_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_result || !has_info) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetCategoryInfoFromContentInfo, ...cpp:468-481. */
    int32_t get_category_info_from_content_info(bool has_content, bool has_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_content || !has_info) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetGameProductInfoFromContentInfo, ...cpp:483-496. */
    int32_t get_game_product_info_from_content_info(bool has_content, bool has_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_content || !has_info) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetProductInfoCreateReq, ...cpp:504-521. Unlike the
     * category twin, this one also requires a live context. */
    int32_t get_product_info_create_req(uint32_t ctx_id, bool has_req_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_req_id) return ERR_INVALID_ARGUMENT;
        if (!find_ctx(ctx_id)) return ERR_CTX_NOT_FOUND;
        return 0;
    }

    /* sceNpCommerce2GetProductInfoStart, ...cpp:523-536. categoryId is NOT
     * checked here; only productId. */
    int32_t get_product_info_start(bool has_product_id) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_product_id) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetProductInfoGetResult, ...cpp:538-551. */
    int32_t get_product_info_get_result(uint32_t buf_size, bool has_buf, bool has_fill_size) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (buf_size < RECV_BUF_SIZE) return ERR_INSUFFICIENT_BUF_SIZE;
        return get_result(has_buf, buf_size, has_fill_size);
    }

    /* sceNpCommerce2InitGetProductInfoResult, ...cpp:553-574. */
    int32_t init_get_product_info_result(bool has_result, bool has_data, uint32_t data_size) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_result || !has_data || !data_size) return ERR_INVALID_ARGUMENT;
        return check_and_get_data(data_size);
    }

    /* sceNpCommerce2GetGameProductInfo, ...cpp:576-589. */
    int32_t get_game_product_info(bool has_result, bool has_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_result || !has_info) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2GetGameSkuInfoFromGameProductInfo, ...cpp:735-748. */
    int32_t get_game_sku_info(bool has_product_info, bool has_sku_info) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (!has_product_info || !has_sku_info) return ERR_INVALID_ARGUMENT;
        return 0;
    }

    /* sceNpCommerce2ExecuteStoreBrowse, ...cpp:94-110. The target TYPE is
     * checked before the target ID, and a product-code browse legitimately
     * carries no id. */
    int32_t execute_store_browse(int32_t target_type, bool has_target_id, bool target_id_nonempty) {
        if (!com2_init_) return ERR_NOT_INITIALIZED;
        if (target_type < STORE_BROWSE_TYPE_CATEGORY || target_type > STORE_BROWSE_TYPE_PRODUCT_CODE)
            return ERR_INVALID_TARGET_TYPE;
        if (target_type < STORE_BROWSE_TYPE_PRODUCT_CODE && (!has_target_id || !target_id_nonempty))
            return ERR_INVALID_TARGET_ID;
        return 0;
    }

    /* sceNpCommerce2AbortReq, ...cpp:1000-1010 and DestroyReq, ...cpp:1012-1022:
     * both only require initialisation. There is no request to find, because
     * CreateReq never created one. */
    int32_t abort_req() { return com2_init_ ? 0 : ERR_NOT_INITIALIZED; }
    int32_t destroy_req() { return com2_init_ ? 0 : ERR_NOT_INITIALIZED; }

    /* sceNpCommerce2DestroyGetCategoryContentsResult, ...cpp:498-502 and
     * DestroyGetProductInfoResult, ...cpp:591-595. Both return CELL_OK
     * unconditionally -- NO initialisation check, and no argument check. That
     * asymmetry is RPCS3's and is reproduced rather than tidied. */
    static int32_t destroy_result() { return 0; }

    /* check_and_get_data, sceNpCommerce2.cpp:82-92. RPCS3 refuses a payload
     * under 60 bytes with INVALID_RESULT_DATA, and then refuses everything
     * else too: the extraction is a TODO that unconditionally returns
     * INVALID_RESULT_DATA. There is no store data to parse offline, so "valid
     * result data" would be a fabrication. */
    static int32_t check_and_get_data(uint32_t data_size) {
        if (data_size < RESULT_DATA_MIN) return ERR_INVALID_RESULT_DATA;
        return ERR_INVALID_RESULT_DATA;
    }

    /* get_result, sceNpCommerce2.cpp:389-397, shared by the three GetResult
     * entry points. Nulls are INVALID_ARGUMENT; otherwise CELL_OK with NOTHING
     * written -- buf and fill_size are left as the caller had them. */
    static int32_t get_result(bool has_buf, uint32_t buf_size, bool has_fill_size) {
        if (!has_buf || !buf_size || !has_fill_size) return ERR_INVALID_ARGUMENT;
        return 0;
    }

private:
    bool com2_init_ = false;
    ContextRecord contexts_[CTX_MAX];
};

/* The NP manager's flag, owned by np.cpp (NpState::init). sceNpCommerce2Init
 * answers SCE_NP_ERROR_NOT_INITIALIZED when the manager is down, never
 * SCE_NP_COMMERCE2_ERROR_NOT_INITIALIZED (sceNpCommerce2.cpp:138-139 returns
 * SCE_NP_ERROR_NOT_INITIALIZED for a missing manager and the COMMERCE2 code
 * only for a missing module). */
bool np_manager_initialised();

}  // namespace commerce2
}  // namespace hle

#endif  // PS3BASE_HLE_COMMERCE2_H
