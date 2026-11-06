/* ps3base -- the window title.

 * The window used to carry the name of the fork's origin. Once a disc is
 * mounted the honest title is the disc's own: its TITLE from PARAM.SFO, which
 * is what the console would show. With no disc, the runtime's own name.
 */
#include "runtime/vfs.h"

#include <string>

namespace ps3base {

std::string window_title()
{
    const std::string title = vfs::disc_sfo().str("TITLE");
    if (!title.empty()) return title;
    return "ps3base";
}

std::wstring window_title_w()
{
    const std::string t = window_title();
    return std::wstring(t.begin(), t.end());
}

}  // namespace ps3base
