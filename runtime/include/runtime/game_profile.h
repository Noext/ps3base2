/* ps3base -- the game profile.

 * One type describes everything a title-specific layer may contribute to the
 * boot. The generic runtime never names a game; it asks the profile, and the
 * profile is empty for a title that has nothing extra to say.
 *
 * What lives behind this interface is exactly what does NOT generalise:
 *
 *   - the guest image name and its disc metadata,
 *   - the sysutil/disc paths the CRT is handed,
 *   - hooks registered by guest address (patches, job intercepts, watchpoints),
 *   - SPU job natives, which are handwritten rewrites of one title's own jobs.
 *
 * Everything else -- the loader, the LV2 kernel, the VFS, PPU and SPU
 * execution, the RSX front end -- is generic and must stay so. If a change to
 * the runtime needs to know a title's name, it belongs here instead.
 */
#ifndef PS3BASE_GAME_PROFILE_H
#define PS3BASE_GAME_PROFILE_H

#include <cstdint>
#include <string>
#include <vector>

namespace game {

/* A hook the profile installs before the guest runs. `guest_addr` is a guest
 * text address; `what` is for the log. Returning false leaves it uninstalled,
 * so a profile whose addresses do not match the loaded image degrades to doing
 * nothing rather than faulting. */
struct AddressHook {
    uint32_t guest_addr;
    const char* what;
};

class Profile {
public:
    virtual ~Profile() = default;

    /* Short identifier: matches the image name in recomp/images.json. */
    virtual const char* name() const = 0;

    /* The guest path handed to the CRT as argv[0]. Empty means "derive it from
     * the disc metadata", which is what a generic boot wants. */
    virtual std::string self_path(const std::string& title_id) const
    {
        (void)title_id;
        return {};
    }

    /* Address-keyed hooks. Empty for a generic title. */
    virtual std::vector<AddressHook> address_hooks() const { return {}; }

    /* Install the title's SPU job natives. Called once, lazily, by the first
     * SPU thread. A generic title installs nothing and its SPU work is AOT
     * lifted like any other code. */
    virtual void install_spu_natives() const {}

    /* Extra PPU work before the guest starts, if the title needs a shim.
     * Default: nothing. */
    virtual void install_ppu_shims() const {}
};

/* The profile in force. Never null: an empty profile is the default. */
const Profile& current();
void set_profile(const Profile* p);

/* The empty profile -- what a title with no special layer gets. */
const Profile& empty_profile();

/* Register a profile at static-init time. One file per title does this; adding
 * a title never edits the runtime. */
struct Registrar {
    explicit Registrar(const Profile* p);
};

/* Look a profile up by image name. Only the profiles this build was configured
 * with exist; anything else gets the empty one. */
const Profile* find(const std::string& name);

/* How many profiles this build compiled in, and the first of them. A build
 * with exactly one does not need the operator to name it. */
unsigned profiles_available();
const Profile* first_profile();

}  // namespace game

#endif
