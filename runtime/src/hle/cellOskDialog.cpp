#include "common.h"
#include "runtime/sysutil_events.h"

#include <cstdlib>
#include <mutex>
#include <string>

/* cellOskDialog -- the on-screen keyboard.
 *
 * The title needs it for every text field: the world name and the seed. Without
 * it those fields stay empty no matter what is typed at the host keyboard,
 * because the host keyboard is only ever translated into PAD BUTTONS
 * (runtime/src/host_input.cpp) -- there is no path from an SDL key to a guest
 * text field. Measured consequence: every "seeded" run produced a world with a
 * different RandomSeed, because the title received zero characters.
 *
 * The neighbouring ports solve it the same way: LoadAsync reads the caller's
 * CellOskDialogInputFieldInfo, raises CELL_SYSUTIL_OSKDIALOG_LOADED, and when a
 * scripted answer exists it raises CELL_SYSUTIL_OSKDIALOG_FINISHED;
 * UnloadAsync then hands the text back through
 * CellOskDialogCallbackReturnParam. References:
 *   refs/jdc-ps3recomp/libs/misc/cellOskDialog.c:341
 *   refs/dod3-recomp/ps3recomp/libs/misc/cellOskDialog.c:122
 *
 * PS3BASE_OSK_TEXT is the answer to supply. The title cannot tell it apart from
 * a player who typed the text at the OSK.
 */

namespace hle {

namespace {

constexpr uint32_t EVENT_OSK_LOADED = 0x0502;
constexpr uint32_t EVENT_OSK_FINISHED = 0x0503;
constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002b501);
constexpr int32_t ERR_BUSY = static_cast<int32_t>(0x8002b502);
constexpr uint32_t MAX_TEXT = 512;

struct Osk {
    std::mutex m;
    bool open = false;
    bool aborted = false;
    std::string text;
};

Osk& state() {
    static Osk s;
    return s;
}

/* Guest UTF-16 big-endian -> host UTF-8. */
std::string from_guest(uint32_t ea, uint32_t limit) {
    std::string out;
    if (!ea || !limit) return out;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(vm::ptr(ea));
    if (!p) return out;
    for (uint32_t i = 0; i < limit; i++) {
        const uint16_t ch = static_cast<uint16_t>((p[i * 2] << 8) | p[i * 2 + 1]);
        if (!ch) break;
        if (ch < 0x80) {
            out.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return out;
}

/* Host UTF-8 -> guest UTF-16 big-endian, NUL terminated. */
uint32_t to_guest(uint32_t ea, uint32_t capacity, const std::string& text) {
    if (!ea || capacity < 2) return 0;
    uint8_t* p = reinterpret_cast<uint8_t*>(vm::ptr(ea));
    if (!p) return 0;
    uint32_t n = 0;
    for (size_t i = 0; i < text.size() && n + 1 < capacity;) {
        uint32_t cp = static_cast<unsigned char>(text[i]);
        size_t adv = 1;
        if (cp >= 0xF0 && i + 3 < text.size()) {
            cp = ((cp & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
            adv = 4;
        } else if (cp >= 0xE0 && i + 2 < text.size()) {
            cp = ((cp & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F);
            adv = 3;
        } else if (cp >= 0xC0 && i + 1 < text.size()) {
            cp = ((cp & 0x1F) << 6) | (text[i + 1] & 0x3F);
            adv = 2;
        }
        i += adv;
        if (cp > 0xFFFF) cp = 0xFFFD;
        p[n * 2] = static_cast<uint8_t>(cp >> 8);
        p[n * 2 + 1] = static_cast<uint8_t>(cp & 0xFF);
        n++;
    }
    p[n * 2] = 0;
    p[n * 2 + 1] = 0;
    return n;
}

/* Raise a sysutil event to the callbacks the title registered. */
void post_event(uint32_t code) {
    for (int slot = 0; slot < 4; slot++) {
        const uint32_t fn = callback_function(slot);
        if (!fn) continue;
        const uint32_t arg = callback_arg(slot);
        LOG_INFO("cellOsk", "event 0x%04x -> callback slot %d (0x%08x)", code, slot, fn);
        sysutil_post([fn, arg, code](ppu::Context& c) {
            call_guest(c, fn, static_cast<uint64_t>(code), static_cast<uint64_t>(arg));
        });
    }
}

}  // namespace

HLE_FUNC(cellSysutil, cellOskDialogLoadAsync) {
    const uint32_t param = a32(ctx, 1);
    const uint32_t info = a32(ctx, 2);
    if (!param || !info) return ret(ctx, ERR_PARAM);

    /* CellOskDialogInputFieldInfo { message*, init_text*, limit_length } */
    const uint32_t message_ea = vm::read32(info + 0);
    const uint32_t init_ea = vm::read32(info + 4);
    const uint32_t limit = vm::read32(info + 8);
    if (limit > MAX_TEXT) return ret(ctx, ERR_PARAM);

    Osk& s = state();
    {
        std::lock_guard lk(s.m);
        if (s.open) return ret(ctx, ERR_BUSY);
        s.open = true;
        s.text = from_guest(init_ea, limit ? limit : MAX_TEXT);
    }
    const std::string prompt = from_guest(message_ea, MAX_TEXT);
    LOG_INFO("cellOsk", "LoadAsync prompt '%s' initial '%s' limit %u", prompt.c_str(), s.text.c_str(), limit);

    post_event(EVENT_OSK_LOADED);

    if (const char* want = std::getenv("PS3BASE_OSK_TEXT")) {
        {
            std::lock_guard lk(s.m);
            s.text = want;
        }
        LOG_INFO("cellOsk", "scripted answer '%s' (PS3BASE_OSK_TEXT)", want);
        post_event(EVENT_OSK_FINISHED);
    } else {
        LOG_INFO("cellOsk",
                 "no PS3BASE_OSK_TEXT: the dialog stays open, so the title's text "
                 "fields will stay empty");
    }
    return ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellOskDialogUnloadAsync) {
    /* CellOskDialogCallbackReturnParam { result, numCharsResultString, pResultString } */
    const uint32_t result_ea = a32(ctx, 0);
    if (!result_ea) return ret(ctx, ERR_PARAM);
    Osk& s = state();
    std::lock_guard lk(s.m);
    const uint32_t capacity = vm::read32(result_ea + 4);
    const uint32_t buf = vm::read32(result_ea + 8);
    const uint32_t n = to_guest(buf, capacity, s.text);
    /* An aborted dialog reports CELL_OSKDIALOG_INPUT_FIELD_RESULT_ABORT (2,
     * refs/.../cellOskDialog.h:36); a finished one reports OK (0). The
     * previous body always wrote 0, so a title could not tell an abort from a
     * confirmation and took the aborted text as typed input. */
    vm::write32(result_ea + 0, s.aborted ? 2u : 0u);
    vm::write32(result_ea + 4, n);
    s.open = false;
    s.aborted = false;
    LOG_INFO("cellOsk", "UnloadAsync -> '%s' (%u chars)", s.text.c_str(), n);
    return ret(ctx, 0);
}

HLE_FUNC(cellSysutil, cellOskDialogSetLayoutMode) { return ret(ctx, 0); }
HLE_FUNC(cellSysutil, cellOskDialogSetKeyLayoutOption) { return ret(ctx, 0); }
HLE_FUNC(cellSysutil, cellOskDialogSetInitialKeyLayout) { return ret(ctx, 0); }
HLE_FUNC(cellSysutil, cellOskDialogAddSupportLanguage) { return ret(ctx, 0); }
HLE_FUNC(cellSysutil, cellOskDialogSetDefaultResponse) { return ret(ctx, 0); }
HLE_FUNC(cellSysutil, cellOskDialogDisableDimmer) { return ret(ctx, 0); }
HLE_FUNC(cellOskExtUtility, cellOskDialogExtSetInitialScale) { return ret(ctx, 0); }
HLE_FUNC(cellOskExtUtility, cellOskDialogExtRegisterKeyboardEventHookCallback) { return ret(ctx, 0); }
HLE_FUNC(cellOskExtUtility, cellOskDialogExtRegisterConfirmWordFilterCallback) { return ret(ctx, 0); }

/* Exposed to cellSysutil's cellOskDialogAbort, so "is a dialog open" has a
 * single owner instead of a second copy of the state in another file. */
namespace osk {

bool is_open() {
    Osk& s = state();
    std::lock_guard lk(s.m);
    return s.open;
}

/* refs/.../cellOskDialog.cpp:816-825: the abort path sets the ABORT input
 * result and raises CELL_SYSUTIL_OSKDIALOG_FINISHED, the same event a normal
 * finish raises; cellOskDialogUnloadAsync then reports result 2 (ABORT,
 * refs/.../cellOskDialog.h:36) so the title can tell them apart. */
void abort() {
    Osk& s = state();
    {
        std::lock_guard lk(s.m);
        s.open = false;
        s.text.clear();
        s.aborted = true;
    }
    LOG_INFO("cellOsk", "abort -> CELL_SYSUTIL_OSKDIALOG_FINISHED with ABORT result");
    post_event(EVENT_OSK_FINISHED);
}

}  // namespace osk

}  // namespace hle
