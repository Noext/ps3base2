/* ps3base -- sceNpTrophy, tested against the reference.
 *
 * The twelve functions Red Dead Redemption imports (out-rdr/ppu/table.cpp:81831-81842)
 * are driven HERE, with a real ppu::Context and real guest memory, through the
 * same handlers the import path calls. What is stubbed is only what is outside
 * the module: hle::Reg (no import table), hle::call_guest (no PPU scheduler to
 * run a guest callback) and hle::sysutil_post (no guest thread to drain the
 * callback queue). Those three are seams, and the test asserts what the module
 * HANDS them: callback arguments, order and count.
 *
 * No game data: every trophy file here is built by this test (a synthetic
 * TROPHY.TRP carrying a TROPCONF.SFM). No GPU, no network.
 *
 * The properties, each of which was a wrong answer somewhere before:
 *   1. the state machine's return codes and their ORDER (the init check before
 *      the argument check where RPCS3 does it, the reverse where it does not);
 *   2. the context name composed from SceNpCommunicationId, including the
 *      truncation at NUL and the "_%02d" suffix;
 *   3. the three commSign checks -- magic, six zero bytes, version 0x100;
 *   4. TRP parsing: header, checksum for version >= 2, entry list, the
 *      required-space arithmetic, and the TROP_XX/TROP.SFM/TROPCONF.SFM
 *      selection before install;
 *   5. TROPUSR.DAT: generate, save, reload, unlock, and the platinum rule;
 *   6. the register status sequence (content and order), and the abort when the
 *      synchronous status callback answers negative;
 *   7. every handler's use of guest memory: what it writes, and that it refuses
 *      an unmapped pointer instead of faulting.
 */
#include "runtime/memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

/* The module under test, included so the handler bodies are callable. */
#include "../runtime/src/hle/sceNpTrophy.cpp"

/* --- the three seams ---------------------------------------------------- */
namespace {
std::map<std::string, hle::Fn>& test_registry() {
    static std::map<std::string, hle::Fn> m;
    return m;
}

struct GuestCall {
    uint32_t opd;
    uint64_t a0, a1, a2, a3, a4;
};

std::vector<GuestCall>& guest_calls() {
    static std::vector<GuestCall> v;
    return v;
}

/* What call_guest hands back to the module: the callback's r3. */
int32_t& next_callback_result() {
    static int32_t v = 0;
    return v;
}

std::vector<std::function<void(ppu::Context&)>>& posted() {
    static std::vector<std::function<void(ppu::Context&)>> v;
    return v;
}
}

namespace hle {
Reg::Reg(const char* module, const char* name, Fn fn) {
    test_registry()[std::string(module) + "::" + name] = fn;
}

void call_guest(ppu::Context& ctx, uint32_t opd, uint64_t a0, uint64_t a1, uint64_t a2,
                uint64_t a3, uint64_t a4) {
    guest_calls().push_back({opd, a0, a1, a2, a3, a4});
    ctx.gpr[3] = static_cast<uint64_t>(static_cast<int64_t>(next_callback_result()));
}

void sysutil_post(std::function<void(ppu::Context&)> fn) { posted().push_back(std::move(fn)); }
}

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s (line %d)\n", msg, __LINE__); } \
} while (0)

namespace {

using namespace hle::trophy;
namespace fs = std::filesystem;

int64_t low32(int64_t v) { return static_cast<int64_t>(static_cast<int32_t>(v & 0xFFFFFFFFll)); }

/* Every handler call: a fresh context, the arguments in r3.., the result in r3. */
int64_t call(void (*fn)(ppu::Context&, uint8_t*), uint64_t a0 = 0, uint64_t a1 = 0,
             uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0) {
    ppu::Context c{};
    c.gpr[3] = a0; c.gpr[4] = a1; c.gpr[5] = a2; c.gpr[6] = a3; c.gpr[7] = a4;
    fn(c, vm::g_base);
    return low32(static_cast<int64_t>(c.gpr[3]));
}

std::string tmp_root;
std::string vfs_root;
std::string disc_root;

void write_host(const std::string& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<uint8_t> read_host(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> b(static_cast<size_t>(n > 0 ? n : 0));
    if (!b.empty()) f.read(reinterpret_cast<char*>(b.data()), n);
    return b;
}

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = static_cast<uint8_t>(v >> 24); b[at + 1] = static_cast<uint8_t>(v >> 16);
    b[at + 2] = static_cast<uint8_t>(v >> 8); b[at + 3] = static_cast<uint8_t>(v);
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
    put32(b, at, static_cast<uint32_t>(v >> 32)); put32(b, at + 4, static_cast<uint32_t>(v));
}

/* A synthetic TRP. Version 1 skips RPCS3's SHA-1 verification (TRP.cpp:133);
 * version 2 exercises it, with the hash computed the way TRPLoader does it
 * (zero the sha1 field, hash the whole file). */
struct TrpFile {
    std::string name;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> build_trp(const std::vector<TrpFile>& files, uint32_t version) {
    const uint32_t element = 0x40;
    const size_t entries_at = sizeof(TrpHeader);
    const size_t data_at = entries_at + files.size() * element;

    size_t total = data_at;
    for (const auto& f : files) total += f.data.size();

    std::vector<uint8_t> trp(total, 0);
    put32(trp, 0, 0xDCA24D00u);
    put32(trp, 4, version);
    put64(trp, 8, total);
    put32(trp, 16, static_cast<uint32_t>(files.size()));
    put32(trp, 20, element);
    put32(trp, 24, 0);

    size_t at = data_at;
    for (size_t i = 0; i < files.size(); i++) {
        const size_t e = entries_at + i * element;
        std::string nm = files[i].name;
        if (nm.size() >= 32) nm.resize(31);
        std::memcpy(&trp[e], nm.data(), nm.size());
        put64(trp, e + 32, at);
        put64(trp, e + 40, files[i].data.size());
        std::memcpy(&trp[at], files[i].data.data(), files[i].data.size());
        at += files[i].data.size();
    }

    if (version >= 2) {
        std::memset(&trp[0x1c], 0, 20);
        uint8_t hash[20];
        sha1(trp.data(), trp.size(), hash);
        std::memcpy(&trp[0x1c], hash, 20);
    }
    return trp;
}

const char* kConf =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<trophyconf>\n"
    "  <title-name>Red Dead Redemption</title-name>\n"
    "  <title-detail>A western &amp; a story.</title-detail>\n"
    "  <trophy id=\"0\" pid=\"4\" ttype=\"B\" hidden=\"no\"><name>First Steps</name><detail>D0</detail></trophy>\n"
    "  <trophy id=\"1\" pid=\"4\" ttype=\"B\" hidden=\"yes\"><name>Hidden One</name><detail>D1</detail></trophy>\n"
    "  <trophy id=\"2\" pid=\"4\" ttype=\"S\" hidden=\"no\"><name>Silver</name><detail>D2</detail></trophy>\n"
    "  <trophy id=\"3\" pid=\"4\" ttype=\"G\" hidden=\"no\"><name>Gold</name><detail>D3</detail></trophy>\n"
    "  <trophy id=\"4\" pid=\"4294967295\" ttype=\"P\" hidden=\"no\"><name>Platinum</name><detail>D4</detail></trophy>\n"
    "</trophyconf>\n";

std::vector<uint8_t> bytes_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

struct TrpState {
    std::string trp_name = "BLES00680_00";
    std::string disc_trp;
};

TrpState g_trp;

void install_synthetic_trp(uint32_t version) {
    std::vector<TrpFile> files;
    files.push_back({"TROPCONF.SFM", bytes_of(kConf)});
    files.push_back({"TROP_01.SFM", bytes_of(kConf)});
    files.push_back({"TROP_02.SFM", bytes_of(kConf)});
    files.push_back({"ICON0.PNG", {0x89, 'P', 'N', 'G'}});
    files.push_back({"TROP000.PNG", {0x89, 'P', 'N', 'G'}});
    write_host(g_trp.disc_trp, build_trp(files, version));
}

/* Fill the SceNpCommunicationId / SceNpCommunicationSignature pair a real title
 * passes. Layout from sceNp.h:1258-1263 and :1359-1362. */
void fill_comm_id(uint32_t ea, const char* data9, uint8_t num) {
    std::memset(vm::ptr(ea), 0, sizeof(CommId));
    std::memcpy(vm::ptr(ea), data9, std::strlen(data9));
    vm::ptr(ea)[10] = num;
}

void fill_signature(uint32_t ea, uint32_t magic = COMM_SIGN_MAGIC, uint16_t version = 0x100,
                    bool padding_zero = true) {
    std::memset(vm::ptr(ea), 0, sizeof(Signature));
    uint8_t buf[4] = {static_cast<uint8_t>(magic >> 24), static_cast<uint8_t>(magic >> 16),
                      static_cast<uint8_t>(magic >> 8), static_cast<uint8_t>(magic)};
    std::memcpy(vm::ptr(ea), buf, 4);
    vm::ptr(ea)[4] = static_cast<uint8_t>(version >> 8);
    vm::ptr(ea)[5] = static_cast<uint8_t>(version);
    if (!padding_zero) std::memset(vm::ptr(ea) + 6, 0x7F, 6);
}

uint32_t g_next = 0x21000000;
uint32_t alloc_area(uint32_t size, uint32_t align = 0x1000) {
    const uint32_t a = g_next;
    uint32_t sz = size;
    if (!vm::map(a, sz)) return 0;
    g_next += size;
    (void)align;
    return a;
}

}  // namespace

int main() {
    if (!vm::init()) {
        std::fprintf(stderr, "vm::init failed\n");
        return 1;
    }

    /* --- 0. the structures the guest sees --------------------------------
     * The static_asserts in nptrophy_hle.h fired at compile time; these name
     * the totals here so a change to one is a readable test failure too. */
    CHECK(sizeof(GameDetails) == 0x498, "SceNpTrophyGameDetails is 1176 (0x498) bytes");
    CHECK(sizeof(GameData) == 0x14, "SceNpTrophyGameData is 20 bytes");
    CHECK(sizeof(TrophyDetails) == 0x48C, "SceNpTrophyDetails is 1164 (0x48C) bytes");
    CHECK(sizeof(TrophyData) == 0x10, "SceNpTrophyData is 16 bytes");
    CHECK(sizeof(FlagArray) == 0x10, "SceNpTrophyFlagArray is 16 bytes");
    CHECK(sizeof(TrpHeader) == 0x40, "TRPHeader is 64 bytes");
    CHECK(sizeof(TrpEntry) == 0x40, "TRPEntry is 64 bytes");
    CHECK(sizeof(TropusrHeader) == 0x30, "TROPUSRHeader is 48 bytes");
    CHECK(sizeof(TropusrTableHeader) == 0x20, "TROPUSRTableHeader is 32 bytes");
    CHECK(sizeof(TropusrEntry4) == 0x60, "TROPUSREntry4 is 96 bytes");
    CHECK(sizeof(TropusrEntry6) == 0x70, "TROPUSREntry6 is 112 bytes");

    /* --- 1. SHA-1, the TRP integrity check -------------------------------
     * FIPS 180-1 known answers. A wrong digest accepts a corrupt trophy pack. */
    {
        uint8_t out[20];
        sha1(reinterpret_cast<const uint8_t*>(""), 0, out);
        const uint8_t empty[20] = {0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                   0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09};
        CHECK(std::memcmp(out, empty, 20) == 0, "SHA-1 of the empty string");
        sha1(reinterpret_cast<const uint8_t*>("abc"), 3, out);
        const uint8_t abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
        CHECK(std::memcmp(out, abc, 20) == 0, "SHA-1 of 'abc'");
        const char* longmsg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        sha1(reinterpret_cast<const uint8_t*>(longmsg), std::strlen(longmsg), out);
        const uint8_t lm[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                                0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
        CHECK(std::memcmp(out, lm, 20) == 0, "SHA-1 of the 56-byte two-block vector");
    }

    tmp_root = (fs::temp_directory_path() / ("nptrophy-test-" + std::to_string(::getpid()))).string();
    vfs_root = tmp_root + "/vfs";
    disc_root = tmp_root + "/disc";
    fs::create_directories(vfs_root + "/dev_hdd0");
    fs::create_directories(disc_root + "/PS3_GAME");
    ::setenv("PS3BASE_VFS_DIR", vfs_root.c_str(), 1);
    ::setenv("PS3BASE_DISC_DIR", disc_root.c_str(), 1);
    ::setenv("PS3BASE_RPCS3_DIR", (tmp_root + "/rpcs3").c_str(), 1);

    g_trp.disc_trp = disc_root + "/PS3_GAME/TROPDIR/" + g_trp.trp_name + "/TROPHY.TRP";

    const uint32_t context_ea = alloc_area(0x100);
    const uint32_t handle_ea = alloc_area(0x100);
    const uint32_t comm_id_ea = alloc_area(0x100);
    const uint32_t sign_ea = alloc_area(0x200);
    const uint32_t details_ea = alloc_area(0x2000);
    const uint32_t data_ea = alloc_area(0x2000);
    const uint32_t reqspace_ea = alloc_area(0x100);
    const uint32_t platinum_ea = alloc_area(0x100);
    CHECK(context_ea && handle_ea && comm_id_ea && sign_ea && details_ea && data_ea,
          "guest memory for the arguments");
    if (fails) return 1;

    /* --- 2. the state machine, before Init ------------------------------- */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateHandle, handle_ea) == ERR_NOT_INITIALIZED,
          "CreateHandle before Init is NOT_INITIALIZED (sceNpTrophy.cpp:342-345)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyTerm) == ERR_NOT_INITIALIZED,
          "Term before Init is NOT_INITIALIZED (:321-324)");
    fill_comm_id(comm_id_ea, "BLES00680", 0);
    fill_signature(sign_ea);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_NOT_INITIALIZED,
          "CreateContext before Init is NOT_INITIALIZED (:445-448)");

    /* A null commSign is INVALID_ARGUMENT even without Init: RPCS3 dereferences
     * it at :434-439, before its own init check. */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, 0, 0) ==
              ERR_INVALID_ARGUMENT,
          "CreateContext(NULL commSign) is INVALID_ARGUMENT, checked BEFORE init (:434-437)");

    /* --- 3. Init/Term ---------------------------------------------------- */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyInit, 0, 0, 0, 0) == 0, "Init returns CELL_OK");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyInit, 0, 0, 0, 0) == ERR_ALREADY_INITIALIZED,
          "Init twice is ALREADY_INITIALIZED (:298-301)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyTerm) == 0, "Term returns CELL_OK");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyInit, 0, 0, 0, 1) == ERR_NOT_SUPPORTED,
          "Init with options > 0 is NOT_SUPPORTED (:303-306)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyInit, 0, 0, 0, 0) == 0, "Init after Term succeeds");

    /* --- 4. handles ------------------------------------------------------ */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateHandle, 0) == ERR_INVALID_ARGUMENT,
          "CreateHandle(NULL) is INVALID_ARGUMENT (:347-350)");
    vm::write32(handle_ea, 0xDEADBEEF);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateHandle, handle_ea) == 0,
          "CreateHandle succeeds");
    const uint32_t handle = vm::read32(handle_ea);
    CHECK(handle == ID_BASE, "the first handle id is 1, RPCS3's id_base (:92)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateHandle, handle_ea) == 0, "a second handle");
    CHECK(vm::read32(handle_ea) == ID_BASE + 1, "the second handle id is 2");

    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, 0) == ERR_INVALID_ARGUMENT,
          "DestroyHandle(0) is INVALID_ARGUMENT (:377-381)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, 99) == ERR_INVALID_ARGUMENT,
          "DestroyHandle(out of range) is INVALID_ARGUMENT");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, 4) == ERR_UNKNOWN_HANDLE,
          "DestroyHandle of an unused id is UNKNOWN_HANDLE (:383-386)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, ID_BASE + 1) == 0,
          "DestroyHandle of a live handle succeeds");

    /* --- 5. CreateContext: every check, in RPCS3's order ------------------ */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, 0, comm_id_ea, sign_ea, 0) ==
              ERR_INVALID_ARGUMENT,
          "CreateContext(NULL context) is INVALID_ARGUMENT (:450-453)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, 0, sign_ea, 0) ==
              ERR_INVALID_ARGUMENT,
          "CreateContext(NULL commId) is INVALID_ARGUMENT (:450-453)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 2) ==
              ERR_NOT_SUPPORTED,
          "CreateContext(options > READ_ONLY) is NOT_SUPPORTED (:455-458)");

    fill_signature(sign_ea, 0x12345678u);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_INVALID_NP_COMM_ID,
          "a wrong signature magic is INVALID_NP_COMM_ID (:483-486)");
    fill_signature(sign_ea, COMM_SIGN_MAGIC, 0x200);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_INVALID_NP_COMM_ID,
          "signature version != 0x100 is INVALID_NP_COMM_ID (:494-498)");
    fill_signature(sign_ea, COMM_SIGN_MAGIC, 0x100, false);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_INVALID_NP_COMM_ID,
          "non-zero signature padding is INVALID_NP_COMM_ID (:488-492)");

    fill_signature(sign_ea);
    fill_comm_id(comm_id_ea, "BLES00680", 100);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_INVALID_NP_COMM_ID,
          "a communication number > 99 is INVALID_NP_COMM_ID (:463-467)");

    fill_comm_id(comm_id_ea, "BLES00680", 0);
    vm::write32(context_ea, 0xDEADBEEF);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) == 0,
          "a valid CreateContext succeeds");
    const uint32_t context = vm::read32(context_ea);
    CHECK(context == ID_BASE, "the first context id is 1");
    CHECK(hle::manager().context(context) &&
              hle::manager().context(context)->trp_name == "BLES00680_00",
          "the context name is the 9-byte id plus '_%02d' (sceNpTrophy.cpp:473-501)");
    CHECK(hle::manager().context(context) && !hle::manager().context(context)->read_only,
          "the default context is writable");

    /* The name is cut at the first NUL, and the suffix follows it (:476-479). */
    fill_comm_id(comm_id_ea, "AB", 5);
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) == 0,
          "a short communication id still creates a context");
    CHECK(hle::manager().context(vm::read32(context_ea))->trp_name == "AB_05",
          "a short id is truncated at NUL before the suffix");

    /* Read-only is OPTIONS_CREATE_CONTEXT_READ_ONLY = 1 (:88, :514). */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 1) == 0,
          "a read-only context is accepted");
    const uint32_t ro_context = vm::read32(context_ea);
    CHECK(hle::manager().context(ro_context)->read_only, "options bit 0 sets read_only");

    /* The pool is 4 (:41), so a fifth context is EXCEEDS_MAX (:506-509). */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) == 0,
          "the fourth context fits");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) ==
              ERR_EXCEEDS_MAX,
          "the fifth context is EXCEEDS_MAX");

    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyContext, 0) == ERR_INVALID_ARGUMENT,
          "DestroyContext(0) is INVALID_ARGUMENT (:541-545)");
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyContext, 5) == ERR_INVALID_ARGUMENT,
          "DestroyContext past the 4-id pool is INVALID_ARGUMENT too (:541-545)");

    /* --- 6. GetGameInfo before registration ------------------------------ */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo, context, handle, details_ea, data_ea) ==
              ERR_CONTEXT_NOT_REGISTERED,
          "GetGameInfo on an unregistered context is CONTEXT_NOT_REGISTERED (:965-969)");

    /* --- 7. RegisterContext without a TROPHY.TRP ------------------------- */
    {
        next_callback_result() = 0;
        guest_calls().clear();
        posted().clear();
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyRegisterContext, context, handle, 0x30000000, 0, 0) ==
                  ERR_CONF_DOES_NOT_EXIST,
              "RegisterContext with no TROPHY.TRP is CONF_DOES_NOT_EXIST (:610-615)");
        CHECK(guest_calls().empty(), "and the status callback is NOT called");
    }

    /* --- 8. TRP parsing and the required-space arithmetic ---------------- */
    install_synthetic_trp(1);
    {
        std::vector<uint8_t> bytes = read_host(g_trp.disc_trp);
        Trp trp;
        CHECK(trp.load(bytes), "a synthetic version-1 TRP parses");
        CHECK(trp.entries().size() == 5, "all five entries are read");
        CHECK(trp.contains("TROPCONF.SFM"), "TROPCONF.SFM is in the pack");
        CHECK(!trp.contains("TROPCONF"), "exact names only");
        CHECK(trp.contains("TROP_01.SFM"), "TROP_01.SFM is in the pack");
        CHECK(trp.entries()[0].name[0] == 'T', "the entry name is copied verbatim");
        const uint64_t expect = bytes.size() - sizeof(TrpHeader) - 5 * 0x40;
        CHECK(trp.required_space() == expect,
              "required space is size - header - count*element (TRP.cpp:186-192)");

        /* The selection before install, sceNpTrophy.cpp:677-708. Language 1. */
        Trp plan = trp;
        CHECK(prepare_entries(plan, 1) == EntryPlan::Ok, "a pack with TROP_01.SFM is installable");
        CHECK(plan.contains("TROPCONF.SFM"), "TROP_01.SFM was renamed to TROPCONF.SFM");
        CHECK(!plan.contains("TROP_01.SFM"), "the language file no longer exists under its old name");
        CHECK(!plan.contains("TROP_02.SFM"), "the other language files are discarded");

        /* Without a language file, TROP.SFM takes over. */
        std::vector<TrpFile> files;
        files.push_back({"TROP.SFM", bytes_of(kConf)});
        files.push_back({"TROPCONF.SFM", bytes_of("<trophyconf/>")});
        Trp generic;
        CHECK(generic.load(build_trp(files, 1)), "a pack with only TROP.SFM parses");
        CHECK(prepare_entries(generic, 1) == EntryPlan::Ok, "TROP.SFM is a valid fallback");
        CHECK(generic.contains("TROPCONF.SFM"), "TROP.SFM was renamed to TROPCONF.SFM");
        CHECK(!generic.contains("TROP.SFM"), "and the old name is gone");

        /* With neither, the config is incomplete -> ILLEGAL_UPDATE (:693-698). */
        std::vector<TrpFile> bare;
        bare.push_back({"ICON0.PNG", {1, 2, 3}});
        Trp none;
        CHECK(none.load(build_trp(bare, 1)), "a pack with no config parses");
        CHECK(prepare_entries(none, 1) == EntryPlan::NoConfig,
              "a pack with no config at all is refused");
    }

    /* Integrity: a version-2 pack whose hash does not match must be rejected. */
    {
        std::vector<TrpFile> files;
        files.push_back({"TROPCONF.SFM", bytes_of(kConf)});
        std::vector<uint8_t> good = build_trp(files, 2);
        Trp ok;
        CHECK(ok.load(good), "a version-2 TRP with a correct SHA-1 parses");
        good[0x30] ^= 0xFF;   /* one byte of padding, inside the hashed range */
        Trp bad;
        CHECK(!bad.load(good),
              "a version-2 TRP with a corrupted payload is REJECTED (TRP.cpp:158-162)");
    }

    /* --- 9. RegisterContext, end to end on a synthetic pack --------------- */
    {
        next_callback_result() = 0;
        guest_calls().clear();
        posted().clear();
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyRegisterContext, context, handle, 0x30000000, 0x1234, 0) == 0,
              "RegisterContext with a TROPHY.TRP succeeds");
        CHECK(guest_calls().size() == 1, "the status callback is called exactly once synchronously");
        CHECK(guest_calls().size() == 1 && guest_calls()[0].a0 == context,
              "the callback's first argument is the context");
        CHECK(guest_calls().size() == 1 && guest_calls()[0].a1 == STATUS_NOT_INSTALLED,
              "and the second is NOT_INSTALLED, because the HDD copy did not exist (:622-623)");
        CHECK(guest_calls().size() == 1 && guest_calls()[0].a2 == 0 && guest_calls()[0].a3 == 0,
              "completed and total are both 0 on the status call (:631)");
        CHECK(guest_calls().size() == 1 && guest_calls()[0].a4 == 0x1234,
              "the fifth argument is the caller's arg -- dropping it was a real bug here (:159, :797)");

        /* Five trophies: progress total 4, finalize total max(4,9)-5 = 4, so the
         * sequence is 4 + 5 + 5 + 1 = 15 deliveries (:769-775). */
        const auto expected = register_statuses(5);
        CHECK(expected.size() == 15, "five trophies produce 15 status deliveries");
        CHECK(posted().size() == expected.size(), "RegisterContext queues exactly those");
        CHECK(expected.front().status == STATUS_PROCESSING_SETUP && expected.front().completed == 0 &&
                  expected.front().total == 3,
              "the sequence starts at PROCESSING_SETUP 0/3");
        CHECK(expected.back().status == STATUS_PROCESSING_COMPLETE && expected.back().completed == 0 &&
                  expected.back().total == 0,
              "and ends at PROCESSING_COMPLETE 0/0");

        /* Drain the queue the way cellSysutilCheckCallback does and check the
         * arguments each delivery carries. */
        ppu::Context c{};
        for (auto& fn : posted()) fn(c);
        CHECK(guest_calls().size() == 16, "every queued status reaches the callback");
        CHECK(guest_calls().size() == 16 && guest_calls()[1].a1 == STATUS_PROCESSING_SETUP &&
                  guest_calls()[1].a2 == 0 && guest_calls()[1].a3 == 3,
              "the first queued delivery is SETUP 0/3");
        CHECK(guest_calls().size() == 16 && guest_calls()[5].a1 == STATUS_PROCESSING_PROGRESS &&
                  guest_calls()[5].a2 == 0 && guest_calls()[5].a3 == 4,
              "the progress phase begins after SETUP completes (4 deliveries)");
        CHECK(guest_calls().size() == 16 && guest_calls()[15].a1 == STATUS_PROCESSING_COMPLETE,
              "the last delivery is PROCESSING_COMPLETE");

        /* The install happened: TROPCONF.SFM and TROPUSR.DAT are on the HDD. */
        const std::string hdd = vfs_root + "/dev_hdd0/home/00000001/trophy/" + g_trp.trp_name;
        CHECK(fs::is_regular_file(hdd + "/TROPCONF.SFM"),
              "registration installed TROPCONF.SFM into /dev_hdd0/home/00000001/trophy");
        CHECK(fs::is_regular_file(hdd + "/TROPUSR.DAT"),
              "and generated TROPUSR.DAT from it (TROPUSR.cpp:34-75)");
        CHECK(fs::is_regular_file(hdd + "/TROP000.PNG"), "the icon entries were extracted too");
        CHECK(!fs::exists(hdd + "/TROP_02.SFM"), "the other languages were discarded");

        /* A second registration sees the data already installed (:622-623). */
        guest_calls().clear();
        posted().clear();
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyRegisterContext, context, handle, 0x30000000, 0, 0) == 0,
              "a second RegisterContext succeeds");
        CHECK(guest_calls().size() == 1 && guest_calls()[0].a1 == STATUS_INSTALLED,
              "and now reports STATUS_INSTALLED");
        posted().clear();

        /* Its TROPUSR.DAT reloads and describes the five trophies. */
        Tropusr reloaded;
        CHECK(reloaded.load(read_host(hdd + "/TROPUSR.DAT")), "the written TROPUSR.DAT reloads");
        CHECK(reloaded.count() == 5, "it holds five trophies");
        CHECK(reloaded.grade(0) == GRADE_BRONZE, "trophy 0 is bronze");
        CHECK(reloaded.grade(2) == GRADE_SILVER, "trophy 2 is silver");
        CHECK(reloaded.grade(3) == GRADE_GOLD, "trophy 3 is gold");
        CHECK(reloaded.grade(4) == GRADE_PLATINUM, "trophy 4 is platinum");

        /* An aborted handle refuses registration with ERR_ABORT (:159-163),
         * which is the check get_context_ex performs. */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyAbortHandle, handle) == 0, "AbortHandle succeeds");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyRegisterContext, context, handle, 0x30000000, 0, 0) ==
                  ERR_ABORT,
              "an aborted handle makes RegisterContext answer SCE_NP_TROPHY_ERROR_ABORT");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyAbortHandle, handle) == 0,
              "aborting twice is still CELL_OK -- RPCS3 does not test the flag (:411-427)");
    }

    /* A fresh handle for the rest of the suite: the one above is aborted, and
     * an aborted handle refuses everything through get_context_ex (:159-163). */
    CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateHandle, handle_ea) == 0,
          "a new handle after the aborted one");
    const uint32_t live = vm::read32(handle_ea);

    /* --- 10. the callback's negative answer aborts registration ----------- */
    {
        ppu::Context c{};
        c.gpr[3] = handle_ea;
        hle::hle_sceNpTrophy_sceNpTrophyCreateHandle(c, vm::g_base);
        const uint32_t h = vm::read32(handle_ea);
        next_callback_result() = -1;
        guest_calls().clear();
        posted().clear();
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyRegisterContext, context, h, 0x30000000, 0, 0) ==
                  ERR_PROCESSING_ABORTED,
              "a negative status callback answer is PROCESSING_ABORTED (:631-635)");
        CHECK(posted().empty(), "and no status sequence is queued afterwards");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, h) == 0, "cleanup");
        next_callback_result() = 0;
    }

    /* --- 11. GetRequiredDiskSpace ---------------------------------------- */
    {
        const uint32_t h = live;
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetRequiredDiskSpace, context, h, 0, 0) ==
                  ERR_INVALID_ARGUMENT,
              "GetRequiredDiskSpace(NULL reqspace) is INVALID_ARGUMENT (:843-846)");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetRequiredDiskSpace, context, h, reqspace_ea, 1) ==
                  ERR_NOT_SUPPORTED,
              "options > 0 is NOT_SUPPORTED (:848-851)");
        vm::write64(reqspace_ea, 0xDEADBEEF);
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetRequiredDiskSpace, context, h, reqspace_ea, 0) == 0,
              "with the data installed it succeeds");
        CHECK(vm::read64(reqspace_ea) == 0,
              "and reports 0: the config is already installed (:901-904)");
    }

    /* --- 12. GetGameInfo over the installed data -------------------------- */
    {
        std::memset(vm::ptr(details_ea), 0xCD, sizeof(GameDetails));
        std::memset(vm::ptr(data_ea), 0xCD, sizeof(GameData));
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo, context, live, details_ea, data_ea) == 0,
              "GetGameInfo succeeds on a registered context");
        CHECK(vm::read32(details_ea + 0) == 5, "numTrophies counts the five 'trophy' elements");
        CHECK(vm::read32(details_ea + 4) == 1, "one platinum");
        CHECK(vm::read32(details_ea + 8) == 1, "one gold");
        CHECK(vm::read32(details_ea + 12) == 1, "one silver");
        CHECK(vm::read32(details_ea + 16) == 2, "two bronze");
        CHECK(std::string(reinterpret_cast<const char*>(vm::ptr(details_ea + offsetof(GameDetails, title)))) ==
                  "Red Dead Redemption",
              "title-name reaches details->title");
        CHECK(std::string(reinterpret_cast<const char*>(vm::ptr(details_ea + offsetof(GameDetails, description)))) ==
                  "A western & a story.",
              "title-detail reaches details->description, with the entity decoded");
        CHECK(vm::read32(details_ea + offsetof(GameDetails, reserved)) == 0, "the reserved bytes are zeroed");
        CHECK(vm::read32(data_ea + 0) == 0, "nothing is unlocked yet");

        /* Both pointers null is INVALID_ARGUMENT (:971-974). */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo, context, live, 0, 0) ==
                  ERR_INVALID_ARGUMENT,
              "GetGameInfo(NULL, NULL) is INVALID_ARGUMENT");
    }

    /* --- 13. UnlockTrophy ------------------------------------------------- */
    {
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 99, platinum_ea) ==
                  ERR_INVALID_TROPHY_ID,
              "an id past the trophy count is INVALID_TROPHY_ID (:1094-1097)");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 4, platinum_ea) ==
                  ERR_CANNOT_UNLOCK_PLATINUM,
              "unlocking the platinum directly is CANNOT_UNLOCK_PLATINUM (:1099-1102)");

        vm::write32(platinum_ea, 0xAAAAAAAA);
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 0, platinum_ea) == 0,
              "unlocking trophy 0 succeeds");
        CHECK(vm::read32(platinum_ea) == INVALID_TROPHY_ID,
              "platinumId is INVALID_TROPHY_ID while trophies remain locked (:1134-1138)");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 0, platinum_ea) ==
                  ERR_ALREADY_UNLOCKED,
              "a second unlock is ALREADY_UNLOCKED (:1104-1107)");

        /* The read-only context refuses an unlock with INVALID_CONTEXT, not
         * INVALID_ARGUMENT (:1081 with test_writeable, :139-143). */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, ro_context, live, 1, platinum_ea) ==
                  ERR_INVALID_CONTEXT,
              "a read-only context refuses an unlock with INVALID_CONTEXT");

        /* The hidden trophy is unlockable; only its DETAILS are refused while
         * locked. After 1, 2 and 3 the platinum (pid 4 links them all) follows. */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 1, platinum_ea) == 0,
              "the hidden trophy unlocks");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 2, platinum_ea) == 0,
              "trophy 2 unlocks");
        CHECK(vm::read32(platinum_ea) == INVALID_TROPHY_ID, "still no platinum with gold outstanding");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, context, live, 3, platinum_ea) == 0,
              "trophy 3 unlocks");
        CHECK(vm::read32(platinum_ea) == 4,
              "the platinum (id 4) unlocks when everything linking to it does (TROPUSR.cpp:310-329)");

        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo, context, live, details_ea, data_ea) == 0,
              "GetGameInfo after the unlocks");
        CHECK(vm::read32(data_ea + 0) == 5, "all five trophies report unlocked");
        CHECK(vm::read32(data_ea + 4) == 1, "including the platinum");
        CHECK(vm::read32(data_ea + 16) == 2, "and both bronzes");

        /* Persistence: the save happened, and the reloaded file agrees. */
        const std::string hdd = vfs_root + "/dev_hdd0/home/00000001/trophy/" + g_trp.trp_name;
        Tropusr saved;
        CHECK(saved.load(read_host(hdd + "/TROPUSR.DAT")),
              "TROPUSR.DAT was rewritten by UnlockTrophy (:1140-1144)");
        CHECK(saved.unlock_state(0) == 1 && saved.unlock_state(3) == 1 && saved.unlock_state(4) == 1,
              "the unlocked state survived the save/load round trip");
        CHECK(saved.timestamp(0) != 0, "and so did a non-zero timestamp (:1115)");
    }

    /* --- 14. GetTrophyInfo ------------------------------------------------ */
    {
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, context, live, 128, details_ea, data_ea) ==
                  ERR_INVALID_TROPHY_ID,
              "trophyId > 127 is INVALID_TROPHY_ID before anything else (:1376-1379)");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, context, live, -1, details_ea, data_ea) ==
                  ERR_INVALID_TROPHY_ID,
              "a negative trophyId is INVALID_TROPHY_ID as well");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, context, live, 0, 0, 0) ==
                  ERR_INVALID_ARGUMENT,
              "GetTrophyInfo(NULL, NULL) is INVALID_ARGUMENT (:1261-1264)");

        /* Trophy 2 (silver, hidden="no", unlocked by section 13). */
        std::memset(vm::ptr(details_ea), 0xCD, sizeof(TrophyDetails));
        std::memset(vm::ptr(data_ea), 0xCD, sizeof(TrophyData));
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, context, live, 2, details_ea, data_ea) == 0,
              "GetTrophyInfo succeeds for a visible trophy");
        CHECK(vm::read32(details_ea + 0) == 2, "details->trophyId is the id asked for");
        CHECK(vm::read32(details_ea + 4) == GRADE_SILVER, "and trophyGrade is SILVER for ttype S");
        CHECK(std::string(reinterpret_cast<const char*>(vm::ptr(details_ea + offsetof(TrophyDetails, name)))) ==
                  "Silver",
              "the <name> child reaches details->name");
        CHECK(std::string(reinterpret_cast<const char*>(vm::ptr(details_ea + offsetof(TrophyDetails, description)))) ==
                  "D2",
              "and <detail> reaches details->description");
        CHECK(vm::ptr(details_ea + offsetof(TrophyDetails, hidden))[0] == 0,
              "hidden is false for hidden=\"no\"");
        CHECK(vm::read64(data_ea + 0) != 0, "data->timestamp is the stored tick");
        CHECK(vm::read32(data_ea + 8) == 2, "data->trophyId round-trips");
        CHECK(vm::ptr(data_ea + offsetof(TrophyData, unlocked))[0] == 1, "data->unlocked is 1");

        /* An id that is inside 0..127 but absent from the config. */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, context, live, 120, details_ea, data_ea) ==
                  ERR_INVALID_TROPHY_ID,
              "an id the config does not describe is INVALID_TROPHY_ID (:1354-1357)");

        /* A context that never registered. The pool is 4 and full, so free one
         * first -- which is also the check that DestroyContext really frees. */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyContext, ID_BASE + 1) == 0,
              "DestroyContext frees a slot");
        fill_comm_id(comm_id_ea, "BLES00680", 1);
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyCreateContext, context_ea, comm_id_ea, sign_ea, 0) == 0,
              "a context for an unregistered communication id");
        const uint32_t unregistered = vm::read32(context_ea);
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo, unregistered, live, 0, details_ea, data_ea) ==
                  ERR_CONTEXT_NOT_REGISTERED,
              "GetTrophyInfo on it is CONTEXT_NOT_REGISTERED (:1266-1270)");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy, unregistered, live, 0, platinum_ea) ==
                  ERR_CONTEXT_NOT_REGISTERED,
              "and so is UnlockTrophy (:1088-1092)");
    }

    /* --- 15. the hidden-trophy rule -------------------------------------- */
    {
        TrophyConf conf;
        CHECK(conf.parse(bytes_of(kConf)), "the test config parses");
        CHECK(conf.trophies.size() == 5, "and describes five trophies");
        Tropusr fresh;
        CHECK(fresh.generate(conf), "a fresh TROPUSR is generated from it");
        TrophyDetails d{};
        TrophyData dt{};
        CHECK(trophy_info(fresh, conf, 1, &d, &dt) == ERR_HIDDEN,
              "a hidden, locked trophy answers SCE_NP_TROPHY_ERROR_HIDDEN");
        CHECK(fresh.unlock(1, 1, 1), "unlocking it in the table");
        CHECK(trophy_info(fresh, conf, 1, &d, &dt) == 0, "an unlocked hidden trophy answers CELL_OK");
        CHECK(d.hidden == 1, "with details->hidden still set");
        CHECK(trophy_info(fresh, conf, 99, &d, &dt) == ERR_INVALID_TROPHY_ID,
              "an absent id is INVALID_TROPHY_ID");
        CHECK(trophy_info(fresh, conf, 0, nullptr, nullptr) == ERR_INVALID_ARGUMENT,
              "no output pointer at all is INVALID_ARGUMENT");
    }

    /* --- 16. the save/load round trip is byte-exact ----------------------- */
    {
        TrophyConf conf;
        conf.parse(bytes_of(kConf));
        Tropusr a;
        a.generate(conf);
        a.unlock(2, 1234, 5678);
        const std::vector<uint8_t> bytes = a.save();
        Tropusr b;
        CHECK(b.load(bytes), "a saved TROPUSR loads back");
        CHECK(b.count() == a.count(), "with the same trophy count");
        CHECK(b.timestamp(2) == 5678, "and the same timestamp (timestamp2, TROPUSR.cpp:354-364)");
        CHECK(b.grade(2) == a.grade(2), "and the same grade");
        CHECK(b.header().magic == TROPUSR_MAGIC, "and the file magic (TROPUSR.cpp:9)");
        CHECK(b.header().tables_count == 2, "two tables, type 4 and type 6 (:216-223)");
        CHECK(bytes.size() == sizeof(TropusrHeader) + 2 * sizeof(TropusrTableHeader) +
                                   a.count() * (sizeof(TropusrEntry4) + sizeof(TropusrEntry6)),
              "the file is header + table headers + the two tables, with no padding");
    }

    /* --- 17. Term wipes the state ---------------------------------------- */
    {
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyTerm) == 0, "Term succeeds");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo, context, live, details_ea, data_ea) ==
                  ERR_NOT_INITIALIZED,
              "after Term every checked call is NOT_INITIALIZED (:953-956)");
        /* DestroyHandle has no init check at all (:371-375): after Term it still
         * answers, with UNKNOWN_HANDLE because Term cleared the pool (:326-327),
         * and NOT with NOT_INITIALIZED. */
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle, live) == ERR_UNKNOWN_HANDLE,
              "DestroyHandle keeps answering after Term -- UNKNOWN_HANDLE, not NOT_INITIALIZED");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyInit, 0, 0, 0, 0) == 0, "Init again");
        CHECK(call(hle::hle_sceNpTrophy_sceNpTrophyDestroyContext, context) == ERR_UNKNOWN_CONTEXT,
              "a context id from before Term is UNKNOWN_CONTEXT, not a stale handle");
    }

    /* --- 18. the registration names the dispatcher looks up ---------------
     * The dispatcher builds "module::name" from the import table, so a typo
     * here resolves the import nowhere and the handler never runs. */
    {
        struct Expected { const char* name; hle::Fn fn; };
        const Expected expected[] = {
            {"sceNpTrophyInit", &hle::hle_sceNpTrophy_sceNpTrophyInit},
            {"sceNpTrophyTerm", &hle::hle_sceNpTrophy_sceNpTrophyTerm},
            {"sceNpTrophyCreateHandle", &hle::hle_sceNpTrophy_sceNpTrophyCreateHandle},
            {"sceNpTrophyDestroyHandle", &hle::hle_sceNpTrophy_sceNpTrophyDestroyHandle},
            {"sceNpTrophyAbortHandle", &hle::hle_sceNpTrophy_sceNpTrophyAbortHandle},
            {"sceNpTrophyCreateContext", &hle::hle_sceNpTrophy_sceNpTrophyCreateContext},
            {"sceNpTrophyDestroyContext", &hle::hle_sceNpTrophy_sceNpTrophyDestroyContext},
            {"sceNpTrophyRegisterContext", &hle::hle_sceNpTrophy_sceNpTrophyRegisterContext},
            {"sceNpTrophyGetRequiredDiskSpace", &hle::hle_sceNpTrophy_sceNpTrophyGetRequiredDiskSpace},
            {"sceNpTrophyGetGameInfo", &hle::hle_sceNpTrophy_sceNpTrophyGetGameInfo},
            {"sceNpTrophyUnlockTrophy", &hle::hle_sceNpTrophy_sceNpTrophyUnlockTrophy},
            {"sceNpTrophyGetTrophyInfo", &hle::hle_sceNpTrophy_sceNpTrophyGetTrophyInfo},
        };
        CHECK(sizeof(expected) / sizeof(expected[0]) == 12,
              "the twelve functions this title imports (out-rdr/ppu/table.cpp:81831-81842)");
        for (const auto& e : expected) {
            const std::string key = std::string("sceNpTrophy::") + e.name;
            const auto it = test_registry().find(key);
            CHECK(it != test_registry().end(), key.c_str());
            CHECK(it != test_registry().end() && it->second == e.fn,
                  (key + " maps to this handler").c_str());
        }
    }

    std::error_code ec;
    fs::remove_all(tmp_root, ec);

    std::printf("nptrophy: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
