#pragma once
#include <stddef.h>
#include <stdint.h>

namespace Fast { namespace ShipyardXenos {

// Indexed draws retain the packed float layout and triangle order. Validate
// bounds before the SDK reads either stream; no index may address padding.
inline bool ValidIndexedTriangleInput(const float* vertices, size_t floatCount, size_t vertexCount,
                                      const uint16_t* indices, size_t triangles, size_t floatsPerVertex) {
    const size_t maximum = static_cast<size_t>(-1);
    if (!vertices || !indices || !vertexCount || vertexCount > 65536 || !triangles || !floatsPerVertex ||
        floatsPerVertex > 0xffffffffu / sizeof(float) || vertexCount > maximum / floatsPerVertex ||
        floatCount != vertexCount * floatsPerVertex || floatCount > maximum / sizeof(float) ||
        triangles > 0xffffffffu / 3 || triangles > maximum / (3 * sizeof(uint16_t)))
        return false;
    for (size_t i = 0; i < triangles * 3; ++i)
        if (indices[i] >= vertexCount)
            return false;
    return true;
}

}} // namespace Fast::ShipyardXenos
