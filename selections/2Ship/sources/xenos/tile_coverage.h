#pragma once

#include <stddef.h>
#include <float.h>

namespace Fast { namespace ShipyardXenos {

enum { TileCoverageMaximumVertices = 4096 };

// Profiling classifier only. The vertex shader passes the first float4 through
// as clip position. A convex triangle whose vertices all lie strictly below
// this homogeneous horizontal plane cannot touch the preceding tile.
inline bool BatchInsideSecondTile(const float* vertices, size_t vertexCount, size_t stride,
                                  unsigned viewportY, unsigned viewportHeight, unsigned splitY) {
    const double guardPixels = 2.0;
    if (!vertices || !vertexCount || vertexCount > TileCoverageMaximumVertices || stride < 4 ||
        !viewportHeight || splitY < viewportY ||
        splitY + guardPixels >= viewportY + viewportHeight)
        return false;
    const double boundary =
        1.0 - 2.0 * (splitY + guardPixels - viewportY) / viewportHeight;
    for (size_t vertex = 0; vertex < vertexCount; ++vertex) {
        const float* position = vertices + vertex * stride;
        const double y = position[1], w = position[3];
        if (!_finite(position[0]) || !_finite(y) || !_finite(position[2]) ||
            !_finite(w) || w <= 0.0 || !(y < boundary * w))
            return false;
    }
    return true;
}

}} // namespace Fast::ShipyardXenos
