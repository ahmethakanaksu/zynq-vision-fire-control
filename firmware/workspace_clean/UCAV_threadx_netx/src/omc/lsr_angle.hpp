/*
 * lsr_angle.hpp -- pixel-to-angle conversion for laser-range requests.
 *
 * Pure function: no global state, no I/O. Given a bounding-box centroid
 * (cx, cy) in [0, FRAME_W) x [0, FRAME_H), produce the (pitch, yaw)
 * angles required to point the gimballed camera at that pixel. Angles
 * are returned in milli-degrees (1 deg = 1000 mdeg) so the wire format
 * stays integer-only -- no float on the link.
 *
 * Implementation note: libm is not linked in this bare-metal Vitis
 * project, so std::tan / std::atan are unavailable. tan(half-FOV) is a
 * compile-time constant baked into the formula, and atan is computed as
 * a 5-term Taylor series. Across the input range we actually see
 * (|nx * tan(hfov/2)| <= 0.577) the absolute error stays below
 * ~6e-5 rad (~3.5 mdeg), well under the milli-degree quantization step.
 */

#ifndef OMC_LSR_ANGLE_HPP_
#define OMC_LSR_ANGLE_HPP_

namespace omc {

/* Compute pitch (vertical) and yaw (horizontal) angles in millidegrees
 * from a bounding-box centroid pixel (cx, cy).
 *
 *   - pitch_mdeg includes the camera's fixed downward tilt
 *     (CAMERA_BASE_PITCH_DOWN_DEG) minus the per-pixel pitch offset.
 *   - yaw_mdeg is positive to the right of the image center.
 */
void compute_lsr_angles_mdeg(int cx, int cy, int& pitch_mdeg, int& yaw_mdeg);

} /* namespace omc */

#endif /* OMC_LSR_ANGLE_HPP_ */
