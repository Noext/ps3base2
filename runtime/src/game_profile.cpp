/* ps3base -- the game profile registry. See runtime/game_profile.h. */
#include "runtime/game_profile.h"

#include <cstring>
#include <vector>

namespace game {

namespace {

/* The empty profile: a title that needs nothing beyond the generic runtime. */
class EmptyProfile final : public Profile {
public:
    const char* name() const override { return ""; }
};

/* Profiles register themselves here at static-init time. A single translation
 * unit per profile appends itself, so adding a title never touches this file. */
std::vector<const Profile*>& registry()
{
    static std::vector<const Profile*> v;
    return v;
}

const EmptyProfile g_empty;
const Profile* g_current = &g_empty;

}  // namespace

Registrar::Registrar(const Profile* p) { registry().push_back(p); }

const Profile& empty_profile() { return g_empty; }
const Profile& current() { return *g_current; }
void set_profile(const Profile* p) { g_current = p ? p : &g_empty; }

const Profile* find(const std::string& name)
{
    /* Profiles are registered at static-init time by the files that define
     * them, so this stays a linear scan over a handful. */
    for (const Profile* p : registry()) {
        const char* n = p->name();
        if (n && name == n) return p;
    }
    return nullptr;
}

}  // namespace game
