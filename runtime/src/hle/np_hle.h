/* ps3base -- the decision logic of sceNp, in a header so the unit test drives
 * the same code the imports run (same reason as cellgame_hle.h: a test that
 * re-implements the thing under test proves nothing).
 *
 * Everything here is host-side: no guest memory, no PPU context, no network,
 * so tests/np.cpp links nothing at all.
 *
 * The specification is RPCS3's, by file and line. Every constant below cites
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNp.h and every rule cites sceNp.cpp.
 *
 * Offline is a real console state, not a stub: a signed-out console answers
 * SCE_NP_ERROR_OFFLINE (as a NON-error value: RPCS3 wraps it in not_an_error,
 * refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h:75-84) from the identity getters, and
 * refuses the network-only operations with the codes below.
 */
#ifndef PS3BASE_HLE_NP_H
#define PS3BASE_HLE_NP_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace hle {
namespace np {

/* ---- return codes ------------------------------------------------
 * sceNp.h:57-79 NP Manager Utility. */
constexpr int32_t E_NOT_INITIALIZED      = static_cast<int32_t>(0x8002aa01);
constexpr int32_t E_ALREADY_INITIALIZED  = static_cast<int32_t>(0x8002aa02);
constexpr int32_t E_INVALID_ARGUMENT     = static_cast<int32_t>(0x8002aa03);
constexpr int32_t E_ID_NOT_FOUND         = static_cast<int32_t>(0x8002aa06);
constexpr int32_t E_INVALID_STATE        = static_cast<int32_t>(0x8002aa0a);
constexpr int32_t E_OFFLINE              = static_cast<int32_t>(0x8002aa0c);
constexpr int32_t E_INSUFFICIENT_BUFFER  = static_cast<int32_t>(0x8002aa0f);

/* sceNp.h:82-110 sceNpBasic. Note 0x8002a66a is NO_EVENT and 0x8002a66b is
 * EXCEEDS_MAX; the previous runtime constant was named NO_EVENT and held the
 * EXCEEDS_MAX value, so every "is there an NP event" poll reported "too many"
 * instead of "nothing pending". */
constexpr int32_t E_BASIC_NOT_INITIALIZED  = static_cast<int32_t>(0x8002a662);
constexpr int32_t E_BASIC_NOT_SUPPORTED    = static_cast<int32_t>(0x8002a663);
constexpr int32_t E_BASIC_INVALID_ARGUMENT = static_cast<int32_t>(0x8002a665);
constexpr int32_t E_BASIC_NO_EVENT         = static_cast<int32_t>(0x8002a66a);
constexpr int32_t E_BASIC_EXCEEDS_MAX      = static_cast<int32_t>(0x8002a66b);
constexpr int32_t E_BASIC_NOT_REGISTERED   = static_cast<int32_t>(0x8002a66d);
constexpr int32_t E_BASIC_NOT_CONNECTED    = static_cast<int32_t>(0x8002a678);

/* sceNp.h:158-188 sceNpCommunityLookup. */
constexpr int32_t E_COMM_ALREADY_INITIALIZED   = static_cast<int32_t>(0x8002a101);
constexpr int32_t E_COMM_NOT_INITIALIZED       = static_cast<int32_t>(0x8002a102);
constexpr int32_t E_COMM_INVALID_ARGUMENT      = static_cast<int32_t>(0x8002a104);
constexpr int32_t E_COMM_INSUFFICIENT_ARGUMENT = static_cast<int32_t>(0x8002a1a1);
constexpr int32_t E_COMM_INVALID_ID            = static_cast<int32_t>(0x8002a1a3);
constexpr int32_t E_COMM_INVALID_ONLINE_ID     = static_cast<int32_t>(0x8002a1a4);

/* sceNp.h:371-377 auth. */
constexpr int32_t E_AUTH_INVALID_ARGUMENT = static_cast<int32_t>(0x8002a015);

/* sceNp.h:500-523 signaling. */
constexpr int32_t E_SIG_NOT_INITIALIZED  = static_cast<int32_t>(0x8002a801);
constexpr int32_t E_SIG_CTX_NOT_FOUND    = static_cast<int32_t>(0x8002a805);
constexpr int32_t E_SIG_CONN_NOT_FOUND   = static_cast<int32_t>(0x8002a80e);
constexpr int32_t E_SIG_INVALID_ARGUMENT = static_cast<int32_t>(0x8002a815);
constexpr int32_t E_SIG_OWN_NP_ID        = static_cast<int32_t>(0x8002a816);

/* sceNp.h:328-347 DRM. */
constexpr int32_t E_DRM_INVALID_PARAM  = static_cast<int32_t>(0x80029502);
constexpr int32_t E_DRM_NO_ENTITLEMENT = static_cast<int32_t>(0x80029513);

/* sceNp.h:117-132 np util. */
constexpr int32_t E_UTIL_INVALID_ARGUMENT = static_cast<int32_t>(0x8002ab01);
constexpr int32_t E_UTIL_NOT_MATCH        = static_cast<int32_t>(0x8002ab0f);

/* CellError, refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h:97 (CELL_ENOENT). */
constexpr int32_t CELL_ENOENT = static_cast<int32_t>(0x80010006);

/* ---- limits ------------------------------------------------------
 * sceNp.h:1105-1120. */
constexpr uint32_t MAX_MESSAGE_SIZE        = 512;
constexpr uint32_t MAX_PRESENCE_SIZE       = 128;
constexpr uint32_t DESCRIPTION_CHAR_MAX    = 341;
constexpr uint32_t PLAYER_HISTORY_MAX      = 8;
constexpr uint32_t SEND_MSG_MAX_RECIPIENTS = 12;
constexpr uint32_t COOKIE_MAX_SIZE         = 1024;
constexpr int32_t  TICKET_PARAM_FIRST      = 0;   /* SCE_NP_TICKET_PARAM_SERIAL_ID */
constexpr int32_t  TICKET_PARAM_LAST       = 11;  /* SCE_NP_TICKET_PARAM_SUBJECT_DOB */
constexpr uint64_t LOOKUP_MIN_TIMEOUT      = 10000000; /* 10 s, sceNp.cpp:3376 */

/* sceNp.h:853-857. */
constexpr int32_t STATUS_OFFLINE        = -1;
constexpr int32_t STATUS_GETTING_TICKET = 0;
constexpr int32_t STATUS_LOGGING_IN     = 2;
constexpr int32_t STATUS_ONLINE         = 3;

/* sceNp.h:987-989 and :995-1000. */
constexpr int32_t CONN_INACTIVE = 0;
constexpr int32_t CONN_PENDING  = 1;
constexpr int32_t CONN_ACTIVE   = 2;

/* SceNpId is 0x24 bytes: SceNpOnlineId handle { data[16+1], dummy[3] }, opt[8],
 * reserved[8]. sceNp.h:1266-1298 (CHECK_SIZE_ALIGN(SceNpId, 0x24, 1)). */
constexpr uint32_t NPID_SIZE = 0x24;

/* np::is_same_npid, refs/rpcs3/rpcs3/Emu/NP/np_helpers.cpp:124-146: the first
 * 16 bytes of handle.data decide, nothing else. */
inline bool same_npid(const uint8_t* a, const uint8_t* b) {
    return std::strncmp(reinterpret_cast<const char*>(a),
                        reinterpret_cast<const char*>(b), 16) == 0;
}

/* "Is this peer our own NP id?" RPCS3 asks np::is_same_npid(nph.get_npid(), peer)
 * (sceNp.cpp:6967, :7129), and np_handler::get_npid() is the identity the
 * manager holds -- all zeros until a sign-in writes one. Comparing against that
 * same all-zero self is therefore the reference behaviour. */
inline bool is_own_npid(const uint8_t* peer, const uint8_t* self) {
    return same_npid(peer, self);
}

/* ---- state -------------------------------------------------------
 * One object, shared by every sceNp handler (np.cpp and sceNp.cpp alike).
 * A function-local static, so a test including this header drives the same
 * code the runtime runs and needs no link step. */
struct TransCtx {
    int32_t id = 0;
    int32_t title = 0;
    uint64_t timeout = 0;
    bool complete = false;
    int32_t result = 0;
};

struct SigCtx {
    uint32_t id = 0;
    uint32_t handler = 0;
    uint32_t ext_handler = 0;
    uint32_t arg = 0;
    uint32_t ext_arg = 0;
};

struct Conn {
    uint32_t id = 0;
    uint8_t npid[NPID_SIZE] = {};
    int32_t status = CONN_INACTIVE;
    uint32_t addr = 0;
    uint16_t port = 0;
};

struct State {
    bool np_init = false;
    bool lookup_init = false;
    bool score_init = false;
    /* RPCS3 keeps a single basic handler slot: a second registration is
     * EXCEEDS_MAX (sceNp.cpp:937-940, :963-966). */
    bool basic_handler_registered = false;
    uint32_t basic_handler = 0;
    uint32_t basic_arg = 0;
    uint32_t manager_cb = 0;
    uint32_t manager_cb_arg = 0;
    /* The ticket is empty without a PSN session; every reader below treats an
     * empty ticket as RPCS3's ticket.empty() branch. */
    uint32_t ticket_size = 0;
    int32_t next_id = 1;
    int32_t next_req_id = 1;
    std::vector<int32_t> title_ctxs;
    std::vector<TransCtx> trans;
    std::vector<SigCtx> sigs;
    std::vector<Conn> conns;
};

inline State& state() {
    static State s;
    return s;
}

/* A result: the return code plus, when the call writes one, the value.
 * has_value says the caller's output structure must be stored. */
struct Result {
    int64_t code = 0;
    bool has_value = false;
    int32_t value = 0;
};

inline Result ok(int64_t code = 0) { return Result{code, false, 0}; }
inline Result write(int32_t v) { return Result{0, true, v}; }
inline Result write_with_error(int64_t code, int32_t v) { return Result{code, true, v}; }

/* ---- sceNpLookup, sceNp.cpp:3232-3568 ---------------------------------- */
inline int64_t lookup_init(State& s) {
    /* sceNp.cpp:3238-3248: already-initialised is checked BEFORE NP init. */
    if (s.lookup_init) return E_COMM_ALREADY_INITIALIZED;
    if (!s.np_init) return E_NOT_INITIALIZED;
    s.lookup_init = true;
    return 0;
}

inline int64_t lookup_term(State& s) {
    /* sceNp.cpp:3259-3267. */
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    if (!s.np_init) return E_NOT_INITIALIZED;
    s.lookup_init = false;
    return 0;
}

/* sceNpLookupCreateTitleCtx, sceNp.cpp:3274-3296. The id is handed back as a
 * not_an_error value (a positive id), destroyed by DestroyTitleCtx. */
inline Result lookup_create_title_ctx(State& s, bool have_comm, bool have_self) {
    if (!s.lookup_init) return ok(E_COMM_NOT_INITIALIZED);
    if (!have_comm || !have_self) return ok(E_COMM_INSUFFICIENT_ARGUMENT);
    const int32_t id = s.next_id++;
    s.title_ctxs.push_back(id);
    return write(id);
}

inline int64_t lookup_destroy_title_ctx(State& s, int32_t id) {
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    for (size_t i = 0; i < s.title_ctxs.size(); i++) {
        if (s.title_ctxs[i] != id) continue;
        s.title_ctxs.erase(s.title_ctxs.begin() + static_cast<long>(i));
        return 0;
    }
    return E_COMM_INVALID_ID; /* sceNp.cpp:3309-3311 */
}

/* sceNpLookupCreateTransactionCtx, sceNp.cpp:3315-3344: ONLINE is required
 * before the id is even looked up. */
inline Result lookup_create_transaction_ctx(State& s, int32_t title_id, int32_t psn_status) {
    if (!s.lookup_init) return ok(E_COMM_NOT_INITIALIZED);
    if (psn_status != STATUS_ONLINE) return ok(E_COMM_INVALID_ONLINE_ID);
    bool found = false;
    for (int32_t t : s.title_ctxs) found = found || (t == title_id);
    if (!found) return ok(E_COMM_INVALID_ID);
    TransCtx t;
    t.id = s.next_id++;
    t.title = title_id;
    s.trans.push_back(t);
    return write(t.id);
}

inline int64_t lookup_destroy_transaction_ctx(State& s, int32_t id) {
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    for (size_t i = 0; i < s.trans.size(); i++) {
        if (s.trans[i].id != id) continue;
        s.trans.erase(s.trans.begin() + static_cast<long>(i));
        return 0;
    }
    return E_COMM_INVALID_ID;
}

/* sceNpLookupSetTimeout, sceNp.cpp:3365-3395: the timeout is rejected before
 * the id is resolved, and a transaction ctx or a title ctx will do. */
inline int64_t lookup_set_timeout(State& s, int32_t id, uint64_t timeout) {
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    if (timeout < LOOKUP_MIN_TIMEOUT) return E_COMM_INVALID_ARGUMENT;
    for (auto& t : s.trans) if (t.id == id) { t.timeout = timeout; return 0; }
    for (int32_t t : s.title_ctxs) if (t == id) return 0;
    return E_COMM_INVALID_ID;
}

/* sceNpLookupPollAsync, sceNp.cpp:3442-3469: an unfinished transaction is
 * not_an_error(1), not an error. */
inline Result lookup_poll_async(State& s, int32_t id) {
    if (!s.lookup_init) return ok(E_COMM_NOT_INITIALIZED);
    for (const auto& t : s.trans) {
        if (t.id != id) continue;
        if (!t.complete) return ok(1);
        return write(t.result);
    }
    return ok(E_COMM_INVALID_ID);
}

/* The online gate shared by every lookup request. sceNp.cpp:3491-3494
 * (lookup_npid), :3838-3841 (title small storage), :3867-3870. Both return
 * INVALID_ONLINE_ID offline. */
inline int64_t lookup_gate(int32_t psn_status) {
    return psn_status == STATUS_ONLINE ? 0 : E_COMM_INVALID_ONLINE_ID;
}

/* sceNpLookupNpIdAsync, sceNp.cpp:3471-3520. */
inline int64_t lookup_npid_async(State& s, bool have_online_id, int32_t id, int32_t psn_status) {
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    if (!have_online_id) return E_COMM_INSUFFICIENT_ARGUMENT;
    bool found = false;
    for (const auto& t : s.trans) found = found || (t.id == id);
    if (!found) return E_COMM_INVALID_ID;
    return lookup_gate(psn_status);
}

/* sceNpLookupTitleSmallStorageAsync, sceNp.cpp:3843-3894. Note the maxSize
 * check happens before the init check, and contentLength is an output. */
inline int64_t lookup_title_small_storage_async(State& s, uint32_t max_size, bool have_data,
                                                bool have_len, bool have_option,
                                                int32_t psn_status) {
    if (max_size > 0x10000) return E_COMM_INVALID_ARGUMENT;
    if (!s.lookup_init) return E_COMM_NOT_INITIALIZED;
    if (!have_data || !have_len) return E_COMM_INSUFFICIENT_ARGUMENT;
    if (have_option) return E_COMM_INVALID_ARGUMENT;
    return lookup_gate(psn_status);
}

/* ---- sceNp manager, sceNp.cpp:3896-4442 ------------------------------- */
inline Result manager_get_status(State& s, bool have_out, int32_t psn_status) {
    /* sceNp.cpp:3940-3952. */
    if (!s.np_init) return ok(E_NOT_INITIALIZED);
    if (!have_out) return ok(E_INVALID_ARGUMENT);
    return write(psn_status);
}

/* sceNpManagerGetNpId, sceNp.cpp:4017-4046. OFFLINE is returned as
 * not_an_error: the value is the code, the sign bit is not an error report. */
inline int64_t manager_get_np_id(State& s, bool have_out, int32_t psn_status) {
    if (!s.np_init) return E_NOT_INITIALIZED;
    if (!have_out) return E_INVALID_ARGUMENT;
    if (psn_status == STATUS_OFFLINE) return E_OFFLINE;
    if (psn_status != STATUS_LOGGING_IN && psn_status != STATUS_ONLINE) return E_INVALID_STATE;
    return 0;
}

/* sceNpManagerGetContentRatingFlag, sceNp.cpp:4216-4247: TWO outputs, and the
 * second is the AGE. RPCS3 guards both behind get_psn_status(), but a parental
 * control is a property of the LOCAL user, not of a network session: the values
 * it writes (restricted 0, age 18, sceNp.cpp:4243-4244) are available with no
 * session at all, and that is what the console answers.
 *
 * Measured, not assumed: returning the negative OFFLINE code here put the title
 * on its "chat restrictions" error path, whose dialog has no offline exit
 * (docs/loading-43/REPORT.md). The code is CELL_OK and the values are an
 * unrestricted local profile. */
inline Result manager_get_content_rating(State& s, bool have_restricted, bool have_age) {
    if (!s.np_init) return ok(E_NOT_INITIALIZED);
    if (!have_restricted || !have_age) return ok(E_INVALID_ARGUMENT);
    return write(0);
}

/* sceNpManagerGetChatRestrictionFlag, sceNp.cpp:4249-4279. 0 is "no
 * restriction" (sceNp.cpp:4276), and the same measurement applies: this title
 * reads a negative return as "this account cannot play online" and refuses
 * every world, including a save load it would otherwise complete offline. */
inline Result manager_get_chat_restriction(State& s, bool have_out) {
    if (!s.np_init) return ok(E_NOT_INITIALIZED);
    if (!have_out) return ok(E_INVALID_ARGUMENT);
    return write(0);
}

/* sceNpManagerGetNetworkTime, sceNp.cpp:3955-3984. Every early return here is
 * not_an_error, including NOT_INITIALIZED. */
inline Result manager_get_network_time(State& s, bool have_out, int32_t psn_status) {
    if (!s.np_init) return ok(E_NOT_INITIALIZED);
    if (!have_out) return ok(E_INVALID_ARGUMENT);
    if (psn_status == STATUS_OFFLINE) return ok(E_OFFLINE);
    if (psn_status != STATUS_ONLINE) return ok(E_INVALID_STATE);
    return write(0); /* CellRtcTick, microseconds: no session, no network clock */
}

/* sceNpManagerGetTicket, sceNp.cpp:4382-4411. With no PSN session the ticket
 * is empty, so the size-only query reports 0 and a real query copies nothing. */
inline Result manager_get_ticket(State& s, bool have_size) {
    if (!s.np_init) return ok(E_NOT_INITIALIZED);
    if (!have_size) return ok(E_INVALID_ARGUMENT);
    return write(static_cast<int32_t>(s.ticket_size));
}

/* sceNpManagerGetTicketParam, sceNp.cpp:4413-4442: an empty ticket is
 * INVALID_STATE, not INVALID_ARGUMENT, once the id is in range. */
inline int64_t manager_get_ticket_param(State& s, int32_t param_id, bool have_out) {
    if (!s.np_init) return E_NOT_INITIALIZED;
    if (!have_out || param_id < TICKET_PARAM_FIRST || param_id > TICKET_PARAM_LAST)
        return E_INVALID_ARGUMENT;
    if (s.ticket_size == 0) return E_INVALID_STATE;
    return 0;
}

/* sceNpManagerRequestTicket, sceNp.cpp:4318-4347. */
inline int64_t manager_request_ticket(State& s, bool have_npid, bool have_service,
                                      uint32_t cookie_size, int32_t psn_status) {
    if (!s.np_init) return E_NOT_INITIALIZED;
    if (!have_npid || !have_service || cookie_size > COOKIE_MAX_SIZE)
        return E_AUTH_INVALID_ARGUMENT;
    if (psn_status == STATUS_OFFLINE) return E_OFFLINE;
    if (psn_status != STATUS_LOGGING_IN && psn_status != STATUS_ONLINE) return E_INVALID_STATE;
    return 0;
}

inline int64_t manager_register_callback(State& s, bool have_cb) {
    /* sceNp.cpp:3896-3916. */
    if (!s.np_init) return E_NOT_INITIALIZED;
    if (!have_cb) return E_INVALID_ARGUMENT;
    return 0;
}

inline int64_t manager_unregister_callback(State& s) {
    /* sceNp.cpp:3918-3932. */
    if (!s.np_init) return E_NOT_INITIALIZED;
    s.manager_cb = 0;
    s.manager_cb_arg = 0;
    return 0;
}

/* ---- sceNpBasic, sceNp.cpp:926-1400, :1750-2510 ----------------------- */
inline int64_t basic_register_handler(State& s, bool have_ctx, bool have_handler) {
    /* sceNp.cpp:952-976. */
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (s.basic_handler_registered) return E_BASIC_EXCEEDS_MAX;
    if (!have_ctx || !have_handler) return E_BASIC_INVALID_ARGUMENT;
    s.basic_handler_registered = true;
    return 0;
}

inline int64_t basic_unregister_handler(State& s) {
    /* sceNp.cpp:978-997. */
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (!s.basic_handler_registered) return E_BASIC_NOT_REGISTERED;
    s.basic_handler_registered = false;
    s.basic_handler = 0;
    s.basic_arg = 0;
    return 0;
}

/* sceNpBasicGetEvent, sceNp.cpp:2488-2510 plus np_handler.cpp:1141-1180: with
 * an empty queue the answer is not_an_error(SCE_NP_BASIC_ERROR_NO_EVENT). */
inline Result basic_get_event(State& s, bool have_event, bool have_from, bool have_data,
                              bool have_size, bool queue_empty) {
    if (!s.np_init) return ok(E_BASIC_NOT_INITIALIZED);
    if (!s.basic_handler_registered) return ok(E_BASIC_NOT_REGISTERED);
    if (!have_event || !have_from || !have_data || !have_size)
        return ok(E_BASIC_INVALID_ARGUMENT);
    if (queue_empty) return ok(E_BASIC_NO_EVENT);
    return ok(0);
}

/* sceNpBasicSetPresence, sceNp.cpp:999-1027: local, no online gate. */
inline int64_t basic_set_presence(State& s, uint32_t size) {
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (!s.basic_handler_registered) return E_BASIC_NOT_REGISTERED;
    if (size > MAX_PRESENCE_SIZE) return E_BASIC_EXCEEDS_MAX;
    return 0;
}

/* _sceNpBasicSendMessage, sceNp.cpp:1150-1215. */
inline int64_t basic_send_message(State& s, bool have_to, uint32_t size, int32_t psn_status) {
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (!s.basic_handler_registered) return E_BASIC_NOT_REGISTERED;
    if (!have_to || size == 0) return E_BASIC_INVALID_ARGUMENT;
    if (size > MAX_MESSAGE_SIZE) return E_BASIC_EXCEEDS_MAX;
    if (psn_status != STATUS_ONLINE) return E_BASIC_NOT_CONNECTED;
    return 0;
}

/* sceNpBasicAddFriend, sceNp.cpp:1750-1767: it builds a message with
 * mainType ADD_FRIEND, size 0 and sends it through sceNpBasicSendMessageGui.
 * RPCS3's own composition then rejects it: sceNp.cpp:1306-1307 is
 * "if (!msg->size) return SCE_NP_BASIC_ERROR_INVALID_ARGUMENT" and AddFriend
 * never sets a size, so the reference itself answers INVALID_ARGUMENT. That is
 * reproduced, not invented: a title testing for a successful add-friend on
 * RPCS3 sees this code, and matching it keeps the title on its error path. */
inline int64_t basic_add_friend(State& s, bool have_contact) {
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (!s.basic_handler_registered) return E_BASIC_NOT_REGISTERED;
    if (!have_contact) return E_BASIC_INVALID_ARGUMENT;
    return E_BASIC_INVALID_ARGUMENT;
}

/* sceNpBasicAddPlayersHistoryAsync, sceNp.cpp:1956-2003. Local history, no
 * online gate: the entries are stored and a request id is handed back. */
inline Result basic_add_players_history(State& s, uint32_t count, bool have_npids,
                                        bool empty_first_entry, bool description_too_long) {
    if (!s.np_init) return ok(E_BASIC_NOT_INITIALIZED);
    if (count == 0) return ok(E_BASIC_INVALID_ARGUMENT);
    if (count > PLAYER_HISTORY_MAX) return ok(E_BASIC_EXCEEDS_MAX);
    if (!have_npids) return ok(E_BASIC_INVALID_ARGUMENT);
    if (empty_first_entry) return ok(E_BASIC_INVALID_ARGUMENT);
    if (description_too_long) return ok(E_BASIC_EXCEEDS_MAX);
    return write(s.next_req_id++);
}

/* sceNpBasicGetFriendPresenceByNpId, sceNp.cpp:1880-1903: the argument checks
 * come before NP init (this one is the reverse of its neighbours), then the
 * presence lookup needs a PSN session: NOT_CONNECTED without one
 * (np_handler.cpp:1836-1838). */
inline int64_t basic_get_friend_presence(const State& s, bool have_pres, bool have_npid) {
    if (!have_pres) return E_BASIC_INVALID_ARGUMENT;
    if (!s.np_init) return E_BASIC_NOT_INITIALIZED;
    if (!have_npid) return E_BASIC_INVALID_ARGUMENT;
    return E_BASIC_NOT_CONNECTED;
}

/* ---- sceNpSignaling, sceNp.cpp:6812-7174 ------------------------------ */
inline Result signaling_create_ctx(State& s, bool have_npid, bool have_out) {
    /* sceNp.cpp:6812-6841. The context is a local object; RPCS3 allocates it
     * without consulting the sign-in state, so this succeeds offline too. */
    if (!s.np_init) return ok(E_SIG_NOT_INITIALIZED);
    if (!have_npid || !have_out) return ok(E_SIG_INVALID_ARGUMENT);
    SigCtx c;
    c.id = static_cast<uint32_t>(s.next_id++);
    s.sigs.push_back(c);
    return write(static_cast<int32_t>(c.id));
}

inline int64_t signaling_destroy_ctx(State& s, uint32_t id) {
    /* sceNp.cpp:6843-6863. */
    if (!s.np_init) return E_SIG_NOT_INITIALIZED;
    for (size_t i = 0; i < s.sigs.size(); i++) {
        if (s.sigs[i].id != id) continue;
        s.sigs.erase(s.sigs.begin() + static_cast<long>(i));
        return 0;
    }
    return E_SIG_CTX_NOT_FOUND;
}

inline bool signaling_has_ctx(const State& s, uint32_t id) {
    for (const auto& c : s.sigs) if (c.id == id) return true;
    return false;
}

inline int64_t signaling_add_extended_handler(State& s, uint32_t id, uint32_t handler,
                                              uint32_t arg) {
    /* sceNp.cpp:6865-6888. */
    if (!s.np_init) return E_SIG_NOT_INITIALIZED;
    for (auto& c : s.sigs) {
        if (c.id != id) continue;
        c.ext_handler = handler;
        c.ext_arg = arg;
        return 0;
    }
    return E_SIG_CTX_NOT_FOUND;
}

/* sceNpSignalingActivateConnection, sceNp.cpp:6946-6974: a peer that is our
 * own NP id is refused, and one connection id per peer is reused
 * (signaling_handler.cpp:795-820, get_always_conn_id). */
inline Result signaling_activate(State& s, uint32_t ctx_id, const uint8_t* npid,
                                  const uint8_t* self, bool have_npid, bool have_out) {
    if (!s.np_init) return ok(E_SIG_NOT_INITIALIZED);
    if (!signaling_has_ctx(s, ctx_id)) return ok(E_SIG_CTX_NOT_FOUND);
    if (!have_npid || !have_out) return ok(E_SIG_INVALID_ARGUMENT);
    if (is_own_npid(npid, self)) return ok(E_SIG_OWN_NP_ID);
    for (auto& c : s.conns) {
        if (!same_npid(c.npid, npid)) continue;
        return write(static_cast<int32_t>(c.id));
    }
    Conn c;
    c.id = static_cast<uint32_t>(s.next_id++);
    std::memcpy(c.npid, npid, NPID_SIZE);
    c.status = CONN_PENDING; /* init_sig1 -> PENDING until the peer answers */
    s.conns.push_back(c);
    return write(static_cast<int32_t>(c.id));
}

/* sceNpSignalingTerminateConnection, sceNp.cpp:6994-7010: no id check, and
 * stop_sig tolerates a connection it does not know (signaling_handler.cpp:729). */
inline int64_t signaling_terminate(State& s, uint32_t conn_id) {
    if (!s.np_init) return E_SIG_NOT_INITIALIZED;
    for (size_t i = 0; i < s.conns.size(); i++) {
        if (s.conns[i].id != conn_id) continue;
        s.conns.erase(s.conns.begin() + static_cast<long>(i));
        return 0;
    }
    return 0;
}

/* sceNpSignalingGetConnectionStatus, sceNp.cpp:7012-7046: an unknown
 * connection reports INACTIVE and then CONN_NOT_FOUND. */
inline Result signaling_get_conn_status(State& s, uint32_t conn_id, bool have_status) {
    if (!s.np_init) return ok(E_SIG_NOT_INITIALIZED);
    if (!have_status) return ok(E_SIG_INVALID_ARGUMENT);
    for (const auto& c : s.conns) {
        if (c.id != conn_id) continue;
        return write(c.status);
    }
    return write_with_error(E_SIG_CONN_NOT_FOUND, CONN_INACTIVE);
}

/* sceNpSignalingGetConnectionInfo, sceNp.cpp:7048-7111. */
inline int64_t signaling_get_conn_info(State& s, uint32_t conn_id, int32_t code, bool have_info) {
    if (!s.np_init) return E_SIG_NOT_INITIALIZED;
    if (!have_info) return E_SIG_INVALID_ARGUMENT;
    bool found = false;
    for (const auto& c : s.conns) found = found || (c.id == conn_id);
    if (!found) return E_SIG_CONN_NOT_FOUND;
    if (code < 1 || code > 6) return E_SIG_INVALID_ARGUMENT; /* sceNp.h:995-1000 */
    return 0;
}

/* sceNpSignalingGetConnectionFromNpId, sceNp.cpp:7113-7145. */
inline Result signaling_conn_from_npid(const State& s, const uint8_t* npid, const uint8_t* self,
                                       bool have_npid, bool have_out) {
    if (!s.np_init) return ok(E_SIG_NOT_INITIALIZED);
    if (!have_npid || !have_out) return ok(E_SIG_INVALID_ARGUMENT);
    if (is_own_npid(npid, self)) return ok(E_SIG_OWN_NP_ID);
    for (const auto& c : s.conns) {
        if (!same_npid(c.npid, npid)) continue;
        return write(static_cast<int32_t>(c.id));
    }
    return ok(E_SIG_CONN_NOT_FOUND);
}

/* sceNpSignalingGetConnectionFromPeerAddress, sceNp.cpp:7147-7174. */
inline int64_t signaling_conn_from_addr(const State& s, bool have_out) {
    if (!s.np_init) return E_SIG_NOT_INITIALIZED;
    if (!have_out) return E_SIG_INVALID_ARGUMENT;
    return E_SIG_CONN_NOT_FOUND;
}

/* ---- sceNpScore init, sceNp.cpp:5456-5475 -----------------------------
 * Local, no network: the score utility is only a flag, but the flag has the
 * console's order of checks (already-init before NP-init) and a second init is
 * COMMUNITY ALREADY_INITIALIZED. */
inline int64_t score_init(State& s) {
    if (s.score_init) return E_COMM_ALREADY_INITIALIZED;
    if (!s.np_init) return E_NOT_INITIALIZED;
    s.score_init = true;
    return 0;
}

inline int64_t score_term(State& s) {
    /* sceNp.cpp:5477-5495. */
    if (!s.score_init) return E_COMM_NOT_INITIALIZED;
    if (!s.np_init) return E_NOT_INITIALIZED;
    s.score_init = false;
    return 0;
}

/* ---- sceNpUtil, sceNp.cpp:7301-7314 ----------------------------------- */
inline int64_t util_cmp_np_id(bool have_a, bool have_b, bool equal) {
    if (!have_a || !have_b) return E_UTIL_INVALID_ARGUMENT;
    return equal ? 0 : E_UTIL_NOT_MATCH;
}

/* ---- sceNpDrm, sceNp.cpp:634-753 --------------------------------------
 * The host side answers three questions: is the path present, what is its
 * first word, and can a licence be verified. A file that is neither SCE\0 nor
 * NPD\0 is what RPCS3 calls "assume its just unencrypted" (sceNp.cpp:729-733).
 * The two encrypted magics need a licence this runtime has no way to verify,
 * so they answer NO_ENTITLEMENT rather than pretending the file opened. */
enum class DrmVerdict { Missing, Unencrypted, EncryptedUnverifiable };

inline int64_t drm_is_available(bool have_path, DrmVerdict verdict) {
    if (!have_path) return E_DRM_INVALID_PARAM;
    switch (verdict) {
    case DrmVerdict::Missing: return CELL_ENOENT; /* lv2_file::open's fs error */
    case DrmVerdict::Unencrypted: return 0;
    case DrmVerdict::EncryptedUnverifiable: return E_DRM_NO_ENTITLEMENT;
    }
    return E_DRM_NO_ENTITLEMENT;
}

/* The first word decides which file type this is, sceNp.cpp:675-690:
 * "SCE\0" (0x53434500) is a self, "NPD\0" (0x4e504400) an edata/sdata. */
inline DrmVerdict drm_verdict_for(uint32_t first_word) {
    if (first_word == 0x53434500u || first_word == 0x4e504400u)
        return DrmVerdict::EncryptedUnverifiable;
    return DrmVerdict::Unencrypted;
}

}  // namespace np
}  // namespace hle

#endif
