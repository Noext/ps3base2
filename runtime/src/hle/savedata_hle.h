/* ps3base -- the parts of cellSaveData that a host-side test can drive.
 *
 * cellSaveDataListDelete is claimed in this milestone's brief to be a no-op in
 * RPCS3. It is NOT: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp:2634-2639
 * hands the call to savedata_op(SAVEDATA_OP_LIST_DELETE, ...), which enumerates
 * the account's saves, calls funcList, and on a selected entry runs delete_save
 * (cellSaveData.cpp:1206-1258) and funcDone. The enumeration, the sort, the
 * prefix rules and the filename filter are all either real behaviour or wrong
 * answers, so they live here and are tested.
 *
 * Constants and structures:
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:8-24    errors
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:75-108  sizes and modes
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:161-166  sort types
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:175-247  structures
 *   refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp:479-714  arg checks
 */
#ifndef PS3BASE_HLE_SAVEDATA_H
#define PS3BASE_HLE_SAVEDATA_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace hle {
namespace savedata {

using Instance = void (*)(int*);

/* refs/.../cellSaveData.h:8-24. */
constexpr int32_t ERR_CBRESULT = static_cast<int32_t>(0x8002b401);
constexpr int32_t ERR_ACCESS = static_cast<int32_t>(0x8002b402);
constexpr int32_t ERR_INTERNAL = static_cast<int32_t>(0x8002b403);
constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002b404);
constexpr int32_t ERR_NOSPACE = static_cast<int32_t>(0x8002b405);
constexpr int32_t ERR_BROKEN = static_cast<int32_t>(0x8002b406);
constexpr int32_t ERR_FAILURE = static_cast<int32_t>(0x8002b407);
constexpr int32_t ERR_BUSY = static_cast<int32_t>(0x8002b408);
constexpr int32_t ERR_NOUSER = static_cast<int32_t>(0x8002b409);
constexpr int32_t ERR_SIZEOVER = static_cast<int32_t>(0x8002b40a);
constexpr int32_t ERR_NODATA = static_cast<int32_t>(0x8002b40b);

/* Callback results, refs/.../cellSaveData.h:26-38. */
constexpr int32_t CBRESULT_OK_LAST_NOCONFIRM = 2;
constexpr int32_t CBRESULT_OK_LAST = 1;
constexpr int32_t CBRESULT_OK_NEXT = 0;

/* refs/.../cellSaveData.h:84-93. */
constexpr uint32_t DIRLIST_MAX = 2048;
constexpr uint32_t DIRNAME_SIZE = 32;
constexpr uint32_t FILENAME_SIZE = 13;
constexpr uint32_t PREFIX_SIZE = 256;
constexpr uint32_t LISTITEM_MAX = 2048;
constexpr uint32_t DISABLE_TROPHY_OWNERSHIP_CHECK = 1u << 16;

/* Structure sizes asserted by RPCS3, refs/.../cellSaveData.cpp:600-640
 * (CHECK_SIZE): CellSaveDataSetList 16, CellSaveDataDirList 48,
 * CellSaveDataFileStat 56, CellSaveDataListGet 0x58. */
constexpr uint32_t SET_LIST_SIZE = 16;
constexpr uint32_t DIR_LIST_SIZE = 48;
constexpr uint32_t FILE_STAT_SIZE = 56;
constexpr uint32_t LIST_GET_SIZE = 0x58;

/* refs/.../cellSaveData.h:114-120. */
constexpr uint32_t SORTTYPE_MODIFIEDTIME = 0;
constexpr uint32_t SORTTYPE_SUBTITLE = 1;
constexpr uint32_t SORTORDER_DESCENT = 0;
constexpr uint32_t SORTORDER_ASCENT = 1;

/* The two files firmware hides from every listing, and only those:
 * refs/.../cellSaveData.cpp:1600-1650 (check_filename with both flags false
 * excludes PARAM.SFO and PARAM.PFD). */
inline bool is_system_file(const std::string& name) {
    return name == "PARAM.SFO" || name == "PARAM.PFD";
}

/* refs/.../cellSaveData.cpp:218-226 (is_valid_dir_name) on top of
 * refs/.../cellSysutil.cpp:452-509 (sysutil_check_name_string): length 1..31,
 * characters A-Z, 0-9, '-' and '_', and it may not start with '_'. */
inline bool is_valid_dir_name(const std::string& s) {
    if (s.empty() || s.size() > DIRNAME_SIZE - 1) return false;
    if (s[0] == '_') return false;
    for (char c : s) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return true;
}

/* A save entry as the list needs it. Fields: refs/.../cellSaveData.h:338-357
 * (SaveDataEntry). */
struct Entry {
    std::string dir_name;   /* SAVEDATA_DIRECTORY from the entry's PARAM.SFO */
    std::string escaped;    /* the directory name on disk */
    std::string list_param; /* SAVEDATA_LIST_PARAM */
    std::string title;
    std::string subtitle;
    uint64_t size = 0;
    int64_t mtime = 0;
    bool has_sfo = false;
};

/* The listing filter of refs/.../cellSaveData.cpp:855-935:
 *   - only directories whose name is a valid dir name;
 *   - a name matches if it STARTS WITH any prefix in the list; prefixes are
 *     '|'-separated; an empty prefix list means "everything";
 *   - for SAVEDATA_OP_LIST_DELETE only, a single prefix of "*" means "every
 *     savedata of this title", and RPCS3 rewrites it to the title id
 *     (cellSaveData.cpp:830-836). An asterisk is NOT special for any other
 *     operation, which is why it is a parameter here and not a constant. */
inline std::vector<std::string> split_prefixes(const std::string& raw) {
    std::vector<std::string> out;
    if (raw.empty()) return out;
    size_t pos = 0;
    while (pos <= raw.size()) {
        const size_t bar = raw.find('|', pos);
        const size_t end = bar == std::string::npos ? raw.size() : bar;
        out.push_back(raw.substr(pos, end - pos));
        if (bar == std::string::npos) break;
        pos = bar + 1;
    }
    return out;
}

/* Returns the effective prefix list, applying the LIST_DELETE asterisk rule. */
inline std::vector<std::string> effective_prefixes(const std::string& raw, bool allow_asterisk,
                                                   const std::string& title_id) {
    std::vector<std::string> list = split_prefixes(raw);
    if (allow_asterisk && list.size() == 1 && list.front() == "*") list.front() = title_id;
    return list;
}

inline bool name_matches(const std::string& name, const std::vector<std::string>& prefixes) {
    if (prefixes.empty()) return true;   /* an empty prefix list lists everything */
    for (const std::string& p : prefixes) {
        if (name.size() >= p.size() && name.compare(0, p.size(), p) == 0) return true;
    }
    return false;
}

/* The sort of refs/.../cellSaveData.cpp:937-985: by mtime, tie-broken by
 * subtitle; or by subtitle, tie-broken by mtime; the order flips for DESCENT. */
inline void sort_entries(std::vector<Entry>& entries, uint32_t sort_type, uint32_t sort_order) {
    auto less = [sort_type](const Entry& a, const Entry& b) {
        if (sort_type == SORTTYPE_SUBTITLE) {
            if (a.subtitle == b.subtitle) return a.mtime < b.mtime;
            return a.subtitle < b.subtitle;
        }
        if (a.mtime == b.mtime) return a.subtitle < b.subtitle;
        return a.mtime < b.mtime;
    };
    if (sort_order == SORTORDER_ASCENT) {
        std::stable_sort(entries.begin(), entries.end(), less);
    } else {
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) { return less(b, a); });
    }
}

/* How many entries a call reports and how many it can actually place.
 * refs/.../cellSaveData.cpp:862-885: dirNum counts every match, dirListNum
 * counts only those that fit under dirListMax, and the caller's buffer is
 * cleared exactly once, on the first match. */
struct Listing {
    uint32_t dir_num = 0;
    uint32_t dir_list_num = 0;
    bool clear_buffer = false;
};

inline Listing count_listing(const std::vector<Entry>& sorted, uint32_t dir_list_max) {
    Listing l;
    l.dir_num = static_cast<uint32_t>(sorted.size());
    l.dir_list_num = std::min(l.dir_num, dir_list_max);
    l.clear_buffer = l.dir_num != 0;
    return l;
}

/* The argument checks of refs/.../cellSaveData.cpp:479-714 that apply to the
 * delete-list call, in RPCS3's order. Returns 0 when the arguments pass, or the
 * numbered reason RPCS3 reports in its message. Kept as numbers because the
 * "sysutil savedata parameter error : N" text is what a caller greps for. */
struct ListArgs {
    uint32_t version = 0;
    bool has_set_list = false;
    uint32_t sort_type = 0;
    uint32_t sort_order = 0;
    bool has_prefix = false;
    bool prefix_terminated = false;
    bool prefix_length_ok = true;
    uint32_t dir_list_max = 0;
    uint32_t buf_size = 0;
    bool has_buf = false;
    uint32_t file_list_max = 1;   /* LIST_DELETE requires exactly 0 */
    bool has_func_list = false;
    bool has_func_done = false;
};

constexpr uint32_t VERSION_CURRENT = 1;   /* CELL_SAVEDATA_VERSION_420 */

enum class Op { ListDelete, ListSave, ListLoad };

inline uint32_t check_list_args(const ListArgs& a, Op op) {
    if (a.version > VERSION_CURRENT) return 1;
    if (!a.has_set_list) return 11;
    if (a.sort_type > SORTTYPE_SUBTITLE) return 12;
    if (a.sort_order > SORTORDER_ASCENT) return 13;
    if (!a.has_prefix) return 15;
    if (!a.prefix_terminated || !a.prefix_length_ok) return 17;

    const bool is_delete = op == Op::ListDelete;
    if (is_delete && !a.has_func_done) return 137;
    if (!is_delete && !a.has_func_list) return 18;
    if (is_delete && !a.has_func_list) return 18;

    /* dirListMax * 48 must fit the caller's buffer (cellSaveData.cpp:629-640). */
    if (a.dir_list_max > DIRLIST_MAX) return 8;
    if (uint64_t(a.dir_list_max) * DIR_LIST_SIZE > a.buf_size) return 7;
    if (a.buf_size && !a.has_buf) return 6;
    /* A delete list carries no file list at all (cellSaveData.cpp:648-653). */
    if (is_delete && a.file_list_max != 0) return 9;
    return 0;
}

/* The CellSaveDataDoneGet the done callback receives, refs/.../cellSaveData.h:320-327:
 *   be s32 excResult; char dirName[32]; be s32 sizeKB; be s32 hddFreeSizeKB;
 *   char reserved[64]; */
struct DoneGet {
    int32_t exc_result = 0;
    std::string dir_name;
    int32_t size_kb = 0;
    int32_t hdd_free_size_kb = 40 * 1024 * 1024 - 256;
};

/* Size of a save, in KiB, as delete_save computes it: every non-directory
 * entry, each rounded up to a whole KiB, summed, then divided
 * (refs/.../cellSaveData.cpp:1222-1232). */
inline int32_t size_kb_of(const std::vector<uint64_t>& file_sizes) {
    uint64_t bytes = 0;
    for (uint64_t s : file_sizes) bytes += (s + 1023) & ~uint64_t(1023);
    return static_cast<int32_t>(bytes / 1024);
}

/* delete_save also reports NODATA when the directory was not there
 * (cellSaveData.cpp:1244-1247). */
inline DoneGet done_of(bool had_directory, const std::string& dir_name,
                       const std::vector<uint64_t>& file_sizes) {
    DoneGet d;
    d.dir_name = dir_name;
    if (!had_directory) {
        d.exc_result = ERR_NODATA;
        return d;
    }
    d.exc_result = 0;
    d.size_kb = size_kb_of(file_sizes);
    return d;
}

}  // namespace savedata
}  // namespace hle

#endif
