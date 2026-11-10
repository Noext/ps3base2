#pragma once
#include <atomic>

/* The one behavioural signal specific to the main-menu screen that this build
 * shares with the native reference: the guest creating its friend-list thread.
 * Native creates it when the menu opens, and a boot that never reaches the menu
 * never creates it. Keyed here, in a header, so every translation unit agrees on
 * the symbol instead of each declaring its own extern. */
extern std::atomic<uint32_t> g_menu_thread_creations;

/* Interface threads the native reference creates when its menu opens: the
 * option loader and the save-list readers. Counted separately so the menu line
 * can require corroboration instead of trusting one marker. */
extern std::atomic<uint32_t> g_ui_thread_creations;
