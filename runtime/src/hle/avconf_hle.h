/* ps3base -- cellVideoOutSetGamma, kept in a header so a test drives the same
 * code the import runs.
 *
 * Reference: refs/rpcs3/rpcs3/Emu/Cell/Modules/cellAvconfExt.cpp:448-467.
 * RPCS3 validates the arguments in a specific order and then stores gamma in
 * its avconf object, which its present path uses to build an sRGB conversion
 * (refs/.../GL/GLPresent.cpp:438-445, VK/VKPresent.cpp:850-886).
 *
 * This runtime blits straight to the swapchain with vkCmdBlitImage
 * (runtime/src/gpu/presenter.cpp:1025-1043) and has no gamma stage, so storing
 * a number no code reads would be exactly the fake success the depot rules
 * forbid: the title would be told its gamma took effect while the picture was
 * unchanged. What the function does instead:
 *
 *   - validates as firmware does (out of range, wrong videoOut);
 *   - returns CELL_OK when the request is the identity (gamma == 1.0), because
 *     "apply gamma 1.0" and "do nothing" are the same transformation, so the
 *     success is true;
 *   - returns CELL_VIDEO_OUT_ERROR_NOT_IMPLEMENTED for any other in-range
 *     value, naming the number in the log, because this runtime cannot apply
 *     it. A title that asks for 1.1 is told the adjustment did not happen.
 *
 * The constants are from refs/rpcs3/rpcs3/Emu/Cell/Modules/cellVideoOut.h:8-16
 * and :21.
 */
#ifndef PS3BASE_HLE_AVCONF_H
#define PS3BASE_HLE_AVCONF_H

#include <cstdint>

namespace hle {
namespace avconf {

constexpr int32_t ERR_NOT_IMPLEMENTED = static_cast<int32_t>(0x8002b220);
constexpr int32_t ERR_PARAMETER_OUT_OF_RANGE = static_cast<int32_t>(0x8002b223);
constexpr int32_t ERR_UNSUPPORTED_VIDEO_OUT = static_cast<int32_t>(0x8002b225);

constexpr uint32_t VIDEO_OUT_PRIMARY = 0;

/* Accepted gamma window, refs/.../cellAvconfExt.cpp:452-455. */
constexpr float GAMMA_MIN = 0.8f;
constexpr float GAMMA_MAX = 1.2f;

/* The value the last accepted call asked for. Read by the tests and by the
 * log line, never claimed as "applied". */
inline float& requested_gamma() {
    static float g = 1.0f;
    return g;
}

inline bool is_identity(float gamma) {
    /* 1/255 is the resolution a 8-bit framebuffer can carry, so anything
     * closer than that is the same picture. */
    const float d = gamma - 1.0f;
    return d <= 1.0f / 255.0f && d >= -1.0f / 255.0f;
}

/* Returns one of the constants above. */
inline int32_t set_gamma(uint32_t video_out, float gamma) {
    if (!(gamma >= GAMMA_MIN && gamma <= GAMMA_MAX)) {   /* also rejects NaN */
        return ERR_PARAMETER_OUT_OF_RANGE;
    }
    if (video_out != VIDEO_OUT_PRIMARY) {
        return ERR_UNSUPPORTED_VIDEO_OUT;
    }
    requested_gamma() = gamma;
    if (is_identity(gamma)) return 0;
    return ERR_NOT_IMPLEMENTED;
}

}  // namespace avconf
}  // namespace hle

#endif
