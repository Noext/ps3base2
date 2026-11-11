/* ps3base -- the decision logic of cellGame, in a header so a test drives the
 * same code the imports run.
 *
 * Same reason as lwmutex_hle.h: a test that re-implements the thing under test
 * proves nothing. Everything here is host-side and guest-memory-free, so the
 * unit test links no PPU runtime at all.
 *
 * Sources, by name and line:
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.h:8-29     CellGameError
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.h:58-67    sizes
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.h:81-99    game types, RET_*
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.h:106-143  PARAM.SFO ids
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.cpp:1446-1476  strkey_flag
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.cpp:1487-1545  key table
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.cpp:914-933    CATEGORY test
 *   refs/rpcs3/rpcs3/Loader/PSF.h:146-149                 is_cat_hdd
 *   refs/rpcs3/rpcs3/Loader/PSF.cpp:299-330               SFO writer
 */
#ifndef PS3BASE_HLE_CELLGAME_H
#define PS3BASE_HLE_CELLGAME_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hle {
namespace cellgame {

/* refs/.../cellGame.h:8-29. */
constexpr int32_t ERR_NOTFOUND = static_cast<int32_t>(0x8002cb04);
constexpr int32_t ERR_BROKEN = static_cast<int32_t>(0x8002cb05);
constexpr int32_t ERR_INTERNAL = static_cast<int32_t>(0x8002cb06);
constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002cb07);
constexpr int32_t ERR_ACCESS = static_cast<int32_t>(0x8002cb09);
constexpr int32_t ERR_NOSPACE = static_cast<int32_t>(0x8002cb20);
constexpr int32_t ERR_NOTSUPPORTED = static_cast<int32_t>(0x8002cb21);
constexpr int32_t ERR_FAILURE = static_cast<int32_t>(0x8002cb22);
constexpr int32_t ERR_INVALID_ID = static_cast<int32_t>(0x8002cb25);
constexpr int32_t ERR_EXIST = static_cast<int32_t>(0x8002cb26);
constexpr int32_t ERR_NOTPATCH = static_cast<int32_t>(0x8002cb27);

/* refs/.../cellGame.h:96-99 and :63. */
constexpr int32_t RET_OK = 0;
constexpr int32_t RET_CANCEL = 1;
constexpr int32_t RET_NONE = 2;
constexpr int32_t SIZEKB_NOTCALC = -1;
constexpr int32_t HDD_FREE_SIZE_KB = 40 * 1024 * 1024 - 256;

/* refs/.../cellGame.h:81-84, :93. */
constexpr uint32_t GAMETYPE_DISC = 1;
constexpr uint32_t GAMETYPE_HDD = 2;
constexpr uint32_t GAMETYPE_GAMEDATA = 3;
constexpr uint32_t ATTRIBUTE_PATCH = 0x1;

/* refs/.../cellGame.h:58-67. */
constexpr uint32_t GAME_PATH_MAX = 128;
constexpr uint32_t DIRNAME_SIZE = 32;
constexpr uint32_t SYSP_TITLE_SIZE = 128;
constexpr uint32_t SYSP_TITLEID_SIZE = 10;
constexpr uint32_t SYSP_VERSION_SIZE = 6;
constexpr uint32_t SYSP_PS3_SYSTEM_VER_SIZE = 8;
constexpr uint32_t SYSP_APP_VER_SIZE = 6;

/* refs/.../cellGame.h:106-130. */
enum : uint32_t {
    PARAMID_TITLE = 0,
    PARAMID_TITLE_DEFAULT = 1,
    PARAMID_TITLE_ID = 100,
    PARAMID_VERSION = 101,
    PARAMID_PS3_SYSTEM_VER = 105,
    PARAMID_APP_VER = 106,
};

/* The strkey_flag bitset, refs/.../cellGame.cpp:1446-1476. */
constexpr uint32_t KEY_GET_GAME_DATA = 1u << 0;
constexpr uint32_t KEY_SET_GAME_DATA = 1u << 1;
constexpr uint32_t KEY_GET_OTHER = 1u << 2;

struct StringKey {
    const char* name;
    uint32_t max;
    uint32_t flags;
};

/* refs/.../cellGame.cpp:1487-1545 (get_param_string_key). */
inline StringKey key_for(uint32_t id) {
    switch (id) {
    case PARAMID_TITLE: return {"TITLE", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_GET_OTHER | KEY_SET_GAME_DATA};
    case PARAMID_TITLE_DEFAULT: return {"TITLE", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_GET_OTHER | KEY_SET_GAME_DATA};
    case 2: return {"TITLE_00", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 3: return {"TITLE_01", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 4: return {"TITLE_02", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 5: return {"TITLE_03", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 6: return {"TITLE_04", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 7: return {"TITLE_05", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 8: return {"TITLE_06", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 9: return {"TITLE_07", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 10: return {"TITLE_08", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 11: return {"TITLE_09", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 12: return {"TITLE_10", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 13: return {"TITLE_11", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 14: return {"TITLE_12", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 15: return {"TITLE_13", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 16: return {"TITLE_14", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 17: return {"TITLE_15", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 18: return {"TITLE_16", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 19: return {"TITLE_17", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 20: return {"TITLE_18", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case 21: return {"TITLE_19", SYSP_TITLE_SIZE, KEY_GET_GAME_DATA | KEY_SET_GAME_DATA};
    case PARAMID_TITLE_ID: return {"TITLE_ID", SYSP_TITLEID_SIZE, KEY_GET_GAME_DATA | KEY_GET_OTHER};
    case PARAMID_VERSION: return {"VERSION", SYSP_VERSION_SIZE, KEY_GET_GAME_DATA};
    case PARAMID_PS3_SYSTEM_VER: return {"PS3_SYSTEM_VER", SYSP_PS3_SYSTEM_VER_SIZE, 0};
    case PARAMID_APP_VER: return {"APP_VER", SYSP_APP_VER_SIZE, KEY_GET_GAME_DATA | KEY_GET_OTHER};
    }
    return {nullptr, 0, 0};
}

/* Which SFO the last Check call established. RPCS3 content_permission::check_mode
 * (refs/.../cellGame.cpp:60-80). */
enum class CheckMode { NotSet, DiscGame, Patch, HddGame, GameData };

/* refs/.../cellGame.cpp:1458-1474 (string_key_info::is_supported). */
inline bool key_supported(const StringKey& k, bool is_setter, CheckMode mode) {
    if (!k.name) return false;
    switch (mode) {
    case CheckMode::GameData:
    case CheckMode::Patch:
        return (k.flags & (is_setter ? KEY_SET_GAME_DATA : KEY_GET_GAME_DATA)) != 0;
    case CheckMode::HddGame:
    case CheckMode::DiscGame:
        return !is_setter && (k.flags & KEY_GET_OTHER) != 0;
    case CheckMode::NotSet:
        return false;
    }
    return false;
}

/* refs/rpcs3/rpcs3/Loader/PSF.h:146-149. */
inline bool is_cat_hdd(const std::string& cat) {
    return cat.size() == 2 && cat[1] != 'D' && cat != "DG" && cat != "MS";
}

/* The CATEGORY test of cellGameDataCheck, refs/.../cellGame.cpp:918-928.
 * Match means "the data there is the kind asked for"; Mismatch means it is
 * there but of another kind (BROKEN); Absent means no SFO was readable at all
 * (RET_NONE). */
enum class CategoryMatch { Match, Mismatch, Absent };

inline CategoryMatch check_category(uint32_t type, const std::string& cat, bool sfo_present) {
    if (!sfo_present) return CategoryMatch::Absent;
    bool mismatch = false;
    switch (type) {
    case GAMETYPE_HDD: mismatch = !is_cat_hdd(cat); break;
    case GAMETYPE_GAMEDATA: mismatch = cat != "GD"; break;
    case GAMETYPE_DISC: mismatch = cat != "DG"; break;
    default: mismatch = true; break;
    }
    return mismatch ? CategoryMatch::Mismatch : CategoryMatch::Match;
}

/* The CATEGORY switch of cellGameBootCheck, refs/.../cellGame.cpp:789-831. */
struct BootMode {
    uint32_t type = GAMETYPE_DISC;
    uint32_t attributes = 0;
    CheckMode mode = CheckMode::DiscGame;
    bool from_patch_dir = false;
};

inline BootMode boot_mode_from_category(const std::string& cat) {
    BootMode m;
    if (cat == "DG") {
        m.type = GAMETYPE_DISC;
        m.attributes = 0;
        m.mode = CheckMode::DiscGame;
    } else if (cat == "GD") {
        /* Patch data: reported as a DISC game carrying the patch attribute
         * (cellGame.cpp:803-810). */
        m.type = GAMETYPE_DISC;
        m.attributes = ATTRIBUTE_PATCH;
        m.mode = CheckMode::Patch;
        m.from_patch_dir = true;
    } else {
        m.type = GAMETYPE_HDD;
        m.attributes = 0;
        m.mode = CheckMode::HddGame;
        m.from_patch_dir = true;
    }
    return m;
}

/* refs/.../cellGame.h:132-143: the dialog types, and only these. */
inline bool content_error_type_valid(uint32_t type) {
    return type <= 2 || (type >= 100 && type <= 102);
}
inline bool content_error_type_exits(uint32_t type) { return type > 2; }

/* --- PARAM.SFO writer ----------------------------------------------------
 * Format: refs/rpcs3/rpcs3/Loader/PSF.cpp:299-330 (save_object) and the key
 * format constants in refs/.../PSF.h:37-40. 20-byte header, 16-byte index
 * entries, NUL-terminated keys padded to 4, one max_size-byte data slot per
 * entry, little-endian. */
struct SfoEntry {
    std::string key;
    std::string str;
    uint32_t max;
};

inline void put16(std::vector<uint8_t>& v, size_t off, uint16_t x) {
    v[off] = uint8_t(x);
    v[off + 1] = uint8_t(x >> 8);
}
inline void put32(std::vector<uint8_t>& v, size_t off, uint32_t x) {
    for (int i = 0; i < 4; i++) v[off + i] = uint8_t(x >> (8 * i));
}
inline uint32_t get_le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline uint16_t get_le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

/* Build the SFO image. Pure, so a test can check the bytes without a file. */
inline std::vector<uint8_t> build_psf(const std::vector<SfoEntry>& entries) {
    std::vector<uint8_t> index(entries.size() * 16, 0);
    std::string keys;
    std::vector<uint8_t> data;
    for (size_t i = 0; i < entries.size(); i++) {
        const SfoEntry& e = entries[i];
        const size_t at = i * 16;
        put16(index, at, static_cast<uint16_t>(keys.size()));
        put16(index, at + 2, 0x0204);   /* format: string, PSF.h:37-40 */
        put32(index, at + 4, static_cast<uint32_t>(std::min<size_t>(e.str.size() + 1, e.max)));
        put32(index, at + 8, e.max);
        put32(index, at + 12, static_cast<uint32_t>(data.size()));
        keys += e.key;
        keys.push_back('\0');
        const size_t off = data.size();
        data.resize(off + e.max, 0);
        if (e.max > 1) std::memcpy(&data[off], e.str.data(), std::min<size_t>(e.str.size(), e.max - 1));
    }
    while (keys.size() % 4) keys.push_back('\0');

    std::vector<uint8_t> out(20, 0);
    out[1] = 'P';
    out[2] = 'S';
    out[3] = 'F';
    put32(out, 4, 0x0101);
    const uint32_t key_table = static_cast<uint32_t>(20 + index.size());
    put32(out, 8, key_table);
    put32(out, 12, static_cast<uint32_t>(key_table + keys.size()));
    put32(out, 16, static_cast<uint32_t>(entries.size()));
    out.insert(out.end(), index.begin(), index.end());
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

inline bool write_psf(const std::string& host_file, const std::vector<SfoEntry>& entries) {
    const std::vector<uint8_t> out = build_psf(entries);
    FILE* f = std::fopen(host_file.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

/* The four entries a game-data directory carries, refs/.../cellGame.cpp:1327-1332. */
inline std::vector<SfoEntry> game_data_entries(const std::string& title_id, const std::string& title,
                                               const std::string& version) {
    return {{"CATEGORY", "GD", 4},
            {"TITLE_ID", title_id, 16},
            {"TITLE", title, SYSP_TITLE_SIZE},
            {"VERSION", version, 8}};
}

/* Parse the image build_psf produces, so a test can round-trip. Mirrors
 * vfs::Psf::load (runtime/src/vfs.cpp) but takes bytes instead of a path. */
struct ParsedKey {
    std::string key;
    uint16_t fmt;
    uint32_t len;
    uint32_t max;
    std::string str;
    uint32_t num;
};

inline bool parse_psf(const std::vector<uint8_t>& d, std::vector<ParsedKey>& out) {
    out.clear();
    if (d.size() < 20 || d[0] != 0 || d[1] != 'P' || d[2] != 'S' || d[3] != 'F') return false;
    if (get_le32(&d[4]) != 0x0101) return false;
    const uint32_t key_table = get_le32(&d[8]);
    const uint32_t data_table = get_le32(&d[12]);
    const uint32_t count = get_le32(&d[16]);
    if (key_table < 20 || key_table > data_table || data_table > d.size()) return false;
    if (20 + uint64_t(count) * 16 > d.size()) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* e = &d[20 + i * 16];
        const uint16_t key_off = get_le16(e);
        ParsedKey k;
        k.fmt = get_le16(e + 2);
        k.len = get_le32(e + 4);
        k.max = get_le32(e + 8);
        const uint32_t data_off = get_le32(e + 12);
        const size_t key_at = key_table + key_off;
        if (key_at >= data_table) return false;
        k.key = reinterpret_cast<const char*>(&d[key_at]);
        if (data_off >= d.size() - data_table) return false;
        const uint8_t* v = &d[data_table + data_off];
        if (k.fmt == 0x0404) {
            k.num = get_le32(v);
        } else {
            size_t n = 0;
            while (n < k.len && v[n]) n++;
            k.str.assign(reinterpret_cast<const char*>(v), n);
        }
        out.push_back(std::move(k));
    }
    return true;
}

}  // namespace cellgame
}  // namespace hle

#endif
