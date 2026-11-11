/* ps3base -- cellGame's decision logic, driven through the SAME header the
 * imports use (runtime/src/hle/cellgame_hle.h).
 *
 * No game data, no GPU, no network: the subject is rules, not assets.
 *
 * Every property here is one the previous implementation got wrong, so each
 * check is the regression:
 *   1. a data directory whose PARAM.SFO CATEGORY is the wrong kind is BROKEN,
 *      not "valid" and not "absent";
 *   2. a directory with no SFO is RET_NONE (2), not an error;
 *   3. cellGamePatchCheck's not-a-patch error is 0x8002cb27, not 0x8002cb0e;
 *   4. cellGameGetParamString's id table is RPCS3's, including the ids that do
 *      not exist and the modes each key is permitted in;
 *   5. the PARAM.SFO the module writes parses back to the values it was given.
 */
#include "hle/cellgame_hle.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cg = hle::cellgame;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

int main() {
    /* --- 1. the CATEGORY test -------------------------------------------
     * Values from refs/rpcs3/rpcs3/Emu/Cell/Modules/cellGame.cpp:918-928. */
    CHECK(cg::check_category(cg::GAMETYPE_GAMEDATA, "GD", true) == cg::CategoryMatch::Match,
          "game data + CATEGORY GD matches");
    CHECK(cg::check_category(cg::GAMETYPE_GAMEDATA, "DG", true) == cg::CategoryMatch::Mismatch,
          "game data + CATEGORY DG is a MISMATCH (previously reported as valid)");
    CHECK(cg::check_category(cg::GAMETYPE_GAMEDATA, "", true) == cg::CategoryMatch::Mismatch,
          "game data + empty CATEGORY is a MISMATCH");
    CHECK(cg::check_category(cg::GAMETYPE_GAMEDATA, "GD", false) == cg::CategoryMatch::Absent,
          "no SFO at all is ABSENT, not a mismatch");
    CHECK(cg::check_category(cg::GAMETYPE_DISC, "DG", true) == cg::CategoryMatch::Match,
          "disc + CATEGORY DG matches");
    CHECK(cg::check_category(cg::GAMETYPE_DISC, "GD", true) == cg::CategoryMatch::Mismatch,
          "disc + CATEGORY GD is a mismatch");
    CHECK(cg::check_category(cg::GAMETYPE_HDD, "HG", true) == cg::CategoryMatch::Match,
          "HDD game + CATEGORY HG matches");
    CHECK(cg::check_category(cg::GAMETYPE_HDD, "GD", true) == cg::CategoryMatch::Mismatch,
          "HDD game + CATEGORY GD is a mismatch (is_cat_hdd rejects it)");

    /* is_cat_hdd, refs/rpcs3/rpcs3/Loader/PSF.h:146-149. */
    CHECK(!cg::is_cat_hdd("DG"), "DG is not an HDD category");
    CHECK(!cg::is_cat_hdd("GD"), "GD is not an HDD category");
    CHECK(!cg::is_cat_hdd("MS"), "MS is not an HDD category");
    CHECK(cg::is_cat_hdd("HG"), "HG is an HDD category");
    CHECK(cg::is_cat_hdd("XX"), "XX behaves as HDD (second char is not D)");
    CHECK(!cg::is_cat_hdd("G"), "a one-character category is not HDD");

    /* --- 2. the return codes themselves -------------------------------- */
    CHECK(cg::RET_NONE == 2, "CELL_GAME_RET_NONE is 2");
    CHECK(cg::RET_OK == 0, "CELL_GAME_RET_OK is 0");
    CHECK(cg::ERR_NOTPATCH == static_cast<int32_t>(0x8002cb27),
          "CELL_GAME_ERROR_NOTPATCH is 0x8002cb27, not the 0x8002cb0e the old code returned");
    CHECK(cg::ERR_BROKEN == static_cast<int32_t>(0x8002cb05), "CELL_GAME_ERROR_BROKEN is 0x8002cb05");
    CHECK(cg::SIZEKB_NOTCALC == -1, "CELL_GAME_SIZEKB_NOTCALC is -1");

    /* --- 3. boot mode from CATEGORY (cellGame.cpp:789-831) -------------- */
    {
        const cg::BootMode d = cg::boot_mode_from_category("DG");
        CHECK(d.type == cg::GAMETYPE_DISC && d.attributes == 0 && d.mode == cg::CheckMode::DiscGame,
              "DG boots as a DISC game with no attributes");
        CHECK(d.from_patch_dir == false, "DG reads the disc's own SFO");

        const cg::BootMode p = cg::boot_mode_from_category("GD");
        CHECK(p.type == cg::GAMETYPE_DISC && p.attributes == cg::ATTRIBUTE_PATCH &&
                  p.mode == cg::CheckMode::Patch,
              "GD boots as a DISC game with CELL_GAME_ATTRIBUTE_PATCH");
        CHECK(p.from_patch_dir, "GD reads the mounted patch directory's SFO");

        const cg::BootMode h = cg::boot_mode_from_category("HG");
        CHECK(h.type == cg::GAMETYPE_HDD && h.attributes == 0 && h.mode == cg::CheckMode::HddGame,
              "HG boots as an HDD game");
    }

    /* --- 4. the parameter-id table and its per-mode permissions --------
     * Ids and modes: cellGame.cpp:1487-1545, 1458-1474. */
    CHECK(cg::key_for(0).name && std::strcmp(cg::key_for(0).name, "TITLE") == 0, "id 0 is TITLE");
    CHECK(cg::key_for(1).name && std::strcmp(cg::key_for(1).name, "TITLE") == 0,
          "id 1 (TITLE_DEFAULT) is also TITLE");
    CHECK(cg::key_for(2).name && std::strcmp(cg::key_for(2).name, "TITLE_00") == 0, "id 2 is TITLE_00");
    CHECK(cg::key_for(3).name && std::strcmp(cg::key_for(3).name, "TITLE_01") == 0, "id 3 is TITLE_01");
    CHECK(cg::key_for(21).name && std::strcmp(cg::key_for(21).name, "TITLE_19") == 0, "id 21 is TITLE_19");
    CHECK(cg::key_for(100).name && std::strcmp(cg::key_for(100).name, "TITLE_ID") == 0, "id 100 is TITLE_ID");
    CHECK(cg::key_for(101).name && std::strcmp(cg::key_for(101).name, "VERSION") == 0, "id 101 is VERSION");
    CHECK(cg::key_for(105).name && std::strcmp(cg::key_for(105).name, "PS3_SYSTEM_VER") == 0,
          "id 105 is PS3_SYSTEM_VER");
    CHECK(cg::key_for(106).name && std::strcmp(cg::key_for(106).name, "APP_VER") == 0, "id 106 is APP_VER");
    CHECK(cg::key_for(22).name == nullptr, "id 22 does not exist");
    CHECK(cg::key_for(102).name == nullptr,
          "id 102 is an INTEGER parameter, not a string one, so it has no string key");

    CHECK(cg::key_for(0).max == 128, "TITLE's maximum is 128 bytes");
    CHECK(cg::key_for(100).max == 10, "TITLE_ID's maximum is 10 bytes");
    CHECK(cg::key_for(101).max == 6, "VERSION's maximum is 6 bytes");

    /* The mode rules. A disc game may read TITLE and TITLE_ID and APP_VER and
     * nothing else; game data may read and write; PS3_SYSTEM_VER has no flags
     * at all so it is refused everywhere. */
    CHECK(cg::key_supported(cg::key_for(0), false, cg::CheckMode::DiscGame), "TITLE readable from a disc game");
    CHECK(!cg::key_supported(cg::key_for(0), true, cg::CheckMode::DiscGame), "TITLE not settable on a disc game");
    CHECK(!cg::key_supported(cg::key_for(101), false, cg::CheckMode::DiscGame),
          "VERSION is not readable from a disc game (get_game_data only)");
    CHECK(cg::key_supported(cg::key_for(0), true, cg::CheckMode::GameData), "TITLE settable on game data");
    CHECK(cg::key_supported(cg::key_for(105), false, cg::CheckMode::GameData) == false,
          "PS3_SYSTEM_VER is refused even for game data");
    CHECK(!cg::key_supported(cg::key_for(0), false, cg::CheckMode::NotSet),
          "no key is readable before any Check call");

    /* --- 5. the content-error dialog's type set (cellGame.h:132-143) ---- */
    CHECK(cg::content_error_type_valid(0), "dialog type 0 is valid");
    CHECK(cg::content_error_type_valid(2), "dialog type 2 is valid");
    CHECK(!cg::content_error_type_valid(3), "dialog type 3 is NOT valid");
    CHECK(cg::content_error_type_valid(100), "dialog type 100 is valid");
    CHECK(cg::content_error_type_valid(102), "dialog type 102 is valid");
    CHECK(!cg::content_error_type_valid(103), "dialog type 103 is NOT valid");
    CHECK(!cg::content_error_type_exits(2), "type 2 does not terminate the application");
    CHECK(cg::content_error_type_exits(100), "type 100 asks to terminate the application");

    /* --- 6. the PARAM.SFO writer round-trips ---------------------------
     * This is what cellGameContentPermit writes into a new game-data
     * directory, and what vfs::Psf::load then reads. */
    {
        const std::vector<uint8_t> sfo =
            cg::build_psf(cg::game_data_entries("BLES00680", "Red Dead Redemption", "01.00"));
        CHECK(sfo.size() > 20, "the SFO image has a header");
        CHECK(sfo[0] == 0 && sfo[1] == 'P' && sfo[2] == 'S' && sfo[3] == 'F', "the SFO magic is \\0PSF");
        CHECK(sfo[4] == 0x01 && sfo[5] == 0x01, "the SFO version is 0x0101 little-endian");

        std::vector<cg::ParsedKey> keys;
        CHECK(cg::parse_psf(sfo, keys), "the written SFO parses back");
        CHECK(keys.size() == 4, "the game-data SFO has four keys");

        std::string category, title_id, title, version;
        for (const cg::ParsedKey& k : keys) {
            if (k.key == "CATEGORY") category = k.str;
            else if (k.key == "TITLE_ID") title_id = k.str;
            else if (k.key == "TITLE") title = k.str;
            else if (k.key == "VERSION") version = k.str;
        }
        CHECK(category == "GD", "CATEGORY round-trips as GD");
        CHECK(title_id == "BLES00680", "TITLE_ID round-trips");
        CHECK(title == "Red Dead Redemption", "TITLE round-trips");
        CHECK(version == "01.00", "VERSION round-trips");
    }
    /* A title longer than the 128-byte slot is truncated, not overflowed. */
    {
        const std::string long_title(400, 'A');
        const std::vector<uint8_t> sfo = cg::build_psf(cg::game_data_entries("BLES00680", long_title, "01.00"));
        std::vector<cg::ParsedKey> keys;
        CHECK(cg::parse_psf(sfo, keys), "an over-long TITLE still parses");
        for (const cg::ParsedKey& k : keys) {
            if (k.key == "TITLE") CHECK(k.str.size() <= 127, "an over-long TITLE is truncated to 127 bytes");
        }
    }
    /* An empty string entry does not produce a stray byte. */
    {
        const std::vector<uint8_t> sfo = cg::build_psf(cg::game_data_entries("", "", ""));
        std::vector<cg::ParsedKey> keys;
        CHECK(cg::parse_psf(sfo, keys), "an SFO of empty values parses");
        CHECK(keys.size() == 4, "an SFO of empty values still has four keys");
    }
    /* A non-SFO image is rejected rather than read as garbage. */
    {
        std::vector<uint8_t> junk(64, 0xAB);
        std::vector<cg::ParsedKey> keys;
        CHECK(!cg::parse_psf(junk, keys), "arbitrary bytes are not a PSF");
    }

    std::printf("cellgame: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
