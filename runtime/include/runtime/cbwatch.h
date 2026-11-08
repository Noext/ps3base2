#pragma once

#include <cstdint>

/* The two ledgers this runtime needs to answer "what is the title still
 * waiting for": every import a thread calls (with its return value), and every
 * guest callback the title registered against every delivery it received.
 *
 * Both are off unless PS3BASE_CENSUS is set, because they cost a map lookup
 * per import and per callback. A callback that was registered and never
 * delivered is the direct shape of a stall, and it is invisible in a log that
 * only shows what was called. */
namespace cbwatch {

bool enabled();

/* Import ledger. Called from the single import dispatch point, so every HLE
 * and LLE import lands here exactly once. */
void note_import(const char* module, const char* name, uint64_t ret, uint32_t lr, bool lle);

/* Callback ledger. note_register names the site that registered the OPD;
 * note_call counts one delivery of it. */
void note_register(const char* kind, uint32_t opd, uint32_t arg);
void note_call(const char* from, uint32_t opd, uint64_t arg1);

/* Writes both ledgers to the log. Periodic, so a run that is killed by
 * "timeout" still leaves its inventory behind. */
void dump(const char* why);

}
