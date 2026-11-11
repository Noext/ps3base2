/* ps3base -- sceNpTrophy's decision logic and trophy-file formats, in a header
 * so a unit test drives the SAME code the imports run.
 *
 * Same reason as cellgame_hle.h: a test that re-implements the thing under test
 * proves nothing. Everything here is host-side and guest-memory-free, so the
 * unit test links no PPU runtime and needs no game data, no GPU, no network.
 *
 * Sources, by file and line (refs/rpcs3/rpcs3/...):
 *   Emu/Cell/Modules/sceNpTrophy.h:10-56    SceNpTrophyError
 *   Emu/Cell/Modules/sceNpTrophy.h:58-70    sizes, flag layout
 *   Emu/Cell/Modules/sceNpTrophy.h:72-91    invalid id, grades, options
 *   Emu/Cell/Modules/sceNpTrophy.h:93-135   the four guest structs
 *   Emu/Cell/Modules/sceNpTrophy.h:137-157  SceNpTrophyStatus and COMM_SIGN magic
 *   Emu/Cell/Modules/sceNpTrophy.cpp:37-110 trophy_context_t / trophy_handle_t id pools
 *   Emu/Cell/Modules/sceNpTrophy.cpp:118-166 get_context_ex
 *   Emu/Cell/Modules/sceNpTrophy.cpp:290-311 sceNpTrophyInit
 *   Emu/Cell/Modules/sceNpTrophy.cpp:313-332 sceNpTrophyTerm
 *   Emu/Cell/Modules/sceNpTrophy.cpp:334-361 sceNpTrophyCreateHandle
 *   Emu/Cell/Modules/sceNpTrophy.cpp:363-389 sceNpTrophyDestroyHandle
 *   Emu/Cell/Modules/sceNpTrophy.cpp:397-428 sceNpTrophyAbortHandle
 *   Emu/Cell/Modules/sceNpTrophy.cpp:430-526 sceNpTrophyCreateContext
 *   Emu/Cell/Modules/sceNpTrophy.cpp:528-553 sceNpTrophyDestroyContext
 *   Emu/Cell/Modules/sceNpTrophy.cpp:769-775 RegisterContext status sequence
 *   Emu/Cell/Modules/sceNpTrophy.cpp:1007-1057 GetGameInfo counting
 *   Emu/Cell/Modules/sceNpTrophy.cpp:1088-1138 UnlockTrophy decisions
 *   Emu/Cell/Modules/sceNpTrophy.cpp:1259-1370 NpTrophyGetTrophyInfo
 *   Loader/TRP.h:3-22                      TRPHeader / TRPEntry
 *   Loader/TRP.cpp:109-192                 LoadHeader / GetRequiredSpace
 *   Loader/TRP.cpp:194-246                 ContainsEntry / RemoveEntry / RenameEntry
 *   Loader/TROPUSR.h:6-58                  TROPUSRHeader / TableHeader / Entry4 / Entry6
 *   Loader/TROPUSR.cpp:34-232              Load / LoadTables / Generate / Save
 *   Loader/TROPUSR.cpp:234-394             the getters and UnlockTrophy
 */
#ifndef PS3BASE_HLE_NPTROPHY_H
#define PS3BASE_HLE_NPTROPHY_H

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace hle {
namespace trophy {

/* --- error codes ---------------------------------------------------------
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpTrophy.h:10-56. Only the codes the
 * twelve imported functions can return are named; the rest stay in the header
 * so a later handler does not retype them from memory. */
constexpr int32_t ERR_ALREADY_INITIALIZED     = static_cast<int32_t>(0x80022901);
constexpr int32_t ERR_NOT_INITIALIZED         = static_cast<int32_t>(0x80022902);
constexpr int32_t ERR_NOT_SUPPORTED           = static_cast<int32_t>(0x80022903);
constexpr int32_t ERR_CONTEXT_NOT_REGISTERED  = static_cast<int32_t>(0x80022904);
constexpr int32_t ERR_INVALID_ARGUMENT        = static_cast<int32_t>(0x80022906);
constexpr int32_t ERR_EXCEEDS_MAX             = static_cast<int32_t>(0x80022907);
constexpr int32_t ERR_UNKNOWN_CONTEXT         = static_cast<int32_t>(0x8002290a);
constexpr int32_t ERR_INVALID_CONTEXT         = static_cast<int32_t>(0x8002290e);
constexpr int32_t ERR_PROCESSING_ABORTED      = static_cast<int32_t>(0x8002290f);
constexpr int32_t ERR_ABORT                   = static_cast<int32_t>(0x80022910);
constexpr int32_t ERR_UNKNOWN_HANDLE          = static_cast<int32_t>(0x80022911);
constexpr int32_t ERR_LOCKED                  = static_cast<int32_t>(0x80022912);
constexpr int32_t ERR_HIDDEN                  = static_cast<int32_t>(0x80022913);
constexpr int32_t ERR_CANNOT_UNLOCK_PLATINUM  = static_cast<int32_t>(0x80022914);
constexpr int32_t ERR_ALREADY_UNLOCKED        = static_cast<int32_t>(0x80022915);
constexpr int32_t ERR_INVALID_NP_COMM_ID      = static_cast<int32_t>(0x80022918);
constexpr int32_t ERR_CONF_DOES_NOT_EXIST     = static_cast<int32_t>(0x8002291b);
constexpr int32_t ERR_ILLEGAL_UPDATE          = static_cast<int32_t>(0x8002292a);
constexpr int32_t ERR_INVALID_TROPHY_ID       = static_cast<int32_t>(0x80022920);

/* refs/.../sceNpTrophy.h:58-91. */
constexpr uint32_t TITLE_MAX_SIZE     = 128;
constexpr uint32_t GAME_DESCR_MAX_SIZE = 1024;
constexpr uint32_t NAME_MAX_SIZE      = 128;
constexpr uint32_t DESCR_MAX_SIZE     = 1024;
constexpr uint32_t FLAG_SETSIZE       = 128;
constexpr uint32_t FLAG_BITS_SHIFT    = 5;
constexpr uint32_t INVALID_TROPHY_ID  = 0xffffffffu;

constexpr uint32_t GRADE_UNKNOWN  = 0;
constexpr uint32_t GRADE_PLATINUM = 1;
constexpr uint32_t GRADE_GOLD     = 2;
constexpr uint32_t GRADE_SILVER   = 3;
constexpr uint32_t GRADE_BRONZE   = 4;

constexpr uint64_t OPTIONS_CREATE_CONTEXT_READ_ONLY = 1;
constexpr uint64_t OPTIONS_REGISTER_CONTEXT_SHOW_ERROR_EXIT = 1;

/* refs/.../sceNpTrophy.h:137-157. */
constexpr uint32_t STATUS_UNKNOWN             = 0;
constexpr uint32_t STATUS_NOT_INSTALLED       = 1;
constexpr uint32_t STATUS_DATA_CORRUPT        = 2;
constexpr uint32_t STATUS_INSTALLED           = 3;
constexpr uint32_t STATUS_REQUIRES_UPDATE     = 4;
constexpr uint32_t STATUS_PROCESSING_SETUP    = 5;
constexpr uint32_t STATUS_PROCESSING_PROGRESS = 6;
constexpr uint32_t STATUS_PROCESSING_FINALIZE = 7;
constexpr uint32_t STATUS_PROCESSING_COMPLETE = 8;
constexpr uint32_t STATUS_CHANGES_DETECTED    = 9;

constexpr uint32_t COMM_SIGN_MAGIC = 0xB9DDE13B;

/* --- the guest-visible structures, mirrored byte for byte ---------------
 * refs/.../sceNpTrophy.h:93-135. Kept as host structs with the SAME layout so
 * a handler can build one here and copy it out field by field with offsetof;
 * the static_asserts are what stop a guessed size from reaching the guest. */
struct GameDetails {
    uint32_t numTrophies;
    uint32_t numPlatinum;
    uint32_t numGold;
    uint32_t numSilver;
    uint32_t numBronze;
    char title[TITLE_MAX_SIZE];
    char description[GAME_DESCR_MAX_SIZE];
    uint8_t reserved[4];
};
static_assert(sizeof(GameDetails) == 1176, "SceNpTrophyGameDetails: 5*4 + 128 + 1024 + 4");
static_assert(offsetof(GameDetails, title) == 20, "title follows the five counters");
static_assert(offsetof(GameDetails, description) == 148, "description follows title");
static_assert(offsetof(GameDetails, reserved) == 1172, "reserved trails description");

struct GameData {
    uint32_t unlockedTrophies;
    uint32_t unlockedPlatinum;
    uint32_t unlockedGold;
    uint32_t unlockedSilver;
    uint32_t unlockedBronze;
};
static_assert(sizeof(GameData) == 20, "SceNpTrophyGameData");

struct TrophyDetails {
    int32_t trophyId;
    uint32_t trophyGrade;
    char name[NAME_MAX_SIZE];
    char description[DESCR_MAX_SIZE];
    uint8_t hidden;
    uint8_t reserved[3];
};
static_assert(sizeof(TrophyDetails) == 1164, "SceNpTrophyDetails: 8 + 128 + 1024 + 4");
static_assert(offsetof(TrophyDetails, name) == 8, "name follows id and grade");
static_assert(offsetof(TrophyDetails, description) == 136, "description follows name");
static_assert(offsetof(TrophyDetails, hidden) == 1160, "hidden trails description");

struct TrophyData {
    uint64_t timestamp;
    int32_t trophyId;
    uint8_t unlocked;
    uint8_t reserved[3];
};
static_assert(sizeof(TrophyData) == 16, "SceNpTrophyData: u64 + s32 + b8 + 3");

struct FlagArray {
    uint32_t flag_bits[FLAG_SETSIZE >> FLAG_BITS_SHIFT];
};
static_assert(sizeof(FlagArray) == 16, "SceNpTrophyFlagArray: four u32 for 128 bits");

/* SceNpCommunicationId and SceNpCommunicationSignature, the two structures
 * sceNpTrophyCreateContext reads. refs/.../sceNp.h:1258-1263 and :1359-1362,
 * with SCE_NP_COMMUNICATION_SIGNATURE_SIZE = 160 at sceNp.h:1053. */
struct CommId {
    char data[9 + 1];
    uint8_t num;
    char dummy;
};
static_assert(sizeof(CommId) == 12, "SceNpCommunicationId: 10 + 1 + 1");

struct Signature {
    uint8_t data[160];
};
static_assert(sizeof(Signature) == 160, "SceNpCommunicationSignature");

/* --- SHA-1 ---------------------------------------------------------------
 * TRPLoader::LoadHeader verifies a SHA-1 over the whole TRP for version >= 2
 * (refs/.../Loader/TRP.cpp:133-166). Without it a corrupt trophy pack is
 * accepted and the failure lands in TROPCONF.SFM parsing instead.
 * FIPS 180-1; the known-answer vectors are in tests/nptrophy.cpp. */
inline uint32_t rol32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

inline bool sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    const uint64_t bits = static_cast<uint64_t>(len) * 8ull;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; i--) msg.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xff));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);            k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6u; }
            const uint32_t t = rol32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol32(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[i * 4 + 0] = static_cast<uint8_t>(h[i] >> 24);
        out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    return true;
}

/* Big-endian accessors over a byte buffer. The guest is big-endian and these
 * files are big-endian too, so a host-order struct is a bug that reads
 * plausible. */
inline uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}
inline uint64_t be64(const uint8_t* p) {
    return (static_cast<uint64_t>(be32(p)) << 32) | be32(p + 4);
}

/* Write a C string into a fixed guest/host buffer the way strcpy_trunc does:
 * always terminates, never overruns. */
inline void copy_trunc(char* dst, size_t cap, const std::string& s) {
    if (!cap) return;
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

/* --- TROPHY.TRP ----------------------------------------------------------
 * refs/.../Loader/TRP.h:3-22. TRPHeader is 0x40 bytes, TRPEntry 0x40. */
struct TrpHeader {
    uint32_t trp_magic;
    uint32_t trp_version;
    uint64_t trp_file_size;
    uint32_t trp_files_count;
    uint32_t trp_element_size;
    uint32_t trp_dev_flag;
    uint8_t sha1[20];
    uint8_t padding[16];
};
static_assert(sizeof(TrpHeader) == 0x40, "TRPHeader is 64 bytes");
static_assert(offsetof(TrpHeader, trp_file_size) == 8, "file size at 8");
static_assert(offsetof(TrpHeader, sha1) == 0x1c, "sha1 at 0x1c");
static_assert(offsetof(TrpHeader, padding) == 0x30, "padding at 0x30");

struct TrpEntry {
    char name[32];
    uint64_t offset;
    uint64_t size;
    uint32_t unknown;
    char padding[12];
};
static_assert(sizeof(TrpEntry) == 0x40, "TRPEntry is 64 bytes");
static_assert(offsetof(TrpEntry, offset) == 32, "entry offset follows the name");
static_assert(offsetof(TrpEntry, size) == 40, "entry size follows the offset");

class Trp {
public:
    /* refs/.../Loader/TRP.cpp:109-184. */
    bool load(const std::vector<uint8_t>& data) {
        header_ = TrpHeader{};
        entries_.clear();
        if (data.size() < sizeof(TrpHeader)) return false;

        header_.trp_magic = be32(&data[0]);
        header_.trp_version = be32(&data[4]);
        header_.trp_file_size = be64(&data[8]);
        header_.trp_files_count = be32(&data[16]);
        header_.trp_element_size = be32(&data[20]);
        header_.trp_dev_flag = be32(&data[24]);
        std::memcpy(header_.sha1, &data[0x1c], 20);
        std::memcpy(header_.padding, &data[0x30], 16);

        if (header_.trp_magic != 0xDCA24D00u) return false;

        if (header_.trp_version >= 2) {
            /* TRP.cpp:135-163: the size must cover the header, and the hash over
             * the file with the sha1 field zeroed must match. */
            if (header_.trp_file_size < sizeof(TrpHeader)) return false;
            if (data.size() >= header_.trp_file_size) {
                std::vector<uint8_t> copy(data.begin(), data.begin() + static_cast<size_t>(header_.trp_file_size));
                std::memset(&copy[0x1c], 0, 20);
                uint8_t hash[20];
                sha1(copy.data(), copy.size(), hash);
                if (std::memcmp(hash, header_.sha1, 20) != 0) return false;
            } else {
                /* TRP.cpp:144-147: a short read only logs "Failed verifying
                 * checksum" and continues; the entries read decides. */
            }
        }

        const uint64_t first = sizeof(TrpHeader);
        const uint64_t need = first + static_cast<uint64_t>(header_.trp_files_count) * sizeof(TrpEntry);
        if (need > data.size()) return false;

        entries_.resize(header_.trp_files_count);
        for (uint32_t i = 0; i < header_.trp_files_count; i++) {
            const uint8_t* e = &data[first + static_cast<size_t>(i) * sizeof(TrpEntry)];
            std::memcpy(entries_[i].name, e, 32);
            entries_[i].offset = be64(e + 32);
            entries_[i].size = be64(e + 40);
            entries_[i].unknown = be32(e + 48);
            std::memcpy(entries_[i].padding, e + 52, 12);
        }
        return true;
    }

    /* refs/.../Loader/TRP.cpp:186-192: the bytes the install will write. */
    uint64_t required_space() const {
        const uint64_t element = static_cast<uint64_t>(header_.trp_files_count) * header_.trp_element_size;
        return header_.trp_file_size - sizeof(TrpHeader) - element;
    }

    /* refs/.../Loader/TRP.cpp:194-230. */
    bool contains(const std::string& filename) const {
        if (filename.size() >= sizeof(TrpEntry::name)) return false;
        for (const auto& e : entries_) if (entry_name(e) == filename) return true;
        return false;
    }
    void remove(const std::string& filename) {
        if (filename.size() >= sizeof(TrpEntry::name)) return;
        for (size_t i = 0; i < entries_.size();) {
            if (entry_name(entries_[i]) == filename) entries_.erase(entries_.begin() + i);
            else i++;
        }
    }
    void rename(const std::string& from, const std::string& to) {
        if (from.size() >= sizeof(TrpEntry::name) || to.size() >= sizeof(TrpEntry::name)) return;
        for (auto& e : entries_) {
            if (entry_name(e) == from) copy_trunc(e.name, sizeof(e.name), to);
        }
    }

    static std::string entry_name(const TrpEntry& e) {
        return std::string(e.name, strnlen(e.name, sizeof(e.name)));
    }

    const TrpHeader& header() const { return header_; }
    const std::vector<TrpEntry>& entries() const { return entries_; }

private:
    TrpHeader header_{};
    std::vector<TrpEntry> entries_;
};

/* The entry selection sceNpTrophyRegisterContext performs before installing a
 * TRP, refs/.../Emu/Cell/Modules/sceNpTrophy.cpp:677-708:
 *
 *   - if a "TROP_XX.SFM" exists for the current language, discard TROPCONF.SFM
 *     and TROP.SFM and rename the language file to TROPCONF.SFM;
 *   - otherwise, if TROP.SFM exists, discard TROPCONF.SFM and rename TROP.SFM;
 *   - otherwise TROPCONF.SFM must already be there, or the config is
 *     "invalid/incomplete" and registration answers
 *     SCE_NP_TROPHY_ERROR_ILLEGAL_UPDATE;
 *   - every other TROP_XX.SFM (00..18) is dropped.
 *
 * Returned by plan, applied by the caller, so the test can assert the outcome
 * without a filesystem. */
enum class EntryPlan {
    Ok,
    NoConfig,   /* -> SCE_NP_TROPHY_ERROR_ILLEGAL_UPDATE */
};

inline int32_t language() {
    /* CELL_SYSUTIL_SYSTEMPARAM_ID_LANG, answered by cellSysutilGetSystemParamInt
     * (runtime/src/hle/cellSysutil.cpp, id 0x111 -> 1). 1 is
     * CELL_SYSUTIL_LANG_ENGLISH_US (refs/.../cellSysutil.h:96), which is what
     * RPCS3's default is (system_config.h:327). */
    return 1;
}

inline std::string trp_language_entry(int32_t lang) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "TROP_%02d.SFM", lang);
    return buf;
}

/* refs/.../sceNpTrophy.cpp:677-708, in RPCS3's order. */
inline EntryPlan prepare_entries(Trp& trp, int32_t lang) {
    const std::string target = trp_language_entry(lang);

    if (trp.contains(target)) {
        trp.remove("TROPCONF.SFM");
        trp.remove("TROP.SFM");
        trp.rename(target, "TROPCONF.SFM");
    } else if (trp.contains("TROP.SFM")) {
        trp.remove("TROPCONF.SFM");
        trp.rename("TROP.SFM", "TROPCONF.SFM");
    } else if (!trp.contains("TROPCONF.SFM")) {
        return EntryPlan::NoConfig;
    }

    /* Discard unnecessary TROP_XX.SFM files (:700-708). */
    for (int32_t i = 0; i <= 18; i++) {
        if (i == lang) continue;
        trp.remove(trp_language_entry(i));
    }
    return EntryPlan::Ok;
}

/* --- TROPCONF.SFM --------------------------------------------------------
 * The trophy configuration is XML. RPCS3 parses it with rXml
 * (refs/.../Loader/TROPUSR.cpp:172-213, :294-330) and reads children of the
 * "trophyconf" element (:12-32). This is a minimal reader for exactly that
 * shape: elements, attributes, text, entities and comments. It does not claim
 * to be a general XML parser -- what it must do is return the same strings
 * rXmlNode::GetNodeContent and GetAttribute return for a trophy config. */
namespace xml {

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<Node> children;
    std::string text;

    const std::string& attr(const std::string& key) const {
        static const std::string none;
        for (const auto& a : attrs) if (a.first == key) return a.second;
        return none;
    }
    const Node* child(const std::string& key) const {
        for (const auto& c : children) if (c.name == key) return &c;
        return nullptr;
    }
};

inline void decode_entities(const std::string& in, std::string& out) {
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] != '&') { out.push_back(in[i]); continue; }
        const size_t semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out.push_back(in[i]); continue; }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") out.push_back('&');
        else if (ent == "lt") out.push_back('<');
        else if (ent == "gt") out.push_back('>');
        else if (ent == "quot") out.push_back('"');
        else if (ent == "apos") out.push_back('\'');
        else if (!ent.empty() && ent[0] == '#') {
            const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const unsigned long v = std::strtoul(ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            if (v < 0x80) out.push_back(static_cast<char>(v));
            else if (v < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (v >> 6)));
                out.push_back(static_cast<char>(0x80 | (v & 0x3F)));
            } else if (v < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (v >> 12)));
                out.push_back(static_cast<char>(0x80 | ((v >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (v & 0x3F)));
            }
        } else { out.push_back('&'); continue; }
        i = semi;
    }
}

inline bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

class Parser {
public:
    Parser(const std::string& s) : s_(s) {}

    bool document(Node& out) {
        skip_bom();
        if (!skip_misc()) return false;
        if (!element(out)) return false;
        skip_misc();
        return true;
    }

private:
    bool skip_bom() {
        if (s_.size() >= 3 && static_cast<uint8_t>(s_[0]) == 0xEF &&
            static_cast<uint8_t>(s_[1]) == 0xBB && static_cast<uint8_t>(s_[2]) == 0xBF) i_ = 3;
        return true;
    }
    void skip_ws() { while (i_ < s_.size() && is_space(s_[i_])) i_++; }
    /* Prolog, comments and doctype between elements. */
    bool skip_misc() {
        for (;;) {
            skip_ws();
            if (i_ + 1 >= s_.size()) return false;
            if (s_[i_] != '<') return true;
            if (s_.compare(i_, 4, "<!--") == 0) {
                const size_t e = s_.find("-->", i_ + 4);
                if (e == std::string::npos) return false;
                i_ = e + 3;
            } else if (s_.compare(i_, 2, "<?") == 0) {
                const size_t e = s_.find("?>", i_ + 2);
                if (e == std::string::npos) return false;
                i_ = e + 2;
            } else if (s_.compare(i_, 2, "<!") == 0) {
                const size_t e = s_.find('>', i_ + 2);
                if (e == std::string::npos) return false;
                i_ = e + 1;
            } else {
                return true;
            }
        }
    }
    bool name(std::string& out) {
        const size_t start = i_;
        while (i_ < s_.size() && !is_space(s_[i_]) && s_[i_] != '>' && s_[i_] != '/' && s_[i_] != '=') i_++;
        if (i_ == start) return false;
        out = s_.substr(start, i_ - start);
        return true;
    }
    bool element(Node& out) {
        if (i_ >= s_.size() || s_[i_] != '<') return false;
        i_++;
        if (!name(out.name)) return false;

        for (;;) {
            skip_ws();
            if (i_ >= s_.size()) return false;
            if (s_[i_] == '/') {
                if (i_ + 1 >= s_.size() || s_[i_ + 1] != '>') return false;
                i_ += 2;
                return true;
            }
            if (s_[i_] == '>') { i_++; break; }
            std::string key;
            if (!name(key)) return false;
            skip_ws();
            if (i_ >= s_.size() || s_[i_] != '=') return false;
            i_++;
            skip_ws();
            if (i_ >= s_.size() || (s_[i_] != '"' && s_[i_] != '\'')) return false;
            const char quote = s_[i_++];
            const size_t start = i_;
            while (i_ < s_.size() && s_[i_] != quote) i_++;
            if (i_ >= s_.size()) return false;
            std::string value;
            decode_entities(s_.substr(start, i_ - start), value);
            i_++;
            out.attrs.emplace_back(std::move(key), std::move(value));
        }

        /* Content: text, child elements, CDATA and comments. */
        for (;;) {
            if (i_ >= s_.size()) return false;
            if (s_[i_] == '<') {
                if (i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
                    const size_t e = s_.find('>', i_);
                    if (e == std::string::npos) return false;
                    i_ = e + 1;
                    return true;
                }
                if (s_.compare(i_, 4, "<!--") == 0) {
                    const size_t e = s_.find("-->", i_ + 4);
                    if (e == std::string::npos) return false;
                    i_ = e + 3;
                    continue;
                }
                if (s_.compare(i_, 9, "<![CDATA[") == 0) {
                    const size_t e = s_.find("]]>", i_ + 9);
                    if (e == std::string::npos) return false;
                    out.text.append(s_, i_ + 9, e - i_ - 9);
                    i_ = e + 3;
                    continue;
                }
                Node child;
                if (!element(child)) return false;
                out.children.push_back(std::move(child));
            } else {
                const size_t start = i_;
                while (i_ < s_.size() && s_[i_] != '<') i_++;
                decode_entities(s_.substr(start, i_ - start), out.text);
            }
        }
    }

    const std::string& s_;
    size_t i_ = 0;
};

}  // namespace xml

/* One trophy as TROPCONF.SFM describes it. Field names follow RPCS3's reads:
 * "id", "pid", "ttype" first char and "hidden" first char
 * (refs/.../Loader/TROPUSR.cpp:194-205, :298-299; sceNpTrophy.cpp:1302-1325). */
struct TrophyEntry {
    int32_t id = 0;
    int32_t pid = 0;
    char type = '\0';
    bool hidden = false;
    std::string name;
    std::string detail;
};

struct TrophyConf {
    std::string title_name;
    std::string title_description;
    std::vector<TrophyEntry> trophies;

    bool parse(const std::vector<uint8_t>& bytes) {
        const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        xml::Parser parser(text);
        xml::Node doc;
        if (!parser.document(doc)) return false;

        /* trophy_xml_document::GetRoot: the document root, or its first child
         * when that child is named "trophyconf"
         * (refs/.../Loader/TROPUSR.cpp:12-32). */
        const xml::Node* root = &doc;
        if (root->name != "trophyconf") {
            const xml::Node* found = root->child("trophyconf");
            if (!found) return false;
            root = found;
        }

        if (const xml::Node* n = root->child("title-name")) title_name = n->text;
        if (const xml::Node* n = root->child("title-detail")) title_description = n->text;

        trophies.clear();
        for (const auto& child : root->children) {
            if (child.name != "trophy") continue;
            TrophyEntry t;
            t.id = static_cast<int32_t>(std::strtol(child.attr("id").c_str(), nullptr, 10));
            t.pid = static_cast<int32_t>(std::strtol(child.attr("pid").c_str(), nullptr, 10));
            const std::string ttype = child.attr("ttype");
            t.type = ttype.empty() ? '\0' : ttype[0];
            const std::string hidden = child.attr("hidden");
            t.hidden = !hidden.empty() && hidden[0] == 'y';
            if (const xml::Node* n = child.child("name")) t.name = n->text;
            if (const xml::Node* n = child.child("detail")) t.detail = n->text;
            trophies.push_back(std::move(t));
        }
        return true;
    }

    /* The grade RPCS3 derives from the first character of ttype
     * (refs/.../Loader/TROPUSR.cpp:197-205). */
    static uint32_t grade_of(char type) {
        switch (type) {
        case 'P': return GRADE_PLATINUM;
        case 'G': return GRADE_GOLD;
        case 'S': return GRADE_SILVER;
        case 'B': return GRADE_BRONZE;
        default: return GRADE_UNKNOWN;
        }
    }
};

/* --- TROPUSR.DAT ---------------------------------------------------------
 * refs/.../Loader/TROPUSR.h:6-58. Sizes are asserted because Generate writes
 * them as the entry_size field and Load reads whole structs. */
struct TropusrHeader {
    uint32_t magic;
    uint32_t unk1;
    uint32_t tables_count;
    uint32_t unk2;
    uint8_t reserved[32];
};
static_assert(sizeof(TropusrHeader) == 0x30, "TROPUSRHeader is 48 bytes");

struct TropusrTableHeader {
    uint32_t type;
    uint32_t entries_size;
    uint32_t unk1;
    uint32_t entries_count;
    uint64_t offset;
    uint64_t reserved;
};
static_assert(sizeof(TropusrTableHeader) == 0x20, "TROPUSRTableHeader is 32 bytes");

struct TropusrEntry4 {
    uint32_t entry_type;
    uint32_t entry_size;
    uint32_t entry_id;
    uint32_t entry_unk1;
    uint32_t trophy_id;
    uint32_t trophy_grade;
    uint32_t trophy_pid;
    uint8_t unk6[68];
};
static_assert(sizeof(TropusrEntry4) == 0x60, "TROPUSREntry4 is 96 bytes");

struct TropusrEntry6 {
    uint32_t entry_type;
    uint32_t entry_size;
    uint32_t entry_id;
    uint32_t entry_unk1;
    uint32_t trophy_id;
    uint32_t trophy_state;
    uint32_t unk4;
    uint32_t unk5;
    uint64_t timestamp1;
    uint64_t timestamp2;
    uint8_t unk6[64];
};
static_assert(sizeof(TropusrEntry6) == 0x70, "TROPUSREntry6 is 112 bytes");

constexpr uint32_t TROPUSR_MAGIC = 0x818F54AD;
constexpr uint32_t TROPUSR_ENTRY4_SIZE = sizeof(TropusrEntry4) - 0x10;   /* 0x50 */
constexpr uint32_t TROPUSR_ENTRY6_SIZE = sizeof(TropusrEntry6) - 0x10;   /* 0x60 */

class Tropusr {
public:
    /* refs/.../Loader/TROPUSR.cpp:34-143: header, table headers, tables. Any
     * short or inconsistent read is a failure, and the caller regenerates. */
    bool load(const std::vector<uint8_t>& data) {
        header_ = TropusrHeader{};
        table4_.clear();
        table6_.clear();
        if (data.size() < sizeof(TropusrHeader)) return false;

        header_.magic = be32(&data[0]);
        header_.unk1 = be32(&data[4]);
        header_.tables_count = be32(&data[8]);
        header_.unk2 = be32(&data[12]);
        std::memcpy(header_.reserved, &data[16], 32);
        if (header_.magic != TROPUSR_MAGIC) return false;
        if (header_.tables_count > 8) return false;

        const uint64_t tables_at = 0x30;
        if (tables_at + static_cast<uint64_t>(header_.tables_count) * sizeof(TropusrTableHeader) > data.size())
            return false;

        for (uint32_t i = 0; i < header_.tables_count; i++) {
            const uint8_t* p = &data[tables_at + static_cast<size_t>(i) * sizeof(TropusrTableHeader)];
            const uint32_t type = be32(p);
            const uint32_t entries_size = be32(p + 4);
            const uint32_t count = be32(p + 12);
            const uint64_t offset = be64(p + 16);
            (void)entries_size;

            if (type == 4u) {
                const uint64_t need = offset + static_cast<uint64_t>(count) * sizeof(TropusrEntry4);
                if (need > data.size()) return false;
                table4_.resize(count);
                for (uint32_t e = 0; e < count; e++) {
                    const uint8_t* q = &data[offset + static_cast<size_t>(e) * sizeof(TropusrEntry4)];
                    TropusrEntry4& t = table4_[e];
                    t.entry_type = be32(q);
                    t.entry_size = be32(q + 4);
                    t.entry_id = be32(q + 8);
                    t.entry_unk1 = be32(q + 12);
                    t.trophy_id = be32(q + 16);
                    t.trophy_grade = be32(q + 20);
                    t.trophy_pid = be32(q + 24);
                    std::memcpy(t.unk6, q + 28, 68);
                }
            } else if (type == 6u) {
                const uint64_t need = offset + static_cast<uint64_t>(count) * sizeof(TropusrEntry6);
                if (need > data.size()) return false;
                table6_.resize(count);
                for (uint32_t e = 0; e < count; e++) {
                    const uint8_t* q = &data[offset + static_cast<size_t>(e) * sizeof(TropusrEntry6)];
                    TropusrEntry6& t = table6_[e];
                    t.entry_type = be32(q);
                    t.entry_size = be32(q + 4);
                    t.entry_id = be32(q + 8);
                    t.entry_unk1 = be32(q + 12);
                    t.trophy_id = be32(q + 16);
                    t.trophy_state = be32(q + 20);
                    t.unk4 = be32(q + 24);
                    t.unk5 = be32(q + 28);
                    t.timestamp1 = be64(q + 32);
                    t.timestamp2 = be64(q + 40);
                    std::memcpy(t.unk6, q + 48, 64);
                }
            }
        }
        return true;
    }

    /* refs/.../Loader/TROPUSR.cpp:163-232: build both tables from TROPCONF.SFM
     * and write them. */
    bool generate(const TrophyConf& conf) {
        table4_.clear();
        table6_.clear();
        for (const auto& t : conf.trophies) {
            TropusrEntry4 e4{};
            e4.entry_type = 4;
            e4.entry_size = TROPUSR_ENTRY4_SIZE;
            e4.entry_id = static_cast<uint32_t>(table4_.size());
            e4.trophy_id = static_cast<uint32_t>(t.id);
            e4.trophy_grade = TrophyConf::grade_of(t.type);
            e4.trophy_pid = static_cast<uint32_t>(t.pid);
            table4_.push_back(e4);

            TropusrEntry6 e6{};
            e6.entry_type = 6;
            e6.entry_size = TROPUSR_ENTRY6_SIZE;
            e6.entry_id = static_cast<uint32_t>(table6_.size());
            e6.trophy_id = static_cast<uint32_t>(t.id);
            table6_.push_back(e6);
        }

        header_ = TropusrHeader{};
        header_.magic = TROPUSR_MAGIC;
        header_.unk1 = 0x00010000;
        header_.tables_count = 2;
        header_.unk2 = 0;
        return true;
    }

    /* refs/.../Loader/TROPUSR.cpp:146-161, with the offsets recomputed the way
     * Generate sets them (:215-219) so a save after an unlock is consistent. */
    std::vector<uint8_t> save() const {
        std::vector<uint8_t> out;
        out.resize(sizeof(TropusrHeader) + 2 * sizeof(TropusrTableHeader));

        TropusrHeader header = header_;
        header.tables_count = 2;
        put32(out, 0, header.magic);
        put32(out, 4, header.unk1);
        put32(out, 8, header.tables_count);
        put32(out, 12, header.unk2);
        std::memcpy(&out[16], header.reserved, 32);

        const uint64_t offset4 = out.size();
        out.resize(out.size() + table4_.size() * sizeof(TropusrEntry4));
        for (size_t i = 0; i < table4_.size(); i++) {
            uint8_t* q = &out[offset4 + i * sizeof(TropusrEntry4)];
            const TropusrEntry4& t = table4_[i];
            put32(q, 0, t.entry_type);
            put32(q, 4, t.entry_size);
            put32(q, 8, t.entry_id);
            put32(q, 12, t.entry_unk1);
            put32(q, 16, t.trophy_id);
            put32(q, 20, t.trophy_grade);
            put32(q, 24, t.trophy_pid);
            std::memcpy(q + 28, t.unk6, 68);
        }

        const uint64_t offset6 = out.size();
        out.resize(out.size() + table6_.size() * sizeof(TropusrEntry6));
        for (size_t i = 0; i < table6_.size(); i++) {
            uint8_t* q = &out[offset6 + i * sizeof(TropusrEntry6)];
            const TropusrEntry6& t = table6_[i];
            put32(q, 0, t.entry_type);
            put32(q, 4, t.entry_size);
            put32(q, 8, t.entry_id);
            put32(q, 12, t.entry_unk1);
            put32(q, 16, t.trophy_id);
            put32(q, 20, t.trophy_state);
            put32(q, 24, t.unk4);
            put32(q, 28, t.unk5);
            put64(q, 32, t.timestamp1);
            put64(q, 40, t.timestamp2);
            std::memcpy(q + 48, t.unk6, 64);
        }

        uint8_t* h4 = &out[sizeof(TropusrHeader)];
        put32(h4, 0, 4);
        put32(h4, 4, TROPUSR_ENTRY4_SIZE);
        put32(h4, 8, 1);
        put32(h4, 12, static_cast<uint32_t>(table4_.size()));
        put64(h4, 16, offset4);
        put64(h4, 24, 0);

        uint8_t* h6 = &out[sizeof(TropusrHeader) + sizeof(TropusrTableHeader)];
        put32(h6, 0, 6);
        put32(h6, 4, TROPUSR_ENTRY6_SIZE);
        put32(h6, 8, 1);
        put32(h6, 12, static_cast<uint32_t>(table6_.size()));
        put64(h6, 16, offset6);
        put64(h6, 24, 0);
        return out;
    }

    /* refs/.../Loader/TROPUSR.cpp:234-364. */
    uint32_t count() const { return static_cast<uint32_t>(table6_.size()); }
    uint32_t unlocked_count() const {
        uint32_t n = 0;
        for (const auto& t : table6_) if (t.trophy_state) n++;
        return n;
    }
    uint32_t grade(uint32_t id) const { return id < table4_.size() ? table4_[id].trophy_grade : GRADE_UNKNOWN; }
    uint32_t unlock_state(uint32_t id) const { return id < table6_.size() ? table6_[id].trophy_state : 0; }
    uint64_t timestamp(uint32_t id) const { return id < table6_.size() ? table6_[id].timestamp2 : 0; }

    /* refs/.../Loader/TROPUSR.cpp:366-379. */
    bool unlock(uint32_t id, uint64_t timestamp1, uint64_t timestamp2) {
        if (id >= table6_.size()) return false;
        table6_[id].trophy_state = 1;
        table6_[id].timestamp1 = timestamp1;
        table6_[id].timestamp2 = timestamp2;
        return true;
    }

    /* refs/.../Loader/TROPUSR.cpp:252-330. The pid is refreshed from the config
     * first ("for backwards compatibility", :301-308), then the platinum is
     * unlocked only when every trophy pointing at it is unlocked. */
    uint32_t unlocked_platinum_id(uint32_t trophy_id, const TrophyConf& conf) {
        if (trophy_id >= table6_.size() || trophy_id >= table4_.size()) return INVALID_TROPHY_ID;
        if (table6_.size() != table4_.size()) return INVALID_TROPHY_ID;

        for (const auto& t : conf.trophies) {
            const uint32_t id = static_cast<uint32_t>(t.id);
            if (id < table4_.size() && table4_[id].trophy_id == id)
                table4_[id].trophy_pid = static_cast<uint32_t>(t.pid);
        }

        const uint32_t pid = table4_[trophy_id].trophy_pid;
        if (pid == INVALID_TROPHY_ID || unlock_state(pid)) return INVALID_TROPHY_ID;

        for (size_t i = 0; i < table4_.size(); i++) {
            if (table4_[i].trophy_pid == pid && !table6_[i].trophy_state) return INVALID_TROPHY_ID;
        }
        return pid;
    }

    /* Test access; the handlers never touch the raw tables. */
    const std::vector<TropusrEntry4>& table4() const { return table4_; }
    const std::vector<TropusrEntry6>& table6() const { return table6_; }
    const TropusrHeader& header() const { return header_; }

private:
    static void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { put32(&b[at], 0, v); }
    static void put32(uint8_t* p, size_t at, uint32_t v) {
        p[at + 0] = static_cast<uint8_t>(v >> 24);
        p[at + 1] = static_cast<uint8_t>(v >> 16);
        p[at + 2] = static_cast<uint8_t>(v >> 8);
        p[at + 3] = static_cast<uint8_t>(v);
    }
    static void put64(uint8_t* p, size_t at, uint64_t v) {
        put32(p, at, static_cast<uint32_t>(v >> 32));
        put32(p, at + 4, static_cast<uint32_t>(v));
    }

    TropusrHeader header_{};
    std::vector<TropusrEntry4> table4_;
    std::vector<TropusrEntry6> table6_;
};

/* --- the module's state --------------------------------------------------
 * One context and one handle pool, ids 1..4 each, exactly RPCS3's id pools
 * (refs/.../sceNpTrophy.cpp:39-41 and :92-94: id_base 1, id_step 1, id_count 4).
 * Per-handle and per-context state, never one global "current trophy". */
constexpr uint32_t ID_BASE = 1;
constexpr uint32_t ID_COUNT = 4;

struct Context {
    bool used = false;
    std::string trp_name;
    bool read_only = false;
    bool registered = false;
    Tropusr tropusr;
};

struct Handle {
    bool used = false;
    bool aborted = false;
};

class Manager {
public:
    /* refs/.../sceNpTrophy.cpp:290-311. */
    int32_t init(uint64_t options) {
        if (initialized_) return ERR_ALREADY_INITIALIZED;
        if (options > 0) return ERR_NOT_SUPPORTED;
        initialized_ = true;
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:313-332. */
    int32_t term() {
        if (!initialized_) return ERR_NOT_INITIALIZED;
        for (auto& c : contexts_) c = Context{};
        for (auto& h : handles_) h = Handle{};
        initialized_ = false;
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:334-361. */
    int32_t create_handle(uint32_t& out) {
        if (!initialized_) return ERR_NOT_INITIALIZED;
        for (uint32_t i = 0; i < ID_COUNT; i++) {
            if (!handles_[i].used) {
                handles_[i] = Handle{};
                handles_[i].used = true;
                out = ID_BASE + i;
                return 0;
            }
        }
        return ERR_EXCEEDS_MAX;
    }

    /* refs/.../sceNpTrophy.cpp:363-389: the init check is commented out there,
     * so this answers without one. */
    int32_t destroy_handle(uint32_t handle) {
        if (handle < ID_BASE || handle >= ID_BASE + ID_COUNT) return ERR_INVALID_ARGUMENT;
        Handle& h = handles_[handle - ID_BASE];
        if (!h.used) return ERR_UNKNOWN_HANDLE;
        h = Handle{};
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:397-428. */
    int32_t abort_handle(uint32_t handle) {
        if (handle < ID_BASE || handle >= ID_BASE + ID_COUNT) return ERR_INVALID_ARGUMENT;
        Handle& h = handles_[handle - ID_BASE];
        if (!h.used) return ERR_UNKNOWN_HANDLE;
        h.aborted = true;
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:430-526. The order of the checks is RPCS3's and
     * is visible to the caller: a null commSign is INVALID_ARGUMENT even when
     * the module was never initialised. */
    int32_t create_context(uint32_t& out, const CommId& comm_id, const Signature& signature,
                           uint64_t options) {
        if (!initialized_) return ERR_NOT_INITIALIZED;
        if (options > OPTIONS_CREATE_CONTEXT_READ_ONLY) return ERR_NOT_SUPPORTED;

        const int32_t num = static_cast<int32_t>(comm_id.num);
        if (num > 99) return ERR_INVALID_NP_COMM_ID;

        /* TRP.cpp-independent checks, straight from sceNpTrophy.cpp:483-498:
         * the magic at data[0] as be32, six zero padding bytes from data[6],
         * and the version 0x100 at data[4] as be16. */
        if (be32(signature.data) != COMM_SIGN_MAGIC) return ERR_INVALID_NP_COMM_ID;
        for (int i = 6; i < 12; i++) {
            if (signature.data[i] != 0) return ERR_INVALID_NP_COMM_ID;
        }
        if (be16(signature.data + 4) != 0x100) return ERR_INVALID_NP_COMM_ID;

        uint32_t slot = ID_COUNT;
        for (uint32_t i = 0; i < ID_COUNT; i++) {
            if (!contexts_[i].used) { slot = i; break; }
        }
        if (slot == ID_COUNT) return ERR_EXCEEDS_MAX;

        /* Name: up to 9 bytes of data, cut at the first NUL, plus "_%02d" of the
         * communication number (sceNpTrophy.cpp:473-501). */
        std::string base(comm_id.data, 9);
        const size_t nul = base.find('\0');
        if (nul != std::string::npos) base.resize(nul);
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "_%02d", num);

        Context& c = contexts_[slot];
        c = Context{};
        c.used = true;
        c.trp_name = base + suffix;
        c.read_only = (options & OPTIONS_CREATE_CONTEXT_READ_ONLY) != 0;
        out = ID_BASE + slot;
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:528-553. */
    int32_t destroy_context(uint32_t context) {
        if (!initialized_) return ERR_NOT_INITIALIZED;
        if (context < ID_BASE || context >= ID_BASE + ID_COUNT) return ERR_INVALID_ARGUMENT;
        Context& c = contexts_[context - ID_BASE];
        if (!c.used) return ERR_UNKNOWN_CONTEXT;
        c = Context{};
        return 0;
    }

    /* refs/.../sceNpTrophy.cpp:118-166, including the read-only refusal a
     * write call gets (ERR_INVALID_CONTEXT, not INVALID_ARGUMENT). */
    int32_t get_context_ex(uint32_t context, uint32_t handle, bool test_writeable, Context** out) {
        if (out) *out = nullptr;
        if (context < ID_BASE || context >= ID_BASE + ID_COUNT) return ERR_INVALID_ARGUMENT;
        Context& c = contexts_[context - ID_BASE];
        if (!c.used) return ERR_UNKNOWN_CONTEXT;
        if (test_writeable && c.read_only) return ERR_INVALID_CONTEXT;

        if (handle < ID_BASE || handle >= ID_BASE + ID_COUNT) return ERR_INVALID_ARGUMENT;
        const Handle& h = handles_[handle - ID_BASE];
        if (!h.used) return ERR_UNKNOWN_HANDLE;
        if (h.aborted) return ERR_ABORT;

        if (out) *out = &c;
        return 0;
    }

    Context* context(uint32_t id) {
        if (id < ID_BASE || id >= ID_BASE + ID_COUNT) return nullptr;
        Context& c = contexts_[id - ID_BASE];
        return c.used ? &c : nullptr;
    }

    bool initialized() const { return initialized_; }
    void reset() {
        initialized_ = false;
        for (auto& c : contexts_) c = Context{};
        for (auto& h : handles_) h = Handle{};
    }

private:
    bool initialized_ = false;
    Context contexts_[ID_COUNT];
    Handle handles_[ID_COUNT];
};

/* --- the decisions the imported functions make ---------------------------
 * These are the branches of RPCS3's bodies, separated from file and guest
 * memory so a test can drive them with tables built in memory. */

/* refs/.../sceNpTrophy.cpp:769-775: every (status, completed) pair the register
 * callback receives, in order. RPCS3 sends them through the sysutil callback
 * queue, one status per cellSysutilCheckCallback call; the pacing it adds
 * (:807-830) exists to imitate VSH's cadence and is not part of the sequence. */
struct RegisterStatus {
    uint32_t status;
    int32_t completed;
    int32_t total;
};

inline std::vector<RegisterStatus> register_statuses(uint32_t trophies_count) {
    const int32_t progress = static_cast<int32_t>(trophies_count) - 1;
    const int32_t finalize = (progress > 9 ? progress : 9) - 5;
    const std::pair<uint32_t, int32_t> statuses[] = {
        {STATUS_PROCESSING_SETUP, 3},
        {STATUS_PROCESSING_PROGRESS, progress},
        {STATUS_PROCESSING_FINALIZE, finalize},
        {STATUS_PROCESSING_COMPLETE, 0},
    };
    std::vector<RegisterStatus> out;
    for (const auto& s : statuses) {
        for (int32_t completed = 0; completed <= s.second; completed++)
            out.push_back({s.first, completed, s.second});
    }
    return out;
}

/* refs/.../sceNpTrophy.cpp:1007-1057. The caller has already zeroed both. */
inline void game_info(const Tropusr& tropusr, const TrophyConf& conf, GameDetails* details, GameData* data) {
    for (const auto& t : conf.trophies) {
        if (details) {
            details->numTrophies++;
            switch (t.type) {
            case 'B': details->numBronze++; break;
            case 'S': details->numSilver++; break;
            case 'G': details->numGold++; break;
            case 'P': details->numPlatinum++; break;
            default: break;
            }
        }
        if (data && tropusr.unlock_state(static_cast<uint32_t>(t.id))) {
            data->unlockedTrophies++;
            switch (t.type) {
            case 'B': data->unlockedBronze++; break;
            case 'S': data->unlockedSilver++; break;
            case 'G': data->unlockedGold++; break;
            case 'P': data->unlockedPlatinum++; break;
            default: break;
            }
        }
    }
}

/* refs/.../sceNpTrophy.cpp:1259-1370 (NpTrophyGetTrophyInfo). The file was
 * already read and parsed; this is every branch after that. */
inline int32_t trophy_info(const Tropusr& tropusr, const TrophyConf& conf, int32_t trophy_id,
                           TrophyDetails* details, TrophyData* data) {
    if (!details && !data) return ERR_INVALID_ARGUMENT;
    if (!tropusr.count()) return ERR_CONTEXT_NOT_REGISTERED;

    const TrophyEntry* found = nullptr;
    for (const auto& t : conf.trophies) {
        if (t.id == trophy_id) { found = &t; break; }
    }
    if (!found) return ERR_INVALID_TROPHY_ID;

    const bool unlocked = tropusr.unlock_state(static_cast<uint32_t>(trophy_id)) != 0;
    if (found->hidden && !unlocked) return ERR_HIDDEN;

    if (details) {
        TrophyDetails d{};
        d.trophyId = trophy_id;
        d.hidden = found->hidden ? 1 : 0;
        switch (found->type) {
        case 'B': d.trophyGrade = GRADE_BRONZE; break;
        case 'S': d.trophyGrade = GRADE_SILVER; break;
        case 'G': d.trophyGrade = GRADE_GOLD; break;
        case 'P': d.trophyGrade = GRADE_PLATINUM; break;
        default: break;
        }
        copy_trunc(d.name, sizeof(d.name), found->name);
        copy_trunc(d.description, sizeof(d.description), found->detail);
        *details = d;
    }
    if (data) {
        TrophyData d{};
        d.trophyId = trophy_id;
        d.unlocked = unlocked ? 1 : 0;
        d.timestamp = tropusr.timestamp(static_cast<uint32_t>(trophy_id));
        *data = d;
    }
    return 0;
}

/* refs/.../sceNpTrophy.cpp:1088-1138, after the context and the trophy id have
 * been validated. Returns CELL_OK even when the save later fails (RPCS3 logs
 * that, :1141-1144). */
inline int32_t unlock_trophy(Tropusr& tropusr, const TrophyConf& conf, int32_t trophy_id,
                             uint64_t tick, uint32_t* platinum_id) {
    if (!tropusr.count()) return ERR_CONTEXT_NOT_REGISTERED;
    if (trophy_id < 0 || static_cast<uint32_t>(trophy_id) >= tropusr.count())
        return ERR_INVALID_TROPHY_ID;
    if (tropusr.grade(static_cast<uint32_t>(trophy_id)) == GRADE_PLATINUM)
        return ERR_CANNOT_UNLOCK_PLATINUM;
    if (tropusr.unlock_state(static_cast<uint32_t>(trophy_id)))
        return ERR_ALREADY_UNLOCKED;

    tropusr.unlock(static_cast<uint32_t>(trophy_id), tick, tick);

    const uint32_t platinum = tropusr.unlocked_platinum_id(static_cast<uint32_t>(trophy_id), conf);
    if (platinum != INVALID_TROPHY_ID) tropusr.unlock(platinum, tick, tick);

    if (platinum_id) *platinum_id = platinum;
    return 0;
}

}  // namespace trophy
}  // namespace hle

#endif  // PS3BASE_HLE_NPTROPHY_H
