#pragma once
#include <cstdint>

namespace HDAW {

// 4-point (cubic) Lagrange interpolation at fractional sample position `pos`
// within buffer `buf` of length `len`. pos in [0, len). Clamps at the ends.
// RT-safe: pure arithmetic, noexcept, no allocation.
//
// Uses the closed-form cubic Lagrange coefficients over the 4-sample window
// [i-1, i, i+1, i+2] where i = floor(pos). Buffer reads outside [0, len) are
// clamped to the nearest valid index, which degrades gracefully to fewer
// effective points at the boundaries. The coefficients sum to 1 for any
// fractional offset, so flat regions interpolate flat; at integer offsets the
// basis collapses to a unit pick of the integer sample.
inline float lagrange4(const float* buf, int64_t len, double pos) noexcept
{
    if (len <= 0) return 0.0f;
    if (pos <= 0.0) return buf[0];
    if (pos >= static_cast<double>(len - 1)) return buf[len - 1];

    const int64_t i = static_cast<int64_t>(pos);   // floor, base sample index
    const double   f = pos - static_cast<double>(i); // fractional offset in [0, 1)

    auto at = [&](int64_t k) noexcept -> double {
        if (k < 0) k = 0;
        else if (k >= len) k = len - 1;
        return static_cast<double>(buf[k]);
    };
    const double ym1 = at(i - 1);
    const double y0  = at(i);
    const double y1  = at(i + 1);
    const double y2  = at(i + 2);

    // Closed-form 4-point Lagrange basis for nodes at offsets {-1, 0, +1, +2}
    // evaluated at fractional position f in [0, 1).
    //   L_{-1}(f) = -f * (f - 1) * (f - 2) / 6
    //   L_ 0 (f) =  (f + 1) * (f - 1) * (f - 2) / 2
    //   L_+1(f) = -f * (f + 1) * (f - 2) / 2
    //   L_+2(f) =  f * (f + 1) * (f - 1) / 6
    const double c_m1 = -f * (f - 1.0) * (f - 2.0) / 6.0;
    const double c_0  =  (f + 1.0) * (f - 1.0) * (f - 2.0) / 2.0;
    const double c_1  = -f * (f + 1.0) * (f - 2.0) / 2.0;
    const double c_2  =  f * (f + 1.0) * (f - 1.0) / 6.0;

    const double v = c_m1 * ym1 + c_0 * y0 + c_1 * y1 + c_2 * y2;
    return static_cast<float>(v);
}

} // namespace HDAW
