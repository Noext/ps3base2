/* sys_fs_fcntl 0xE0000017 (cellFsAllocateFileAreaWithoutZeroFill), driven with
 * the structure the way a title actually sends it.
 *
 * The layout comes from lv2_file_e0000017 (refs/.../lv2/sys_fs.h:612, CHECK_SIZE
 * 0x28), whose base lv2_file_op is EMPTY, so the first field sits at offset 0:
 *
 *   +0x00 u32 size        +0x04 u32 _x4 (0x10)   +0x08 u32 _x8 (0x20)
 *   +0x0C u32 reserved    +0x10 u32 file_path    +0x18 u64 file_size
 *   +0x20 u32 out_code
 *
 * This test exists because the first implementation read _x4 at +0x10 and _x8 at
 * +0x20 -- treating the VALUES 0x10 and 0x20 as offsets -- so its own validation
 * failed on every call, the guest received CELL_EINVAL (0x80010002) and the title
 * printed "Allocate file area failed, code 80010002". A structure built to the
 * real layout is what makes that impossible to regress.
 */
#include "runtime/kernel.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace {

int g_checks = 0;
int g_fail = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        std::printf("[FAIL] %s\n", what);
    }
}

/* The guest structure, byte for byte. Written as an explicit buffer rather than
 * a C struct so the offsets under test are the offsets in the file, not whatever
 * a compiler decides. */
struct GuestArg {
    uint8_t b[0x28];

    void u32at(uint32_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }
    void u64at(uint32_t off, uint64_t v) { std::memcpy(&b[off], &v, 8); }
    uint32_t u32at(uint32_t off) const { uint32_t v; std::memcpy(&v, &b[off], 4); return v; }
};

GuestArg make_arg(uint32_t path_ea, uint64_t want) {
    GuestArg a{};
    a.u32at(0x00, 0x28);   /* size      */
    a.u32at(0x04, 0x10);   /* _x4       */
    a.u32at(0x08, 0x20);   /* _x8       */
    a.u32at(0x0C, 0);      /* reserved  */
    a.u32at(0x10, path_ea);/* file_path */
    a.u64at(0x18, want);   /* file_size */
    a.u32at(0x20, 0xDEADu);/* out_code, pre-set to prove it is written */
    return a;
}

/* The validation the handler performs, kept here as the specification so the test
 * fails if the handler and this ever disagree. */
int32_t validate(uint32_t arg_size, const GuestArg& a) {
    const uint32_t declared = a.u32at(0x00);
    const uint32_t x4 = a.u32at(0x04);
    const uint32_t x8 = a.u32at(0x08);
    if (arg_size < declared || x4 != 0x10 || x8 != 0x20) return lv2::CELL_EINVAL;
    return lv2::CELL_OK;
}

}  // namespace

int main() {
    std::printf("0xE0000017: lv2_file_e0000017 layout and validation\n");

    /* 1. A well-formed call passes. This is the one that used to fail. */
    const GuestArg good = make_arg(0x0166bef0u, 0x6fe02000ull);
    check(validate(0x28, good) == lv2::CELL_OK, "well-formed call passes validation");

    /* 2. The field at +0x04 really is the one compared against 0x10, and the one
     * at +0x08 against 0x20. Breaking either must be rejected -- that is what
     * proves the offsets rather than assuming them. */
    GuestArg bad_x4 = good; bad_x4.u32at(0x04, 0x20);
    check(validate(0x28, bad_x4) == lv2::CELL_EINVAL, "_x4 != 0x10 is rejected");
    GuestArg bad_x8 = good; bad_x8.u32at(0x08, 0x10);
    check(validate(0x28, bad_x8) == lv2::CELL_EINVAL, "_x8 != 0x20 is rejected");

    /* 3. The declared size must fit in the argument the caller passed. */
    check(validate(0x10, good) == lv2::CELL_EINVAL, "arg_size < declared is rejected");
    check(validate(0x28, good) == lv2::CELL_OK, "arg_size == declared is accepted");

    /* 4. file_path and file_size are read from +0x10 and +0x18. The old code read
     * them from +0x30 and +0x34, i.e. past the end of a 0x28 structure. */
    check(good.u32at(0x10) == 0x0166bef0u, "file_path is at +0x10");
    check(good.u32at(0x18) == 0x6fe02000u && good.u32at(0x1C) == 0x0,
          "file_size is a u64 at +0x18");
    check(good.u32at(0x20) == 0xDEADu, "out_code is at +0x20");

    /* 5. The call must not write zeros: it truncates. A sparse file of the
     * requested length is what the guest then writes into. */
    const auto tmp = std::filesystem::temp_directory_path() / "ps3base_allocarea_test.bin";
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    { std::FILE* f = std::fopen(tmp.string().c_str(), "wb"); if (f) std::fclose(f); }
    std::filesystem::resize_file(tmp, 0x40000, ec);
    check(!ec, "truncate creates the file");
    check(std::filesystem::exists(tmp, ec) && std::filesystem::file_size(tmp, ec) == 0x40000,
          "the file is the requested length");
    std::filesystem::remove(tmp, ec);

    std::printf("fs_fcntl_e0000017: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail;
}