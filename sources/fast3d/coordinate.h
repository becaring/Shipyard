#pragma once

#include <stdint.h>

namespace Shipyard {
namespace Fast3D {

// Shared UV arithmetic. The caller selects the tile and half-texel policy,
// and applies any framebuffer transform after normalization.
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

// Exact powers of two: kCoordinatePower[k] == 2^k, kCoordinateInverse[k] == 2^-k.
__declspec(align(128)) static const float kCoordinatePower[32] = {
    1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f,
    256.0f, 512.0f, 1024.0f, 2048.0f, 4096.0f, 8192.0f, 16384.0f, 32768.0f,
    65536.0f, 131072.0f, 262144.0f, 524288.0f, 1048576.0f, 2097152.0f, 4194304.0f, 8388608.0f,
    16777216.0f, 33554432.0f, 67108864.0f, 134217728.0f,
    268435456.0f, 536870912.0f, 1073741824.0f, 2147483648.0f};
__declspec(align(128)) static const float kCoordinateInverse[32] = {
    1.0f, 0.5f, 0.25f, 0.125f, 0.0625f, 0.03125f, 0.015625f, 0.0078125f,
    1.0f / 256.0f, 1.0f / 512.0f, 1.0f / 1024.0f, 1.0f / 2048.0f,
    1.0f / 4096.0f, 1.0f / 8192.0f, 1.0f / 16384.0f, 1.0f / 32768.0f,
    1.0f / 65536.0f, 1.0f / 131072.0f, 1.0f / 262144.0f, 1.0f / 524288.0f,
    1.0f / 1048576.0f, 1.0f / 2097152.0f, 1.0f / 4194304.0f, 1.0f / 8388608.0f,
    1.0f / 16777216.0f, 1.0f / 33554432.0f, 1.0f / 67108864.0f, 1.0f / 134217728.0f,
    1.0f / 268435456.0f, 1.0f / 536870912.0f, 1.0f / 1073741824.0f, 1.0f / 2147483648.0f};

// Prepare tile-dependent arithmetic once to avoid repeated Xenon integer-to-
// float conversions during packing. Keep raw / 32 and the tile shift separate,
// preserving Coordinate's intermediate rounding and subtraction/addition order.
// Reciprocal multiplication is limited to nonzero power-of-two extents;
// other extents retain division. The half-texel policy is supplied per vertex.
struct CoordinateAxis {
    float scale, start, dimension, reciprocal;
    float rawStart;
    int rawShift;
    bool scaled, exact, generic;

    __forceinline void Prepare(int shift, float startValue, uint32_t size) {
        rawShift = shift;
        rawStart = startValue;
        scaled = shift != 0;
        generic = shift < 0 || shift > 16;
        scale = shift > 0 && shift <= 10 ? kCoordinateInverse[shift]
              : shift > 10 && shift <= 16 ? kCoordinatePower[16 - shift] : 1.0f;
        start = startValue * 0.25f;
        exact = size != 0 && (size & (size - 1)) == 0;
        if (exact) {
            unsigned exponent = 0;
            while ((size >> exponent) != 1)
                ++exponent;
            dimension = kCoordinatePower[exponent];
            reciprocal = kCoordinateInverse[exponent];
        } else {
            dimension = (float)size;
            reciprocal = 0.0f;
        }
    }

    __forceinline float Apply(float raw, bool half) const {
        if (generic)
            return Coordinate(raw, rawShift, rawStart, half, dimension);
        float value = raw * 0.03125f;
        if (scaled)
            value *= scale;
        value -= start;
        if (half)
            value += 0.5f;
        return exact ? value * reciprocal : value / dimension;
    }
};

} // namespace Fast3D
} // namespace Shipyard
