#include "object.h"
#include "runtime/vfs.h"
#include "../crypto/edat.h"
#include "runtime/trace.h"
#include "runtime/hostsleep.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <io.h>
#endif
#include "runtime/compat.h"
#include <sys/stat.h>

namespace crash {
void log_here(const char* tag);
}

namespace lv2 {

namespace {

namespace fsys = std::filesystem;

struct DirEntry {
    std::string name;
    bool dir;
    uint64_t size;
    int64_t mtime;
};

struct Fd {
    std::mutex io;
    uint32_t path_id = 0;
    uint32_t number = 0;
    FILE* f = nullptr;
    std::string guest;
    std::string host;
    int32_t flags = 0;
    bool readonly_mount = false;
    bool is_dir = false;
    std::vector<DirEntry> entries;
    size_t dir_pos = 0;
};

std::mutex g_lock;
std::map<uint32_t, std::shared_ptr<Fd>> g_fds;

constexpr int32_t O_ACCMODE = 3, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0100, O_EXCL = 0200, O_TRUNC = 01000, O_APPEND = 02000;
constexpr int64_t CELL_EROFS = static_cast<int32_t>(0x80010036);
constexpr int64_t CELL_ENOTMOUNTED = static_cast<int32_t>(0x8001003A);
constexpr int64_t CELL_ENAMETOOLONG = static_cast<int32_t>(0x80010032);
constexpr uint32_t CELL_ENOTSUP_CODE = 0x80010037;

bool is_ro(const std::string& guest) {
    std::string g = guest;
    for (auto& ch : g) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (g == "/dev_hdd0/game/bces01741" || g.rfind("/dev_hdd0/game/bces01741/", 0) == 0) return true;
    return g.rfind("/dev_bdvd", 0) == 0 || g.rfind("/dev_ps2disc", 0) == 0;
}

uint32_t block_size(const std::string& guest) {
    if (guest.rfind("/dev_hdd0", 0) == 0) return 4096;
    if (guest.rfind("/dev_hdd1", 0) == 0) return 32768;
    if (guest.rfind("/dev_bdvd", 0) == 0) return 2048;
    if (guest.rfind("/dev_flash", 0) == 0) return 8192;
    if (guest.rfind("/dev_usb", 0) == 0) return 4096;
    return 512;
}

bool no_uid(const std::string& guest) { return guest.rfind("/dev_hdd0", 0) != 0; }

int64_t read_path(uint32_t addr, std::string& out) {
    if (!addr) return CELL_EFAULT;
    const char* p = reinterpret_cast<const char*>(vm::ptr(addr));
    size_t n = strnlen(p, 1025);
    if (n > 1024) return CELL_ENAMETOOLONG;
    out.assign(p, n);
    if (out.empty() || out[0] != '/') return CELL_ENOENT;
    return CELL_OK;
}

int64_t resolve(uint32_t addr, std::string& guest, std::string& host) {
    if (int64_t e = read_path(addr, guest)) return e;
    host = vfs::host_path(guest);
    if (host.empty()) return CELL_ENOTMOUNTED;
    return CELL_OK;
}

uint32_t alloc_fd(std::shared_ptr<Fd> fd) {
    std::lock_guard lk(g_lock);
    for (uint32_t i = 3; i < 255; i++) {
        if (!g_fds.count(i)) {
            g_fds[i] = std::move(fd);
            return i;
        }
    }
    return 0;
}

std::shared_ptr<Fd> get_fd(uint32_t fd) {
    std::lock_guard lk(g_lock);
    auto it = g_fds.find(fd);
    return it == g_fds.end() ? nullptr : it->second;
}

void write_stat(uint32_t sb, bool dir, uint64_t size, int64_t atime, int64_t mtime, int64_t ctime, const std::string& guest) {
    bool ro = is_ro(guest);
    uint32_t mode = dir ? (0040000 | 0777) : (0100000 | 0666);
    if (ro) mode &= ~0222u;
    int32_t id = no_uid(guest) ? -1 : 0;
    vm::write32(sb, mode);
    vm::write32(sb + 4, static_cast<uint32_t>(id));
    vm::write32(sb + 8, static_cast<uint32_t>(id));
    vm::write64(sb + 0xC, static_cast<uint64_t>(atime));
    vm::write64(sb + 0x14, static_cast<uint64_t>(mtime));
    vm::write64(sb + 0x1C, static_cast<uint64_t>(ctime));
    vm::write64(sb + 0x24, dir ? block_size(guest) : size);
    vm::write64(sb + 0x2C, block_size(guest));
}

int64_t host_stat(const std::string& host, struct _stat64& st) {
    if (_stat64(host.c_str(), &st) != 0) return CELL_ENOENT;
    return CELL_OK;
}

void fill_dirent(uint32_t d, const DirEntry& e) {
    vm::ptr(d)[0] = e.dir ? 1 : 2;
    size_t n = std::min<size_t>(e.name.size(), 255);
    vm::ptr(d)[1] = static_cast<uint8_t>(n);
    std::memcpy(vm::ptr(d + 2), e.name.data(), n);
    vm::ptr(d + 2)[n] = 0;
}

int64_t do_read(Fd& fd, uint32_t buf, uint64_t n, uint64_t& got) {
    got = 0;
    if (fd.is_dir || !fd.f) return CELL_EBADF;
    if ((fd.flags & O_ACCMODE) == O_WRONLY) return CELL_EBADF;
    int64_t pos = trace::on(trace::FS) ? _ftelli64(fd.f) : 0;
    vm::host_write(buf, n);
    got = std::fread(vm::ptr(buf), 1, n, fd.f);
    if (trace::on(trace::FS)) {
        if (!fd.path_id) fd.path_id = trace::intern(fd.guest);
        trace::emit(trace::T_FS_READ, fd.path_id, static_cast<uint64_t>(pos), n, buf, got, fd.number);
    }
    return CELL_OK;
}

int64_t do_write(Fd& fd, uint32_t buf, uint64_t n, uint64_t& put) {
    put = 0;
    if (fd.is_dir || !fd.f) return CELL_EBADF;
    if ((fd.flags & O_ACCMODE) == 0) return CELL_EBADF;
    if (trace::on(trace::FS)) {
        if (!fd.path_id) fd.path_id = trace::intern(fd.guest);
        trace::emit(trace::T_FS_WRITE, fd.path_id, static_cast<uint64_t>(_ftelli64(fd.f)), n, buf, 0, fd.number);
    }
    if (fd.readonly_mount) return CELL_EROFS;
    if (fd.flags & O_APPEND) _fseeki64(fd.f, 0, SEEK_END);
    put = std::fwrite(vm::ptr(buf), 1, n, fd.f);
    return CELL_OK;
}

}

LV2_SYSCALL(801, sys_fs_open) {
    if (const char* k = std::getenv("GOWA_OPEN_STACK")) {
        std::string g;
        if (!read_path(u32arg(ctx, 0), g) && g.find(k) != std::string::npos) crash::log_here(g.c_str());
    }
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) {
        /* A path that does not resolve is the single most common reason a title
         * finds none of its own files, and returning the error silently sends
         * the hunt to the wrong subsystem. */
        LOG_WARN("fs", "open: cannot resolve '%.*s' -> 0x%llx",
                 static_cast<int>(strnlen(reinterpret_cast<const char*>(vm::ptr(u32arg(ctx, 0))), 512)),
                 reinterpret_cast<const char*>(vm::ptr(u32arg(ctx, 0))),
                 static_cast<unsigned long long>(e));
        return e;
    }
    int32_t flags = s32arg(ctx, 1);
    uint32_t out = u32arg(ctx, 2);
    bool ro = is_ro(guest);
    std::error_code ec;

    /* Transparent NPDRM: a file whose first four bytes are "NPD\0" is an
     * encrypted SDATA/EDATA container. Red Dead Redemption ships its whole game
     * archive that way (rdr2_installed.sdat), so the title was parsing ciphertext
     * as if it were an RPF6 archive, failing, and never starting its renderer.
     *
     * edat_resolve() decrypts once into "<name>.dec" beside the source and returns
     * that path; afterwards every syscall -- read, lseek, fstat, size -- operates on
     * the plaintext file, so nothing else in the FS layer needs to know. The cache
     * is reused while it is newer than the source, so the decrypt cost is paid once
     * per title, not once per run (539 MB at startup would otherwise be a 40 s
     * stall every time).
     *
     * Decryption failing is not fatal: the original path is returned and the guest
     * gets its own error from the data it reads, which is a better signal than an
     * open() the caller cannot attribute. PS3_EDAT=0 disables the whole path. */
    {
        static char dec_buf[4096];
        const char* dec = edat_resolve(host.c_str(), dec_buf, sizeof dec_buf);
        if (dec && dec != host.c_str() && std::strcmp(dec, host.c_str()) != 0) {
            LOG_INFO("fs", "open '%s' -> decrypted '%s'", guest.c_str(), dec);
            host = dec;
        }
    }

    if (fsys::is_directory(host, ec)) {
        LOG_WARN("fs", "open '%s' -> EISDIR (%s)", guest.c_str(), host.c_str());
        return CELL_EISDIR;
    }
    bool exists = fsys::exists(host, ec);
    int32_t acc = flags & O_ACCMODE;
    if (ro && (acc != 0 || (flags & (O_TRUNC | O_CREAT)))) {
        if (!exists) return CELL_ENOENT;
        flags &= ~(O_ACCMODE | O_TRUNC | O_CREAT | O_APPEND);
        acc = 0;
    }
    if (exists && (flags & O_CREAT) && (flags & O_EXCL)) return CELL_EEXIST;
    if (!exists && !(flags & O_CREAT)) {
        LOG_WARN("fs", "open '%s' -> ENOENT (host '%s')", guest.c_str(), host.c_str());
        return CELL_ENOENT;
    }
    const char* mode = "rb";
    if (acc == 0) mode = "rb";
    else if (!exists || (flags & O_TRUNC)) mode = acc == O_WRONLY ? "wb" : "w+b";
    else mode = "r+b";
    FILE* f = std::fopen(host.c_str(), mode);
    if (!f) {
        LOG_WARN("fs", "open '%s' -> %s (host '%s', mode %s)", guest.c_str(),
                 exists ? "EIO" : "ENOENT", host.c_str(), mode);
        return exists ? CELL_EIO : CELL_ENOENT;
    }
    auto fd = std::make_shared<Fd>();
    fd->f = f;
    fd->guest = guest;
    fd->host = host;
    fd->flags = flags;
    fd->readonly_mount = ro;
    uint32_t id = alloc_fd(fd);
    if (!id) {
        std::fclose(f);
        return CELL_EMFILE;
    }
    vm::write32(out, id);
    fd->number = id;
    if (trace::on(trace::FS)) {
        fd->path_id = trace::intern(guest);
        trace::emit(trace::T_FS_OPEN, fd->path_id, static_cast<uint32_t>(flags), id);
    }
    LOG_INFO("fs", "open '%s' flags 0%o -> %u", guest.c_str(), flags, id);
    return CELL_OK;
}

LV2_SYSCALL(802, sys_fs_read) {
    auto fd = get_fd(u32arg(ctx, 0));
    uint32_t buf = u32arg(ctx, 1);
    uint64_t n = u64arg(ctx, 2);
    uint32_t nread = u32arg(ctx, 3);
    if (!nread) return CELL_EFAULT;
    if (!fd) return CELL_EBADF;
    uint64_t got = 0;
    int64_t r;
    {
        std::lock_guard io(fd->io);
        r = n ? do_read(*fd, buf, n, got) : CELL_OK;
    }
    static const double us_per_kb = std::getenv("GOWA_FS_US_PER_KB") ? std::atof(std::getenv("GOWA_FS_US_PER_KB")) : 0.0;
    if (us_per_kb > 0.0 && got) host::sleep_us(static_cast<uint64_t>(us_per_kb * double(got) / 1024.0));
    /* PS3BASE_FS_LOG=1: requested against returned, per read. A short read is
     * silent corruption further down -- the title parses a truncated archive and
     * reports a missing entry, which looks like a mount problem and is not. */
    static const bool fs_log = std::getenv("PS3BASE_FS_LOG") != nullptr;
    if (fs_log) {
        LOG_INFO("fs", "read fd %u req %llu got %llu -> 0x%llx",
                 u32arg(ctx, 0), static_cast<unsigned long long>(n),
                 static_cast<unsigned long long>(got),
                 static_cast<unsigned long long>(r));
    }
    vm::write64(nread, got);
    return r;
}

LV2_SYSCALL(803, sys_fs_write) {
    auto fd = get_fd(u32arg(ctx, 0));
    uint32_t nwrite = u32arg(ctx, 3);
    if (!nwrite) return CELL_EFAULT;
    if (!fd) return CELL_EBADF;
    uint64_t put = 0;
    int64_t r;
    {
        std::lock_guard io(fd->io);
        r = do_write(*fd, u32arg(ctx, 1), u64arg(ctx, 2), put);
    }
    vm::write64(nwrite, put);
    return r;
}

LV2_SYSCALL(804, sys_fs_close) {
    std::shared_ptr<Fd> fd;
    {
        std::lock_guard lk(g_lock);
        auto it = g_fds.find(u32arg(ctx, 0));
        if (it == g_fds.end() || it->second->is_dir) return CELL_EBADF;
        fd = it->second;
        g_fds.erase(it);
    }
    if (fd->f) std::fclose(fd->f);
    if (trace::on(trace::FS)) trace::emit(trace::T_FS_CLOSE, fd->path_id, fd->number);
    return CELL_OK;
}

LV2_SYSCALL(805, sys_fs_opendir) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    std::error_code ec;
    if (!fsys::exists(host, ec)) return CELL_ENOENT;
    if (!fsys::is_directory(host, ec)) return CELL_ENOTDIR;
    auto fd = std::make_shared<Fd>();
    fd->is_dir = true;
    fd->guest = guest;
    fd->host = host;
    fd->entries.push_back({".", true, 0, 0});
    fd->entries.push_back({"..", true, 0, 0});
    std::vector<DirEntry> list;
    for (auto& e : fsys::directory_iterator(host, ec)) {
        DirEntry d;
        d.name = e.path().filename().string();
        d.dir = e.is_directory(ec);
        d.size = d.dir ? 0 : e.file_size(ec);
        d.mtime = 0;
        list.push_back(d);
    }
    std::sort(list.begin(), list.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
    fd->entries.insert(fd->entries.end(), list.begin(), list.end());
    uint32_t id = alloc_fd(fd);
    if (!id) return CELL_EMFILE;
    vm::write32(u32arg(ctx, 1), id);
    return CELL_OK;
}

LV2_SYSCALL(806, sys_fs_readdir) {
    auto fd = get_fd(u32arg(ctx, 0));
    uint32_t dir = u32arg(ctx, 1), nread = u32arg(ctx, 2);
    if (!dir || !nread) return CELL_EFAULT;
    if (!fd || !fd->is_dir) return CELL_EBADF;
    if (fd->dir_pos >= fd->entries.size()) {
        std::memset(vm::ptr(dir), 0, 3);
        vm::write64(nread, 0);
        return CELL_OK;
    }
    fill_dirent(dir, fd->entries[fd->dir_pos++]);
    vm::write64(nread, 0x102);
    return CELL_OK;
}

LV2_SYSCALL(807, sys_fs_closedir) {
    std::lock_guard lk(g_lock);
    auto it = g_fds.find(u32arg(ctx, 0));
    if (it == g_fds.end() || !it->second->is_dir) return CELL_EBADF;
    g_fds.erase(it);
    return CELL_OK;
}

LV2_SYSCALL(808, sys_fs_stat) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    struct _stat64 st;
    if (host_stat(host, st)) {
#if defined(PS3BASE_FS_PROBE) && PS3BASE_FS_PROBE
        /* The boot freezes at a sys_fs_stat call site; LOG_DEBUG is invisible in the
         * wasm run, so name the path that is failing. */
        {
            static std::atomic<uint64_t> miss_n{0};
            const uint64_t miss = miss_n.fetch_add(1) + 1;
            if ((miss & 0x3FF) == 0) LOG_INFO("fsratio", "stat miss=%llu", miss);
        }
        static std::atomic<int> n{0};
        if (n.fetch_add(1) < 25)
            LOG_WARN("fsprobe", "stat '%s' -> ENOENT (host '%s')", guest.c_str(), host.c_str());
#endif
        LOG_DEBUG("fs", "stat '%s' -> ENOENT", guest.c_str());
        return CELL_ENOENT;
    }
    write_stat(u32arg(ctx, 1), (st.st_mode & _S_IFDIR) != 0, static_cast<uint64_t>(st.st_size), st.st_atime, st.st_mtime, st.st_ctime, guest);
    LOG_DEBUG("fs", "stat '%s' -> size %llu mtime %lld", guest.c_str(), static_cast<unsigned long long>(st.st_size), static_cast<long long>(st.st_mtime));
    return CELL_OK;
}

/* cellFsChmod / sys_fs_chmod (834). refs/.../sys_fs.cpp:3420 makes this a
 * todo() that reports success without touching the file -- the "never a fake
 * success" case AGENTS.md forbids: a title told its file has the mode it asked
 * for then writes where the host refuses.
 *
 * Red Dead Redemption calls it through libfs at 0x34004964 right after creating
 * its install file; the ENOSYS return there stopped the installation with no
 * error and no progress. The progress dialog sat at zero and the title never
 * reached its renderer.
 *
 * The guest mode is a 32-bit CellFsMode whose low 12 bits are permission bits.
 * A host that refuses is reported as CELL_EACCES, not swallowed. */
/* cellFsUtime / sys_fs_utime (815), refs/.../sys_fs.cpp. RPCS3 logs and returns
 * CELL_OK without setting anything; this runtime sets the real timestamps,
 * because an installer that reads back the mtime it just wrote and finds the old
 * one decides its copy never happened.
 *
 * Reached in Red Dead Redemption from libfs at 0x34005db0, immediately after
 * sys_fs_chmod: fixing one took the title straight to the next missing syscall.
 * The two CellFsUtimbuf fields are 64-bit seconds (actime, modtime). */
LV2_SYSCALL(815, sys_fs_utime) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    const uint32_t buf = u32arg(ctx, 1);
    if (!buf) return CELL_EFAULT;
    /* CellFsUtimbuf is two 64-bit seconds: actime then modtime. */
    const int64_t actime = static_cast<int64_t>(vm::read64(buf));
    const int64_t modtime = static_cast<int64_t>(vm::read64(buf + 8));
    std::error_code ec;
    if (!std::filesystem::exists(host, ec)) return CELL_ENOENT;
    using namespace std::chrono;
    const auto sys_tp = system_clock::time_point(seconds(modtime));
    const auto file_tp = std::filesystem::file_time_type::clock::from_sys(sys_tp);
    std::filesystem::last_write_time(host, file_tp, ec);
    if (ec) {
        LOG_WARN("fs", "utime %s refused: %s", guest.c_str(), ec.message().c_str());
        return CELL_EIO;
    }
    (void)actime;
    LOG_DEBUG("fs", "utime %s -> %lld", guest.c_str(), static_cast<long long>(modtime));
    return CELL_OK;
}

LV2_SYSCALL(834, sys_fs_chmod) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    const uint32_t mode = u32arg(ctx, 1);
    std::error_code ec;
    if (!std::filesystem::exists(host, ec)) return CELL_ENOENT;
    std::filesystem::permissions(host,
        static_cast<std::filesystem::perms>(mode & 0777),
        std::filesystem::perm_options::replace, ec);
    if (ec) {
        LOG_WARN("fs", "chmod %s mode 0%o refused: %s", guest.c_str(), mode & 0777, ec.message().c_str());
        return CELL_EACCES;
    }
    LOG_DEBUG("fs", "chmod %s -> 0%o", guest.c_str(), mode & 0777);
    return CELL_OK;
}

LV2_SYSCALL(809, sys_fs_fstat) {
    auto fd = get_fd(u32arg(ctx, 0));
    if (!fd) return CELL_EBADF;
    struct _stat64 st;
    if (host_stat(fd->host, st)) return CELL_EIO;
    if (fd->f) {
        int64_t pos = _ftelli64(fd->f);
        _fseeki64(fd->f, 0, SEEK_END);
        st.st_size = _ftelli64(fd->f);
        _fseeki64(fd->f, pos, SEEK_SET);
    }
    write_stat(u32arg(ctx, 1), fd->is_dir, static_cast<uint64_t>(st.st_size), st.st_atime, st.st_mtime, st.st_ctime, fd->guest);
    return CELL_OK;
}

LV2_SYSCALL(811, sys_fs_mkdir) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    if (is_ro(guest)) return CELL_EROFS;
    std::error_code ec;
    if (fsys::exists(host, ec)) return CELL_EEXIST;
    if (!fsys::exists(fsys::path(host).parent_path(), ec)) return CELL_ENOENT;
    fsys::create_directory(host, ec);
    return ec ? CELL_EIO : CELL_OK;
}

LV2_SYSCALL(812, sys_fs_rename) {
    std::string g1, h1, g2, h2;
    if (int64_t e = resolve(u32arg(ctx, 0), g1, h1)) return e;
    if (int64_t e = resolve(u32arg(ctx, 1), g2, h2)) return e;
    if (is_ro(g1) || is_ro(g2)) return CELL_EROFS;
    std::error_code ec;
    if (!fsys::exists(h1, ec)) return CELL_ENOENT;
    if (fsys::exists(h2, ec)) return CELL_EEXIST;
    fsys::rename(h1, h2, ec);
    return ec ? CELL_EIO : CELL_OK;
}

LV2_SYSCALL(813, sys_fs_rmdir) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    if (is_ro(guest)) return CELL_EROFS;
    std::error_code ec;
    if (!fsys::exists(host, ec)) return CELL_ENOENT;
    if (!fsys::is_directory(host, ec)) return CELL_ENOTDIR;
    if (!fsys::is_empty(host, ec)) return CELL_ENOTEMPTY;
    fsys::remove(host, ec);
    return ec ? CELL_EIO : CELL_OK;
}

LV2_SYSCALL(814, sys_fs_unlink) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    if (is_ro(guest)) return CELL_EROFS;
    std::error_code ec;
    if (!fsys::exists(host, ec)) return CELL_ENOENT;
    if (fsys::is_directory(host, ec)) return CELL_EISDIR;
    fsys::remove(host, ec);
    return ec ? CELL_EIO : CELL_OK;
}

LV2_SYSCALL(816, sys_fs_access) {
    return CELL_OK;
}

LV2_SYSCALL(818, sys_fs_lseek) {
    auto fd = get_fd(u32arg(ctx, 0));
    if (!fd || fd->is_dir || !fd->f) return CELL_EBADF;
    int64_t off = static_cast<int64_t>(u64arg(ctx, 1));
    int32_t whence = s32arg(ctx, 2);
    if (whence < 0 || whence > 2) return CELL_EINVAL;
    std::lock_guard io(fd->io);
    int64_t base_pos = whence == 0 ? 0 : whence == 1 ? _ftelli64(fd->f) : (_fseeki64(fd->f, 0, SEEK_END), _ftelli64(fd->f));
    int64_t np = base_pos + off;
    if (np < 0) return CELL_EINVAL;
    _fseeki64(fd->f, np, SEEK_SET);
    if (uint32_t pos = u32arg(ctx, 3)) vm::write64(pos, static_cast<uint64_t>(np));
    return CELL_OK;
}

LV2_SYSCALL(819, sys_fs_fdatasync) {
    auto fd = get_fd(u32arg(ctx, 0));
    if (!fd) return CELL_EBADF;
    if (fd->f) std::fflush(fd->f);
    return CELL_OK;
}

LV2_SYSCALL(820, sys_fs_fsync) {
    auto fd = get_fd(u32arg(ctx, 0));
    if (!fd) return CELL_EBADF;
    if (fd->f) std::fflush(fd->f);
    return CELL_OK;
}

LV2_SYSCALL(831, sys_fs_truncate) {
    std::string guest, host;
    if (int64_t e = resolve(u32arg(ctx, 0), guest, host)) return e;
    if (is_ro(guest)) return CELL_EROFS;
    std::error_code ec;
    if (!fsys::exists(host, ec)) return CELL_ENOENT;
    fsys::resize_file(host, u64arg(ctx, 1), ec);
    return ec ? CELL_EIO : CELL_OK;
}

LV2_SYSCALL(832, sys_fs_ftruncate) {
    auto fd = get_fd(u32arg(ctx, 0));
    if (!fd || !fd->f) return CELL_EBADF;
    if (fd->readonly_mount) return CELL_EROFS;
    std::fflush(fd->f);
    return _chsize_s(_fileno(fd->f), static_cast<int64_t>(u64arg(ctx, 1))) == 0 ? CELL_OK : CELL_EIO;
}

LV2_SYSCALL(840, sys_fs_disk_free) {
    std::string guest;
    uint32_t p = u32arg(ctx, 0);
    if (!p) return CELL_EFAULT;
    guest = reinterpret_cast<const char*>(vm::ptr(p));
    uint64_t avail = is_ro(guest) ? 0 : (40ull << 30) - 512;
    if (uint32_t t = u32arg(ctx, 1)) vm::write64(t, avail);
    if (uint32_t a = u32arg(ctx, 2)) vm::write64(a, avail);
    return CELL_OK;
}

LV2_SYSCALL(817, sys_fs_fcntl) {
    uint32_t fdn = u32arg(ctx, 0);
    uint32_t op = u32arg(ctx, 1);
    uint32_t arg = u32arg(ctx, 2);
    uint32_t size = u32arg(ctx, 3);
#if defined(PS3BASE_FS_PROBE) && PS3BASE_FS_PROBE
    {
        /* Which fcntl operation does the boot keep issuing, and does it succeed? */
        static std::atomic<uint64_t> hist[8];
        static std::atomic<int> n{0};
        const int slot = op == 0x80000004 ? 0 : op == 0x8000000A ? 1 : op == 0x8000000B ? 2
                       : op == 0x80000009 ? 3 : 4;
        hist[slot].fetch_add(1);
        if (n.fetch_add(1) % 100 == 99)
            LOG_INFO("fcntlprobe", "op 0x%08x fdn %u size %u (slots 04:%llu 0A:%llu 0B:%llu 09:%llu other:%llu)",
                     op, fdn, size,
                     (unsigned long long)hist[0].load(), (unsigned long long)hist[1].load(),
                     (unsigned long long)hist[2].load(), (unsigned long long)hist[3].load(),
                     (unsigned long long)hist[4].load());
    }
#endif
    switch (op) {
    case 0x80000004:
        if (size > 4) return CELL_EINVAL;
        vm::write32(arg, 0);
        return CELL_OK;
    case 0x8000000A:
    case 0x8000000B: {
        if (size < 0x38) return CELL_EINVAL;
        auto fd = get_fd(vm::read32(arg + 0x10));
        if (!fd || !fd->f) return CELL_EBADF;
        std::lock_guard io(fd->io);
        int64_t old = _ftelli64(fd->f);
        _fseeki64(fd->f, static_cast<int64_t>(vm::read64(arg + 0x18)), SEEK_SET);
        uint64_t done = 0;
        int64_t r = op == 0x8000000A ? do_read(*fd, vm::read32(arg + 0x14), vm::read64(arg + 0x20), done)
                                     : do_write(*fd, vm::read32(arg + 0x14), vm::read64(arg + 0x20), done);
        _fseeki64(fd->f, old, SEEK_SET);
        if (r) return r;
        vm::write32(arg + 0x28, 0);
        vm::write64(arg + 0x30, done);
        return CELL_OK;
    }
    case 0xC0000002:
        vm::write32(arg + 0x18, 0);
        vm::write32(arg + 0x1C, 4096);
        vm::write64(arg + 0x20, ((40ull << 30) - 1) / 4096);
        return CELL_OK;
    case 0xC0000006:
        /* MEASURED: answering this as a capacity (block size + count) BREAKS the
         * boot -- assets stop loading entirely (0 MediaPS3/pngdec events instead of
         * 125) and the pc falls back to 0x003a18b0. The title treats a valid answer
         * here as a precondition it then tests differently. ENOTSUP is what makes it
         * take the path that loads assets, so keep it.
         *
         * What this op actually queries is unresolved; the retry loop it causes is
         * downstream of the real stall, not the stall itself. */
        if (size == 0x20 && vm::read32(arg) == 0x20 && vm::read32(arg + 4) == 0x10 && vm::read32(arg + 8) == 0x18) {
            vm::write32(arg + 0x18, CELL_ENOTSUP_CODE);
            vm::write32(arg + 0x1C, 0);
        }
        return CELL_OK;
    case 0xC0000008:
        vm::write32(arg + 0x20, 0);
        return CELL_OK;
    case 0xE0000012: {
        if (size < 0x10) return CELL_EINVAL;
        auto fd = get_fd(fdn);
        if (!fd || !fd->is_dir) return CELL_EBADF;
        uint32_t ptr = vm::read32(arg + 8);
        uint32_t max = vm::read32(arg + 0xC);
        uint32_t n = 0;
        if (max && fd->dir_pos < fd->entries.size()) {
            const DirEntry& e = fd->entries[fd->dir_pos++];
            std::string guest = fd->guest + "/" + e.name;
            struct _stat64 st{};
            host_stat(fd->host + "/" + e.name, st);
            write_stat(ptr, e.dir, e.size, st.st_atime, st.st_mtime, st.st_ctime, guest);
            fill_dirent(ptr + 0x34, e);
            n = 1;
        }
        if (max > n) std::memset(vm::ptr(ptr + n * 0x138), 0, (max - n) * 0x138);
        vm::write32(arg + 4, n);
        vm::write32(arg, 0);
        return CELL_OK;
    }
    default:
        LOG_DEBUG("fs", "fcntl op 0x%x on fd %u ignored", op, fdn);
        return CELL_OK;
    }
}

}
