#include "common.h"
#include "runtime/msgdialog.h"
#include "runtime/sysutil_events.h"

#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>

namespace hle {
namespace msgdialog {

namespace {

/* --- the constants, from the header RPCS3 ships, not from memory ----------
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/cellMsgDialog.h:13-70 */
constexpr uint32_t TYPE_SE_NORMAL = 1u << 0;
constexpr uint32_t TYPE_BG_INVISIBLE = 1u << 2;
constexpr uint32_t BUTTON_TYPE_NONE = 0u << 4;
constexpr uint32_t BUTTON_TYPE_YESNO = 1u << 4;
constexpr uint32_t BUTTON_TYPE_OK = 2u << 4;
constexpr uint32_t DISABLE_CANCEL_ON = 1u << 7;

constexpr int32_t BUTTON_NONE = -1;
constexpr int32_t BUTTON_OK = 1;      /* == BUTTON_YES */
constexpr int32_t BUTTON_NO = 2;
constexpr int32_t BUTTON_ESCAPE = 3;

constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002b301);
constexpr int32_t ERR_BUSY = static_cast<int32_t>(0x8002b105);   /* CELL_SYSUTIL_ERROR_BUSY */
constexpr int32_t ERR_NOT_OPENED = static_cast<int32_t>(0x8002b302);

/* Pad bits as cellPad hands them over: b[3] is the face/shoulder byte.
 * Cross = 0x40, Circle = 0x20 (runtime/src/hle/cellPad.cpp button_of). */
constexpr uint16_t PAD_CIRCLE = 0x20;
constexpr uint16_t PAD_CROSS = 0x40;

enum class State { Closed, Open, Closing };

struct Dialog {
    State state = State::Closed;
    uint32_t type = 0;
    uint32_t callback = 0;
    uint32_t userdata = 0;
    std::string message;
    /* Deadline in timebase ticks for a pending close; 0 means "immediately". */
    uint64_t close_at = 0;
    int32_t pending_button = BUTTON_NONE;
    bool has_pending_button = false;
};

std::mutex g_lock;
Dialog g_dlg;
uint16_t g_prev_buttons = 0;
bool g_prev_valid = false;
std::atomic<uint32_t> g_opens{0};
std::atomic<uint32_t> g_resolves{0};
std::atomic<int32_t> g_last_button{BUTTON_NONE};

/* The button a press means, or BUTTON_NONE when the press does nothing.
 * Mirrors refs/.../overlay_message_dialog.cpp on_button_pressed: for an
 * OK-only dialog Circle is swallowed, and for a cancel-only (NONE) dialog
 * Cross is swallowed. Returning NONE is what makes "the dialog ignored every
 * press" a deliberate, typed outcome instead of a guess. */
int32_t button_for(const Dialog& d, uint16_t pressed_mask) {
    const uint32_t btn = d.type & 0x70u;
    const bool cross = (pressed_mask & PAD_CROSS) != 0;
    const bool circle = (pressed_mask & PAD_CIRCLE) != 0;
    switch (btn) {
    case BUTTON_TYPE_OK:
        if (cross) return BUTTON_OK;
        return BUTTON_NONE;          /* Circle ignored, as on hardware */
    case BUTTON_TYPE_YESNO:
        if (cross) return BUTTON_OK; /* YES == 1 */
        if (circle) return BUTTON_NO;
        return BUTTON_NONE;
    case BUTTON_TYPE_NONE:
    default:
        /* Cancel-only, and only interactive when cancel is not disabled. */
        if (d.type & DISABLE_CANCEL_ON) return BUTTON_NONE;
        if (circle) return BUTTON_ESCAPE;
        return BUTTON_NONE;          /* Cross ignored */
    }
}

/* Queue the guest callback. sysutil_post is drained by
 * cellSysutilCheckCallback on the guest thread, so this lands on the thread
 * that owns the callback queue -- the same discipline as RPCS3's
 * sysutil_register_cb (refs/.../cellMsgDialog.cpp:196-201). */
void deliver(uint32_t callback, uint32_t userdata, int32_t button) {
    g_resolves.fetch_add(1);
    g_last_button.store(button);
    if (!callback) return;
    LOG_INFO("msgdialog", "callback 0x%08x -> button %d (userdata 0x%08x)", callback, button, userdata);
    sysutil_post([callback, userdata, button](ppu::Context& c) {
        call_guest(c, callback, static_cast<uint64_t>(static_cast<int64_t>(button)), userdata);
    });
}

void resolve_locked(int32_t button) {
    const uint32_t cb = g_dlg.callback;
    const uint32_t ud = g_dlg.userdata;
    g_dlg = Dialog{};
    deliver(cb, ud, button);
}

}  // namespace

int32_t open(uint32_t type, const char* message, uint32_t callback, uint32_t userdata) {
    std::lock_guard lk(g_lock);
    if (g_dlg.state != State::Closed) {
        /* One dialog at a time, like firmware. RPCS3 returns BUSY here and the
         * title's own retry loop treats it as "try again", so this is the
         * return value that matters for a title that opens dialogs in a race
         * with a previous one closing. */
        LOG_WARN("msgdialog", "open2 type 0x%x while a dialog is already open -> BUSY", type);
        return ERR_BUSY;
    }
    /* Param validation, refs/.../cellMsgDialog.cpp:376-425. */
    if (!message || std::strlen(message) >= 512) return ERR_PARAM;
    const uint32_t btn = type & 0x70u;
    if (btn != BUTTON_TYPE_NONE && btn != BUTTON_TYPE_YESNO && btn != BUTTON_TYPE_OK) {
        return ERR_PARAM;
    }
    g_dlg = Dialog{};
    g_dlg.state = State::Open;
    g_dlg.type = type;
    g_dlg.callback = callback;
    g_dlg.userdata = userdata;
    g_dlg.message = message;
    g_opens.fetch_add(1);
    LOG_INFO("msgdialog", "open2 type 0x%x button_type %u msg '%s' callback 0x%08x userdata 0x%08x -> OPEN",
             type, btn >> 4, message, callback, userdata);
    return 0;
}

int32_t close_dialog(uint32_t delay_ms) {
    std::lock_guard lk(g_lock);
    if (g_dlg.state == State::Closed) return ERR_NOT_OPENED;
    /* RPCS3 keeps the dialog up for the requested delay and closes it from the
     * dialog thread (refs/.../cellMsgDialog.cpp:520-547). Record the deadline
     * and let the pad tick carry it out; the callback still fires.
     *
     * The button for a close() is the one the dialog type implies for the
     * confirm button -- firmware's close is "as if the confirm button was
     * pressed", and CELL_MSGDIALOG_BUTTON_OK is 1 in every variant here. */
    if (delay_ms == 0 || g_dlg.state == State::Closing) {
        const int32_t button = g_dlg.state == State::Closing ? g_dlg.pending_button : BUTTON_OK;
        resolve_locked(button);
        LOG_INFO("msgdialog", "close(%u ms) -> closed, button %d", delay_ms, button);
        return 0;
    }
    g_dlg.state = State::Closing;
    g_dlg.close_at = ppu::timebase() + static_cast<uint64_t>(delay_ms) * 80000ull;
    g_dlg.pending_button = BUTTON_OK;
    LOG_INFO("msgdialog", "close(%u ms) -> closing at tbase 0x%llx", delay_ms,
             static_cast<unsigned long long>(g_dlg.close_at));
    return 0;
}

int32_t abort() {
    uint32_t cb = 0, ud = 0;
    {
        std::lock_guard lk(g_lock);
        if (g_dlg.state == State::Closed) {
            /* RPCS3 returns OK here, "tested on HW" (cellMsgDialog.cpp:563). */
            return 0;
        }
        cb = g_dlg.callback;
        ud = g_dlg.userdata;
        g_dlg = Dialog{};
    }
    /* "this shouldn't call on_close". Count the drop so a log can tell an
     * aborted dialog from a resolved one. */
    g_opens.fetch_add(0);
    LOG_INFO("msgdialog", "abort() -> dialog dropped WITHOUT callback (cb 0x%08x userdata 0x%08x)", cb, ud);
    return 0;
}

bool is_open() {
    std::lock_guard lk(g_lock);
    return g_dlg.state != State::Closed;
}

uint32_t dialog_type() {
    std::lock_guard lk(g_lock);
    return g_dlg.type;
}

namespace {
/* Values the open dialog's progress bars hold. RPCS3's overlay keeps the same
 * numbers and draws with them; nothing here draws a message dialog, so the
 * stored value is what the calls can honestly produce. */
float g_bar_value[2] = {0.0f, 0.0f};
}

void progress_bar_inc(uint32_t index, uint32_t delta) {
    if (index >= 2) return;
    std::lock_guard lk(g_lock);
    g_bar_value[index] += static_cast<float>(delta);
}

void progress_bar_reset(uint32_t index) {
    if (index >= 2) return;
    std::lock_guard lk(g_lock);
    g_bar_value[index] = 0.0f;
}

float progress_bar_value(uint32_t index) { return index < 2 ? g_bar_value[index] : 0.0f; }

uint32_t opens_seen() { return g_opens.load(); }
uint32_t resolves_seen() { return g_resolves.load(); }
uint32_t last_button_value() { return static_cast<uint32_t>(g_last_button.load()); }

const char* last_button_name() {
    switch (g_last_button.load()) {
    case BUTTON_OK: return "OK/YES";
    case BUTTON_NO: return "NO";
    case BUTTON_ESCAPE: return "ESCAPE";
    default: return "NONE";
    }
}

void on_pad(const uint16_t* b) {
    int32_t fire = BUTTON_NONE;
    bool have_fire = false;
    uint32_t cb = 0, ud = 0;
    {
        std::lock_guard lk(g_lock);
        if (g_dlg.state == State::Closed) {
            g_prev_buttons = b[3];
            g_prev_valid = true;
            return;
        }
        const uint16_t now = b[3];
        const uint16_t pressed = g_prev_valid ? static_cast<uint16_t>(now & ~g_prev_buttons) : 0;
        g_prev_buttons = now;
        g_prev_valid = true;

        /* Time-based close first: a close() with a delay must not be starved by
         * a title that stops calling cellPadGetData for a moment. */
        if (g_dlg.state == State::Closing) {
            if (ppu::timebase() >= g_dlg.close_at) {
                fire = g_dlg.pending_button;
                have_fire = true;
                cb = g_dlg.callback;
                ud = g_dlg.userdata;
                g_dlg = Dialog{};
            }
        } else if (pressed) {
            const int32_t button = button_for(g_dlg, pressed);
            if (button != BUTTON_NONE) {
                fire = button;
                have_fire = true;
                cb = g_dlg.callback;
                ud = g_dlg.userdata;
                LOG_INFO("msgdialog", "pad press 0x%04x on type 0x%x -> button %d, closing", pressed,
                         g_dlg.type, button);
                g_dlg = Dialog{};
            } else {
                /* The press is deliberately swallowed. Logged because this is
                 * exactly the "dialog ignored every press" symptom, and now it
                 * is typed and attributable instead of silent. */
                LOG_INFO("msgdialog", "pad press 0x%04x on type 0x%x -> ignored (button type %u)",
                         pressed, g_dlg.type, (g_dlg.type & 0x70u) >> 4);
            }
        }
    }
    if (have_fire) deliver(cb, ud, fire);
}

}  // namespace msgdialog
}  // namespace hle
