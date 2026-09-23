#pragma once

#include <float.h>
#include <stdint.h>

namespace Shipyard { namespace Math {
inline double FMin(double a, double b) {
    if (_isnan(a)) return b;
    if (_isnan(b)) return a;
    return a < b ? a : b;
}

inline double FMax(double a, double b) {
    if (_isnan(a)) return b;
    if (_isnan(b)) return a;
    return a > b ? a : b;
}

inline uint8_t ColorByte(float component) {
    const double scaled = component * 255;
    return static_cast<uint8_t>(FMin(FMax(scaled, 0), 255));
}
} }
