/* ps3base -- the God of War: Ascension profile.

 * This is the reference example of a non-trivial profile, and the only place in
 * the tree allowed to know this game exists. Everything it declares is
 * something that does NOT generalise: guest addresses, SPU job natives, the
 * CRT's argv[0].
 *
 * Compiled only when PS3BASE_PROFILE_GOWA is on. A build without it has an
 * empty profile and boots any title on the generic runtime alone.
 */
#include "runtime/game_profile.h"

#ifdef PS3BASE_PROFILE_GOWA

#include <cstdlib>

namespace {

class GowaProfile final : public game::Profile {
public:
    const char* name() const override { return "gowa"; }

    std::string self_path(const std::string& title_id) const override
    {
        if (title_id.empty()) return "/dev_bdvd/PS3_GAME/USRDIR/GOWA.SELF";
        return "/dev_hdd0/game/" + title_id + "/USRDIR/GOWA.SELF";
    }

    /* Address-keyed watchpoints. These are this title's guest addresses; a
     * foreign image never sees them because this profile is not in force. */
    std::vector<game::AddressHook> address_hooks() const override
    {
        if (!std::getenv("PS3BASE_GAME_HOOKS")) return {};
        return {
            {0x52bd7c, "panic"},
            {0x3a2adc, "ring"},
            {0x3a2690, "ring"},
            {0x3a12a0, "ring"},
            {0x3a0000, "ring"},
            {0x39f3c4, "ring"},
            {0x39f06c, "ring"},
            {0x39b6f4, "ring"},
        };
    }

    /* The SPU job rewrites stay in runtime/src/jobsys and runtime/src/spu/*;
     * this only turns them on for the title that owns them. */
    void install_spu_natives() const override;
};

/* Defined in runtime/src/spu/hle_builtin.cpp: registers this title's SPU job
 * natives. Only reachable when the profile is in force. */
namespace spu { void install_gowa_natives(); }

void GowaProfile::install_spu_natives() const { spu::install_gowa_natives(); }

GowaProfile g_profile;
game::Registrar g_registrar(&g_profile);

}  // namespace

#endif  // PS3BASE_PROFILE_GOWA
