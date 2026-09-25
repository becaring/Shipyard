#pragma once

namespace Shipyard {
namespace Fast3D {

// Shared UV arithmetic for the two pinned Fast3D interpreters. The caller
// selects the tile, half-texel policy, destination and any later FB transform.
// shift is a tile shift in [0, 15]; dimension is the caller's texture extent.
__forceinline float Coordinate(float raw, int shift, float start, bool half, float dimension) {
    float value = raw / 32.0f;
    if (shift != 0) {
        if (shift <= 10) {
            value /= 1 << shift;
        } else {
            value *= 1 << (16 - shift);
        }
    }
    value -= start / 4.0f;
    if (half)
        value += 0.5f;
    return value / dimension;
}

} // namespace Fast3D
} // namespace Shipyard
