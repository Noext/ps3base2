#include "common.h"
#include "runtime/force.h"
#include "runtime/vfs.h"
#include "runtime/compat.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace fsys = std::filesystem;

namespace hle {

namespace {

constexpr int32_t ERR_CBRESULT = static_cast<int32_t>(0x8002b401);
constexpr int32_t ERR_ACCESS = static_cast<int32_t>(0x8002b402);
constexpr int32_t ERR_PARAM = static_cast<int32_t>(0x8002b404);
constexpr int32_t ERR_NODATA = static_cast<int32_t>(0x8002b40b);

constexpr int32_t CB_OK_LAST = 1;
constexpr int32_t CB_OK_NEXT = 0;
constexpr int32_t CB_OK_LAST_NOCONFIRM = 2;

constexpr uint32_t OP_READ = 0;
constexpr uint32_t OP_WRITE = 1;
constexpr uint32_t OP_DELETE = 2;
constexpr uint32_t OP_WRITE_NOTRUNC = 3;

constexpr uint32_t CB_RESULT = 0x0;
constexpr uint32_t STAT_GET = 0x100;
constexpr uint32_t STAT_SET = 0x800;
constexpr uint32_t FILE_GET = 0x900;
constexpr uint32_t FILE_SET = 0xA00;
constexpr uint32_t SCRATCH_SIZE = 0x1000;

constexpr uint32_t SG_FREE_KB = 0x0;
constexpr uint32_t SG_IS_NEW = 0x4;
constexpr uint32_t SG_DIR = 0x8;
constexpr uint32_t SG_PARAM = 0x40;
constexpr uint32_t SG_BIND = 0x650;
constexpr uint32_t SG_SIZE_KB = 0x654;
constexpr uint32_t SG_SYS_SIZE_KB = 0x658;
constexpr uint32_t SG_FILE_NUM = 0x65C;
constexpr uint32_t SG_FILE_LIST_NUM = 0x660;
constexpr uint32_t SG_FILE_LIST = 0x664;
constexpr uint32_t SG_SIZE = 0x6A8;

constexpr uint32_t P_TITLE = 0x0;
constexpr uint32_t P_SUBTITLE = 0x80;
constexpr uint32_t P_DETAIL = 0x100;
constexpr uint32_t P_ATTRIBUTE = 0x500;
constexpr uint32_t P_LIST_PARAM = 0x508;

std::mutex g_lock;
uint32_t g_scratch = 0;

const char* content_name(uint32_t type) {
    switch (type) {
    case 2: return "ICON0.PNG";
    case 3: return "ICON1.PAM";
    case 4: return "PIC1.PNG";
    case 5: return "SND0.AT3";
    default: return nullptr;
    }
}

uint32_t content_type(const std::string& name) {
    if (name == "ICON0.PNG") return 2;
    if (name == "ICON1.PAM") return 3;
    if (name == "PIC1.PNG") return 4;
    if (name == "SND0.AT3") return 5;
    return 1;
}

struct PsfEntry {
    uint16_t fmt;
    uint32_t max;
    std::string str;
    uint32_t num;
};

void write_psf(const std::string& path, const std::map<std::string, PsfEntry>& entries) {
    std::string keys;
    std::vector<uint8_t> data;
    std::vector<uint8_t> index;
    auto put16 = [](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    auto put32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i))); };
    for (const auto& [key, e] : entries) {
        put16(index, static_cast<uint16_t>(keys.size()));
        put16(index, e.fmt);
        uint32_t len = e.fmt == 0x0404 ? 4 : static_cast<uint32_t>(std::min<size_t>(e.str.size() + (e.fmt == 0x0204 ? 1 : 0), e.max));
        put32(index, len);
        put32(index, e.max);
        put32(index, static_cast<uint32_t>(data.size()));
        keys += key;
        keys.push_back('\0');
        size_t at = data.size();
        data.resize(at + e.max, 0);
        if (e.fmt == 0x0404) {
            for (int i = 0; i < 4; i++) data[at + i] = uint8_t(e.num >> (8 * i));
        } else {
            std::memcpy(&data[at], e.str.data(), std::min<size_t>(e.str.size(), e.max));
        }
    }
    while (keys.size() % 4) keys.push_back('\0');
    std::vector<uint8_t> out = {0, 'P', 'S', 'F'};
    put32(out, 0x101);
    put32(out, static_cast<uint32_t>(20 + index.size()));
    put32(out, static_cast<uint32_t>(20 + index.size() + keys.size()));
    put32(out, static_cast<uint32_t>(entries.size()));
    out.insert(out.end(), index.begin(), index.end());
    out.insert(out.end(), keys.begin(), keys.end());
    out.insert(out.end(), data.begin(), data.end());
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
}

void write_param(const std::string& dir, const std::string& dir_name, uint32_t param) {
    std::map<std::string, PsfEntry> e;
    e["CATEGORY"] = {0x0204, 4, "SD", 0};
    e["TITLE"] = {0x0204, 128, gstr(param + P_TITLE, 128), 0};
    e["SUB_TITLE"] = {0x0204, 128, gstr(param + P_SUBTITLE, 128), 0};
    e["DETAIL"] = {0x0204, 1024, gstr(param + P_DETAIL, 1024), 0};
    e["ATTRIBUTE"] = {0x0404, 4, "", vm::read32(param + P_ATTRIBUTE)};
    e["SAVEDATA_DIRECTORY"] = {0x0204, 64, dir_name, 0};
    e["SAVEDATA_LIST_PARAM"] = {0x0004, 8, gstr(param + P_LIST_PARAM, 8), 0};
    write_psf(dir + "/PARAM.SFO", e);
}

void fill_param(uint32_t param, const vfs::Psf& sfo) {
    std::memset(vm::ptr(param), 0, 0x610);
    put_str(param + P_TITLE, sfo.str("TITLE"), 128);
    put_str(param + P_SUBTITLE, sfo.str("SUB_TITLE"), 128);
    put_str(param + P_DETAIL, sfo.str("DETAIL"), 1024);
    vm::write32(param + P_ATTRIBUTE, sfo.num("ATTRIBUTE"));
    std::string lp = sfo.str("SAVEDATA_LIST_PARAM");
    std::memcpy(vm::ptr(param + P_LIST_PARAM), lp.data(), std::min<size_t>(lp.size(), 8));
}

std::string save_root() { return vfs::host_path("/dev_hdd0/home/00000001/savedata"); }

int64_t file_time(const std::string& path, int which) {
    struct _stat64 st{};
    if (_stat64(path.c_str(), &st)) return 0;
    return which == 0 ? st.st_atime : which == 1 ? st.st_mtime : st.st_ctime;
}

struct Op {
    ppu::Context& ctx;
    std::string dir_name;
    std::string dir;
    uint32_t set_buf;
    uint32_t func_stat;
    uint32_t func_file;
    uint32_t userdata;
    bool save;
};

void fill_stat(const Op& op, uint32_t sg, bool exists) {
    std::memset(vm::ptr(sg), 0, SG_SIZE);
    vm::write32(sg + SG_FREE_KB, 40 * 1024 * 1024);
    vm::write32(sg + SG_IS_NEW, exists ? 0 : 1);
    put_str(sg + SG_DIR + 0x18, op.dir_name, 32);
    uint32_t list_max = vm::read32(op.set_buf + 4);
    uint32_t buf = vm::read32(op.set_buf + 0x24);
    uint32_t buf_size = vm::read32(op.set_buf + 0x20);
    vm::write32(sg + SG_FILE_LIST, buf);
    if (!exists) return;
    vm::write64(sg + SG_DIR + 0x0, file_time(op.dir, 0));
    vm::write64(sg + SG_DIR + 0x8, file_time(op.dir, 1));
    vm::write64(sg + SG_DIR + 0x10, file_time(op.dir, 2));
    vfs::Psf sfo;
    sfo.load(op.dir + "/PARAM.SFO");
    fill_param(sg + SG_PARAM, sfo);
    uint32_t n = 0, listed = 0;
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& de : fsys::directory_iterator(op.dir, ec)) {
        if (!de.is_regular_file()) continue;
        std::string name = de.path().filename().string();
        uint64_t size = de.file_size();
        total += size;
        if (name == "PARAM.SFO" || name == "PARAM.PFD") continue;
        n++;
        uint32_t e = buf + listed * 0x38;
        if (listed < list_max && buf && (listed + 1) * 0x38 <= buf_size) {
            std::memset(vm::ptr(e), 0, 0x38);
            vm::write32(e, content_type(name) == 1 ? 0 : content_type(name));
            vm::write64(e + 0x8, size);
            std::string hp = de.path().string();
            vm::write64(e + 0x10, file_time(hp, 0));
            vm::write64(e + 0x18, file_time(hp, 1));
            vm::write64(e + 0x20, file_time(hp, 2));
            put_str(e + 0x28, name, 13);
            listed++;
        }
    }
    vm::write32(sg + SG_SIZE_KB, static_cast<uint32_t>((total + 1023) / 1024));
    vm::write32(sg + SG_SYS_SIZE_KB, 35);
    vm::write32(sg + SG_FILE_NUM, n);
    vm::write32(sg + SG_FILE_LIST_NUM, listed);
}

int32_t file_op(const Op& op, uint32_t fs, uint32_t fg) {
    if (!fs || !vm::range_mapped(fs, 0x40)) return ERR_ACCESS;
    uint32_t oper = vm::read32(fs + 0x0);
    uint32_t type = vm::read32(fs + 0x8);
    std::string name = content_name(type) ? content_name(type) : gstr(vm::read32(fs + 0x1C), 13);
    if (name.empty()) return ERR_PARAM;
    std::string path = op.dir + "/" + name;
    uint32_t offset = vm::read32(fs + 0x20), size = vm::read32(fs + 0x24), buf_size = vm::read32(fs + 0x28), buf = vm::read32(fs + 0x2C);
    uint32_t done = 0;
    switch (oper) {
    case OP_READ: {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return ERR_NODATA;
        _fseeki64(f, offset, SEEK_SET);
        uint32_t want = size ? std::min(size, buf_size) : buf_size;
        vm::host_write(buf, want);
        done = static_cast<uint32_t>(std::fread(vm::ptr(buf), 1, want, f));
        std::fclose(f);
        break;
    }
    case OP_WRITE:
    case OP_WRITE_NOTRUNC: {
        std::error_code ec;
        fsys::create_directories(op.dir, ec);
        FILE* f = std::fopen(path.c_str(), oper == OP_WRITE || !fsys::exists(path, ec) ? "wb" : "r+b");
        if (!f) return ERR_ACCESS;
        _fseeki64(f, offset, SEEK_SET);
        done = static_cast<uint32_t>(std::fwrite(vm::ptr(buf), 1, std::min(size, buf_size), f));
        std::fclose(f);
        break;
    }
    case OP_DELETE: {
        std::error_code ec;
        fsys::remove(path, ec);
        break;
    }
    default:
        return ERR_PARAM;
    }
    vm::write32(fg, done);
    LOG_INFO("savedata", "%s %s/%s type %u offset 0x%x size 0x%x -> 0x%x", oper == OP_READ ? "read" : oper == OP_DELETE ? "delete" : "write", op.dir_name.c_str(), name.c_str(), type, offset, size, done);
    return 0;
}

int32_t run_op(Op& op) {
    std::lock_guard lk(g_lock);
    if (!g_scratch) g_scratch = vm::alloc(vm::Region::Stack, SCRATCH_SIZE, 0x100);
    if (!g_scratch) return ERR_ACCESS;
    uint32_t cb = g_scratch + CB_RESULT, sg = g_scratch + STAT_GET, ss = g_scratch + STAT_SET, fg = g_scratch + FILE_GET, fs = g_scratch + FILE_SET;
    std::error_code ec;
    bool exists = fsys::exists(op.dir + "/PARAM.SFO", ec);
    if (!op.save && !exists) {
        LOG_INFO("savedata", "load '%s' -> no data", op.dir_name.c_str());
        return ERR_NODATA;
    }
    fill_stat(op, sg, exists);
    if (!vm::range_mapped(op.func_stat, 4) || !vm::range_mapped(op.func_file, 4)) {
        LOG_WARN("savedata", "'%s' callback pointers 0x%08x/0x%08x are not mapped",
                 op.dir_name.c_str(), op.func_stat, op.func_file);
        return ERR_PARAM;
    }
    std::memset(vm::ptr(cb), 0, 0x20);
    vm::write32(cb + 0x10, op.userdata);
    std::memset(vm::ptr(ss), 0, 0x10);
    call_guest(op.ctx, op.func_stat, cb, sg, ss);
    int32_t result = static_cast<int32_t>(vm::read32(cb));
    if (result < 0) {
        LOG_WARN("savedata", "'%s' stat callback result %d", op.dir_name.c_str(), result);
        return ERR_CBRESULT;
    }
    uint32_t set_param = vm::read32(ss + 0x0);
    uint32_t recreate = vm::read32(ss + 0x4) & 0xFFFF;
    if (op.save) {
        if (!exists && !set_param) return ERR_PARAM;
        if (exists && (recreate == 2 || recreate == 3)) {
            for (const auto& de : fsys::directory_iterator(op.dir, ec)) fsys::remove(de.path(), ec);
        }
        fsys::create_directories(op.dir, ec);
        if (set_param) write_param(op.dir, op.dir_name, set_param);
    }
    LOG_INFO("savedata", "%s '%s' (%s) stat result %d setParam 0x%x recreate 0x%x indicator 0x%x needKB %d progress %u", op.save ? "save" : "load", op.dir_name.c_str(), exists ? "existing" : "new", result,
             set_param, vm::read32(ss + 0x4), vm::read32(ss + 0x8), static_cast<int32_t>(vm::read32(cb + 0x8)), vm::read32(cb + 0x4));
    std::memset(vm::ptr(fg), 0, 0x44);
    while (result == CB_OK_NEXT) {
        std::memset(vm::ptr(cb), 0, 0x20);
        vm::write32(cb + 0x10, op.userdata);
        std::memset(vm::ptr(fs), 0, 0x30);
        call_guest(op.ctx, op.func_file, cb, fg, fs);
        result = static_cast<int32_t>(vm::read32(cb));
        if (result < 0) {
            LOG_WARN("savedata", "'%s' file callback result %d", op.dir_name.c_str(), result);
            return ERR_CBRESULT;
        }
        if (result == CB_OK_LAST || result == CB_OK_LAST_NOCONFIRM) break;
        if (int32_t r = file_op(op, fs, fg)) return r;
    }
    return 0;
}

int32_t auto_op(ppu::Context& ctx, bool save) {
    std::string name = gstr(a32(ctx, 1), 64);
    Op op{ctx, name, save_root() + "/" + name, a32(ctx, 3), a32(ctx, 4), a32(ctx, 5), a32(ctx, 7), save};
    if (name.empty() || !op.set_buf || !op.func_stat || !op.func_file) return ERR_PARAM;
    return run_op(op);
}

}

HLE_FUNC(cellSysutil, cellSaveDataAutoLoad2) { ret(ctx, auto_op(ctx, false)); }

HLE_FUNC(cellSysutil, cellSaveDataAutoSave2) { ret(ctx, auto_op(ctx, true)); }

/* cellSaveDataUserAutoLoad has the same shape as AutoLoad2 but keys the file by
 * user id. Without it the title's save check falls through to the unresolved
 * handler, reports failure, and shows a modal error -- which is what stopped the
 * run before the world could load. The user is folded into the directory name so
 * each account keeps its own slot. */
/* cellSaveDataUserListAutoLoad(version, userId, errDialog, setList, setBuf,
 * funcFixed, funcStat, funcFile, container, userdata): enumerate the account's
 * saves rather than loading one.
 *
 * For the LIST variants RPCS3 calls funcFixed with (result, listGet, fixedSet)
 * BEFORE anything else, and the title decides what to do from that. Skipping it
 * left the title waiting on an empty screen -- the Start Game panel with
 * placeholder icons. With no save on disk the list is empty and the fixed
 * callback says "no data", which is the answer that leads to "create a world".
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp, the funcFixed
 * call site in savedata_op. */
HLE_FUNC(cellSysutil, cellSaveDataUserListAutoLoad) {
    const uint32_t version = a32(ctx, 0);
    const uint32_t user = a32(ctx, 1);
    const uint32_t err_dialog = a32(ctx, 2);
    const uint32_t set_list = a32(ctx, 3);
    const uint32_t set_buf = a32(ctx, 4);
    const uint32_t func_fixed = a32(ctx, 5);
    const uint32_t func_stat = a32(ctx, 6);
    (void)func_stat;
    LOG_INFO("savedata",
             "UserListAutoLoad version 0x%x user %u errDialog 0x%x setList 0x%08x setBuf 0x%08x "
             "funcFixed 0x%08x container 0x%x",
             version, user, err_dialog, set_list, set_buf, func_fixed, a32(ctx, 8));
    if (!set_list || !set_buf || !func_fixed) return ret(ctx, static_cast<int32_t>(ERR_PARAM));

    /* The fixed callback decides the flow. Give it a well-formed empty list:
     * CellSaveDataListGet { u32 dirNum, u32 listNum, ptr dirList, reserved[64] },
     * and a CellSaveDataCBResult it writes its verdict into. */
    std::lock_guard lk(g_lock);
    if (!g_scratch) g_scratch = vm::alloc(vm::Region::Stack, SCRATCH_SIZE, 0x100);
    if (!g_scratch) return ret(ctx, static_cast<int32_t>(ERR_ACCESS));
    const uint32_t cb = g_scratch + CB_RESULT;
    const uint32_t list_get = g_scratch + CB_RESULT + 0x40;
    const uint32_t fixed_set = list_get + 0x60;
    std::memset(vm::ptr(list_get), 0, 0x60);
    vm::write32(list_get + 0, 0);   /* dirNum: nothing saved */
    vm::write32(list_get + 4, 0);   /* dirListNum */
    vm::write32(list_get + 8, 0);   /* dirList */
    std::memset(vm::ptr(fixed_set), 0, 0x10);
    std::memset(vm::ptr(cb), 0, 0x20);
    vm::write32(cb + 0x10, a32(ctx, 9));   /* userdata */
    LOG_INFO("savedata", "UserListAutoLoad: calling funcFixed 0x%08x with empty list",
             func_fixed);
    call_guest(ctx, func_fixed, cb, list_get, fixed_set);
    const int32_t fixed_result = static_cast<int32_t>(vm::read32(cb));
    LOG_INFO("savedata", "UserListAutoLoad: funcFixed returned %d -> no saved data", fixed_result);
    ret(ctx, static_cast<int32_t>(ERR_NODATA));
}

/* cellSaveDataUserAutoSave(version, userId, dirName, errDialog, setBuf,
 * funcStat, funcFile, container, userdata). This is what "Create New World"
 * reaches: the title writes the world's save once it has generated it. The
 * user id is folded into the directory name, as for UserAutoLoad. */
/* cellSaveDataUserAutoSave(version, userId, dirName, errDialog, setBuf,
 * funcStat, funcFile, container, userdata).
 *
 * Nine arguments: the ninth is the first stack argument under the PPU ABI, not
 * r3+9. Reading it as a register was how container (the eighth, 0xffffffff in
 * the title's call) ended up mistaken for funcFile.
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.cpp:2574. */
HLE_FUNC(cellSysutil, cellSaveDataUserAutoSave) {
    const uint32_t version = a32(ctx, 0);
    const uint32_t user = a32(ctx, 1);
    const uint32_t dir_name_ea = a32(ctx, 2);
    const uint32_t err_dialog = a32(ctx, 3);
    const uint32_t set_buf = a32(ctx, 4);
    const uint32_t func_stat = a32(ctx, 5);
    const uint32_t func_file = a32(ctx, 6);
    const uint32_t container = a32(ctx, 7);
    /* Argument 9 onward lives on the caller's stack, above the frame header. */
    const uint32_t userdata = vm::read32(static_cast<uint32_t>(ctx.gpr[1]) + 0x60);

    std::string name = gstr(dir_name_ea, 32);
    LOG_INFO("savedata",
             "UserAutoSave v 0x%x user %u errDialog 0x%x setBuf 0x%08x stat 0x%08x "
             "file 0x%08x container 0x%x userdata 0x%08x dirName 0x%08x '%s'",
             version, user, err_dialog, set_buf, func_stat, func_file, container,
             userdata, dir_name_ea, name.c_str());
    if (name.empty() || !set_buf || !func_stat || !func_file) {
        return ret(ctx, static_cast<int32_t>(ERR_PARAM));
    }
    const std::string folded = "u" + std::to_string(user) + "_" + name;
    Op op{ctx, folded, save_root() + "/" + folded, set_buf, func_stat, func_file,
          userdata, true};
    {
        const int32_t rc = run_op(op);
        force::notify_save_done();
        ret(ctx, rc);
    }
}

/* cellSaveDataEnableOverlay(enable): the autosave indicator. Nothing to draw. */
HLE_FUNC(cellSysutil, cellSaveDataEnableOverlay) {
    ret(ctx, 0);
}

/* cellSaveDataDelete2(container). */
HLE_FUNC(cellSysutil, cellSaveDataDelete2) { ret(ctx, 0); }

HLE_FUNC(cellSysutil, cellSaveDataFixedDelete) { ret(ctx, 0); }

HLE_FUNC(cellSysutil, cellSaveDataUserFixedDelete) { ret(ctx, 0); }

/* Same nine-argument shape as UserAutoSave, loading instead of saving. */
HLE_FUNC(cellSysutil, cellSaveDataUserAutoLoad) {
    const uint32_t version = a32(ctx, 0);
    const uint32_t user = a32(ctx, 1);
    const uint32_t dir_name_ea = a32(ctx, 2);
    const uint32_t err_dialog = a32(ctx, 3);
    const uint32_t set_buf = a32(ctx, 4);
    const uint32_t func_stat = a32(ctx, 5);
    const uint32_t func_file = a32(ctx, 6);
    const uint32_t container = a32(ctx, 7);
    const uint32_t userdata = vm::read32(static_cast<uint32_t>(ctx.gpr[1]) + 0x60);
    (void)version; (void)err_dialog; (void)container;

    std::string name = gstr(dir_name_ea, 32);
    if (name.empty() || !set_buf || !func_stat || !func_file) {
        return ret(ctx, static_cast<int32_t>(ERR_PARAM));
    }
    const std::string folded = "u" + std::to_string(user) + "_" + name;
    Op op{ctx, folded, save_root() + "/" + folded, set_buf, func_stat, func_file,
          userdata, false};
    const int32_t r = run_op(op);
    LOG_INFO("savedata", "UserAutoLoad user %u '%s' -> %d", user, folded.c_str(), r);
    ret(ctx, r);
}

}
