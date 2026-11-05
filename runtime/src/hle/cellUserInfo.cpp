/* ps3base -- cellUserInfo: who is logged in.

 * A title calls these at startup to learn the account it should save under and
 * whose trophies to load. Returning ENOSYS gave the caller a null list, and the
 * trophy thread then dereferenced a field of a structure nobody had filled --
 * a crash an address away from the honest cause.
 *
 * There is one user here: id 1, name "User". That is a complete answer for a
 * single-account machine, and a title that reads it gets a usable structure
 * rather than zeros.
 */
#include "common.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace hle {

namespace {

constexpr uint32_t kUsernameSize = 128;
constexpr uint32_t kUserMax = 64;

/* CellUserInfoUserStat: u32 id, char name[128]. */
void write_userstat(uint32_t ea, uint32_t id, const char* name)
{
    vm::write32(ea, id);
    char* dst = reinterpret_cast<char*>(vm::ptr(ea + 4));
    std::memset(dst, 0, kUsernameSize);
    const size_t n = std::strlen(name);
    std::memcpy(dst, name, n < kUsernameSize ? n : kUsernameSize);
}

}  // namespace

/* cellUserInfoGetList( u32* listNum, CellUserInfoUserList* listBuf,
 *                      u32* currentUserId ) */
HLE_FUNC(cellUserInfo, cellUserInfoGetList) {
    const uint32_t list_num = a32(ctx, 0);
    const uint32_t list_buf = a32(ctx, 1);
    const uint32_t current = a32(ctx, 2);

    if (!list_num) {
        if (list_buf || !current) return ret(ctx, static_cast<int32_t>(0x80024802));
        vm::write32(current, 1);
        return ret(ctx, 0);
    }

    if (list_buf) {
        /* One user, and the list is an array of u32 ids. */
        vm::write32(list_buf, 1);
        for (uint32_t i = 1; i < kUserMax; i++) vm::write32(list_buf + i * 4, 0);
    }
    vm::write32(list_num, 1);
    if (current) vm::write32(current, 1);
    LOG_INFO("cellUserInfo", "GetList -> 1 user (id 1)");
    ret(ctx, 0);
}

/* cellUserInfoGetStat( u32 id, CellUserInfoUserStat* stat ) */
HLE_FUNC(cellUserInfo, cellUserInfoGetStat) {
    const uint32_t id = a32(ctx, 0);
    const uint32_t stat = a32(ctx, 1);
    if (id == 0 || id > kUserMax) return ret(ctx, static_cast<int32_t>(0x80024802));
    if (!stat) return ret(ctx, static_cast<int32_t>(0x80024802));
    write_userstat(stat, 1, "User");
    LOG_INFO("cellUserInfo", "GetStat(%u) -> id 1, name 'User'", id);
    ret(ctx, 0);
}

/* cellUserInfoSelectUser_SetList and friends exist but are only reached when a
 * title lets the player pick an account. Nothing here offers a choice, so the
 * list is the one user and the selection is not offered.
 */
HLE_FUNC(cellUserInfo, cellUserInfoSelectUser_SetList) {
    ret(ctx, 0);
}

}  // namespace hle
