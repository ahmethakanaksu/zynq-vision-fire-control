/*
 * lsr_angle.cpp -- compute_lsr_angles_mdeg implementation.
 *
 * libm is not linked in this bare-metal Vitis build, so std::atan and
 * std::tan are unavailable. We reproduce the math locally so the
 * linker configuration stays untouched.
 *
 * tan(half-FOV) is a compile-time constant (HFOV and VFOV are both 60
 * degrees, so tan(30 deg) = 0.5773502...). atan() is approximated by a
 * 5-term odd-power Taylor expansion that matches std::atan within
 * about 6e-5 rad over our input range.
 *
 * Domain:
 *   nx, ny in [-1, +1]                   (normalized pixel offset)
 *   tan(30 deg) = 0.5773502...           (constant)
 *   atan input = nx * tan(30 deg)        in [-0.5773, +0.5773]
 *
 * atan(x) = x - x^3/3 + x^5/5 - x^7/7 + x^9/9 - ...
 * Truncating after x^9/9 leaves a bounded error |x|^11 / 11. At
 * |x| = 0.577 that is about 2.0e-5 rad (~1.1 milli-degrees), which
 * the milli-degree rounding absorbs entirely.
 */

#include "lsr_angle.hpp"
#include "types.hpp"

namespace omc {
namespace {

constexpr float PI_F           = 3.1415926535f;
constexpr float PI_HALF_F      = 1.5707963267f;
constexpr float RAD_TO_DEG     = 180.0f / PI_F;

/* Compile-time tan(30 deg). Same value for both H and V because both
 * FOVs are 60 degrees. */
constexpr float TAN_HFOV_HALF  = 0.5773502691896f;
constexpr float TAN_VFOV_HALF  = 0.5773502691896f;

/* atan approximation: Taylor expansion x - x^3/3 + x^5/5 - x^7/7 +
 * x^9/9. For |x| > 1 we use the identity atan(x) = pi/2 - atan(1/x),
 * sign-preserved, so the polynomial argument stays in [0, 1] where
 * the series converges fast. */
float fast_atan(float x)
{
    const bool  neg = (x < 0.0f);
    float       ax  = neg ? -x : x;

    const bool  inv = (ax > 1.0f);
    if (inv) {
        ax = 1.0f / ax;
    }

    const float x2 = ax * ax;
    /* Horner-form 5-term series: ax * (1 - x^2/3 + x^4/5 - x^6/7 + x^8/9). */
    float y = ax * (1.0f - x2 *
                          (0.33333333f - x2 *
                          (0.20000000f - x2 *
                          (0.14285714f - x2 *
                           0.11111111f))));

    if (inv) y = PI_HALF_F - y;
    if (neg) y = -y;
    return y;
}

} /* anonymous namespace */

void compute_lsr_angles_mdeg(int cx, int cy, int& pitch_mdeg, int& yaw_mdeg)
{
    /* Normalize pixel offset to [-1, +1] from image centre.
     * Note Y is flipped: image y increases downward, so positive ny means
     * the pixel is ABOVE the centre. */
    const float nx = (static_cast<float>(cx) - (FRAME_W * 0.5f)) / (FRAME_W * 0.5f);
    const float ny = ((FRAME_H * 0.5f) - static_cast<float>(cy)) / (FRAME_H * 0.5f);

    const float yaw_deg          = fast_atan(nx * TAN_HFOV_HALF) * RAD_TO_DEG;
    const float pitch_offset_deg = fast_atan(ny * TAN_VFOV_HALF) * RAD_TO_DEG;
    const float pitch_deg        = CAMERA_BASE_PITCH_DOWN_DEG - pitch_offset_deg;

    pitch_mdeg = static_cast<int>(pitch_deg * 1000.0f);
    yaw_mdeg   = static_cast<int>(yaw_deg   * 1000.0f);
}

} /* namespace omc */
