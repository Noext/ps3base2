/* ps3base -- sceNp's decision logic, driven through the SAME header the imports
 * use (runtime/src/hle/np_hle.h).
 *
 * No game data, no GPU, no network: the subject is rules, not assets.
 *
 * Each block below is one RPCS3 rule, with the file and line it comes from.
 * The emphasis is on the codes that are easy to get backwards:
 *   - OFFLINE is not_an_error: the negative value is the ANSWER, not a fault;
 *   - NO_EVENT (0x8002a66a) is not EXCEEDS_MAX (0x8002a66b), which the old
 *     runtime conflated;
 *   - a lookup transaction needs ONLINE before its id is even looked at;
 *   - AddFriend fails inside RPCS3's own composition (size 0), so its answer is
 *     INVALID_ARGUMENT, not "queued";
 *   - a DRM path that is not SCE/NPD is unencrypted and succeeds.
 */
#include "hle/np_hle.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace np = hle::np;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

static np::State fresh() { return np::State{}; }

/* SceNpId as RPCS3 lays it out (sceNp.h:1266-1298, 0x24 bytes). Only the first
 * 17 bytes matter to every comparison in this module. */
static void fill_npid(uint8_t* id, const char* name) {
    std::memset(id, 0, np::NPID_SIZE);
    std::strncpy(reinterpret_cast<char*>(id), name, 16);
}

int main() {
    /* --- 0. the return codes themselves --------------------------------
     * sceNp.h:57-79, :82-110, :158-188, :500-523, :117-132, :328-347. */
    CHECK(np::E_NOT_INITIALIZED == static_cast<int32_t>(0x8002aa01), "NOT_INITIALIZED is 0x8002aa01");
    CHECK(np::E_ALREADY_INITIALIZED == static_cast<int32_t>(0x8002aa02), "ALREADY_INITIALIZED is 0x8002aa02");
    CHECK(np::E_INVALID_ARGUMENT == static_cast<int32_t>(0x8002aa03), "INVALID_ARGUMENT is 0x8002aa03");
    CHECK(np::E_INVALID_STATE == static_cast<int32_t>(0x8002aa0a), "INVALID_STATE is 0x8002aa0a");
    CHECK(np::E_OFFLINE == static_cast<int32_t>(0x8002aa0c), "OFFLINE is 0x8002aa0c");
    CHECK(np::E_BASIC_NO_EVENT == static_cast<int32_t>(0x8002a66a),
          "BASIC NO_EVENT is 0x8002a66a -- NOT 0x8002a66b, which is EXCEEDS_MAX");
    CHECK(np::E_BASIC_EXCEEDS_MAX == static_cast<int32_t>(0x8002a66b), "BASIC EXCEEDS_MAX is 0x8002a66b");
    CHECK(np::E_BASIC_NOT_REGISTERED == static_cast<int32_t>(0x8002a66d), "BASIC NOT_REGISTERED is 0x8002a66d");
    CHECK(np::E_BASIC_NOT_CONNECTED == static_cast<int32_t>(0x8002a678), "BASIC NOT_CONNECTED is 0x8002a678");
    CHECK(np::E_COMM_INVALID_ONLINE_ID == static_cast<int32_t>(0x8002a1a4), "COMMUNITY INVALID_ONLINE_ID is 0x8002a1a4");
    CHECK(np::E_SIG_CONN_NOT_FOUND == static_cast<int32_t>(0x8002a80e), "SIGNALING CONN_NOT_FOUND is 0x8002a80e");
    CHECK(np::E_UTIL_NOT_MATCH == static_cast<int32_t>(0x8002ab0f), "UTIL NOT_MATCH is 0x8002ab0f");
    CHECK(np::E_DRM_NO_ENTITLEMENT == static_cast<int32_t>(0x80029513), "DRM NO_ENTITLEMENT is 0x80029513");
    CHECK(np::STATUS_OFFLINE == -1 && np::STATUS_ONLINE == 3, "manager status OFFLINE -1 / ONLINE 3");
    CHECK(np::NPID_SIZE == 0x24, "SceNpId is 0x24 bytes (CHECK_SIZE_ALIGN, sceNp.h:1298)");
    CHECK(np::LOOKUP_MIN_TIMEOUT == 10000000, "lookup timeout floor is 10 s (sceNp.cpp:3376)");

    /* --- 1. sceNpLookup lifecycle, sceNp.cpp:3232-3395 ------------------ */
    {
        np::State s = fresh();
        CHECK(np::lookup_init(s) == np::E_NOT_INITIALIZED,
              "LookupInit before sceNpInit is NOT_INITIALIZED (sceNp.cpp:3243)");
        s.np_init = true;
        CHECK(np::lookup_init(s) == 0, "LookupInit succeeds once NP is up");
        CHECK(np::lookup_init(s) == np::E_COMM_ALREADY_INITIALIZED,
              "a second LookupInit is COMMUNITY ALREADY_INITIALIZED, checked first (sceNp.cpp:3238)");

        /* The id comes back as a VALUE, so a caller reading r3 gets it. */
        np::Result t = np::lookup_create_title_ctx(s, true, true);
        CHECK(t.code == 0 && t.has_value && t.value > 0,
              "CreateTitleCtx returns a positive id as not_an_error (sceNp.cpp:3292)");
        const int32_t title = t.value;
        CHECK(np::lookup_create_title_ctx(s, false, true).code == np::E_COMM_INSUFFICIENT_ARGUMENT,
              "no communicationId is INSUFFICIENT_ARGUMENT (sceNp.cpp:3287)");

        /* Transactions require ONLINE *before* the id lookup. */
        CHECK(np::lookup_create_transaction_ctx(s, title, np::STATUS_OFFLINE).code ==
                  np::E_COMM_INVALID_ONLINE_ID,
              "CreateTransactionCtx offline is INVALID_ONLINE_ID (sceNp.cpp:3326)");
        CHECK(np::lookup_create_transaction_ctx(s, 9999, np::STATUS_ONLINE).code == np::E_COMM_INVALID_ID,
              "an unknown title ctx is INVALID_ID (sceNp.cpp:3333)");
        np::Result tr = np::lookup_create_transaction_ctx(s, title, np::STATUS_ONLINE);
        CHECK(tr.code == 0 && tr.has_value, "a transaction ctx is created online");
        const int32_t trans = tr.value;

        /* SetTimeout: the floor is checked before the id. */
        CHECK(np::lookup_set_timeout(s, trans, 9999999) == np::E_COMM_INVALID_ARGUMENT,
              "a timeout under 10 s is INVALID_ARGUMENT (sceNp.cpp:3376)");
        CHECK(np::lookup_set_timeout(s, trans, 20000000) == 0, "a 20 s timeout is accepted");
        CHECK(np::lookup_set_timeout(s, title, 20000000) == 0, "a TITLE ctx id is accepted too (sceNp.cpp:3385)");
        CHECK(np::lookup_set_timeout(s, 4242, 20000000) == np::E_COMM_INVALID_ID,
              "an unknown id is INVALID_ID (sceNp.cpp:3391)");

        /* PollAsync: incomplete is not_an_error(1), not an error. */
        np::Result p = np::lookup_poll_async(s, trans);
        CHECK(p.code == 1 && !p.has_value,
              "PollAsync on a running transaction returns 1 as not_an_error (sceNp.cpp:3464)");
        for (auto& c : s.trans) if (c.id == trans) { c.complete = true; c.result = 0x2aaaaaaa; }
        p = np::lookup_poll_async(s, trans);
        CHECK(p.code == 0 && p.has_value && p.value == 0x2aaaaaaa,
              "a completed transaction writes its result (sceNp.cpp:3467)");
        CHECK(np::lookup_poll_async(s, 4242).code == np::E_COMM_INVALID_ID,
              "PollAsync on an unknown id is INVALID_ID");

        CHECK(np::lookup_destroy_transaction_ctx(s, trans) == 0, "DestroyTransactionCtx succeeds");
        CHECK(np::lookup_destroy_transaction_ctx(s, trans) == np::E_COMM_INVALID_ID,
              "destroying it twice is INVALID_ID (sceNp.cpp:3359)");
        CHECK(np::lookup_destroy_title_ctx(s, title) == 0, "DestroyTitleCtx succeeds");
        CHECK(np::lookup_term(s) == 0, "LookupTerm succeeds");
        CHECK(np::lookup_term(s) == np::E_COMM_NOT_INITIALIZED,
              "a second LookupTerm is COMMUNITY NOT_INITIALIZED (sceNp.cpp:3259)");
    }

    /* --- 2. lookup requests are gated on ONLINE ------------------------ */
    {
        np::State s = fresh();
        s.np_init = true;
        s.lookup_init = true;
        const bool offline = np::STATUS_OFFLINE;

        CHECK(np::lookup_npid_async(s, true, 0, offline) == np::E_COMM_INVALID_ID,
              "LookupNpIdAsync consults the id BEFORE the sign-in state (sceNp.cpp:3485)");
        CHECK(np::lookup_npid_async(s, false, 0, offline) == np::E_COMM_INSUFFICIENT_ARGUMENT,
              "a null onlineId is INSUFFICIENT_ARGUMENT (sceNp.cpp:3480)");

        /* maxSize is checked before init: sceNp.cpp:3845. */
        CHECK(np::lookup_title_small_storage_async(s, 0x10001, true, true, false, offline) ==
                  np::E_COMM_INVALID_ARGUMENT,
              "a maxSize over 0x10000 is INVALID_ARGUMENT first of all (sceNp.cpp:3845)");
        CHECK(np::lookup_title_small_storage_async(s, 0x100, false, true, false, offline) ==
                  np::E_COMM_INSUFFICIENT_ARGUMENT,
              "a null data buffer is INSUFFICIENT_ARGUMENT (sceNp.cpp:3852)");
        CHECK(np::lookup_title_small_storage_async(s, 0x100, true, true, true, offline) ==
                  np::E_COMM_INVALID_ARGUMENT,
              "a non-null option is INVALID_ARGUMENT (sceNp.cpp:3862)");
        CHECK(np::lookup_title_small_storage_async(s, 0x100, true, true, false, offline) ==
                  np::E_COMM_INVALID_ONLINE_ID,
              "offline, TitleSmallStorageAsync is INVALID_ONLINE_ID (sceNp.cpp:3867)");
        CHECK(np::lookup_title_small_storage_async(s, 0x100, true, true, false, np::STATUS_ONLINE) == 0,
              "online it passes the gate");
    }

    /* --- 3. the manager getters ---------------------------------------- */
    {
        np::State s = fresh();
        np::Result r = np::manager_get_status(s, true, np::STATUS_OFFLINE);
        CHECK(r.code == np::E_NOT_INITIALIZED && !r.has_value,
              "GetStatus before init is NOT_INITIALIZED (sceNp.cpp:3940)");
        s.np_init = true;
        r = np::manager_get_status(s, true, np::STATUS_OFFLINE);
        CHECK(r.code == 0 && r.has_value && r.value == -1,
              "GetStatus writes OFFLINE (-1) and returns CELL_OK (sceNp.cpp:3950)");
        CHECK(np::manager_get_status(s, false, np::STATUS_OFFLINE).code == np::E_INVALID_ARGUMENT,
              "GetStatus with a null out pointer is INVALID_ARGUMENT");

        CHECK(np::manager_get_np_id(s, false, np::STATUS_ONLINE) == np::E_INVALID_ARGUMENT,
              "GetNpId null out is INVALID_ARGUMENT (sceNp.cpp:4028)");
        CHECK(np::manager_get_np_id(s, true, np::STATUS_OFFLINE) == np::E_OFFLINE,
              "GetNpId offline returns 0x8002aa0c (sceNp.cpp:4033)");
        CHECK(np::manager_get_np_id(s, true, np::STATUS_ONLINE) == 0, "online it succeeds");
        CHECK(np::manager_get_np_id(s, true, np::STATUS_GETTING_TICKET) == np::E_INVALID_STATE,
              "a status that is neither LOGGING_IN nor ONLINE is INVALID_STATE (sceNp.cpp:4040)");

        /* Content rating: TWO outputs, the second is the AGE. */
        /* A parental control is a LOCAL property. The reference writes 0/18 and
         * the negative OFFLINE return put this title on its chat-restriction
         * dialog, whose only exit is "uncheck Online Game"
         * (docs/loading-43/REPORT.md). Both must be CELL_OK with no network. */
        r = np::manager_get_content_rating(s, true, true);
        CHECK(r.code == 0 && r.has_value && r.value == 0,
              "GetContentRatingFlag answers CELL_OK, restriction 0, with no session (sceNp.cpp:4243)");
        CHECK(np::manager_get_content_rating(s, false, true).code == np::E_INVALID_ARGUMENT,
              "either null output is INVALID_ARGUMENT (sceNp.cpp:4227)");
        np::State down;
        CHECK(np::manager_get_content_rating(down, true, true).code == np::E_NOT_INITIALIZED,
              "before NP init it is NOT_INITIALIZED (sceNp.cpp:4222)");

        r = np::manager_get_chat_restriction(s, true);
        CHECK(r.code == 0 && r.has_value && r.value == 0,
              "GetChatRestrictionFlag answers CELL_OK and 0, never OFFLINE and never 1 (sceNp.cpp:4276)");
        CHECK(np::manager_get_chat_restriction(s, false).code == np::E_INVALID_ARGUMENT,
              "a null output is INVALID_ARGUMENT (sceNp.cpp:4260)");

        /* Network time: NOT_INITIALIZED is a VALUE here, and OFFLINE is not an error either. */
        np::State fresh_np;
        r = np::manager_get_network_time(fresh_np, true, np::STATUS_OFFLINE);
        CHECK(r.code == np::E_NOT_INITIALIZED && !r.has_value,
              "GetNetworkTime before init returns the code as not_an_error (sceNp.cpp:3963)");
        r = np::manager_get_network_time(s, true, np::STATUS_OFFLINE);
        CHECK(r.code == np::E_OFFLINE, "GetNetworkTime offline returns OFFLINE (sceNp.cpp:3971)");
        r = np::manager_get_network_time(s, true, np::STATUS_ONLINE);
        CHECK(r.code == 0 && r.has_value && r.value == 0, "online it writes a tick");

        /* Ticket: empty without a session, so the size query says 0. */
        r = np::manager_get_ticket(s, true);
        CHECK(r.code == 0 && r.has_value && r.value == 0,
              "an empty ticket reports size 0 (sceNp.cpp:4400)");
        CHECK(np::manager_get_ticket(s, false).code == np::E_INVALID_ARGUMENT,
              "a null bufferSize is INVALID_ARGUMENT (sceNp.cpp:4393)");
        CHECK(np::manager_get_ticket_param(s, 0, true) == np::E_INVALID_STATE,
              "a ticket param with an empty ticket is INVALID_STATE (sceNp.cpp:4431)");
        CHECK(np::manager_get_ticket_param(s, 12, true) == np::E_INVALID_ARGUMENT,
              "paramId 12 is out of range (sceNp.h:1215-1226)");
        CHECK(np::manager_get_ticket_param(s, -1, true) == np::E_INVALID_ARGUMENT,
              "paramId -1 is out of range");

        CHECK(np::manager_request_ticket(s, true, true, 0, np::STATUS_OFFLINE) == np::E_OFFLINE,
              "RequestTicket offline is OFFLINE (sceNp.cpp:4334)");
        CHECK(np::manager_request_ticket(s, true, true, 1025, np::STATUS_ONLINE) ==
                  np::E_AUTH_INVALID_ARGUMENT,
              "a cookie over 1024 bytes is AUTH EINVALID_ARGUMENT (sceNp.cpp:4329)");

        CHECK(np::manager_register_callback(s, false) == np::E_INVALID_ARGUMENT,
              "RegisterCallback with a null callback is INVALID_ARGUMENT (sceNp.cpp:3907)");
        CHECK(np::manager_register_callback(s, true) == 0, "a real callback registers");
        s.manager_cb = 0x1234;
        CHECK(np::manager_unregister_callback(s) == 0 && s.manager_cb == 0,
              "UnregisterCallback clears the slot (sceNp.cpp:3929)");
        np::State fresh_unreg;
        CHECK(np::manager_unregister_callback(fresh_unreg) == np::E_NOT_INITIALIZED,
              "UnregisterCallback before init is NOT_INITIALIZED");
    }

    /* --- 4. sceNpBasic -------------------------------------------------- */
    {
        np::State s = fresh();
        CHECK(np::basic_register_handler(s, true, true) == np::E_BASIC_NOT_INITIALIZED,
              "a basic handler before NP init is BASIC NOT_INITIALIZED (sceNp.cpp:958)");
        s.np_init = true;
        CHECK(np::basic_register_handler(s, false, true) == np::E_BASIC_INVALID_ARGUMENT,
              "no context is BASIC INVALID_ARGUMENT (sceNp.cpp:968)");
        CHECK(np::basic_register_handler(s, true, true) == 0, "the first registration succeeds");
        CHECK(np::basic_register_handler(s, true, true) == np::E_BASIC_EXCEEDS_MAX,
              "the second is BASIC EXCEEDS_MAX: one slot only (sceNp.cpp:963)");

        /* GetEvent: empty queue is NO_EVENT, and the constant is 0x8002a66a. */
        np::Result r = np::basic_get_event(s, true, true, true, true, true);
        CHECK(r.code == np::E_BASIC_NO_EVENT,
              "an empty event queue is BASIC NO_EVENT 0x8002a66a (np_handler.cpp:1144)");
        CHECK(np::basic_get_event(s, true, true, true, false, true).code ==
                  np::E_BASIC_INVALID_ARGUMENT,
              "a null size is BASIC INVALID_ARGUMENT (sceNp.cpp:2504)");
        np::State unreg = fresh();
        unreg.np_init = true;
        CHECK(np::basic_get_event(unreg, true, true, true, true, true).code ==
                  np::E_BASIC_NOT_REGISTERED,
              "GetEvent without a registration is BASIC NOT_REGISTERED (sceNp.cpp:2499)");

        CHECK(np::basic_set_presence(s, 129) == np::E_BASIC_EXCEEDS_MAX,
              "a presence over 128 bytes is EXCEEDS_MAX (sceNp.h:1106)");
        CHECK(np::basic_set_presence(s, 128) == 0, "128 bytes is accepted");
        np::State fresh_pres;
        CHECK(np::basic_set_presence(fresh_pres, 1) == np::E_BASIC_NOT_INITIALIZED,
              "SetPresence before init is NOT_INITIALIZED");

        /* SendMessage: offline is NOT_CONNECTED, as a non-error value. */
        CHECK(np::basic_send_message(s, true, 0, np::STATUS_ONLINE) == np::E_BASIC_INVALID_ARGUMENT,
              "a zero size is INVALID_ARGUMENT (sceNp.cpp:1163)");
        CHECK(np::basic_send_message(s, true, 513, np::STATUS_ONLINE) == np::E_BASIC_EXCEEDS_MAX,
              "over 512 bytes is EXCEEDS_MAX (sceNp.h:1105)");
        CHECK(np::basic_send_message(s, true, 8, np::STATUS_OFFLINE) == np::E_BASIC_NOT_CONNECTED,
              "offline it is BASIC NOT_CONNECTED (sceNp.cpp:1180)");
        CHECK(np::basic_send_message(s, true, 8, np::STATUS_ONLINE) == 0, "online it is accepted");

        /* AddFriend: RPCS3's own composition rejects it at the size check. */
        CHECK(np::basic_add_friend(s, true) == np::E_BASIC_INVALID_ARGUMENT,
              "AddFriend answers BASIC INVALID_ARGUMENT (sceNp.cpp:1306 via :1750-1766)");
        np::State fresh_friend;
        CHECK(np::basic_add_friend(fresh_friend, true) == np::E_BASIC_NOT_INITIALIZED,
              "AddFriend before init is NOT_INITIALIZED (sceNp.cpp:1936)");

        /* PlayersHistory: local, no online gate, hands back a request id. */
        r = np::basic_add_players_history(s, 0, true, false, false);
        CHECK(r.code == np::E_BASIC_INVALID_ARGUMENT, "a count of 0 is INVALID_ARGUMENT (sceNp.cpp:1967)");
        r = np::basic_add_players_history(s, 9, true, false, false);
        CHECK(r.code == np::E_BASIC_EXCEEDS_MAX, "more than 8 players is EXCEEDS_MAX (sceNp.cpp:1972)");
        r = np::basic_add_players_history(s, 2, true, true, false);
        CHECK(r.code == np::E_BASIC_INVALID_ARGUMENT,
              "an entry with an empty online id is INVALID_ARGUMENT (sceNp.cpp:1984)");
        r = np::basic_add_players_history(s, 2, true, false, true);
        CHECK(r.code == np::E_BASIC_EXCEEDS_MAX, "a long description is EXCEEDS_MAX (sceNp.cpp:1990)");
        r = np::basic_add_players_history(s, 2, true, false, false);
        CHECK(r.code == 0 && r.has_value && r.value >= 1,
              "a valid call returns a request id without any network (sceNp.cpp:1995)");

        /* Friend presence: the argument checks precede the init check here. */
        np::State fresh_fp1;
        CHECK(np::basic_get_friend_presence(fresh_fp1, false, true) == np::E_BASIC_INVALID_ARGUMENT,
              "a null presence output is INVALID_ARGUMENT even before init (sceNp.cpp:1884)");
        np::State fresh_fp2;
        CHECK(np::basic_get_friend_presence(fresh_fp2, true, true) == np::E_BASIC_NOT_INITIALIZED,
              "then NOT_INITIALIZED (sceNp.cpp:1891)");
        CHECK(np::basic_get_friend_presence(s, true, true) == np::E_BASIC_NOT_CONNECTED,
              "and with no session, BASIC NOT_CONNECTED (np_handler.cpp:1837)");
    }

    /* --- 5. sceNpSignaling ---------------------------------------------- */
    {
        np::State s = fresh();
        CHECK(np::signaling_create_ctx(s, true, true).code == np::E_SIG_NOT_INITIALIZED,
              "SignalingCreateCtx before init is SIGNALING NOT_INITIALIZED (sceNp.cpp:6820)");
        s.np_init = true;
        np::Result r = np::signaling_create_ctx(s, false, true);
        CHECK(r.code == np::E_SIG_INVALID_ARGUMENT,
              "a null npId is SIGNALING INVALID_ARGUMENT (sceNp.cpp:6825)");
        r = np::signaling_create_ctx(s, true, true);
        CHECK(r.code == 0 && r.has_value && r.value > 0,
              "the context is LOCAL: it is created offline (sceNp.cpp:6828)");
        const uint32_t ctx = static_cast<uint32_t>(r.value);

        CHECK(np::signaling_add_extended_handler(s, 999, 1, 2) == np::E_SIG_CTX_NOT_FOUND,
              "an unknown ctx is SIGNALING CTX_NOT_FOUND (sceNp.cpp:6878)");
        CHECK(np::signaling_add_extended_handler(s, ctx, 0x1111, 0x2222) == 0,
              "a known ctx takes the handler");

        /* A peer that is ourselves is refused. */
        uint8_t self[np::NPID_SIZE], peer[np::NPID_SIZE];
        fill_npid(self, "rdrself");
        fill_npid(peer, "rdrpeer");
        r = np::signaling_activate(s, ctx, self, self, true, true);
        CHECK(r.code == np::E_SIG_OWN_NP_ID,
              "activating a connection to our own NP id is OWN_NP_ID (sceNp.cpp:6967)");
        r = np::signaling_activate(s, ctx, peer, self, true, true);
        CHECK(r.code == 0 && r.has_value, "a real peer gets a connection id");
        const uint32_t conn = static_cast<uint32_t>(r.value);
        r = np::signaling_activate(s, ctx, peer, self, true, true);
        CHECK(r.code == 0 && static_cast<uint32_t>(r.value) == conn,
              "the same peer reuses its connection id (signaling_handler.cpp:795)");

        /* An unknown connection reports INACTIVE and then CONN_NOT_FOUND. */
        r = np::signaling_get_conn_status(s, 4242, true);
        CHECK(r.code == np::E_SIG_CONN_NOT_FOUND && r.has_value && r.value == np::CONN_INACTIVE,
              "an unknown connection answers CONN_NOT_FOUND with INACTIVE written (sceNp.cpp:7034)");
        r = np::signaling_get_conn_status(s, conn, true);
        CHECK(r.code == 0 && r.has_value && r.value == np::CONN_PENDING,
              "a fresh connection is PENDING (signaling_handler.cpp:812)");

        CHECK(np::signaling_get_conn_info(s, conn, 0, true) == np::E_SIG_INVALID_ARGUMENT,
              "connection info code 0 is INVALID_ARGUMENT (sceNp.cpp:7106)");
        CHECK(np::signaling_get_conn_info(s, 4242, 1, true) == np::E_SIG_CONN_NOT_FOUND,
              "an unknown connection is CONN_NOT_FOUND (sceNp.cpp:7068)");

        r = np::signaling_conn_from_npid(s, peer, self, true, true);
        CHECK(r.code == 0 && static_cast<uint32_t>(r.value) == conn,
              "GetConnectionFromNpId finds the peer (sceNp.cpp:7142)");
        r = np::signaling_conn_from_npid(s, self, self, true, true);
        CHECK(r.code == np::E_SIG_OWN_NP_ID, "asking for ourselves is OWN_NP_ID (sceNp.cpp:7131)");
        CHECK(np::signaling_conn_from_addr(s, true) == np::E_SIG_CONN_NOT_FOUND,
              "an unknown peer address is CONN_NOT_FOUND (sceNp.cpp:7168)");

        CHECK(np::signaling_terminate(s, conn) == 0, "TerminateConnection succeeds");
        CHECK(np::signaling_terminate(s, conn) == 0,
              "terminating an unknown connection still succeeds (signaling_handler.cpp:729)");
        CHECK(np::signaling_destroy_ctx(s, ctx) == 0, "DestroyCtx succeeds");
        CHECK(np::signaling_destroy_ctx(s, ctx) == np::E_SIG_CTX_NOT_FOUND,
              "destroying it twice is CTX_NOT_FOUND (sceNp.cpp:6856)");
    }

    /* --- 6. sceNpUtil and the NP id comparison -------------------------- */
    {
        uint8_t a[np::NPID_SIZE], b[np::NPID_SIZE], c[np::NPID_SIZE];
        fill_npid(a, "rdrplayer");
        fill_npid(b, "rdrplayer");
        fill_npid(c, "otherplayer");
        CHECK(np::same_npid(a, b), "identical online ids compare equal (np_helpers.cpp:132)");
        CHECK(!np::same_npid(a, c), "different online ids do not");
        /* The difference may live past byte 16 and must be ignored: only 16
         * bytes are compared. */
        b[20] = 0x77;
        CHECK(np::same_npid(a, b),
              "a difference in opt[]/reserved[] is IGNORED -- only 16 bytes count (np_helpers.cpp:132)");

        CHECK(np::util_cmp_np_id(true, true, true) == 0, "CmpNpId on equal ids is CELL_OK");
        CHECK(np::util_cmp_np_id(true, true, false) == np::E_UTIL_NOT_MATCH,
              "CmpNpId on different ids is UTIL NOT_MATCH as not_an_error (sceNp.cpp:7311)");
        CHECK(np::util_cmp_np_id(false, true, true) == np::E_UTIL_INVALID_ARGUMENT,
              "a null id is UTIL INVALID_ARGUMENT (sceNp.cpp:7307)");
    }

    /* --- 7. sceNpDrm ---------------------------------------------------- */
    {
        CHECK(np::drm_is_available(false, np::DrmVerdict::Unencrypted) == np::E_DRM_INVALID_PARAM,
              "a null path is DRM INVALID_PARAM (sceNp.cpp:742)");
        CHECK(np::drm_is_available(true, np::DrmVerdict::Missing) == np::CELL_ENOENT,
              "a missing file is the lv2 open error CELL_ENOENT (sceNp.cpp:662)");
        CHECK(np::drm_is_available(true, np::DrmVerdict::Unencrypted) == 0,
              "an unencrypted file is accepted, as RPCS3 assumes (sceNp.cpp:729-735)");
        CHECK(np::drm_is_available(true, np::DrmVerdict::EncryptedUnverifiable) ==
                  np::E_DRM_NO_ENTITLEMENT,
              "an SCE/NPD file whose licence we cannot verify is NO_ENTITLEMENT (sceNp.cpp:726)");

        CHECK(np::drm_verdict_for(0x53434500u) == np::DrmVerdict::EncryptedUnverifiable,
              "\"SCE\\0\" is a self (sceNp.cpp:675)");
        CHECK(np::drm_verdict_for(0x4e504400u) == np::DrmVerdict::EncryptedUnverifiable,
              "\"NPD\\0\" is an edata/sdata (sceNp.cpp:683)");
        CHECK(np::drm_verdict_for(0x00000000u) == np::DrmVerdict::Unencrypted,
              "anything else is unencrypted");
        CHECK(np::drm_verdict_for(0x7f454c46u) == np::DrmVerdict::Unencrypted,
              "an ELF magic is not NPDRM");
    }

    /* --- 8. the same code is state-machine safe ------------------------ */
    {
        /* Two independent State objects must not interfere: the runtime passes
         * the shared one, the test passes its own, and no rule may read a
         * global. This is the regression for "a flag that survived a call". */
        np::State a = fresh(), b = fresh();
        np::lookup_init(a);
        a.np_init = true;
        CHECK(np::lookup_init(a) == 0, "a is up");
        CHECK(np::lookup_init(b) == np::E_NOT_INITIALIZED, "b is untouched by a");
        CHECK(!b.lookup_init, "no state leaked between instances");
    }

    std::printf("np: %d checks, %d failures\n", checks, fails);
    return fails;
}
