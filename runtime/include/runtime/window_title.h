#pragma once

#include <string>

namespace ps3base {

/* The current game's TITLE from its PARAM.SFO, or "ps3base" with no disc. */
std::string window_title();
std::wstring window_title_w();

}  // namespace ps3base
