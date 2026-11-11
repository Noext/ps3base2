#pragma once

#include "ppu/context.h"

#include <cstdint>

namespace lv2 {

enum : int64_t {
    CELL_OK = 0,
    CELL_EAGAIN = static_cast<int32_t>(0x80010001),
    CELL_EINVAL = static_cast<int32_t>(0x80010002),
    CELL_ENOSYS = static_cast<int32_t>(0x80010003),
    CELL_ENOMEM = static_cast<int32_t>(0x80010004),
    CELL_ESRCH = static_cast<int32_t>(0x80010005),
    CELL_ENOENT = static_cast<int32_t>(0x80010006),
    CELL_ENOEXEC = static_cast<int32_t>(0x80010007),
    CELL_EDEADLK = static_cast<int32_t>(0x80010008),
    CELL_EPERM = static_cast<int32_t>(0x80010009),
    CELL_EBUSY = static_cast<int32_t>(0x8001000A),
    CELL_ETIMEDOUT = static_cast<int32_t>(0x8001000B),
    CELL_EABORT = static_cast<int32_t>(0x8001000C),
    CELL_EFAULT = static_cast<int32_t>(0x8001000D),
    CELL_ECHILD = static_cast<int32_t>(0x8001000E),
    CELL_ESTAT = static_cast<int32_t>(0x8001000F),
    CELL_EALIGN = static_cast<int32_t>(0x80010010),
    CELL_EKRESOURCE = static_cast<int32_t>(0x80010011),
    CELL_EISDIR = static_cast<int32_t>(0x80010012),
    CELL_ECANCELED = static_cast<int32_t>(0x80010013),
    CELL_EEXIST = static_cast<int32_t>(0x80010014),
    CELL_EISCONN = static_cast<int32_t>(0x80010015),
    CELL_ENOTCONN = static_cast<int32_t>(0x80010016),
    CELL_EBADF = static_cast<int32_t>(0x8001002A),
    CELL_EIO = static_cast<int32_t>(0x8001002B),
    /* refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h:144. Used by sys_fs_chmod when the
     * host refuses the mode. */
    CELL_EACCES = static_cast<int32_t>(0x80010029),
    CELL_EMFILE = static_cast<int32_t>(0x8001002C),
    CELL_ENOTDIR = static_cast<int32_t>(0x8001002F),
    CELL_ENOTEMPTY = static_cast<int32_t>(0x8001003C),
    /* refs/rpcs3/rpcs3/Emu/Cell/ErrorCodes.h:158. NOT 0x8001003D, which is
     * CELL_ENOLICDISC (:164) -- a different error entirely. The wrong value made
     * a "not supported" answer indistinguishable from a licence-disc problem, and
     * it disagreed with runtime/src/lv2/fs.cpp which already had 0x80010037.
     * tests/error_codes.cpp pins every code in this enum against RPCS3. */
    CELL_ENOTSUP = static_cast<int32_t>(0x80010037),
    /* refs/.../ErrorCodes.h:164. Only here so the test can prove it is NOT what
     * CELL_ENOTSUP means; nothing returns it yet. */
    CELL_ENOLICDISC = static_cast<int32_t>(0x8001003D),
};

using SyscallFn = int64_t (*)(ppu::Context& ctx, uint8_t* base);

struct SyscallReg {
    SyscallReg(uint32_t number, const char* name, SyscallFn fn);
};

/* Is there lifted code at this guest address? Used to tell an OPD from a bare
 * code pointer: a title supplies both, in different places. */
bool has_code_at(uint32_t addr);

struct ThreadExit {
    uint64_t status;
};

struct ProcessExit {
    int32_t status;
};

const char* syscall_name(uint32_t number);

/* What the LV2 specification calls this number, whether or not it is
 * implemented. Null when the number is not in the table at all. */
const char* spec_name(uint32_t number);

}

#define LV2_SYSCALL(num, name) \
    static int64_t name(ppu::Context& ctx, uint8_t* base); \
    static lv2::SyscallReg reg_##name(num, #name, name); \
    static int64_t name(ppu::Context& ctx, uint8_t* base)
