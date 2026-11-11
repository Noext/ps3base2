#pragma once
/* The system message dialog (cellMsgDialog*) as a real state machine.
 *
 * Why this exists. The previous handler answered the title's callback
 * immediately, on the spot, with a button it guessed from the dialog type
 * (((type >> 4) & 7) == 1 ? 1 : 3). Nothing was ever "open", no button was
 * ever read, and the value handed back did not match the type: a
 * CELL_MSGDIALOG_TYPE_BUTTON_TYPE_OK dialog (type 0x20) got ESCAPE (3), not OK
 * (1). A firmware dialog does the opposite -- it intercepts the pad, decides
 * which button the user pressed, and only then calls the title's callback.
 *
 * The button values are RPCS3's, not invented
 * (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellMsgDialog.h:54-61):
 *   CELL_MSGDIALOG_BUTTON_NONE = -1, INVALID = 0,
 *   OK = 1, YES = 1, NO = 2, ESCAPE = 3.
 * The per-type mapping is RPCS3's overlay, which is what the firmware does
 * (refs/rpcs3/rpcs3/Emu/RSX/Overlays/overlay_message_dialog.cpp:155-195):
 *   BUTTON_TYPE_OK    : Cross  -> OK(1)      ; Circle is IGNORED
 *   BUTTON_TYPE_YESNO : Cross  -> YES(1)     ; Circle -> NO(2)
 *   BUTTON_TYPE_NONE  : Circle -> ESCAPE(3)  ; Cross is IGNORED (cancel only)
 *
 * Delivery is through hle::sysutil_post, the same queue cellSysutilCheckCallback
 * drains on the guest thread -- so the callback runs on the thread that owns
 * the callback queue, never on our render or pad thread.
 */
#include <cstdint>

namespace ppu {
struct Context;
}

namespace hle {
namespace msgdialog {

/* cellMsgDialogOpen2. Returns 0, CELL_SYSUTIL_ERROR_BUSY (0x8002b105) when a
 * dialog is already open, or CELL_MSGDIALOG_ERROR_PARAM (0x8002b301). */
int32_t open(uint32_t type, const char* message, uint32_t callback, uint32_t userdata);

/* cellMsgDialogClose(delay_ms): keep the dialog up for delay_ms more, then
 * deliver the callback with the button that closed it. RPCS3 records the
 * requested wait and lets the dialog thread honour it
 * (refs/.../cellMsgDialog.cpp:520-547); here the deadline is absolute. */
int32_t close_dialog(uint32_t delay_ms);

/* cellMsgDialogAbort(): drop the dialog WITHOUT calling the callback.
 * refs/.../cellMsgDialog.cpp:558-585 -- "this shouldn't call on_close". */
int32_t abort();

/* Sample the pad bytes the title is about to receive and edge-detect the
 * confirm/cancel buttons. Called from cellPad's fill(), i.e. the HLE observes
 * the same pad state it hands to the title -- the firmware equivalent of
 * intercepting the pads while its dialog owns the screen. */
void on_pad(const uint16_t* b);

/* Advance time-based transitions (the close delay). Called from on_pad, which
 * already runs once per guest frame. */
bool is_open();
const char* last_button_name();
uint32_t last_button_value();
/* The type of the currently open dialog (0 when none is open). Progress-bar
 * index validation needs it: progress_bar_count is bits 12-13 of the type
 * (refs/rpcs3/.../cellMsgDialog.h:82). */
uint32_t dialog_type();

/* cellMsgDialogProgressBarInc/Reset/SetMsg record the new bar value. RPCS3's
 * overlay keeps it and draws with it
 * (refs/.../Overlays/overlay_message_dialog.cpp:551-597); this runtime draws no
 * message-dialog pixels, so the value is stored and readable by a test rather
 * than silently discarded -- the call's console effect was to move a bar, and
 * here a bar really is moved. Index and open state are validated by the caller
 * (runtime/src/hle/sysutil_hle.h) before these run. */
void progress_bar_inc(uint32_t index, uint32_t delta);
void progress_bar_reset(uint32_t index);
float progress_bar_value(uint32_t index);

uint32_t opens_seen();
uint32_t resolves_seen();

}  // namespace msgdialog
}  // namespace hle
