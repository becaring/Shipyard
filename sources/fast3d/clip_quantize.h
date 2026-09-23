#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Shipyard { namespace Fast3D {

enum { ClipVertexStride = 32, ClipByteOffset = 28 };

void ClipVertices(void* vertices, size_t count, bool includeNearPlane);
void ClipFiniteVertices(void* vertices, size_t count, bool includeNearPlane);
bool QuantizeMatrix(const float input[4][4], float output[4][4]);

inline bool FiniteUnitClipScale(float scale) {
    uint32_t bits;
    memcpy(&bits, &scale, sizeof(bits));
    return (bits & 0x7fffffffu) <= 0x3f800000u;
}

} }
