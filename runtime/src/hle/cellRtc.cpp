#include "common.h"

/* cellRtc: real-time clock. The title calls these while setting up a world and
 * takes an unimplemented one as a fatal condition, so they return real values
 * rather than zeros.
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellRtc.cpp, cellRtc.h:25.
 *
 * CellRtcDateTime is seven fields, all big-endian:
 *   u16 year, month, day, hour, minute, second; u32 microsecond   (18 bytes)
 * CellRtcTick is one big-endian u64 counted in microseconds since 0001-01-01.
 */

#include <ctime>

namespace hle {

namespace {

constexpr uint64_t RTC_MAGIC_OFFSET = 62135596800000000ull;  /* 1970-01-01 in ticks */

void fill_now(uint32_t p) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    vm::write16(p + 0, static_cast<uint16_t>(tm.tm_year + 1900));
    vm::write16(p + 2, static_cast<uint16_t>(tm.tm_mon + 1));
    vm::write16(p + 4, static_cast<uint16_t>(tm.tm_mday));
    vm::write16(p + 6, static_cast<uint16_t>(tm.tm_hour));
    vm::write16(p + 8, static_cast<uint16_t>(tm.tm_min));
    vm::write16(p + 10, static_cast<uint16_t>(tm.tm_sec));
    vm::write32(p + 12, 0);
}

uint64_t now_ticks() {
    return static_cast<uint64_t>(std::time(nullptr)) * 1000000ull + RTC_MAGIC_OFFSET;
}

}  // namespace

HLE_FUNC(cellRtc, cellRtcGetCurrentClockLocalTime) {
    const uint32_t p = a32(ctx, 0);
    if (!p || !vm::range_mapped(p, 18)) return ret(ctx, static_cast<int32_t>(0x80010508));
    fill_now(p);
    ret(ctx, 0);
}

HLE_FUNC(cellRtc, cellRtcGetCurrentClock) {
    const uint32_t p = a32(ctx, 0);
    if (!p || !vm::range_mapped(p, 18)) return ret(ctx, static_cast<int32_t>(0x80010508));
    fill_now(p);
    ret(ctx, 0);
}

HLE_FUNC(cellRtc, cellRtcGetCurrentTick) {
    const uint32_t p = a32(ctx, 0);
    if (!p || !vm::range_mapped(p, 8)) return ret(ctx, static_cast<int32_t>(0x80010508));
    vm::write64(p, now_ticks());
    ret(ctx, 0);
}

HLE_FUNC(cellRtc, cellRtcSetTick) {
    const uint32_t p = a32(ctx, 0);
    const uint32_t src = a32(ctx, 1);
    if (!p || !src || !vm::range_mapped(p, 18) || !vm::range_mapped(src, 8))
        return ret(ctx, static_cast<int32_t>(0x80010508));
    const uint64_t ticks = vm::read64(src);
    const std::time_t secs = static_cast<std::time_t>((ticks - RTC_MAGIC_OFFSET) / 1000000ull);
    std::tm tm{};
    localtime_r(&secs, &tm);
    vm::write16(p + 0, static_cast<uint16_t>(tm.tm_year + 1900));
    vm::write16(p + 2, static_cast<uint16_t>(tm.tm_mon + 1));
    vm::write16(p + 4, static_cast<uint16_t>(tm.tm_mday));
    vm::write16(p + 6, static_cast<uint16_t>(tm.tm_hour));
    vm::write16(p + 8, static_cast<uint16_t>(tm.tm_min));
    vm::write16(p + 10, static_cast<uint16_t>(tm.tm_sec));
    vm::write32(p + 12, static_cast<uint32_t>(ticks % 1000000ull));
    ret(ctx, 0);
}

HLE_FUNC(cellRtc, cellRtcGetDayOfWeek) {
    /* Signature: (s32 year, s32 month, s32 day) -> 0..6, Sunday is 0. */
    const int year = static_cast<int32_t>(a32(ctx, 0));
    const int month = static_cast<int32_t>(a32(ctx, 1));
    const int day = static_cast<int32_t>(a32(ctx, 2));
    if (month < 1 || month > 12 || day < 1 || day > 31)
        return ret(ctx, static_cast<int32_t>(0x80010502));
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    const std::time_t t = mktime(&tm);
    ret(ctx, static_cast<int64_t>(t == -1 ? 0 : std::localtime(&t)->tm_wday));
}

}  // namespace hle
