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
    /* The file list is sorted by name, exactly as firmware does it
     * (refs/.../cellSaveData.cpp:1613: alphabetical, or the PARAM.SFO
     * RPCS3_BLIST order for new saves). Our previous raw directory_iterator
     * order made the truncated list -- fileListMax is only 3 for this title --
     * keep a *different* subset of files every run, so the title could not
     * count on which entries it would see and abandoned the load.
     * The size added to sizeKB is rounded up per file (align(entry.size, 1024),
     * cellSaveData.cpp:1639), not summed then rounded once. */
    struct FileEnt {
        std::string name;
        std::string host;
        uint64_t size;
    };
    std::vector<FileEnt> files;
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& de : fsys::directory_iterator(op.dir, ec)) {
        if (!de.is_regular_file()) continue;
        const std::string name = de.path().filename().string();
        /* The two system files firmware hides from the list, and only those
         * (refs/.../cellSaveData.cpp:1603 calls check_filename(name, false,
         * false), whose PARAM.SFO/PARAM.PFD test is the only exclusion).
         * Excluding the title's own METADATA was tried and MEASURED to change
         * nothing: fileNum went 4 -> 3 to match the count in METADATA's own
         * header, and the title's file callback still returned -2. */
        if (name == "PARAM.SFO" || name == "PARAM.PFD") continue;
        if (name.size() >= 13) continue;   /* CELL_SAVEDATA_FILENAME_SIZE */
        files.push_back({name, de.path().string(), de.file_size()});
    }
    std::sort(files.begin(), files.end(),
              [](const FileEnt& a, const FileEnt& b) { return a.name < b.name; });

    uint32_t n = 0, listed = 0;
    for (const FileEnt& f : files) {
        total += (f.size + 1023) & ~uint64_t(1023);
        n++;
        const uint32_t e = buf + listed * 0x38;
        if (listed < list_max && buf && (listed + 1) * 0x38 <= buf_size) {
            std::memset(vm::ptr(e), 0, 0x38);
            /* CellSaveDataFileStat.fileType: SECUREFILE is 0, NORMALFILE is 1
             * (refs/.../cellSaveData.h:103). content_type() returns 1 for an
             * ordinary file, and the old code turned that into 0 -- it stamped
             * GAMEDATA, METADATA and THUMB as protected content. */
            vm::write32(e, content_type(f.name));
            vm::write64(e + 0x8, f.size);
            vm::write64(e + 0x10, file_time(f.host, 0));
            vm::write64(e + 0x18, file_time(f.host, 1));
            vm::write64(e + 0x20, file_time(f.host, 2));
            put_str(e + 0x28, f.name, 13);
            listed++;
        }
    }
    vm::write32(sg + SG_SIZE_KB, static_cast<uint32_t>(total / 1024));
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
    const bool trace_files = std::getenv("PS3BASE_SAVEDATA_TRACE") != nullptr;
    if (trace_files) {
        LOG_INFO("savedata",
                 "  statGet: freeKB %d isNew %u dirName '%s' title '%s' sub '%s' bind %u "
                 "sizeKB %d sysKB %d fileNum %u fileListNum %u fileList 0x%08x",
                 static_cast<int32_t>(vm::read32(sg + 0x0)), vm::read32(sg + 0x4),
                 gstr(sg + 0x8 + 0x18, 32).c_str(), gstr(sg + 0x40, 128).c_str(),
                 gstr(sg + 0x40 + 0x80, 128).c_str(), vm::read32(sg + 0x650),
                 static_cast<int32_t>(vm::read32(sg + 0x654)),
                 static_cast<int32_t>(vm::read32(sg + 0x658)),
                 vm::read32(sg + 0x65C), vm::read32(sg + 0x660),
                 vm::read32(sg + 0x664));
        LOG_INFO("savedata", "    setBuf: dirListMax %u fileListMax %u bufSize 0x%x buf 0x%08x",
                 vm::read32(op.set_buf + 0x0), vm::read32(op.set_buf + 0x4),
                 vm::read32(op.set_buf + 0x20), vm::read32(op.set_buf + 0x24));
        for (uint32_t i = 0; i < vm::read32(sg + 0x660) && i < 12; i++) {
            const uint32_t e = vm::read32(sg + 0x664) + i * 0x38;
            LOG_INFO("savedata", "    fileList[%u] type %u size %llu name '%s'", i,
                     vm::read32(e), static_cast<unsigned long long>(vm::read64(e + 0x8)),
                     gstr(e + 0x28, 13).c_str());
        }
    }
    /* The title's own load callback (f_002ea6ac, mc_029.cpp:5683) is a state
     * machine driven by a global at 0xbd4a08:
     *   +332  step (0 METADATA, 1 parse+THUMB, 2 verify THUMB, 3 GAMEDATA, ...)
     *   +2312 version read from METADATA[0..4]
     *   +2316 crc32(GAMEDATA) from METADATA[12..16]
     *   +2320 crc32(THUMB)    from METADATA[16..20]
     *   +2324 GAMEDATA buffer   +2328 GAMEDATA size
     *   +2592 THUMB buffer      +2596 THUMB size
     * When it returns -2 it is either a size mismatch against fileGet->excSize,
     * a CRC32 mismatch, or a null GAMEDATA buffer. Logging all of them says
     * which, instead of guessing. */
    auto sd_global = []() -> uint32_t {
        /* f_002ea6ac does gpr31 = ld32(0xbe4a08): the savedata global is a
         * pointer, not a fixed address (mc_029.cpp:5692,5701). */
        if (!vm::range_mapped(0xbe4a08u, 4)) return 0u;
        const uint32_t g = vm::read32(0xbe4a08u);
        return g && vm::range_mapped(g, 4) ? g : 0u;
    };
    auto crc32_of = [](uint32_t addr, uint32_t len) -> uint32_t {
        static uint32_t table[256];
        static bool init = false;
        if (!init) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[i] = c;
            }
            init = true;
        }
        if (!addr || !len || !vm::range_mapped(addr, len)) return 0u;
        uint32_t c = 0xFFFFFFFFu;
        const uint8_t* p = vm::ptr(addr);
        for (uint32_t i = 0; i < len; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    const bool trace_state = trace_files && !op.save;
    for (int iter = 0; result == CB_OK_NEXT && iter < 4096; iter++) {
        std::memset(vm::ptr(cb), 0, 0x20);
        vm::write32(cb + 0x10, op.userdata);
        std::memset(vm::ptr(fs), 0, 0x30);
        call_guest(op.ctx, op.func_file, cb, fg, fs);
        result = static_cast<int32_t>(vm::read32(cb));
        if (trace_state) {
            const uint32_t g = sd_global();
            if (g && vm::range_mapped(g + 332, 4)) {
                const uint32_t step = vm::read32(g + 332);
                const uint32_t gbuf = vm::read32(g + 2324), gsz = vm::read32(g + 2328);
                const uint32_t tbuf = vm::read32(g + 2592), tsz = vm::read32(g + 2596);
                const uint32_t want_gcrc = vm::read32(g + 2316), want_tcrc = vm::read32(g + 2320);
                const uint32_t exc = vm::read32(fg + 0x0);
                LOG_INFO("savedata",
                         "  state: step %u ver %u gsz 0x%x tsz 0x%x gbuf 0x%08x tbuf 0x%08x "
                         "excSize 0x%x | want crcG %08x crcT %08x | got crcG %08x crcT %08x",
                         step, vm::read32(g + 2312), gsz, tsz, gbuf, tbuf, exc,
                         want_gcrc, want_tcrc,
                         crc32_of(gbuf, gsz), crc32_of(tbuf, tsz));
            } else {
                LOG_INFO("savedata", "  state: global 0x%08x not mapped", g);
            }
        }
        if (trace_files) {
            char hex[3 * 0x40 + 1] = {};
            for (int i = 0; i < 0x40; i++)
                std::snprintf(hex + i * 3, 4, "%02x ", vm::ptr(fs)[i]);
            LOG_INFO("savedata", "  fs bytes: %s", hex);
            LOG_INFO("savedata",
                     "  filecb #%d -> result %d op 0x%x type %u name '%s' off 0x%x size 0x%x bufSize 0x%x fileTypeIn 0x%x",
                     iter, result, vm::read32(fs + 0x0), vm::read32(fs + 0x8),
                     gstr(vm::read32(fs + 0x1C), 13).c_str(), vm::read32(fs + 0x20),
                     vm::read32(fs + 0x24), vm::read32(fs + 0x28), vm::read32(fs + 0x00));
        }
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
    const uint32_t func_file = a32(ctx, 7);
    const uint32_t container = a32(ctx, 8);
    (void)version;
    (void)err_dialog;
    (void)container;
    /* The ninth and tenth arguments are stack arguments under the PPU ABI, not
     * registers: container is at sp+0x60 and userdata at sp+0x64. Reading
     * a32(ctx, 8) picked up r11, which is not an argument at all. */
    /* 9th+ arguments live at sp+0x70 in the caller's parameter area
     * (refs/rpcs3 .../PPUThread.cpp:3251 get_stack_arg: gpr[1] + 0x30 + 8*(i-1)).
     * 0x64 sat inside the r3-r10 save area and read unrelated stack. */
    /* The parameter area slot is a DOUBLEWORD. The title stores it with
     * `st64 sp+112, r30` (out/ppu/mc_029.cpp:4201), and on big-endian POWER the
     * low half of that 8-byte value lives at slot+4. Reading 32 bits at slot+0
     * returns the HIGH half, which is 0 for every small userdata.
     * RPCS3 does not make this mistake because get_stack_arg returns a
     * be_t<u64>* and the caller truncates: (`+*ctx->get_stack_arg(i)` then
     * ppu_gpr_cast<u32>), i.e. it reads the whole dword and keeps the low bits.
     * refs/rpcs3/rpcs3/Emu/Cell/PPUThread.cpp:3251 and
     * refs/rpcs3/rpcs3/Emu/Cell/Modules/sys_libc_.cpp:27. */
    const uint32_t sp_now = static_cast<uint32_t>(ctx.gpr[1]);
    const uint64_t slot64 = vm::read64(sp_now + 0x70);
    const uint32_t stack_userdata = static_cast<uint32_t>(slot64 & 0xffffffffu);
    LOG_INFO("savedata", "UserListAutoLoad stack arg9: slot64 0x%llx hi 0x%08x lo 0x%08x -> userdata 0x%x",
             (unsigned long long)slot64, vm::read32(sp_now + 0x70), vm::read32(sp_now + 0x74),
             stack_userdata);

    LOG_INFO("savedata",
             "UserListAutoLoad version 0x%x user %u errDialog 0x%x setList 0x%08x setBuf 0x%08x "
             "funcFixed 0x%08x funcStat 0x%08x funcFile 0x%08x",
             version, user, err_dialog, set_list, set_buf, func_fixed, func_stat, func_file);
    if (!set_list || !set_buf || !func_fixed) return ret(ctx, static_cast<int32_t>(ERR_PARAM));

    /* cellSaveDataUserListAutoLoad: enumerate the account's savedata and hand
     * the title the real list.
     *
     * The previous implementation always reported an empty list and returned
     * ERR_NODATA. That is not a harmless simplification: this title has four
     * complete world saves on disk (GAMEDATA + PARAM.SFO + ICON0 + THUMB) and
     * its Start Game panel still said "No Games Found", so every path that
     * loads an existing world was unreachable no matter what else worked.
     *
     * Layout (refs/rpcs3/.../cellSaveData.h):
     *   CellSaveDataListGet { u32 dirNum; u32 dirListNum; ptr dirList; char reserved[64]; }
     *   CellSaveDataDirList { char dirName[32]; char listParam[8]; char reserved[8]; }
     *   CellSaveDataSetBuf { u32 dirListMax; u32 fileListMax; u32 reserved[6];
     *                        u32 bufSize; ptr buf; }
     *   CellSaveDataSetList { u32 sortType; u32 sortOrder; ptr dirNamePrefix; ptr reserved; }
     */
    const uint32_t dir_list_max = vm::read32(set_buf + 0x0);
    const uint32_t buf_size = vm::read32(set_buf + 0x20);
    const uint32_t buf = vm::read32(set_buf + 0x24);
    const uint32_t prefix_ea = vm::read32(set_list + 0x8);
    const std::string prefix = prefix_ea ? gstr(prefix_ea, 64) : std::string();

    std::lock_guard lk(g_lock);
    if (!g_scratch) g_scratch = vm::alloc(vm::Region::Stack, SCRATCH_SIZE, 0x100);
    if (!g_scratch) return ret(ctx, static_cast<int32_t>(ERR_ACCESS));
    const uint32_t cb = g_scratch + CB_RESULT;
    const uint32_t list_get = g_scratch + CB_RESULT + 0x40;
    const uint32_t fixed_set = list_get + 0x60;
    std::memset(vm::ptr(list_get), 0, 0x60);
    std::memset(vm::ptr(fixed_set), 0, 0x10);
    std::memset(vm::ptr(cb), 0, 0x20);
    vm::write32(cb + 0x10, stack_userdata);
    vm::write32(list_get + 8, buf);   /* dirList points at the caller's buffer */

    /* A title asks for the saves that belong to it by filling dirNamePrefix;
     * an empty prefix means "everything on the account". The match is a plain
     * prefix test, exactly as the firmware does it. */
    std::error_code ec;
    const std::string root = save_root();
    std::vector<std::string> names;
    for (const auto& de : fsys::directory_iterator(root, ec)) {
        if (!de.is_directory()) continue;
        const std::string name = de.path().filename().string();
        if (name.size() > 32) continue;
        if (!prefix.empty() && name.compare(0, prefix.size(), prefix) != 0) continue;
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());

    /* dirNum is the total number of matches; dirListNum is how many of them
     * fit in the caller's buffer. Filling more than dirListMax would overrun
     * it, and filling more than bufSize allows would overrun the buffer. */
    uint32_t listed = 0;
    uint32_t clear_at = 0;
    for (const std::string& name : names) {
        if (buf && buf_size && clear_at == 0) {
            /* The firmware clears the caller's buffer only when the first
             * match is found, so an empty account leaves it untouched. */
            std::memset(vm::ptr(buf), 0, buf_size);
        }
        clear_at++;
        if (listed >= dir_list_max) continue;
        const uint64_t need = (uint64_t(listed) + 1) * 0x30;
        if (!buf || need > buf_size) continue;
        const uint32_t e = buf + listed * 0x30;
        std::memset(vm::ptr(e), 0, 0x30);
        std::memcpy(vm::ptr(e), name.data(), std::min<size_t>(name.size(), 31));
        /* SAVEDATA_LIST_PARAM comes from the entry's own PARAM.SFO; the list
         * shows the sort order from it, and garbage there reorders or drops
         * entries in the title's own view. */
        vfs::Psf sfo;
        sfo.load(root + "/" + name + "/PARAM.SFO");
        std::string lp = sfo.str("SAVEDATA_LIST_PARAM");
        if (!lp.empty()) std::memcpy(vm::ptr(e + 0x20), lp.data(), std::min<size_t>(lp.size(), 8));
        listed++;
    }
    vm::write32(list_get + 0, static_cast<uint32_t>(names.size()));
    vm::write32(list_get + 4, listed);
    LOG_INFO("savedata",
             "UserListAutoLoad: %zu savedata match prefix '%s' (dirListMax %u bufSize 0x%x buf 0x%08x), listing %u",
             names.size(), prefix.c_str(), dir_list_max, buf_size, buf, listed);

    call_guest(ctx, func_fixed, cb, list_get, fixed_set);
    const int32_t fixed_result = static_cast<int32_t>(vm::read32(cb));
    /* funcFixed returning OK_NEXT plus a chosen dirName means "the user picked
     * one"; with no saves there is nothing to pick and the firmware reports
     * NODATA. */
    if (fixed_result == CB_OK_NEXT) {
        const uint32_t chosen = vm::read32(fixed_set + 0x0);
        const std::string chosen_name = chosen ? gstr(chosen, 32) : std::string();
        LOG_INFO("savedata", "UserListAutoLoad: funcFixed chose '%s'", chosen_name.c_str());
        if (chosen_name.empty() && names.empty())
            return ret(ctx, static_cast<int32_t>(ERR_NODATA));
        if (!chosen_name.empty())
            return ret(ctx, static_cast<int32_t>(ERR_NODATA));  /* the listing call does not load */
    }
    LOG_INFO("savedata", "UserListAutoLoad: funcFixed returned %d with %zu savedata",
             fixed_result, names.size());
    if (names.empty()) return ret(ctx, static_cast<int32_t>(ERR_NODATA));
    ret(ctx, 0);
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
    /* 9th argument (userdata) is at sp+0x70, not sp+0x60: see the note in
     * cellSaveDataUserAutoLoad. */
    const uint32_t userdata = vm::read32(static_cast<uint32_t>(ctx.gpr[1]) + 0x70);

    std::string name = gstr(dir_name_ea, 32);
    LOG_INFO("savedata",
             "UserAutoSave v 0x%x user %u errDialog 0x%x setBuf 0x%08x stat 0x%08x "
             "file 0x%08x container 0x%x userdata 0x%08x dirName 0x%08x '%s'",
             version, user, err_dialog, set_buf, func_stat, func_file, container,
             userdata, dir_name_ea, name.c_str());
    if (name.empty() || !set_buf || !func_stat || !func_file) {
        return ret(ctx, static_cast<int32_t>(ERR_PARAM));
    }
    const std::string folded = name;  /* verbatim: RPCS3 path is savedata/<dirName> */
    Op op{ctx, folded, save_root() + "/" + folded, set_buf, func_stat, func_file,
          userdata, true};
    {
        const int32_t rc = run_op(op);
        force::notify_save_done();
        ret(ctx, rc);
    }
}

/* cellSaveDataGetListItem(dirName, dir, sysFileParam, bind, sizeKB): read
 * one entry's metadata back.
 *
 * The title walks the dirList we fill in cellSaveDataUserListAutoLoad and calls
 * this once per entry, passing the dirName field of the CellSaveDataDirList it
 * was given (measured: r3 equals the first entry's dirName address, r4 the
 * caller's CellSaveDataDirStat). It was not implemented, so the generic handler
 * returned 0 with every out-parameter untouched: the enumeration count was
 * right, the rows rendered blank, and pressing Load found no entry to load.
 *
 * Layouts (refs/rpcs3/rpcs3/Emu/Cell/Modules/cellSaveData.h:197-247, 229-238):
 *   CellSaveDataDirStat          { s64 atime, mtime, ctime; char dirName[32]; }
 *   CellSaveDataSystemFileParam  { char title[128]; char subTitle[128];
 *                                  char detail[1024]; u32 attribute;
 *                                  u32 parental_level; char listParam[8];
 *                                  char reserved[256]; }        (0x610)
 * Reference: cellSaveData.cpp:2293 savedata_get_list_item. */
HLE_FUNC(cellSaveData, cellSaveDataGetListItem) {
    const uint32_t dir_name_ea = a32(ctx, 0);
    const uint32_t dir = a32(ctx, 1);
    const uint32_t sys_file_param = a32(ctx, 2);
    const uint32_t bind = a32(ctx, 3);
    const uint32_t size_kb = a32(ctx, 4);

    const std::string name = gstr(dir_name_ea, 32);
    if (name.empty()) return ret(ctx, static_cast<int32_t>(ERR_PARAM));

    const std::string path = save_root() + "/" + name;
    std::error_code ec;
    if (!fsys::is_directory(path, ec)) {
        LOG_WARN("savedata", "GetListItem '%s' -> no data", name.c_str());
        return ret(ctx, static_cast<int32_t>(ERR_NODATA));
    }

    if (dir) {
        std::memset(vm::ptr(dir), 0, 0x38);
        vm::write64(dir + 0x0, file_time(path, 0));
        vm::write64(dir + 0x8, file_time(path, 1));
        vm::write64(dir + 0x10, file_time(path, 2));
        put_str(dir + 0x18, name, 32);
    }

    if (sys_file_param) {
        vfs::Psf sfo;
        sfo.load(path + "/PARAM.SFO");
        fill_param(sys_file_param, sfo);
        /* fill_param covers title/subTitle/detail/attribute/listParam;
         * parental_level sits at 0x504 (refs/.../cellSaveData.h:235). */
        vm::write32(sys_file_param + 0x504, sfo.num("PARENTAL_LEVEL"));
    }

    if (bind) vm::write32(bind, 0);

    uint32_t kbytes = 0;
    for (const auto& de : fsys::directory_iterator(path, ec)) {
        if (!de.is_regular_file()) continue;
        kbytes += static_cast<uint32_t>((de.file_size() + 1023) / 1024);
    }
    if (size_kb) vm::write32(size_kb, kbytes + 35);

    LOG_INFO("savedata", "GetListItem '%s' -> title '%s' size %u kB", name.c_str(),
             gstr(sys_file_param, 128).c_str(), kbytes + 35);
    ret(ctx, 0);
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
    const uint32_t sp = static_cast<uint32_t>(ctx.gpr[1]);
    /* PPU64 ABI: r3-r10 carry arguments 1-8; arguments 9 onward live in the
     * caller's parameter area beginning at sp+0x70 (0x30 is the 8-dword
     * parameter save area reserved for r3-r10, not a stack argument). This
     * function's 9th argument is userdata, so it is at sp+0x70. Reading
     * sp+0x60 picked up a slot inside the save area and always yielded 0.
     * Cross-check: refs/rpcs3 .../PPUThread.cpp:3251 get_stack_arg(i) returns
     * gpr[1] + 0x30 + 8*(i-1), and the title stores it at gpr[1]+112 in
     * thread_autoload (out/ppu/mc_029.cpp:4201). */
    /* The slot is a doubleword and big-endian: the value is right-justified,
     * so its low 32 bits are at slot+4, not slot+0. See the long note in
     * cellSaveDataUserListAutoLoad below; this is the same read. */
    const uint64_t slot64 = vm::read64(sp + 0x70);
    const uint32_t userdata = static_cast<uint32_t>(slot64 & 0xffffffffu);
    LOG_INFO("savedata", "UserAutoLoad stack arg9: slot64 0x%llx hi 0x%08x lo 0x%08x -> userdata 0x%x",
             (unsigned long long)slot64, vm::read32(sp + 0x70), vm::read32(sp + 0x74), userdata);
    (void)version; (void)err_dialog; (void)container;

    /* The title gates GAMEDATA allocation on result->userdata == 1 (see
     * f_002ea6ac, mc_029.cpp:5813: cr_set(0, gpr30, 1) then bc 4,2 -> L_2ea8f0,
     * the THUMB-only path). So when we pass userdata 0, the title never asks
     * for GAMEDATA at all and returns -2 from its own null-buffer branch at
     * L_2eaa04. Dump the stack window once so the real value is known rather
     * than assumed: the PPU ABI puts the 9th+ arguments at sp+0x50 upward, and
     * the exact offset depends on the caller's frame. */
    if (std::getenv("PS3BASE_SAVEDATA_STACK")) {
        LOG_INFO("savedata", "UserAutoLoad stack: sp=0x%08x", sp);
        for (uint32_t off = 0x40; off <= 0x90; off += 4)
            LOG_INFO("savedata", "  sp+0x%02x = 0x%08x", off, vm::read32(sp + off));
    }

    std::string name = gstr(dir_name_ea, 32);
    if (name.empty() || !set_buf || !func_stat || !func_file) {
        return ret(ctx, static_cast<int32_t>(ERR_PARAM));
    }
    const std::string folded = name;  /* verbatim: RPCS3 path is savedata/<dirName> */
    Op op{ctx, folded, save_root() + "/" + folded, set_buf, func_stat, func_file,
          userdata, false};
    const int32_t r = run_op(op);
    LOG_INFO("savedata", "UserAutoLoad user %u '%s' userdata=0x%x -> %d", user, folded.c_str(), userdata, r);
    ret(ctx, r);
}

}
