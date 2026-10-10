#pragma once

#include "fast3d/coordinate.h"

namespace Shipyard {
namespace Fast3D {


// The caller retains CoordinateAxis::Apply for generic axes. Select each
// prepared-axis branch once for three rows, preserving every lane's scalar
// operations and intermediate rounding order.
__forceinline void ApplyCoordinate3(const Shipyard::Fast3D::CoordinateAxis& axis,
    float raw0, float raw1, float raw2, bool half, float& out0, float& out1, float& out2) {
    float a = raw0 * 0.03125f;
    float b = raw1 * 0.03125f;
    float c = raw2 * 0.03125f;
    if (axis.scaled) {
        a *= axis.scale;
        b *= axis.scale;
        c *= axis.scale;
    }
    a -= axis.start;
    b -= axis.start;
    c -= axis.start;
    if (half) {
        a += 0.5f;
        b += 0.5f;
        c += 0.5f;
    }
    if (axis.exact) {
        const float reciprocal = axis.reciprocal;
        out0 = a * reciprocal;
        out1 = b * reciprocal;
        out2 = c * reciprocal;
    } else {
        const float dimension = axis.dimension;
        out0 = a / dimension;
        out1 = b / dimension;
        out2 = c / dimension;
    }
}

} // namespace Fast3D
} // namespace Shipyard
