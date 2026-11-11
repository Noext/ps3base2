/* ps3base -- cellSysutilAvconfExt::cellVideoOutSetGamma, tested against the
 * reference, through the SAME header the import uses
 * (runtime/src/hle/avconf_hle.h).
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAvconfExt.cpp:448-467.
 * RPCS3 validates in this order -- range first, then videoOut -- stores the
 * value in its avconf object, and its present path builds an sRGB conversion
 * from it (GL/GLPresent.cpp:438-445, VK/VKPresent.cpp:850-886).
 *
 * This runtime has no gamma stage: presenter.cpp blits straight to the
 * swapchain with vkCmdBlitImage (runtime/src/gpu/presenter.cpp:1025-1043).
 * Storing a number nothing reads and answering CELL_OK would be the fake
 * success the depot forbids -- the title would believe its gamma took effect
 * while the picture stayed identical. So:
 *   - the argument validation is firmware's, byte for byte;
 *   - gamma == 1.0 returns CELL_OK, because that transformation IS the
 *     identity, so the success is true;
 *   - any other in-range value returns CELL_VIDEO_OUT_ERROR_NOT_IMPLEMENTED
 *     and the value that was refused is logged.
 */
#include "hle/avconf_hle.h"

#include <cmath>
#include <cstdio>

namespace av = hle::avconf;

static int fails = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; std::fprintf(stderr, "[FAIL] %s\n", msg); } \
} while (0)

int main() {
    /* The constants, refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVideoOut.h:8-16, :21. */
    CHECK(av::ERR_NOT_IMPLEMENTED == static_cast<int32_t>(0x8002b220),
          "CELL_VIDEO_OUT_ERROR_NOT_IMPLEMENTED is 0x8002b220");
    CHECK(av::ERR_PARAMETER_OUT_OF_RANGE == static_cast<int32_t>(0x8002b223),
          "CELL_VIDEO_OUT_ERROR_PARAMETER_OUT_OF_RANGE is 0x8002b223");
    CHECK(av::ERR_UNSUPPORTED_VIDEO_OUT == static_cast<int32_t>(0x8002b225),
          "CELL_VIDEO_OUT_ERROR_UNSUPPORTED_VIDEO_OUT is 0x8002b225");
    CHECK(av::VIDEO_OUT_PRIMARY == 0, "CELL_VIDEO_OUT_PRIMARY is 0");

    /* --- range check first (cellAvconfExt.cpp:452-455) ------------------ */
    CHECK(av::set_gamma(0, 0.5f) == av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 0.5 is out of range");
    CHECK(av::set_gamma(0, 0.79f) == av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 0.79 is out of range");
    CHECK(av::set_gamma(0, 0.8f) != av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 0.8 is in range (boundary)");
    CHECK(av::set_gamma(0, 1.2f) != av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 1.2 is in range (boundary)");
    CHECK(av::set_gamma(0, 1.21f) == av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 1.21 is out of range");
    CHECK(av::set_gamma(0, 2.0f) == av::ERR_PARAMETER_OUT_OF_RANGE, "gamma 2.0 is out of range");
    CHECK(av::set_gamma(0, std::nanf("")) == av::ERR_PARAMETER_OUT_OF_RANGE,
          "a NaN gamma is refused as out of range, not accepted by a sloppy comparison");

    /* --- videoOut check second (:457-460) ------------------------------- */
    CHECK(av::set_gamma(1, 1.0f) == av::ERR_UNSUPPORTED_VIDEO_OUT,
          "a non-primary videoOut is UNSUPPORTED_VIDEO_OUT");
    CHECK(av::set_gamma(1, 0.5f) == av::ERR_PARAMETER_OUT_OF_RANGE,
          "the range is checked BEFORE videoOut, as RPCS3 orders it");

    /* --- the identity case is a TRUE success ------------------------ */
    CHECK(av::set_gamma(0, 1.0f) == 0,
          "gamma 1.0 returns CELL_OK: applying the identity IS doing nothing");
    CHECK(av::requested_gamma() == 1.0f, "the requested gamma is recorded");

    /* --- any other value is honestly refused --------------------------- */
    CHECK(av::set_gamma(0, 1.1f) == av::ERR_NOT_IMPLEMENTED,
          "gamma 1.1 returns NOT_IMPLEMENTED rather than a CELL_OK that changed nothing");
    CHECK(av::set_gamma(0, 0.9f) == av::ERR_NOT_IMPLEMENTED, "gamma 0.9 returns NOT_IMPLEMENTED");
    CHECK(av::set_gamma(0, 0.8f) == av::ERR_NOT_IMPLEMENTED,
          "the lower boundary is accepted by validation and then refused as not implemented");

    /* The identity test's own tolerance: a value within one 8-bit step is the
     * same picture, anything beyond it is not. */
    CHECK(av::is_identity(1.0f), "1.0 is the identity");
    CHECK(av::is_identity(1.0f + 1.0f / 512.0f), "a deviation under 1/255 is the identity");
    CHECK(!av::is_identity(1.0f + 1.0f / 128.0f), "a deviation over 1/255 is not the identity");

    std::printf("avconf: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
