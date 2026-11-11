/* ps3base -- cellSaveData's list/delete rules, driven through the SAME header
 * the import uses (runtime/src/hle/savedata_hle.h).
 *
 * The brief for this milestone says RPCS3 implements cellSaveDataListDelete as
 * a no-op returning CELL_OK, and asks for the line that proves it. There is no
 * such line: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp:2634-2639 calls
 * savedata_op(SAVEDATA_OP_LIST_DELETE, ...), and :1206-1258 is the delete_save
 * lambda that removes a directory. So the rules below are real firmware rules,
 * and each one was missing from this tree before:
 *
 *   1. a delete list must carry fileListMax == 0 (savedata_check_args, :648-653);
 *   2. it must have a funcDone callback (:682-687);
 *   3. the dirListMax * 48 <= bufSize check, and "bufSize set but buf NULL" (:629-645);
 *   4. the asterisk prefix rule applies to LIST_DELETE ONLY (:830-836);
 *   5. only entries with a valid dir name and a readable PARAM.SFO are listed (:855-899);
 *   6. dirNum counts every match while dirListNum counts only what fits (:862-885);
 *   7. the sort is mtime-or-subtitle with the opposite field as tie-break, and
 *      DESCENT reverses it (:937-985);
 *   8. a deleted save's size rounds each file UP to a KiB before summing (:1222-1232);
 *   9. a save directory that is gone reports NODATA in the done structure (:1244-1247).
 *
 * No game data, no GPU, no network, no PPU runtime.
 */
#include "hle/savedata_hle.h"

#include <cstdio>
#include <string>
#include <vector>

namespace sd = hle::savedata;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

namespace {

sd::Entry entry(const std::string& dir, const std::string& sub, int64_t mtime) {
    sd::Entry e;
    e.dir_name = dir;
    e.escaped = dir;
    e.subtitle = sub;
    e.mtime = mtime;
    e.has_sfo = true;
    return e;
}

sd::Entry entry_ok(const std::string& dir, const std::string& sub, int64_t mtime) {
    sd::Entry e;
    e.dir_name = dir;
    e.escaped = dir;
    e.subtitle = sub;
    e.mtime = mtime;
    e.has_sfo = true;
    return e;
}

}  // namespace

int main() {
    /* --- 1. the structure sizes RPCS3 asserts (CHECK_SIZE, :600-640) ----- */
    CHECK(sd::SET_LIST_SIZE == 16, "CellSaveDataSetList is 16 bytes");
    CHECK(sd::DIR_LIST_SIZE == 48, "CellSaveDataDirList is 48 bytes (dirName 32 + listParam 8 + reserved 8)");
    CHECK(sd::FILE_STAT_SIZE == 56, "CellSaveDataFileStat is 56 bytes");
    CHECK(sd::DIRNAME_SIZE == 32, "CELL_SAVEDATA_DIRNAME_SIZE is 32");
    CHECK(sd::FILENAME_SIZE == 13, "CELL_SAVEDATA_FILENAME_SIZE is 13");

    /* The error codes, refs/.../cellSaveData.h:8-24. */
    CHECK(sd::ERR_PARAM == static_cast<int32_t>(0x8002b404), "CELL_SAVEDATA_ERROR_PARAM is 0x8002b404");
    CHECK(sd::ERR_NODATA == static_cast<int32_t>(0x8002b40b), "CELL_SAVEDATA_ERROR_NODATA is 0x8002b40b");
    CHECK(sd::CBRESULT_OK_NEXT == 0 && sd::CBRESULT_OK_LAST == 1 && sd::CBRESULT_OK_LAST_NOCONFIRM == 2,
          "the three OK callback results are 0, 1, 2");

    /* --- 2. the system files, and only those, are hidden (:1600-1650) --- */
    CHECK(sd::is_system_file("PARAM.SFO"), "PARAM.SFO is a system file");
    CHECK(sd::is_system_file("PARAM.PFD"), "PARAM.PFD is a system file");
    CHECK(!sd::is_system_file("ICON0.PNG"), "ICON0.PNG is listed, not hidden");
    CHECK(!sd::is_system_file("GAMEDATA"), "GAMEDATA is listed, not hidden");
    CHECK(!sd::is_system_file("METADATA"), "METADATA is listed, not hidden");

    /* --- 3. directory-name validity (cellSaveData.cpp:218-226 on top of
     * cellSysutil.cpp:452-509) ------------------------------------------ */
    CHECK(sd::is_valid_dir_name("BLES00680-SAVE1"), "letters, digits and '-' are valid");
    CHECK(sd::is_valid_dir_name("BLES00680_SAVE_1"), "underscore inside a name is valid");
    CHECK(!sd::is_valid_dir_name(""), "an empty name is invalid");
    CHECK(!sd::is_valid_dir_name("_HIDDEN"), "a leading underscore is invalid");
    CHECK(!sd::is_valid_dir_name("lower"), "a lowercase letter is invalid");
    CHECK(!sd::is_valid_dir_name("HAS SPACE"), "a space is invalid");
    CHECK(!sd::is_valid_dir_name("HAS/SLASH"), "a slash is invalid");
    CHECK(!sd::is_valid_dir_name(std::string(33, 'A')), "a name of 33 bytes is invalid");
    CHECK(sd::is_valid_dir_name(std::string(31, 'A')), "a name of 31 bytes is valid");

    /* --- 4. prefixes (:830-836, :855-885) ------------------------------- */
    CHECK(sd::split_prefixes("").empty(), "an empty prefix string yields no prefixes");
    {
        const std::vector<std::string> p = sd::split_prefixes("AAA|BBB");
        CHECK(p.size() == 2 && p[0] == "AAA" && p[1] == "BBB", "'AAA|BBB' splits into two prefixes");
    }
    CHECK(sd::split_prefixes("AAA|").size() == 2, "a trailing bar leaves an empty second prefix");

    /* The asterisk rule is for LIST_DELETE only. */
    {
        const std::vector<std::string> del = sd::effective_prefixes("*", true, "BLES00680");
        CHECK(del.size() == 1 && del[0] == "BLES00680",
              "for LIST_DELETE, a lone '*' becomes the title id");
        const std::vector<std::string> save = sd::effective_prefixes("*", false, "BLES00680");
        CHECK(save.size() == 1 && save[0] == "*",
              "for any other operation, '*' stays a literal prefix and matches nothing");
    }
    {
        const std::vector<std::string> p = sd::effective_prefixes("BLES00680", true, "IGNORED");
        CHECK(p.size() == 1 && p[0] == "BLES00680", "a real prefix is left alone");
    }

    CHECK(sd::name_matches("BLES00680-SAVE1", sd::split_prefixes("BLES00680")),
          "a name matches a prefix it starts with");
    CHECK(!sd::name_matches("OTHER-SAVE1", sd::split_prefixes("BLES00680")),
          "a name does not match an unrelated prefix");
    CHECK(sd::name_matches("ANYTHING", {}), "an empty prefix list matches everything");
    CHECK(!sd::name_matches("SHORTER", sd::split_prefixes("LONGER-PREFIX")),
          "a prefix longer than the name does not match");

    /* --- 5. the listing counts (:862-885) ------------------------------- */
    {
        std::vector<sd::Entry> e{entry_ok("A", "", 1), entry_ok("B", "", 2), entry_ok("C", "", 3)};
        const sd::Listing l = sd::count_listing(e, 2);
        CHECK(l.dir_num == 3, "dirNum counts EVERY match");
        CHECK(l.dir_list_num == 2, "dirListNum counts only those under dirListMax");
        CHECK(l.clear_buffer, "a non-empty listing clears the caller's buffer");

        const sd::Listing none = sd::count_listing({}, 8);
        CHECK(none.dir_num == 0 && none.dir_list_num == 0, "an empty listing reports zero");
        CHECK(!none.clear_buffer, "an empty listing leaves the caller's buffer untouched");

        const sd::Listing zero = sd::count_listing(e, 0);
        CHECK(zero.dir_num == 3 && zero.dir_list_num == 0,
              "dirListMax 0 still reports the total but lists nothing");
    }

    /* --- 6. the sort (:937-985) ----------------------------------------- */
    {
        std::vector<sd::Entry> e{entry_ok("C", "beta", 30), entry_ok("A", "alpha", 10),
                                 entry_ok("B", "gamma", 20)};
        sd::sort_entries(e, sd::SORTTYPE_MODIFIEDTIME, sd::SORTORDER_ASCENT);
        CHECK(e[0].dir_name == "A" && e[1].dir_name == "B" && e[2].dir_name == "C",
              "MODIFIEDTIME + ASCENT is oldest first");

        sd::sort_entries(e, sd::SORTTYPE_MODIFIEDTIME, sd::SORTORDER_DESCENT);
        CHECK(e[0].dir_name == "C" && e[2].dir_name == "A", "MODIFIEDTIME + DESCENT is newest first");

        sd::sort_entries(e, sd::SORTTYPE_SUBTITLE, sd::SORTORDER_ASCENT);
        CHECK(e[0].dir_name == "A" && e[1].dir_name == "C" && e[2].dir_name == "B",
              "SUBTITLE + ASCENT sorts by subtitle (alpha, beta, gamma)");
    }
    {
        /* Equal mtimes fall back to the subtitle, in both directions. */
        std::vector<sd::Entry> e{entry_ok("B", "zulu", 7), entry_ok("A", "alpha", 7)};
        sd::sort_entries(e, sd::SORTTYPE_MODIFIEDTIME, sd::SORTORDER_ASCENT);
        CHECK(e[0].dir_name == "A", "equal mtimes are tie-broken by subtitle");
    }

    /* --- 7. the argument checks (:479-714) ------------------------------ */
    {
        sd::ListArgs good;
        good.version = 0;
        good.has_set_list = true;
        good.sort_type = sd::SORTTYPE_MODIFIEDTIME;
        good.sort_order = sd::SORTORDER_DESCENT;
        good.has_prefix = true;
        good.prefix_terminated = true;
        good.dir_list_max = 16;
        good.buf_size = 16 * sd::DIR_LIST_SIZE;
        good.has_buf = true;
        good.file_list_max = 0;
        good.has_func_list = true;
        good.has_func_done = true;
        CHECK(sd::check_list_args(good, sd::Op::ListDelete) == 0, "a well-formed delete list passes");

        sd::ListArgs a = good;
        a.version = 2;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 1, "version above 420 is reason 1");

        a = good; a.has_set_list = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 11, "a NULL setList is reason 11");

        a = good; a.sort_type = 2;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 12, "sortType above SUBTITLE is reason 12");

        a = good; a.sort_order = 2;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 13, "sortOrder above ASCENT is reason 13");

        a = good; a.has_prefix = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 15, "a NULL dirNamePrefix is reason 15");

        a = good; a.prefix_terminated = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 17, "an unterminated prefix is reason 17");

        a = good; a.dir_list_max = sd::DIRLIST_MAX + 1;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 8, "dirListMax above 2048 is reason 8");

        a = good; a.buf_size = 4;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 7,
              "a buffer too small for dirListMax entries is reason 7");

        a = good; a.has_buf = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 6, "bufSize set with a NULL buf is reason 6");

        /* THE delete-specific rule: a delete list carries no file list. */
        a = good; a.file_list_max = 1;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 9,
              "fileListMax != 0 on a delete list is reason 9 (the delete-specific check)");

        /* And it must have funcDone, which the save/load variants do not. */
        a = good; a.has_func_done = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 137, "a delete list without funcDone is reason 137");

        a = good; a.has_func_list = false;
        CHECK(sd::check_list_args(a, sd::Op::ListDelete) == 18, "a delete list without funcList is reason 18");
    }

    /* --- 8. the size of a save, and its done structure (:1222-1247) ----- */
    CHECK(sd::size_kb_of({}) == 0, "a save with no files is 0 KB");
    CHECK(sd::size_kb_of({1024}) == 1, "a 1 KiB file is 1 KB");
    CHECK(sd::size_kb_of({1}) == 1, "a 1-byte file rounds UP to 1 KB");
    CHECK(sd::size_kb_of({1, 1, 1}) == 3,
          "three 1-byte files round up individually to 3 KB, not 1 (the rounding happens per file)");
    CHECK(sd::size_kb_of({1500}) == 2, "a 1500-byte file is 2 KiB");

    {
        const sd::DoneGet d = sd::done_of(true, "BLES00680-SAVE1", {2048, 1});
        CHECK(d.exc_result == 0, "deleting a real directory reports excResult 0");
        CHECK(d.dir_name == "BLES00680-SAVE1", "the done structure carries the directory name");
        CHECK(d.size_kb == 3, "the done size is the per-file rounded sum");
        CHECK(d.hdd_free_size_kb == 40 * 1024 * 1024 - 256, "the done structure reports the free space");
    }
    {
        const sd::DoneGet d = sd::done_of(false, "GONE", {});
        CHECK(d.exc_result == sd::ERR_NODATA,
              "a directory that is already gone reports NODATA, not success");
    }

    std::printf("savedata: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
