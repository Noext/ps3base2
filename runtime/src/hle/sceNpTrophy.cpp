#include "common.h"

#include "nptrophy_hle.h"

#include "runtime/sysutil_events.h"
#include "runtime/vfs.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

/* sceNpTrophy: trophies.
 *
 * The twelve functions this title imports (out-rdr/ppu/table.cpp:81831-81842),
 * each written against RPCS3, whose return codes and writes are the
 * specification (AGENTS.md, "An implemented command answers what the console
 * answers"):
 *
 *   sceNpTrophyInit                sceNpTrophy.cpp:290-311
 *   sceNpTrophyTerm                sceNpTrophy.cpp:313-332
 *   sceNpTrophyCreateHandle        sceNpTrophy.cpp:334-361
 *   sceNpTrophyDestroyHandle       sceNpTrophy.cpp:363-389
 *   sceNpTrophyAbortHandle         sceNpTrophy.cpp:397-428
 *   sceNpTrophyCreateContext       sceNpTrophy.cpp:430-526
 *   sceNpTrophyDestroyContext      sceNpTrophy.cpp:528-553
 *   sceNpTrophyRegisterContext     sceNpTrophy.cpp:560-837
 *   sceNpTrophyGetRequiredDiskSpace sceNpTrophy.cpp:839-910
 *   sceNpTrophyGetGameInfo         sceNpTrophy.cpp:945-1060
 *   sceNpTrophyUnlockTrophy        sceNpTrophy.cpp:1068-1168
 *   sceNpTrophyGetTrophyInfo       sceNpTrophy.cpp:1372-1398 (and NpTrophyGetTrophyInfo :1259-1370)
 *
 * What an offline console does, and therefore what this does:
 *
 *   - Init, Term, CreateHandle, DestroyHandle, AbortHandle, CreateContext and
 *     DestroyContext are LOCAL. Trophies live under /dev_hdd0/home/<usr>/trophy
 *     and the context name comes from the title's own communication id; no
 *     network is involved. They succeed, exactly as RPCS3 succeeds. Reporting
 *     an "offline" error here would tell the title its trophy state was never
 *     created, and every later call would answer with a state error that has
 *     nothing to do with the truth.
 *   - RegisterContext, GetGameInfo, UnlockTrophy, GetTrophyInfo and
 *     GetRequiredDiskSpace need the installed trophy data, which comes from
 *     TROPHY.TRP on the disc. When that file or the installed copy is absent,
 *     RPCS3 itself answers SCE_NP_TROPHY_ERROR_CONF_DOES_NOT_EXIST
 *     (:614, :887) or SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED (:968, :1091,
 *     :1221, :1269). Those are the console's answers, not invented refusals.
 *   - The RPCN synchronisation in RegisterContext (:723-758) and the popup
 *     notification (:1146-1156) are emulator-side services this runtime does
 *     not have. They are absent by design, not faked: the first is a network
 *     login this runtime never performs, the second a UI overlay.
 *
 * The state machine, the file formats and every branch decision live in
 * nptrophy_hle.h so tests/nptrophy.cpp drives the same code the imports run.
 */

namespace hle {

namespace {

using namespace trophy;

/* The user whose trophy directory is used. RPCS3 composes
 * "/dev_hdd0/home/" + Emu.GetUsr() + "/trophy/" (:77, :600, :622, ...); the
 * runtime's VFS creates home/00000001 (runtime/src/vfs.cpp:98), the same
 * default user cellSaveData uses (runtime/src/hle/cellSaveData.cpp:154). */
const char* kUser = "00000001";

/* cellRtc ticks are microseconds since 0001-01-01, which is 1970-01-01 plus
 * 62135596800000000 ticks. Same constant and same reasoning as
 * runtime/src/hle/cellRtc.cpp:17-19, which is what cellRtcGetCurrentTick
 * returns (refs/.../cellRtc.cpp); RPCS3 reads the tick in UnlockTrophy through
 * cellRtcGetCurrentTick (:1109-1113). */
constexpr uint64_t RTC_MAGIC_OFFSET = 62135596800000000ull;

uint64_t now_ticks() {
    return static_cast<uint64_t>(std::time(nullptr)) * 1000000ull + RTC_MAGIC_OFFSET;
}

/* One state machine for the whole module, as RPCS3's sce_np_trophy_manager is
 * one g_fxo object (sceNpTrophy.cpp:112-186). The lock is this runtime's
 * substitute for its shared_mutex: the guest is single-threaded here, but a
 * callback delivered from the sysutil queue still re-enters. */
Manager& manager() {
    static Manager m;
    return m;
}

std::mutex& mtx() {
    static std::mutex m;
    return m;
}

std::string trophy_dir(const std::string& trp_name) {
    return std::string("/dev_hdd0/home/") + kUser + "/trophy/" + trp_name;
}

/* TROPDIR lookup. RPCS3 tries Emu.GetDir()+"TROPDIR/"... and, for a disc title
 * (Emu.GetCat() == "GD"), falls back to /dev_bdvd/PS3_GAME/TROPDIR/...
 * (:600-608, :874-882). This runtime boots a disc image, so the boot path IS
 * the disc path; both spellings are tried so a patch directory that carries its
 * own TROPDIR is found by the first one once the profile points there. */
bool read_host(const std::string& host, std::vector<uint8_t>& out) {
    if (host.empty()) return false;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(host, ec)) return false;
    std::ifstream f(host, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size) f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good() || f.eof();
}

bool read_guest_file(const std::string& guest, std::vector<uint8_t>& out) {
    return read_host(vfs::host_path(guest), out);
}

bool is_dir(const std::string& guest) {
    const std::string host = vfs::host_path(guest);
    if (host.empty()) return false;
    std::error_code ec;
    return std::filesystem::is_directory(host, ec);
}

bool write_guest_file(const std::string& guest, const std::vector<uint8_t>& bytes) {
    const std::string host = vfs::host_path(guest);
    if (host.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(host).parent_path(), ec);
    std::ofstream f(host, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()),
                                static_cast<std::streamsize>(bytes.size()));
    return f.good();
}

/* Open TROPHY.TRP the way RPCS3 does, from the boot directory's TROPDIR
 * (sceNpTrophy.cpp:600-608). RPCS3 has two spellings because it can boot from
 * an HDD copy; this runtime always boots the disc, where GetDir() is
 * "/dev_bdvd/PS3_GAME/" (System.cpp:1710, :1972, :2690), so the two coincide
 * and the GD fallback is the same path. */
bool read_trp(const std::string& trp_name, std::vector<uint8_t>& out) {
    return read_guest_file("/dev_bdvd/PS3_GAME/TROPDIR/" + trp_name + "/TROPHY.TRP", out);
}

/* Load the installed trophy data of a context: TROPUSR.DAT next to
 * TROPCONF.SFM. When TROPUSR.DAT is missing or corrupt, RPCS3 regenerates it
 * from TROPCONF.SFM (TROPUSR.cpp:34-75, :163-232). */
bool load_installed(Context& c) {
    const std::string dir = trophy_dir(c.trp_name);
    std::vector<uint8_t> dat;
    if (read_guest_file(dir + "/TROPUSR.DAT", dat) && c.tropusr.load(dat)) {
        c.registered = true;
        return true;
    }

    std::vector<uint8_t> conf_bytes;
    if (!read_guest_file(dir + "/TROPCONF.SFM", conf_bytes)) return false;
    TrophyConf conf;
    if (!conf.parse(conf_bytes)) return false;

    if (!c.tropusr.generate(conf)) return false;
    /* Best effort, as RPCS3's generate path is: the data is usable in memory
     * even when the write fails, and the caller is not told otherwise. */
    write_guest_file(dir + "/TROPUSR.DAT", c.tropusr.save());
    c.registered = true;
    return true;
}

void write_game_details(uint32_t ea, const GameDetails& d) {
    vm::write32(ea + 0, d.numTrophies);
    vm::write32(ea + 4, d.numPlatinum);
    vm::write32(ea + 8, d.numGold);
    vm::write32(ea + 12, d.numSilver);
    vm::write32(ea + 16, d.numBronze);
    std::memcpy(vm::ptr(ea + offsetof(GameDetails, title)), d.title, TITLE_MAX_SIZE);
    std::memcpy(vm::ptr(ea + offsetof(GameDetails, description)), d.description, GAME_DESCR_MAX_SIZE);
    std::memset(vm::ptr(ea + offsetof(GameDetails, reserved)), 0, sizeof(d.reserved));
}

void write_game_data(uint32_t ea, const GameData& d) {
    vm::write32(ea + 0, d.unlockedTrophies);
    vm::write32(ea + 4, d.unlockedPlatinum);
    vm::write32(ea + 8, d.unlockedGold);
    vm::write32(ea + 12, d.unlockedSilver);
    vm::write32(ea + 16, d.unlockedBronze);
}

void write_trophy_details(uint32_t ea, const TrophyDetails& d) {
    vm::write32(ea + 0, static_cast<uint32_t>(d.trophyId));
    vm::write32(ea + 4, d.trophyGrade);
    std::memcpy(vm::ptr(ea + offsetof(TrophyDetails, name)), d.name, NAME_MAX_SIZE);
    std::memcpy(vm::ptr(ea + offsetof(TrophyDetails, description)), d.description, DESCR_MAX_SIZE);
    vm::ptr(ea + offsetof(TrophyDetails, hidden))[0] = d.hidden;
    std::memset(vm::ptr(ea + offsetof(TrophyDetails, reserved)), 0, sizeof(d.reserved));
}

void write_trophy_data(uint32_t ea, const TrophyData& d) {
    vm::write64(ea + 0, d.timestamp);
    vm::write32(ea + 8, static_cast<uint32_t>(d.trophyId));
    vm::ptr(ea + offsetof(TrophyData, unlocked))[0] = d.unlocked;
    std::memset(vm::ptr(ea + offsetof(TrophyData, reserved)), 0, sizeof(d.reserved));
}

/* Extract an already-selected TRP. The selection itself (prepare_entries) is
 * in the header so the test drives it; this is only the file work, against
 * refs/.../Loader/TRP.cpp:21-107. */
bool install_trp(const std::string& dest_guest, const std::vector<uint8_t>& trp_bytes,
                 uint32_t language) {
    Trp trp;
    if (!trp.load(trp_bytes)) return false;
    if (prepare_entries(trp, language) != EntryPlan::Ok) return false;

    const std::string dest = vfs::host_path(dest_guest);
    if (dest.empty()) return false;

    const std::string temp = dest + ".tmp";
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    std::filesystem::create_directories(temp, ec);
    if (!std::filesystem::is_directory(temp, ec)) return false;

    /* TRP.cpp:41-45: carry TROPUSR.DAT over so already-earned trophies survive
     * a reinstall. */
    std::filesystem::copy_file(dest + "/TROPUSR.DAT", temp + "/TROPUSR.DAT",
                               std::filesystem::copy_options::skip_existing, ec);

    bool ok = true;
    for (const auto& e : trp.entries()) {
        const std::string name = Trp::entry_name(e);
        const uint64_t first = e.offset;
        const uint64_t last = e.offset + e.size;
        if (last > trp_bytes.size() || first > last) { ok = false; break; }
        const std::string path = temp + "/" + name;
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) { ok = false; break; }
        if (e.size) f.write(reinterpret_cast<const char*>(trp_bytes.data() + first),
                            static_cast<std::streamsize>(e.size));
        if (!f.good()) { ok = false; break; }
    }

    if (ok) {
        /* TRP.cpp:79-96: replace the old directory only when every entry was
         * written, so a partial extract never destroys working data. */
        std::filesystem::remove_all(dest, ec);
        std::filesystem::rename(temp, dest, ec);
        ok = !ec;
    }
    if (!ok) std::filesystem::remove_all(temp, ec);
    return ok;
}

}  // namespace

/* sceNpTrophyInit(pool, poolSize, containerId, options)
 * refs/rpcs3/rpcs3/Emu/Cell/Modules/sceNpTrophy.cpp:290-311. */
HLE_FUNC(sceNpTrophy, sceNpTrophyInit) {
    const uint64_t options = a64(ctx, 3);
    std::lock_guard lk(mtx());
    const int32_t rc = manager().init(options);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyInit(options=0x%llx) -> 0x%08x",
                 static_cast<unsigned long long>(options), static_cast<uint32_t>(rc));
    }
    ret(ctx, rc);
}

/* sceNpTrophyTerm()
 * refs/.../sceNpTrophy.cpp:313-332. */
HLE_FUNC(sceNpTrophy, sceNpTrophyTerm) {
    std::lock_guard lk(mtx());
    const int32_t rc = manager().term();
    if (rc) LOG_WARN("sceNpTrophy", "sceNpTrophyTerm() -> 0x%08x", static_cast<uint32_t>(rc));
    ret(ctx, rc);
}

/* sceNpTrophyCreateHandle(handle)
 * refs/.../sceNpTrophy.cpp:334-361. */
HLE_FUNC(sceNpTrophy, sceNpTrophyCreateHandle) {
    const uint32_t handle_ea = a32(ctx, 0);
    std::lock_guard lk(mtx());
    if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
    if (!handle_ea || !vm::range_mapped(handle_ea, 4)) return ret(ctx, ERR_INVALID_ARGUMENT);

    uint32_t id = 0;
    const int32_t rc = manager().create_handle(id);
    if (rc) return ret(ctx, rc);
    vm::write32(handle_ea, id);
    LOG_INFO("sceNpTrophy", "sceNpTrophyCreateHandle -> handle 0x%x", id);
    ret(ctx, 0);
}

/* sceNpTrophyDestroyHandle(handle)
 * refs/.../sceNpTrophy.cpp:363-389. RPCS3's is_initialized check is commented
 * out there ("TODO: find out if this is checked"), so this answers without
 * one too. */
HLE_FUNC(sceNpTrophy, sceNpTrophyDestroyHandle) {
    const uint32_t handle = a32(ctx, 0);
    std::lock_guard lk(mtx());
    const int32_t rc = manager().destroy_handle(handle);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyDestroyHandle(0x%x) -> 0x%08x", handle,
                 static_cast<uint32_t>(rc));
    }
    ret(ctx, rc);
}

/* sceNpTrophyAbortHandle(handle)
 * refs/.../sceNpTrophy.cpp:397-428. */
HLE_FUNC(sceNpTrophy, sceNpTrophyAbortHandle) {
    const uint32_t handle = a32(ctx, 0);
    std::lock_guard lk(mtx());
    const int32_t rc = manager().abort_handle(handle);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyAbortHandle(0x%x) -> 0x%08x", handle,
                 static_cast<uint32_t>(rc));
    }
    ret(ctx, rc);
}

/* sceNpTrophyCreateContext(context, commId, commSign, options)
 * refs/.../sceNpTrophy.cpp:430-526. */
HLE_FUNC(sceNpTrophy, sceNpTrophyCreateContext) {
    const uint32_t context_ea = a32(ctx, 0);
    const uint32_t comm_id_ea = a32(ctx, 1);
    const uint32_t comm_sign_ea = a32(ctx, 2);
    const uint64_t options = a64(ctx, 3);

    /* A null commSign is INVALID_ARGUMENT before anything else, because RPCS3
     * dereferences it for the notice line (:434-439). */
    if (!comm_sign_ea || !vm::range_mapped(comm_sign_ea, sizeof(Signature)))
        return ret(ctx, ERR_INVALID_ARGUMENT);

    CommId comm_id{};
    const bool have_id = comm_id_ea && vm::range_mapped(comm_id_ea, sizeof(CommId));
    if (have_id) {
        std::memcpy(comm_id.data, vm::ptr(comm_id_ea), sizeof(comm_id.data));
        comm_id.num = vm::ptr(comm_id_ea)[10];
        comm_id.dummy = static_cast<char>(vm::ptr(comm_id_ea)[11]);
    }

    Signature signature{};
    std::memcpy(signature.data, vm::ptr(comm_sign_ea), sizeof(signature.data));

    std::lock_guard lk(mtx());
    if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
    if (!context_ea || !comm_id_ea) return ret(ctx, ERR_INVALID_ARGUMENT);
    if (!vm::range_mapped(context_ea, 4)) return ret(ctx, ERR_INVALID_ARGUMENT);

    uint32_t out = 0;
    const int32_t rc = manager().create_context(out, comm_id, signature, options);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyCreateContext(commId='%s' num=%u) -> 0x%08x",
                 std::string(comm_id.data, strnlen(comm_id.data, 10)).c_str(),
                 static_cast<unsigned>(comm_id.num), static_cast<uint32_t>(rc));
        return ret(ctx, rc);
    }
    vm::write32(context_ea, out);
    LOG_INFO("sceNpTrophy", "sceNpTrophyCreateContext -> context 0x%x (trp '%s', read_only %d)",
             out, manager().context(out)->trp_name.c_str(),
             manager().context(out)->read_only ? 1 : 0);
    ret(ctx, 0);
}

/* sceNpTrophyDestroyContext(context)
 * refs/.../sceNpTrophy.cpp:528-553. */
HLE_FUNC(sceNpTrophy, sceNpTrophyDestroyContext) {
    const uint32_t context = a32(ctx, 0);
    std::lock_guard lk(mtx());
    const int32_t rc = manager().destroy_context(context);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyDestroyContext(0x%x) -> 0x%08x", context,
                 static_cast<uint32_t>(rc));
    }
    ret(ctx, rc);
}

/* sceNpTrophyRegisterContext(context, handle, statusCb, arg, options)
 * refs/.../sceNpTrophy.cpp:560-837.
 *
 * The callback is called once SYNCHRONOUSLY with the install status, and its
 * return decides whether registration proceeds (:631-635). Everything after
 * that is a status sequence delivered through the sysutil callback queue, one
 * status per cellSysutilCheckCallback call (:794-805). RPCS3 also spaces the
 * sequence out over two seconds (:807-830) to imitate VSH's cadence; that is
 * pacing, not content, and it is not reproduced here. */
HLE_FUNC(sceNpTrophy, sceNpTrophyRegisterContext) {
    const uint32_t context = a32(ctx, 0);
    const uint32_t handle = a32(ctx, 1);
    const uint32_t status_cb = a32(ctx, 2);
    const uint32_t arg = a32(ctx, 3);
    const uint64_t options = a64(ctx, 4);

    std::string trp_name;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);

        Context* c = nullptr;
        const int32_t rc = manager().get_context_ex(context, handle, true, &c);
        if (rc) return ret(ctx, rc);
        if (!status_cb) return ret(ctx, ERR_INVALID_ARGUMENT);
        if (options > OPTIONS_REGISTER_CONTEXT_SHOW_ERROR_EXIT) return ret(ctx, ERR_NOT_SUPPORTED);
        trp_name = c->trp_name;
    }

    std::vector<uint8_t> trp_bytes;
    if (!read_trp(trp_name, trp_bytes)) {
        LOG_WARN("sceNpTrophy",
                 "sceNpTrophyRegisterContext: no TROPHY.TRP for '%s' -> "
                 "SCE_NP_TROPHY_ERROR_CONF_DOES_NOT_EXIST",
                 trp_name.c_str());
        return ret(ctx, ERR_CONF_DOES_NOT_EXIST);
    }

    const bool installed = is_dir(trophy_dir(trp_name));
    const uint32_t trp_status = installed ? STATUS_INSTALLED : STATUS_NOT_INSTALLED;

    /* The one synchronous call. A negative answer aborts the registration. */
    hle::call_guest(ctx, status_cb, context, trp_status, 0, 0, arg);
    const int32_t cb_rc = static_cast<int32_t>(ctx.gpr[3] & 0xFFFFFFFFll);
    if (cb_rc < 0) {
        LOG_WARN("sceNpTrophy",
                 "sceNpTrophyRegisterContext: status callback returned %d -> "
                 "SCE_NP_TROPHY_ERROR_PROCESSING_ABORTED",
                 cb_rc);
        return ret(ctx, ERR_PROCESSING_ABORTED);
    }

    /* The callback may have called back into this module. Re-check everything,
     * exactly as RPCS3 does (:641-667). */
    uint32_t trophies = 0;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
        Context* c = nullptr;
        const int32_t rc = manager().get_context_ex(context, handle, false, &c);
        if (rc) return ret(ctx, rc);
        if (c->trp_name != trp_name) return ret(ctx, ERR_UNKNOWN_CONTEXT);

        Trp trp;
        if (!trp.load(trp_bytes)) {
            LOG_ERROR("sceNpTrophy",
                      "sceNpTrophyRegisterContext: TROPHY.TRP header is invalid (%s)", trp_name.c_str());
            return ret(ctx, ERR_ILLEGAL_UPDATE);
        }
        if (prepare_entries(trp, language()) != EntryPlan::Ok) {
            LOG_ERROR("sceNpTrophy",
                      "sceNpTrophyRegisterContext: invalid/incomplete trophy config (%s)",
                      trp_name.c_str());
            return ret(ctx, ERR_ILLEGAL_UPDATE);
        }

        if (!install_trp(trophy_dir(trp_name), trp_bytes, language()) ||
            !load_installed(*c)) {
            LOG_ERROR("sceNpTrophy",
                      "sceNpTrophyRegisterContext: failed to install '%s' -> "
                      "SCE_NP_TROPHY_ERROR_ILLEGAL_UPDATE",
                      trp_name.c_str());
            c->registered = false;
            return ret(ctx, ERR_ILLEGAL_UPDATE);
        }
        trophies = c->tropusr.count();
    }

    LOG_INFO("sceNpTrophy", "sceNpTrophyRegisterContext: '%s' registered (%u trophies, status %u)",
             trp_name.c_str(), trophies, trp_status);

    /* The status sequence. Each callback runs on the guest thread when it next
     * calls cellSysutilCheckCallback, which is where a firmware delivery lands
     * too -- never on the calling thread, never in the middle of this
     * function's own guest stack frame. */
    for (const auto& st : register_statuses(trophies)) {
        const uint32_t status = st.status;
        const int32_t completed = st.completed;
        const int32_t total = st.total;
        sysutil_post([status_cb, context, status, completed, total, arg](ppu::Context& c) {
            call_guest(c, status_cb, context, status, static_cast<uint64_t>(completed),
                       static_cast<uint64_t>(total), arg);
        });
    }

    ret(ctx, 0);
}

/* sceNpTrophyGetRequiredDiskSpace(context, handle, reqspace, options)
 * refs/.../sceNpTrophy.cpp:839-910. */
HLE_FUNC(sceNpTrophy, sceNpTrophyGetRequiredDiskSpace) {
    const uint32_t context = a32(ctx, 0);
    const uint32_t handle = a32(ctx, 1);
    const uint32_t reqspace_ea = a32(ctx, 2);
    const uint64_t options = a64(ctx, 3);

    if (!reqspace_ea || !vm::range_mapped(reqspace_ea, 8)) return ret(ctx, ERR_INVALID_ARGUMENT);
    if (options > 0) return ret(ctx, ERR_NOT_SUPPORTED);

    std::string trp_name;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
        Context* c = nullptr;
        const int32_t rc = manager().get_context_ex(context, handle, false, &c);
        if (rc) return ret(ctx, rc);
        trp_name = c->trp_name;
    }

    uint64_t space = 0;
    if (is_dir(trophy_dir(trp_name))) {
        /* Already installed: nothing left to write (RPCS3:901-904). */
    } else {
        std::vector<uint8_t> bytes;
        if (!read_trp(trp_name, bytes)) {
            LOG_WARN("sceNpTrophy",
                     "sceNpTrophyGetRequiredDiskSpace: no TROPHY.TRP for '%s' -> "
                     "SCE_NP_TROPHY_ERROR_CONF_DOES_NOT_EXIST",
                     trp_name.c_str());
            return ret(ctx, ERR_CONF_DOES_NOT_EXIST);
        }
        Trp trp;
        if (trp.load(bytes)) {
            space = trp.required_space();
        } else {
            LOG_ERROR("sceNpTrophy",
                      "sceNpTrophyGetRequiredDiskSpace: invalid TROPHY.TRP header (%s)",
                      trp_name.c_str());
        }
    }

    vm::write64(reqspace_ea, space);
    LOG_INFO("sceNpTrophy", "sceNpTrophyGetRequiredDiskSpace -> 0x%llx",
             static_cast<unsigned long long>(space));
    ret(ctx, 0);
}

/* sceNpTrophyGetGameInfo(context, handle, details, data)
 * refs/.../sceNpTrophy.cpp:945-1060. */
HLE_FUNC(sceNpTrophy, sceNpTrophyGetGameInfo) {
    const uint32_t context = a32(ctx, 0);
    const uint32_t handle = a32(ctx, 1);
    const uint32_t details_ea = a32(ctx, 2);
    const uint32_t data_ea = a32(ctx, 3);

    Context* c = nullptr;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
        const int32_t rc = manager().get_context_ex(context, handle, false, &c);
        if (rc) return ret(ctx, rc);
        if (!c->registered) return ret(ctx, ERR_CONTEXT_NOT_REGISTERED);
    }
    if (!details_ea && !data_ea) return ret(ctx, ERR_INVALID_ARGUMENT);

    std::string trp_name;
    Tropusr tropusr;
    {
        std::lock_guard lk(mtx());
        trp_name = c->trp_name;
        tropusr = c->tropusr;
    }

    std::vector<uint8_t> conf_bytes;
    if (!read_guest_file(trophy_dir(trp_name) + "/TROPCONF.SFM", conf_bytes)) {
        LOG_WARN("sceNpTrophy",
                 "sceNpTrophyGetGameInfo: no TROPCONF.SFM for '%s' -> "
                 "SCE_NP_TROPHY_ERROR_CONF_DOES_NOT_EXIST",
                 trp_name.c_str());
        return ret(ctx, ERR_CONF_DOES_NOT_EXIST);
    }

    if (details_ea) std::memset(vm::ptr(details_ea), 0, sizeof(GameDetails));
    if (data_ea) std::memset(vm::ptr(data_ea), 0, sizeof(GameData));

    TrophyConf conf;
    if (!conf.parse(conf_bytes)) {
        /* RPCS3 logs and returns CELL_OK with the zeroed structures
         * (:990-997, "TODO: return some error"). */
        LOG_ERROR("sceNpTrophy", "sceNpTrophyGetGameInfo: TROPCONF.SFM did not parse (%s)",
                  trp_name.c_str());
        return ret(ctx, 0);
    }

    GameDetails details{};
    GameData data{};
    copy_trunc(details.title, sizeof(details.title), conf.title_name);
    copy_trunc(details.description, sizeof(details.description), conf.title_description);
    game_info(tropusr, conf, details_ea ? &details : nullptr, data_ea ? &data : nullptr);

    if (details_ea) write_game_details(details_ea, details);
    if (data_ea) write_game_data(data_ea, data);

    LOG_INFO("sceNpTrophy", "sceNpTrophyGetGameInfo: %u trophies, %u unlocked",
             details.numTrophies, data.unlockedTrophies);
    ret(ctx, 0);
}

/* sceNpTrophyUnlockTrophy(context, handle, trophyId, platinumId)
 * refs/.../sceNpTrophy.cpp:1068-1168. */
HLE_FUNC(sceNpTrophy, sceNpTrophyUnlockTrophy) {
    const uint32_t context = a32(ctx, 0);
    const uint32_t handle = a32(ctx, 1);
    const int32_t trophy_id = s32(ctx, 2);
    const uint32_t platinum_ea = a32(ctx, 3);

    Context* c = nullptr;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
        /* test_writeable = true: a read-only context refuses an unlock with
         * INVALID_CONTEXT (sceNpTrophy.cpp:1081, :139-143). */
        const int32_t rc = manager().get_context_ex(context, handle, true, &c);
        if (rc) return ret(ctx, rc);
        if (!c->registered) {
            LOG_WARN("sceNpTrophy",
                     "sceNpTrophyUnlockTrophy: context 0x%x is not registered -> "
                     "SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED",
                     context);
            return ret(ctx, ERR_CONTEXT_NOT_REGISTERED);
        }

        std::string trp_name = c->trp_name;

        /* The config is needed for the platinum link ids
         * (TROPUSRLoader::GetUnlockedPlatinumID reads it, TROPUSR.cpp:268-308).
         * Without it the platinum stays locked, which is RPCS3's behaviour when
         * the file is missing (TROPUSR.cpp:272-275 returns the invalid id). */
        std::vector<uint8_t> conf_bytes;
        TrophyConf conf;
        if (read_guest_file(trophy_dir(trp_name) + "/TROPCONF.SFM", conf_bytes))
            conf.parse(conf_bytes);

        const uint64_t tick = now_ticks();
        uint32_t platinum = INVALID_TROPHY_ID;
        const int32_t unlock_rc = unlock_trophy(c->tropusr, conf, trophy_id, tick, &platinum);
        if (unlock_rc) {
            LOG_WARN("sceNpTrophy", "sceNpTrophyUnlockTrophy(id=%d) -> 0x%08x", trophy_id,
                     static_cast<uint32_t>(unlock_rc));
            return ret(ctx, unlock_rc);
        }

        if (platinum != INVALID_TROPHY_ID) {
            LOG_INFO("sceNpTrophy",
                     "sceNpTrophyUnlockTrophy: platinum trophy %u unlocked as well", platinum);
        }
        if (platinum_ea && vm::range_mapped(platinum_ea, 4))
            vm::write32(platinum_ea, platinum);

        /* Persist. RPCS3 logs a failed save and still returns CELL_OK
         * (:1140-1144); the in-memory state is already correct. */
        if (!write_guest_file(trophy_dir(trp_name) + "/TROPUSR.DAT", c->tropusr.save())) {
            LOG_ERROR("sceNpTrophy", "sceNpTrophyUnlockTrophy: failed to save TROPUSR.DAT");
        }
        LOG_INFO("sceNpTrophy", "sceNpTrophyUnlockTrophy: trophy %d unlocked (tick 0x%llx)",
                 trophy_id, static_cast<unsigned long long>(tick));
    }
    ret(ctx, 0);
}

/* sceNpTrophyGetTrophyInfo(context, handle, trophyId, details, data)
 * refs/.../sceNpTrophy.cpp:1372-1398, then NpTrophyGetTrophyInfo :1259-1370. */
HLE_FUNC(sceNpTrophy, sceNpTrophyGetTrophyInfo) {
    const uint32_t context = a32(ctx, 0);
    const uint32_t handle = a32(ctx, 1);
    const int32_t trophy_id = s32(ctx, 2);
    const uint32_t details_ea = a32(ctx, 3);
    const uint32_t data_ea = a32(ctx, 4);

    /* "max 128 trophies" (sceNpTrophy.cpp:1376-1379). */
    if (trophy_id < 0 || trophy_id > 127) return ret(ctx, ERR_INVALID_TROPHY_ID);

    Context* c = nullptr;
    {
        std::lock_guard lk(mtx());
        if (!manager().initialized()) return ret(ctx, ERR_NOT_INITIALIZED);
        const int32_t rc = manager().get_context_ex(context, handle, false, &c);
        if (rc) return ret(ctx, rc);
    }

    std::string trp_name;
    Tropusr tropusr;
    bool registered = false;
    {
        std::lock_guard lk(mtx());
        trp_name = c->trp_name;
        tropusr = c->tropusr;
        registered = c->registered;
    }

    if (!details_ea && !data_ea) return ret(ctx, ERR_INVALID_ARGUMENT);
    if (!registered) {
        LOG_WARN("sceNpTrophy",
                 "sceNpTrophyGetTrophyInfo: context 0x%x is not registered -> "
                 "SCE_NP_TROPHY_ERROR_CONTEXT_NOT_REGISTERED",
                 context);
        return ret(ctx, ERR_CONTEXT_NOT_REGISTERED);
    }

    std::vector<uint8_t> conf_bytes;
    if (!read_guest_file(trophy_dir(trp_name) + "/TROPCONF.SFM", conf_bytes)) {
        LOG_WARN("sceNpTrophy",
                 "sceNpTrophyGetTrophyInfo: no TROPCONF.SFM for '%s' -> "
                 "SCE_NP_TROPHY_ERROR_CONF_DOES_NOT_EXIST",
                 trp_name.c_str());
        return ret(ctx, ERR_CONF_DOES_NOT_EXIST);
    }

    TrophyConf conf;
    if (!conf.parse(conf_bytes)) {
        /* RPCS3 logs the parse failure, then finds no matching trophy and
         * returns INVALID_TROPHY_ID (:1285-1297, :1354-1357). */
        LOG_ERROR("sceNpTrophy", "sceNpTrophyGetTrophyInfo: TROPCONF.SFM did not parse (%s)",
                  trp_name.c_str());
        return ret(ctx, ERR_INVALID_TROPHY_ID);
    }

    TrophyDetails details{};
    TrophyData data{};
    const int32_t rc = trophy_info(tropusr, conf, trophy_id,
                                   details_ea ? &details : nullptr,
                                   data_ea ? &data : nullptr);
    if (rc) {
        LOG_WARN("sceNpTrophy", "sceNpTrophyGetTrophyInfo(id=%d) -> 0x%08x", trophy_id,
                 static_cast<uint32_t>(rc));
        return ret(ctx, rc);
    }

    if (details_ea) write_trophy_details(details_ea, details);
    if (data_ea) write_trophy_data(data_ea, data);
    ret(ctx, 0);
}

}  // namespace hle
