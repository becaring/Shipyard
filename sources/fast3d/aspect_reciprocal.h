#pragma once

#include <math.h>

namespace Shipyard {
namespace Fast3D {

// Under round-to-nearest, numerator * (1/denominator) in binary64 rounds to
// the binary32 quotient: its error is below any binary32 quotient's distance
// from a rounding midpoint. Extreme magnitudes and aspects keep the division.
struct AspectReciprocal {
    bool ready;
    double reciprocal;
    explicit AspectReciprocal(float denominator)
        : ready(denominator >= 0.25f && denominator <= 4.0f),
          reciprocal(denominator >= 0.25f && denominator <= 4.0f ? 1.0 / (double)denominator : 0.0) {}
    __forceinline float Divide(float numerator, float denominator) const {
        const float magnitude = fabsf(numerator);
        if (ready && magnitude >= 1.0e-30f && magnitude <= 1.0e30f)
            return (float)((double)numerator * reciprocal);
        return numerator / denominator;
    }
};

} // namespace Fast3D
} // namespace Shipyard
